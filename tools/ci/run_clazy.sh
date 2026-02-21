#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"

if ! command -v clazy-standalone >/dev/null 2>&1; then
  echo "clazy-standalone not found; skipping advisory run."
  exit 0
fi

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "${BUILD_DIR}/compile_commands.json not found; skipping advisory run."
  exit 0
fi

mapfile -t FILES < <(git ls-files '*.cpp')
if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No cpp files found."
  exit 0
fi

echo "Running clazy advisory checks on ${#FILES[@]} files..."
set +e
clazy-standalone --checks=level0,level1 -p "${BUILD_DIR}" "${FILES[@]}"
STATUS=$?
set -e

if [[ ${STATUS} -ne 0 ]]; then
  echo "clazy reported issues (advisory mode)."
fi

exit 0
