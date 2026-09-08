"""Join exact NuttX string matches with Ghidra function references."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
from pathlib import Path


def split_sources(value: str) -> list[str]:
    return [item for item in value.split("|") if item]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("matches", type=Path)
    parser.add_argument("xrefs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    literal_sources: dict[str, list[str]] = {}
    with args.matches.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            literal_sources[row["literal"]] = split_sources(row["sources"])

    functions: dict[tuple[str, str], dict[str, object]] = defaultdict(
        lambda: {"sources": Counter(), "literals": set()})
    with args.xrefs.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            sources = literal_sources.get(row["text"])
            if not sources:
                continue
            entry = functions[(row["function_address"], row["function_name"])]
            if row["text"] not in entry["literals"]:
                entry["literals"].add(row["text"])
                entry["sources"].update(sources)

    rows = []
    confidence_count = Counter()
    for (address, name), evidence in functions.items():
        counts: Counter = evidence["sources"]
        ranked = counts.most_common()
        top_count = ranked[0][1]
        unique_top = len(ranked) == 1 or ranked[1][1] < top_count
        if top_count >= 2 and unique_top:
            confidence = "high"
        elif len(evidence["literals"]) == 1 and len(ranked) == 1:
            confidence = "medium"
        else:
            confidence = "ambiguous"
        confidence_count[confidence] += 1
        rows.append({
            "function_address": address,
            "current_name": name,
            "confidence": confidence,
            "literal_count": len(evidence["literals"]),
            "source_candidates": "|".join(
                f"{source}:{count}" for source, count in ranked[:8]),
            "literals": "|".join(sorted(evidence["literals"])),
        })

    rank = {"high": 0, "medium": 1, "ambiguous": 2}
    rows.sort(key=lambda row: (rank[row["confidence"]], -row["literal_count"], row["function_address"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)

    print(f"functions with an exact NuttX literal: {len(rows)}")
    for confidence in ("high", "medium", "ambiguous"):
        print(f"{confidence}: {confidence_count[confidence]}")
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
