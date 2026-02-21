#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"
#include "peripherals/interrupt.h"
#include "cpu/cpu.h"
#include "memory/mem.h"

/* DC000000: Interrupt controller */
interrupt_state intr;

static struct {
    bool initialized;
    bool enabled;
    bool all;
    bool filter[32];
} irq_trace_cfg;

static struct {
    bool initialized;
    bool enabled;
    bool all;
    bool onkey_only;
    bool include_unchanged;
} vic_trace_cfg;

static const char *irq_trace_name(uint32_t int_num)
{
    switch (int_num) {
        case INT_POWER: return "POWER_MANAGEMENT";
        case INT_KEYPAD: return "KEYPAD";
        case INT_IRQ30: return "IRQ30";
        default: return "";
    }
}

static void irq_trace_init(void)
{
    if (irq_trace_cfg.initialized)
        return;
    irq_trace_cfg.initialized = true;

    const char *spec = getenv("FIREBIRD_TRACE_IRQ");
    if (!spec || !*spec)
        return;

    if (!strcmp(spec, "*") || !strcmp(spec, "all")) {
        irq_trace_cfg.enabled = true;
        irq_trace_cfg.all = true;
        return;
    }

    const char *p = spec;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';')
            p++;
        if (!*p)
            break;

        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p) {
            p++;
            continue;
        }

        if (value >= 0 && value < 32) {
            irq_trace_cfg.enabled = true;
            irq_trace_cfg.filter[value] = true;
        }
        p = end;
    }
}

static bool irq_trace_should_log(uint32_t int_num)
{
    irq_trace_init();
    if (!irq_trace_cfg.enabled || int_num >= 32)
        return false;
    return irq_trace_cfg.all || irq_trace_cfg.filter[int_num];
}

static void irq_trace_log_transition(uint32_t int_num, bool on)
{
    if (!irq_trace_should_log(int_num))
        return;
    const char *name = irq_trace_name(int_num);
    if (*name) {
        fprintf(stderr,
                "[FBIRQ] irq=%u (%s) state=%d active=0x%08X pc=0x%08X\n",
                int_num, name, on ? 1 : 0, intr.active, arm.reg[15]);
    } else {
        fprintf(stderr,
                "[FBIRQ] irq=%u state=%d active=0x%08X pc=0x%08X\n",
                int_num, on ? 1 : 0, intr.active, arm.reg[15]);
    }
    fflush(stderr);
}

static void vic_trace_init(void)
{
    if (vic_trace_cfg.initialized)
        return;
    vic_trace_cfg.initialized = true;

    const char *spec = getenv("FIREBIRD_TRACE_VIC");
    if (!spec || !*spec)
        return;

    if (!strcmp(spec, "1") || !strcmp(spec, "true")
        || !strcmp(spec, "*") || !strcmp(spec, "all")) {
        vic_trace_cfg.enabled = true;
        vic_trace_cfg.all = true;
    } else if (!strcmp(spec, "onkey") || !strcmp(spec, "power")) {
        vic_trace_cfg.enabled = true;
        vic_trace_cfg.onkey_only = true;
    }

    const char *unchanged = getenv("FIREBIRD_TRACE_VIC_UNCHANGED");
    if (unchanged && (*unchanged == '1' || *unchanged == 'y' || *unchanged == 'Y'
            || *unchanged == 't' || *unchanged == 'T'))
        vic_trace_cfg.include_unchanged = true;
}

static bool vic_trace_should_log(uint32_t int_num)
{
    vic_trace_init();
    if (!vic_trace_cfg.enabled)
        return false;
    if (vic_trace_cfg.all)
        return true;
    if (vic_trace_cfg.onkey_only)
        return int_num == INT_POWER || int_num == INT_IRQ30;
    return false;
}

static void vic_trace_log_transition(uint32_t int_num, bool on,
                                     uint32_t prev_active,
                                     uint32_t prev_raw_status,
                                     uint32_t prev_status,
                                     uint32_t prev_irq_pending,
                                     uint32_t prev_fiq_pending)
{
    if (!vic_trace_should_log(int_num))
        return;

    const uint32_t irq_pending = intr.active & intr.mask[0] & ~intr.mask[1];
    const uint32_t fiq_pending = intr.active & intr.mask[0] & intr.mask[1];
    const bool changed = prev_active != intr.active
                      || prev_raw_status != intr.raw_status
                      || prev_status != intr.status
                      || prev_irq_pending != irq_pending
                      || prev_fiq_pending != fiq_pending;
    if (!changed && !vic_trace_cfg.include_unchanged)
        return;
    const char *name = irq_trace_name(int_num);

    if (*name) {
        fprintf(stderr,
                "[FBVIC] src=%u(%s) set=%d changed=%d "
                "active:%08X->%08X raw:%08X->%08X status:%08X->%08X "
                "mask_irq=%08X mask_fiq=%08X pend_irq:%08X->%08X pend_fiq:%08X->%08X "
                "pc=0x%08X\n",
                int_num, name, on ? 1 : 0, changed ? 1 : 0,
                prev_active, intr.active,
                prev_raw_status, intr.raw_status,
                prev_status, intr.status,
                intr.mask[0], intr.mask[1],
                prev_irq_pending, irq_pending,
                prev_fiq_pending, fiq_pending,
                arm.reg[15]);
    } else {
        fprintf(stderr,
                "[FBVIC] src=%u set=%d changed=%d "
                "active:%08X->%08X raw:%08X->%08X status:%08X->%08X "
                "mask_irq=%08X mask_fiq=%08X pend_irq:%08X->%08X pend_fiq:%08X->%08X "
                "pc=0x%08X\n",
                int_num, on ? 1 : 0, changed ? 1 : 0,
                prev_active, intr.active,
                prev_raw_status, intr.raw_status,
                prev_status, intr.status,
                intr.mask[0], intr.mask[1],
                prev_irq_pending, irq_pending,
                prev_fiq_pending, fiq_pending,
                arm.reg[15]);
    }
    fflush(stderr);
}

static void get_current_int(int is_fiq, int *current) {
    uint32_t masked_status = intr.status & intr.mask[is_fiq];
    int pri_limit = intr.priority_limit[is_fiq];
    int best = -1;
    int i;
    for (i = 0; i < 32; i++) {
        if (masked_status & (1u << i) && intr.priority[i] < pri_limit) {
            best = i;
            pri_limit = intr.priority[i];
        }
    }
    *current = best;
}

static void update() {
    uint32_t prev_raw_status = intr.raw_status;
    intr.raw_status = intr.active ^ ~intr.noninverted;

    intr.sticky_status |= (intr.raw_status & ~prev_raw_status);
    intr.status = (intr.raw_status    & ~intr.sticky)
                | (intr.sticky_status &  intr.sticky);

    int is_fiq;
    for (is_fiq = 0; is_fiq < 2; is_fiq++) {
        int i = -1;
        get_current_int(is_fiq, &i);
        if (i >= 0) {
            arm.interrupts |= 0x80 >> is_fiq;
        } else {
            arm.interrupts &= ~(0x80 >> is_fiq);
        }
    }
    cpu_int_check();
}

uint32_t int_read_word(uint32_t addr) {
    int group = addr >> 8 & 3;
    if (group < 2) {
        int is_fiq = group;
        int current = -1;
        switch (addr & 0xFF) {
            case 0x00:
                return intr.status & intr.mask[is_fiq];
            case 0x04:
                return intr.status;
            case 0x08:
            case 0x0C:
                return intr.mask[is_fiq];
            case 0x20:
                get_current_int(is_fiq, &current);
                return current;
            case 0x24:
                get_current_int(is_fiq, &current);
                if (current >= 0) {
                    intr.prev_pri_limit[is_fiq] = intr.priority_limit[is_fiq];
                    intr.priority_limit[is_fiq] = intr.priority[current];
                }
                return current;
            case 0x28:
                current = -1;
                get_current_int(is_fiq, &current);
                if (current < 0) {
                    arm.interrupts &= ~(0x80 >> is_fiq);
                    cpu_int_check();
                }
                return intr.prev_pri_limit[is_fiq];
            case 0x2C:
                return intr.priority_limit[is_fiq];
        }
    } else if (group == 2) {
        switch (addr & 0xFF) {
            case 0x00: return intr.noninverted;
            case 0x04: return intr.sticky;
            case 0x08: return 0;
        }
    } else {
        if (!(addr & 0x80))
            return intr.priority[addr >> 2 & 0x1F];
    }
    return bad_read_word(addr);
}
void int_write_word(uint32_t addr, uint32_t value) {
    int group = addr >> 8 & 3;
    if (group < 2) {
        int is_fiq = group;
        switch (addr & 0xFF) {
            case 0x04: intr.sticky_status &= ~value; update(); return;
            case 0x08: intr.mask[is_fiq] |= value; update(); return;
            case 0x0C: intr.mask[is_fiq] &= ~value; update(); return;
            case 0x2C: intr.priority_limit[is_fiq] = value & 0x0F; update(); return;
        }
    } else if (group == 2) {
        switch (addr & 0xFF) {
            case 0x00: intr.noninverted = value; update(); return;
            case 0x04: intr.sticky = value; update(); return;
            case 0x08: return;
        }
    } else {
        if (!(addr & 0x80)) {
            intr.priority[addr >> 2 & 0x1F] = value & 7;
            return;
        }
    }
    return bad_write_word(addr, value);
}

static void update_cx() {
    uint32_t active_irqs = intr.active & intr.mask[0] & ~intr.mask[1];
    if (active_irqs != 0)
    {
        // Fallback first
        intr.irq_handler_cur = intr.irq_handler_def;

        // Vectored handling enabled?
        for(unsigned int i = 0; i < sizeof(intr.irq_ctrl_vect)/sizeof(intr.irq_ctrl_vect[0]); ++i)
        {
            uint8_t ctrl = intr.irq_ctrl_vect[i];
            if((ctrl & 0x20) == 0)
                continue; // Disabled

            if(active_irqs & (1 << (ctrl & 0x1F)))
            {
                // Vector enabled and active
                intr.irq_handler_cur = intr.irq_addr_vect[i];
                break;
            }
        }

        arm.interrupts |= 0x80;
    }
    else
        arm.interrupts &= ~0x80;

    if (intr.active & intr.mask[0] & intr.mask[1])
        arm.interrupts |= 0x40;
    else
        arm.interrupts &= ~0x40;
    cpu_int_check();
}

uint32_t int_cx_read_word(uint32_t addr) {
    uint32_t offset = addr & 0x3FFFFFF;
    if(offset < 0x100)
    {
        switch(offset)
        {
        case 0x00: return intr.active & intr.mask[0] & ~intr.mask[1];
        case 0x04: return intr.active & intr.mask[0] & intr.mask[1];
        case 0x08: return intr.active;
        case 0x0C: return intr.mask[1];
        case 0x10: return intr.mask[0];
        case 0x30: return intr.irq_handler_cur;
        case 0x34: return intr.irq_handler_def;
        }
    }
    else if(offset < 0x300)
    {
        uint8_t entry = (offset & 0xFF) >> 2;
        if(entry < 16)
        {
            if(offset < 0x200)
                return intr.irq_addr_vect[entry];
            else
                return intr.irq_ctrl_vect[entry];
        }
    }
    else if(offset >= 0xFE0 && offset < 0x1000) // ID regs
        return (uint32_t[]){0x90, 0x11, 0x04, 0x00, 0x0D, 0xF0, 0x05, 0x81}[(offset - 0xFE0) >> 2];

    return bad_read_word(addr);
}
void int_cx_write_word(uint32_t addr, uint32_t value) {
    uint32_t offset = addr & 0x3FFFFFF;
    if(offset < 0x100)
    {
        switch(offset)
        {
        case 0x004: return;
        case 0x00C: intr.mask[1] = value; update_cx(); return;
        case 0x010: intr.mask[0] |= value; update_cx(); return;
        case 0x014: intr.mask[0] &= ~value; update_cx(); return;
        case 0x01C: return;
        case 0x030: /* An ack, but ignored here. */ return;
        case 0x034: intr.irq_handler_def = value; return;
        }
    }
    else if(offset < 0x300)
    {
        uint8_t entry = (offset & 0xFF) >> 2;
        if(entry < 16)
        {
            if(offset < 0x200)
                intr.irq_addr_vect[entry] = value;
            else
                intr.irq_ctrl_vect[entry] = value;

            return;
        }
    }
    else if(offset == 0x34c)
        return; // No idea. Bug?

    bad_write_word(addr, value);
    return;
}

void int_set(uint32_t int_num, bool on) {
    if (int_num >= 32)
        return;
    const uint32_t prev_active = intr.active;
    const uint32_t prev_raw_status = intr.raw_status;
    const uint32_t prev_status = intr.status;
    const uint32_t prev_irq_pending = intr.active & intr.mask[0] & ~intr.mask[1];
    const uint32_t prev_fiq_pending = intr.active & intr.mask[0] & intr.mask[1];
    if (on) intr.active |= 1 << int_num;
    else    intr.active &= ~(1 << int_num);
    if (((prev_active >> int_num) & 1u) != (on ? 1u : 0u))
        irq_trace_log_transition(int_num, on);
    if (!emulate_cx)
        update();
    else
        update_cx();
    vic_trace_log_transition(int_num, on, prev_active, prev_raw_status, prev_status,
                             prev_irq_pending, prev_fiq_pending);
}

void int_reset() {
    memset(&intr, 0, sizeof intr);
    intr.noninverted = -1;
    intr.priority_limit[0] = 8;
    intr.priority_limit[1] = 8;
}

bool interrupt_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &intr, sizeof(intr));
}

bool interrupt_resume(const emu_snapshot *snapshot)
{
    return snapshot_read(snapshot, &intr, sizeof(intr));
}
