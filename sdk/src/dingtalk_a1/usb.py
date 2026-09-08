"""HID owner authentication + ADB SYNC. No flashing/binding/reset implementation."""
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
import json
import os
import re
import socket
import stat as statmod
import struct
import subprocess
import threading
import time
import uuid

from .protocol import DeviceError, HID_HEADER, encode, accepted

VID, PID, REPORT_ID, REPORT_BYTES = 0x17ef, 0x0101, 1, 127
PERSONAL_ROOT = "/emmc/mindlink"


class A1USB:
    def __init__(self, identity):
        self.identity = identity
        self.device = None
        self.sequence = 10

    @staticmethod
    def enumerate():
        import hid
        return [d for d in hid.enumerate(VID, PID) if d["usage_page"] == 0xff00 and d["usage"] == 1]

    def __enter__(self):
        import hid
        if not self.identity.serial_number:
            raise ValueError("USB requires serial_number in the identity file")
        found = [d for d in self.enumerate() if d["serial_number"] == self.identity.serial_number]
        if len(found) != 1:
            raise DeviceError(f"Expected the selected A1 HID interface; found {len(found)}")
        self.device = hid.device()
        self.device.open_path(found[0]["path"])
        return self

    def __exit__(self, *_):
        self.device.close()
        self.device = None

    def _request(self, command, body=None, timeout=5):
        if self.device is None:
            raise DeviceError("Open USB using a with block first")
        self.sequence = (self.sequence + 1) & 255
        packet = encode(command, self.sequence, body, header=HID_HEADER)
        if len(packet) > 1024:
            raise ValueError("HID packet exceeds the known 1024-byte limit")
        for offset in range(0, len(packet), REPORT_BYTES):
            report = bytes([REPORT_ID]) + packet[offset:offset + REPORT_BYTES].ljust(REPORT_BYTES, b"\0")
            if self.device.write(report) != len(report):
                raise DeviceError("Short HID write")
        response, total = bytearray(), None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = bytes(self.device.read(REPORT_BYTES + 1, 300))
            if not data:
                continue
            if data[0] != REPORT_ID:
                raise DeviceError("Unexpected HID Report ID")
            response.extend(data[1:])
            if total is None and len(response) >= 8:
                kind, cmd, seq, size = HID_HEADER.unpack_from(response)
                if (kind, cmd, seq) != (0x31, command, self.sequence) or size > 1016:
                    raise DeviceError("Unexpected HID response header")
                total = 8 + size
            if total is not None and len(response) >= total:
                return json.loads(response[8:total].decode("utf-8"))
        raise TimeoutError(f"HID command {command} timed out")

    def info(self):
        return self._request(400)

    def authenticate(self):
        if self.info().get("sn") != self.identity.serial_number:
            raise DeviceError("Device SN does not match the supplied identity")
        challenge = accepted(self._request(403, {
            "corpId": self.identity.corp_id, "did": self.identity.did}))
        return accepted(self._request(401, {
            "did": self.identity.did, "payload": self.identity.token(challenge["random"])}))

    def enter_adb(self):
        """STOPS normal recording and BLE. Exiting the context does NOT restore them."""
        self.authenticate()
        if self.info().get("work_mode") == 1:
            return {"status": "success", "work_mode": 1, "already_enabled": True}
        result = self._request(402, {"work_mode": 1})
        if result.get("status") != "success":
            raise DeviceError("A1 did not accept the ADB mode switch")
        return result


def emmc_path(value, *, write=False):
    path = PurePosixPath(value)
    if not path.is_absolute() or ".." in path.parts or "\\" in value or any(ord(c) < 32 for c in value):
        raise ValueError("Use an absolute /emmc path without traversal")
    root = PERSONAL_ROOT if write else "/emmc"
    result = str(path)
    if result != root and not result.startswith(root + "/"):
        raise PermissionError(f"This operation is limited to {root}")
    if write and any(not re.fullmatch(r"[\w .()-]+", p) or p.endswith((" ", ".")) for p in path.parts[1:]):
        raise ValueError("Use letters, numbers, spaces, _, -, parentheses; no shell metacharacters")
    return result


def recv_exact(stream, size):
    output = bytearray()
    while len(output) < size:
        part = stream.recv(size - len(output))
        if not part:
            raise DeviceError("ADB closed during a transfer")
        output.extend(part)
    return bytes(output)


def adb_service(stream, name):
    payload = name.encode("utf-8")
    stream.sendall(f"{len(payload):04x}".encode("ascii") + payload)
    tag = recv_exact(stream, 4)
    if tag == b"FAIL":
        size = int(recv_exact(stream, 4), 16)
        raise DeviceError(recv_exact(stream, size).decode(errors="replace"))
    if tag != b"OKAY":
        raise DeviceError("Unexpected ADB service response")


class USBStorage:
    """Use only AFTER explicit HID enter_adb(). Each instance serializes USB work.

    Reads /emmc; writes/deletes only /emmc/mindlink. 'mindlink' is our directory,
    not a partition. No arbitrary shell, /dev access or firmware writes in this API.
    """
    def __init__(self, serial, *, adb="adb", port=5037, max_upload_bytes=1024**3,
                 reserve_bytes=1024**3, timeout=30):
        if not serial:
            raise ValueError("Choose one device serial")
        self.serial, self.adb, self.port = serial, str(adb), port
        self.max_upload_bytes, self.reserve_bytes = max_upload_bytes, reserve_bytes
        self.timeout = timeout
        self._lock = threading.RLock()

    def _run(self, *args):
        result = subprocess.run([self.adb, "-P", str(self.port), *args], capture_output=True,
                                timeout=self.timeout, creationflags=0x08000000 if os.name == "nt" else 0)
        output = result.stdout.decode("utf-8", errors="replace").strip()
        if result.returncode or re.search(r"(^|\n)(nsh:|ERROR:)", output):
            raise DeviceError(output or result.stderr.decode(errors="replace"))
        return output

    def start_server(self):
        self._run("start-server")

    def _shell(self, command):
        return self._run("-s", self.serial, "shell", command)

    def space(self):
        with self._lock:
            for line in self._shell("df /emmc").splitlines():
                fields = line.split()
                if len(fields) == 5 and fields[-1] == "/emmc":
                    block, total, used, free = map(int, fields[:4])
                    return {"total_bytes": block * total, "used_bytes": block * used,
                            "free_bytes": block * free}
        raise DeviceError("Unrecognized NuttX df output")

    @contextmanager
    def _sync(self, command, path):
        with socket.create_connection(("127.0.0.1", self.port), timeout=self.timeout) as stream:
            adb_service(stream, "host:transport:" + self.serial)
            adb_service(stream, "sync:")
            encoded = path.encode("utf-8")
            stream.sendall(command + struct.pack("<I", len(encoded)) + encoded)
            yield stream

    @staticmethod
    def _fail(stream):
        size = struct.unpack("<I", recv_exact(stream, 4))[0]
        raise DeviceError(recv_exact(stream, size).decode(errors="replace"))

    def stat(self, path):
        with self._lock, self._sync(b"STAT", emmc_path(path)) as stream:
            tag = recv_exact(stream, 4)
            if tag == b"FAIL":
                self._fail(stream)
            if tag != b"STAT":
                raise DeviceError("Unexpected ADB STAT response")
            mode, size, modified = struct.unpack("<III", recv_exact(stream, 12))
            return {"mode": mode, "size": size, "modified": modified}

    def list(self, path="/emmc"):
        path = emmc_path(path)
        with self._lock:
            if not statmod.S_ISDIR(self.stat(path)["mode"]):
                raise NotADirectoryError(path)
            result = []
            with self._sync(b"LIST", path) as stream:
                while True:
                    tag = recv_exact(stream, 4)
                    if tag == b"DONE":
                        return result
                    if tag == b"FAIL":
                        self._fail(stream)
                    if tag != b"DENT":
                        raise DeviceError("Unexpected ADB directory entry")
                    mode, size, modified, length = struct.unpack("<IIII", recv_exact(stream, 16))
                    name = recv_exact(stream, length).decode("utf-8")
                    if name in (".", "..") or "/" in name:
                        continue
                    result.append({"name": name, "path": path.rstrip("/") + "/" + name,
                                   "size": size, "modified": modified,
                                   "type": "directory" if statmod.S_ISDIR(mode) else "file" if statmod.S_ISREG(mode) else "special"})

    def read_chunks(self, path):
        with self._lock, self._sync(b"RECV", emmc_path(path)) as stream:
            while True:
                tag, size = struct.unpack("<4sI", recv_exact(stream, 8))
                if tag == b"DONE":
                    return
                if tag == b"FAIL":
                    raise DeviceError(recv_exact(stream, size).decode(errors="replace"))
                if tag != b"DATA" or size > 65536:
                    raise DeviceError("Invalid ADB data block")
                yield recv_exact(stream, size)

    def download(self, path, destination, *, on_progress=None):
        target = Path(destination)
        target.parent.mkdir(parents=True, exist_ok=True)
        part = target.with_name(target.name + ".part")
        if target.exists() or part.exists():
            raise FileExistsError(target)
        with self._lock:
            metadata = self.stat(path)
            if not statmod.S_ISREG(metadata["mode"]):
                raise ValueError("Download only regular /emmc files")
            received = 0
            with part.open("xb") as output:
                for chunk in self.read_chunks(path):
                    output.write(chunk)
                    received += len(chunk)
                    if on_progress:
                        on_progress(received, metadata["size"])
            if received != metadata["size"]:
                raise DeviceError("File changed or download incomplete; .part retained")
            if target.exists():
                raise FileExistsError(target)
            part.rename(target)
        return {"path": str(target.resolve()), "bytes": received}

    def mkdir(self, path=PERSONAL_ROOT):
        path = emmc_path(path, write=True)
        with self._lock:
            if self.stat(path)["mode"]:
                raise FileExistsError(path)
            if not statmod.S_ISDIR(self.stat(str(PurePosixPath(path).parent))["mode"]):
                raise NotADirectoryError("Parent directory is missing")
            self._shell('mkdir "' + path + '"')
            if not statmod.S_ISDIR(self.stat(path)["mode"]):
                raise DeviceError("mkdir did not create the directory")

    def upload(self, source, path, *, verify=False, on_progress=None):
        """Non-overwriting streaming upload. Optional byte-for-byte readback; no hash.

        Default 1 GiB cap/reserve are host policy, NOT firmware partition limits.
        """
        source, path = Path(source), emmc_path(path, write=True)
        if path == PERSONAL_ROOT:
            raise ValueError("Select a file below the personal directory")
        size = source.stat().st_size
        if not 0 <= size <= self.max_upload_bytes:
            raise ValueError("File exceeds configured max_upload_bytes")
        parent = str(PurePosixPath(path).parent)
        with self._lock:
            if any(e["name"].casefold() == PurePosixPath(path).name.casefold() for e in self.list(parent)):
                raise FileExistsError(path)
            if self.space()["free_bytes"] < size + self.reserve_bytes:
                raise DeviceError("Not enough space while preserving the recording reserve")
            temporary = parent + "/.a1-sdk-" + uuid.uuid4().hex + ".part"
            started = time.monotonic()
            try:
                with source.open("rb") as input_file, self._sync(b"SEND", temporary + ",33206") as stream:
                    sent = 0
                    while sent < size:
                        chunk = input_file.read(min(65536, size - sent))
                        if not chunk:
                            raise DeviceError("Source file shrank during upload")
                        stream.sendall(b"DATA" + struct.pack("<I", len(chunk)) + chunk)
                        sent += len(chunk)
                        if on_progress:
                            on_progress(sent, size)
                    stream.sendall(b"DONE" + struct.pack("<I", int(time.time())))
                    tag, length = struct.unpack("<4sI", recv_exact(stream, 8))
                    if tag == b"FAIL":
                        raise DeviceError(recv_exact(stream, length).decode(errors="replace"))
                    if tag != b"OKAY":
                        raise DeviceError("A1 did not acknowledge the upload")
                if self.stat(temporary)["size"] != size:
                    raise DeviceError("Uploaded file length mismatch")
                if verify:
                    received = 0
                    with source.open("rb") as check:
                        for chunk in self.read_chunks(temporary):
                            if check.read(len(chunk)) != chunk:
                                raise DeviceError("Readback differs from source")
                            received += len(chunk)
                    if received != size:
                        raise DeviceError("Readback is truncated")
                if self.stat(path)["mode"]:
                    raise FileExistsError(path)
                self._shell('mv "' + temporary + '" "' + path + '"')
            except BaseException:
                # Only this invocation's newly-created temporary path is eligible.
                try:
                    self._shell('rm "' + temporary + '"')
                except (OSError, DeviceError, subprocess.SubprocessError):
                    pass  # Unplugging can leave this .part; list() exposes it for cleanup.
                raise
        return {"path": path, "bytes": size, "readback_verified": verify,
                "elapsed_seconds": round(time.monotonic() - started, 3)}

    def delete_file(self, path):
        """Delete ONE regular personal file, never a folder or device recording."""
        path = emmc_path(path, write=True)
        with self._lock:
            if not statmod.S_ISREG(self.stat(path)["mode"]):
                raise ValueError("Select a regular personal file")
            self._shell('rm "' + path + '"')
            if self.stat(path)["mode"]:
                raise DeviceError("Deletion not confirmed")

    def motor_test(self):
        """USB-only fixed factory vibration sequence; NOT vibrate(duration) or BLE feedback.

        The owner previously felt this sequence. Do not invoke on connect automatically.
        """
        with self._lock:
            return self._shell("lnv_motor_test")
