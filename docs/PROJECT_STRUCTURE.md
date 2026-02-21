# Firebird Project Structure and Engineering Guide

This document is the canonical guide for how to structure new code and refactors in Firebird.

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
- `debugger/`: Debug panel widgets and docking manager.
- `app/`: Cross-UI runtime bridge services (`EmuThread`, `QMLBridge`).
- `core/`: Emulation core and hardware models.
  - `core/power/`: Power-path override control helpers used by UI and debugger.
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

- Target: <= 600 lines for `.cpp` implementation files.
- Hard exception threshold: > 1000 lines requires an explicit waiver in the PR.
- Generated files are excluded.

Exception template (required if >1000 lines):

```text
Reason:
Boundary considered:
Alternatives evaluated:
Follow-up split ticket:
```

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
