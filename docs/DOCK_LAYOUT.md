# Dock Layout Initialization

This document describes the desktop dock initialization and restore order.

## Required sequence

`MainWindow` follows a strict order during startup:

1. Create all primary docks.
2. Create debugger docks (including extra dynamic hex docks).
3. Restore window geometry and dock layout (`restoreState`).
4. Apply post-restore linkage behavior (for example keypad QML reload behavior).

Changing this order can break persisted layouts because `restoreState` requires all target dock object names to exist first.

## Persistence

- Layout state is saved with `content_window->saveState(WindowStateVersion)`.
- A JSON bridge export is also saved in settings key `windowLayoutJson` for migration tooling.
- Layout state is restored with version fallback from `WindowStateVersion` down to `1`.
- If restore fails for all versions, default dock layout is applied.

## Layout Profiles

- `Docks -> Layouts` includes profile actions for:
  - Load: `default`, `debugging`, `custom`
  - Save as: `default`, `debugging`, `custom`
- Profiles are stored as JSON files in `AppConfigLocation/layouts/`.
- Each profile uses the same JSON schema (`windowStateBase64` + dock metadata) as the migration bridge.
- The selected profile is persisted (`layoutProfile`) and attempted first during startup before legacy `windowState` fallback.

## Design notes

- Docks use stable `objectName` values to preserve persisted layout identity.
- Utility docks now behave as regular docks (no forced dedicated sidebar column).

## Related code

- Dock creation: `mainwindow.cpp` (`convertTabsToDocks`)
- Reset defaults: `mainwindow.cpp` (`resetDockLayout`)
- Debug docks: `debugger/dockmanager.cpp`
