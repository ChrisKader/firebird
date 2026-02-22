#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import List


def run(cmd: List[str], cwd: str | None = None) -> int:
    print("+", " ".join(cmd))
    return subprocess.call(cmd, cwd=cwd)


def run_capture_json(cmd: List[str]) -> list[dict]:
    output = subprocess.check_output(cmd, text=True)
    return json.loads(output)


def ensure_gh_available() -> None:
    if shutil.which("gh"):
        return
    raise RuntimeError("GitHub CLI 'gh' is required for CI fallback targets.")


def latest_run_id(workflow: str, ref: str, repo: str) -> int:
    cmd = [
        "gh",
        "run",
        "list",
        "--workflow",
        workflow,
        "--limit",
        "1",
        "--json",
        "databaseId,headBranch",
    ]
    if ref:
        cmd.extend(["--branch", ref])
    if repo:
        cmd.extend(["--repo", repo])
    runs = run_capture_json(cmd)
    if not runs:
        raise RuntimeError(f"No workflow runs found for workflow '{workflow}'.")
    run_id = runs[0].get("databaseId")
    if not run_id:
        raise RuntimeError(f"Could not determine run id for workflow '{workflow}'.")
    return int(run_id)


def main() -> int:
    parser = argparse.ArgumentParser(description="Dispatch, watch, and download artifacts from a GitHub Actions workflow.")
    parser.add_argument("--workflow", required=True, help="Workflow file name, e.g. windows.yml")
    parser.add_argument("--ref", default="", help="Git ref/branch to run against (default: workflow default branch)")
    parser.add_argument("--artifact-dir", required=True, help="Directory where artifacts are downloaded")
    parser.add_argument("--repo", default="", help="Optional owner/repo override")
    args = parser.parse_args()

    ensure_gh_available()

    run_cmd = ["gh", "workflow", "run", args.workflow]
    if args.ref:
        run_cmd.extend(["--ref", args.ref])
    if args.repo:
        run_cmd.extend(["--repo", args.repo])
    if run(run_cmd) != 0:
        return 1

    run_id = latest_run_id(args.workflow, args.ref, args.repo)
    print(f"Watching run id: {run_id}")

    watch_cmd = ["gh", "run", "watch", str(run_id), "--exit-status"]
    if args.repo:
        watch_cmd.extend(["--repo", args.repo])
    if run(watch_cmd) != 0:
        return 1

    artifact_dir = Path(args.artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)

    download_cmd = ["gh", "run", "download", str(run_id), "--dir", str(artifact_dir)]
    if args.repo:
        download_cmd.extend(["--repo", args.repo])
    if run(download_cmd) != 0:
        return 1

    print(f"Artifacts downloaded to: {artifact_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
