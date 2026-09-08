"""List and disassemble symbols from an ARM ELF relocatable object.

This is a read-only reverse-engineering helper for the published BEST1700
static libraries.  Relocation names are printed beside the instruction or
data offset that consumes them, which makes library-to-firmware ABI matching
possible even before final link addresses exist.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_LITTLE_ENDIAN, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile


def relocations_by_section(elf: ELFFile) -> dict[int, dict[int, str]]:
    result: dict[int, dict[int, str]] = {}
    for section in elf.iter_sections():
        if section["sh_type"] not in ("SHT_REL", "SHT_RELA"):
            continue
        target_index = int(section["sh_info"])
        symbol_table = elf.get_section(section["sh_link"])
        target = result.setdefault(target_index, {})
        for relocation in section.iter_relocations():
            symbol = symbol_table.get_symbol(relocation["r_info_sym"])
            name = symbol.name or f"section:{symbol['st_shndx']}"
            target[int(relocation["r_offset"])] = name
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("object", type=Path)
    parser.add_argument("--symbol", default=".*", help="regular expression")
    parser.add_argument("--all", action="store_true", help="include non-functions")
    parser.add_argument("--list", action="store_true", help="list symbols only")
    args = parser.parse_args()
    pattern = re.compile(args.symbol)

    with args.object.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            raise SystemExit("ELF object has no .symtab")
        relocations = relocations_by_section(elf)
        decoder = Cs(
            CS_ARCH_ARM,
            CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN | CS_MODE_MCLASS,
        )

        for symbol in symbols.iter_symbols():
            kind = symbol["st_info"]["type"]
            if not pattern.search(symbol.name):
                continue
            if kind != "STT_FUNC" and not args.all:
                continue
            if not isinstance(symbol["st_shndx"], int):
                continue
            section_index = int(symbol["st_shndx"])
            section = elf.get_section(section_index)
            size = int(symbol["st_size"])
            value = int(symbol["st_value"])
            start = (value & ~1) - int(section["sh_addr"])
            print(
                f"\n{symbol.name} type={kind} size=0x{size:x} "
                f"section={section.name}+0x{start:x}"
            )
            if args.list or size == 0:
                continue
            blob = section.data()[start : start + size]
            symbol_relocations = relocations.get(section_index, {})
            if kind == "STT_FUNC":
                for instruction in decoder.disasm(blob, value & ~1):
                    offset = start + instruction.address - (value & ~1)
                    relocation = symbol_relocations.get(offset)
                    suffix = f" ; -> {relocation}" if relocation else ""
                    print(
                        f"  {instruction.address:08x}: "
                        f"{instruction.mnemonic:<8} {instruction.op_str}{suffix}"
                    )
            else:
                print("  " + blob.hex(" "))
                for offset, target in sorted(symbol_relocations.items()):
                    if start <= offset < start + size:
                        print(f"  +0x{offset - start:x} -> {target}")


if __name__ == "__main__":
    main()
