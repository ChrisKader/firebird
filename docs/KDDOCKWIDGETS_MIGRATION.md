# KDDockWidgets Migration Notes

This file tracks KDDockWidgets migration status and follow-on tasks.

See also:
- `docs/PROJECT_STRUCTURE.md` for KDD usage/styling standards.
- `docs/REFERENCE_MAP.md` for source-linked rule coverage.

## Build Flag

- CMake option: `FIREBIRD_ENABLE_KDDOCKWIDGETS` (default: `ON`)
- When enabled:
  - Requires `find_package(KDDockWidgets 2.4 EXACT REQUIRED)`
  - Defines `FIREBIRD_USE_KDDOCKWIDGETS=1`
  - Links against an exported KDDockWidgets target if available

## Default Build Path

KDDockWidgets is now part of the default desktop build path. Use `build/`
as the canonical CMake build directory; a separate `build-kdd/` flow is no
longer required.

## Platform Status (Current)

- Linux: not verified in this phase
- macOS: scaffold builds with option ON (default path)
- Windows: not verified in this phase
- Android: not verified in this phase
- Emscripten/WebAssembly: not verified in this phase

## Phase Status

Status snapshot as of February 21, 2026:

- Phase 1 (modularization groundwork): closed
- Phase 2 (typed connect migration in affected UI paths): closed
- Phase 3 (KDD styling compliance + visual parity checks): closed
- Phase 4 (docking-path safety/lifecycle cleanup): closed
- Phase 5+ (wider repo decomposition and enforcement ratchets): pending

## Converter Utility

The repo now includes `firebird-layout-convert` (built from `tools/layout_convert.cpp`).

Example:

```bash
./build/firebird-layout-convert \
  --settings "$HOME/.config/firebird-emu/nspire_emu_thread.ini" \
  --output /tmp/firebird-layout.json \
  --pretty
```

What it does:

- Reads legacy `windowState` from settings.
- Creates placeholder docks for known Firebird dock object names.
- Calls `restoreState()` with version fallback.
- Emits migration JSON with dock area/visibility/floating/geometry info.

## Wrapper Layer

- `ui/docking/widgets/kdockwidget.h` / `ui/docking/widgets/kdockwidget.cpp` now provide `KDockWidget`.
- `DockWidget` now aliases `KDockWidget` when `FIREBIRD_USE_KDDOCKWIDGETS` is enabled,
  and remains a `QDockWidget` subclass on non-KDD builds.
- Main and debugger dock creation paths now construct `KDockWidget`, which reduces future migration churn when swapping to KDDockWidgets-backed docks.

## Per-Dock State Hook

- Debug widgets now opt into `DockStateSerializable` (`ui/docking/state/dockstate.h`) to persist custom per-dock UI state.
- `DockManager` saves/restores these states under `debugDockState` in layout JSON/profile files.
