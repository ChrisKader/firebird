# State Slots

Firebird supports numbered quick-save slots via the **State** menu.

## Slot files

- Slot format: `slot_<N>.fbsnapshot`
- Typical slot numbers: `1..9`

## Storage location

Slot location depends on the active kit configuration:

- If the active kit has a snapshot path configured, slot files are created in that snapshot's directory.
- If no kit snapshot path exists, slot files are created in `QStandardPaths::AppDataLocation`.

This allows quick-save/load to work both for configured kits and ad-hoc sessions.

## Behavior

- Save to slot: writes the current emulator state into `slot_<N>.fbsnapshot`.
- Load from slot: resumes from `slot_<N>.fbsnapshot` if present.
- Empty slot: if the slot file does not exist, the UI reports that the slot is empty.

## Related code

- Path computation: `mainwindow.cpp` (`stateSlotPath`)
- Slot save/load actions: `MainWindow::saveStateSlot`, `MainWindow::loadStateSlot`
