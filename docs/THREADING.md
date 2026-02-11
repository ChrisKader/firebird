# Threading Model

This document describes the current threading model used by Firebird's desktop UI and emulation core.

## Threads

- GUI thread: owns `MainWindow`, all Qt widgets, and all dock/console rendering.
- Emulation thread: `EmuThread` (`app/emuthread.cpp`) running `emu_loop()` and core callbacks.

## Signal/slot direction

- Emu -> GUI uses `Qt::QueuedConnection` in `mainwindow.cpp` so UI updates always run on the GUI thread.
  - Examples: `serialChar`, `debugStr`, `nlogStr`, `isBusy`, `debuggerEntered`, state transitions.
- GUI -> Emu currently uses direct calls/flag updates (for example pause/reset/debugger commands).
  - These calls are intentionally lightweight and mostly set shared flags that the emulation loop consumes.

## Current safety guarantees

- UI widgets are only created/destroyed on the GUI thread.
- Core CPU execution (`emu_loop`) runs on the emulation thread.
- Cross-thread UI updates are queued via Qt signals.

## Known limitations

- Debug widgets still read global emulator state directly (for example via globals in `core/emu.h`).
- There is no single atomic "debug snapshot" object yet.
- `volatile` globals are still used in several places; this is not equivalent to full synchronization.

## Practical rules for contributors

- Do not touch Qt widgets from emulation/core code paths.
- Route text/status updates through `EmuThread` signals (`debugStr`, `nlogStr`, `statusMsg`, etc.).
- Keep GUI -> Emu calls minimal (set intent/flags, avoid heavy work).
- If adding a new debug panel, prefer refreshing while paused and avoid long blocking work in the GUI thread.

## Planned direction

- Introduce a CPU/debug snapshot object captured at pause points.
- Move debug widgets to read the snapshot rather than raw global state.
- Replace `volatile` shared state with atomics/explicit synchronization where appropriate.
