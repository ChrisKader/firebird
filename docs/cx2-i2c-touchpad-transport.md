# CX II I2C Touchpad Transport (`0x90050000`)

This document captures the TI-Nspire/OSLoader/Bootloader transport path behind
MMIO `0x90050000`.

Important scope note:

- This path was investigated during PMIC reverse engineering.
- In Firebird, APB slot `0x05` maps to `touchpad_cx_read/touchpad_cx_write` and
  is therefore touchpad-side in emulator ownership.
- PMIC/PMU docs should treat this as a ruled-out PMIC transport path.

Related docs:

- `docs/cx2-pmic-register-map.md`
- `docs/cx2-power-code-audit.md`

## 1) Service transport entry points (TI-Nspire OS)

Observed wrapper path in `TI-Nspire.bin` (base `0x10000000`):

- `0x1000A8BC`: opens service ID `21` (`r0=0x15`, `r1=-1`) via `0x10074C54`
- `0x1000A8E8`: sends a message via `0x1007548C` and returns `msg->result`
- Wrapper helpers at `0x1000A910..0x1000AAC0` build command packets on stack

Dispatcher:

- `0x1000A700` routes command IDs (`0x3E9..0x3F1`) to low-level helpers.

## 2) Observed message structure

Minimum proven fields used by wrappers and dispatcher:

```c
typedef struct Service21Msg {
    uint32_t command;      // +0x00
    uint32_t reserved0;    // +0x04 (zeroed by wrappers)
    int32_t  result;       // +0x08 (written by dispatcher, returned by wrapper)
    uint32_t arg0;         // +0x0C
    uint32_t arg1;         // +0x10
    // additional stack bytes exist in wrappers but are not required for these commands
} Service21Msg;
```

## 3) Wrapper pack rules

Recovered from TI-Nspire region `0x1000A910..0x1000AAC0`:

| Wrapper | Command | Packed args |
| --- | --- | --- |
| `0x1000A910` | `0x3EB` | `arg0 = selector` |
| `0x1000A93C` | `0x3EA` | none |
| `0x1000A964` | `0x3E9` | none |
| `0x1000A98C` | `0x3EE` | `arg0 = selector` |
| `0x1000A9B8` | `0x3ED` | `arg0 = (reg_hi << 8) | reg_lo`, `arg1 = buf` |
| `0x1000A9F0` | `0x3EC` | `arg0 = (reg_hi << 8) | reg_lo`, `arg1 = buf` |
| `0x1000AA24` | `0x3F1` | `arg0 = selector` (validated `1..12`) |
| `0x1000AA64` | `0x3F0` | `arg0 = (selector << 16) | len`, `arg1 = buf` |
| `0x1000AAC0` | `0x3EF` | `arg0 = (selector << 16) | len`, `arg1 = buf` |

Dispatcher routing (`0x1000A700`):

| Command | Helper |
| --- | --- |
| `0x3E9` | `0x1000A148` |
| `0x3EA` | `0x1000A224` |
| `0x3EB` | `0x1000A058` |
| `0x3EC` | `0x1000A2D8` |
| `0x3ED` | `0x1000A410` |
| `0x3EE` | `0x1000A4FC` |
| `0x3EF` | `0x1000A558` |
| `0x3F0` | `0x1000A624` |
| `0x3F1` | `0x1000A624` (variant: second arg cleared) |

## 4) `0x90050000` register behavior

Observed in TI-Nspire/OSLoader/Bootloader helper routines:

| Register | Access pattern | Meaning (inferred) |
| --- | --- | --- |
| `+0x04` | `strh` | Control/address staging register |
| `+0x10` | `strh`/`ldrb` | TX/RX data byte port |
| `+0x38` | `strb` | Transfer count (`len`) |
| `+0x3C` | `strb` | Transfer count (`len+1`) |
| `+0x6C` | `strh` | Transaction latch / local handshake |
| `+0x70` | `ldr` + bit tests | Controller status flags |

Status usage (`+0x70`):

- bit `0`: busy flag (`0x1000A070`, `0x1000A224` wait loop)
- bit `2`: status/error probe (`0x1000A0A8`)
- bit `3`: RX-ready/data-available probe (`0x1000A088`)

## 5) Confirmed callsite examples

TI-Nspire callsites with selector/length patterns:

- `0x100027A0`: `0x1000AAC0(selector=8, len=6, buf=sp+0x10)`
- `0x10002810`: `0x1000AAC0(selector=10, len=6, buf=sp+0x10)`
- `0x10002840`: `0x1000AAC0(selector=7, len=6, buf=sp+0x10)`
- `0x10002888`: `0x1000AA64(selector=3, len=1, buf=sp+0x10)`
- `0x10002DCC`: `0x1000AA64(selector=9, len=2, buf=...)`
- `0x10002E0C`: `0x1000AA64(selector=12, len=1, buf=...)`

## 6) Cross-image confirmation

Same `0x90050000` helper pattern appears in:

- OSLoader: `0x132071B8..0x1320722C`, block helpers `0x13207428..0x13207624`
- Bootloader: `0x112071A0..0x11207214`, block helpers `0x11207410..0x1120760C`

## 7) Emulator ownership conclusion

- This is confirmed CPU-side serial traffic over the `0x90050000` controller.
- In Firebird, this path belongs to the touchpad controller model (`i2c_touchpad`).
- PMIC identity/state paths remain:
  - direct PMIC ID/status at `0x90100004`
  - PMU/sideband state at `0x90140000/0x901408xx`
