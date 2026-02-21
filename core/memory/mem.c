#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "emu.h"
#include "os/os.h"
#include "interrupt.h"
#include "misc.h"
#include "keypad.h"
#include "memory/flash.h"
#include "link.h"
#include "sha256.h"
#include "des.h"
#include "lcd.h"
#include "usb/usblink.h"
#include "usb/usb.h"
#include "casplus.h"
#include "memory/mem.h"
#include "debug.h"
#include "cpu/translate.h"
#include "usb/usb_cx2.h"
#include "cx2.h"
#include "cpu/cpu.h"
#include "nspire_log_hook.h"

uint8_t   (*read_byte_map[64])(uint32_t addr);
uint16_t  (*read_half_map[64])(uint32_t addr);
uint32_t  (*read_word_map[64])(uint32_t addr);
void (*write_byte_map[64])(uint32_t addr, uint8_t value);
void (*write_half_map[64])(uint32_t addr, uint16_t value);
void (*write_word_map[64])(uint32_t addr, uint32_t value);

/* For invalid/unknown physical addresses */
uint8_t bad_read_byte(uint32_t addr)               { warn("Bad read_byte: %08x", addr); return 0; }
uint16_t bad_read_half(uint32_t addr)              { warn("Bad read_half: %08x", addr); return 0; }
uint32_t bad_read_word(uint32_t addr)              { warn("Bad read_word: %08x", addr); return 0; }
void bad_write_byte(uint32_t addr, uint8_t value)  { warn("Bad write_byte: %08x %02x", addr, value); }
void bad_write_half(uint32_t addr, uint16_t value) { warn("Bad write_half: %08x %04x", addr, value); }
void bad_write_word(uint32_t addr, uint32_t value) { warn("Bad write_word: %08x %08x", addr, value); }

static bool mmio_trace_checked = false;
static bool mmio_trace_enabled = false;
static unsigned mmio_trace_lines = 0;
static const unsigned mmio_trace_max_lines = 200000;
static bool mmio_trace_pc_checked = false;
static bool mmio_trace_pc_enabled = false;

static bool mmio_trace_in_scope(uint32_t addr)
{
    return (addr >= 0x90020000u && addr < 0x90030000u)
        || (addr >= 0x900B0000u && addr < 0x900B1000u)
        || (addr >= 0x90100000u && addr < 0x90110000u)
        || (addr >= 0x90140000u && addr < 0x90150000u);
}

static bool mmio_trace_on(void)
{
    if (!mmio_trace_checked) {
        const char *env = getenv("FIREBIRD_MMIO_TRACE");
        mmio_trace_enabled = env && *env;
        mmio_trace_checked = true;
    }
    return mmio_trace_enabled;
}

static bool mmio_trace_with_pc(void)
{
    if (!mmio_trace_pc_checked) {
        const char *env = getenv("FIREBIRD_MMIO_TRACE_PC");
        mmio_trace_pc_enabled = env && *env;
        mmio_trace_pc_checked = true;
    }
    return mmio_trace_pc_enabled;
}

static void mmio_trace_read(uint32_t addr, uint32_t value, unsigned size)
{
    if (!mmio_trace_on() || !mmio_trace_in_scope(addr) || mmio_trace_lines >= mmio_trace_max_lines)
        return;
    if (mmio_trace_with_pc())
        fprintf(stderr, "[MMIO R%u] %08x -> %08x @pc=%08x\n", size, addr, value, arm.reg[15]);
    else
        fprintf(stderr, "[MMIO R%u] %08x -> %08x\n", size, addr, value);
    mmio_trace_lines++;
}

static void mmio_trace_write(uint32_t addr, uint32_t value, unsigned size)
{
    if (!mmio_trace_on() || !mmio_trace_in_scope(addr) || mmio_trace_lines >= mmio_trace_max_lines)
        return;
    if (mmio_trace_with_pc())
        fprintf(stderr, "[MMIO W%u] %08x <- %08x @pc=%08x\n", size, addr, value, arm.reg[15]);
    else
        fprintf(stderr, "[MMIO W%u] %08x <- %08x\n", size, addr, value);
    mmio_trace_lines++;
}

uint8_t *mem_and_flags = NULL;
struct mem_area_desc mem_areas[5];

void *phys_mem_ptr(uint32_t addr, uint32_t size) {
    unsigned int i;
    for (i = 0; i < sizeof(mem_areas)/sizeof(*mem_areas); i++) {
        uint32_t offset = addr - mem_areas[i].base;
        if (offset < mem_areas[i].size && size <= mem_areas[i].size - offset)
            return mem_areas[i].ptr + offset;
    }
    return NULL;
}

uint32_t phys_mem_addr(void *ptr) {
    int i;
    for (i = 0; i < 3; i++) {
        uint32_t offset = (uint8_t *)ptr - mem_areas[i].ptr;
        if (offset < mem_areas[i].size)
            return mem_areas[i].base + offset;
    }
    return -1; // should never happen
}

SYSVABI void read_action(void *ptr) {
    if (gdb_connected) {
        uint32_t addr = phys_mem_addr(ptr);
        debugger(DBG_READ_BREAKPOINT, addr);
    }
}

SYSVABI void write_action(void *ptr) {
    uint32_t *flags = &RAM_FLAGS((size_t)ptr & ~3);
    if (*flags & RF_WRITE_BREAKPOINT) {
        if (gdb_connected)
            debugger(DBG_WRITE_BREAKPOINT, phys_mem_addr(ptr));
    }
#ifndef NO_TRANSLATION
    if (*flags & RF_CODE_TRANSLATED) {
        logprintf(LOG_CPU, "Wrote to translated code at %08x. Deleting translations.\n", phys_mem_addr(ptr));
        invalidate_translation(*flags >> RFS_TRANSLATION_INDEX);
    } else {
        *flags &= ~RF_CODE_NO_TRANSLATE;
    }
    *flags &= ~RF_CODE_EXECUTED;
#endif
}

/* 00000000, 10000000, A4000000: ROM and RAM */
uint8_t memory_read_byte(uint32_t addr) {
    uint8_t *ptr = phys_mem_ptr(addr, 1);
    if (!ptr) return bad_read_byte(addr);
    if (RAM_FLAGS((size_t)ptr & ~3) & DO_READ_ACTION) read_action(ptr);
    return *ptr;
}
uint16_t memory_read_half(uint32_t addr) {
    uint16_t *ptr = phys_mem_ptr(addr, 2);
    if (!ptr) return bad_read_half(addr);
    if (RAM_FLAGS((size_t)ptr & ~3) & DO_READ_ACTION) read_action(ptr);
    return *ptr;
}
uint32_t memory_read_word(uint32_t addr) {
    uint32_t *ptr = phys_mem_ptr(addr, 4);
    if (!ptr) return bad_read_word(addr);
    if (RAM_FLAGS(ptr) & DO_READ_ACTION) read_action(ptr);
    return *ptr;
}
void memory_write_byte(uint32_t addr, uint8_t value) {
    uint8_t *ptr = phys_mem_ptr(addr, 1);
    if (!ptr) { bad_write_byte(addr, value); return; }
    uint32_t flags = RAM_FLAGS((size_t)ptr & ~3);
    if (flags & RF_READ_ONLY) { bad_write_byte(addr, value); return; }
    if (flags & DO_WRITE_ACTION) write_action(ptr);
    *ptr = value;
    nspire_log_hook_on_memory_write(addr, 1);
}
void memory_write_half(uint32_t addr, uint16_t value) {
    uint16_t *ptr = phys_mem_ptr(addr, 2);
    if (!ptr) { bad_write_half(addr, value); return; }
    uint32_t flags = RAM_FLAGS((size_t)ptr & ~3);
    if (flags & RF_READ_ONLY) { bad_write_half(addr, value); return; }
    if (flags & DO_WRITE_ACTION) write_action(ptr);
    *ptr = value;
    nspire_log_hook_on_memory_write(addr, 2);
}
void memory_write_word(uint32_t addr, uint32_t value) {
    uint32_t *ptr = phys_mem_ptr(addr, 4);
    if (!ptr) { bad_write_word(addr, value); return; }
    uint32_t flags = RAM_FLAGS(ptr);
    if (flags & RF_READ_ONLY) { bad_write_word(addr, value); return; }
    if (flags & DO_WRITE_ACTION) write_action(ptr);
    *ptr = value;
    nspire_log_hook_on_memory_write(addr, 4);
}

/* The APB (Advanced Peripheral Bus) hosts peripherals that do not require
 * high bandwidth. The bridge to the APB is accessed via addresses 90xxxxxx. */
/* The AMBA specification does not mention anything about transfer sizes in APB,
 * so probably all reads/writes are effectively 32 bit. */
struct apb_map_entry {
    uint32_t (*read)(uint32_t addr);
    void (*write)(uint32_t addr, uint32_t value);
} apb_map[0x16];
void apb_set_map(int entry, uint32_t (*read)(uint32_t addr), void (*write)(uint32_t addr, uint32_t value)) {
    apb_map[entry].read = read;
    apb_map[entry].write = write;
}
uint8_t apb_read_byte(uint32_t addr) {
    if (addr >= 0x90150000) return bad_read_byte(addr);
    uint8_t ret = apb_map[addr >> 16 & 31].read(addr & ~3) >> ((addr & 3) << 3);
    mmio_trace_read(addr, ret, 8);
    return ret;
}
uint16_t apb_read_half(uint32_t addr) {
    if (addr >= 0x90150000) return bad_read_half(addr);
    uint16_t ret = apb_map[addr >> 16 & 31].read(addr & ~2) >> ((addr & 2) << 3);
    mmio_trace_read(addr, ret, 16);
    return ret;
}
uint32_t apb_read_word(uint32_t addr) {
    if (addr >= 0x90150000) return bad_read_word(addr);
    uint32_t ret = apb_map[addr >> 16 & 31].read(addr);
    mmio_trace_read(addr, ret, 32);
    return ret;
}
void apb_write_byte(uint32_t addr, uint8_t value) {
    if (addr >= 0x90150000) { bad_write_byte(addr, value); return; }
    mmio_trace_write(addr, value, 8);
    apb_map[addr >> 16 & 31].write(addr & ~3, value * 0x01010101u);
}
void apb_write_half(uint32_t addr, uint16_t value) {
    if (addr >= 0x90150000) { bad_write_half(addr, value); return; }
    mmio_trace_write(addr, value, 16);
    apb_map[addr >> 16 & 31].write(addr & ~2, value * 0x00010001u);
}
void apb_write_word(uint32_t addr, uint32_t value) {
    if (addr >= 0x90150000) { bad_write_word(addr, value); return; }
    mmio_trace_write(addr, value, 32);
    apb_map[addr >> 16 & 31].write(addr, value);
}

uint32_t FASTCALL mmio_read_byte(uint32_t addr) {
    return read_byte_map[addr >> 26](addr);
}
uint32_t FASTCALL mmio_read_half(uint32_t addr) {
    return read_half_map[addr >> 26](addr);
}
uint32_t FASTCALL mmio_read_word(uint32_t addr) {
    return read_word_map[addr >> 26](addr);
}
void FASTCALL mmio_write_byte(uint32_t addr, uint32_t value) {
    write_byte_map[addr >> 26](addr, value);
}
void FASTCALL mmio_write_half(uint32_t addr, uint32_t value) {
    write_half_map[addr >> 26](addr, value);
}
void FASTCALL mmio_write_word(uint32_t addr, uint32_t value) {
    write_word_map[addr >> 26](addr, value);
}

uint8_t null_read_byte(uint32_t addr) {
    (void) addr;
    return 0;
}
uint16_t null_read_half(uint32_t addr) {
    (void) addr;
    return 0;
}
uint32_t null_read_word(uint32_t addr) {
    (void) addr;
    return 0;
}
void null_write_byte(uint32_t addr, uint8_t value) {
    (void) addr;
    (void) value;
}
void null_write_half(uint32_t addr, uint16_t value) {
    (void) addr;
    (void) value;
}
void null_write_word(uint32_t addr, uint32_t value) {
    (void) addr;
    (void) value;
}

void (*reset_procs[32])(void);
unsigned int reset_proc_count;

void add_reset_proc(void (*proc)(void))
{
    if (reset_proc_count == sizeof(reset_procs)/sizeof(*reset_procs))
        abort();
    reset_procs[reset_proc_count++] = proc;
}

static uint32_t current_product = 0;

bool memory_initialize(uint32_t sdram_size)
{
    // If the memory size or product differ, reinitialize
    if(mem_and_flags && (sdram_size != mem_areas[1].size || product != current_product))
        memory_deinitialize();

    if(mem_and_flags)
        return true;

    uint32_t total_mem = 0;
    int i;

    mem_and_flags = os_reserve(MEM_MAXSIZE * 2);
    if(!mem_and_flags)
    {
        emuprintf("os_reserve failed!\n");
        return false;
    }

    // Boot ROM
    mem_areas[0].base = 0x0;
    mem_areas[0].size = 0x80000;

    // SDRAM
    mem_areas[1].base = 0x10000000;
    mem_areas[1].size = sdram_size;

    if (emulate_casplus)
    {
        mem_areas[2].base = 0x20000000;
        mem_areas[2].size = 0x40000;
    }
    else if (emulate_cx2)
    {
        mem_areas[2].base = 0xA4000000;
        mem_areas[2].size = 0x40000; // Double of CX

        mem_areas[3].base = 0xA8000000;
        mem_areas[3].size = 320 * 240 * 2; // One RGB565 frame
    }
    else
    {
        // Classic and CX
        mem_areas[2].base = 0xA4000000;
        mem_areas[2].size = 0x20000;
    }

    for (i = 0; i != sizeof(mem_areas)/sizeof(*mem_areas); i++) {
        if (mem_areas[i].size) {
            mem_areas[i].ptr = mem_and_flags + total_mem;
            total_mem += mem_areas[i].size;
        }
    }

    assert (total_mem <= MEM_MAXSIZE);

    current_product = product;

    if (product == 0x0D0) {
        // Lab cradle OS reads calibration data from F007xxxx,
        // probably a mirror of ROM at 0007xxxx
        mem_areas[3].base = 0xF0000000;
        mem_areas[3].size = mem_areas[0].size;
        mem_areas[3].ptr = mem_areas[0].ptr;
    }

    if (emulate_cx2)
    {
        mem_areas[4].base = 0xA0000000;
        mem_areas[4].size = mem_areas[0].size;
        mem_areas[4].ptr = mem_areas[0].ptr;
    }

    for (int i = 0; i < 64; i++) {
        // will fallback to bad_* on non-memory addresses
        read_byte_map[i] = memory_read_byte;
        read_half_map[i] = memory_read_half;
        read_word_map[i] = memory_read_word;
        write_byte_map[i] = memory_write_byte;
        write_half_map[i] = memory_write_half;
        write_word_map[i] = memory_write_word;
    }

    if (emulate_casplus) {
        read_byte_map[0x08 >> 2] = casplus_nand_read_byte;
        read_half_map[0x08 >> 2] = casplus_nand_read_half;
        write_byte_map[0x08 >> 2] = casplus_nand_write_byte;
        write_half_map[0x08 >> 2] = casplus_nand_write_half;

        read_byte_map[0xFF >> 2] = omap_read_byte;
        read_half_map[0xFF >> 2] = omap_read_half;
        read_word_map[0xFF >> 2] = omap_read_word;
        write_byte_map[0xFF >> 2] = omap_write_byte;
        write_half_map[0xFF >> 2] = omap_write_half;
        write_word_map[0xFF >> 2] = omap_write_word;

        add_reset_proc(casplus_reset);
        return true;
    }

    read_byte_map[0x90 >> 2] = apb_read_byte;
    read_half_map[0x90 >> 2] = apb_read_half;
    read_word_map[0x90 >> 2] = apb_read_word;
    write_byte_map[0x90 >> 2] = apb_write_byte;
    write_half_map[0x90 >> 2] = apb_write_half;
    write_word_map[0x90 >> 2] = apb_write_word;
    for (size_t i = 0; i < sizeof(apb_map)/sizeof(*apb_map); i++) {
        apb_map[i].read = bad_read_word;
        apb_map[i].write = bad_write_word;
    }

    apb_set_map(0x00, gpio_read, gpio_write);
    add_reset_proc(gpio_reset);
    apb_set_map(0x06, watchdog_read, watchdog_write);
    add_reset_proc(watchdog_reset);
    apb_set_map(0x09, rtc_read, rtc_write);
    add_reset_proc(rtc_reset);
    apb_set_map(0x0A, misc_read, misc_write);
    apb_set_map(0x0E, keypad_read, keypad_write);
    add_reset_proc(keypad_reset);
    apb_set_map(0x0F, hdq1w_read, hdq1w_write);
    add_reset_proc(hdq1w_reset);

    apb_set_map(0x11, led_read_word, led_write_word);
    add_reset_proc(led_reset);

    read_byte_map[0xAC >> 2] = sdio_read_byte;
    read_half_map[0xAC >> 2] = sdio_read_half;
    read_word_map[0xAC >> 2] = sdio_read_word;
    write_byte_map[0xAC >> 2] = sdio_write_byte;
    write_half_map[0xAC >> 2] = sdio_write_half;
    write_word_map[0xAC >> 2] = sdio_write_word;
    add_reset_proc(sdio_reset);

    if(!emulate_cx2)
    {
        read_byte_map[0xB0 >> 2] = usb_read_byte;
        read_half_map[0xB0 >> 2] = usb_read_half;
        read_word_map[0xB0 >> 2] = usb_read_word;
        write_word_map[0xB0 >> 2] = usb_write_word;

        //TODO: It's a different controller, but for now we use the same state
        read_byte_map[0xB4 >> 2] = usb_read_byte;
        read_half_map[0xB4 >> 2] = usb_read_half;
        read_word_map[0xB4 >> 2] = usb_read_word;
        write_word_map[0xB4 >> 2] = usb_write_word;
    }
    else
    {
        read_byte_map[0xB0 >> 2] = usb_cx2_read_byte;
        read_half_map[0xB0 >> 2] = usb_cx2_read_half;
        read_word_map[0xB0 >> 2] = usb_cx2_read_word;
        write_word_map[0xB0 >> 2] = usb_cx2_write_word;

        read_byte_map[0xB4 >> 2] = null_read_byte;
        read_half_map[0xB4 >> 2] = null_read_half;
        read_word_map[0xB4 >> 2] = null_read_word;
        write_word_map[0xB4 >> 2] = null_write_word;
    }
    add_reset_proc(usb_reset);
    add_reset_proc(usb_cx2_reset);
    add_reset_proc(usblink_reset);

    read_word_map[0xC0 >> 2] = lcd_read_word;
    write_word_map[0xC0 >> 2] = lcd_write_word;
    add_reset_proc(lcd_reset);

    if (!emulate_cx2) {
        read_word_map[0xC4 >> 2] = adc_read_word;
        write_word_map[0xC4 >> 2] = adc_write_word;
    } else {
        read_word_map[0xC4 >> 2] = adc_cx2_read_word;
        write_word_map[0xC4 >> 2] = adc_cx2_write_word;
    }
    add_reset_proc(adc_reset);

    des_initialize();
    read_word_map[0xC8 >> 2] = des_read_word;
    write_word_map[0xC8 >> 2] = des_write_word;
    add_reset_proc(des_reset);

    read_word_map[0xCC >> 2] = sha256_read_word;
    write_word_map[0xCC >> 2] = sha256_write_word;
    add_reset_proc(sha256_reset);

    if (!emulate_cx) {
        read_byte_map[0x08 >> 2] = nand_phx_raw_read_byte;
        write_byte_map[0x08 >> 2] = nand_phx_raw_write_byte;

        write_word_map[0x8F >> 2] = sdramctl_write_word;

        apb_set_map(0x01, timer_read, timer_write);
        apb_set_map(0x0B, pmu_read, pmu_write);
        add_reset_proc(pmu_reset);
        apb_set_map(0x0C, timer_read, timer_write);
        apb_set_map(0x0D, timer_read, timer_write);
        add_reset_proc(timer_reset);
        apb_set_map(0x02, serial_read, serial_write);
        add_reset_proc(serial_reset);
        apb_set_map(0x08, bad_read_word, unknown_9008_write);
        apb_set_map(0x10, ti84_io_link_read, ti84_io_link_write);
        add_reset_proc(ti84_io_link_reset);

        read_word_map[0xA9 >> 2] = spi_read_word;
        write_word_map[0xA9 >> 2] = spi_write_word;

        read_word_map[0xB8 >> 2] = nand_phx_read_word;
        write_word_map[0xB8 >> 2] = nand_phx_write_word;
        add_reset_proc(nand_phx_reset);

        read_word_map[0xDC >> 2] = int_read_word;
        write_word_map[0xDC >> 2] = int_write_word;
        add_reset_proc(int_reset);
    } else {
        apb_set_map(0x01, timer_cx_read, timer_cx_write);
        apb_set_map(0x0C, timer_cx_read, timer_cx_write);
        apb_set_map(0x0D, timer_cx_read, timer_cx_write);
        add_reset_proc(timer_cx_reset);
        apb_set_map(0x02, serial_cx_read, serial_cx_write);
        add_reset_proc(serial_cx_reset);
        apb_set_map(0x03, fastboot_cx_read, fastboot_cx_write);
        /*
         * Clear fastboot RAM on cold boot only (not soft reset).
         * This RAM persists across soft resets to pass boot parameters,
         * but should start clean on a fresh emulator start.
         */
        fastboot_cx_reset();
        apb_set_map(0x05, touchpad_cx_read, touchpad_cx_write);
        add_reset_proc(touchpad_cx_reset);

        if(emulate_cx2)
        {
            apb_set_map(0x04, cx2_lcd_spi_read, cx2_lcd_spi_write);
            apb_set_map(0x07, serial_cx2_read, serial_cx2_write);
            add_reset_proc(serial_cx2_reset);
            apb_set_map(0x08, unknown_9008_read, unknown_9008_write);
            apb_set_map(0x0B, adc_cx2_read_word, adc_cx2_write_word);
            /* CX II firmware primarily uses 0x900B ADC, but mirror C400 as well
             * so either MMIO window sees the same controller state. */
            read_word_map[0xC4 >> 2] = adc_cx2_read_word;
            write_word_map[0xC4 >> 2] = adc_cx2_write_word;
            apb_set_map(0x10, tg2989_pmic_read, tg2989_pmic_write);
            add_reset_proc(tg2989_pmic_reset);
            apb_set_map(0x12, memc_ddr_read, memc_ddr_write);
            add_reset_proc(memc_ddr_reset);
            apb_set_map(0x13, cx2_backlight_read, cx2_backlight_write);
            add_reset_proc(cx2_backlight_reset);
            apb_set_map(0x14, aladdin_pmu_read, aladdin_pmu_write);
            add_reset_proc(aladdin_pmu_reset);

            read_word_map[0xB8 >> 2] = spinand_cx2_read_word;
            read_byte_map[0xB8 >> 2] = spinand_cx2_read_byte;
            write_word_map[0xB8 >> 2] = spinand_cx2_write_word;
            write_byte_map[0xB8 >> 2] = spinand_cx2_write_byte;
            add_reset_proc(flash_spi_reset);

            read_word_map[0xBC >> 2] = dma_cx2_read_word;
            write_word_map[0xBC >> 2] = dma_cx2_write_word;
            add_reset_proc(dma_cx2_reset);
        }
        else
        {
            read_word_map[0x8F >> 2] = memctl_cx_read_word;
            write_word_map[0x8F >> 2] = memctl_cx_write_word;
            add_reset_proc(memctl_cx_reset);

            apb_set_map(0x04, spi_cx_read, spi_cx_write);
            apb_set_map(0x0B, pmu_read, pmu_write);
            add_reset_proc(pmu_reset);

            read_byte_map[0x80 >> 2] = nand_cx_read_byte;
            read_word_map[0x80 >> 2] = nand_cx_read_word;
            write_byte_map[0x80 >> 2] = nand_cx_write_byte;
            write_word_map[0x80 >> 2] = nand_cx_write_word;

            read_word_map[0xB8 >> 2] = sramctl_read_word;
            write_word_map[0xB8 >> 2] = sramctl_write_word;
        }

        read_word_map[0xDC >> 2] = int_cx_read_word;
        write_word_map[0xDC >> 2] = int_cx_write_word;
        add_reset_proc(int_reset);
    }

    return true;
}

void memory_reset()
{
    for(unsigned int i = 0; i < reset_proc_count; i++)
        reset_procs[i]();
}

void memory_deinitialize()
{
    if(mem_and_flags)
    {
        // translation_table uses absolute addresses
        flush_translations();
        memset(mem_areas, 0, sizeof(mem_areas));
        os_free(mem_and_flags, MEM_MAXSIZE * 2);
        mem_and_flags = NULL;
    }

    reset_proc_count = 0;
}

static size_t map_append(char *out, size_t out_size, size_t pos, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t avail = (pos < out_size) ? (out_size - pos) : 0;
    int written = vsnprintf(out ? out + pos : NULL, avail, fmt, ap);
    va_end(ap);
    if (written < 0)
        return pos;
    if (out && avail > 0 && (size_t)written >= avail)
        return out_size - 1;
    return pos + (size_t)written;
}

static bool mem_area_is_read_only(uint8_t *ptr, uint32_t size)
{
    if (!ptr || size == 0)
        return false;
    uint32_t aligned = size & ~3u;
    uint8_t *end = ptr + aligned;
    for (uint32_t *p = (uint32_t *)ptr; (uint8_t *)p < end; ++p)
    {
        if (!(RAM_FLAGS(p) & RF_READ_ONLY))
            return false;
    }
    return true;
}

size_t memory_build_gdb_map(char *out, size_t out_size)
{
    size_t pos = 0;
    if (!out || !out_size) {
        out = NULL;
        out_size = 0;
    }

    pos = map_append(out, out_size, pos, "<?xml version=\"1.0\"?><memory-map>");

    // RAM/ROM areas
    for (size_t i = 0; i < sizeof(mem_areas) / sizeof(mem_areas[0]); ++i)
    {
        if (!mem_areas[i].size)
            continue;
        const bool is_rom = (mem_areas[0].ptr && mem_areas[i].ptr == mem_areas[0].ptr);
        const char *type = is_rom ? "rom" : "ram";
        const char *name = NULL;
        switch (mem_areas[i].base)
        {
            case 0x00000000: name = "boot_rom"; break;
            case 0x10000000: name = "sdram"; break;
            case 0x20000000: name = "internal_sram"; break;
            case 0xA4000000: name = "internal_sram"; break;
            case 0xA8000000: name = "vram"; break;
            case 0xF0000000: name = "boot_rom_mirror"; break;
            case 0xA0000000: name = "boot_rom_mirror"; break;
        }
        char name_buf[32];
        if (!name) {
            if (is_rom) {
                name = "boot_rom_mirror";
            } else {
                snprintf(name_buf, sizeof(name_buf), "mem_area_%zu", i);
                name = name_buf;
            }
        }
        pos = map_append(out, out_size, pos,
                         "<memory type=\"%s\" start=\"0x%08x\" length=\"0x%08x\" name=\"%s\"/>",
                         type, mem_areas[i].base, mem_areas[i].size, name);
    }

    // APB submap (0x90000000)
    if (read_word_map[0x90 >> 2] == apb_read_word)
    {
        for (size_t i = 0; i < sizeof(apb_map) / sizeof(apb_map[0]); ++i)
        {
            if (apb_map[i].read == bad_read_word && apb_map[i].write == bad_write_word)
                continue;
            uint32_t base = 0x90000000u + (uint32_t)(i << 16);
            const char *name = NULL;
            switch (i)
            {
                case 0x00: name = "gpio"; break;
                case 0x06: name = "watchdog"; break;
                case 0x09: name = "rtc"; break;
                case 0x0A: name = "misc"; break;
                case 0x0E: name = "keypad"; break;
                case 0x0F: name = "hdq1w"; break;
                case 0x11: name = "led"; break;
                default: break;
            }
            if (emulate_cx2)
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x03: name = "fastboot_ram"; break;
                    case 0x04: name = "lcd_spi"; break;
                    case 0x05: name = "i2c_touchpad"; break;
                    case 0x07: name = "uart1"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "adc"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    case 0x12: name = "sdram_ctrl"; break;
                    case 0x13: name = "backlight"; break;
                    case 0x14: name = "pmu_aladdin"; break;
                    default: break;
                }
            }
            else if (emulate_cx)
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x03: name = "fastboot_ram"; break;
                    case 0x04: name = "spi"; break;
                    case 0x05: name = "i2c_touchpad"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "pmu"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    default: break;
                }
            }
            else
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "pmu"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    case 0x10: name = "ti84_link"; break;
                    default: break;
                }
            }
            char name_buf[32];
            if (!name) {
                snprintf(name_buf, sizeof(name_buf), "apb_0x%02zx", i);
                name = name_buf;
            }
            pos = map_append(out, out_size, pos,
                             "<memory type=\"ram\" start=\"0x%08x\" length=\"0x00010000\" name=\"%s\"/>",
                             base, name);
        }
    }

    if (emulate_cx2)
    {
        struct mmio_region {
            uint32_t base;
            uint32_t size;
            const char *name;
        };
        static const struct mmio_region cx2_mmio_regions[] = {
            { 0xAC000000, 0x00001000, "sdio" },
            { 0xB0000000, 0x00001000, "usb_otg_top" },
            { 0xB4000000, 0x00001000, "usb_otg_bottom" },
            { 0xB8000000, 0x00010000, "spi_nand" },
            { 0xBC000000, 0x00001000, "dma" },
            { 0xC0000000, 0x00001000, "lcd" },
            { 0xC4000000, 0x00001000, "adc" },
            { 0xC8010000, 0x00001000, "des" },
            { 0xCC000000, 0x00001000, "sha256" },
            { 0xDC000000, 0x00001000, "interrupt_controller" },
        };
        for (size_t i = 0; i < sizeof(cx2_mmio_regions) / sizeof(cx2_mmio_regions[0]); ++i)
        {
            uint32_t window = cx2_mmio_regions[i].base & 0xFC000000u;
            if (read_word_map[window >> 26] == memory_read_word)
                continue;
            if (read_word_map[window >> 26] == apb_read_word)
                continue;
            pos = map_append(out, out_size, pos,
                             "<memory type=\"ram\" start=\"0x%08x\" length=\"0x%08x\" name=\"%s\"/>",
                             cx2_mmio_regions[i].base, cx2_mmio_regions[i].size, cx2_mmio_regions[i].name);
        }
    }
    else
    {
        // Other MMIO segments (64MB windows)
        for (uint32_t i = 0; i < 64; ++i)
        {
            if (read_word_map[i] == memory_read_word)
                continue;
            if (read_word_map[i] == apb_read_word)
                continue;
            uint32_t base = i << 26;
            pos = map_append(out, out_size, pos,
                             "<memory type=\"ram\" start=\"0x%08x\" length=\"0x04000000\" name=\"mmio_%02x\"/>",
                             base, i);
        }
    }

    pos = map_append(out, out_size, pos, "</memory-map>");
    if (out && out_size > 0) {
        if (pos >= out_size)
            pos = out_size - 1;
        out[pos] = 0;
    }
    return pos;
}

size_t memory_build_fb_map(char *out, size_t out_size)
{
    size_t pos = 0;
    if (!out || !out_size) {
        out = NULL;
        out_size = 0;
    }

    pos = map_append(out, out_size, pos, "FBMAP v1\n");

    // RAM/ROM areas
    for (size_t i = 0; i < sizeof(mem_areas) / sizeof(mem_areas[0]); ++i)
    {
        if (!mem_areas[i].size)
            continue;
        const bool is_rom = (mem_areas[0].ptr && mem_areas[i].ptr == mem_areas[0].ptr);
        const char *type = is_rom ? "rom" : "ram";
        const char *perm = mem_area_is_read_only(mem_areas[i].ptr, mem_areas[i].size) ? "r-x" : "rwx";
        const char *name = NULL;
        switch (mem_areas[i].base)
        {
            case 0x00000000: name = "boot_rom"; break;
            case 0x10000000: name = "sdram"; break;
            case 0x20000000: name = "internal_sram"; break;
            case 0xA4000000: name = "internal_sram"; break;
            case 0xA8000000: name = "vram"; break;
            case 0xF0000000: name = "boot_rom_mirror"; break;
            case 0xA0000000: name = "boot_rom_mirror"; break;
        }
        char name_buf[32];
        if (!name) {
            if (is_rom) {
                name = "boot_rom_mirror";
            } else {
                snprintf(name_buf, sizeof(name_buf), "mem_area_%zu", i);
                name = name_buf;
            }
        }
        pos = map_append(out, out_size, pos,
                         "%08x %08x %s %s %s\n",
                         mem_areas[i].base, mem_areas[i].size, type, perm, name);
    }

    // APB submap (0x90000000)
    if (read_word_map[0x90 >> 2] == apb_read_word)
    {
        for (size_t i = 0; i < sizeof(apb_map) / sizeof(apb_map[0]); ++i)
        {
            if (apb_map[i].read == bad_read_word && apb_map[i].write == bad_write_word)
                continue;
            uint32_t base = 0x90000000u + (uint32_t)(i << 16);
            const char *name = NULL;
            switch (i)
            {
                case 0x00: name = "gpio"; break;
                case 0x06: name = "watchdog"; break;
                case 0x09: name = "rtc"; break;
                case 0x0A: name = "misc"; break;
                case 0x0E: name = "keypad"; break;
                case 0x0F: name = "hdq1w"; break;
                case 0x11: name = "led"; break;
                default: break;
            }
            if (emulate_cx2)
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x03: name = "fastboot_ram"; break;
                    case 0x04: name = "lcd_spi"; break;
                    case 0x05: name = "i2c_touchpad"; break;
                    case 0x07: name = "uart1"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "adc"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    case 0x12: name = "sdram_ctrl"; break;
                    case 0x13: name = "backlight"; break;
                    case 0x14: name = "pmu_aladdin"; break;
                    default: break;
                }
            }
            else if (emulate_cx)
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x03: name = "fastboot_ram"; break;
                    case 0x04: name = "spi"; break;
                    case 0x05: name = "i2c_touchpad"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "pmu"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    default: break;
                }
            }
            else
            {
                switch (i)
                {
                    case 0x01: name = "fast_timer"; break;
                    case 0x02: name = "uart0"; break;
                    case 0x08: name = "cradle_spi"; break;
                    case 0x0B: name = "pmu"; break;
                    case 0x0C: name = "timer_first"; break;
                    case 0x0D: name = "timer_second"; break;
                    case 0x10: name = "ti84_link"; break;
                    default: break;
                }
            }
            char name_buf[32];
            if (!name) {
                snprintf(name_buf, sizeof(name_buf), "apb_0x%02zx", i);
                name = name_buf;
            }
            pos = map_append(out, out_size, pos,
                             "%08x %08x io rw- %s\n",
                             base, 0x00010000u, name);
        }
    }

    if (emulate_cx2)
    {
        struct mmio_region {
            uint32_t base;
            uint32_t size;
            const char *name;
        };
        static const struct mmio_region cx2_mmio_regions[] = {
            { 0xAC000000, 0x00001000, "sdio" },
            { 0xB0000000, 0x00001000, "usb_otg_top" },
            { 0xB4000000, 0x00001000, "usb_otg_bottom" },
            { 0xB8000000, 0x00010000, "spi_nand" },
            { 0xBC000000, 0x00001000, "dma" },
            { 0xC0000000, 0x00001000, "lcd" },
            { 0xC4000000, 0x00001000, "adc" },
            { 0xC8010000, 0x00001000, "des" },
            { 0xCC000000, 0x00001000, "sha256" },
            { 0xDC000000, 0x00001000, "interrupt_controller" },
        };
        for (size_t i = 0; i < sizeof(cx2_mmio_regions) / sizeof(cx2_mmio_regions[0]); ++i)
        {
            uint32_t window = cx2_mmio_regions[i].base & 0xFC000000u;
            if (read_word_map[window >> 26] == memory_read_word)
                continue;
            if (read_word_map[window >> 26] == apb_read_word)
                continue;
            pos = map_append(out, out_size, pos,
                             "%08x %08x io rw- %s\n",
                             cx2_mmio_regions[i].base, cx2_mmio_regions[i].size, cx2_mmio_regions[i].name);
        }
    }
    else
    {
        // Other MMIO segments (64MB windows)
        for (uint32_t i = 0; i < 64; ++i)
        {
            if (read_word_map[i] == memory_read_word)
                continue;
            if (read_word_map[i] == apb_read_word)
                continue;
            uint32_t base = i << 26;
            pos = map_append(out, out_size, pos,
                             "%08x %08x io rw- mmio_%02x\n",
                             base, 0x04000000u, i);
        }
    }

    if (out && out_size > 0) {
        if (pos >= out_size)
            pos = out_size - 1;
        out[pos] = 0;
    }
    return pos;
}

bool memory_query_region(uint32_t addr, struct memory_region_info *info)
{
    if (!info)
        return false;

    for (size_t i = 0; i < sizeof(mem_areas) / sizeof(mem_areas[0]); ++i)
    {
        if (!mem_areas[i].size)
            continue;
        uint32_t start = mem_areas[i].base;
        uint32_t end = start + mem_areas[i].size;
        if (addr >= start && addr < end)
        {
            bool ro = mem_area_is_read_only(mem_areas[i].ptr, mem_areas[i].size);
            info->start = start;
            info->size = mem_areas[i].size;
            strcpy(info->perm, ro ? "r-x" : "rwx");
            return true;
        }
    }

    if (read_word_map[0x90 >> 2] == apb_read_word)
    {
        if (addr >= 0x90000000u && addr < 0x90150000u)
        {
            uint32_t idx = (addr - 0x90000000u) >> 16;
            if (idx < (uint32_t)(sizeof(apb_map) / sizeof(apb_map[0])))
            {
                if (apb_map[idx].read != bad_read_word || apb_map[idx].write != bad_write_word)
                {
                    info->start = 0x90000000u + (idx << 16);
                    info->size = 0x00010000u;
                    strcpy(info->perm, "rw-");
                    return true;
                }
            }
        }
    }

    if (emulate_cx2)
    {
        struct mmio_region {
            uint32_t base;
            uint32_t size;
        };
        static const struct mmio_region cx2_mmio_regions[] = {
            { 0xAC000000, 0x00001000 },
            { 0xB0000000, 0x00001000 },
            { 0xB4000000, 0x00001000 },
            { 0xB8000000, 0x00010000 },
            { 0xBC000000, 0x00001000 },
            { 0xC0000000, 0x00001000 },
            { 0xC4000000, 0x00001000 },
            { 0xC8010000, 0x00001000 },
            { 0xCC000000, 0x00001000 },
            { 0xDC000000, 0x00001000 },
        };
        for (size_t i = 0; i < sizeof(cx2_mmio_regions) / sizeof(cx2_mmio_regions[0]); ++i)
        {
            uint32_t start = cx2_mmio_regions[i].base;
            uint32_t end = start + cx2_mmio_regions[i].size;
            if (addr >= start && addr < end)
            {
                info->start = start;
                info->size = cx2_mmio_regions[i].size;
                strcpy(info->perm, "rw-");
                return true;
            }
        }
    }

    uint32_t window = addr & 0xFC000000u;
    uint32_t index = window >> 26;
    if (read_word_map[index] != memory_read_word && read_word_map[index] != apb_read_word)
    {
        info->start = window;
        info->size = 0x04000000u;
        strcpy(info->perm, "rw-");
        return true;
    }

    return false;
}

bool memory_suspend(emu_snapshot *snapshot)
{
    assert(mem_and_flags);

    uint32_t sdram_size = mem_areas[1].size;

    // TODO: CAS+ and ti84_io?
    return snapshot_write(snapshot, &sdram_size, sizeof(sdram_size))
            // TODO: No flags saved. Only RF_EXEC_BREAKPOINT and maybe RF_READ_ONLY are interesting.
            && snapshot_write(snapshot, mem_and_flags, MEM_MAXSIZE)
            && misc_suspend(snapshot)
            && keypad_suspend(snapshot)
            && usb_suspend(snapshot)
            && lcd_suspend(snapshot)
            && des_suspend(snapshot)
            && sha256_suspend(snapshot)
            && serial_suspend(snapshot)
            && interrupt_suspend(snapshot)
            && serial_cx_suspend(snapshot)
            && serial_cx2_suspend(snapshot)
            && cx2_suspend(snapshot)
            && usb_cx2_suspend(snapshot);
}

bool memory_resume(const emu_snapshot *snapshot)
{
    uint32_t sdram_size = mem_areas[1].size;

    return snapshot_read(snapshot, &sdram_size, sizeof(sdram_size))
            && memory_initialize(sdram_size)
            && (memory_reset(), true) // To have peripherals register with sched
            && snapshot_read(snapshot, mem_and_flags, MEM_MAXSIZE)
            && memset(mem_and_flags + MEM_MAXSIZE, 0, MEM_MAXSIZE) // Set all flags to 0
            && misc_resume(snapshot)
            && keypad_resume(snapshot)
            && usb_resume(snapshot)
            && lcd_resume(snapshot)
            && des_resume(snapshot)
            && sha256_resume(snapshot)
            && serial_resume(snapshot)
            && interrupt_resume(snapshot)
            && serial_cx_resume(snapshot)
            && serial_cx2_resume(snapshot)
            && cx2_resume(snapshot)
            && usb_cx2_resume(snapshot);
}
