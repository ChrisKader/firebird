#ifndef _H_DISASM
#define _H_DISASM

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char reg_name[16][4];

uint32_t disasm_arm_insn(uint32_t pc);
uint32_t disasm_arm_insn2(uint32_t pc, uint32_t *pc_ptr);
uint32_t disasm_thumb_insn(uint32_t pc);

/* Buffer-output variants: write mnemonic text to caller-provided buffer
 * instead of gui_debug_printf(). Return instruction size (2/4), or 0
 * if the address is unmapped. *raw_out receives the raw instruction word. */
uint32_t disasm_arm_insn_buf(uint32_t pc, char *buf, int buf_size, uint32_t *raw_out);
uint32_t disasm_thumb_insn_buf(uint32_t pc, char *buf, int buf_size, uint32_t *raw_out);

#ifdef __cplusplus
}
#endif

#endif
