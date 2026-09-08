"""Build a reproducible AP product-logic worklist from Ghidra exports.

This does not claim that a named or decompiled function is understood. It keeps
three separate signals: product evidence, semantic review, and compatible-code
traceability. The output is intended to make the 70–80% target measurable.
"""
from __future__ import annotations

import csv
import math
import re
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXPORT = ROOT / "decompiled" / "exports" / "nuttx_ap.bin"
ANALYSIS = ROOT / "analysis"

PRODUCT_NAME = re.compile(
    r"^(?:dt_|dtiot_|audio_status$|flush_voice_memo|voice_|"
    r"lnv_(?:key|hid|wifi|webserver|motor|audio|emmc|adb|usb)|factory_)"
)
DEFAULT_NAME = re.compile(r"^(?:FUN_|thunk_FUN_|EXT_FUN_|thunk_EXT_FUN_)")
PRODUCT_TEXT = re.compile(
    r"(?:BLE_REQUEST|dingtalk|DTIOT|voice[ _-]?memo|/emmc/audio|persist\.dt\.aud|"
    r"AI key|aikey|work_mode|raw[ _-]?transfer|schedule[ _-]?record|"
    r"incognito|deviceSecret|device_secret|remark|vibra|HID.*ADB|ADB.*HID|"
    r"wifi.*(?:AP|ssid)|(?:AP|ssid).*wifi)",
    re.IGNORECASE,
)


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def main() -> None:
    functions = {row["address"].lower(): row for row in read_tsv(EXPORT / "functions.tsv")}
    calls = read_tsv(EXPORT / "calls.tsv")
    strings = read_tsv(EXPORT / "all_string_xrefs.tsv")

    outgoing: dict[str, set[str]] = defaultdict(set)
    incoming: dict[str, set[str]] = defaultdict(set)
    for call in calls:
        caller, callee = call["caller"].lower(), call["callee"].lower()
        if caller in functions and callee in functions:
            outgoing[caller].add(callee)
            incoming[callee].add(caller)

    product_strings: dict[str, set[str]] = defaultdict(set)
    for row in strings:
        address = row["function_address"].lower()
        text = row["text"]
        if address in functions and PRODUCT_TEXT.search(text):
            product_strings[address].add(text.replace("\t", " ").replace("\n", " "))

    focus_addresses = {
        path.name.split("_", 1)[0].lower()
        for path in (ROOT / "decompiled" / "focus").glob("*.c")
    }
    reviews = {
        row["address"].lower(): row
        for row in read_tsv(ANALYSIS / "semantic_reviews.tsv")
    }
    reviewed_addresses = set(reviews) & set(functions)
    traceability = (ROOT / "compat-src" / "docs" / "TRACEABILITY.md").read_text(
        encoding="utf-8"
    )
    traceability_addresses = {
        match.group(1).lower()
        for match in re.finditer(r"AP\s+`0x([0-9a-fA-F]+)`", traceability)
    }
    modeled_addresses = {
        address for address, row in reviews.items() if row["status"] == "modeled"
    } | traceability_addresses

    named_seeds = {
        address for address, row in functions.items() if PRODUCT_NAME.search(row["name"])
    }
    string_seeds = set(product_strings)
    business_seeds = named_seeds | string_seeds | reviewed_addresses

    neighborhood = set(business_seeds)
    for address in business_seeds:
        neighborhood.update(outgoing[address])
        neighborhood.update(incoming[address])

    rows: list[dict[str, object]] = []
    for address in neighborhood:
        row = functions[address]
        seed_neighbors = len(outgoing[address] & business_seeds) + len(incoming[address] & business_seeds)
        score = (
            (100 if address in focus_addresses else 0)
            + (70 if address in named_seeds else 0)
            + (55 if address in string_seeds else 0)
            + min(40, 8 * len(product_strings[address]))
            + min(40, 4 * seed_neighbors)
            + min(15, int(math.log2(max(1, int(row["body_bytes"])))))
        )
        evidence = []
        if address in named_seeds:
            evidence.append("product_name")
        if address in string_seeds:
            evidence.append("product_string")
        if address in focus_addresses:
            evidence.append("focus_export")
        if address in reviewed_addresses:
            evidence.append("semantic_review")
        if address in modeled_addresses:
            evidence.append("compat_trace")
        if not evidence:
            evidence.append("direct_call_neighbor")
        rows.append(
            {
                "score": score,
                "address": address,
                "name": row["name"],
                "body_bytes": row["body_bytes"],
                "evidence": ",".join(evidence),
                "reviewed": int(address in reviewed_addresses),
                "compat_modeled": int(address in modeled_addresses),
                "default_name": int(bool(DEFAULT_NAME.search(row["name"]))),
                "product_string_count": len(product_strings[address]),
                "business_neighbor_count": seed_neighbors,
                "callers": len(incoming[address]),
                "callees": len(outgoing[address]),
                "example_string": next(iter(sorted(product_strings[address])), "")[:240],
                "file": row["file"],
            }
        )
    rows.sort(key=lambda item: (-int(item["score"]), item["address"]))

    ANALYSIS.mkdir(exist_ok=True)
    worklist = ANALYSIS / "ap-product-function-worklist.tsv"
    with worklist.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)

    seed_reviewed = len(business_seeds & reviewed_addresses)
    seed_focused = len(business_seeds & focus_addresses)
    seed_modeled = len(business_seeds & modeled_addresses)
    seed_named = sum(not DEFAULT_NAME.search(functions[address]["name"]) for address in business_seeds)
    unknown_ranked = [row for row in rows if row["default_name"] and not row["reviewed"]]
    report = ROOT / "PRODUCT-RECONSTRUCTION-STATUS.md"
    lines = [
        "# A1 AP product reconstruction status",
        "",
        "Generated from the V1.6.88 AP Ghidra function/call/string exports. These",
        "numbers measure evidence handling, not source-code identity.",
        "",
        "## Measured workset",
        "",
        f"- all AP functions: {len(functions):,}",
        f"- evidence-backed product/business seeds: {len(business_seeds):,}",
        f"- direct call neighborhood: {len(neighborhood):,}",
        f"- seeds with meaningful recovered names: {seed_named:,} ({seed_named / len(business_seeds):.1%})",
        f"- seeds copied into the focused export: {seed_focused:,} ({seed_focused / len(business_seeds):.1%})",
        f"- seeds with a written semantic review: {seed_reviewed:,} ({seed_reviewed / len(business_seeds):.1%})",
        f"- seeds tied to compatible-source traceability addresses: {seed_modeled:,} ({seed_modeled / len(business_seeds):.1%})",
        "",
        "A successful Ghidra decompile is intentionally not counted as understood.",
        "The 70–80% target uses written semantic reviews, not focused-file membership.",
        "A second metric tracks which reviewed behaviors exist in compatible C.",
        "",
        "## Highest-ranked unnamed, unreviewed functions",
        "",
        "| score | address | bytes | product neighbors | example evidence |",
        "| ---: | --- | ---: | ---: | --- |",
    ]
    for row in unknown_ranked[:30]:
        evidence = str(row["example_string"]).replace("|", "\\|") or "call-graph adjacency"
        lines.append(
            f"| {row['score']} | `0x{row['address']}` | {row['body_bytes']} | "
            f"{row['business_neighbor_count']} | {evidence} |"
        )
    lines.extend(
        [
            "",
            "Machine-readable queue: `analysis/ap-product-function-worklist.tsv`.",
            "Re-run `build-product-worklist.py` after each naming/review pass.",
            "",
        ]
    )
    report.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {worklist} ({len(rows)} rows)")
    print(f"wrote {report}")
    print(
        f"business seeds={len(business_seeds)}, reviewed={seed_reviewed}, focused={seed_focused}, "
        f"compat-traced={seed_modeled}, neighborhood={len(neighborhood)}"
    )


if __name__ == "__main__":
    main()
