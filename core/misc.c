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

// Miscellaneous hardware modules deemed too trivial to get their own files

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

void gpio_reset() {
    memset(&gpio, 0, sizeof gpio);
    gpio.direction.w = 0xFFFFFFFFFFFFFFFF;
    gpio.output.w    = 0x0000000000000000;

    gpio.input.w     = 0x00001000071F001F;
    touchpad_gpio_reset();
}
uint32_t gpio_read(uint32_t addr) {
    int port = addr >> 6 & 7;
    switch (addr & 0x3F) {
        case 0x04: return 0;
        case 0x08: return 0;
        case 0x10: return gpio.direction.b[port];
        case 0x14: return gpio.output.b[port];
        case 0x18: return gpio.input.b[port] | gpio.output.b[port];
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
        /* Interrupt handling not implemented */
        case 0x04: return;
        case 0x08: return;
        case 0x0C: return;
        case 0x10:
            change = (gpio.direction.b[port] ^ value) << (8*port);
            gpio.direction.b[port] = value;
            if (change & 0xA)
                touchpad_gpio_change();
            return;
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
static timer_state timer;
#define ADDR_TO_TP(addr) (&timer.pairs[((addr) >> 16) % 5])

uint32_t timer_read(uint32_t addr) {
    struct timerpair *tp = ADDR_TO_TP(addr);
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
    struct timerpair *tp = ADDR_TO_TP(addr);
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
    int_set(INT_TIMER0 + (tp - timer.pairs), tp->int_status & tp->int_mask);
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
    timer_advance(&timer.pairs[0], 703);
    timer_advance(&timer.pairs[1], 1);
    timer_advance(&timer.pairs[2], 1);
}
void timer_reset() {
    memset(timer.pairs, 0, sizeof timer.pairs);
    int i;
    for (i = 0; i < 3; i++) {
        timer.pairs[i].timers[0].control = 0x10;
        timer.pairs[i].timers[1].control = 0x10;
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
static watchdog_state watchdog;

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
        cpu_events |= EVENT_RESET;
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
    struct timerpair *tp = &timer.pairs[((addr - 0x10) >> 3) & 3];
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
    struct timerpair *tp = &timer.pairs[(addr - 0x10) >> 3 & 3];
    switch (addr & 0x0FFF) {
        case 0x04: return;
        case 0x08: cpu_events |= EVENT_RESET; return;
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
static timer_cx_state timer_cx;
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
    int which = (addr >> 16) % 5;
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
    int which = (addr >> 16) % 5;
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
        case 0x20: hdq1w.lcd_contrast = value; return;
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
uint8_t sdio_read_byte(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x29: return -1;
    }
    return bad_read_byte(addr);
}
uint16_t sdio_read_half(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x10: return -1;
        case 0x12: return -1;
        case 0x14: return -1;
        case 0x16: return -1;
        case 0x18: return -1;
        case 0x1A: return -1;
        case 0x1C: return -1;
        case 0x1E: return -1;
        case 0x2C: return -1;
        case 0x30: return -1;
    }
    return bad_read_half(addr);
}
uint32_t sdio_read_word(uint32_t addr) {
    switch (addr & 0x3FFFFFF) {
        case 0x20: return -1;
    }
    return bad_read_word(addr);
}
void sdio_write_byte(uint32_t addr, uint8_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x29: return;
        case 0x2E: return;
        case 0x2F: return;
    }
    bad_write_byte(addr, value);
}
void sdio_write_half(uint32_t addr, uint16_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x04: return;
        case 0x0C: return;
        case 0x0E: return;
        case 0x2C: return;
        case 0x30: return;
        case 0x32: return;
        case 0x34: return;
        case 0x36: return;
    }
    bad_write_half(addr, value);
}
void sdio_write_word(uint32_t addr, uint32_t value) {
    switch (addr & 0x3FFFFFF) {
        case 0x00: return;
        case 0x08: return;
        case 0x20: return;
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

static uint16_t adc_read_channel(int n) {
    if (pmu.disable2 & 0x10)
        return 0x3FF;

    // Scale for channels 1-2:   155 units = 1 volt
    // Scale for other channels: 310 units = 1 volt
    if (n == 3) {
        // A value from 0 to 20 indicates normal TI-Nspire keypad.
        // A value from 21 to 42 indicates TI-84+ keypad.
        // A value around 73 indicates a TI-Nspire with touchpad
        return 73;
    } else {
        return 930;
    }
}
void adc_reset() {
    memset(&adc, 0, sizeof adc);
}
uint32_t adc_read_word(uint32_t addr) {
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

// Not really implemented
uint32_t adc_cx2_read_word(uint32_t addr)
{
    (void) addr;
    return 0x6969;
}

void adc_cx2_write_word(uint32_t addr, uint32_t value)
{
    (void) addr;
    (void) value;
    // It expects an IRQ on writing something
    int_set(INT_ADC, 1);
}

bool misc_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &memctl_cx, sizeof(memctl_cx))
            && snapshot_write(snapshot, &gpio, sizeof(gpio))
            && snapshot_write(snapshot, &timer, sizeof(timer))
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
            && snapshot_read(snapshot, &timer, sizeof(timer))
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
