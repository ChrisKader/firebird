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
- Primary persisted layout store is profile JSON in `AppConfigLocation/layouts/`
  (default profile: `layouts/default.json`).
- A JSON bridge export is also saved in settings key `windowLayoutJson` for migration tooling.
- Debug dock custom widget state is saved separately in settings key `debugDockStateJson`
  and embedded in layout/profile JSON as `debugDockState`.
- Layout state is restored with version fallback from `WindowStateVersion` down to `1`.
- Startup restore order is:
  1. selected profile JSON (`layoutProfile`, defaulting to `default`)
  2. legacy `windowState` in QSettings
  3. legacy `windowLayoutJson` bridge in QSettings
  4. reset to default layout
- If startup used legacy settings data, Firebird migrates it into the selected profile JSON,
  writes `layouts.bak.json` in the layout profile directory, and shows a one-time migration notice.
- If a profile JSON is malformed, Firebird preserves a copy as
  `*.corrupt.<timestamp>.json` and falls back to legacy/default restore logic.

## Layout Profiles

- `Docks -> Layouts` includes profile actions for:
  - Load: `default`, `debugging`, `custom`
  - Save as: `default`, `debugging`, `custom`
- Profiles are stored as JSON files in `AppConfigLocation/layouts/`.
- Each profile uses the same JSON schema (`windowStateBase64` + dock metadata) as the migration bridge.
- Profiles also store `debugDockState` for per-widget options (for example disassembly address,
  register display format, console filter/history, refresh rates, and memory-visualizer parameters).
- The selected profile is persisted (`layoutProfile`) and attempted first during startup before legacy `windowState` fallback.

## Design notes

- Docks use stable `objectName` values to preserve persisted layout identity.
- Utility docks now behave as regular docks (no forced dedicated sidebar column).
- External screen view is implemented as a regular floating dock (`dockExternalLCD`)
  instead of a separate top-level window.
- Debug dock auto-show behavior respects `dockFocusPolicy`:
  - `Always Raise`
  - `Raise on Explicit Actions`
  - `Never Raise Automatically`
- `resetDockLayout()` arranges bottom docks into grouped tabs:
  - Memory: Memory, Memory Visualizer, MMU Viewer, extra memory views
  - System: Port Monitor, Timer Monitor, LCD State, Cycle Counter
  - Debug tools: Console, Breakpoints, Watchpoints, Key History
- Layout undo/redo is available under `Edit`:
  - `Undo Layout` (`Ctrl+Alt+Z`)
  - `Redo Layout` (`Ctrl+Alt+Shift+Z`)
  - History is capped at 10 snapshots.

## Related code

- Dock creation: `mainwindow.cpp` (`convertTabsToDocks`)
- Reset defaults: `mainwindow.cpp` (`resetDockLayout`)
- Debug docks: `debugger/dockmanager.cpp`
