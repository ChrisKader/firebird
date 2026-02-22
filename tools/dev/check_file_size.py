#!/usr/bin/env python3
"""Advisory file-size checker for tracked C/C++ sources.

Counts effective lines: excludes blank lines and comment-only lines.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable


@dataclass
class FileStat:
    path: str
    lines: int


def tracked_sources() -> list[str]:
    cmd = ["git", "ls-files", "*.cpp", "*.h", "*.hpp", "*.c"]
    out = subprocess.check_output(cmd, text=True)
    files = [line.strip() for line in out.splitlines() if line.strip()]
    excluded_prefixes = (".build/", ".git/")
    return [f for f in files if not f.startswith(excluded_prefixes)]


def effective_line_count(path: str) -> int:
    p = pathlib.Path(path)
    try:
        with p.open("r", encoding="utf-8", errors="ignore") as fh:
            return count_effective_lines(fh)
    except OSError:
        return 0


def count_effective_lines(lines: Iterable[str]) -> int:
    in_block_comment = False
    effective = 0

    for raw_line in lines:
        line = str(raw_line)
        i = 0
        n = len(line)
        in_string = False
        in_char = False
        escaped = False
        has_code = False

        while i < n:
            c = line[i]
            nxt = line[i + 1] if i + 1 < n else ""

            if in_block_comment:
                if c == "*" and nxt == "/":
                    in_block_comment = False
                    i += 2
                    continue
                i += 1
                continue

            if in_string:
                has_code = True
                if escaped:
                    escaped = False
                elif c == "\\":
                    escaped = True
                elif c == '"':
                    in_string = False
                i += 1
                continue

            if in_char:
                has_code = True
                if escaped:
                    escaped = False
                elif c == "\\":
                    escaped = True
                elif c == "'":
                    in_char = False
                i += 1
                continue

            if c == "/" and nxt == "/":
                break
            if c == "/" and nxt == "*":
                in_block_comment = True
                i += 2
                continue
            if c == '"':
                in_string = True
                has_code = True
                i += 1
                continue
            if c == "'":
                in_char = True
                has_code = True
                i += 1
                continue
            if not c.isspace():
                has_code = True
            i += 1

        if has_code:
            effective += 1

    return effective


def main() -> int:
    parser = argparse.ArgumentParser(description="Check tracked source file sizes")
    parser.add_argument("--target", type=int, default=600)
    parser.add_argument("--cap", type=int, default=1000)
    parser.add_argument("--strict-cap", action="store_true", help="Return non-zero if any file exceeds cap")
    args = parser.parse_args()

    stats = [FileStat(path=f, lines=effective_line_count(f)) for f in tracked_sources()]
    stats.sort(key=lambda s: s.lines, reverse=True)

    over_target = [s for s in stats if s.lines > args.target]
    over_cap = [s for s in stats if s.lines > args.cap]

    print(f"Target: <= {args.target} effective lines, Cap: <= {args.cap} effective lines")
    print(f"Tracked files scanned: {len(stats)}")
    print(f"Over target: {len(over_target)}")
    print(f"Over cap: {len(over_cap)}")

    if over_target:
        print("\nTop files over target:")
        print("lines\tpath")
        for s in over_target[:25]:
            print(f"{s.lines}\t{s.path}")

    if over_cap:
        print("\nFiles over cap (exception required):")
        print("lines\tpath")
        for s in over_cap:
            print(f"{s.lines}\t{s.path}")

    if args.strict_cap and over_cap:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
