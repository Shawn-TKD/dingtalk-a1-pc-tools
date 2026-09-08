"""Match functions from ARM ELF objects against an already linked firmware image.

The BEST1700 reference libraries are relocatable ELF objects while the A1 image
contains final linked addresses.  This helper masks relocation sites, selects
the longest remaining byte run as an index, and then verifies every unmasked
byte.  It intentionally reports only unique, sufficiently large matches.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable

from elftools.elf.elffile import ELFFile


@dataclass(frozen=True)
class FunctionPattern:
    object_name: str
    symbol_name: str
    data: bytes
    masked: frozenset[int]
    relocation_count: int


def relocation_masks(elf: ELFFile) -> dict[int, set[int]]:
    """Return section-relative byte offsets affected by link relocations.

    The BEST1700 inputs use 32-bit ARM/Thumb relocations for calls, branches,
    MOVW/MOVT pairs and data addresses.  Masking four bytes is conservative for
    all of them.  It may discard two neighboring stable bytes for a 16-bit
    relocation, but it cannot manufacture a match because all other bytes are
    still verified.
    """

    result: dict[int, set[int]] = {}
    for section in elf.iter_sections():
        if section["sh_type"] not in ("SHT_REL", "SHT_RELA"):
            continue
        target = result.setdefault(int(section["sh_info"]), set())
        for relocation in section.iter_relocations():
            offset = int(relocation["r_offset"])
            target.update(range(offset, offset + 4))
    return result


def load_patterns(path: Path) -> Iterable[FunctionPattern]:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            return
        masks = relocation_masks(elf)
        for symbol in symbols.iter_symbols():
            if symbol["st_info"]["type"] != "STT_FUNC":
                continue
            if not isinstance(symbol["st_shndx"], int):
                continue
            size = int(symbol["st_size"])
            if size == 0:
                continue
            section_index = int(symbol["st_shndx"])
            section = elf.get_section(section_index)
            start = (int(symbol["st_value"]) & ~1) - int(section["sh_addr"])
            data = section.data()[start : start + size]
            section_mask = masks.get(section_index, set())
            masked = frozenset(
                offset - start
                for offset in section_mask
                if start <= offset < start + size
            )
            relocation_count = len(
                {
                    (offset - start) // 4
                    for offset in section_mask
                    if start <= offset < start + size
                }
            )
            yield FunctionPattern(
                object_name=path.name,
                symbol_name=symbol.name,
                data=data,
                masked=masked,
                relocation_count=relocation_count,
            )


def stable_runs(pattern: FunctionPattern) -> list[tuple[int, bytes]]:
    runs: list[tuple[int, bytes]] = []
    start: int | None = None
    for offset in range(len(pattern.data) + 1):
        stable = offset < len(pattern.data) and offset not in pattern.masked
        if stable and start is None:
            start = offset
        if not stable and start is not None:
            runs.append((start, pattern.data[start:offset]))
            start = None
    return sorted(runs, key=lambda item: len(item[1]), reverse=True)


def find_matches(image: bytes, pattern: FunctionPattern, min_anchor: int) -> list[int]:
    runs = stable_runs(pattern)
    if not runs or len(runs[0][1]) < min_anchor:
        return []
    anchor_offset, anchor = runs[0]
    matches: list[int] = []
    cursor = 0
    while True:
        found = image.find(anchor, cursor)
        if found < 0:
            break
        candidate = found - anchor_offset
        cursor = found + 1
        if candidate < 0 or candidate + len(pattern.data) > len(image):
            continue
        if all(
            image[candidate + offset] == value
            for offset, value in enumerate(pattern.data)
            if offset not in pattern.masked
        ):
            matches.append(candidate)
    return matches


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("objects", type=Path, nargs="+")
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--min-size", type=int, default=12)
    parser.add_argument("--min-anchor", type=int, default=8)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    image = args.image.read_bytes()
    rows: list[dict[str, str | int]] = []
    for object_path in args.objects:
        for pattern in load_patterns(object_path):
            if len(pattern.data) < args.min_size:
                continue
            matches = find_matches(image, pattern, args.min_anchor)
            if len(matches) != 1:
                continue
            offset = matches[0]
            rows.append(
                {
                    "object": pattern.object_name,
                    "symbol": pattern.symbol_name,
                    "size": len(pattern.data),
                    "relocations": pattern.relocation_count,
                    "image_offset": f"0x{offset:08x}",
                    "address": f"0x{args.base + offset:08x}",
                }
            )

    rows.sort(key=lambda row: int(str(row["address"]), 16))
    fieldnames = [
        "object",
        "symbol",
        "size",
        "relocations",
        "image_offset",
        "address",
    ]
    output: BinaryIO | None = None
    try:
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            output = args.output.open("wb")
            text_stream = __import__("io").TextIOWrapper(output, encoding="utf-8", newline="")
        else:
            text_stream = __import__("sys").stdout
        writer = csv.DictWriter(text_stream, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
        if output:
            text_stream.flush()
    finally:
        if output:
            output.close()


if __name__ == "__main__":
    main()
