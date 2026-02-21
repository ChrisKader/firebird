#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "emu.h"
#include "disasm.h"
#include "mmu.h"

void backtrace(uint32_t fp) {
    uint32_t *frame;
    gui_debug_printf("Frame     PrvFrame Self     Return   Start\n");
    do {
        gui_debug_printf("%08X:", fp);
        frame = (uint32_t*) virt_mem_ptr(fp - 12, 16);
        if (!frame) {
            gui_debug_printf(" invalid address\n");
            break;
        }
        //vgui_debug_printf(" %08X %08X %08X %08X\n", (void *)frame);
        if (frame[0] <= fp) /* don't get stuck in infinite loop :) */
            break;
        fp = frame[0];
    } while (frame[2] != 0);
}

void dump(uint32_t addr) {
    uint32_t start = addr;
    uint32_t end = addr + 0x7F;

    uint32_t row, col;
    for (row = start & ~0xF; row <= end; row += 0x10) {
        uint8_t *ptr = (uint8_t*) virt_mem_ptr(row, 16);
        if (!ptr) {
            gui_debug_printf("Address %08X is not in RAM.\n", row);
            break;
        }
        gui_debug_printf("%08X  ", row);
        for (col = 0; col < 0x10; col++) {
            addr = row + col;
            if (addr < start || addr > end)
                gui_debug_printf("  ");
            else
                gui_debug_printf("%02X", ptr[col]);
            gui_debug_printf(col == 7 && addr >= start && addr < end ? "-" : " ");
        }
        gui_debug_printf("  ");
        for (col = 0; col < 0x10; col++) {
            addr = row + col;
            if (addr < start || addr > end)
                gui_debug_printf(" ");
            else if (ptr[col] < 0x20)
                gui_debug_printf(".");
            else
            {
                char str[] = {(char) ptr[col], 0};
                gui_debug_printf(str);
            }
        }
        gui_debug_printf("\n");
    }
}

uint32_t parse_expr(char *str) {
    uint32_t sum = 0;
    int sign = 1;
    if (str == NULL)
        return 0;
    while (*str) {
        int reg;
        if (isxdigit(*str)) {
            sum += sign * strtoul(str, &str, 16);
            sign = 1;
        } else if (*str == '+') {
            str++;
        } else if (*str == '-') {
            sign = -1;
            str++;
        } else if (*str == 'v') {
            sum += sign * mmu_translate(strtoul(str + 1, &str, 16), false, NULL, NULL);
            sign = 1;
        } else if (*str == 'r') {
            reg = strtoul(str + 1, &str, 10);
            if(reg > 15)
            {
                gui_debug_printf("Reg number out of range!\n");
                return 0;
            }
            sum += sign * arm.reg[reg];
            sign = 1;
        } else {
            for (reg = 13; reg < 16; reg++) {
                if (!memcmp(str, reg_name[reg], 2)) {
                    str += 2;
                    sum += sign * arm.reg[reg];
                    sign = 1;
                    goto ok;
                }
            }
            gui_debug_printf("syntax error\n");
            return 0;
ok:;
        }
    }
    return sum;
}

uint32_t disasm_insn(uint32_t pc) {
    return (arm.cpsr_low28 & 0x20) ? disasm_thumb_insn(pc) : disasm_arm_insn(pc);
}

void disasm(uint32_t (*dis_func)(uint32_t pc)) {
    char *arg = strtok(NULL, " \n\r");
    uint32_t addr = arg ? parse_expr(arg) : arm.reg[15];
    int i;
    for (i = 0; i < 16; i++) {
        uint32_t len = dis_func(addr);
        if (!len) {
            gui_debug_printf("Address %08X is not in RAM.\n", addr);
            break;
        }
        addr += len;
    }
}
