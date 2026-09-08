"""Owner-scoped A1 USB transport. Browse /emmc; write only /emmc/mindlink."""
import io
import json
import re
import socket
import stat
import struct
import subprocess
import sys
import threading
import time
import uuid
from contextlib import contextmanager
from pathlib import Path, PurePosixPath

LAB = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(LAB))
from a1_usb_readback import adb_service, recv_exact
from a1_usb_session import A1Usb, authenticate

PERSONAL = '/emmc/mindlink'
MAX_UPLOAD = 1024 * 1024 * 1024
RESERVE_BYTES = 1024 * 1024 * 1024


class DeviceError(Exception):
    pass


def safe_path(value, write=False):
    if not isinstance(value, str) or any(ord(c) < 32 for c in value) or '\\' in value:
        raise ValueError('文件路径不合法')
    path = PurePosixPath(value)
    if '..' in path.parts or not path.is_absolute():
        raise ValueError('不允许跨目录访问')
    normalized = str(path)
    root = PERSONAL if write else '/emmc'
    if normalized != root and not normalized.startswith(root + '/'):
        raise PermissionError('上传和新建仅允许在个人文件夹中' if write else '网页仅开放 /emmc 文件区')
    if write and any(not re.fullmatch(r'[\w .()-]+', part) or part.endswith((' ', '.'))
                     for part in path.parts[1:]):
        raise ValueError('文件名请使用中英文、数字、空格、下划线、短横线或括号，末尾不能是点或空格')
    return normalized


class A1Storage:
    def __init__(self, adb, credentials):
        self.adb = str(adb)
        self.identity = json.loads(Path(credentials).read_text(encoding='utf-8-sig'))
        self.serial = self.identity['serial_number']
        self.lock = threading.RLock()
        self.info = {}
        self.capacity = None

    def shell(self, command):
        result = subprocess.run([self.adb, '-s', self.serial, 'shell', command],
                                capture_output=True, timeout=30, creationflags=0x08000000)
        text = result.stdout.decode('utf-8', errors='replace').strip()
        if result.returncode or re.search(r'(^|\n)(nsh:|ERROR:)', text):
            raise DeviceError(text or result.stderr.decode(errors='replace') or 'USB 命令失败')
        return text

    def status(self):
        with self.lock:
            result = subprocess.run([self.adb, 'devices'], capture_output=True, timeout=6,
                                    creationflags=0x08000000)
            lines = result.stdout.decode(errors='replace').splitlines()
            state = next((line.split()[1] for line in lines if line.startswith(self.serial+'\t')), 'missing')
            return {'connected': state == 'device', 'state': state, 'serial': self.serial,
                    'info': self.info, 'capacity': self.capacity, 'personal_root': PERSONAL,
                    'max_upload_bytes': MAX_UPLOAD}

    def connect(self):
        with self.lock:
            client = A1Usb(self.serial)
            try:
                self.info = client.request(400)
                if self.info['sn'] != self.serial:
                    raise DeviceError('当前设备与保存的身份不一致')
                auth = authenticate(client, self.identity)
                if auth.get('code') != 200:
                    raise DeviceError('设备身份认证未通过')
                if self.info.get('work_mode') != 1:
                    response = client.request(402, {'work_mode': 1})
                    if response.get('status') != 'success':
                        raise DeviceError('设备未接受 USB 模式切换')
                    self.info['work_mode'] = 1
            finally:
                client.close()
            for _ in range(20):
                if self.status()['connected']:
                    self.capacity = self.space()
                    return self.status()
                time.sleep(.25)
            raise DeviceError('身份已验证，但 ADB 尚未就绪，请稍后重试')

    def space(self):
        for line in self.shell('df /emmc').splitlines():
            parts = line.split()
            if len(parts) == 5 and parts[-1] == '/emmc':
                block, total, used, free = map(int, parts[:4])
                return {'total': block*total, 'used': block*used, 'free': block*free}
        raise DeviceError('无法读取存储容量')

    @contextmanager
    def sync(self, operation, path):
        with socket.create_connection(('127.0.0.1', 5037), timeout=30) as stream:
            adb_service(stream, 'host:transport:'+self.serial)
            adb_service(stream, 'sync:')
            encoded = path.encode('utf-8')
            stream.sendall(operation+struct.pack('<I', len(encoded))+encoded)
            yield stream

    def _failure(self, stream):
        length = struct.unpack('<I', recv_exact(stream, 4))[0]
        raise DeviceError(recv_exact(stream, length).decode(errors='replace'))

    def stat(self, path):
        path = safe_path(path)
        with self.sync(b'STAT', path) as stream:
            tag = recv_exact(stream, 4)
            if tag == b'FAIL': self._failure(stream)
            if tag != b'STAT': raise DeviceError('设备返回了异常文件信息')
            mode, size, modified = struct.unpack('<III', recv_exact(stream, 12))
            return {'mode': mode, 'size': size, 'modified': modified}

    def list(self, path):
        path = safe_path(path)
        with self.lock:
            metadata = self.stat(path)
            if not metadata['mode']: raise FileNotFoundError('文件夹不存在')
            if not stat.S_ISDIR(metadata['mode']): raise ValueError('该路径不是文件夹')
            entries = []
            with self.sync(b'LIST', path) as stream:
                while True:
                    tag = recv_exact(stream, 4)
                    if tag == b'DONE': break
                    if tag == b'FAIL': self._failure(stream)
                    if tag != b'DENT': raise DeviceError('设备返回了异常目录数据')
                    mode, size, modified, length = struct.unpack('<IIII', recv_exact(stream, 16))
                    name = recv_exact(stream, length).decode('utf-8')
                    if name in ('.', '..') or '/' in name: continue
                    item_path = path.rstrip('/')+'/'+name
                    entries.append({'name': name, 'path': item_path, 'size': size, 'modified': modified,
                                    'type': 'folder' if stat.S_ISDIR(mode) else 'file' if stat.S_ISREG(mode) else 'special',
                                    'writable': item_path == PERSONAL or item_path.startswith(PERSONAL+'/')})
            entries.sort(key=lambda item: (item['type'] != 'folder', item['name'].casefold()))
            return {'path': path, 'entries': entries,
                    'writable': path == PERSONAL or path.startswith(PERSONAL+'/')}

    def read_chunks(self, path):
        path = safe_path(path)
        with self.sync(b'RECV', path) as stream:
            while True:
                tag, length = struct.unpack('<4sI', recv_exact(stream, 8))
                if tag == b'DONE': return
                if tag == b'FAIL': raise DeviceError(recv_exact(stream, length).decode(errors='replace'))
                if tag != b'DATA' or length > 65536: raise DeviceError('设备返回了异常文件块')
                yield recv_exact(stream, length)

    def mkdir(self, path):
        path = safe_path(path, write=True)
        with self.lock:
            if self.stat(path)['mode']: raise FileExistsError('同名目录或文件已经存在')
            parent = str(PurePosixPath(path).parent)
            if not stat.S_ISDIR(self.stat(parent)['mode']): raise FileNotFoundError('上级目录不存在')
            self.shell('mkdir "'+path+'"')
            if not stat.S_ISDIR(self.stat(path)['mode']): raise DeviceError('创建目录未成功')
            return {'path': path}

    def upload(self, path, source, size):
        path = safe_path(path, write=True)
        if path == PERSONAL: raise ValueError('请选择个人目录中的文件名')
        if not 0 <= size <= MAX_UPLOAD: raise ValueError('单个文件最多 1 GiB')
        with self.lock:
            parent = str(PurePosixPath(path).parent)
            entries = self.list(parent)['entries']
            if any(e['name'].casefold() == PurePosixPath(path).name.casefold() for e in entries):
                raise FileExistsError('已有同名文件，为避免覆盖，请先换一个文件名')
            if self.space()['free'] < size+RESERVE_BYTES:
                raise ValueError('空间不足：为录音至少保留 1 GiB')
            temporary = parent+'/.a1-upload-'+uuid.uuid4().hex+'.part'
            started = time.monotonic()
            try:
                with self.sync(b'SEND', temporary+',33206') as stream:
                    remaining = size
                    while remaining:
                        chunk = source.read(min(65536, remaining))
                        if not chunk: raise DeviceError('上传源文件提前结束')
                        stream.sendall(b'DATA'+struct.pack('<I', len(chunk))+chunk)
                        remaining -= len(chunk)
                    stream.sendall(b'DONE'+struct.pack('<I', int(time.time())))
                    tag, length = struct.unpack('<4sI', recv_exact(stream, 8))
                    if tag == b'FAIL': raise DeviceError(recv_exact(stream, length).decode(errors='replace'))
                    if tag != b'OKAY': raise DeviceError('设备未确认文件写入')
                source.seek(0)
                received = 0
                for chunk in self.read_chunks(temporary):
                    if source.read(len(chunk)) != chunk:
                        raise DeviceError('写入后读回内容不一致，未发布文件')
                    received += len(chunk)
                if received != size: raise DeviceError('写入后读回长度不一致')
                # Same-directory rename publishes only a completely verified upload.
                if self.stat(path)['mode']: raise FileExistsError('目标已出现同名文件，未覆盖')
                self.shell('mv "'+temporary+'" "'+path+'"')
                if self.stat(path)['size'] != size: raise DeviceError('文件移动后长度不一致')
            except Exception:
                # Only our fresh temporary file is eligible for cleanup.
                try: self.shell('rm "'+temporary+'"')
                except Exception: pass
                raise
            self.capacity = self.space()
            return {'path': path, 'bytes': size, 'verified': True,
                    'elapsed_seconds': round(time.monotonic()-started, 3)}
