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

/* -- Registers ----------------------------------------------- */

void debug_get_registers(uint32_t regs_out[16], uint32_t *cpsr_out,
                         uint32_t *spsr_out, bool *has_spsr_out);
bool debug_set_register(int reg_num, uint32_t value);
bool debug_set_cpsr(uint32_t value);
bool debug_is_thumb_mode(void);

/* Read banked registers for a specific mode.
 * mode: MODE_USR/MODE_FIQ/MODE_IRQ/MODE_SVC/MODE_ABT/MODE_UND (from cpu.h)
 * regs_out[16]: filled with r0-r15 for that mode (non-banked regs = current).
 * spsr_out: filled with SPSR for that mode (0 for USR/SYS). */
void debug_get_banked_registers(uint32_t mode, uint32_t regs_out[16],
                                uint32_t *spsr_out);

/* Read key CP15 registers.
 * out[0]=SCTLR, out[1]=TTBR0, out[2]=DACR,
 * out[3]=DFSR, out[4]=IFSR, out[5]=FAR */
void debug_get_cp15(uint32_t out[6]);

/* -- Disassembly --------------------------------------------- */

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

/* -- Memory -------------------------------------------------- */

/* Read/write via virtual (MMU-translated) addresses.
 * Returns number of bytes actually transferred. */
int debug_read_memory(uint32_t vaddr, void *buf, int size);
int debug_write_memory(uint32_t vaddr, const void *buf, int size);

/* -- Breakpoints --------------------------------------------- */

struct debug_breakpoint {
    uint32_t addr;
    uint32_t hit_count;
    uint32_t size;       /* watchpoint range: 1/2/4 or custom byte count */
    bool     exec;
    bool     read;
    bool     write;
    bool     enabled;
};

/* List all active breakpoints. Returns count of entries filled. */
int  debug_list_breakpoints(struct debug_breakpoint *out, int max_count);

/* Set breakpoint flags at `addr`. At least one of exec/read/write must
 * be true. Returns false if the address is not in RAM. */
bool debug_set_breakpoint(uint32_t addr, bool exec, bool read, bool write);

/* Clear all breakpoint flags at `addr`. */
bool debug_clear_breakpoint(uint32_t addr);

/* Enable or disable a breakpoint at `addr` without removing it.
 * Disabled breakpoints remain in the list but don't trigger. */
bool debug_set_breakpoint_enabled(uint32_t addr, bool enabled);

/* Reset the hit counter for the breakpoint at `addr`. */
bool debug_reset_hit_count(uint32_t addr);

/* Increment the hit counter when a breakpoint triggers (called from cpu). */
void debug_increment_hit_count(uint32_t addr);

/* Set a condition expression for the breakpoint at `addr`.
 * Supported syntax: "r0==0x1234", "hit>=5", "[0xA0000000]==0xFF"
 * Pass NULL or "" to clear the condition. */
bool debug_set_breakpoint_condition(uint32_t addr, const char *condition);

/* Get the condition string for the breakpoint at `addr`.
 * Returns "" if no condition is set. */
const char *debug_get_breakpoint_condition(uint32_t addr);

/* Evaluate the condition for the breakpoint at `addr`.
 * Returns true if no condition is set or if condition is met. */
bool debug_evaluate_condition(uint32_t addr);

/* -- Step Out ------------------------------------------------ */

/* Set a temporary breakpoint at the current LR (r14) to implement
 * step-out / "finish" behavior. The breakpoint auto-clears on hit. */
void debug_step_out(void);

/* -- Search -------------------------------------------------- */

/* Search physical memory starting at `start` for `pattern`.
 * Returns the address of the first match, or 0xFFFFFFFF if not found. */
uint32_t debug_search_memory(uint32_t start, uint32_t length,
                             const uint8_t *pattern, int pattern_len);

/* -- Side-effect-free Peripheral Peek ------------------------ */

/* Read a 32-bit peripheral register by physical address WITHOUT going
 * through MMIO dispatch.  Reads directly from the peripheral state structs,
 * so it is safe to call from any thread (no side effects, no locks needed).
 * Returns true if the address was recognized, false otherwise. */
bool debug_peek_reg(uint32_t paddr, uint32_t *out);

/* -- Snapshot Persistence ------------------------------------ */

typedef struct emu_snapshot emu_snapshot;
bool debug_suspend(emu_snapshot *snapshot);
bool debug_resume(const emu_snapshot *snapshot);

/* Clear all breakpoint metadata (used when loading v4 snapshots that
 * don't include debug state, to prevent stale data from persisting). */
void debug_clear_metadata(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_API_H */
