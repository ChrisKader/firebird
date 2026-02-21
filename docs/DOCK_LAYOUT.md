# Dock Layout Initialization

This document describes the desktop dock initialization and restore order.

See also:
- `docs/PROJECT_STRUCTURE.md` for architectural boundaries and coding rules.
- `docs/REFERENCE_MAP.md` for rule-to-source traceability.

## Required sequence

`MainWindow` follows a strict order during startup:

1. Create all primary docks.
2. Create debugger docks (including extra dynamic hex docks).
3. Restore window geometry and dock layout (`KDDockWidgets::LayoutSaver`).
4. Apply post-restore linkage behavior (for example keypad QML reload behavior).

Changing this order can break persisted layouts because restore requires all target dock object names to exist first.

## Persistence

- Layout state is saved with `KDDockWidgets::LayoutSaver::serializeLayout()`.
- Canonical save path is `MainWindow::closeEvent` (`mainwindow/runtime/window.cpp`), so
  layout/geometry persistence runs while the dock tree is still alive.
- Primary persisted layout store is profile JSON in `AppConfigLocation/layouts/`
  (default profile: `layouts/default.json`).
- A compact serialized layout blob is also saved in settings key `dockLayoutJson`.
- A JSON layout snapshot is also saved in settings key `windowLayoutJson`.
- Debug dock custom widget state is saved separately in settings key `debugDockStateJson`
  and embedded in layout/profile JSON as `debugDockState`.
- Startup restore order is:
  1. selected profile JSON (`layoutProfile`, defaulting to `default`)
  2. serialized `dockLayoutJson` in QSettings
  3. legacy `windowLayoutJson` hint map in QSettings
  4. reset to default layout
- If startup used legacy `windowLayoutJson` data, Firebird migrates it into the selected profile JSON,
  writes `layouts.bak.json` in the layout profile directory, and shows a one-time migration notice.
- If a profile JSON is malformed, Firebird preserves a copy as
  `*.corrupt.<timestamp>.json` and falls back to settings/default restore logic.

## Layout Profiles

- `Docks -> Layouts` includes profile actions for:
  - Load: `default`, `debugging`, `widescreen`, `custom`
  - Save as: `default`, `debugging`, `widescreen`, `custom`
- Profiles are stored as JSON files in `AppConfigLocation/layouts/`.
- Each profile uses schema `firebird.kdd.layout.v1` with `layoutBase64` and dock metadata.
- Profiles also store `debugDockState` for per-widget options (for example disassembly address,
  register display format, console filter/history, refresh rates, and memory-visualizer parameters).
- The selected profile is persisted (`layoutProfile`) and attempted first during startup before settings fallback.

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

- Dock creation: `mainwindow/docks/setup.cpp` (`convertTabsToDocks`)
- Reset defaults: `mainwindow/docks/reset.cpp` (`resetDockLayout`)
- Baseline reset payload builders: `mainwindow/docks/baseline.cpp`
- Layout profile/persistence helpers: `mainwindow/layout_persistence.cpp`
- Startup restore orchestration: `mainwindow.cpp` (`restoreStartupLayoutFromSettings`)
- Debug docks: `debugger/dockmanager.cpp`
