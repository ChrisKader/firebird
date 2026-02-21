# Powerpath Diagram Assets

This folder holds standalone Mermaid source files for `powerpath.md`.

Files:

- `01-stage-map.mmd`
- `02-overrides-to-effective-inputs.mmd`
- `03-qualification-vsys-charger.mmd`
- `04-adc-code-synthesis.mmd`
- `05-projection-surfaces.mmd`

## Local Viewing (interactive)

- Open any `.mmd` file in VS Code with a Mermaid preview extension.
- Or paste file content into https://mermaid.live for pan/zoom.

## SVG Render

From repo root:

```bash
./tools/render_powerpath_diagrams.sh
```

If `mmdc` is not installed and you want to use `npx`:

```bash
./tools/render_powerpath_diagrams.sh --use-npx
```
