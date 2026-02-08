#include <string.h>
#include "debug_api.h"
#include "cpu.h"
#include "mem.h"
#include "disasm.h"
#include "translate.h"
#include "debug.h"

/* ── Registers ─────────────────────────────────────────────── */

void debug_get_registers(uint32_t regs_out[16], uint32_t *cpsr_out,
                         uint32_t *spsr_out, bool *has_spsr_out)
{
    for (int i = 0; i < 16; i++)
        regs_out[i] = arm.reg[i];
    *cpsr_out = get_cpsr();

    uint32_t mode = *cpsr_out & 0x1F;
    bool has_spsr = (mode != MODE_USR && mode != MODE_SYS);
    *has_spsr_out = has_spsr;
    *spsr_out = has_spsr ? get_spsr() : 0;
}

bool debug_set_register(int reg_num, uint32_t value)
{
    if (reg_num < 0 || reg_num > 15)
        return false;
    arm.reg[reg_num] = value;
    return true;
}

bool debug_set_cpsr(uint32_t value)
{
    set_cpsr_full(value);
    return true;
}

bool debug_is_thumb_mode(void)
{
    return (get_cpsr() >> 5) & 1;
}

/* ── Disassembly ───────────────────────────────────────────── */

int debug_disassemble(uint32_t start_addr, struct debug_disasm_line *out,
                      int count)
{
    uint32_t addr = start_addr;
    bool is_thumb = (get_cpsr() >> 5) & 1;
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

/* ── Memory ────────────────────────────────────────────────── */

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
        if (!ptr)
            break;

        memcpy(dst, ptr, chunk);
        dst += chunk;
        vaddr += chunk;
        total += chunk;
    }

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

/* ── Breakpoints ───────────────────────────────────────────── */

int debug_list_breakpoints(struct debug_breakpoint *out, int max_count)
{
    int count = 0;

    for (unsigned area = 0; area < sizeof(mem_areas) / sizeof(*mem_areas); area++) {
        if (!mem_areas[area].ptr)
            continue;
        uint32_t *flags_start = &RAM_FLAGS(mem_areas[area].ptr);
        uint32_t *flags_end = &RAM_FLAGS(mem_areas[area].ptr + mem_areas[area].size);

        for (uint32_t *flags = flags_start; flags != flags_end; flags++) {
            if (*flags & (RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT)) {
                if (count >= max_count)
                    return count;

                uint32_t addr = mem_areas[area].base +
                    (uint32_t)((uint8_t *)flags - (uint8_t *)flags_start);
                out[count].addr  = addr;
                out[count].exec  = (*flags & RF_EXEC_BREAKPOINT) != 0;
                out[count].read  = (*flags & RF_READ_BREAKPOINT) != 0;
                out[count].write = (*flags & RF_WRITE_BREAKPOINT) != 0;
                count++;
            }
        }
    }

    return count;
}

bool debug_set_breakpoint(uint32_t addr, bool exec, bool read, bool write)
{
    void *ptr = virt_mem_ptr(addr & ~3, 4);
    if (!ptr)
        return false;

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

    return true;
}

bool debug_clear_breakpoint(uint32_t addr)
{
    void *ptr = virt_mem_ptr(addr & ~3, 4);
    if (!ptr)
        return false;

    RAM_FLAGS(ptr) &= ~(RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT);
    return true;
}

/* ── Search ────────────────────────────────────────────────── */

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
