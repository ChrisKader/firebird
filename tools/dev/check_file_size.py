#!/usr/bin/env python3
"""Advisory file-size checker for tracked C/C++ sources."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from dataclasses import dataclass


@dataclass
class FileStat:
    path: str
    lines: int


def tracked_sources() -> list[str]:
    cmd = ["git", "ls-files", "*.cpp", "*.h", "*.hpp", "*.c"]
    out = subprocess.check_output(cmd, text=True)
    files = [line.strip() for line in out.splitlines() if line.strip()]
    excluded_prefixes = ("build/", ".deps/", ".git/")
    return [f for f in files if not f.startswith(excluded_prefixes)]


def line_count(path: str) -> int:
    p = pathlib.Path(path)
    try:
        with p.open("r", encoding="utf-8", errors="ignore") as fh:
            return sum(1 for _ in fh)
    except OSError:
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check tracked source file sizes")
    parser.add_argument("--target", type=int, default=600)
    parser.add_argument("--cap", type=int, default=1000)
    parser.add_argument("--strict-cap", action="store_true", help="Return non-zero if any file exceeds cap")
    args = parser.parse_args()

    stats = [FileStat(path=f, lines=line_count(f)) for f in tracked_sources()]
    stats.sort(key=lambda s: s.lines, reverse=True)

    over_target = [s for s in stats if s.lines > args.target]
    over_cap = [s for s in stats if s.lines > args.cap]

    print(f"Target: <= {args.target} lines, Cap: <= {args.cap} lines")
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
