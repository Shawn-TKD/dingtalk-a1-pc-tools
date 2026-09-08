"""Wire facts recovered from the existing owner-tested clients and V1.6.88."""
from dataclasses import dataclass
import json
import struct

SERVICE = "0000fe3c-0000-1000-8000-00805f9b34fb"
WRITE = "0000fe1c-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fe1b-0000-1000-8000-00805f9b34fb"
BLE_HEADER = struct.Struct(">BHBI")
HID_HEADER = struct.Struct("<BHBI")  # USB is little-endian, unlike BLE!


class DeviceError(RuntimeError):
    pass


@dataclass(frozen=True)
class Frame:
    kind: int
    command: int
    sequence: int
    payload: bytes

    def json(self) -> dict:
        value = json.loads(self.payload.decode("utf-8"))
        if not isinstance(value, dict):
            raise ValueError("Expected JSON object")
        return value


def encode(command, sequence, body=None, kind=0x13, *, header=BLE_HEADER):
    payload = (b"" if body is None else body if isinstance(body, bytes) else
               json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    if len(payload) > 39992:
        raise ValueError("Request exceeds the recovered firmware receive buffer")
    return header.pack(kind, command, sequence & 255, len(payload)) + payload


class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 8:
            if self.buffer[0] not in (0x13, 0x14, 0x31):
                del self.buffer[0]
                continue
            kind, cmd, seq, size = BLE_HEADER.unpack_from(self.buffer)
            if size > 60000:
                self.buffer.clear()
                raise ValueError("A1 frame length exceeds the recovered transmit limit")
            if len(self.buffer) < 8 + size:
                break
            frames.append(Frame(kind, cmd, seq, bytes(self.buffer[8:8 + size])))
            del self.buffer[:8 + size]
        return frames


def accepted(body, codes=(200,)):
    if body.get("code") not in codes:
        raise DeviceError(f"Device returned code={body.get('code')}")
    return body


def file_index_page(payload):
    if len(payload) < 4:
        raise ValueError("Truncated file index")
    code, count = struct.unpack_from(">HH", payload)
    accepted({"code": code})
    end = 4 + count * 8
    if len(payload) < end:
        raise ValueError("Invalid file index length/tail")
    tail = payload[end:]
    truncated = False
    tail_present = False
    if (len(tail) == 8 and tail[:3] == b"\0\0\0" and
            tail[3] in (0, 1) and tail[4:] == b"ZZZZ"):
        tail_present, truncated = True, bool(tail[3])
    elif any(b not in (0, 0x5a) for b in tail):
        # Older peers/captures may omit the stock trailer or expose only
        # transport padding. They cannot carry a reliable truncation flag.
        raise ValueError("Invalid file index length/tail")
    result = []
    for offset in range(4, end, 8):
        flag, fid, raw = struct.unpack_from(">HIH", payload, offset)
        # A 16-bit field is not a trustworthy duration for recordings > 18 hours.
        result.append({"fid": fid, "flag": flag, "duration_hint_seconds": raw,
                       "status_or_duration_raw": raw})
    return {"entries": result, "truncated": truncated, "tail_present": tail_present}


def file_index(payload):
    """Backward-compatible list-only view; use file_index_page for pagination metadata."""
    return file_index_page(payload)["entries"]


def crc32_bzip2(data):
    crc = 0xffffffff
    for value in data:
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04c11db7) & 0xffffffff if crc & 0x80000000 else (crc << 1) & 0xffffffff
    return crc ^ 0xffffffff


def raw_file_block(payload):
    if len(payload) < 12:
        raise ValueError("Truncated raw-file block")
    sequence, length = struct.unpack_from(">II", payload)
    if length > 8000 or len(payload) != length + 12:
        raise ValueError("Invalid raw-file block length")
    data = payload[8:8 + length]
    expected_crc = struct.unpack_from(">I", payload, 8 + length)[0]
    if crc32_bzip2(data) != expected_crc:
        raise ValueError("Raw-file block CRC mismatch")
    return sequence, data


def find_size(body):
    if isinstance(body, dict):
        for key in ("size", "file_size", "fileSize", "total_size", "totalSize"):
            value = body.get(key)
            if isinstance(value, (int, str)) and str(value).isdigit():
                return int(value)
        children = body.values()
    elif isinstance(body, list):
        children = body
    else:
        return None
    return next((size for child in children if (size := find_size(child)) is not None), None)


def file_block(payload):
    if len(payload) < 16:
        raise ValueError("Truncated file block header")
    _, fid, _, number, length = struct.unpack_from(">HIHII", payload)
    if length > len(payload) - 16:
        raise ValueError("Truncated file block data")
    data = payload[16:16 + length]
    tail = payload[16 + length:]
    if tail:
        if len(tail) != 8:
            raise ValueError("Invalid file block trailer")
        _, expected_crc = struct.unpack(">II", tail)
        if crc32_bzip2(data) != expected_crc:
            raise ValueError("File block CRC mismatch")
    return fid, number, data


def audio_push(payload):
    if len(payload) < 28:
        raise ValueError("Truncated live audio header")
    fid = int.from_bytes(payload[4:8], "big")
    seq, size = struct.unpack_from(">II", payload, 16)
    if size > len(payload) - 28 or size % 84:
        raise ValueError("Unsupported live Opus block; expected 84-byte units")
    return fid, seq, [payload[i:i + 84] for i in range(28, 28 + size, 84)]


def event_body(frame):
    body = frame.json()
    # Android native callbacks can wrap the same wire JSON in `body`.
    return body["body"] if isinstance(body.get("body"), dict) else body


def ut_records(payload):
    """0x000C is batched telemetry, NOT an unconditional button/marker event."""
    if len(payload) < 10 or payload[:2] != b"ZZ" or payload[-2:] != b"ZZ":
        raise ValueError("Unrecognized UT report")
    size = int.from_bytes(payload[2:4], "big")
    if size % 20 or len(payload) != size + 10 or int.from_bytes(payload[-6:-2], "big") != size // 20:
        raise ValueError("UT record boundaries disagree")
    for offset in range(4, 4 + size, 20):
        seq = int.from_bytes(payload[offset:offset + 2], "big")
        ts = int.from_bytes(payload[offset + 4:offset + 8], "big")
        cls, code = payload[offset + 8], payload[offset + 11]
        arg0, arg1 = struct.unpack_from(">II", payload, offset + 12)
        yield {"sequence": seq, "device_timestamp": ts, "class": cls, "code": code,
               "argument0": arg0, "argument1": arg1,
               "is_marker": cls == 0x0b and code == 1 and arg0 == 0}
