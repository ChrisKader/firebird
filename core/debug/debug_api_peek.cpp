#include <string.h>

#include "debug_api.h"

#include "emu.h"
#include "memory/mem.h"
#include "memory/mmu.h"
#include "debug.h"
#include "lcd.h"
#include "misc.h"

/* Read peripheral registers directly from state structs.
 * This bypasses the MMIO dispatch machinery entirely, so it's safe to call
 * from any thread while the emulator is running. Individual aligned 32-bit
 * reads are atomic on ARM and x86, so worst case is a slightly stale value. */
bool debug_peek_reg(uint32_t paddr, uint32_t *out)
{
    /* LCD controller: 0xC0000000 .. 0xC0000FFF */
    if ((paddr >> 12) == (0xC0000000 >> 12)) {
        uint32_t off = paddr & 0xFFF;
        switch (off) {
        case 0x000: case 0x004: case 0x008: case 0x00C:
            *out = lcd.timing[off >> 2]; return true;
        case 0x010: *out = lcd.upbase; return true;
        case 0x014: *out = lcd.lpbase; return true;
        case 0x018: *out = emulate_cx ? lcd.control : lcd.int_mask; return true;
        case 0x01C: *out = emulate_cx ? lcd.int_mask : lcd.control; return true;
        case 0x020: *out = lcd.int_status; return true;
        case 0x024: *out = lcd.int_status & lcd.int_mask; return true;
        case 0xC00: *out = lcd.cursor_control; return true;
        case 0xC04: *out = lcd.cursor_config; return true;
        case 0xC08: *out = lcd.cursor_palette[0]; return true;
        case 0xC0C: *out = lcd.cursor_palette[1]; return true;
        case 0xC10: *out = lcd.cursor_xy; return true;
        case 0xC14: *out = lcd.cursor_clip; return true;
        case 0xC20: *out = lcd.cursor_int_mask; return true;
        case 0xC28: *out = lcd.cursor_int_status; return true;
        default: *out = 0; return true; /* unrecognized LCD offset -- return 0 silently */
        }
    }

    /* Classic timers (non-CX):
     *   0x90010000 = pair 0, 0x900C0000 = pair 1, 0x900D0000 = pair 2 */
    if (!emulate_cx) {
        int pair = -1;
        if (paddr >= 0x90010000 && paddr < 0x90011000) pair = 0;
        else if (paddr >= 0x900C0000 && paddr < 0x900C1000) pair = 1;
        else if (paddr >= 0x900D0000 && paddr < 0x900D1000) pair = 2;

        if (pair >= 0) {
            uint32_t off = paddr & 0x3F;
            const struct timerpair *tp = &timer_classic.pairs[pair];
            switch (off) {
            case 0x00: *out = tp->timers[0].value; return true;
            case 0x04: *out = tp->timers[0].divider; return true;
            case 0x08: *out = tp->timers[0].control; return true;
            case 0x0C: *out = tp->timers[1].value; return true;
            case 0x10: *out = tp->timers[1].divider; return true;
            case 0x14: *out = tp->timers[1].control; return true;
            default: *out = 0; return true;
            }
        }
    }

    /* CX SP804 timers:
     *   0x90010000 = Fast timer (which=0)
     *   0x900C0000 = Slow timer 0 (which=1)
     *   0x900D0000 = Slow timer 1 (which=2) */
    if (emulate_cx) {
        int which = -1;
        if (paddr >= 0x90010000 && paddr < 0x90011000) which = 0;
        else if (paddr >= 0x900C0000 && paddr < 0x900C1000) which = 1;
        else if (paddr >= 0x900D0000 && paddr < 0x900D1000) which = 2;

        if (which >= 0) {
            uint32_t off = paddr & 0xFFF;
            int ti = (off >> 5) & 1;         /* timer index within pair */
            uint32_t reg = off & 0x1F;
            const struct cx_timer *t = &timer_cx.timer[which][ti];
            switch (reg) {
            case 0x00: *out = t->load; return true;
            case 0x04: *out = t->value; return true;  /* snapshot value, not live countdown */
            case 0x08: *out = t->control; return true;
            case 0x0C: *out = t->interrupt; return true;
            default: *out = 0; return true;
            }
        }
    }

    /* Watchdog: 0x90060000 */
    if (paddr >= 0x90060000 && paddr < 0x90061000) {
        uint32_t off = paddr & 0xFFF;
        switch (off) {
        case 0x000: *out = watchdog.load; return true;
        case 0x004: *out = watchdog.value; return true;
        case 0x008: *out = watchdog.control; return true;
        case 0x00C: *out = watchdog.interrupt; return true;
        case 0xC00: *out = watchdog.locked; return true;
        default: *out = 0; return true;
        }
    }

    return false; /* address not recognized */
}

void debug_step_out(void)
{
    uint32_t lr = arm.reg[14];
    void *ptr = virt_mem_ptr(lr & ~3, 4);
    if (!ptr)
        return;

    RAM_FLAGS(ptr) |= RF_EXEC_DEBUG_NEXT;
}

uint32_t debug_search_memory(uint32_t start, uint32_t length,
                             const uint8_t *pattern, int pattern_len)
{
    if (pattern_len <= 0)
        return 0xFFFFFFFF;

    uint8_t *base = (uint8_t *)phys_mem_ptr(start, length);
    if (!base)
        return 0xFFFFFFFF;

    uint8_t *ptr = base;
    uint8_t *end = base + length;

    while (ptr < end) {
        ptr = (uint8_t *)memchr(ptr, pattern[0], end - ptr);
        if (!ptr)
            return 0xFFFFFFFF;
        if ((end - ptr) >= pattern_len && memcmp(ptr, pattern, pattern_len) == 0)
            return start + (uint32_t)(ptr - base);
        ptr++;
    }

    return 0xFFFFFFFF;
}
