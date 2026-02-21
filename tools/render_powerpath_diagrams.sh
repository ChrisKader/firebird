#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIAG_DIR="$ROOT_DIR/docs/diagrams/powerpath"

if ! ls "$DIAG_DIR"/*.mmd >/dev/null 2>&1; then
  echo "No .mmd files found in $DIAG_DIR" >&2
  exit 1
fi

USE_NPX=0
if [[ "${1:-}" == "--use-npx" ]]; then
  USE_NPX=1
fi

if command -v mmdc >/dev/null 2>&1; then
  RUNNER=(mmdc)
elif [[ "$USE_NPX" -eq 1 ]] && command -v npx >/dev/null 2>&1; then
  RUNNER=(npx --yes @mermaid-js/mermaid-cli)
else
  cat >&2 <<EOF
Missing Mermaid CLI ('mmdc').

Options:
  1) Install Mermaid CLI locally and rerun:
     npm install -g @mermaid-js/mermaid-cli
  2) Try one-shot npx install (requires network):
     ./tools/render_powerpath_diagrams.sh --use-npx
EOF
  exit 1
fi

for in_file in "$DIAG_DIR"/*.mmd; do
  out_file="${in_file%.mmd}.svg"
  echo "Rendering $(basename "$in_file") -> $(basename "$out_file")"
  "${RUNNER[@]}" -i "$in_file" -o "$out_file" -b transparent -t neutral
done

echo "Done. SVG files are in $DIAG_DIR"
