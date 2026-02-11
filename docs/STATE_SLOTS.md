# State Slot System

This document describes Firebird's quick save/load slots exposed under `State -> Save to Slot` and `State -> Load from Slot`.

## Slot IDs

- Supported slots: `1` through `9`.
- Save shortcuts: `Ctrl+1` .. `Ctrl+9`.
- Load shortcuts: `Ctrl+Shift+1` .. `Ctrl+Shift+9`.

## Storage Path Resolution

Slot files are resolved by `stateSlotPath(int slot)` in `mainwindow.cpp`.

Resolution order:

1. If the active kit has a snapshot path configured, slots are stored in that snapshot directory.
2. Otherwise, slots fall back to `QStandardPaths::AppDataLocation`.

Final filename format:

- `slot_1.fbsnapshot`
- ...
- `slot_9.fbsnapshot`

## Behavior Notes

- Slot saves/loads use the same suspend/resume snapshot mechanisms as manual snapshot actions.
- Slots are intentionally per-kit when a kit snapshot path exists.
- If no kit snapshot path exists, slots remain available via the app data fallback location.
