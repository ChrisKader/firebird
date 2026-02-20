#include <stdio.h>
#include <string.h>
#include <time.h>
#include "emu.h"
#include "interrupt.h"
#include "schedule.h"
#include "misc.h"
#include "keypad.h"
#include "flash.h"
#include "mem.h"
#include "cx2.h"
#include "usblink.h"

// Miscellaneous hardware modules deemed too trivial to get their own files

/* Hardware configuration overrides (GUI-settable, declared in emu.h) */
int16_t adc_battery_level_override = -1;
int8_t  adc_charging_override = -1;
int16_t lcd_contrast_override = -1;
int16_t adc_keypad_type_override = -1;
int battery_mv_override = -1;
charger_state_t charger_state_override = CHARGER_AUTO;
int8_t usb_cable_connected_override = -1;
int8_t usb_otg_cable_override = -1;
int8_t battery_present_override = -1;
int vbus_mv_override = -1;
int vsled_mv_override = -1;
int8_t dock_attached_override = -1;

/* 8FFF0000 */
void sdramctl_write_word(uint32_t addr, uint32_t value) {
    switch (addr - 0x8FFF0000) {
        case 0x00: return;
        case 0x04: return;
        case 0x08: return;
        case 0x0C: return;
        case 0x10: return;
        case 0x14: return;
    }
    bad_write_word(addr, value);
}

static memctl_cx_state memctl_cx;

uint32_t nandctl_cx_read_word(uint32_t addr)
{
    switch(addr - 0x8FFF1000)
    {
    case 0x000: return 0x20; // memc_status (raw interrupt bit set when flash op complete?)
    case 0x004: return 0x56; // memif_cfg
    case 0x300: return 0x00; // ecc_status
    case 0x304: return memctl_cx.nandctl_ecc_memcfg; // ecc_memcfg
    case 0xFE0: return 0x51;
    case 0xFE4: return 0x13;
    case 0xFE8: return 0x34;
    case 0xFEC: return 0x00;
    }
    return bad_read_word(addr);
}

void nandctl_cx_write_word(uint32_t addr, uint32_t value)
{
    switch(addr - 0x8FFF1000)
    {
    case 0x008: return; // memc_cfg_set
    case 0x00C: return; // memc_cfg_clr
    case 0x010: return; // direct_cmd
    case 0x014: return; // set_cycles
    case 0x018: return; // set_opmode
    case 0x204: nand.nand_writable = value & 1; return;
    case 0x304: memctl_cx.nandctl_ecc_memcfg = value; return;
    case 0x308: return; // ecc_memcommand1
    case 0x30C: return; // ecc_memcommand2
    }
    bad_write_word(addr, value);
}

void memctl_cx_reset(void) {
    memctl_cx.status = 0;
    memctl_cx.config = 0;
    memctl_cx.nandctl_ecc_memcfg = 0;
}
uint32_t memctl_cx_read_word(uint32_t addr) {
    if(addr >= 0x8FFF1000)
        return nandctl_cx_read_word(addr);

    switch (addr - 0x8FFF0000) {
        case 0x0000: return memctl_cx.status | 0x80;
        case 0x000C: return memctl_cx.config;
        case 0x0FE0: return 0x40;
        case 0x0FE4: return 0x13;
        case 0x0FE8: return 0x14;
        case 0x0FEC: return 0x00;
    }
    return bad_read_word(addr);
}
void memctl_cx_write_word(uint32_t addr, uint32_t value) {
    if(addr >= 0x8FFF1000)
        return nandctl_cx_write_word(addr, value);

    switch (addr - 0x8FFF0000) {
        case 0x0004:
            switch (value) {
                case 0: memctl_cx.status = 1; return; // go
                case 1: memctl_cx.status = 3; return; // sleep
                case 2: case 3: memctl_cx.status = 2; return; // wakeup, pause
                case 4: memctl_cx.status = 0; return; // configure
            }
            break;
        case 0x0008: return;
        case 0x000C: memctl_cx.config = value; return;
        case 0x0010: return; // refresh_prd
        case 0x0018: return; // t_dqss
        case 0x0028: return; // t_rcd
        case 0x002C: return; // t_rfc
        case 0x0030: return; // t_rp
        case 0x0104: return;
        case 0x0200: return;
    }
    bad_write_word(addr, value);
}

/* 90000000 */
struct gpio_state gpio;

static void gpio_int_check(void) {
    uint64_t active = gpio.int_status.w & gpio.int_mask.w;
    int_set(INT_GPIO, active != 0);
}

static bool gpio_cx2_usb_plug_present(void)
{
    /* Hackspire GPIO pin mapping:
     *   GPIO20 (section 2 bit 4) is high when top USB plug is attached. */
    if (hw_override_get_usb_otg_cable() > 0)
        return true;

    const int8_t cable_override = hw_override_get_usb_cable_connected();
    if (cable_override >= 0) {
        if (cable_override == 0)
            return false;
        const int vbus_mv_forced = hw_override_get_vbus_mv();
        return vbus_mv_forced >= 4500;
    }

    const int vbus_mv = hw_override_get_vbus_mv();
    if (vbus_mv >= 0)
        return vbus_mv >= 4500;

    return false;
}

static bool gpio_cx2_cradle_attached(void)
{
    /* Hardware register dump confirms dock detect (section 2 bit 3) is
     * active-high: 0 when no dock, 1 when dock attached. */
    const int8_t dock_override = hw_override_get_dock_attached();
    return dock_override > 0;
}

static bool gpio_cx2_cradle_power_present(void)
{
    /* TI_Cradle_Initialize distinguishes "cradle detect" from "cradle power
     * detect". Model power-detect as a separate active-high signal derived
     * from dock rail availability rather than mirroring active-low detect. */
    if (!gpio_cx2_cradle_attached())
        return false;

    const int vsled_mv = hw_override_get_vsled_mv();
    if (vsled_mv >= 0)
        return vsled_mv >= 4500;

    /* With no explicit dock-rail override, default to unpowered.
     * Physical dock attach and dock power are separate signals. */
    return false;
}

static void gpio_sync_cx2_detect_inputs(void)
{
    if (!emulate_cx2)
        return;

    /* Section 2 — confirmed by hardware register dump:
     *   All bits LOW when no USB / no dock connected.
     *   bit 4 (GPIO20): USB plug present (active-high)
     *   bit 3 (GPIO19): dock/cradle detect (active-high per HW dump)
     *   bit 6 (GPIO22): cradle power detect (active-high) */
    if (gpio_cx2_usb_plug_present())
        gpio.input.b[2] |= 0x10u;
    else
        gpio.input.b[2] &= (uint8_t)~0x10u;

    if (gpio_cx2_cradle_attached())
        gpio.input.b[2] |= 0x08u;
    else
        gpio.input.b[2] &= (uint8_t)~0x08u;

    if (gpio_cx2_cradle_power_present())
        gpio.input.b[2] |= 0x40u;
    else
        gpio.input.b[2] &= (uint8_t)~0x40u;

    /* TI-Nspire.bin cradle init checks logical GPIO IDs 7 and 3 via the GPIO
     * service command path (0x3EF). Across observed table variants these can
     * resolve either to GPIO19/20 (section2) or legacy alias slots. Keep
     * alias bits low on all banks unless explicitly driven by modeled sources
     * so disconnected boot does not report cradle-power-high. */
    if (!gpio_cx2_cradle_attached() && !gpio_cx2_cradle_power_present()) {
        int i;
        for (i = 0; i < 8; i++)
            gpio.input.b[i] &= (uint8_t)~0x88u;
    }
}

static uint8_t gpio_effective_input_byte(int port)
{
    if (emulate_cx2)
        gpio_sync_cx2_detect_inputs();
    return gpio.input.b[port];
}

static uint8_t gpio_data_byte(int port)
{
    /* GPIO data register reflects physical input on input-configured pins and
     * output latch on output-configured pins. */
    uint8_t input = gpio_effective_input_byte(port);
    uint8_t direction = gpio.direction.b[port];
    return (uint8_t)((input & direction) | (gpio.output.b[port] & ~direction));
}

void gpio_reset() {
    memset(&gpio, 0, sizeof gpio);
    gpio.direction.w = 0xFFFFFFFFFFFFFFFF;
    gpio.output.w    = 0x0000000000000000;

    /* CX II boot must not report pre-attached cradle/sled/USB unless the user
     * explicitly overrides those rails. Start all CX II GPIO inputs low and
     * let gpio_sync_cx2_detect_inputs() drive only modeled detect pins. */
    gpio.input.w = emulate_cx2 ? 0x0000000000000000ULL : 0x00000000071F001FULL;
    gpio_sync_cx2_detect_inputs();
    gpio.prev_input.w = gpio.input.w;
    touchpad_gpio_reset();
}
uint32_t gpio_read(uint32_t addr) {
    int port = addr >> 6 & 7;
    switch (addr & 0x3F) {
        case 0x00: return gpio_data_byte(port);
        case 0x04: return gpio.int_status.b[port];
        case 0x08: return gpio.int_mask.b[port];
        case 0x0C: return gpio.int_edge.b[port];
        case 0x10: return gpio.direction.b[port];
        case 0x14: return gpio.output.b[port];
        case 0x18:
            /* CX II GPIO service command 0x3EF reads +0x18 as physical pin
             * level. Returning output-latched state here can report false
             * highs on detect lines (e.g. cradle power/detect). */
            return gpio_effective_input_byte(port);
        case 0x1C: return gpio.invert.b[port];
        case 0x20: return gpio.sticky.b[port];
        case 0x24: return gpio.unknown_24.b[port];
    }
    return bad_read_word(addr);
}
void gpio_write(uint32_t addr, uint32_t value) {
    int port = addr >> 6 & 7;
    uint32_t change;
    switch (addr & 0x3F) {
        case 0x04: /* Interrupt status clear */
            gpio.int_status.b[port] &= ~value;
            gpio_int_check();
            return;
        case 0x08: /* Interrupt mask */
            gpio.int_mask.b[port] = value;
            gpio_int_check();
            return;
        case 0x0C: /* Edge detect config */
            gpio.int_edge.b[port] = value;
            return;
        case 0x10:
            change = (gpio.direction.b[port] ^ value) << (8*port);
            gpio.direction.b[port] = value;
            if (change & 0xA)
                touchpad_gpio_change();
            return;
        case 0x00: /* Data register write alias */
        case 0x14:
            change = (gpio.output.b[port] ^ value) << (8*port);
            gpio.output.b[port] = value;
            if (change & 0xA)
                touchpad_gpio_change();
            return;
        case 0x1C: gpio.invert.b[port] = value; return;
        case 0x20: gpio.sticky.b[port] = value; return;
        case 0x24: gpio.unknown_24.b[port] = value; return;
    }
    bad_write_word(addr, value);
}

/* 90010000, 900C0000, 900D0000 */
timer_state timer_classic;

static int timer_bank_from_addr(uint32_t addr)
{
    switch ((addr >> 16) & 0xFFFF) {
        case 0x9001: return 0; // Fast timer
        case 0x900C: return 1; // Slow timer 0
        case 0x900D: return 2; // Slow timer 1
        default: return -1;
    }
}

static struct timerpair *timer_pair_from_addr(uint32_t addr)
{
    int which = timer_bank_from_addr(addr);
    if (which < 0)
        return NULL;
    return &timer_classic.pairs[which];
}

uint32_t timer_read(uint32_t addr) {
    struct timerpair *tp = timer_pair_from_addr(addr);
    if (!tp)
        return bad_read_word(addr);
    cycle_count_delta = 0; // Avoid slowdown by fast-forwarding through polling loops
    switch (addr & 0x003F) {
        case 0x00: return tp->timers[0].value;
        case 0x04: return tp->timers[0].divider;
        case 0x08: return tp->timers[0].control;
        case 0x0C: return tp->timers[1].value;
        case 0x10: return tp->timers[1].divider;
        case 0x14: return tp->timers[1].control;
        case 0x18: case 0x1C: case 0x20: case 0x24: case 0x28: case 0x2C:
            return tp->completion_value[((addr & 0x3F) - 0x18) >> 2];
    }
    return bad_read_word(addr);
}
void timer_write(uint32_t addr, uint32_t value) {
    struct timerpair *tp = timer_pair_from_addr(addr);
    if (!tp) {
        bad_write_word(addr, value);
        return;
    }
    switch (addr & 0x003F) {
        case 0x00: tp->timers[0].start_value = tp->timers[0].value = value; return;
        case 0x04: tp->timers[0].divider = value; return;
        case 0x08: tp->timers[0].control = value & 0x1F; return;
        case 0x0C: tp->timers[1].start_value = tp->timers[1].value = value; return;
        case 0x10: tp->timers[1].divider = value; return;
        case 0x14: tp->timers[1].control = value & 0x1F; return;
        case 0x18: case 0x1C: case 0x20: case 0x24: case 0x28: case 0x2C:
            tp->completion_value[((addr & 0x3F) - 0x18) >> 2] = value; return;
        case 0x30: return;
    }
    bad_write_word(addr, value);
}
static void timer_int_check(struct timerpair *tp) {
    int_set(INT_TIMER0 + (tp - timer_classic.pairs), tp->int_status & tp->int_mask);
}
void timer_advance(struct timerpair *tp, int ticks) {
    struct timer *t;
    for (t = &tp->timers[0]; t != &tp->timers[2]; t++) {
        int newticks;
        if (t->control & 0x10)
            continue;
        for (newticks = t->ticks + ticks; newticks > t->divider; newticks -= (t->divider + 1)) {
            int compl = t->control & 7;
            t->ticks = 0;

            if (compl == 0 && t->value == 0)
                /* nothing */;
            else if (compl != 0 && compl != 7 && t->value == tp->completion_value[compl - 1])
                t->value = t->start_value;
            else
                t->value += (t->control & 8) ? +1 : -1;

            if (t == &tp->timers[0]) {
                for (compl = 0; compl < 6; compl++) {
                    if (t->value == tp->completion_value[compl]) {
                        tp->int_status |= 1 << compl;
                        timer_int_check(tp);
                    }
                }
            }
        }
        t->ticks = newticks;
    }
}
static void timer_event(int index) {
    // TODO: should use seperate schedule item for each timer,
    //       only fired on significant events
    event_repeat(index, 1);
    timer_advance(&timer_classic.pairs[0], 703);
    timer_advance(&timer_classic.pairs[1], 1);
    timer_advance(&timer_classic.pairs[2], 1);
}
void timer_reset() {
    memset(timer_classic.pairs, 0, sizeof timer_classic.pairs);
    int i;
    for (i = 0; i < 3; i++) {
        timer_classic.pairs[i].timers[0].control = 0x10;
        timer_classic.pairs[i].timers[1].control = 0x10;
    }
    sched.items[SCHED_TIMERS].clock = CLOCK_32K;
    sched.items[SCHED_TIMERS].proc = timer_event;
}

/*
 * 90030000: 4KiB "fastboot" RAM.
 * Persists across soft resets (warm boot) but cleared on cold boot.
 * Also saved/restored in snapshots (see misc_suspend/misc_resume).
 */
static fastboot_state fastboot;

void fastboot_cx_reset(void) {
    memset(&fastboot, 0, sizeof(fastboot));
}

uint32_t fastboot_cx_read(uint32_t addr) {
    if((addr & 0xFFFF) >= 0x1000)
        return bad_read_word(addr); // On HW it repeats

    return fastboot.mem[(addr & 0xFFF) >> 2];
}
void fastboot_cx_write(uint32_t addr, uint32_t value) {
    if((addr & 0xFFFF) >= 0x1000)
        return bad_write_word(addr, value);

    fastboot.mem[(addr & 0xFFF) >> 2] = value;
}

/* 90040000: PL022 connected to the LCI over SPI */
uint32_t spi_cx_read(uint32_t addr) {
    switch (addr & 0xFFF)
    {
    case 0xC:
        return 0x6;
    default:
        return 0;
    }
}
void spi_cx_write(uint32_t addr, uint32_t value) {
    (void) addr; (void) value;
}

/* 90060000 */
watchdog_state watchdog;

static void watchdog_reload() {
    if (watchdog.control & 1) {
        if (watchdog.load == 0)
            error("Watchdog period set to 0");
        event_set(SCHED_WATCHDOG, watchdog.load);
    }
}
static void watchdog_event(int index) {
    (void) index;

    if (watchdog.control >> 1 & watchdog.interrupt) {
        warn("Resetting due to watchdog timeout");
        emu_request_reset_hard();
    } else {
        watchdog.interrupt = 1;
        int_set(INT_WATCHDOG, 1);
        event_repeat(SCHED_WATCHDOG, watchdog.load);
    }
}
void watchdog_reset() {
    memset(&watchdog, 0, sizeof watchdog);
    watchdog.load = 0xFFFFFFFF;
    watchdog.value = 0xFFFFFFFF;
    sched.items[SCHED_WATCHDOG].clock = CLOCK_APB;
    sched.items[SCHED_WATCHDOG].second = -1;
    sched.items[SCHED_WATCHDOG].proc = watchdog_event;
}
uint32_t watchdog_read(uint32_t addr) {
    switch (addr & 0xFFF) {
        case 0x000: return watchdog.load;
        case 0x004:
            if (watchdog.control & 1)
                return event_ticks_remaining(SCHED_WATCHDOG);
            return watchdog.value;
        case 0x008: return watchdog.control;
        case 0x010: return watchdog.interrupt;
        case 0x014: return watchdog.control & watchdog.interrupt;
        case 0xC00: return watchdog.locked;
        case 0xFE0: return 0x05;
        case 0xFE4: return 0x18;
        case 0xFE8: return 0x14;
        case 0xFEC: return 0x00;
        case 0xFF0: return 0x0D;
        case 0xFF4: return 0xF0;
        case 0xFF8: return 0x05;
        case 0xFFC: return 0xB1;
        default:
            return bad_read_word(addr);
    }
}
void watchdog_write(uint32_t addr, uint32_t value) {
    switch (addr & 0xFFF) {
        case 0x000:
            if (!watchdog.locked) {
                watchdog.load = value;
                watchdog_reload();
            }
            return;
        case 0x008:
            if (!watchdog.locked) {
                uint8_t prev = watchdog.control;
                watchdog.control = value & 3;
                if (~prev & value & 1) {
                    watchdog_reload();
                } else if (prev & ~value & 1) {
                    watchdog.value = event_ticks_remaining(SCHED_WATCHDOG);
                    event_clear(SCHED_WATCHDOG);
                }
                int_set(INT_WATCHDOG, watchdog.control & watchdog.interrupt);
            }
            return;
        case 0x00C:
            if (!watchdog.locked) {
                watchdog.interrupt = 0;
                watchdog_reload();
                int_set(INT_WATCHDOG, 0);
            }
            return;
        case 0xC00:
            watchdog.locked = (value != 0x1ACCE551);
            return;
    }
    bad_write_word(addr, value);
}

/* 90080000: also an FTSSP010 */
uint32_t unknown_9008_read(uint32_t addr) {
    switch (addr & 0xFFFF) {
        case 0x00: return 0;
        case 0x08: return 0;
        /* FTSSP010 Status Register: TFE | TNF in idle state. */
        case 0x0C: return 0x06;
        case 0x10: return 0;
        case 0x1C: return 0;
        case 0x60: return 0;
        case 0x64: return 0;
    }
    return bad_read_word(addr);
}

void unknown_9008_write(uint32_t addr, uint32_t value) {
    switch (addr & 0xFFFF) {
        case 0x00: return;
        case 0x08: return;
        case 0x0C: return;
        case 0x10: return;
        case 0x14: return;
        case 0x18: return;
        case 0x1C: return;
    }
    bad_write_word(addr, value);
}

/* 90090000 */
struct rtc_state rtc;

void rtc_reset() {
    rtc.offset = 0;
}

uint32_t rtc_read(uint32_t addr) {
    switch (addr & 0xFFFF) {
        case 0x00: return time(NULL) - rtc.offset;
        case 0x14: return 0;
        case 0xFE0: return 0x31;
        case 0xFE4: return 0x10;
        case 0xFE8: return 0x04;
        case 0xFEC: return 0x00;
        case 0xFF0: return 0x0D;
        case 0xFF4: return 0xF0;
        case 0xFF8: return 0x05;
        case 0xFFC: return 0xB1;
    }
    return bad_read_word(addr);
}
void rtc_write(uint32_t addr, uint32_t value) {
    switch (addr & 0xFFFF) {
        case 0x04: return;
        case 0x08: rtc.offset = time(NULL) - value; return;
        case 0x0C: return;
        case 0x10: return;
        case 0x1C: return;
    }
    bad_write_word(addr, value);
}

/* 900A0000 */
uint32_t misc_read(uint32_t addr) {
    struct timerpair *tp = &timer_classic.pairs[((addr - 0x10) >> 3) & 3];
    static const struct { uint32_t hi, lo; } idreg[4] = {
    { 0x00000000, 0x00000000 },
    { 0x04000001, 0x00010105 },
    { 0x88000001, 0x00010107 },
    { 0x8C000000, 0x00000002 },
};
    switch (addr & 0x0FFF) {
        case 0x00: {
            if(emulate_cx2)
                return 0x202;
            else if(emulate_cx)
                return 0x101;

            return 0x01000010;
        }
        case 0x04: return 0;
        case 0x0C: return 0;
        case 0x10: case 0x18: case 0x20:
            if (emulate_cx) break;
            return tp->int_status;
        case 0x14: case 0x1C: case 0x24:
            if (emulate_cx) break;
            return tp->int_mask;
            /* Registers 28 and 2C give a 64-bit number (28 is low, 2C is high),
         * which comprises 56 data bits and 8 parity checking bits:
         *    Bit 0 is a parity check of all data bits
         *    Bits 1, 2, 4, 8, 16, and 32 are parity checks of the data bits whose
         *       positions, expressed in binary, have that respective bit set.
         *    Bit 63 is a parity check of bits 1, 2, 4, 8, 16, and 32.
         * With this system, any single-bit error can be detected and corrected.
         * (But why would that happen?! I have no idea.)
         *
         * Anyway, bits 58-62 are the "ASIC user flags", a byte which must
         * match the 80E0 field in an OS image. 01 = CAS, 00 = non-CAS. */
        case 0x28: return idreg[asic_user_flags].lo;
        case 0x2C: return idreg[asic_user_flags].hi;
    }
    return bad_read_word(addr);
}
void misc_write(uint32_t addr, uint32_t value) {
    struct timerpair *tp = &timer_classic.pairs[(addr - 0x10) >> 3 & 3];
    switch (addr & 0x0FFF) {
        case 0x04: return;
        case 0x08: emu_request_reset_soft(); return;
        case 0x10: case 0x18: case 0x20:
            if (emulate_cx) break;
            tp->int_status &= ~value;
            timer_int_check(tp);
            return;
        case 0x14: case 0x1C: case 0x24:
            if (emulate_cx) break;
            tp->int_mask = value & 0x3F;
            timer_int_check(tp);
            return;
        case 0xF04: return;
    }
    bad_write_word(addr, value);
}

/* 900B0000 */
struct pmu_state pmu;

static void timer_cx_schedule_fast(void);
static void timer_cx_schedule_slow(void);

void pmu_reset(void) {
    memset(&pmu, 0, sizeof pmu);
    // No idea what the clock speeds should actually be on reset,
    // but we have to set them to something
    pmu.clocks = pmu.clocks_load = emulate_cx ? 0x0F1002 : 0x141002;
    sched.clock_rates[CLOCK_CPU] = 90000000;
    sched.clock_rates[CLOCK_AHB] = 45000000;
    sched.clock_rates[CLOCK_APB] = 22500000;
    timer_cx_schedule_fast();
    timer_cx_schedule_slow();
}
uint32_t pmu_read(uint32_t addr) {
    switch (addr & 0x003F) {
        case 0x00: return pmu.clocks_load;
        case 0x04: return pmu.wake_mask;
        case 0x08: return 0x2000;
        case 0x0C: return 0;
        case 0x14: return 0;
        case 0x18: return pmu.disable;
        case 0x20: return pmu.disable2;
        case 0x24: return pmu.clocks;
            /* Bit 4 clear when ON key pressed */
        case 0x28: return 0x114 & ~(keypad.key_map[0] >> 5 & 0x10);
    }
    return bad_read_word(addr);
}
void pmu_write(uint32_t addr, uint32_t value) {
    switch (addr & 0x003F) {
        case 0x00: pmu.clocks_load = value; return;
        case 0x04: pmu.wake_mask = value & 0x1FFFFFF; return;
        case 0x08: return;
        case 0x0C:
            if (value & 4) {
                uint32_t clocks = pmu.clocks_load;
                uint32_t base;
                uint32_t cpudiv = (clocks & 0xFE) ? (clocks & 0xFE) : 2;
                uint32_t ahbdiv = (clocks >> 12 & 7) + 1;
                if (!emulate_cx) {
                    if (clocks & 0x100)
                        base = 27000000;
                    else
                        base = 300000000 - 6000000 * (clocks >> 16 & 0x1F);
                } else {
                    if (clocks & 0x100) {
                        base = 48000000;
                        cpudiv = 1 << (clocks >> 30);
                        ahbdiv = 2;
                    } else {
                        base = 6000000 * (clocks >> 15 & 0x3F);
                        if (base == 0) {
                            warn("Ignoring PMU clock change with base 0");
                            return;
                        }
                    }
                }
                uint32_t new_rates[3];
                new_rates[CLOCK_CPU] = base / cpudiv;
                new_rates[CLOCK_AHB] = new_rates[CLOCK_CPU] / ahbdiv;
                new_rates[CLOCK_APB] = new_rates[CLOCK_AHB] / 2;
                sched_set_clocks(3, new_rates);
                //warn("Changed clock speeds: %u %u %u", new_rates[0], new_rates[1], new_rates[2]);
                pmu.clocks = clocks;
                int_set(INT_POWER, 1); // CX boot1 expects an interrupt
                timer_cx_schedule_fast();
                timer_cx_schedule_slow();
            }
            return;
        case 0x10: pmu.on_irq_enabled = value; return;
        case 0x14: int_set(INT_POWER, 0); return;
        case 0x18: pmu.disable = value; return;
        case 0x20: pmu.disable2 = value; return;
    }
    bad_write_word(addr, value);
}

/* 90010000, 900C0000(?), 900D0000 */
timer_cx_state timer_cx;
static void timer_cx_event(int index);
static void timer_cx_fast_event(int index);
static uint32_t timer_cx_fast_scheduled_ticks = 1;
static uint32_t timer_cx_slow_scheduled_cpu_ticks = 1;
static uint8_t timer_cx_clock_select[3];

/*
 * SP804 Prescaler calculation.
 * Per ARM SP804 TRM, TimerXControl bits [3:2] select prescale:
 *   00 = divide by 1   (stages 0)
 *   01 = divide by 16  (stages 4)
 *   10 = divide by 256 (stages 8)
 *   11 = undefined, treated as divide by 256 for compatibility
 */
static inline uint32_t timer_cx_prescale_shift(uint8_t control) {
    uint32_t bits = (control >> 2) & 3;
    if (bits == 3)
        bits = 2;  /* Undefined value, treat as 256 per SP804 convention */
    return bits * 4;  /* 0, 4, or 8 -> period 1, 16, or 256 */
}

static uint32_t timer_cx_clock_rate(int which) {
    if (which < 0 || which >= 3)
        return 0;

    /* CX II timers: observed to run off APB for timer0, but timer1/2 behave
     * like the slow timers (32 kHz) on real hardware, matching previous
     * emulation. */
    if (emulate_cx2) {
        if (which == 0)
            return sched.clock_rates[CLOCK_APB];
        return sched.clock_rates[CLOCK_32K];
    }

    /* CX: fast timer is configurable; other timers default to 32 kHz
     * (selector bit1 set after reset). */
    uint8_t sel = timer_cx_clock_select[which];
    if (sel & 0x2)
        return sched.clock_rates[CLOCK_32K];
    if (sel & 0x1)
        return 10000000;
    return 33000000;
}

static uint32_t timer_cx_ticks_to_cpu(uint32_t timer_ticks, uint32_t timer_rate) {
    if (timer_rate == 0 || timer_ticks == UINT32_MAX)
        return UINT32_MAX;

    uint64_t cpu_rate = sched.clock_rates[CLOCK_CPU];
    uint64_t cpu_ticks = (uint64_t)timer_ticks * cpu_rate;
    cpu_ticks = (cpu_ticks + timer_rate - 1) / timer_rate; // ceil to avoid early firing
    if (cpu_ticks == 0)
        cpu_ticks = 1;
    if (cpu_ticks > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)cpu_ticks;
}

void timer_cx_int_check(int which) {
    int_set(INT_TIMER0+which, (timer_cx.timer[which][0].interrupt & timer_cx.timer[which][0].control >> 5)
            | (timer_cx.timer[which][1].interrupt & timer_cx.timer[which][1].control >> 5));
}

/*
 * Calculate the current timer value for accurate reads.
 * Per ARM SP804 TRM, reading TimerXValue returns the current countdown value,
 * which changes every prescaled clock tick. This function computes the value
 * based on elapsed time since the last scheduler update.
 */
static uint32_t timer_cx_current_value(int which, int timer_idx) {
    struct cx_timer *t = &timer_cx.timer[which][timer_idx];

    /* If timer is disabled, return stored value */
    if (!(t->control & 0x80))
        return t->value;

    /* Get scheduler event for this timer group */
    int sched_idx = (which == 0) ? SCHED_TIMER_FAST : SCHED_TIMERS;
    struct sched_item *item = &sched.items[sched_idx];

    /* If no event scheduled, return stored value */
    if (item->second < 0)
        return t->value;

    /* Get remaining CPU ticks until the scheduled event */
    uint32_t remaining_cpu = event_ticks_remaining(sched_idx);

    /* Convert CPU ticks to timer ticks */
    uint32_t timer_rate = timer_cx_clock_rate(which);
    uint32_t cpu_rate = sched.clock_rates[CLOCK_CPU];
    if (timer_rate == 0 || cpu_rate == 0)
        return t->value;

    uint64_t remaining_timer = (uint64_t)remaining_cpu * timer_rate / cpu_rate;

    /* Get the scheduled timer ticks for this group */
    uint32_t scheduled_ticks;
    if (which == 0) {
        scheduled_ticks = timer_cx_fast_scheduled_ticks;
    } else {
        /* For slow timers, we scheduled in CPU ticks, convert back */
        scheduled_ticks = (uint64_t)timer_cx_slow_scheduled_cpu_ticks * timer_rate / cpu_rate;
    }

    /* Calculate elapsed timer ticks since scheduling */
    uint32_t elapsed;
    if (remaining_timer >= scheduled_ticks)
        elapsed = 0;  /* Shouldn't happen, but be safe */
    else
        elapsed = scheduled_ticks - (uint32_t)remaining_timer;

    if (elapsed == 0)
        return t->value;

    /* Apply elapsed ticks through prescaler to get steps (value decrements) */
    const uint32_t shift = timer_cx_prescale_shift(t->control);
    uint32_t total_prescale = t->prescale + elapsed;
    uint32_t steps = total_prescale >> shift;

    if (steps == 0)
        return t->value;

    /* Calculate current value (countdown), respecting 16/32-bit mode */
    uint32_t value = (t->control & 2) ? t->value : (t->value & 0xFFFF);

    if (value >= steps) {
        value -= steps;
    } else {
        /* Would have crossed zero - but event hasn't fired yet, so just return 0 */
        value = 0;
    }

    return (t->control & 2) ? value : ((t->value & 0xFFFF0000) | value);
}

uint32_t timer_cx_read(uint32_t addr) {
    cycle_count_delta += 1000; // avoid slowdown with polling loops
    int which = timer_bank_from_addr(addr);
    if (which < 0)
        return bad_read_word(addr);
    int timer_idx = (addr >> 5) & 1;
    struct cx_timer *t = &timer_cx.timer[which][timer_idx];
    switch (addr & 0xFFFF) {
        case 0x0000: case 0x0020: return t->load;
        /*
         * TimerXValue (0x04/0x24): Per SP804 TRM, returns current countdown.
         * We compute this on-the-fly for accuracy instead of returning stale value.
         */
        case 0x0004: case 0x0024: return timer_cx_current_value(which, timer_idx);
        case 0x0008: case 0x0028: return t->control;
        case 0x0010: case 0x0030: return t->interrupt;
        case 0x0014: case 0x0034: return t->interrupt & t->control >> 5;
        case 0x0018: case 0x0038: return t->load;
        case 0x001C: case 0x003C: return 0; //?
        // The OS reads from 0x80 and writes it into 0x30 ???
        case 0x0080: return timer_cx_clock_select[which];
        case 0x0FE0: return 0x04;
        case 0x0FE4: return 0x18;
        case 0x0FE8: return 0x14;
        case 0x0FEC: return 0x00;
        case 0x0FF0: return 0x0D;
        case 0x0FF4: return 0xF0;
        case 0x0FF8: return 0x05;
        case 0x0FFC: return 0xB1;
    }
    return bad_read_word(addr);
}
/*
 * Calculate timer ticks until the next interrupt (value reaches 0).
 * Used by the scheduler to determine when to fire the timer event.
 */
static inline uint32_t timer_cx_ticks_to_next(const struct cx_timer *t) {
    if (!(t->control & 0x80))
        return UINT32_MAX;  /* Timer disabled */

    const uint32_t shift = timer_cx_prescale_shift(t->control);
    const uint32_t period = 1u << shift;
    const uint32_t mask = period - 1;
    uint32_t remainder = t->prescale & mask;
    uint32_t ticks_to_step = period - remainder;
    if (ticks_to_step == 0)
        ticks_to_step = period;

    /* If reload pending, schedule for one step to process it */
    if (t->reload)
        return ticks_to_step;

    /* Get current timer value (respecting 16/32-bit mode per SP804 TimerSize bit) */
    uint32_t value = (t->control & 2) ? t->value : (t->value & 0xFFFF);

    if (value == 0) {
        /* One-shot at 0: timer stopped per SP804 OneShot behavior */
        if (t->control & 1)
            return UINT32_MAX;
        /* Free-running/periodic at 0: will wrap/reload on next step */
        return ticks_to_step;
    }

    /*
     * Calculate ticks until value reaches 0 and fires interrupt.
     * First step costs ticks_to_step (prescaler remainder), subsequent steps cost period each.
     * Total = ticks_to_step + (value - 1) * period
     */
    uint64_t total = (uint64_t)ticks_to_step + (uint64_t)(value - 1) * period;

    if (total > UINT32_MAX)
        return UINT32_MAX;

    return (uint32_t)total;
}

/*
 * Advance timer state by the given number of timer ticks.
 * This implements the SP804 countdown behavior including prescaler,
 * interrupt generation, and reload/wrap modes.
 */
static void timer_cx_advance_ticks(int which, uint32_t ticks) {
    if (which < 0 || which >= 3 || ticks == 0)
        return;

    for (int i = 0; i < 2; i++) {
        struct cx_timer *t = &timer_cx.timer[which][i];

        /*
         * SP804 prescaler: divides input clock by 1, 16, or 256.
         * The prescaler accumulates ticks and generates a "step" (counter decrement)
         * each time it overflows.
         */
        const uint32_t shift = timer_cx_prescale_shift(t->control);
        const uint32_t period = 1u << shift;
        const uint32_t mask = period - 1;

        uint32_t old_prescale = t->prescale;
        uint32_t steps;

        if (shift == 0) {
            /* No prescaling: each tick is a step */
            steps = ticks;
            t->prescale = 0;
        } else {
            uint32_t new_prescale = old_prescale + ticks;
            steps = (new_prescale >> shift) - (old_prescale >> shift);
            t->prescale = new_prescale & mask;
        }

        if (!(t->control & 0x80) || steps == 0)
            continue;

        /* SP804 control bits */
        const uint32_t max_val = (t->control & 2) ? UINT32_MAX : 0xFFFF;  /* TimerSize */
        const bool one_shot = t->control & 1;   /* OneShot */
        const bool periodic = t->control & 0x40; /* TimerMode: 1=periodic, 0=free-running */

        uint32_t value = (t->control & 2) ? t->value : (t->value & 0xFFFF);

        /* Handle pending reload (from write to Load register) */
        if (t->reload) {
            t->reload = 0;
            value = t->load & max_val;
            steps--;
            if (steps == 0) {
                t->value = (t->control & 2) ? value : (t->value & 0xFFFF0000) | value;
                continue;
            }
        }

        /* SP804 OneShot: timer stops when it reaches 0 */
        if (one_shot && value == 0)
            continue;

        /*
         * Handle value == 0 in non-one-shot mode: first step wraps/reloads.
         * Per SP804 TRM, interrupt fires on transition TO 0, not FROM 0.
         */
        if (value == 0) {
            value = periodic ? (t->load & max_val) : max_val;
            steps--;
            if (steps == 0) {
                t->value = (t->control & 2) ? value : (t->value & 0xFFFF0000) | value;
                continue;
            }
        }

        /* Check if we reach or cross 0 (fires interrupt per SP804 RawInt behavior) */
        if (value > steps) {
            /* Simple countdown, doesn't reach 0 */
            value -= steps;
        } else if (value == steps) {
            /* Countdown to exactly 0, fire interrupt (no wrap yet) */
            value = 0;
            t->interrupt = 1;
            timer_cx_int_check(which);
        } else {
            /* Will cross 0 at least once - fire interrupt */
            t->interrupt = 1;

            if (one_shot) {
                /* SP804 OneShot: stop at 0 */
                value = 0;
            } else {
                /*
                 * SP804 periodic/free-running: calculate position after wrap.
                 * remaining = steps after hitting 0 and reloading
                 */
                uint32_t remaining = steps - value - 1;
                uint32_t reload_val = periodic ? (t->load & max_val) : max_val;
                uint32_t cycle = reload_val + 1;

                if (cycle == 0) {
                    /* 32-bit max value: cycle is 2^32, remaining always fits */
                    value = reload_val - remaining;
                } else {
                    remaining %= cycle;
                    value = reload_val - remaining;
                }
            }
            timer_cx_int_check(which);
        }

        t->value = (t->control & 2) ? value : (t->value & 0xFFFF0000) | (value & 0xFFFF);
    }
}

static uint32_t timer_cx_fast_next_ticks(void) {
    uint32_t next0 = timer_cx_ticks_to_next(&timer_cx.timer[0][0]);
    uint32_t next1 = timer_cx_ticks_to_next(&timer_cx.timer[0][1]);
    uint32_t next = next0 < next1 ? next0 : next1;
    return next;
}

static void timer_cx_schedule_fast(void) {
    struct sched_item *item = &sched.items[SCHED_TIMER_FAST];
    item->clock = CLOCK_CPU;

    uint32_t timer_rate = timer_cx_clock_rate(0);
    if (sched.clock_rates[CLOCK_CPU] == 0 || timer_rate == 0) {
        item->second = -1;
        item->tick = 0;
        item->cputick = 0;
        return;
    }
    uint32_t next = timer_cx_fast_next_ticks();
    if (next == UINT32_MAX) {
        item->second = -1;
        item->tick = 0;
        item->cputick = 0;
        return;
    }
    uint32_t cpu_ticks = timer_cx_ticks_to_cpu(next, timer_rate);
    if (cpu_ticks == UINT32_MAX) {
        item->second = -1;
        item->tick = 0;
        item->cputick = 0;
        return;
    }
    timer_cx_fast_scheduled_ticks = next;
    event_set(SCHED_TIMER_FAST, cpu_ticks);
}

void timer_cx_write(uint32_t addr, uint32_t value) {
    int which = timer_bank_from_addr(addr);
    if (which < 0) {
        bad_write_word(addr, value);
        return;
    }
    struct cx_timer *t = &timer_cx.timer[which][addr >> 5 & 1];
    switch (addr & 0xFFFF) {
        case 0x0000: case 0x0020:
            /*
             * TimerXLoad: Per SP804 TRM, writing to the Load register causes
             * the counter to immediately restart from the new value.
             * This differs from BGLoad which only updates for next reload.
             */
            t->load = value;
            t->value = value;
            t->prescale = 0;  /* Reset prescaler on immediate load */
            t->reload = 0;    /* Clear any pending deferred reload */
            goto schedule;
        case 0x0018: case 0x0038:
            /*
             * TimerXBGLoad: Per SP804 TRM, writing to Background Load updates
             * the load value but does NOT immediately affect the counter.
             * The new value is used on the next periodic reload.
             */
            t->load = value;
            goto schedule;
        case 0x0004: case 0x0024: goto schedule;
        case 0x0008: case 0x0028:
            t->control = value;
            timer_cx_int_check(which);
        goto schedule;
        case 0x000C: case 0x002C: t->interrupt = 0; timer_cx_int_check(which); goto schedule;

        case 0x0080:
            timer_cx_clock_select[which] = value;
            goto schedule; // Clock source select
    }
    bad_write_word(addr, value);
    return;
schedule:
    if (which == 0)
        timer_cx_schedule_fast();
    else
        timer_cx_schedule_slow();
}

static uint32_t timer_cx_slow_next_cpu_ticks(void) {
    uint32_t best = UINT32_MAX;
    if (sched.clock_rates[CLOCK_CPU] == 0)
        return UINT32_MAX;

    for (int which = 1; which <= 2; which++) {
        uint32_t timer_rate = timer_cx_clock_rate(which);
        if (timer_rate == 0)
            continue;

        uint32_t next0 = timer_cx_ticks_to_next(&timer_cx.timer[which][0]);
        uint32_t next1 = timer_cx_ticks_to_next(&timer_cx.timer[which][1]);
        uint32_t next = next0 < next1 ? next0 : next1;
        uint32_t cpu_ticks = timer_cx_ticks_to_cpu(next, timer_rate);
        if (cpu_ticks == UINT32_MAX)
            continue;

        if (cpu_ticks < best)
            best = cpu_ticks;
    }
    return best;
}

static void timer_cx_schedule_slow(void) {
    struct sched_item *item = &sched.items[SCHED_TIMERS];
    item->clock = CLOCK_CPU;

    if (sched.clock_rates[CLOCK_CPU] == 0) {
        item->second = -1;
        item->tick = 0;
        item->cputick = 0;
        return;
    }

    uint32_t cpu_ticks = timer_cx_slow_next_cpu_ticks();
    if (cpu_ticks == UINT32_MAX) {
        item->second = -1;
        item->tick = 0;
        item->cputick = 0;
        return;
    }

    timer_cx_slow_scheduled_cpu_ticks = cpu_ticks ? cpu_ticks : 1;
    event_set(SCHED_TIMERS, timer_cx_slow_scheduled_cpu_ticks);
}

static void timer_cx_event(int index) {
    if (cpu_events & EVENT_SLEEP) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    uint32_t cpu_rate = sched.clock_rates[CLOCK_CPU];
    if (cpu_rate == 0) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    uint32_t cpu_ticks = timer_cx_slow_scheduled_cpu_ticks ? timer_cx_slow_scheduled_cpu_ticks : 1;
    for (int which = 1; which <= 2; which++) {
        uint32_t timer_rate = timer_cx_clock_rate(which);
        if (timer_rate == 0)
            continue;

        uint64_t timer_ticks = (uint64_t)cpu_ticks * timer_rate / cpu_rate;
        if (timer_ticks == 0)
            timer_ticks = 1;
        if (timer_ticks > UINT32_MAX)
            timer_ticks = UINT32_MAX;
        timer_cx_advance_ticks(which, (uint32_t)timer_ticks);
    }

    uint32_t next_cpu = timer_cx_slow_next_cpu_ticks();
    if (next_cpu == UINT32_MAX) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    timer_cx_slow_scheduled_cpu_ticks = next_cpu ? next_cpu : 1;
    event_repeat(index, timer_cx_slow_scheduled_cpu_ticks);
}
static void timer_cx_fast_event(int index) {
    if (cpu_events & EVENT_SLEEP) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    uint32_t timer_rate = timer_cx_clock_rate(0);
    if (timer_rate == 0) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    uint32_t ticks = timer_cx_fast_scheduled_ticks ? timer_cx_fast_scheduled_ticks : 1;
    timer_cx_advance_ticks(0, ticks);

    uint32_t next = timer_cx_fast_next_ticks();
    if (next == UINT32_MAX) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    timer_cx_fast_scheduled_ticks = next;
    uint32_t cpu_ticks = timer_cx_ticks_to_cpu(next, timer_rate);
    if (cpu_ticks == UINT32_MAX) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }
    event_repeat(index, cpu_ticks);
}
void timer_cx_reset() {
    memset(timer_cx.timer, 0, sizeof(timer_cx.timer));
    memset(timer_cx_clock_select, 0, sizeof(timer_cx_clock_select));
    int which, i;
    for (which = 0; which < 3; which++) {
        for (i = 0; i < 2; i++) {
            timer_cx.timer[which][i].value = 0xFFFFFFFF;
            timer_cx.timer[which][i].control = 0x20;
        }
        if (which > 0)
            timer_cx_clock_select[which] = 0x2; // default slow timers to 32 kHz
    }
    sched.items[SCHED_TIMERS].clock = CLOCK_CPU;
    sched.items[SCHED_TIMERS].proc = timer_cx_event;
    sched.items[SCHED_TIMER_FAST].clock = CLOCK_CPU;
    sched.items[SCHED_TIMER_FAST].proc = timer_cx_fast_event;
    timer_cx_fast_scheduled_ticks = 1;
    timer_cx_slow_scheduled_cpu_ticks = 1;
    timer_cx_schedule_fast();
    timer_cx_schedule_slow();
}

void timer_cx_wake(void) {
    timer_cx_schedule_fast();
    timer_cx_schedule_slow();
}

/* 900F0000 */
hdq1w_state hdq1w;

void hdq1w_reset() {
    hdq1w.lcd_contrast = 0;
}
uint32_t hdq1w_read(uint32_t addr) {
    switch (addr & 0xFFFF) {
        case 0x08: return 0;
        case 0x0C: return 0;
        case 0x10: return 0;
        case 0x14: return 0;
        case 0x20: return hdq1w.lcd_contrast;
    }
    return bad_read_word(addr);
}
void hdq1w_write(uint32_t addr, uint32_t value) {
    switch (addr & 0xFFFF) {
        case 0x04: return;
        case 0x0C: return;
        case 0x14: return;
        case 0x20:
            /* On CX2, contrast is driven by the backlight PWM controller
             * at 90130000, not the HDQ1W register.  Ignore OS writes here
             * so they don't overwrite the PWM-derived value. */
            if (!emulate_cx2 && hw_override_get_lcd_contrast() < 0)
                hdq1w.lcd_contrast = value;
            return;
    }
    bad_write_word(addr, value);
}

/* 90110000: LED */
static led_state led;

void led_reset() {
    memset(&led, 0, sizeof(led));
}

uint32_t led_read_word(uint32_t addr) {
    uint32_t offset = addr & 0xFFFF;
    if(offset == 0)
        return 0;
    else if(offset >= 0xB00 && offset - 0xB00 < sizeof(led.regs))
        return led.regs[(offset - 0xB00) >> 2];

    return bad_read_word(addr);
}
void led_write_word(uint32_t addr, uint32_t value) {
    uint32_t offset = addr & 0xFFFF;
    if(offset == 0)
        return;
    else if(offset >= 0xB00 && offset - 0xB00 < sizeof(led.regs)) {
        led.regs[(offset - 0xB00) >> 2] = value;
        return;
    }

    bad_write_word(addr, value);
}

/* A9000000: SPI */
uint32_t spi_read_word(uint32_t addr) {
    switch (addr - 0xA9000000) {
        case 0x0C: return 0;
        case 0x10: return 1;
        case 0x14: return 0;
        case 0x18: return -1;
        case 0x1C: return -1;
        case 0x20: return 0;
    }
    return bad_read_word(addr);
}
void spi_write_word(uint32_t addr, uint32_t value) {
    switch (addr - 0xA9000000) {
        case 0x08: return;
        case 0x0C: return;
        case 0x14: return;
        case 0x18: return;
        case 0x1C: return;
        case 0x20: return;
    }
    bad_write_word(addr, value);
}

/* AC000000: SDIO */
typedef struct sdio_state {
    uint16_t block_size;
    uint16_t block_count;
    uint32_t argument;
    uint16_t transfer_mode;
    uint16_t command;
    uint32_t response[4];
    uint32_t present_state;
    uint32_t host_control_power;
    uint32_t clock_timeout_reset;
    uint8_t timeout_control;
    uint8_t software_reset;
    uint16_t normal_int_status;
    uint16_t error_int_status;
    uint16_t normal_int_status_enable;
    uint16_t error_int_status_enable;
    uint16_t normal_int_signal_enable;
    uint16_t error_int_signal_enable;
    uint16_t auto_cmd_error_status;
    uint16_t host_control2;
} sdio_state;

static sdio_state sdio;

void sdio_reset(void)
{
    memset(&sdio, 0, sizeof(sdio));
    /* Default to "no SDIO card/module present" for plain handheld boot.
     * This prevents guest WLAN/SDIO init from treating absent hardware as
     * a valid attached module and wedging in probe flows. */
    sdio.present_state = 0x00000000u;
}

uint8_t sdio_read_byte(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x28: return sdio.timeout_control;
        case 0x29: return sdio.software_reset;
        case 0x2E: return (uint8_t)sdio.error_int_status;
        case 0x2F: return (uint8_t)(sdio.error_int_status >> 8);
    }
    return bad_read_byte(addr);
}
uint16_t sdio_read_half(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x04: return sdio.block_size;
        case 0x06: return sdio.block_count;
        case 0x0C: return sdio.transfer_mode;
        case 0x0E: return sdio.command;
        case 0x10: return (uint16_t)(sdio.response[0] & 0xFFFFu);
        case 0x12: return (uint16_t)(sdio.response[0] >> 16);
        case 0x14: return (uint16_t)(sdio.response[1] & 0xFFFFu);
        case 0x16: return (uint16_t)(sdio.response[1] >> 16);
        case 0x18: return (uint16_t)(sdio.response[2] & 0xFFFFu);
        case 0x1A: return (uint16_t)(sdio.response[2] >> 16);
        case 0x1C: return (uint16_t)(sdio.response[3] & 0xFFFFu);
        case 0x1E: return (uint16_t)(sdio.response[3] >> 16);
        case 0x2C: return sdio.normal_int_status;
        case 0x2E: return sdio.error_int_status;
        case 0x30: return sdio.normal_int_status_enable;
        case 0x32: return sdio.error_int_status_enable;
        case 0x34: return sdio.normal_int_signal_enable;
        case 0x36: return sdio.error_int_signal_enable;
        case 0x38: return sdio.auto_cmd_error_status;
        case 0x3A: return sdio.host_control2;
        case 0x3C: return 0;      /* capabilities low */
        case 0x3E: return 0;      /* capabilities high */
        case 0xFE: return 0x0002; /* host controller version */
    }
    return bad_read_half(addr);
}
uint32_t sdio_read_word(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x00:
            return (uint32_t)sdio.block_size | ((uint32_t)sdio.block_count << 16);
        case 0x08:
            return sdio.argument;
        case 0x20:
            return sdio.present_state;
        case 0x24:
            return sdio.host_control_power;
        case 0x28:
            return sdio.clock_timeout_reset;
        case 0x3C:
            return 0;
        case 0x40:
            return 0;
    }
    return bad_read_word(addr);
}
void sdio_write_byte(uint32_t addr, uint8_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x28:
            sdio.timeout_control = value;
            sdio.clock_timeout_reset = (sdio.clock_timeout_reset & 0xFFFFFF00u) | value;
            return;
        case 0x29:
            sdio.software_reset = value;
            if (value) {
                /* Minimal controller reset semantics used by driver init. */
                sdio.normal_int_status = 0;
                sdio.error_int_status = 0;
                sdio.software_reset = 0;
            }
            return;
        case 0x2E:
            sdio.error_int_status &= (uint16_t)~value;
            return;
        case 0x2F:
            sdio.error_int_status &= (uint16_t)~((uint16_t)value << 8);
            return;
    }
    bad_write_byte(addr, value);
}
void sdio_write_half(uint32_t addr, uint16_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x04:
            sdio.block_size = value;
            return;
        case 0x06:
            sdio.block_count = value;
            return;
        case 0x0C:
            sdio.transfer_mode = value;
            return;
        case 0x0E:
            sdio.command = value;
            /* Immediately complete command/data when card is present.
             * If absent, raise error status so the guest can bail out
             * cleanly instead of spinning waiting for usable media. */
            if (sdio.present_state & 0x00010000u) {
                sdio.normal_int_status |= 0x0001u; /* Command Complete */
                if (sdio.transfer_mode & 0x0001u)
                    sdio.normal_int_status |= 0x0002u; /* Transfer Complete */
            } else {
                sdio.normal_int_status |= 0x8001u; /* Error + Command complete */
                sdio.error_int_status |= 0x0001u;  /* Command Timeout */
                if (sdio.transfer_mode & 0x0001u)
                    sdio.error_int_status |= 0x0010u; /* Data Timeout */
            }
            return;
        case 0x2C:
            sdio.normal_int_status &= (uint16_t)~value; /* W1C */
            return;
        case 0x2E:
            sdio.error_int_status &= (uint16_t)~value;  /* W1C */
            return;
        case 0x30:
            sdio.normal_int_status_enable = value;
            return;
        case 0x32:
            sdio.error_int_status_enable = value;
            return;
        case 0x34:
            sdio.normal_int_signal_enable = value;
            return;
        case 0x36:
            sdio.error_int_signal_enable = value;
            return;
        case 0x38:
            sdio.auto_cmd_error_status = value;
            return;
        case 0x3A:
            sdio.host_control2 = value;
            return;
    }
    bad_write_half(addr, value);
}
void sdio_write_word(uint32_t addr, uint32_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x00:
            sdio.block_size = (uint16_t)(value & 0xFFFFu);
            sdio.block_count = (uint16_t)(value >> 16);
            return;
        case 0x08:
            sdio.argument = value;
            return;
        case 0x20:
            return;
        case 0x24:
            sdio.host_control_power = value;
            return;
        case 0x28:
            sdio.clock_timeout_reset = value;
            sdio.timeout_control = (uint8_t)(value & 0xFFu);
            return;
    }
    bad_write_word(addr, value);
}

/* B8000000 */
uint32_t sramctl_read_word(uint32_t addr) {
    switch (addr - 0xB8001000) {
        case 0xFE0: return 0x52;
        case 0xFE4: return 0x13;
        case 0xFE8: return 0x34;
        case 0xFEC: return 0x00;
    }
    return bad_read_word(addr);
}
void sramctl_write_word(uint32_t addr, uint32_t value) {
    switch (addr - 0xB8001000) {
        case 0x010: return;
        case 0x014: return;
        case 0x018: return;
    }
    bad_write_word(addr, value);
    return;
}

/* C4000000: ADC (Analog-to-Digital Converter) */
static adc_state adc;
typedef struct adc_cx2_state {
    uint32_t reg[0x1000 / sizeof(uint32_t)];
    uint32_t bg_counter;
    uint32_t sample_tick;
    uint32_t slot18_programmed_ctrl;
    bool slot18_programmed_valid;
} adc_cx2_state;
static adc_cx2_state adc_cx2;

enum {
    CX2_BATTERY_MV_MIN = 3000,
    CX2_BATTERY_MV_MAX = 4200,
    CX2_BATTERY_RUN_MV_MIN = 3300,
    CX2_BATTERY_PRECHARGE_MV = 3000,
    CX2_VBUS_MV_MIN = 0,
    CX2_VBUS_MV_MAX = 5500,
    CX2_VSLED_MV_MIN = 0,
    CX2_VSLED_MV_MAX = 5500,
    CX2_VBUS_VALID_MV_MIN = 4500,
    CX2_VSLED_VALID_MV_MIN = 4500,
    CX2_VSYS_EXT_TARGET_MV = 3600,
    CX2_VSYS_PGOOD_MV_MIN = 3200,
    CX2_USB_PATH_DROP_MV = 100,
    CX2_DOCK_PATH_DROP_MV = 100,
    CX2_BAT_PATH_DROP_MV = 50,
    /* Test mode: emulate a direct 10-bit battery ADC domain. */
    CX2_ADC_CODE_MIN = 0x0000,
    CX2_ADC_CODE_MAX = 0x03FF,
};

typedef enum cx2_source_in_use {
    CX2_SOURCE_NONE = 0,
    CX2_SOURCE_BATTERY,
    CX2_SOURCE_USB,
    CX2_SOURCE_DOCK,
} cx2_source_in_use_t;

typedef struct cx2_power_model_state {
    bool battery_present;
    bool usb_attached;
    bool usb_otg;
    bool dock_attached;
    bool battery_run_ok;
    bool battery_precharge;
    bool usb_ok;
    bool dock_ok;
    cx2_source_in_use_t source;
    charger_state_t charger_state;
    int battery_mv;
    int vbus_mv;
    int vsled_mv;
    int vsys_mv;
    bool power_good;
    uint16_t battery_code;
    uint16_t vbus_code;
    uint16_t vsled_code;
    uint16_t vsys_code;
    uint16_t vref_code;
    uint16_t vref_aux_code;
} cx2_power_model_state_t;

static uint32_t cx2_adc_vref_code(void);
static uint32_t cx2_adc_vref_aux_code(void);

bool cx2_battery_override_active(void)
{
    return hw_override_get_battery_mv() >= 0;
}

bool cx2_effective_battery_present(void)
{
    const int8_t present_override = hw_override_get_battery_present();
    if (present_override >= 0)
        return present_override != 0;
    return true;
}

static int clamp_int(int value, int min, int max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static int cx2_effective_battery_mv(void)
{
    const int battery_mv = hw_override_get_battery_mv();
    if (battery_mv >= 0)
        return clamp_int(battery_mv, CX2_BATTERY_MV_MIN, CX2_BATTERY_MV_MAX);

    /* Default to a full battery, matching classic ADC default behavior. */
    return CX2_BATTERY_MV_MAX;
}

static bool cx2_effective_usb_attached(void)
{
    const int8_t usb_override = hw_override_get_usb_cable_connected();
    if (usb_override >= 0) {
        if (usb_override == 0)
            return false;
        const int vbus_mv_forced = hw_override_get_vbus_mv();
        return vbus_mv_forced >= CX2_VBUS_VALID_MV_MIN;
    }

    /* Physical model default: disconnected unless an explicit rail override
     * says VBUS is actually present. Internal usblink/session state must not
     * implicitly power the PMU path. */
    const int vbus_mv = hw_override_get_vbus_mv();
    if (vbus_mv >= 0)
        return vbus_mv >= CX2_VBUS_VALID_MV_MIN;
    return false;
}

static bool cx2_effective_usb_otg(void)
{
    return hw_override_get_usb_otg_cable() > 0;
}

static int cx2_effective_vbus_mv(void)
{
    const int vbus_mv = hw_override_get_vbus_mv();
    if (vbus_mv >= 0)
        return clamp_int(vbus_mv, CX2_VBUS_MV_MIN, CX2_VBUS_MV_MAX);

    return 0;
}

static bool cx2_effective_dock_attached(void)
{
    const int8_t dock_override = hw_override_get_dock_attached();
    if (dock_override >= 0)
        return dock_override != 0;
    return false;
}

static int cx2_effective_vsled_mv(void)
{
    const int vsled_mv = hw_override_get_vsled_mv();
    if (vsled_mv >= 0)
        return clamp_int(vsled_mv, CX2_VSLED_MV_MIN, CX2_VSLED_MV_MAX);

    if (!cx2_effective_dock_attached())
        return 0;
    /* Do not implicitly source dock power just because a dock is attached. */
    return 0;
}

static uint16_t cx2_adc_code_from_mv(int mv)
{
    /* Normal polarity: higher mV -> higher code.
     *
     * The firmware uses a scale of ~4.57 mV per ADC count (confirmed by
     * bootloader UART output: code 370 -> 1691 mV).
     *   4200 mV -> code 919,  3000 mV -> code 657
     * VREF is 704, so battery codes exceed VREF at normal levels;
     * this is expected -- the firmware's conversion math handles it. */
    const int code_at_3000 = 0x291;  /* 657 @ 3.0V */
    const int code_at_4200 = 0x397;  /* 919 @ 4.2V */
    int clamped_mv = clamp_int(mv, CX2_BATTERY_MV_MIN, CX2_BATTERY_MV_MAX);
    int span_mv = CX2_BATTERY_MV_MAX - CX2_BATTERY_MV_MIN;
    int pos_mv = clamped_mv - CX2_BATTERY_MV_MIN;
    int code = code_at_3000 + (pos_mv * (code_at_4200 - code_at_3000) + span_mv / 2) / span_mv;
    return (uint16_t)clamp_int(code, CX2_ADC_CODE_MIN, CX2_ADC_CODE_MAX);
}

static uint16_t cx2_adc_code_from_vbus_mv(int mv)
{
    /* Physical rail to ADC code mapping:
     * 0mV -> 0x000, 5000mV -> 0x330.
     * This avoids reporting "present-ish" voltage when the rail is truly 0mV. */
    int clamped_mv = clamp_int(mv, 0, 5000);
    int code = (clamped_mv * 0x330 + 2500) / 5000;
    return (uint16_t)clamp_int(code, CX2_ADC_CODE_MIN, CX2_ADC_CODE_MAX);
}

static uint16_t cx2_adc_code_from_vsys_mv(int mv)
{
    int clamped_mv = clamp_int(mv, 0, CX2_BATTERY_MV_MAX);
    int code = (clamped_mv * 0x397 + CX2_BATTERY_MV_MAX / 2) / CX2_BATTERY_MV_MAX;
    return (uint16_t)clamp_int(code, CX2_ADC_CODE_MIN, CX2_ADC_CODE_MAX);
}

static void cx2_build_power_model(cx2_power_model_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->battery_present = cx2_effective_battery_present();
    state->battery_mv = state->battery_present ? cx2_effective_battery_mv() : 0;
    state->usb_attached = cx2_effective_usb_attached();
    state->usb_otg = cx2_effective_usb_otg();
    state->dock_attached = cx2_effective_dock_attached();
    state->vbus_mv = cx2_effective_vbus_mv();
    state->vsled_mv = state->dock_attached ? cx2_effective_vsled_mv() : 0;
    state->battery_run_ok = state->battery_present && state->battery_mv >= CX2_BATTERY_RUN_MV_MIN;
    state->battery_precharge = state->battery_present && state->battery_mv < CX2_BATTERY_PRECHARGE_MV;
    state->usb_ok = state->usb_attached && !state->usb_otg && state->vbus_mv >= CX2_VBUS_VALID_MV_MIN;
    state->dock_ok = state->dock_attached && state->vsled_mv >= CX2_VSLED_VALID_MV_MIN;

    if (state->usb_ok)
        state->source = CX2_SOURCE_USB;
    else if (state->dock_ok)
        state->source = CX2_SOURCE_DOCK;
    else if (state->battery_present)
        state->source = CX2_SOURCE_BATTERY;
    else
        state->source = CX2_SOURCE_NONE;

    int vusb_path = state->usb_ok
        ? clamp_int(state->vbus_mv - CX2_USB_PATH_DROP_MV, 0, CX2_VSYS_EXT_TARGET_MV)
        : 0;
    int vdock_path = state->dock_ok
        ? clamp_int(state->vsled_mv - CX2_DOCK_PATH_DROP_MV, 0, CX2_VSYS_EXT_TARGET_MV)
        : 0;
    int vbat_path = state->battery_present ? state->battery_mv - CX2_BAT_PATH_DROP_MV : 0;
    if (vbat_path < 0)
        vbat_path = 0;

    int vext_sel = 0;
    if (state->source == CX2_SOURCE_USB)
        vext_sel = vusb_path;
    else if (state->source == CX2_SOURCE_DOCK)
        vext_sel = vdock_path;

    state->vsys_mv = (vext_sel > vbat_path) ? vext_sel : vbat_path;
    state->power_good = state->vsys_mv >= CX2_VSYS_PGOOD_MV_MIN;

    const charger_state_t charger_override = hw_override_get_charger_state();
    if (charger_override >= CHARGER_DISCONNECTED && charger_override <= CHARGER_CHARGING) {
        state->charger_state = charger_override;
    } else if (!state->usb_ok && !state->dock_ok) {
        state->charger_state = CHARGER_DISCONNECTED;
    } else if (!state->battery_present || state->usb_otg) {
        state->charger_state = CHARGER_CONNECTED_NOT_CHARGING;
    } else if (state->battery_precharge || state->battery_mv < (CX2_BATTERY_MV_MAX - 20)) {
        state->charger_state = CHARGER_CHARGING;
    } else {
        state->charger_state = CHARGER_CONNECTED_NOT_CHARGING;
    }

    state->vref_code = (uint16_t)(cx2_adc_vref_code() & CX2_ADC_CODE_MAX);
    state->vref_aux_code = (uint16_t)(cx2_adc_vref_aux_code() & CX2_ADC_CODE_MAX);
    state->battery_code = state->battery_present ? cx2_adc_code_from_mv(state->battery_mv) : 0;
    state->vbus_code = cx2_adc_code_from_vbus_mv(state->vbus_mv);
    state->vsled_code = cx2_adc_code_from_vbus_mv(state->vsled_mv);
    state->vsys_code = cx2_adc_code_from_vsys_mv(state->vsys_mv);
}

uint32_t adc_cx2_effective_battery_code(void)
{
    cx2_power_model_state_t state;
    cx2_build_power_model(&state);
    return state.battery_code;
}

static uint32_t cx2_adc_vref_code(void)
{
    /* VREF is used as a scaling DIVISOR in the firmware conversion:
     *   VSys_mV = battery_code * 3225 / vref_code
     * Battery codes intentionally exceed VREF (unlike a typical ratiometric
     * ADC).  Confirmed by bootloader output: code 370, VREF 704 → 1695 mV
     * matches the observed "VSys:1691".  Do not raise this value. */
    return 0x02C0u;  /* 704 */
}

static uint32_t cx2_adc_vref_aux_code(void)
{
    /* Secondary reference channel, slightly below primary VREF. */
    return 0x02B8u;  /* 696 */
}

static uint32_t cx2_adc_bg_reload(void)
{
    uint32_t period = adc_cx2.reg[0x110u >> 2] & 0xFFFFu;
    /* Keep periodic completions very responsive in the PMU polling domain.
     * For the common 0x960 period used by bootloader code this yields 1 tick,
     * avoiding ADC timeout paths that quickly force power-off. */
    uint32_t reload = period ? (period >> 11) : 1u;
    if (reload < 1u)
        reload = 1u;
    if (reload > 16u)
        reload = 16u;
    return reload;
}

charger_state_t cx2_effective_charger_state(void)
{
    cx2_power_model_state_t state;
    cx2_build_power_model(&state);
    return state.charger_state;
}

static int cx2_adc_code_to_mv(uint32_t code, uint32_t vref_code, uint32_t full_scale_mv)
{
    if (vref_code == 0u)
        return 0;
    return (int)((code * full_scale_mv + (vref_code / 2u)) / vref_code);
}

void cx2_get_power_rails(cx2_power_rails_t *rails)
{
    if (!rails)
        return;

    cx2_power_model_state_t state;
    cx2_build_power_model(&state);

    rails->battery_present = state.battery_present;
    rails->charger_state = state.charger_state;
    rails->battery_code = state.battery_code;
    rails->vsys_code = state.vsys_code;
    rails->vsled_code = state.vsled_code;
    rails->vref_code = state.vref_code;
    rails->vref_aux_code = state.vref_aux_code;
    rails->vbus_code = state.vbus_code;
    rails->battery_mv = state.battery_mv;
    rails->vsys_mv = state.vsys_mv;
    rails->vsled_mv = state.vsled_mv;
    rails->vbus_mv = state.vbus_mv;
    rails->vref_mv = 3225;
    rails->vref_aux_mv = cx2_adc_code_to_mv(state.vref_aux_code, state.vref_code, 3225u);
}

static void cx2_adc_refresh_samples(void)
{
    if (cpu_events & EVENT_SLEEP)
        return;

    cx2_power_model_state_t state;
    cx2_build_power_model(&state);

    uint32_t batt = state.battery_code;
    uint32_t vref = state.vref_code;
    uint32_t vref_aux = state.vref_aux_code;
    /* Slot 0x18: compound format read back by firmware.
     * Upper bits = channel control (programmed by firmware, default 0x2A00),
     * bits [17:16] = charger state, bits [9:0] = VBUS ADC code. */
    uint32_t slot18 = adc_cx2.slot18_programmed_valid
        ? adc_cx2.slot18_programmed_ctrl : 0x00002A00u;
    slot18 &= ~(0x00030000u | CX2_ADC_CODE_MAX);
    switch (state.charger_state) {
    case CHARGER_CONNECTED_NOT_CHARGING: slot18 |= 0x00010000u; break;
    case CHARGER_CHARGING:               slot18 |= 0x00020000u; break;
    default: break;
    }
    slot18 |= state.vbus_code & CX2_ADC_CODE_MAX;

    /* 0x900B0000..0x900B001C: 8-entry sample bank.
     * All battery slots use the same normal-polarity code (higher mV =
     * higher code).  Battery codes exceed VREF at normal charge levels;
     * the firmware's conversion formula (batt * 3225 / vref) handles this. */
    adc_cx2.reg[0x00u >> 2] = batt;
    adc_cx2.reg[0x04u >> 2] = vref;
    adc_cx2.reg[0x08u >> 2] = vref_aux;
    adc_cx2.reg[0x0Cu >> 2] = batt;
    adc_cx2.reg[0x10u >> 2] = vref;
    adc_cx2.reg[0x14u >> 2] = vref_aux;
    adc_cx2.reg[0x18u >> 2] = slot18;
    adc_cx2.reg[0x1Cu >> 2] = batt;

    /* Channel-window result registers (+0x10 in each 0x20-byte channel block)
     * are consumed by firmware conversion paths. Keep them coherent with the
     * live rail model to avoid falling back to floor-voltage behavior. */
    for (uint32_t chan = 0; chan < 7u; chan++) {
        uint32_t base = (0x100u + chan * 0x20u) >> 2;
        uint32_t code = batt;
        switch (chan) {
        case 0: code = batt; break;
        case 1: code = batt; break;
        case 2: code = state.vsys_code; break;
        case 3: code = state.vsys_code; break;
        case 4: code = vref; break;
        case 5: code = state.vsled_code; break;
        case 6: code = state.vbus_code; break;
        default: break;
        }
        adc_cx2.reg[base + (0x10u >> 2)] = code & CX2_ADC_CODE_MAX;
    }
}

static bool cx2_adc_channel_offset(uint32_t offset, uint32_t *chan, uint32_t *regoff)
{
    if (offset < 0x100u || offset >= 0x1E0u)
        return false;
    uint32_t rel = offset - 0x100u;
    uint32_t c = rel / 0x20u;
    if (c >= 7u)
        return false;
    if (chan)
        *chan = c;
    if (regoff)
        *regoff = rel % 0x20u;
    return true;
}

static void cx2_adc_mark_channel_done(uint32_t chan)
{
    uint32_t base = (0x100u + chan * 0x20u) >> 2;
    adc_cx2.reg[base + (0x08u >> 2)] |= 1u;
    adc_cx2.reg[base + (0x0Cu >> 2)] |= 1u;
}

static bool cx2_adc_start_requested(uint32_t cmd);

static bool cx2_adc_channel_started(uint32_t chan)
{
    uint32_t base = (0x100u + chan * 0x20u) >> 2;
    return cx2_adc_start_requested(adc_cx2.reg[base + (0x00u >> 2)])
        || cx2_adc_start_requested(adc_cx2.reg[base + (0x04u >> 2)]);
}

static bool cx2_adc_mark_started_channels_done(void)
{
    bool any = false;
    for (uint32_t chan = 0; chan < 7u; chan++) {
        if (!cx2_adc_channel_started(chan))
            continue;
        cx2_adc_mark_channel_done(chan);
        any = true;
    }
    return any;
}

static bool cx2_adc_irq_should_assert(void)
{
    uint32_t chan;
    for (chan = 0; chan < 7; chan++) {
        uint32_t base = (0x100u + chan * 0x20u) >> 2;
        uint32_t s08 = adc_cx2.reg[base + (0x08u >> 2)];
        uint32_t s0c = adc_cx2.reg[base + (0x0Cu >> 2)];
        uint32_t status = s08 | s0c;
        if (status & 1u)
            return true;
    }
    return false;
}

static void cx2_adc_update_irq(void)
{
    bool on = cx2_adc_irq_should_assert();
    int_set(INT_ADC, on);
    /* CX II bootloader ADC paths use logical IRQ 13 mapping in several places.
     * Mirror the source onto raw IRQ 13 as well so either mask path can fire. */
    int_set(13, on);
    aladdin_pmu_set_adc_pending(on);
}

void adc_cx2_background_step(void)
{
    if (cpu_events & EVENT_SLEEP)
        return;

    /* 0x118 bit0 enables periodic conversions in observed boot flows. */
    if ((adc_cx2.reg[0x118u >> 2] & 1u) == 0)
        return;
    if (cx2_adc_irq_should_assert()) {
        /* Keep sample bank live even while completion status stays latched. */
        cx2_adc_refresh_samples();
        /* If status is latched, keep IRQ/pending in sync with that latch. */
        cx2_adc_update_irq();
        return;
    }

    if (adc_cx2.bg_counter == 0u)
        adc_cx2.bg_counter = cx2_adc_bg_reload();
    if (--adc_cx2.bg_counter != 0u)
        return;

    adc_cx2.bg_counter = cx2_adc_bg_reload();
    cx2_adc_refresh_samples();
    if (!cx2_adc_mark_started_channels_done())
        cx2_adc_mark_channel_done(0);
    cx2_adc_update_irq();
}

void adc_cx2_clear_pending(void)
{
    uint32_t chan;
    for (chan = 0; chan < 7; chan++) {
        uint32_t base = (0x100u + chan * 0x20u) >> 2;
        adc_cx2.reg[base + (0x08u >> 2)] &= ~(1u | 2u);
        adc_cx2.reg[base + (0x0Cu >> 2)] &= ~(1u | 2u);
    }
    cx2_adc_update_irq();
}

static bool cx2_adc_start_requested(uint32_t cmd)
{
    return (cmd & 1u) != 0u || cmd == 0x00070111u || cmd == 0x00071100u;
}

static void cx2_adc_latch_completion(void)
{
    cx2_adc_refresh_samples();
    if (!cx2_adc_mark_started_channels_done())
        cx2_adc_mark_channel_done(0);
    adc_cx2.bg_counter = cx2_adc_bg_reload();
    cx2_adc_update_irq();
}

static uint16_t adc_read_channel(int n) {
    if (pmu.disable2 & 0x10) {
        if (n == 3) {
            const int16_t keypad_override = hw_override_get_adc_keypad_type();
            if (keypad_override >= 0)
                return (uint16_t)keypad_override;
        } else {
            const int16_t battery_override = hw_override_get_adc_battery_level();
            if (battery_override >= 0)
                return (uint16_t)battery_override;
        }
        return 0x3FF;
    }

    // Scale for channels 1-2:   155 units = 1 volt
    // Scale for other channels: 310 units = 1 volt
    if (n == 3) {
        // A value from 0 to 20 indicates normal TI-Nspire keypad.
        // A value from 21 to 42 indicates TI-84+ keypad.
        // A value around 73 indicates a TI-Nspire with touchpad
        const int16_t keypad_override = hw_override_get_adc_keypad_type();
        return (keypad_override >= 0)
            ? (uint16_t)keypad_override : 73;
    } else {
        // Channels 1-2: battery voltage
        const int16_t battery_override = hw_override_get_adc_battery_level();
        return (battery_override >= 0)
            ? (uint16_t)battery_override : 930;
    }
}
void adc_reset() {
    memset(&adc, 0, sizeof adc);
    memset(&adc_cx2, 0, sizeof adc_cx2);
    adc_cx2.slot18_programmed_ctrl = 0u;
    adc_cx2.slot18_programmed_valid = false;
    adc_cx2.bg_counter = 0u;
    /* On real hardware the ADC controller retains config across CPU soft
     * resets.  The OS never re-initialises the periodic conversion register
     * (0x118) or the period (0x110) after the bootloader sets them, so
     * pre-enable them here so the ADC interrupt chain stays alive. */
    adc_cx2.reg[0x110u >> 2] = 0x0960u;
    adc_cx2.reg[0x118u >> 2] = 1u;
    cx2_adc_refresh_samples();
    /* Bootloader expects initial pending-like bits on channel 0 status regs. */
    adc_cx2.reg[0x108u >> 2] = 1u;
    adc_cx2.reg[0x10Cu >> 2] = 1u;
    cx2_adc_update_irq();
}
uint32_t adc_read_word(uint32_t addr) {
    if (emulate_cx2) {
        static bool warned_legacy_adc_for_cx2 = false;
        if (!warned_legacy_adc_for_cx2) {
            warned_legacy_adc_for_cx2 = true;
            fprintf(stderr, "[FBDBG] WARNING: legacy adc_read_word used in CX II path (addr=%08X)\n", addr);
            fflush(stderr);
        }
    }
    int n;
    if (!(addr & 0x100)) {
        switch (addr & 0xFF) {
            case 0x00: return adc.int_status & adc.int_mask;
            case 0x04: return adc.int_status;
            case 0x08: return adc.int_mask;
        }
    } else if ((n = addr >> 5 & 7) < 7) {
        struct adc_channel *c = &adc.channel[n];
        switch (addr & 0x1F) {
            case 0x00: return 0;
            case 0x04: return c->unknown;
            case 0x08: return c->count;
            case 0x0C: return c->address;
            case 0x10: return c->value;
            case 0x14: return c->speed;
        }
    }
    return bad_read_word(addr);
}
void adc_write_word(uint32_t addr, uint32_t value) {
    int n;
    if (!(addr & 0x100)) {
        switch (addr & 0xFF) {
            case 0x04: // Interrupt acknowledge
                adc.int_status &= ~value;
                int_set(INT_ADC, adc.int_status & adc.int_mask);
                return;
            case 0x08: // Interrupt enable
                adc.int_mask = value & 0xFFFFFFF;
                int_set(INT_ADC, adc.int_status & adc.int_mask);
                return;
            case 0x0C:
            case 0x10:
            case 0x14:
                return;
        }
    } else if ((n = addr >> 5 & 7) < 7) {
        struct adc_channel *c = &adc.channel[n];
        switch (addr & 0x1F) {
            case 0x00: // Command register - write 1 to measure voltage and store to +10
                // Other commands do exist, including some
                // that write to memory; not implemented yet.
                c->value = adc_read_channel(n);
                adc.int_status |= 3 << (4 * n);
                int_set(INT_ADC, adc.int_status & adc.int_mask);
                return;
            case 0x04: c->unknown = value & 0xFFFFFFF; return;
            case 0x08: c->count = value & 0x1FFFFFF; return;
            case 0x0C: c->address = value & ~3; return;
            case 0x14: c->speed = value & 0x3FF; return;
        }
    }
    bad_write_word(addr, value);
    return;
}

static FILE *adc_trace_fp = NULL;
static int adc_trace_count = 0;

static void adc_trace(const char *tag, uint32_t addr, uint32_t offset, uint32_t val)
{
    if (adc_trace_count >= 500) return;
    if (!adc_trace_fp) {
        adc_trace_fp = fopen("/tmp/firebird_adc_trace.txt", "w");
        if (!adc_trace_fp) return;
    }
    adc_trace_count++;
    fprintf(adc_trace_fp, "[%s] %08X +%03X %08X\n", tag, addr, offset, val);
    if (adc_trace_count % 50 == 0 || adc_trace_count >= 500)
        fflush(adc_trace_fp);
}

uint32_t adc_cx2_read_word(uint32_t addr)
{
    static bool logged_cx2_adc_path = false;
    if (!logged_cx2_adc_path) {
        logged_cx2_adc_path = true;
        fprintf(stderr, "[FBDBG] adc_cx2_read_word active (first addr=%08X)\n", addr);
        fflush(stderr);
    }

    uint32_t offset = addr & 0xFFF;
    uint32_t index = offset >> 2;
    if (offset <= 0x1Cu)
        cx2_adc_refresh_samples();
    uint32_t reg = adc_cx2.reg[index];
    if (offset <= 0x1Cu && offset != 0x18u)
        reg &= CX2_ADC_CODE_MAX;
    adc_trace("RD", addr, offset, reg);
    return reg;
}

void adc_cx2_write_word(uint32_t addr, uint32_t value)
{
    uint32_t offset = addr & 0xFFF;
    adc_trace("WR", addr, offset, value);
    uint32_t index = offset >> 2;
    uint32_t regoff = 0;
    bool is_channel_reg = cx2_adc_channel_offset(offset, NULL, &regoff);

    if (offset <= 0x1Cu && offset != 0x18u)
        value &= CX2_ADC_CODE_MAX;

    /* Channel status regs (+0x08/+0x0C): write-1-to-clear. */
    if (is_channel_reg && (regoff == 0x08u || regoff == 0x0Cu)) {
        uint32_t old = adc_cx2.reg[index];
        uint32_t newv = old & ~(value & 3u);
        adc_cx2.reg[index] = newv;
        if ((adc_cx2.reg[0x118u >> 2] & 1u) != 0u && !cx2_adc_irq_should_assert())
            adc_cx2.bg_counter = cx2_adc_bg_reload();
        cx2_adc_update_irq();
        return;
    }

    /* Generic store for all other registers. */
    adc_cx2.reg[index] = value;
    if (offset == 0x18u) {
        adc_cx2.slot18_programmed_ctrl = value & 0x0003FC00u;
        adc_cx2.slot18_programmed_valid = true;
    }

    if (is_channel_reg && (regoff == 0x00u || regoff == 0x04u)
            && cx2_adc_start_requested(value)) {
        /* Channel launch drives conversion/status handshake. */
        cx2_adc_latch_completion();
        return;
    }

    if (offset == 0x110u) {
        if ((adc_cx2.reg[0x118u >> 2] & 1u) != 0u)
            adc_cx2.bg_counter = cx2_adc_bg_reload();
        return;
    }

    if (offset == 0x118u) {
        if ((value & 1u) != 0u) {
            if (!cx2_adc_irq_should_assert())
                adc_cx2.bg_counter = cx2_adc_bg_reload();
        } else {
            adc_cx2.bg_counter = 0u;
        }
        cx2_adc_update_irq();
        return;
    }
}

bool misc_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &memctl_cx, sizeof(memctl_cx))
            && snapshot_write(snapshot, &gpio, sizeof(gpio))
            && snapshot_write(snapshot, &timer_classic, sizeof(timer_classic))
            && snapshot_write(snapshot, &fastboot, sizeof(fastboot))
            && snapshot_write(snapshot, &watchdog, sizeof(watchdog))
            && snapshot_write(snapshot, &rtc, sizeof(rtc))
            && snapshot_write(snapshot, &pmu, sizeof(pmu))
            && snapshot_write(snapshot, &timer_cx, sizeof(timer_cx))
            && snapshot_write(snapshot, &hdq1w, sizeof(hdq1w))
            && snapshot_write(snapshot, &led, sizeof(led))
            && snapshot_write(snapshot, &adc, sizeof(adc));
}

bool misc_resume(const emu_snapshot *snapshot)
{
    bool ok = snapshot_read(snapshot, &memctl_cx, sizeof(memctl_cx))
            && snapshot_read(snapshot, &gpio, sizeof(gpio))
            && snapshot_read(snapshot, &timer_classic, sizeof(timer_classic))
            && snapshot_read(snapshot, &fastboot, sizeof(fastboot))
            && snapshot_read(snapshot, &watchdog, sizeof(watchdog))
            && snapshot_read(snapshot, &rtc, sizeof(rtc))
            && snapshot_read(snapshot, &pmu, sizeof(pmu))
            && snapshot_read(snapshot, &timer_cx, sizeof(timer_cx))
            && snapshot_read(snapshot, &hdq1w, sizeof(hdq1w))
            && snapshot_read(snapshot, &led, sizeof(led))
            && snapshot_read(snapshot, &adc, sizeof(adc));
    if (ok) {
        timer_cx_schedule_fast();
        timer_cx_schedule_slow();
    }
    return ok;
}
