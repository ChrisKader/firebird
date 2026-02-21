# Firebird Rule Reference Map

This appendix maps each enforced or planned engineering rule to sources and impacted code.

| Rule ID | Rule | Sources | Firebird Areas | Status |
|---|---|---|---|---|
| FRB-QT-001 | Use typed signal/slot connections; avoid legacy `SIGNAL/SLOT` | https://doc.qt.io/qt-6/signalsandslots-syntaxes.html | `mainwindow/bootstrap.cpp`, `mainwindow/runtime.cpp`, `mainwindow/docks/setup.cpp`, `app/qmlbridge*.cpp`, `transfer/usblinktreewidget.cpp`, `dialogs/fbaboutdialog.cpp` | enforced |
| FRB-QT-002 | Respect QObject parent ownership and explicit non-owning pointers | https://doc.qt.io/qt-6/objecttrees.html, https://doc.qt.io/qt-6.8/qobject.html | `mainwindow.h`, `app/qmlbridge.h`, `ui/docking/dockmanager*`, `ui/widgets/*`, `ui/*` | partial |
| FRB-QT-003 | Keep UI updates on GUI thread; queued cross-thread UI signals | https://doc.qt.io/qt-6/threads-qobject.html | `mainwindow/bootstrap.cpp`, `mainwindow/runtime.cpp`, `app/qmlbridge*.cpp` | partial |
| FRB-QT-004 | Preserve stable unique names for dock/state persistence | https://doc.qt.io/qt-6/qmainwindow.html | `mainwindow/docks/setup.cpp`, `mainwindow/docks/reset.cpp`, `mainwindow/layout_persistence.cpp`, `docs/DOCK_LAYOUT.md` | enforced |
| FRB-ARCH-001 | Keep UI, docking state, and core logic separated by module boundaries | https://doc.qt.io/qt-6.5/model-view-programming.html, https://kdab.github.io/KDDockWidgets/ | `mainwindow/*`, `app/*`, `core/*`, `ui/docking/dockmanager*`, `ui/widgets/*` | partial |
| FRB-KDD-001 | Avoid unsupported broad dock styling paths on KDD internals | https://kdab.github.io/KDDockWidgets/custom_styling.html | `mainwindow/theme.cpp`, `ui/docking/dockwidget.cpp` | enforced |
| FRB-KDD-002 | Use public KDD APIs only; avoid private-handle reinterpret casts | https://kdab.github.io/KDDockWidgets/installation_and_usage.html, https://github.com/KDAB/KDDockWidgets/tree/main/examples | `ui/docking/kdockwidget.*`, `ui/docking/dockbackend.*`, `mainwindow/docks.cpp`, `mainwindow/docks/reset.cpp`, `mainwindow/docks/connectivity.cpp`, `mainwindow/docks/overlay.cpp`, `mainwindow/layout_persistence.cpp`, `ui/docking/dockmanager.cpp` | enforced |
| FRB-STRUCT-001 | File size target <=600 lines, exception required >1000 | https://github.com/qt-creator/qt-creator, https://doc.qt.io/qtcreator-extending/coding-style.html | whole repo | partial |
| FRB-CI-001 | Advisory static-analysis and structure checks in CI | https://github.com/KDAB/KDDockWidgets/tree/main/tests | `.github/workflows/`, `tools/ci/`, `tools/dev/` | partial |

## Project Inspiration Links

- Qt Creator:
  - https://github.com/qt-creator/qt-creator
  - https://doc.qt.io/qtcreator-extending/coding-style.html
- KDDockWidgets:
  - https://github.com/KDAB/KDDockWidgets
  - https://github.com/KDAB/KDDockWidgets/tree/main/examples
  - https://github.com/KDAB/KDDockWidgets/tree/main/tests
- NVIDIA Nsight + KDD:
  - https://www.kdab.com/nvidia-uses-kddockwidgets-in-nsight/
