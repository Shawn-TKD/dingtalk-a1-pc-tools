"""Loopback-only web application for the owner's A1. No LAN or cloud uploads."""
import argparse
import json
import mimetypes
import secrets
import stat
import tempfile
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import parse_qs, quote, urlsplit

from storage import A1Storage, DeviceError, MAX_UPLOAD, safe_path

ROOT = Path(__file__).resolve().parent


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Paths and device filenames are private; do not log request URLs.
        pass

    def reply(self, status, data, content_type='application/json; charset=utf-8', headers=None):
        if not isinstance(data, bytes): data = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        for name, value in (headers or {}).items(): self.send_header(name, value)
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        if self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
            raise ValueError('需要 JSON 请求')
        size = int(self.headers.get('Content-Length', 0))
        if not 0 < size <= 4096: raise ValueError('请求长度不合法')
        return json.loads(self.rfile.read(size))

    def authenticated(self):
        cookies = SimpleCookie(self.headers.get('Cookie', ''))
        value = cookies.get('a1_session')
        return bool(value and secrets.compare_digest(value.value, self.server.token))

    def dispatch(self, mutation=False):
        host = self.headers.get('Host', '')
        allowed = {f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}'}
        if host not in allowed:
            self.reply(403, {'error': '只允许本机访问'})
            return
        if mutation and self.headers.get('Origin') != 'http://'+host:
            self.reply(403, {'error': '请求来源不匹配，请从本机页面操作'})
            return
        parsed = urlsplit(self.path)
        query = parse_qs(parsed.query)
        if parsed.path == '/api/session' and mutation:
            token = self.body().get('token', '')
            if not isinstance(token, str) or not secrets.compare_digest(token, self.server.token):
                self.reply(401, {'error': '访问链接无效，请使用启动脚本给出的完整链接'})
                return
            self.reply(200, {'ok': True}, headers={'Set-Cookie': 'a1_session='+token+'; HttpOnly; SameSite=Strict; Path=/'})
            return
        if parsed.path.startswith('/api/'):
            if not self.authenticated():
                self.reply(401, {'error': '请用启动脚本给出的完整链接打开一次'})
                return
            self.api(parsed.path, query, mutation)
            return
        if mutation:
            self.reply(404, {'error': '没有这个操作'})
            return
        relative = 'index.html' if parsed.path == '/' else parsed.path.lstrip('/')
        file = (ROOT/'dist'/relative).resolve()
        if not file.is_relative_to((ROOT/'dist').resolve()) or not file.is_file():
            self.reply(404, {'error': '页面文件不存在，请先构建界面'})
            return
        self.reply(200, file.read_bytes(), mimetypes.guess_type(file.name)[0] or 'application/octet-stream')

    def api(self, endpoint, query, mutation):
        device = self.server.device
        if endpoint == '/api/status' and not mutation:
            self.reply(200, device.status())
        elif endpoint == '/api/list' and not mutation:
            self.reply(200, device.list(query.get('path', ['/emmc'])[0]))
        elif endpoint == '/api/connect' and mutation:
            if self.body().get('confirm_mode_change') is not True:
                raise ValueError('请先确认 USB 模式会中断录音和蓝牙连接')
            self.reply(200, device.connect())
        elif endpoint == '/api/mkdir' and mutation:
            self.reply(201, device.mkdir(self.body().get('path')))
        elif endpoint == '/api/upload' and mutation:
            path = safe_path(query.get('path', [''])[0], write=True)
            size = int(self.headers.get('Content-Length', -1))
            if not 0 <= size <= MAX_UPLOAD: raise ValueError('单个文件最多 1 GiB')
            with tempfile.TemporaryFile() as source:
                remaining = size
                while remaining:
                    chunk = self.rfile.read(min(65536, remaining))
                    if not chunk: raise DeviceError('浏览器上传提前中断')
                    source.write(chunk)
                    remaining -= len(chunk)
                source.seek(0)
                result = device.upload(path, source, size)
            self.reply(201, result)
        elif endpoint == '/api/download' and not mutation:
            path = safe_path(query.get('path', [''])[0])
            # Stage a consistent download locally before sending HTTP headers.
            # Firmware shell cannot transport binary safely; use SYNC RECV only.
            with device.lock, tempfile.TemporaryFile() as output:
                metadata = device.stat(path)
                if not stat.S_ISREG(metadata['mode']): raise ValueError('只能下载普通文件，不能下载目录或系统节点')
                received = 0
                for chunk in device.read_chunks(path):
                    output.write(chunk)
                    received += len(chunk)
                if received != metadata['size']: raise DeviceError('文件在传输中变化，请重新下载')
                output.seek(0)
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(received))
                self.send_header('Content-Disposition', "attachment; filename*=UTF-8''"+quote(PurePosixPath(path).name, safe=''))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.end_headers()
                while chunk := output.read(65536): self.wfile.write(chunk)
        else:
            self.reply(404, {'error': '没有这个接口'})

    def handle_request(self, mutation):
        try:
            self.dispatch(mutation)
        except (BrokenPipeError, ConnectionResetError):
            return
        except PermissionError as error: self.reply(403, {'error': str(error)})
        except FileNotFoundError as error: self.reply(404, {'error': str(error)})
        except FileExistsError as error: self.reply(409, {'error': str(error)})
        except (ValueError, KeyError) as error: self.reply(400, {'error': str(error)})
        except Exception as error:
            self.reply(503, {'error': '设备操作未完成：'+str(error)})

    def do_GET(self): self.handle_request(False)
    def do_POST(self): self.handle_request(True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--credentials', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8766)
    args = parser.parse_args()
    local = ROOT/'.local'
    local.mkdir(exist_ok=True)
    token_file = local/'access-token.txt'
    if not token_file.exists(): token_file.write_text(secrets.token_urlsafe(32), encoding='ascii')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.token = token_file.read_text(encoding='ascii').strip()
    server.device = A1Storage(args.adb, args.credentials)
    print(f'A1 file manager: http://127.0.0.1:{args.port}/#token={server.token}', flush=True)
    print('Only /emmc/mindlink accepts uploads. Device/system directories are read-only.', flush=True)
    server.serve_forever()


if __name__ == '__main__': main()
