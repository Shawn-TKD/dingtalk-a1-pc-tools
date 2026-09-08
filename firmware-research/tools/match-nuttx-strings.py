"""Match literal strings in an A1 image against an Apache NuttX source tree."""

from __future__ import annotations

import argparse
import ast
from collections import defaultdict
from pathlib import Path
import re
import warnings


PRINTABLE = set(range(0x20, 0x7F))
SOURCE_SUFFIXES = {".c", ".h", ".s", ".S", ".inc"}
LITERAL = re.compile(r'(?<![A-Za-z0-9_])(?:u8|u|U|L)?"(?:\\.|[^"\\])*"')


def image_strings(path: Path, minimum: int) -> dict[str, list[int]]:
    data = path.read_bytes()
    result: dict[str, list[int]] = defaultdict(list)
    start = 0
    while start < len(data):
        if data[start] not in PRINTABLE:
            start += 1
            continue
        end = start + 1
        while end < len(data) and data[end] in PRINTABLE:
            end += 1
        if end - start >= minimum:
            result[data[start:end].decode("ascii")].append(start)
        start = end + 1
    return result


def decode_literal(token: str) -> str | None:
    quote = token.find('"')
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", SyntaxWarning)
            value = ast.literal_eval(token[quote:])
    except (SyntaxError, ValueError):
        return None
    if not isinstance(value, str):
        return None
    # Binary string extraction stops at control characters such as CR/LF.
    return re.split(r"[\x00-\x1f]", value, maxsplit=1)[0]


def source_literals(root: Path, minimum: int) -> dict[str, set[str]]:
    result: dict[str, set[str]] = defaultdict(set)
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        try:
            source = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        relative = path.relative_to(root).as_posix()
        for match in LITERAL.finditer(source):
            value = decode_literal(match.group(0))
            if value is not None and len(value) >= minimum:
                result[value].add(relative)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("nuttx", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--minimum", type=int, default=8)
    args = parser.parse_args()

    binary = image_strings(args.image, args.minimum)
    sources = source_literals(args.nuttx, args.minimum)
    matches = sorted(set(binary) & set(sources))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        stream.write("offsets\tliteral\tsources\n")
        for value in matches:
            offsets = ",".join(f"0x{offset:08x}" for offset in binary[value])
            escaped = value.replace("\\", "\\\\").replace("\t", "\\t")
            stream.write(f"{offsets}\t{escaped}\t{'|'.join(sorted(sources[value]))}\n")

    print(f"image strings: {len(binary)}")
    print(f"NuttX literals: {len(sources)}")
    print(f"exact matches: {len(matches)}")
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
