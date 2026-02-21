# Firebird Project Structure and Engineering Guide

This document is the canonical guide for how to structure new code and refactors in Firebird.

## Modernization Phase Status

Status snapshot as of February 21, 2026:

- Phase 0 (reference scaffolding + advisory tooling): closed
- Phase 1 (MainWindow modularization): closed
- Phase 2 (typed connect migration): closed
- Phase 3 (KDD styling compliance + visual parity): closed
- Phase 4 (safety + lifecycle cleanup): closed
- Phase 5 (wider repo decomposition): open
- Phase 6 (ratchet enforcement): open

Phase 5 scope includes `core/` subfolderization: as oversized core files are split,
they should be moved into subsystem folders instead of adding new top-level
`core/*.c*` files.

## Source-Backed Rule Policy

- Every architecture/style rule in this file includes at least one primary Qt/KDD source link.
- "Best practice" claims without source references are not accepted in docs or PR rationale.
- For docking-specific behavior, KDDockWidgets docs take precedence over generic Qt guidance.

## Reference Inspirations

- Qt Creator (large modular Qt codebase):
  - https://github.com/qt-creator/qt-creator
  - https://doc.qt.io/qtcreator-extending/coding-style.html
- KDDockWidgets upstream examples/tests:
  - https://github.com/KDAB/KDDockWidgets/tree/main/examples
  - https://github.com/KDAB/KDDockWidgets/tree/main/tests
- NVIDIA Nsight KDD usage note:
  - https://www.kdab.com/nvidia-uses-kddockwidgets-in-nsight/

## Directory Ownership Map

- `mainwindow/`: Desktop main-window behavior modules (bootstrap, docks, theme, runtime actions).
  - `mainwindow.cpp`: orchestration/state persistence entry points.
  - `mainwindow/bootstrap.cpp`: constructor/startup UI wiring.
  - `mainwindow/docks/*.cpp`: dock construction/reset behavior.
  - `mainwindow/layout_persistence.cpp`: profile and layout serialization.
  - `mainwindow/runtime*.cpp`: runtime actions/state transitions.
- `ui/`: Reusable desktop widgets/components.
  - `ui/models/`: Qt model/view backing models shared with QML/widgets.
  - `ui/docking/backend/*`: backend adapters between Qt and KDD docking APIs.
  - `ui/docking/manager/*`: dock orchestration and debug-dock lifecycle/state.
    - `debugdockregistration*`: registry model + aggregation for debug-dock metadata/factories.
  - `ui/docking/widgets/*`: dock widget wrappers (`DockWidget`, `KDockWidget`).
  - `ui/docking/state/*`: per-dock state serialization interfaces.
  - `ui/theme/*`: widget theme and icon helpers.
  - `ui/screen/*`: framebuffer and LCD widget surface plumbing.
  - `ui/input/*`: keypad bridge and key map integration.
  - `ui/text/*`: ANSI text rendering helpers for console-like widgets.
  - `ui/widgets/*`: feature widgets grouped by module (`breakpoints`, `disassembly`, `hexview`, `hwconfig`, etc.).
    - Each debug widget module may expose a local `dockregistration.cpp` for dock metadata/factory registration.
- `app/`: Cross-UI runtime bridge services (`EmuThread`, `QMLBridge`).
- `core/`: Emulation core and hardware models.
  - Active module folders:
    - `core/cpu/`: CPU/interpreter/JIT translation and coprocessor logic.
    - `core/debug/`: debugger command handling, API, remote debug transport, GDB glue.
    - `core/memory/`: RAM/MMU/address-translation and memory-map dispatch.
    - `core/storage/`: persistent storage models (flash image + NAND filesystem helpers).
    - `core/peripherals/`: non-USB device models (interrupt, keypad, LCD, link, serial, misc peripherals, CX2 peripheral map helpers).
    - `core/usb/`: USB device/host emulation and usblink transport.
    - `core/power/`: host-side power-path override/control helpers used by UI/debug flows.
    - `core/os/`: OS-platform shims and target-specific OS glue.
    - `core/tests/`: core-focused tests.
  - Remaining files in `core/` root are legacy/shared units that still need module placement during Phase 5.
- `transfer/`: USB file transfer UI/components.
- `docs/`: Architecture and behavior specifications.
- `tools/`: Dev and CI support scripts.

## Layer Boundaries

### UI Layer

- Qt widgets/QML only. No direct emulator core mutations beyond thread-safe API calls.
- Source: https://doc.qt.io/qt-6/threads-qobject.html

### Runtime Wiring Layer

- `MainWindow`/`QMLBridge` connect UI intent to `EmuThread` via typed signals/slots.
- Prefer compiler-checked typed connect syntax.
- Source: https://doc.qt.io/qt-6/signalsandslots-syntaxes.html

### Docking Layer

- Dock construction, restoration, and persistence are isolated from generic runtime actions.
- Keep layout identities stable via persistent unique object names.
- Source: https://doc.qt.io/qt-6/qmainwindow.html

### Core Layer

- Emulation internals stay independent of widget code.
- UI communicates through narrow service boundaries (`EmuThread`, `QMLBridge`, debug APIs).
- Source: https://doc.qt.io/qt-6.5/model-view-programming.html

## File Size Policy

- Target: <= 600 effective lines for `.cpp` implementation files.
- Hard exception threshold: > 1000 effective lines requires an explicit waiver in the PR.
- Effective lines exclude blank lines and comment-only lines (as measured by `tools/dev/check_file_size.py`).
- Generated files are excluded.

Exception template (required if >1000 lines):

```text
Reason:
Boundary considered:
Alternatives evaluated:
Follow-up split ticket:
```

## Core Subfolderization Policy (Phase 5)

- New files created by `core/` refactors must go under a subsystem folder (for example `core/debug/` or `core/usb/`) rather than `core/` root.
- When splitting an oversized top-level `core/*.c*` file, move extracted units into subsystem folders in the same change.
- Prefer cohesive module names over generic buckets:
  - Preferred: `core/debug/remote.cpp`
  - Avoid: `core/misc2.cpp`
- Keep behavior-preserving scope: folder moves plus decomposition should not include unrelated logic changes.
- Update all build references (`CMakeLists.txt`) and include paths in the same PR.
- Temporary exceptions are allowed only when a move would create circular include churn; exceptions must include a follow-up ticket in the PR.

Sources:
- Qt Creator modular architecture inspiration: https://github.com/qt-creator/qt-creator
- Qt Creator coding style (organization consistency): https://doc.qt.io/qtcreator-extending/coding-style.html
- Separation-of-concerns rationale used across this plan: https://doc.qt.io/qt-6.5/model-view-programming.html

## Qt Rules

### Signals/Slots

- Use typed connects (`&Sender::signal`, `&Receiver::slot`) or lambdas.
- Avoid legacy `SIGNAL()/SLOT()` except documented compatibility edge-cases.
- Source: https://doc.qt.io/qt-6/signalsandslots-syntaxes.html

### Object Ownership

- Prefer QObject parent ownership over manual delete management where practical.
- Non-owning pointers must be clearly identified (`QPointer`, comments, naming).
- Source: https://doc.qt.io/qt-6/objecttrees.html

### Threading

- UI updates must run on GUI thread.
- Emulation-thread -> UI updates should use queued delivery.
- Source: https://doc.qt.io/qt-6/threads-qobject.html

## KDDockWidgets Rules

### Styling

- Avoid broad/global stylesheets on KDD dock internals.
- Prefer palette and limited widget-level styling paths compatible with KDD guidance.
- Source: https://kdab.github.io/KDDockWidgets/custom_styling.html

### Architecture

- Keep GUI concerns separated from docking state and behavior logic.
- Source: https://kdab.github.io/KDDockWidgets/

### Usage Pattern

- Follow example-first API usage and avoid relying on private/internal handles.
- Sources:
  - https://kdab.github.io/KDDockWidgets/installation_and_usage.html
  - https://kdab.github.io/KDDockWidgets/examples_qtwidgets_full.html

## PR Checklist

- [ ] Added/updated source links for any new "best practice" rule.
- [ ] No new legacy `SIGNAL()/SLOT()` usages.
- [ ] New/changed files follow size policy or include waiver.
- [ ] Docking changes avoid unsupported broad stylesheet behavior.
- [ ] Ownership and thread-boundary implications are documented.
- [ ] `cmake --build build -j8` and `ctest --test-dir build --output-on-failure` pass locally.

## Advisory Tooling

- File size advisory: `python3 tools/dev/check_file_size.py`
- Clazy advisory: `tools/ci/run_clazy.sh`
- clang-tidy advisory: `tools/ci/run_clang_tidy_advisory.sh`
- Reference map advisory: `tools/ci/check_reference_map.py`

See `docs/REFERENCE_MAP.md` for rule-to-source coverage and current adoption state.
