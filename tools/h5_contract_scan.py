"""Inventory known DingTalk A1 H5 bridge contracts without copying bundle source."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import tarfile
from typing import Iterable


CONTRACT_TERMS = (
    "internal.dinger.recordOperation",
    "internal.dinger.realStreamOperation",
    "internal.dinger.getDeviceStatus",
    "internal.dinger.getFileList",
    "internal.dinger.fileOperation",
    "internal.dinger.dingerFileOperation",
    "internal.dinger.sendBleCommand",
    "internal.dinger.audio",
    "internal.dinger.otaOperation",
    "fileOperation",
    "dingerFileOperation",
    "otaOperation",
    "internal.channel.subscribe",
    "internal.channel.publish",
    "internal.request.lwp",
    "operationName",
    "operationParam",
    "needAsr",
    "sourceLanguage",
    "targetLanguages",
    "languageHints",
    "simultaneous_interpretation",
    "real_time_translation",
    "transcription",
    "upload_stream",
)
URL_RE = re.compile(r"https?://([A-Za-z0-9.-]+)(?::\d+)?", re.IGNORECASE)


def iter_sources(path: Path) -> Iterable[tuple[str, bytes]]:
    if path.is_dir():
        for child in sorted(path.rglob("*.js")):
            if child.is_file():
                yield child.relative_to(path).as_posix(), child.read_bytes()
        return
    if tarfile.is_tarfile(path):
        with tarfile.open(path, "r:*") as archive:
            for member in archive.getmembers():
                if member.isfile() and member.name.lower().endswith(".js"):
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        yield f"{path.name}!{member.name}", extracted.read()
        return
    yield path.name, path.read_bytes()


def scan_paths(paths: Iterable[Path], max_file_bytes: int = 100_000_000) -> dict:
    counts: Counter[str] = Counter()
    files: defaultdict[str, set[str]] = defaultdict(set)
    hosts: Counter[str] = Counter()
    scanned_files = 0
    skipped_files = 0
    scanned_bytes = 0
    for path in paths:
        for name, content in iter_sources(path):
            if len(content) > max_file_bytes:
                skipped_files += 1
                continue
            scanned_files += 1
            scanned_bytes += len(content)
            text = content.decode("utf-8", "ignore")
            for term in CONTRACT_TERMS:
                count = text.count(term)
                if count:
                    counts[term] += count
                    files[term].add(name)
            for host in URL_RE.findall(text):
                hosts[host.lower()] += 1
    return {
        "scanned_files": scanned_files,
        "skipped_files": skipped_files,
        "scanned_bytes": scanned_bytes,
        "contracts": {
            term: {"count": counts[term], "files": sorted(files[term])}
            for term in CONTRACT_TERMS
            if counts[term]
        },
        "url_hosts": dict(sorted(hosts.items())),
        "source_snippets_included": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path, help="extracted H5 directory, JS, or tar")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--max-file-mb", type=int, default=100)
    args = parser.parse_args()
    for path in args.paths:
        if not path.exists():
            parser.error(f"path does not exist: {path}")
    result = scan_paths(args.paths, max_file_bytes=args.max_file_mb * 1024 * 1024)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print(
            f"Scanned {result['scanned_files']} file(s), {result['scanned_bytes']} bytes; "
            f"skipped {result['skipped_files']}."
        )
        for term, details in result["contracts"].items():
            print(f"{term}: {details['count']} occurrence(s) in {len(details['files'])} file(s)")
        if result["url_hosts"]:
            print("URL hosts:", ", ".join(result["url_hosts"]))
        print("No JavaScript source snippets were emitted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
