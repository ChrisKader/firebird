#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include "debug_api.h"
#include "emu.h"
#include "cpu.h"
#include "mem.h"
#include "mmu.h"
#include "disasm.h"
#include "translate.h"
#include "debug.h"
#include "lcd.h"
#include "misc.h"

/* -- Breakpoint metadata side-table ---------------------------- */

#define BP_META_MAX 512

struct bp_meta {
    uint32_t addr;
    uint32_t hit_count;
    uint32_t size;
    uint32_t last_value;
    bool     enabled;
    bool     in_use;
    bool     type_exec, type_read, type_write;
    char     condition[128];
    bool     has_condition;
};

static struct bp_meta bp_meta_table[BP_META_MAX];

/* -- Debug CPU snapshot --------------------------------------- */

struct debug_cpu_snapshot {
    arm_state arm;
    uint32_t cpsr;
    uint32_t spsr;
    bool has_spsr;
    bool valid;
};

static struct debug_cpu_snapshot g_debug_cpu_snapshot = {};

static void debug_capture_cpu_snapshot_locked(void)
{
    memcpy(&g_debug_cpu_snapshot.arm, &arm, sizeof(g_debug_cpu_snapshot.arm));
    g_debug_cpu_snapshot.cpsr = get_cpsr();
    const uint32_t mode = g_debug_cpu_snapshot.cpsr & 0x1F;
    g_debug_cpu_snapshot.has_spsr = (mode != MODE_USR && mode != MODE_SYS);
    g_debug_cpu_snapshot.spsr = g_debug_cpu_snapshot.has_spsr ? get_spsr() : 0;
    g_debug_cpu_snapshot.valid = true;
}

static const arm_state *debug_cpu_state(void)
{
    if (!g_debug_cpu_snapshot.valid && in_debugger)
        debug_capture_cpu_snapshot_locked();
    return g_debug_cpu_snapshot.valid ? &g_debug_cpu_snapshot.arm : &arm;
}

static uint32_t debug_current_cpsr(void)
{
    if (!g_debug_cpu_snapshot.valid && in_debugger)
        debug_capture_cpu_snapshot_locked();
    return g_debug_cpu_snapshot.valid ? g_debug_cpu_snapshot.cpsr : get_cpsr();
}

static bool debug_current_spsr(uint32_t *out)
{
    if (!out)
        return false;
    if (!g_debug_cpu_snapshot.valid && in_debugger)
        debug_capture_cpu_snapshot_locked();
    if (g_debug_cpu_snapshot.valid) {
        *out = g_debug_cpu_snapshot.spsr;
        return g_debug_cpu_snapshot.has_spsr;
    }

    const uint32_t mode = get_cpsr() & 0x1F;
    const bool has_spsr = (mode != MODE_USR && mode != MODE_SYS);
    *out = has_spsr ? get_spsr() : 0;
    return has_spsr;
}

static struct bp_meta *bp_meta_find(uint32_t addr)
{
    for (int i = 0; i < BP_META_MAX; i++) {
        if (bp_meta_table[i].in_use && bp_meta_table[i].addr == addr)
            return &bp_meta_table[i];
    }
    return NULL;
}

static struct bp_meta *bp_meta_alloc(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (m) return m;

    for (int i = 0; i < BP_META_MAX; i++) {
        if (!bp_meta_table[i].in_use) {
            bp_meta_table[i].in_use = true;
            bp_meta_table[i].addr = addr;
            bp_meta_table[i].hit_count = 0;
            bp_meta_table[i].size = 1;
            bp_meta_table[i].last_value = 0;
            bp_meta_table[i].enabled = true;
            bp_meta_table[i].type_exec = false;
            bp_meta_table[i].type_read = false;
            bp_meta_table[i].type_write = false;
            bp_meta_table[i].condition[0] = '\0';
            bp_meta_table[i].has_condition = false;
            return &bp_meta_table[i];
        }
    }
    return NULL;
}

static void bp_meta_free(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (m) m->in_use = false;
}

/* -- Registers ----------------------------------------------- */

void debug_get_registers(uint32_t regs_out[16], uint32_t *cpsr_out,
                         uint32_t *spsr_out, bool *has_spsr_out)
{
    const arm_state *state = debug_cpu_state();
    for (int i = 0; i < 16; i++)
        regs_out[i] = state->reg[i];
    *cpsr_out = debug_current_cpsr();
    *has_spsr_out = debug_current_spsr(spsr_out);
}

bool debug_set_register(int reg_num, uint32_t value)
{
    if (reg_num < 0 || reg_num > 15)
        return false;
    arm.reg[reg_num] = value;
    if (g_debug_cpu_snapshot.valid)
        g_debug_cpu_snapshot.arm.reg[reg_num] = value;
    return true;
}

bool debug_set_cpsr(uint32_t value)
{
    set_cpsr_full(value);
    if (g_debug_cpu_snapshot.valid)
        debug_capture_cpu_snapshot_locked();
    return true;
}

bool debug_is_thumb_mode(void)
{
    return (debug_current_cpsr() >> 5) & 1;
}

void debug_get_banked_registers(uint32_t mode, uint32_t regs_out[16],
                                uint32_t *spsr_out)
{
    const arm_state *state = debug_cpu_state();
    uint32_t cur_mode = debug_current_cpsr() & 0x1F;
    *spsr_out = 0;

    /* Start with current registers for everything */
    for (int i = 0; i < 16; i++)
        regs_out[i] = state->reg[i];

    if (mode == cur_mode || mode == MODE_SYS) {
        /* Current mode or SYS (shares USR regs) -- just return current */
        if (!debug_current_spsr(spsr_out))
            *spsr_out = 0;
        return;
    }

    /* The current mode's banked regs are swapped OUT into their bank arrays.
     * The requested mode's banked regs are in THEIR bank arrays.
     * r0-r7 are never banked (except r8-r12 for FIQ). */

    switch (mode) {
    case MODE_USR:
        regs_out[13] = state->r13_usr[0];
        regs_out[14] = state->r13_usr[1];
        if (cur_mode == MODE_FIQ) {
            for (int i = 0; i < 5; i++)
                regs_out[8 + i] = state->r8_usr[i];
        }
        break;
    case MODE_FIQ:
        for (int i = 0; i < 5; i++)
            regs_out[8 + i] = state->r8_fiq[i];
        regs_out[13] = state->r13_fiq[0];
        regs_out[14] = state->r13_fiq[1];
        *spsr_out = state->spsr_fiq;
        break;
    case MODE_IRQ:
        regs_out[13] = state->r13_irq[0];
        regs_out[14] = state->r13_irq[1];
        *spsr_out = state->spsr_irq;
        break;
    case MODE_SVC:
        regs_out[13] = state->r13_svc[0];
        regs_out[14] = state->r13_svc[1];
        *spsr_out = state->spsr_svc;
        break;
    case MODE_ABT:
        regs_out[13] = state->r13_abt[0];
        regs_out[14] = state->r13_abt[1];
        *spsr_out = state->spsr_abt;
        break;
    case MODE_UND:
        regs_out[13] = state->r13_und[0];
        regs_out[14] = state->r13_und[1];
        *spsr_out = state->spsr_und;
        break;
    default:
        break;
    }
}

void debug_get_cp15(uint32_t out[6])
{
    const arm_state *state = debug_cpu_state();
    out[0] = state->control;
    out[1] = state->translation_table_base;
    out[2] = state->domain_access_control;
    out[3] = state->data_fault_status;
    out[4] = state->instruction_fault_status;
    out[5] = state->fault_address;
}

/* -- Disassembly --------------------------------------------- */

int debug_disassemble(uint32_t start_addr, struct debug_disasm_line *out,
                      int count)
{
    uint32_t addr = start_addr;
    bool is_thumb = (debug_current_cpsr() >> 5) & 1;
    int filled = 0;

    for (int i = 0; i < count; i++) {
        struct debug_disasm_line *line = &out[filled];
        line->addr = addr;
        line->is_thumb = is_thumb;

        uint32_t size;
        if (is_thumb) {
            size = disasm_thumb_insn_buf(addr, line->text, sizeof(line->text),
                                         &line->raw);
        } else {
            size = disasm_arm_insn_buf(addr, line->text, sizeof(line->text),
                                       &line->raw);
        }

        if (size == 0)
            break; /* unmapped memory */

        line->size = (uint8_t)size;
        addr += size;
        filled++;
    }

    return filled;
}

/* -- Memory -------------------------------------------------- */

/* Try to read a single word via MMIO dispatch (for peripheral registers).
 * Returns true on success, false if the address caused an error. */
static bool try_mmio_read_word(uint32_t paddr, uint32_t *out)
{
    if (setjmp(debugger_error_jmp) == 0) {
        debugger_error_handler_active = true;
        *out = mmio_read_word(paddr);
        debugger_error_handler_active = false;
        return true;
    }
    debugger_error_handler_active = false;
    return false;
}

int debug_read_memory(uint32_t vaddr, void *buf, int size)
{
    uint8_t *dst = (uint8_t *)buf;
    int total = 0;

    while (total < size) {
        /* Translate one page at a time */
        uint32_t page_off = vaddr & 0xFFF;
        uint32_t chunk = 0x1000 - page_off;
        if (chunk > (uint32_t)(size - total))
            chunk = size - total;

        void *ptr = virt_mem_ptr(vaddr, chunk);
        if (ptr) {
            memcpy(dst, ptr, chunk);
            dst += chunk;
            vaddr += chunk;
            total += chunk;
            continue;
        }

        /* virt_mem_ptr failed -- try MMIO word-at-a-time.
         * MMIO dispatch is NOT thread-safe, so only attempt this when the
         * emulator is paused (in_debugger == true). */
        if (!in_debugger)
            goto done;

        uint32_t paddr = mmu_translate(vaddr, false, NULL, NULL);
        if (paddr == 0xFFFFFFFF)
            goto done; /* Translation failed -- unmapped address */

        /* Suppress warnings during debug MMIO reads (e.g. hex view scanning
         * through LCD address space would spam "Bad read_word" otherwise). */
        debug_suppress_warn = true;
        while (chunk >= 4 && total + 4 <= size) {
            uint32_t word;
            if (!try_mmio_read_word(paddr, &word))
                break;
            memcpy(dst, &word, 4);
            dst += 4;
            vaddr += 4;
            paddr += 4;
            total += 4;
            chunk -= 4;
        }
        /* Remaining unaligned bytes: read one more word and extract partial */
        if (chunk > 0 && total < size) {
            uint32_t word = 0;
            if (try_mmio_read_word(paddr & ~3u, &word)) {
                uint32_t byte_off = paddr & 3;
                uint8_t *wp = (uint8_t *)&word;
                while (chunk > 0 && total < size) {
                    *dst++ = wp[byte_off++];
                    vaddr++;
                    paddr++;
                    total++;
                    chunk--;
                }
            }
        }
        debug_suppress_warn = false;
        if (total < size)
            continue;
        break;
    }
done:
    return total;
}

int debug_write_memory(uint32_t vaddr, const void *buf, int size)
{
    const uint8_t *src = (const uint8_t *)buf;
    int total = 0;

    while (total < size) {
        uint32_t page_off = vaddr & 0xFFF;
        uint32_t chunk = 0x1000 - page_off;
        if (chunk > (uint32_t)(size - total))
            chunk = size - total;

        void *ptr = virt_mem_ptr(vaddr, chunk);
        if (!ptr)
            break;

        memcpy(ptr, src, chunk);
        src += chunk;
        vaddr += chunk;
        total += chunk;
    }

    return total;
}

/* -- Breakpoints --------------------------------------------- */

int debug_list_breakpoints(struct debug_breakpoint *out, int max_count)
{
    int count = 0;

    /* Metadata-driven: iterate all entries, check RAM flags when available */
    for (int i = 0; i < BP_META_MAX && count < max_count; i++) {
        if (!bp_meta_table[i].in_use)
            continue;

        uint32_t addr = bp_meta_table[i].addr;
        out[count].addr      = addr;
        out[count].hit_count = bp_meta_table[i].hit_count;
        out[count].size      = bp_meta_table[i].size;
        out[count].enabled   = bp_meta_table[i].enabled;

        if (bp_meta_table[i].enabled) {
            /* Check RAM flags for active breakpoints */
            void *ptr = virt_mem_ptr(addr & ~3, 4);
            if (!ptr) ptr = phys_mem_ptr(addr & ~3, 4);
            if (ptr) {
                uint32_t flags = RAM_FLAGS(ptr);
                out[count].exec  = (flags & RF_EXEC_BREAKPOINT) != 0;
                out[count].read  = (flags & RF_READ_BREAKPOINT) != 0;
                out[count].write = (flags & RF_WRITE_BREAKPOINT) != 0;
            } else {
                /* MMIO watchpoint: use stored type */
                out[count].exec  = bp_meta_table[i].type_exec;
                out[count].read  = bp_meta_table[i].type_read;
                out[count].write = bp_meta_table[i].type_write;
            }
        } else {
            /* Disabled: use stored type */
            out[count].exec  = bp_meta_table[i].type_exec;
            out[count].read  = bp_meta_table[i].type_read;
            out[count].write = bp_meta_table[i].type_write;
        }
        count++;
    }

    return count;
}

bool debug_set_breakpoint(uint32_t addr, bool exec, bool read, bool write)
{
    void *ptr = virt_mem_ptr(addr & ~3, 4);
    if (!ptr)
        ptr = phys_mem_ptr(addr & ~3, 4);

    if (ptr) {
        /* RAM/ROM address: set hardware breakpoint flags */
        uint32_t *flags = &RAM_FLAGS(ptr);
        if (exec) {
            if (*flags & RF_CODE_TRANSLATED)
                flush_translations();
            *flags |= RF_EXEC_BREAKPOINT;
        }
        if (read)
            *flags |= RF_READ_BREAKPOINT;
        if (write)
            *flags |= RF_WRITE_BREAKPOINT;
    } else if (exec) {
        /* Can't set exec breakpoint on non-RAM (MMIO) address */
        return false;
    }
    /* MMIO addresses: watchpoints are metadata-only (show value, no break) */

    struct bp_meta *m = bp_meta_alloc(addr);
    if (!m)
        return false;
    m->type_exec = exec;
    m->type_read = read;
    m->type_write = write;

    return true;
}

bool debug_clear_breakpoint(uint32_t addr)
{
    void *ptr = virt_mem_ptr(addr & ~3, 4);
    if (!ptr)
        ptr = phys_mem_ptr(addr & ~3, 4);
    if (ptr)
        RAM_FLAGS(ptr) &= ~(RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT);

    bp_meta_free(addr);
    return true;
}

bool debug_set_breakpoint_enabled(uint32_t addr, bool enabled)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m) return false;

    if (enabled && !m->enabled) {
        /* Re-enable: set RAM flags back. We need to know what type it was.
         * The meta doesn't store type, so we just set exec (most common).
         * Callers should re-set the breakpoint with proper flags. */
        m->enabled = true;
    } else if (!enabled && m->enabled) {
        /* Disable: clear RAM flags but keep metadata */
        void *ptr = virt_mem_ptr(addr & ~3, 4);
        if (!ptr)
            ptr = phys_mem_ptr(addr & ~3, 4);
        if (ptr)
            RAM_FLAGS(ptr) &= ~(RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT);
        m->enabled = false;
    }

    return true;
}

bool debug_set_breakpoint_size(uint32_t addr, uint32_t size)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m) return false;
    m->size = size;
    return true;
}

bool debug_reset_hit_count(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m) return false;
    m->hit_count = 0;
    return true;
}

void debug_increment_hit_count(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (m) m->hit_count++;
}

void debug_watchpoint_update_value(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m) return;
    /* Read the current value from physical memory */
    void *ptr = phys_mem_ptr(addr & ~3, 4);
    if (ptr)
        m->last_value = *(uint32_t *)ptr;
}

uint32_t debug_get_watchpoint_value(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    return m ? m->last_value : 0;
}

/* -- Conditional Breakpoints --------------------------------- */

bool debug_set_breakpoint_condition(uint32_t addr, const char *condition)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m) return false;

    if (!condition || condition[0] == '\0') {
        m->condition[0] = '\0';
        m->has_condition = false;
    } else {
        strncpy(m->condition, condition, sizeof(m->condition) - 1);
        m->condition[sizeof(m->condition) - 1] = '\0';
        m->has_condition = true;
    }
    return true;
}

const char *debug_get_breakpoint_condition(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m || !m->has_condition) return "";
    return m->condition;
}

/* Parse and evaluate a simple condition expression.
 * Supported forms:
 *   rN==VALUE, rN!=VALUE, rN<VALUE, rN>VALUE, rN<=VALUE, rN>=VALUE
 *   hit==N, hit>=N, hit<N
 *   [ADDR]==VALUE, [ADDR]!=VALUE
 * All values are hex (0x prefix optional). */
bool debug_evaluate_condition(uint32_t addr)
{
    struct bp_meta *m = bp_meta_find(addr);
    if (!m || !m->has_condition)
        return true; /* No condition = always trigger */

    const char *cond = m->condition;

    /* Register comparison: rN op VALUE */
    if (cond[0] == 'r' || cond[0] == 'R') {
        int reg_num = 0;
        const char *p = cond + 1;
        while (*p >= '0' && *p <= '9') {
            reg_num = reg_num * 10 + (*p - '0');
            p++;
        }
        if (reg_num > 15) return true;

        uint32_t reg_val = arm.reg[reg_num];
        uint32_t cmp_val = 0;
        bool eq = false, ne = false, lt = false, gt = false, le = false, ge = false;

        if (p[0] == '=' && p[1] == '=') { eq = true; p += 2; }
        else if (p[0] == '!' && p[1] == '=') { ne = true; p += 2; }
        else if (p[0] == '<' && p[1] == '=') { le = true; p += 2; }
        else if (p[0] == '>' && p[1] == '=') { ge = true; p += 2; }
        else if (p[0] == '<') { lt = true; p += 1; }
        else if (p[0] == '>') { gt = true; p += 1; }
        else return true;

        cmp_val = (uint32_t)strtoul(p, NULL, 0);
        if (eq) return reg_val == cmp_val;
        if (ne) return reg_val != cmp_val;
        if (lt) return reg_val < cmp_val;
        if (gt) return reg_val > cmp_val;
        if (le) return reg_val <= cmp_val;
        if (ge) return reg_val >= cmp_val;
    }

    /* Hit count comparison: hit op N */
    if (strncmp(cond, "hit", 3) == 0) {
        const char *p = cond + 3;
        uint32_t hit = m->hit_count;
        uint32_t cmp_val = 0;

        if (p[0] == '>' && p[1] == '=') { cmp_val = (uint32_t)strtoul(p + 2, NULL, 0); return hit >= cmp_val; }
        if (p[0] == '=' && p[1] == '=') { cmp_val = (uint32_t)strtoul(p + 2, NULL, 0); return hit == cmp_val; }
        if (p[0] == '<') { cmp_val = (uint32_t)strtoul(p + 1, NULL, 0); return hit < cmp_val; }
        return true;
    }

    /* Memory comparison: [ADDR] op VALUE */
    if (cond[0] == '[') {
        const char *p = cond + 1;
        uint32_t mem_addr = (uint32_t)strtoul(p, (char **)&p, 0);
        if (*p == ']') p++;

        uint32_t mem_val = 0;
        debug_read_memory(mem_addr, &mem_val, 4);

        if (p[0] == '=' && p[1] == '=') return mem_val == (uint32_t)strtoul(p + 2, NULL, 0);
        if (p[0] == '!' && p[1] == '=') return mem_val != (uint32_t)strtoul(p + 2, NULL, 0);
        return true;
    }

    return true; /* Unrecognized condition = always trigger */
}

/* -- Step Out ------------------------------------------------ */

void debug_step_out(void)
{
    uint32_t lr = arm.reg[14];
    void *ptr = virt_mem_ptr(lr & ~3, 4);
    if (!ptr) return;

    RAM_FLAGS(ptr) |= RF_EXEC_DEBUG_NEXT;
}

/* -- Search -------------------------------------------------- */

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

/* -- Side-effect-free Peripheral Peek ------------------------ */

/* Read peripheral registers directly from state structs.
 * This bypasses the MMIO dispatch machinery entirely, so it's safe to call
 * from any thread while the emulator is running.  Individual aligned 32-bit
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

/* -- Debug CPU Snapshot -------------------------------------- */

void debug_capture_cpu_snapshot(void)
{
    if (!in_debugger) {
        g_debug_cpu_snapshot.valid = false;
        return;
    }
    debug_capture_cpu_snapshot_locked();
}

void debug_invalidate_cpu_snapshot(void)
{
    g_debug_cpu_snapshot.valid = false;
}

bool debug_has_cpu_snapshot(void)
{
    return g_debug_cpu_snapshot.valid;
}

/* -- Snapshot Persistence ------------------------------------ */

/* On-disk format:
 *   uint32_t count          (number of saved breakpoints)
 *   For each:
 *     uint32_t addr
 *     uint32_t hit_count
 *     uint32_t size
 *     uint8_t  enabled
 *     uint8_t  has_condition
 *     uint8_t  flags  (RF_EXEC|RF_READ|RF_WRITE breakpoint bits)
 *     uint8_t  pad
 *     char     condition[128]
 */

struct bp_save_entry {
    uint32_t addr;
    uint32_t hit_count;
    uint32_t size;
    uint8_t  enabled;
    uint8_t  has_condition;
    uint8_t  flags;
    uint8_t  pad;
    char     condition[128];
};

void debug_clear_metadata(void)
{
    memset(bp_meta_table, 0, sizeof(bp_meta_table));
    debug_invalidate_cpu_snapshot();
}

bool debug_suspend(emu_snapshot *snapshot)
{
    /* Count active breakpoints from both RAM flags and disabled metadata */
    uint32_t count = 0;
    for (int i = 0; i < BP_META_MAX; i++) {
        if (bp_meta_table[i].in_use)
            count++;
    }

    if (!snapshot_write(snapshot, &count, sizeof(count)))
        return false;

    for (int i = 0; i < BP_META_MAX; i++) {
        if (!bp_meta_table[i].in_use)
            continue;

        struct bp_save_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.addr = bp_meta_table[i].addr;
        entry.hit_count = bp_meta_table[i].hit_count;
        entry.size = bp_meta_table[i].size;
        entry.enabled = bp_meta_table[i].enabled ? 1 : 0;
        entry.has_condition = bp_meta_table[i].has_condition ? 1 : 0;

        /* Read current flags from RAM */
        void *ptr = virt_mem_ptr(entry.addr & ~3, 4);
        if (ptr)
            entry.flags = RAM_FLAGS(ptr) & (RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT);

        memcpy(entry.condition, bp_meta_table[i].condition, 128);

        if (!snapshot_write(snapshot, &entry, sizeof(entry)))
            return false;
    }

    return true;
}

bool debug_resume(const emu_snapshot *snapshot)
{
    /* Clear existing metadata */
    memset(bp_meta_table, 0, sizeof(bp_meta_table));
    debug_invalidate_cpu_snapshot();

    uint32_t count = 0;
    if (!snapshot_read(snapshot, &count, sizeof(count)))
        return false;

    for (uint32_t i = 0; i < count; i++) {
        struct bp_save_entry entry;
        if (!snapshot_read(snapshot, &entry, sizeof(entry)))
            return false;

        /* Skip entries beyond our capacity (but still consume from stream) */
        if (i >= BP_META_MAX)
            continue;

        struct bp_meta *m = bp_meta_alloc(entry.addr);
        if (!m) continue;

        m->hit_count = entry.hit_count;
        m->size = entry.size;
        m->enabled = entry.enabled != 0;
        m->has_condition = entry.has_condition != 0;
        memcpy(m->condition, entry.condition, 128);

        /* Restore RAM flags */
        if (m->enabled) {
            void *ptr = virt_mem_ptr(entry.addr & ~3, 4);
            if (ptr)
                RAM_FLAGS(ptr) |= entry.flags;
        }
    }

    return true;
}
