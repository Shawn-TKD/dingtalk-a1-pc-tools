"""Streaming A1 containers -> Ogg/Opus, preserving the compressed audio packets."""
from pathlib import Path
import struct


def _crc_table():
    table = []
    for byte in range(256):
        value = byte << 24
        for _ in range(8):
            value = ((value << 1) ^ (0x04c11db7 if value & 0x80000000 else 0)) & 0xffffffff
        table.append(value)
    return table


_CRC = _crc_table()


def ogg_crc(data):
    value = 0
    for byte in data:
        value = ((value << 8) ^ _CRC[((value >> 24) ^ byte) & 255]) & 0xffffffff
    return value


def make_page(packet, kind, granule, serial, sequence):
    lacing = bytes([255] * (len(packet) // 255) + [len(packet) % 255])
    if len(lacing) > 255:
        raise ValueError("Packet exceeds one Ogg page")
    page = bytearray(b"OggS\0" + bytes([kind]) + struct.pack("<QII", granule, serial & 0xffffffff, sequence)
                     + b"\0" * 4 + bytes([len(lacing)]) + lacing + packet)
    struct.pack_into("<I", page, 22, ogg_crc(page))  # Required Ogg checksum, not a file fingerprint.
    return bytes(page)


def opus_samples(packet):
    """48 kHz granules from RFC 6716 section 3; not a constant 20 ms guess.

    This checks framing duration, not the compressed payload's decodability.
    """
    if not packet:
        raise ValueError("Empty Opus packet")
    config, code = packet[0] >> 3, packet[0] & 3
    samples = ((480, 960, 1920, 2880)[config % 4] if config < 12 else
               (480, 960)[config % 2] if config < 16 else (120, 240, 480, 960)[config % 4])
    if code == 3 and len(packet) < 2:
        raise ValueError("Missing Opus frame count")
    count = 1 if code == 0 else (packet[1] & 63) if code == 3 else 2
    if count == 0 or count * samples > 5760:
        raise ValueError("Invalid Opus duration/frame count")
    return count * samples


class OggWriter:
    """Holds only the last packet so close() can put EOS on the final page."""
    def __init__(self, stream, sample_rate=32000, serial=0xa1d1a1d1):
        self.stream, self.serial = stream, serial
        self.sequence, self.granule, self.packet_count = 2, 0, 0
        self.stereo_packet_count = self.non_20ms_packet_count = 0
        self.pending = None
        head = b"OpusHead" + bytes([1, 1]) + struct.pack("<HIhB", 0, sample_rate, 0, 0)
        vendor = b"dingtalk-a1-sdk/0.1"
        tags = b"OpusTags" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0)
        stream.write(make_page(head, 2, 0, serial, 0))
        stream.write(make_page(tags, 0, 0, serial, 1))

    def write(self, packet):
        samples = opus_samples(packet)
        # Opus can signal stereo-coded packets even with mono output/downmix.
        # One real memo has unusual opening packets; retain and report them.
        self.stereo_packet_count += bool(packet[0] & 4)
        self.non_20ms_packet_count += samples != 960
        if self.pending is not None:
            self.stream.write(make_page(self.pending, 0, self.granule, self.serial, self.sequence))
            self.sequence += 1
        self.pending = packet
        self.granule += samples
        self.packet_count += 1

    def close(self):
        if self.pending is not None:
            self.stream.write(make_page(self.pending, 4, self.granule, self.serial, self.sequence))
            self.pending = None
        self.stream.flush()

    @property
    def duration(self):
        return self.granule / 48000


def validate_ogg(path):
    """Optional full local decode. Requires the 'audio' extra. No cloud upload."""
    import av
    samples = 0
    with av.open(str(path)) as container:
        for frame in container.decode(audio=0):
            samples += frame.samples * 48000 // frame.sample_rate
    return {"decoded_duration_seconds": samples / 48000, "full_decode_verified": True}


def _read(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Truncated audio container")
    return data


def _write_packets(packets, destination, rate, serial, validate):
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_name(target.name + ".part")
    if target.exists() or part.exists():
        raise FileExistsError(target)
    with part.open("xb") as output:
        writer = OggWriter(output, rate, serial)
        for packet in packets:
            writer.write(packet)
        if not writer.packet_count:
            raise ValueError("Empty recording; no playable file produced")
        writer.close()
    result = {"duration_seconds": writer.duration, "packet_count": writer.packet_count,
              "sample_rate": rate, "full_decode_verified": False,
              "stereo_coded_packet_count": writer.stereo_packet_count,
              "non_20ms_packet_count": writer.non_20ms_packet_count}
    if validate:
        result.update(validate_ogg(part))
        if abs(result["decoded_duration_seconds"] - writer.duration) > 0.0001:
            raise ValueError("Ogg timeline and decoded sample count disagree")
    if target.exists():
        raise FileExistsError(target)
    part.rename(target)
    return {"path": str(target.resolve()), **result}


def convert_dtyj(source, destination, *, validate=False):
    source = Path(source)
    with source.open("rb") as stream:
        head = stream.read(4096)
        if len(head) < 12 or head[:4] != b"BABA" or head[8:12] != b"DTYJ":
            raise ValueError("Not a BABA/DTYJ recording")
        if int.from_bytes(head[4:8], "little") != source.stat().st_size - 8:
            raise ValueError("DTYJ declared and actual file lengths differ")
        fmt = head.find(b"fmt ", 12)
        data = head.find(b"data", fmt + 4) if fmt >= 0 else -1
        if fmt < 0 or data < 0 or fmt + 26 > len(head) or data + 12 > len(head):
            raise ValueError("Unrecognized DTYJ fmt/data header layout")
        rate = struct.unpack_from("<I", head, fmt + 12)[0]
        record_size = struct.unpack_from("<H", head, fmt + 24)[0]
        size = struct.unpack_from("<I", head, data + 4)[0]
        if record_size <= 4 or size % record_size or data + 12 + size > source.stat().st_size:
            raise ValueError("Invalid DTYJ fixed-frame area")
        stream.seek(data + 12)
        def packets():
            for _ in range(size // record_size):
                # Prefix is an opaque u32 flag (including 0x1A0), not packet length.
                yield _read(stream, record_size)[4:]
        return _write_packets(packets(), destination, rate, 0xa1d1a1d1, validate)


def export_memos(source, directory, *, validate=False):
    """Export the locally downloaded /emmc/audio/00000000000000 aggregate.

    Preserves the source. Invalid entries are reported, never silently skipped as success.
    """
    result = []
    with Path(source).open("rb") as stream:
        head = _read(stream, 8)
        count = int.from_bytes(head[:4], "big")
        marker, khz, _, packet_size = head[4:]
        if marker != 0x5a or not packet_size:
            raise ValueError("Unsupported memo aggregate header")
        for index in range(count):
            entry = _read(stream, 12)
            if entry[0] != 0x5a:
                raise ValueError(f"Invalid memo {index + 1} header")
            fid = int.from_bytes(entry[1:7], "big")
            seconds = int.from_bytes(entry[7:9], "big")
            size = int.from_bytes(entry[9:12], "big")
            if size % packet_size:
                raise ValueError("Memo length is not a whole number of packets")
            end = stream.tell() + size
            if end > Path(source).stat().st_size:
                raise ValueError("Truncated memo payload")
            metadata = {"index": index + 1, "fid": fid, "declared_duration_seconds": seconds}
            path = Path(directory) / f"memo-{index + 1:03d}-{fid}.ogg"
            try:
                packets = (_read(stream, packet_size) for _ in range(size // packet_size))
                metadata.update(_write_packets(packets, path, khz * 1000, index + 1, validate))
            except ValueError as error:
                metadata["error"] = str(error)
            finally:
                stream.seek(end)
            result.append(metadata)
        if stream.read(1):
            raise ValueError("Unparsed memo aggregate trailing bytes")
    return result
