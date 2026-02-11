# KDDockWidgets Migration Notes

This file tracks Phase 1 dependency groundwork for KDDockWidgets migration.

## Build Flag

- CMake option: `FIREBIRD_ENABLE_KDDOCKWIDGETS` (default: `OFF`)
- When enabled:
  - Requires `find_package(KDDockWidgets 2.4 EXACT REQUIRED)`
  - Defines `FIREBIRD_USE_KDDOCKWIDGETS=1`
  - Links against an exported KDDockWidgets target if available

## Why Disabled by Default

The current UI still uses Qt `QDockWidget`. Enabling KDDockWidgets in this stage
only validates dependency wiring and keeps migration work isolated from the
stable path.

## Platform Status (Current)

- Linux: not verified in this phase
- macOS: scaffold builds with option OFF (default path)
- Windows: not verified in this phase
- Android: not verified in this phase
- Emscripten/WebAssembly: not verified in this phase

## Next Steps

1. Verify `FIREBIRD_ENABLE_KDDOCKWIDGETS=ON` on desktop platforms.
2. Add a small prototype docking area (LCD/Keypad/Console).
3. Start layout migration tooling (`QMainWindow` state -> JSON).

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

- `ui/kdockwidget.h` / `ui/kdockwidget.cpp` now provide `KDockWidget`.
- Current implementation still inherits existing `DockWidget` so behavior is unchanged.
- Main and debugger dock creation paths now construct `KDockWidget`, which reduces future migration churn when swapping to KDDockWidgets-backed docks.

## Per-Dock State Hook

- Debug widgets now opt into `DockStateSerializable` (`ui/dockstate.h`) to persist custom per-dock UI state.
- `DebugDockManager` saves/restores these states under `debugDockState` in layout JSON/profile files.
