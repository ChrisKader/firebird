# KDD Layout Validation Matrix

This document tracks Phase 5 integration coverage for KDDockWidgets layout behavior.

## CI Matrix

- Workflow: `.github/workflows/kdd-layout-validation.yml`
- OS coverage:
  - Linux (`ubuntu-24.04`)
  - macOS (`macos-14`)
  - Windows (`windows-2022`)
- Build mode:
  - `FIREBIRD_ENABLE_KDDOCKWIDGETS=ON`
- Test execution:
  - `ctest --test-dir build-kdd --output-on-failure`

## Covered Automated Scenarios

- Layout save/restore roundtrip (`LayoutSaver::serializeLayout` + `restoreLayout`).
- Profile switching behavior (restore profile A, restore profile B).
- File-based save/restore (`saveToFile` + `restoreFromFile`).
- Corrupted layout payload rejection.
- Relative restore path for display/multi-monitor transitions (`RestoreOption_RelativeToMainWindow`).

## Manual Validation Checklist

- Start app with two monitors connected, move floating docks between monitors, restart, and verify persisted placement is sane.
- Unplug secondary monitor after saving layout and restart; verify recovery without inaccessible off-screen docks.
- Corrupt a profile JSON (`AppConfigLocation/layouts/*.json`) and verify:
  - backup file is created as `*.corrupt.<timestamp>.json`
  - app falls back to valid settings/default layout
  - no crash on startup.
- Toggle between `default`, `debugging`, `widescreen`, and `custom` profiles and verify debug dock custom states persist.
