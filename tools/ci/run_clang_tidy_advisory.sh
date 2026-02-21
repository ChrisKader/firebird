#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not found; skipping advisory run."
  exit 0
fi

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "${BUILD_DIR}/compile_commands.json not found; skipping advisory run."
  exit 0
fi

CHECKS='-*,modernize-use-nullptr,modernize-use-override,modernize-use-bool-literals,performance-unnecessary-value-param,readability-redundant-control-flow'

mapfile -t FILES < <(git ls-files '*.cpp' '*.h')
if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No source files found."
  exit 0
fi

echo "Running clang-tidy advisory checks on ${#FILES[@]} files..."
set +e
clang-tidy -p "${BUILD_DIR}" -checks="${CHECKS}" "${FILES[@]}"
STATUS=$?
set -e

if [[ ${STATUS} -ne 0 ]]; then
  echo "clang-tidy reported issues (advisory mode)."
fi

exit 0
