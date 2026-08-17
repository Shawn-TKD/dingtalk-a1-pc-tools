"""Convert the fixed-frame DTYJ Opus container used by DingTalk A1 to Ogg."""

import argparse
import hashlib
from pathlib import Path
import struct


OGG_CRC_POLYNOMIAL = 0x04C11DB7


def ogg_crc(data: bytes) -> int:
    value = 0
    for byte in data:
        value ^= byte << 24
        for _ in range(8):
            value = ((value << 1) ^ OGG_CRC_POLYNOMIAL) if value & 0x80000000 else value << 1
            value &= 0xFFFFFFFF
    return value


def make_page(packet: bytes, header_type: int, granule: int, serial: int, sequence: int) -> bytes:
    segments = []
    remaining = len(packet)
    while remaining >= 255:
        segments.append(255)
        remaining -= 255
    segments.append(remaining)
    header = (
        b"OggS\x00"
        + bytes([header_type])
        + struct.pack("<QII", granule, serial, sequence)
        + b"\x00\x00\x00\x00"
        + bytes([len(segments)])
        + bytes(segments)
    )
    page = bytearray(header + packet)
    page[22:26] = struct.pack("<I", ogg_crc(page))
    return bytes(page)


def extract_packets(container: bytes) -> tuple[list[bytes], int, str]:
    if container[:4] != b"BABA" or container[8:12] != b"DTYJ":
        raise ValueError("not a DingTalk DTYJ/BABA container")
    declared_total = struct.unpack_from("<I", container, 4)[0]
    if declared_total != len(container) - 8:
        raise ValueError(f"container size mismatch: declared={declared_total} actual={len(container)-8}")

    fmt_offset = container.find(b"fmt ", 12)
    data_offset = container.find(b"data", fmt_offset + 4)
    if fmt_offset < 0 or data_offset < 0:
        raise ValueError("fmt/data chunks not found")
    version = container[16:20].decode("ascii", "replace")
    sample_rate = struct.unpack_from("<I", container, fmt_offset + 12)[0]
    record_size = struct.unpack_from("<H", container, fmt_offset + 24)[0]
    data_size = struct.unpack_from("<I", container, data_offset + 4)[0]
    records_offset = data_offset + 12
    records = container[records_offset : records_offset + data_size]
    if record_size <= 4 or len(records) != data_size or data_size % record_size:
        raise ValueError(
            f"invalid fixed-frame area: size={data_size} record_size={record_size} actual={len(records)}"
        )

    packets = []
    for offset in range(0, len(records), record_size):
        record = records[offset : offset + record_size]
        prefix = record[:4]
        # The first byte is a frame-flag field. Independent samples contain
        # 0x00, 0x20 and 0xC0 on otherwise-normal Opus records, so the upper
        # three bits are accepted as flags. The lower five bits and remaining
        # three reserved bytes must stay zero; this still rejects shifted or
        # corrupt fixed-frame data instead of silently producing broken Ogg.
        if prefix[1:] != b"\x00\x00\x00" or prefix[0] & 0x1F:
            raise ValueError(f"unexpected record prefix at frame {len(packets)}: {prefix.hex()}")
        packet = record[4:]
        if not packet:
            raise ValueError(f"empty Opus packet at frame {len(packets)}")
        packets.append(packet)
    return packets, sample_rate, version


def convert(source: Path, destination: Path, serial: int) -> None:
    if destination.exists():
        raise FileExistsError(f"refusing to overwrite {destination}")
    packets, sample_rate, version = extract_packets(source.read_bytes())

    opus_head = (
        b"OpusHead"
        + bytes([1, 1])
        + struct.pack("<H", 0)
        + struct.pack("<I", sample_rate)
        + struct.pack("<h", 0)
        + b"\x00"
    )
    vendor = f"dtyj_to_ogg/{version}".encode("utf-8")
    opus_tags = b"OpusTags" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0)

    pages = [
        make_page(opus_head, header_type=0x02, granule=0, serial=serial, sequence=0),
        make_page(opus_tags, header_type=0x00, granule=0, serial=serial, sequence=1),
    ]
    granule = 0
    for index, packet in enumerate(packets):
        granule += 960  # A1's 0x4B TOC packets are one 20 ms Opus frame at 48 kHz.
        pages.append(
            make_page(
                packet,
                header_type=0x04 if index == len(packets) - 1 else 0x00,
                granule=granule,
                serial=serial,
                sequence=index + 2,
            )
        )

    content = b"".join(pages)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(content)
    print(
        f"Converted {len(packets)} Opus packets ({len(packets) * 0.02:.2f}s) "
        f"at input rate {sample_rate} Hz to {destination} ({len(content)} bytes); "
        f"sha256={hashlib.sha256(content).hexdigest()}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--serial", type=lambda value: int(value, 0), default=0xA1D1A1D1)
    args = parser.parse_args()
    convert(args.source, args.destination, args.serial)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
