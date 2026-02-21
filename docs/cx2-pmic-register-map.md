# CX II PMIC Register Map (WIP)

This is a reverse-engineered map for CX II PMIC-related bring-up.
Sources: `data/diags.bin`, `data/osloader.bin`, runtime MMIO traces.

Companion full-path audit:
`/Users/ck/dev/firebird/docs/cx2-power-code-audit.md`

## 1) Direct MMIO block at `0x90100000` (TG2989 PMIC)

Observed direct register use in DIAGS:

| Offset | Access | Meaning (current confidence) | Evidence |
| --- | --- | --- | --- |
| `0x04` | `R32` (and tolerant `W32`) | PMIC ID/status word. `bits[24:20]` select PMIC model bucket (`0/2 => TG2989`, `1 => TG2985`, other => fallback). `bit31` sign controls a variant suffix path in banner formatting. `bit0` is treated as a ready/poll bit. Current emulator policy sets model=`1` and keeps sign clear to report `TG2985E` on targeted CX II images. | `diags.bin` function around file offset `0x27aa0` reads `[0x90100000 + 0x04]` twice and decodes these fields. |

Direct reads/writes to other `0x901000xx` offsets were not found in boot ROM handoff stages currently analyzed.

## 2) PMIC command ID interface (not APB register offsets)

Bootloader/OSLoader/DIAGS also use a PMIC command layer with IDs around `0x3E9..0x400`.
These IDs are passed through firmware message wrappers (for example, DIAGS around `0x8560`, OSLoader around `0x8cb4`).

| Command ID | Observed wrapper behavior | Width |
| --- | --- | --- |
| `0x3E9` | read-like wrapper and 2-arg wrapper variant | `u16` / args |
| `0x3EA` | read-like wrapper and 2-arg wrapper variant | `u16` / args |
| `0x3EB` | read-like wrapper | `u32` |
| `0x3EC` | read-like wrapper | `u16` |
| `0x3ED` | read-like wrapper | `u32` |
| `0x3EE` | write-like wrapper with 1 arg | arg |
| `0x3EF` | write-like wrapper with 1 arg | arg |
| `0x3F5` | read-like wrapper | `u16` |
| `0x3FA` | write-like wrapper with 1 arg | arg |
| `0x3FB` | read-like wrapper | `u8` |
| `0x3FC` | write-like wrapper with no explicit arg payload | op |
| `0x3FD` | read-like wrapper | `u32` |
| `0x3FE` | read-like wrapper | `u32` |
| `0x3FF` | write-like wrapper with 1 arg | arg |
| `0x400` | read-like wrapper | `u32` |

Semantics of each command ID are still partially unknown; this table is based on call signatures and return widths only.

## 3) Emulator implications

- The single proven direct PMIC register required for boot diagnostics is `0x90100004`.
- Keeping `bit0=1` and a stable model code in `bits[24:20]` avoids PMIC init/read failures.
- Additional PMIC behavior in CX II firmware likely flows through the command ID path above, not direct APB accesses.

## 4) Battery/ADC behavior used by CX II boot flow

The CX II bootloader low-battery decision is not a direct read of slider mV.
It uses PMIC command `0x3EA` conversions (via firmware wrappers) and compares converted values.

Observed bootloader check behavior:

- Channel 3 and channel 7 are read through `0x3EA`.
- The low-battery warning path triggers when channel 3 conversion is `<= 3299` (`0xCE3` threshold, effectively "below 3300").
- The warning string is `VSys is below %d!  VSys:%d VSled:%d!`.
- Disassembly evidence (`bootloader.bin`, base `0x11200000`):
  - `0x11238EDC`: reads channel `3` then channel `7` (calls through `0x11201C30`).
  - `0x11238F44`: literal `0xCE3` threshold compare for low-battery gate.
  - `0x11238F50`: literal `0xBB7` threshold compare used in VBUS-present path.

Practical implication for emulation:

- Feeding a mismatched raw domain can produce impossible results such as `4200mV` override yielding `VSys:2594` (this was caused by an earlier inverse-polarity bug, now fixed).
- PMU+`0x60` battery field must use the same normal polarity as the ADC sample bank.

## 5) Register/field behavior currently modeled

### `0x900B0000` (FTADCC010 sample bank)

Current model exposes three logical channels in the first sample window:

- `0x900B0000`, `0x900B000C`, `0x900B001C`: battery channels (all normal polarity)
- `0x900B0004` / `0x900B0010`: reference channel (VREF = `0x2C0` / 704)
- `0x900B0008` / `0x900B0014`: auxiliary reference channel (VREF_aux = `0x2B8` / 696)
- `0x900B0018`: mixed VBUS/status slot (seen firmware-write of `0x08002A4D`)
  - High control bits are preserved from firmware writes (masked to control domain).
  - Low 10 bits are live VBUS ADC code.
  - Charger-state bits `[17:16]` are recomposed from emulator charger state.
  - Reads of other sample slots are masked to 10-bit ADC width; `0x18` keeps status bits.

Battery code mapping (current implementation):

- Input: `battery_mv_override` clamped to `[3000, 4200]`
- All battery channels use normal polarity (higher mV = higher code): linear `[0x291, 0x397]`
  - 3000 mV -> `0x291` (657)
  - 4200 mV -> `0x397` (919)
- Conversion formula: `VSys_mV = battery_code * 3225 / vref_code`
  - Confirmed by bootloader UART: code 370 -> 1691 mV (370 * 3225 / 704 = 1695)
- Battery codes exceed VREF at normal charge levels; the firmware handles this

Important pitfall:

- Do not freeze `0x900B0018` low bits to firmware-programmed values only; VBUS detection and charger transitions need a live low-10-bit ADC domain.

### `0x90140060` (Aladdin PMU status/battery field)

Current emulation semantics:

- Bits `[15:6]`: 10-bit battery field.
  - Sourced from `adc_cx2_effective_battery_code()` (normal polarity, same
    domain as ADC sample bank channels).
- Bits `[17:16]`: charger state
  - `00`: disconnected
  - `01`: connected (idle)
  - `11`: charging
- Bits outside `[17:16]` and `[15:6]` are preserved from firmware writes.

## 6) Override semantics and UI notes

- Battery override should be considered active from `battery_mv_override` for CX II flows.
- Legacy raw override (`adc_battery_level_override`) is retained for compatibility/UI persistence, but PMU/ADC CX II model uses the mV path.
- USB connect/disconnect in the UI affects cable-link behavior and may interact with charger state reporting in OS status pages.
- Charger-source precedence (current implementation):
  - explicit charger-state override (if set)
  - explicit USB-cable override
  - battery override fallback (`adc_charging_override`)
  - live USB link state

## 7) Regression checklist

Use this quick validation after battery/PMU changes:

1. Set battery override to `4200mV`, charger state `Disconnected`.
2. Cold boot CX II bootloader.
3. Verify low-battery warning does not print:
   - no `VSys is below 3300!` during normal boot at full battery.
4. In OS status screens:
   - battery state should not be permanently stuck at `100%` with charge icon.
   - toggling USB connect/disconnect should update charge icon/state coherently.
5. In DIAGS (if available), verify `v1/v2/vr` remain finite and track slider direction.

## 8) Relevant emulator implementation files

- `core/peripherals/misc.c` (CX II ADC sample generation, battery code mapping)
- `core/soc/cx2.cpp` (Aladdin PMU register model, `0x90140060` composition)
- `ui/widgets/hwconfig/hwconfigwidget.cpp` (battery/charger override UI wiring)

## 9) Stage-specific expectations (CX II CAS)

This section captures what should happen per boot stage when the ADC/PMU model
is working as intended.

Boot ROM stage:

- Must pass early driver init and load Boot Loader image.
- Should not stall at `Driver initialization complete.` due ADC timeout loops.

Boot Loader stage:

- `ADC FTADCC010` init should complete.
- Repeated `ADC timeout!` lines indicate conversion completion/IRQ path trouble.
- Full-battery override (`4200mV`) should not produce:
  - `VSys is below 3300!`
  - `VSys:2594`-style low values.

DIAGS stage:

- `v1/v2/vr` should remain finite (no `INF`).
- `v1` should track slider direction on channel-0-visible path.
- Power transitions should not force immediate `DRV POWER OFF case...` from low-battery misreads.

OS stage:

- Battery percent should not be permanently `--%` or locked at `100%`.
- USB connect/disconnect should update charge icon coherently.
- Charge icon should not remain latched after disconnect when charger state is disconnected.

## 10) Current register behavior matrix

`0x900B0000..0x001C`:

- `0x00`, `0x0C`, `0x1C`: battery code (all normal polarity, higher mV = higher code)
- `0x04`, `0x10`: reference channel (`vref`)
- `0x08`, `0x14`: auxiliary reference channel (`vref_aux`)
- `0x18`: mixed status/data slot
  - `bits[9:0]`: live VBUS ADC code
  - `bits[17:16]`: charger state (`00`/`01`/`11`)
  - high control bits: preserved from firmware writes in allowed mask domain

`0x90140060`:

- `bits[15:6]`: PMU battery field sourced from normal-polarity battery code
- `bits[17:16]`: charger state
- all other bits preserved from firmware writes

`0x90140020`:

- `0x400`: battery-present (kept asserted so status screen can compute `%`)
- `0x100`: external source/USB present (set only when charger state is not disconnected)
- bit 1 (`0x2`): sleep trigger (write-only behavior; see section 16)

## 11) Known historical failure signatures and causes

`VSys is below 3300!  VSys:2594 VSled:2594!` at high override:

- Cause: inverse polarity bug — battery code decreased as mV increased, producing low conversion results at high override values.
- Fix: all battery channels now use normal polarity (higher mV = higher code).

`VBUS(...) was Present` in disconnected mode:

- Cause: frozen or stale `0x900B0018` low 10-bit VBUS domain.
- Mitigation: keep `0x18` low bits live and recomputed from charger/VBUS model.

OS shows `--%` or inconsistent icon transitions:

- Cause: PMU battery/charger bits not coherent with ADC domain and charger-state signaling.
- Mitigation: keep `0x90140060` and ADC channel model synchronized.

Immediate/frequent `ADC timeout!` loops:

- Cause: conversion status/IRQ completion path not firing as expected.
- Mitigation: ensure background conversion completion and pending-bit handling match boot flow.

## 12) Debug capture recipe

Use MMIO tracing when validating ADC/PMU changes:

1. Launch with `FIREBIRD_MMIO_TRACE=1`.
2. Optional: add `FIREBIRD_MMIO_TRACE_PC=1` to include PCs in trace lines.
3. Capture UART plus MMIO for the same run.
4. Correlate these addresses first:
   - `0x900B0000..0x900B001C`
   - `0x90140020`, `0x90140060`
   - `0x90140850`, `0x90140854`, `0x90140858`
   - `0x90100004`

Trace notes:

- MMIO trace is capped (see `core/memory/mem.c`), so capture from cold boot for most value.
- Always report override settings (mV + charger state + USB link state) with logs.

## 13) CX II Sleep/Wake Mechanism

The CX II deep sleep and wake path works as follows on real hardware:

### Sleep Entry

Firmware writes bit 1 (`0x2`) to PMU+0x20, which triggers deep sleep. The emulator
responds by:

1. Releasing all held keys (`keypad_release_all_keys()`)
2. Setting `EVENT_SLEEP` to park the CPU loop
3. Clearing timer scheduler events (`SCHED_TIMERS`, `SCHED_TIMER_FAST`)
4. Calling `aladdin_pmu_reset()` so the bootrom sees correct clock/PMU state on wake

The PMU reset is critical: without it, the bootrom clock configuration reads stale
values and fails to bring the system back up correctly.

Non-sleep writes to PMU+0x20 (without bit 1) are stored normally as `disable[0]`.

### Wake via ON Key

When the ON key is pressed during sleep:

1. `aladdin_pmu_on_key_wakeup()` latches wake cause:
   - Sets `wakeup_reason = 0x040000` (ON key wake)
   - Latches `int_state |= PMU_INT_WAKE_BIT` (PMU+0x24 bit 1)
   - Latches `PMU_IRQ_ONKEY_BIT` in pending bitmap (PMU+0x854)
   - Suppresses `INT_POWER` during wake to avoid vectoring into uninitialized handlers
2. CPU is woken by a **full reset** through the bootrom/fastboot path:
   - `EVENT_SLEEP` is cleared
   - `timer_cx_reset()` fully resets timer state
   - `cpu_reset()` triggers a soft reset starting from the reset vector (address 0)
3. The bootrom reads PMU+0x00 (`wakeup_reason`), finds the ON key wake signature
   (`0x040000`), locates fastboot data saved by the OS before sleep (at `0x90030000`),
   validates it, and jumps back to OS code in SDRAM

This is a full CPU reset, not an in-place resume from WFI. The OS saves enough
state before sleep that the bootrom fastboot path can restore execution seamlessly.

### Important Notes

- Do not attempt to wake by resuming CPU execution in-place (e.g. restarting timers
  and setting `cycle_count_delta`). Timer interrupts may not be enabled during deep
  sleep, and the firmware expects to go through the bootrom wake path.
- `aladdin_pmu_reset()` must be called during sleep entry, not during wake. The
  bootrom needs clean PMU state to configure clocks correctly.
- `INT_POWER` must be suppressed during deep sleep wake to prevent the interrupt
  from firing into default/uninitialized handlers before the bootrom has set up
  the system.

## 14) Current assumptions and open work

What is modeled now:

- Direct PMIC identity register at `0x90100004` for TG2985E detection path.
- ADC sample bank and PMU field coherence sufficient for boot/OS bring-up experiments.

What remains partially reverse-engineered:

- Full PMIC command semantics for IDs `0x3E9..0x400`.
- Exact calibration table lifetime/source used by all firmware variants.
- Product-image differences between bootloader, osloader, DIAGS, and OS battery UI code paths.

## 15) Offset coverage snapshot (documented live)

This section is a running capture of what firmware actually touches, so changes
are guided by complete coverage instead of single-register guesses.

Firmware-referenced address families (bootloader/osloader/diags disassembly):

- ADC block: `0x900B0000`, `0x900B000C`, `0x900B0018` plus control window under `0x900B0100`.
- PMU block: `0x90140000`, `0x90140008`, `0x90140020`, `0x90140050`, `0x90140060`.
- PMU extended window: `0x90140800`, `0x90140804`, `0x90140808`, `0x9014080C`,
  `0x90140810`, `0x90140814`, `0x90140818`, `0x90140820`, `0x90140828`,
  `0x9014083C`, `0x90140840`, `0x90140850`, `0x90140854`, `0x90140858`.
- PMIC block: `0x90100000` family (notably `0x90100004` identity/status).
- UART block used heavily in DIAGS text output: `0x90020000` family.

MMIO-observed hot offsets (from `/private/tmp/mmio.log`):

- `0x90020018`: 110,989 accesses (UART status polling)
- `0x90140858`: 44,012 accesses (USB/PHY status polling)
- `0x90140030`: 9,240 accesses (clock register)
- `0x900B010C`: 5,705 accesses (ADC status)
- `0x900B0108`: 3,116 accesses (ADC status)
- `0x90140854`: 3,116 accesses (PMU IRQ pending read/clear flow)
- `0x90140020`: 1,888 accesses
- `0x90140810`: 2,255 accesses
- `0x90140060`: 840 accesses
- `0x900B0000..0x900B001C`: 679-776 accesses per sampled slot

### 16.1) `0x90140020` observed patterns

Read values:

- `0x10000400` (454)
- `0x00000400` (278)
- `0x10000500` (209)

Write values:

- `0x10000400` (409)
- `0x10000500` (190)
- `0x00000400` (180)
- `0x10000100` (168)

Interpretation:

- Firmware actively uses upper control bits (not just low power-source bits).
- Any synthesized read must preserve firmware-owned high bits while fixing
  battery/source presence bits used by UI.

### 16.2) `0x90140060` observed patterns

Read values (top):

- `0x0000E800` (322)
- `0x0001C780` (36)
- `0x0001D440` (14)
- `0x00018C00` (14)
- `0x0000D440` (12)
- `0x0000C780` (12)

Write values (top):

- `0x0000E800` (140)
- `0x0004E800` (87)
- `0x0010E800` (46)
- `0x0000E880` (40)
- `0x0001C780` (28)

Interpretation:

- Firmware writes this register with many mode-dependent combinations.
- Emulator must preserve firmware-controlled non-battery bits and only override
  explicitly modeled fields (battery/charger) deterministically.

### 16.3) `0x900B0018` observed patterns

Read values:

- `0x000029C0` (585)
- `0x00012AC0` (160)
- `0x00032B30` (30)

Write values:

- `0x08002A4D` (1)

Interpretation:

- This confirms `0x900B0018` is not a plain 10-bit ADC sample:
  - low 10 bits vary by VBUS code (`0x1C0`, `0x2C0`, `0x330`);
  - charger-state bits (`[17:16]`) vary (`00`, `01`, `11`);
  - firmware control bits around `0x2A00` are retained.

### 16.4) PMU IRQ / USB status path

- `0x90140850` written with `0xFFFFFFFF` repeatedly (IRQ mask setup).
- `0x90140854` written with `0xFFFFFFFF` then `0x00000000` (pending clear flow).
- `0x90140854` reads mostly `0x08000000` (ADC pending bit path active).
- `0x90140858` reads mostly `0x00000000`, occasionally `0x0000003C` when USB path is enabled.

This path is critical for avoiding false ADC timeout behavior and for coherent
charger/USB icon updates.

### 16.5) ADC control-window handshake (`0x900B0100` family)

Observed recurring pattern from MMIO:

- Init writes:
  - `0x900B0110 <- 0x00000960`
  - `0x900B0114 <- 0x00020201`
  - `0x900B0118 <- 0x00000001`
  - `0x900B011C <- 0x00000002`
- Repeating trigger loop:
  - `0x900B0100 <- 0x00070111` (common) or `0x00071100` (periodic)
  - `0x900B0104 <- 0x00000001`
  - poll/clear `0x900B010C` and `0x900B0108` with W1C writes of `0x1`
  - write `0x2` to `0x900B010C`/`0x900B0108` as part of firmware handshake.

Modeling implications:

- Both `0x100` and `0x104` trigger values must latch conversion completion.
- Completion must set `0x108/0x10C` bit0 and update ADC/PMU pending signaling.
- `0x108/0x10C` bit0 must behave as W1C to avoid timeout loops.
- Do not force bit1 high at reset/completion globally; that regressed some
  runs to stall at `Driver initialization complete.`.

### 16.6) PMU extended defaults used by firmware probes

- `0x9014080C` is read as `0x00100000` in current traces and should default to
  that value at reset for stable probe behavior.

### 16.7) Latest stuck-at-init trace snapshot

From the latest `/private/tmp/mmio.log` capture while stalled at
`Driver initialization complete.`:

- `0x900B0000..0x900B0018`: `R=94` each, `0x900B001C`: `R=30`
- `0x900B0100`: `W=101`, `0x900B0104`: `W=94`
- `0x900B0108`: `R=188/W=189`, `0x900B010C`: `R=386/W=292`
- `0x90140850`: `W=194`, `0x90140854`: `R=97/W=291`
- `0x90140858`: `R=0` (USB/PHY branch not exercised in this run)
- `0x90140020`: only `0x00000400` observed on both reads/writes
- `0x90140810`: only `0x00000111` observed on reads

This confirms the stall run is mostly an ADC status handshake loop with limited
sample-bank depth and no USB/PHY polling path.

## 16) Live Disassembly Addendum (2026-02-20)

Disassembler: `arm-none-eabi-objdump -D -b binary -m arm`

### 16.1 Cross-stage PMU init sequence (TI-Nspire/OSLoader/DIAGS)

Confirmed blocks:
- `TI-Nspire.bin` near `0x1408`
- `osloader.bin` near `0xE018`
- `diags.bin` near `0xD8C4`

Behavior:
- `0x90140850 <- 0xFFFFFFFF`
- `0x90140854 <- 0xFFFFFFFF`
- `0x90140854 &= ~0x08000000`
- `0x90140020 |= 0x400`

### 16.2 Shared selector for PMU pair `0x90140050/0x90140060`

Literal pairs found in all three binaries:
- TI-Nspire: `0x14ac/0x14b0`, `0x1560/0x1564`
- OSLoader: `0xE0B4/0xE0B8`, `0xE14C/0xE150`
- DIAGS: `0xD960/0xD964`, `0xD9F8/0xD9FC`

Selector logic tests bit7 of a lookup byte and chooses one base.

### 16.3 Confirmed high-halfword bit mutators on PMU words

Shared helper code:
- reads `[base + 0x50]`
- gets bit index from `[base + key + 0x1d]`
- clear path: drops low 16 then `bic (1 << idx)`
- set path: ORs `(1 << idx)` then keeps high-halfword domain

Representative TI-Nspire instructions:
- `ldr r3, [r0, #0x50]`
- `ldrb ip, [r2, #0x1d]`
- `lsr/lsl #16` normalization
- `bic/orr ... , 1<<idx`

### 16.4 `0x90140020` literal pool usage in TI-Nspire

Confirmed at literal pools around:
- `0x009b8a7c`
- `0x009b8d00`

Both pools include:
- `0x9014080c`
- `0x90140020`

### 16.5 TI-Nspire table entry writing `0x900b0018`

At `0x0042c938`:
- literal address `0x900b0018`
- paired value `0x08002a4d`

This is direct evidence of table-driven writes into ADC slot `+0x18`.

### 16.6 Read-side PMU status query helper (`0x3EB` command path)

Dispatcher block (`0x1BB4..0x1BE4`) compares command ID with `0x3EB` and calls helper `0x0B70`.

Helper `0x0B70` semantics:
- `sel = *(table_base + index)`
- `sel == 0xFF` => returns `1`
- `sel & 0x80 == 0` => read `0x90140060`, test bit `sel`, return inverted boolean
- `sel & 0x80 != 0` => read `0x90140050`, test bit `(sel & 0x7F)`, return inverted boolean

So these status bits are consumed as active-low booleans by the TI-OS command layer.

### 16.7 Read-side helper checks on `0x90140858` / `0x90140810`

Confirmed helper reads:
- `0x0BC8`: `(REG_0x90140858 & 0x0C) == 0x0C`
- `0x0BE8`: `(REG_0x90140858 & 0x30) == 0x30`
- `0x0C90`: `(REG_0x90140810 & 0x100) >> 8`

These are read consumers and are more relevant to runtime power/charge reporting than init-time all-ones writes.

### 16.8 Command handlers that use `0x90140000` as runtime state

TI-Nspire command dispatcher region (`0x17cc..0x1ba4`) includes:

- `0x3EF` path -> helper `0x0C14`:
  - loads `REG_0x90140000`
  - walks selector bytes from `(table + 0x1d + i)`
  - tests `1 << idx` against that register and returns a derived index/status
- `0x3F0` path -> helper `0x15D4`:
  - reads selector byte `(table + index + 0x1d)`
  - sets `1 << idx` into `REG_0x90140000`

Implication for emulation:

- `0x90140000` must be modeled as read/write live state.
- Treating it as read-only wakeup reason breaks these command paths and can feed incorrect module/attach status into later logic.

### 16.9 Batt-stats command wrapper path

TI-Nspire batt-stats producer (`0x10347c..0x10353c`) fills `BattInfo` fields by command helpers:

- source mV: `0x105054` / `0x105088` (mode-dependent)
- ADC mV: `0x105020`
- RCB mV: `0x1028a8`

Command wrapper table:

- `0x3AB8` wraps command `0x3EA` (selector arg written at stack `+0x0C`)
  - called with selectors `1`, `6`, `7` in batt-stats path
- `0x3A80` wraps `0x3EB`
- `0x3A5C` wraps `0x3EC`
- `0x39FC` wraps `0x3EF`

Implication:

- BattInfo mV values are service-command results and can diverge from raw DIAGS ADC slot values unless PMU command-domain codes are modeled separately.
