# KDDockWidgets Migration Notes

This file tracks Phase 1 dependency groundwork for KDDockWidgets migration.

## Build Flag

- CMake option: `FIREBIRD_ENABLE_KDDOCKWIDGETS` (default: `OFF`)
- When enabled:
  - Requires `find_package(KDDockWidgets 2.4 REQUIRED)`
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
