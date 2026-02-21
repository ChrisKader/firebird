#!/usr/bin/env python3
"""Validate that each FRB rule row in REFERENCE_MAP has at least one URL."""

from __future__ import annotations

import pathlib
import re
import sys

RULE_ROW = re.compile(r"^\|\s*FRB-[^|]+\|", re.IGNORECASE)
URL = re.compile(r"https?://")


def main() -> int:
    refmap = pathlib.Path("docs/REFERENCE_MAP.md")
    if not refmap.exists():
        print("docs/REFERENCE_MAP.md not found")
        return 1

    missing = []
    for lineno, line in enumerate(refmap.read_text(encoding="utf-8").splitlines(), start=1):
        if RULE_ROW.search(line) and not URL.search(line):
            missing.append((lineno, line))

    if missing:
        print("Reference map validation failed: rows missing URL(s)")
        for lineno, line in missing:
            print(f"line {lineno}: {line}")
        return 1

    print("Reference map validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
