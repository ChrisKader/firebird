# CX II CAS Power / PMU / ADC Code Audit

This file is a full-path audit for CX II CAS power handling.
It intentionally covers emulator code, boot-stage firmware paths, and runtime MMIO evidence.

## Scope

Analyzed firmware images:

- `data/bootloader.bin` @ `0x11200000`
- `data/osloader.bin` @ `0x13200000`
- `data/diags.bin` @ `0x13200000`
- `data/TI-Nspire.bin` @ `0x10000000`

Disassembly artifacts used:

- `/tmp/bootloader.S`
- `/tmp/osloader.S`
- `/tmp/diags.S`
- `/tmp/os.S`

Runtime trace used:

- `/private/tmp/mmio.log`

## 1) Emulator Address Map (authoritative)

`/Users/ck/dev/firebird/core/mem.c` maps CX II power-related MMIO as:

- `0x900B0000` (`apb_set_map(0x0B, adc_cx2_read_word, adc_cx2_write_word)`) -> FTADCC010 model in `/Users/ck/dev/firebird/core/misc.c`
- `0x90100000` (`apb_set_map(0x10, tg2989_pmic_read, tg2989_pmic_write)`) -> PMIC ID/status model in `/Users/ck/dev/firebird/core/cx2.cpp`
- `0x90140000` (`apb_set_map(0x14, aladdin_pmu_read, aladdin_pmu_write)`) -> Aladdin PMU model in `/Users/ck/dev/firebird/core/cx2.cpp`
- `0x90020000` (`apb_set_map(0x02, serial_cx_read, serial_cx_write)`) -> UART1 path in `/Users/ck/dev/firebird/core/serial.c`
- `0x90070000` (`apb_set_map(0x07, serial_cx2_read, serial_cx2_write)`) -> UART2 path in `/Users/ck/dev/firebird/core/serial.c`

## 2) Emulator Power/ADC/Charge Code Paths

### 2.1 FTADCC010 (`0x900Bxxxx`) in `/Users/ck/dev/firebird/core/misc.c`

Explicitly handled control/sample offsets:

- Sample bank: `0x000..0x01C`
- Trigger/control: `0x100`, `0x104`, `0x108`, `0x10C`, `0x110`, `0x114`, `0x118`, `0x11C`

Implemented behavior:

- Trigger recognition for `0x00070111`, `0x00071100`, and bit0-launch
- W1C behavior on `0x108/0x10C` bit0
- Periodic background completion when `0x118 bit0` enabled
- IRQ mirror on both `INT_ADC` and raw IRQ 13
- PMU pending mirror via `aladdin_pmu_set_adc_pending(true)`

Battery/charger sample composition currently modeled:

- `0x900B0000`, `0x900B000C`, `0x900B001C`: battery code (all normal polarity, higher mV = higher code)
- `0x900B0004` and `0x900B0010`: VREF channel (704)
- `0x900B0008` and `0x900B0014`: auxiliary VREF channel (696)
- `0x900B0018`: mixed status slot (`[17:16]` charger state + low 10-bit VBUS/sample)

### 2.2 PMIC (`0x901000xx`) in `/Users/ck/dev/firebird/core/cx2.cpp`

Explicitly modeled:

- `0x90100004`: TG298x ID/status decode source for DIAGS banner

Model policy:

- bit0 ready asserted
- model bucket set to TG2985
- sign clear to select `...E` variant path

### 2.3 Aladdin PMU (`0x901400xx` and `0x901408xx`) in `/Users/ck/dev/firebird/core/cx2.cpp`

Explicit PMU base behavior:

- `+0x00` wakeup reason
- `+0x20` disable0 read synthesis (`battery-present` and external-source bits)
- `+0x24` interrupt state
- `+0x30` clock register and clock-rate recompute
- `+0x50` disable1 pass-through
- `+0x60` disable2 battery/charger field synthesis with non-owned bits preserved
- `+0xC4` int-enable

Explicit PMU extended window behavior:

- `+0x808` efuse value
- `+0x80C` user flags
- `+0x810` ON-key/flags register mapping
- `+0x850` irq mask
- `+0x854` irq pending + ADC background step integration
- `+0x858` USB PHY status (`0` vs `0x3C`)

All other `0x901408xx` offsets are retained in `noidea[]` pass-through storage.

### 2.4 Charger-state source bug fixed

A concrete logic bug was present: `charger_state_override` defaulted to `CHARGER_DISCONNECTED`, which made the override path always active.

Fix applied:

- Added `CHARGER_AUTO = -1` in `/Users/ck/dev/firebird/core/emu.h`
- Default changed to `CHARGER_AUTO` in `/Users/ck/dev/firebird/core/misc.c`
- UI "override off" now restores `CHARGER_AUTO` in `/Users/ck/dev/firebird/debugger/hwconfig/hwconfigwidget.cpp`

Effect:

- With override off, charge state now follows `usb_cable_connected_override` or live `usblink_connected` as intended.

## 3) Firmware Paths Validated (not guessed)

### 3.1 PMIC ID decode in DIAGS (`0x90100004`)

`/tmp/diags.S` around `0x13227AA0`:

- loads base literal `0x90100000`
- reads `[base + 0x04]` twice
- decodes `bits[24:20]` as model bucket
- uses sign/variant logic to pick PMIC suffix

This is the direct evidence for the PMIC banner path.

### 3.2 ADC launch handshake (bootloader/osloader/diags)

Common routine pattern (example bootloader `0x11201C98`, osloader `0x13201C98`, diags `0x13201544`):

- base literal `0x900B0000`
- sets status bits on `+0x108` and `+0x10C`
- writes launch command to `+0x100`
  - `0x00070111` in one path
  - `0x00071100` in another path

This exactly matches runtime writes in `mmio.log`.

### 3.3 Conversion math path used by command `0x3EA`

Bootloader routine (`/tmp/bootloader.S`, `0x112024CC`) computes converted voltage from raw sample + calibration domain:

- fetch raw via helper at `0x11202468`
- multiply by channel coefficient and divide by 1000
- multiply by mode constant (`0x098B` or `0x0C99`)
- normalize against calibration endpoints (`[+4]`, `[+8]` in table)
- add base offset (`0xBB8` total via `+0xBB0` then `+8`)

This confirms that firmware is not using a trivial direct raw->mV mapping.

### 3.4 Low-battery gate constants

Bootloader low-battery decision path (`0x11238EDC` onward):

- reads channels through `0x3EA` flow
- compares against:
  - `0xCE3` (3299 mV threshold)
  - `0xBB7` (2999 mV related threshold)

This is the direct source of `"VSys is below 3300!"` behavior.

### 3.5 PMU bitfield operations on `0x90140050/0x90140060`

Bootloader/osloader/diags all contain helper routines around `0x...E09C` that:

- choose between `0x90140050` and `0x90140060`
- set/clear bitmasks derived from lookup bytes
- mirror status into `0x90140000 + 0x04` and `+0xC4`
- use `0x90140008` and `0x90140818` in associated control paths

This confirms power state is managed across multiple PMU words, not only one battery register.

## 4) Runtime MMIO Coverage (`/private/tmp/mmio.log`)

Observed address coverage and access counts:

- `900b0000 R=726 W=0`
- `900b0004 R=726 W=0`
- `900b0008 R=726 W=0`
- `900b000c R=726 W=0`
- `900b0010 R=726 W=0`
- `900b0014 R=726 W=0`
- `900b0018 R=726 W=1`
- `900b001c R=630 W=0`
- `900b0100 R=3 W=795`
- `900b0104 R=0 W=726`
- `900b0108 R=1452 W=1452`
- `900b010c R=3036 W=2310`
- `900b0110 R=0 W=3`
- `900b0114 R=0 W=3`
- `900b0118 R=0 W=3`
- `900b011c R=0 W=3`
- `90140000 R=3 W=1`
- `90140004 R=0 W=1`
- `90140008 R=100 W=100`
- `90140020 R=888 W=898`
- `90140024 R=168 W=168`
- `90140030 R=8513 W=168`
- `90140050 R=363 W=363`
- `90140060 R=387 W=387`
- `901400c4 R=0 W=1`
- `90140800 R=8 W=9`
- `90140804 R=3 W=3`
- `90140808 R=29 W=0`
- `9014080c R=49 W=0`
- `90140810 R=2098 W=0`
- `90140814 R=1 W=1`
- `90140820 R=3 W=3`
- `90140828 R=3 W=3`
- `9014083c R=8 W=8`
- `90140840 R=32 W=32`
- `90140850 R=0 W=1460`
- `90140854 R=730 W=2190`
- `90140858 R=30624 W=0`

Key value facts from this run:

- `90140854` read mostly `0x08000000` (ADC pending bit behavior is active)
- `90140858` read mostly `0x00000000`, sometimes `0x0000003C`
- `900b0100` writes are primarily `0x00070111` with secondary `0x00071100`
- `90140060` traffic includes variants `0x00008c00`, `0x00048c00`, `0x00108c00`, `0x0001e800`

### 4.1) Latest regression capture: stuck at `Driver initialization complete.`

The latest `mmio.log` from the stalled boot path has this profile:

- ADC sample-bank reads are present but shallow:
  - `0x900B0000..0x900B0018`: `94` reads each
  - `0x900B001C`: `30` reads
  - `0x900B0018`: `1` write (`0x08002A4D`)
- ADC control loop is active:
  - `0x900B0100`: `101` writes
  - `0x900B0104`: `94` writes
  - `0x900B0108`: `188` reads / `189` writes
  - `0x900B010C`: `386` reads / `292` writes
- PMU IRQ loop is active:
  - `0x90140850`: `194` writes
  - `0x90140854`: `97` reads / `291` writes
- No USB PHY-status reads in this run:
  - `0x90140858`: `0` reads
- PMU status values are static in this run:
  - `0x90140020`: reads and writes are only `0x00000400`
  - `0x90140810`: reads are only `0x00000111`
  - `0x90140060`: values observed are `0x0000BA80`, `0x0004BA80`, `0x0010BA80`, `0x0018BA80` (+ rare transients)

Current decision from this regression:

- Keep ADC status latch/reset on bit0-only (`0x108/0x10C = 1`) instead of forcing `0x3`.
- Reason: forcing bit1 globally regressed some runs back to the
  `Driver initialization complete.` stall.

## 5) Current Modeling Gaps (explicit)

These offsets are touched and stored, but semantics are still mostly pass-through:

- `0x90140004`
- `0x90140804`
- `0x90140814`
- `0x90140820`
- `0x90140828`
- `0x9014083C`
- `0x90140840`

They are not currently hard-failing, but not fully behavior-modeled either.

## 6) Why this matters for the current battery-percent issue

The remaining battery/UI mismatch is likely not a single register bug anymore.
The firmware uses:

- ADC sample relationships (multi-channel)
- conversion math with calibration endpoints
- PMU state words (`0x50/0x60` and control around `0x08`, `0x40818`, pending flow)

So the next changes must be made against this full path, not one offset at a time.

## 7) Reproduction / verification commands

Commands used for this audit:

- `arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x11200000 data/bootloader.bin > /tmp/bootloader.S`
- `arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x13200000 data/osloader.bin > /tmp/osloader.S`
- `arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x13200000 data/diags.bin > /tmp/diags.S`
- `arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x10000000 data/TI-Nspire.bin > /tmp/os.S`
- `rg` scans over `/tmp/*.S` and `/private/tmp/mmio.log`
