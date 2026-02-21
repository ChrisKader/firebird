#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <condition_variable>

#include "armsnippets.h"
#include "debug.h"
#include "interrupt.h"
#include "emu.h"
#include "cpu/cpu.h"
#include "memory/mem.h"
#include "disasm.h"
#include "memory/mmu.h"
#include "cpu/translate.h"
#include "usb/usblink_queue.h"
#include "gdbstub.h"
#include "debug_api.h"
#include "nspire_log_hook.h"
#include "os/os.h"

std::string ln_target_folder;

// Error handler for catching errors during debugger MMIO operations
jmp_buf debugger_error_jmp;
bool debugger_error_handler_active = false;

// Used for debugger input
static std::mutex debug_input_m;
static std::condition_variable debug_input_cv;
static const char * debug_input_cur = nullptr;

static void debug_input_callback(const char *input)
{
    std::unique_lock<std::mutex> lk(debug_input_m);

    debug_input_cur = input;

    debug_input_cv.notify_all();
}

uint32_t parse_expr(char *str);
void dump(uint32_t addr);
uint32_t disasm_insn(uint32_t pc);
void disasm(uint32_t (*dis_func)(uint32_t pc));

void *virt_mem_ptr(uint32_t addr, uint32_t size) {
    // Note: this is not guaranteed to be correct when range crosses page boundary
    return (void *)(intptr_t)phys_mem_ptr(mmu_translate(addr, false, NULL, NULL), size);
}

uint32_t *debug_next;
static void set_debug_next(uint32_t *next) {
    if (debug_next != NULL)
        RAM_FLAGS(debug_next) &= ~RF_EXEC_DEBUG_NEXT;
    if (next != NULL) {
        if (RAM_FLAGS(next) & RF_CODE_TRANSLATED)
            flush_translations();
        RAM_FLAGS(next) |= RF_EXEC_DEBUG_NEXT;
    }
    debug_next = next;
}

bool gdb_connected = false;

// return 1: break (should stop being feed with debugger commands), 0: continue (can be feed with other debugger commands)
int process_debug_cmd(char *cmdline) {
    char *cmd = strtok(cmdline, " \n\r");
    if (!cmd)
        return 0;

    if (!strcasecmp(cmd, "?") || !strcasecmp(cmd, "h")) {
        gui_debug_printf(
                    "Debugger commands:\n"
                    "b - stack backtrace\n"
                    "c - continue\n"
                    "d <address> - dump memory\n"
                    "k <address> <+r|+w|+x|-r|-w|-x> - add/remove breakpoint\n"
                    "k - show breakpoints\n"
                    "ln c - connect\n"
                    "ln s <file> - send a file\n"
                    "ln st <dir> - set target directory\n"
                    "mmu - dump memory mappings\n"
                    "nlog [on|off|scan|status] - TI virtual log hook control\n"
                    "nlog bypass [on|off|status] - bypass OS debug_log filters\n"
                    "n - continue until next instruction\n"
                    "pr <address> - port or memory read\n"
                    "pw <address> <value> - port or memory write\n"
                    "r - show registers\n"
                    "rs <regnum> <value> - change register value\n"
                    "ss <address> <length> <string> - search a string\n"
                    "s - step instruction\n"
                    "t+ - enable instruction translation\n"
                    "t- - disable instruction translation\n"
                    "u[a|t] [address] - disassemble memory\n"
                    "wm <file> <start> <size> - write memory to file\n"
                    "wf <file> <start> [size] - write file to memory\n"
                    "stop - stop the emulation\n"
                    "exec <path> - exec file with ndless\n");
    } else if (!strcasecmp(cmd, "b")) {
        char *fp = strtok(NULL, " \n\r");
        backtrace(fp ? parse_expr(fp) : arm.reg[11]);
    } else if (!strcasecmp(cmd, "mmu")) {
        mmu_dump_tables();
    } else if (!strcasecmp(cmd, "nlog")) {
        char *sub = strtok(NULL, " \n\r");
        if (!sub || !strcasecmp(sub, "status")) {
            nspire_log_hook_status();
        } else if (!strcasecmp(sub, "bypass")) {
            char *mode = strtok(NULL, " \n\r");
            if (!mode || !strcasecmp(mode, "status")) {
                gui_nlog_printf("nlog: bypass enabled=%s installed=%s\n",
                                nspire_log_hook_filter_bypass_is_enabled() ? "yes" : "no",
                                nspire_log_hook_filter_bypass_is_installed() ? "yes" : "no");
            } else if (!strcasecmp(mode, "on")) {
                nspire_log_hook_set_filter_bypass(true);
                gui_nlog_printf("nlog: bypass enabled=%s installed=%s\n",
                                nspire_log_hook_filter_bypass_is_enabled() ? "yes" : "no",
                                nspire_log_hook_filter_bypass_is_installed() ? "yes" : "no");
            } else if (!strcasecmp(mode, "off")) {
                nspire_log_hook_set_filter_bypass(false);
                gui_nlog_printf("nlog: bypass enabled=%s installed=%s\n",
                                nspire_log_hook_filter_bypass_is_enabled() ? "yes" : "no",
                                nspire_log_hook_filter_bypass_is_installed() ? "yes" : "no");
            } else {
                gui_nlog_printf("nlog: bypass expects on/off/status\n");
            }
        } else if (!strcasecmp(sub, "on")) {
            nspire_log_hook_set_enabled(true);
            nspire_log_hook_status();
        } else if (!strcasecmp(sub, "off")) {
            nspire_log_hook_set_enabled(false);
            nspire_log_hook_status();
        } else if (!strcasecmp(sub, "scan")) {
            nspire_log_hook_scan_now();
            nspire_log_hook_status();
        } else {
            gui_nlog_printf("nlog: expected on/off/scan/status/bypass\n");
        }
    } else if (!strcasecmp(cmd, "r")) {
        int i, show_spsr;
        uint32_t cpsr = get_cpsr();
        const char *mode;
        for (i = 0; i < 16; i++) {
            int newline = ((1 << 5) | (1 << 11) | (1 << 15)) & (1 << i);
            gui_debug_printf("%3s=%08x%c", reg_name[i], arm.reg[i], newline ? '\n' : ' ');
        }
        switch (cpsr & 0x1F) {
            case MODE_USR: mode = "usr"; show_spsr = 0; break;
            case MODE_SYS: mode = "sys"; show_spsr = 0; break;
            case MODE_FIQ: mode = "fiq"; show_spsr = 1; break;
            case MODE_IRQ: mode = "irq"; show_spsr = 1; break;
            case MODE_SVC: mode = "svc"; show_spsr = 1; break;
            case MODE_ABT: mode = "abt"; show_spsr = 1; break;
            case MODE_UND: mode = "und"; show_spsr = 1; break;
            default:       mode = "???"; show_spsr = 0; break;
        }
        gui_debug_printf("cpsr=%08x (N=%d Z=%d C=%d V=%d Q=%d IRQ=%s FIQ=%s T=%d Mode=%s)",
                         cpsr,
                         arm.cpsr_n, arm.cpsr_z, arm.cpsr_c, arm.cpsr_v,
                         cpsr >> 27 & 1,
                         (cpsr & 0x80) ? "off" : "on ",
                         (cpsr & 0x40) ? "off" : "on ",
                         cpsr >> 5 & 1,
                         mode);
        if (show_spsr)
            gui_debug_printf(" spsr=%08x", get_spsr());
        gui_debug_printf("\n");
    } else if (!strcasecmp(cmd, "rs")) {
        char *reg = strtok(NULL, " \n\r");
        if (!reg) {
            gui_debug_printf("Parameters are missing.\n");
        } else {
            char *value = strtok(NULL, " \n\r");
            if (!value) {
                gui_debug_printf("Missing value parameter.\n");
            } else {
                int regi = atoi(reg);
                int valuei = parse_expr(value);
                if (regi >= 0 && regi <= 15)
                    arm.reg[regi] = valuei;
                else
                    gui_debug_printf("Invalid register.\n");
            }
        }
    } else if (!strcasecmp(cmd, "k")) {
        const char *addr_str = strtok(NULL, " \n\r");
        const char *flag_str = strtok(NULL, " \n\r");
        if (!flag_str)
            flag_str = "+x";
        if (addr_str) {
            uint32_t addr = parse_expr((char*) addr_str);
            void *ptr = virt_mem_ptr(addr & ~3, 4);
            if (ptr) {
                uint32_t *flags = &RAM_FLAGS(ptr);
                bool on = true;
                for (; *flag_str; flag_str++) {
                    switch (tolower(*flag_str)) {
                        case '+': on = true; break;
                        case '-': on = false; break;
                        case 'r':
                            if (on) *flags |= RF_READ_BREAKPOINT;
                            else *flags &= ~RF_READ_BREAKPOINT;
                            break;
                        case 'w':
                            if (on) *flags |= RF_WRITE_BREAKPOINT;
                            else *flags &= ~RF_WRITE_BREAKPOINT;
                            break;
                        case 'x':
                            if (on) {
                                if (*flags & RF_CODE_TRANSLATED) flush_translations();
                                *flags |= RF_EXEC_BREAKPOINT;
                            } else
                                *flags &= ~RF_EXEC_BREAKPOINT;
                            break;
                    }
                }
            } else {
                gui_debug_printf("Address %08X is not in RAM.\n", addr);
            }
        } else {
            unsigned int area;
            for (area = 0; area < sizeof(mem_areas)/sizeof(*mem_areas); area++) {
                uint32_t *flags;
                uint32_t *flags_start = &RAM_FLAGS(mem_areas[area].ptr);
                uint32_t *flags_end = &RAM_FLAGS(mem_areas[area].ptr + mem_areas[area].size);
                for (flags = flags_start; flags != flags_end; flags++) {
                    uint32_t addr = mem_areas[area].base + ((uint8_t *)flags - (uint8_t *)flags_start);
                    if (*flags & (RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT)) {
                        gui_debug_printf("%08x %c%c%c\n",
                                         addr,
                                         (*flags & RF_READ_BREAKPOINT)  ? 'r' : ' ',
                                         (*flags & RF_WRITE_BREAKPOINT) ? 'w' : ' ',
                                         (*flags & RF_EXEC_BREAKPOINT)  ? 'x' : ' ');
                    }
                }
            }
        }
    } else if (!strcasecmp(cmd, "c")) {
        return 1;
    } else if (!strcasecmp(cmd, "s")) {
        cpu_events |= EVENT_DEBUG_STEP;
        return 1;
    } else if (!strcasecmp(cmd, "n")) {
        set_debug_next((uint32_t*) virt_mem_ptr(arm.reg[15] & ~3, 4) + 1);
        return 1;
    } else if (!strcasecmp(cmd, "finish")) {
        debug_step_out();
        gui_debug_printf("Running until return to 0x%08x\n", arm.reg[14]);
        return 1;
    } else if (!strcasecmp(cmd, "d")) {
        char *arg = strtok(NULL, " \n\r");
        if (!arg) {
            gui_debug_printf("Missing address parameter.\n");
        } else {
            uint32_t addr = parse_expr(arg);
            dump(addr);
        }
    } else if (!strcasecmp(cmd, "u")) {
        disasm(disasm_insn);
    } else if (!strcasecmp(cmd, "ua")) {
        disasm(disasm_arm_insn);
    } else if (!strcasecmp(cmd, "ut")) {
        disasm(disasm_thumb_insn);
    } else if (!strcasecmp(cmd, "ln")) {
        char *ln_cmd = strtok(NULL, " \n\r");
        if (!ln_cmd) return 0;
        if (!strcasecmp(ln_cmd, "c")) {
            usblink_connect();
            return 1; // and continue, ARM code needs to be run
        } else if (!strcasecmp(ln_cmd, "s")) {
            char *file = strtok(NULL, "\n");
            if (!file) {
                gui_debug_printf("Missing file parameter.\n");
            } else {
                // remove optional surrounding quotes
                if (*file == '"') file++;
                size_t len = strlen(file);
                if (*(file + len - 1) == '"')
                    *(file + len - 1) = '\0';
                usblink_connect();

                const char *file_name = file;
                for (const char *p = file; *p; p++)
                    if (*p == ':' || *p == '/' || *p == '\\')
                        file_name = p + 1;

                if (ln_target_folder.length() < 1 || *ln_target_folder.rbegin() != '/')
                    ln_target_folder += '/';

                usblink_queue_put_file(std::string(file), ln_target_folder + std::string(file_name), nullptr, nullptr);
            }
        } else if (!strcasecmp(ln_cmd, "st")) {
            char *dir = strtok(NULL, " \n\r");
            if (dir)
                ln_target_folder = dir;
            else
                gui_debug_printf("Missing directory parameter.\n");
        }
    } else if (!strcasecmp(cmd, "taskinfo")) {
        uint32_t task = parse_expr(strtok(NULL, " \n\r"));
        uint8_t *p = (uint8_t*) virt_mem_ptr(task, 52);
        if (p) {
            gui_debug_printf("Previous:	%08x\n", *(uint32_t *)&p[0]);
            gui_debug_printf("Next:		%08x\n", *(uint32_t *)&p[4]);
            gui_debug_printf("ID:		%c%c%c%c\n", p[15], p[14], p[13], p[12]);
            gui_debug_printf("Name:		%.8s\n", &p[16]);
            gui_debug_printf("Status:		%02x\n", p[24]);
            gui_debug_printf("Delayed suspend:%d\n", p[25]);
            gui_debug_printf("Priority:	%02x\n", p[26]);
            gui_debug_printf("Preemption:	%d\n", p[27]);
            gui_debug_printf("Stack start:	%08x\n", *(uint32_t *)&p[36]);
            gui_debug_printf("Stack end:	%08x\n", *(uint32_t *)&p[40]);
            gui_debug_printf("Stack pointer:	%08x\n", *(uint32_t *)&p[44]);
            gui_debug_printf("Stack size:	%08x\n", *(uint32_t *)&p[48]);
            uint32_t sp = *(uint32_t *)&p[44];
            uint32_t *psp = (uint32_t*) virt_mem_ptr(sp, 18 * 4);
            if (psp) {
#ifdef __i386__
                gui_debug_printf("Stack type:	%d (%s)\n", psp[0], psp[0] ? "Interrupt" : "Normal");
                if (psp[0]) {
                    gui_debug_vprintf("cpsr=%08x  r0=%08x r1=%08x r2=%08x r3=%08x  r4=%08x\n"
                                      "  r5=%08x  r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x\n"
                                      " r11=%08x r12=%08x sp=%08x lr=%08x pc=%08x\n",
                                      (va_list)&psp[1]);
                } else {
                    gui_debug_vprintf("cpsr=%08x  r4=%08x  r5=%08x  r6=%08x r7=%08x r8=%08x\n"
                                      "  r9=%08x r10=%08x r11=%08x r12=%08x pc=%08x\n",
                                      (va_list)&psp[1]);
                }
#endif
            }
        }
    } else if (!strcasecmp(cmd, "tasklist")) {
        uint32_t tasklist = parse_expr(strtok(NULL, " \n\r"));
        uint8_t *p = (uint8_t*) virt_mem_ptr(tasklist, 4);
        if (p) {
            uint32_t first = *(uint32_t *)p;
            uint32_t task = first;
            gui_debug_printf("Task      ID   Name     St D Pr P | StkStart StkEnd   StkPtr   StkSize\n");
            do {
                p = (uint8_t*) virt_mem_ptr(task, 52);
                if (!p)
                    return 0;
                gui_debug_printf("%08X: %c%c%c%c %-8.8s %02x %d %02x %d | %08x %08x %08x %08x\n",
                                 task, p[15], p[14], p[13], p[12],
                        &p[16], /* name */
                        p[24],  /* status */
                        p[25],  /* delayed suspend */
                        p[26],  /* priority */
                        p[27],  /* preemption */
                        *(uint32_t *)&p[36], /* stack start */
                        *(uint32_t *)&p[40], /* stack end */
                        *(uint32_t *)&p[44], /* stack pointer */
                        *(uint32_t *)&p[48]  /* stack size */
                        );
                task = *(uint32_t *)&p[4]; /* next */
            } while (task != first);
        }
    } else if (!strcasecmp(cmd, "t+")) {
        do_translate = true;
    } else if (!strcasecmp(cmd, "t-")) {
        flush_translations();
        do_translate = false;
    } else if (!strcasecmp(cmd, "wm") || !strcasecmp(cmd, "wf")) {
        bool frommem = cmd[1] != 'f';
        char *filename = strtok(NULL, " \n\r");
        char *start_str = strtok(NULL, " \n\r");
        char *size_str = strtok(NULL, " \n\r");
        if (!start_str) {
            gui_debug_printf("Parameters are missing.\n");
            return 0;
        }
        uint32_t start = parse_expr(start_str);
        uint32_t size = 0;
        if (size_str)
            size = parse_expr(size_str);
        void *ram = phys_mem_ptr(start, size);
        if (!ram) {
            gui_debug_printf("Address range %08x-%08x is not in RAM.\n", start, start + size - 1);
            return 0;
        }
        FILE *f = fopen_utf8(filename, frommem ? "wb" : "rb");
        if (!f) {
            gui_perror(filename);
            return 0;
        }
        if (!size && !frommem) {
            fseek (f, 0, SEEK_END);
            size = ftell(f);
            rewind(f);
        }
        size_t ret;
        if(frommem)
            ret = fwrite(ram, size, 1, f);
        else
            ret = fread(ram, size, 1, f);
        if (!ret) {
            fclose(f);
            gui_perror(filename);
            return 0;
        }
        fclose(f);
        return 0;
    } else if (!strcasecmp(cmd, "ss")) {
        char *addr_str = strtok(NULL, " \n\r");
        char *len_str = strtok(NULL, " \n\r");
        char *string = strtok(NULL, " \n\r");
        if (!addr_str || !len_str || !string) {
            gui_debug_printf("Missing parameters.\n");
        } else {
            uint32_t addr = parse_expr(addr_str);
            uint32_t len = parse_expr(len_str);
            char *strptr = (char*) phys_mem_ptr(addr, len);
            char *ptr = strptr;
            char *endptr = strptr + len;
            if (ptr) {
                size_t slen = strlen(string);
                while (1) {
                    ptr = (char*) memchr(ptr, *string, endptr - ptr);
                    if (!ptr) {
                        gui_debug_printf("String not found.\n");
                        return 0;
                    }
                    if (!memcmp(ptr, string, slen)) {
                        uint32_t found_addr = ptr - strptr + addr;
                        gui_debug_printf("Found at address %08x.\n", found_addr);
                        return 0;
                    }
                    if (ptr < endptr)
                        ptr++;
                }
            } else {
                gui_debug_printf("Address range %08x-%08x is not in RAM.\n", addr, addr + len - 1);
            }
        }
        return 0;
    } else if (!strcasecmp(cmd, "int")) {
        gui_debug_printf("active		= %08x\n", intr.active);
        gui_debug_printf("status		= %08x\n", intr.status);
        gui_debug_printf("mask		= %08x %08x\n", intr.mask[0], intr.mask[1]);
        gui_debug_printf("priority_limit	= %02x       %02x\n", intr.priority_limit[0], intr.priority_limit[1]);
        gui_debug_printf("noninverted	= %08x\n", intr.noninverted);
        gui_debug_printf("sticky		= %08x\n", intr.sticky);
        gui_debug_printf("priority:\n");
        int i, j;
        for (i = 0; i < 32; i += 16) {
            gui_debug_printf("\t");
            for (j = 0; j < 16; j++)
                gui_debug_printf("%02x ", intr.priority[i+j]);
            gui_debug_printf("\n");
        }
    } else if (!strcasecmp(cmd, "int+")) {
        int_set(atoi(strtok(NULL, " \n\r")), 1);
    } else if (!strcasecmp(cmd, "int-")) {
        int_set(atoi(strtok(NULL, " \n\r")), 0);
    } else if (!strcasecmp(cmd, "pr")) {
        uint32_t addr = parse_expr(strtok(NULL, " \n\r"));
        /*
         * Set up error handler to catch errors from MMIO read.
         * This prevents error() from longjmp'ing out of the debugger.
         */
        if (setjmp(debugger_error_jmp) == 0) {
            debugger_error_handler_active = true;
            uint32_t value = mmio_read_word(addr);
            debugger_error_handler_active = false;
            gui_debug_printf("%08x\n", value);
        } else {
            debugger_error_handler_active = false;
            /* Error was already printed by error() */
        }
    } else if (!strcasecmp(cmd, "pw")) {
        uint32_t addr = parse_expr(strtok(NULL, " \n\r"));
        uint32_t value = parse_expr(strtok(NULL, " \n\r"));
        /*
         * Set up error handler to catch errors from MMIO write.
         * This prevents error() from longjmp'ing out of the debugger.
         */
        if (setjmp(debugger_error_jmp) == 0) {
            debugger_error_handler_active = true;
            mmio_write_word(addr, value);
            debugger_error_handler_active = false;
        } else {
            debugger_error_handler_active = false;
            /* Error was already printed by error() */
        }
    } else if(!strcasecmp(cmd, "stop")) {
	exiting = true;
        return 0;
    } else if(!strcasecmp(cmd, "exec")) {
        char *path = strtok(NULL, " \n\r");
        if(!path)
        {
            gui_debug_printf("You need to supply a path!\n");
            return 0;
        }

        struct armloader_load_params arg_zero, arg_path;
        arg_zero.t = ARMLOADER_PARAM_VAL;
        arg_zero.v = 0;

        arg_path.t = ARMLOADER_PARAM_PTR;
        arg_path.p = {.ptr = path, .size = (uint32_t) strlen(path) + 1};
        struct armloader_load_params params[3] = {arg_path, arg_zero, arg_zero};
        armloader_load_snippet(SNIPPET_ndls_exec, params, 3, NULL);
        return 1;
    } else {
        gui_debug_printf("Unknown command %s\n", cmd);
    }
    return 0;
}

#define MAX_CMD_LEN 300

static void native_debugger(void) {
    uint32_t *cur_insn = (uint32_t*) virt_mem_ptr(arm.reg[15] & ~3, 4);

    // Did we hit the "next" breakpoint?
    if (cur_insn == debug_next) {
        set_debug_next(NULL);
        disasm_insn(arm.reg[15]);
    }

    if (cpu_events & EVENT_DEBUG_STEP) {
        cpu_events &= ~EVENT_DEBUG_STEP;
        disasm_insn(arm.reg[15]);
    }

    throttle_timer_off();
    while (1) {
        debug_input_cur = nullptr;

        std::unique_lock<std::mutex> lk(debug_input_m);

        gui_debugger_request_input(debug_input_callback);

        while(!debug_input_cur)
        {
            debug_input_cv.wait_for(lk, std::chrono::milliseconds(100), []{return debug_input_cur;});
            if(debug_input_cur || exiting)
                break;

            gui_do_stuff(false);
        }

        gui_debugger_request_input(nullptr);

        if(exiting)
            return;

        char *copy = strdup(debug_input_cur);
        if(!copy)
            return;

        int ret = process_debug_cmd(copy);

        free(copy);

        if(ret)
            break;
        else
            continue;
    }
    throttle_timer_on();
}

bool in_debugger = false;

void debugger(enum DBG_REASON reason, uint32_t addr) {
    /* Avoid debugging the debugger. */
    if (in_debugger)
        return;

    gui_debugger_entered_or_left(in_debugger = true);
    if (!gdb_connected && gdbstub_is_listening())
    {
        gui_debug_printf("Waiting for GDB attach...\n");
        gdbstub_set_waiting_for_attach(true);
        while (!gdb_connected && !exiting)
        {
            gdbstub_recv();
            gui_do_stuff(false);
        }
        gdbstub_set_waiting_for_attach(false);
    }

    if (gdb_connected)
        gdbstub_debugger(reason, addr);
    else
        native_debugger();
    gui_debugger_entered_or_left(in_debugger = false);
}
