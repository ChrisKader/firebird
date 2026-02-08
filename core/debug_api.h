/* Structured debugger API for GUI widgets.
 *
 * All functions must only be called when:
 *   - in_debugger == true (emu thread blocked waiting for input), OR
 *   - the emulator is not running.
 *
 * Thread-safety is guaranteed by the emu thread being blocked. */

#ifndef DEBUG_API_H
#define DEBUG_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Registers ─────────────────────────────────────────────── */

void debug_get_registers(uint32_t regs_out[16], uint32_t *cpsr_out,
                         uint32_t *spsr_out, bool *has_spsr_out);
bool debug_set_register(int reg_num, uint32_t value);
bool debug_set_cpsr(uint32_t value);
bool debug_is_thumb_mode(void);

/* ── Disassembly ───────────────────────────────────────────── */

struct debug_disasm_line {
    uint32_t addr;
    uint32_t raw;       /* raw instruction word/halfword */
    char     text[96];  /* mnemonic + operands, e.g. "cmp\tr3,00000000" */
    uint8_t  size;      /* 2 (Thumb) or 4 (ARM) */
    bool     is_thumb;
};

/* Disassemble `count` instructions starting at `start_addr`.
 * Returns the number of entries actually filled (may be < count
 * if memory is unmapped). */
int debug_disassemble(uint32_t start_addr, struct debug_disasm_line *out,
                      int count);

/* ── Memory ────────────────────────────────────────────────── */

/* Read/write via virtual (MMU-translated) addresses.
 * Returns number of bytes actually transferred. */
int debug_read_memory(uint32_t vaddr, void *buf, int size);
int debug_write_memory(uint32_t vaddr, const void *buf, int size);

/* ── Breakpoints ───────────────────────────────────────────── */

struct debug_breakpoint {
    uint32_t addr;
    bool     exec;
    bool     read;
    bool     write;
};

/* List all active breakpoints. Returns count of entries filled. */
int  debug_list_breakpoints(struct debug_breakpoint *out, int max_count);

/* Set breakpoint flags at `addr`. At least one of exec/read/write must
 * be true. Returns false if the address is not in RAM. */
bool debug_set_breakpoint(uint32_t addr, bool exec, bool read, bool write);

/* Clear all breakpoint flags at `addr`. */
bool debug_clear_breakpoint(uint32_t addr);

/* ── Search ────────────────────────────────────────────────── */

/* Search physical memory starting at `start` for `pattern`.
 * Returns the address of the first match, or 0xFFFFFFFF if not found. */
uint32_t debug_search_memory(uint32_t start, uint32_t length,
                             const uint8_t *pattern, int pattern_len);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_API_H */
