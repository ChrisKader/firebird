#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "emu.h"
#include "fieldparser.h"
#include "memory/flash.h"
#include "memory/mem.h"
#include "cpu/cpu.h"
#include "os/os.h"

nand_state nand;
static uint8_t *nand_data = NULL;

static const struct nand_metrics chips[] = {
    {0x20, 0x35, 0x210, 5, 0x10000}, // ST Micro NAND256R3A
    {0xEF, 0xBA, 0x840, 6, 0x10000}, // Winbond W25N01GW (1 Gbit SPI NAND)
};

bool nand_initialize(bool large, const char *filename)
{
    if (nand_data)
        nand_deinitialize();

    memcpy(&nand.metrics, &chips[large], sizeof(nand_metrics));
    nand.state = 0xFF;

    nand_data = (uint8_t *)os_map_cow(filename, large ? 132 * 1024 * 1024 : 33 * 1024 * 1024);
    if (!nand_data)
        nand_deinitialize();

    return nand_data != nullptr;
}

void nand_deinitialize()
{
    if (nand_data)
        os_unmap_cow(nand_data, (nand.metrics.num_pages == 0x840) ? 132 * 1024 * 1024 : 33 * 1024 * 1024);

    nand_data = nullptr;
}

// -------------------- Classic NAND (parallel-ish abstraction) --------------------

void nand_write_command_byte(uint8_t command)
{
    switch (command)
    {
    case 0x01:
    case 0x50: // READ0, READOOB
        if (nand.metrics.page_size >= 0x800)
            goto unknown;
        // Fallthrough
    case 0x00: // READ0
        nand.nand_area_pointer = (command == 0x50) ? 2 : command;
        nand.nand_addr_state = 0;
        nand.state = 0x00;
        break;
    case 0x10: // PAGEPROG
        if (nand.state == 0x80)
        {
            if (!nand.nand_writable)
                error("program with write protect on");
            uint8_t *pagedata = &nand_data[nand.nand_row * nand.metrics.page_size + nand.nand_col];
            for (int i = 0; i < nand.nand_buffer_pos; i++)
                pagedata[i] &= nand.nand_buffer[i];
            nand.nand_block_modified[nand.nand_row >> nand.metrics.log2_pages_per_block] = true;
            nand.state = 0xFF;
        }
        break;
    case 0x30: // READSTART
        break;
    case 0x60: // ERASE1
        nand.nand_addr_state = 2;
        nand.state = command;
        break;
    case 0x80: // SEQIN
        nand.nand_buffer_pos = 0;
        nand.nand_addr_state = 0;
        nand.state = command;
        break;
    case 0xD0: // ERASE2
        if (nand.state == 0x60)
        {
            uint32_t block_bits = (1u << nand.metrics.log2_pages_per_block) - 1u;
            if (!nand.nand_writable)
                error("erase with write protect on");
            if (nand.nand_row & block_bits)
            {
                warn("NAND flash: erase nonexistent block %x", nand.nand_row);
                nand.nand_row &= ~block_bits; // Assume extra bits ignored like read
            }
            memset(&nand_data[nand.nand_row * nand.metrics.page_size], 0xFF,
                   nand.metrics.page_size << nand.metrics.log2_pages_per_block);
            nand.nand_block_modified[nand.nand_row >> nand.metrics.log2_pages_per_block] = true;
            nand.state = 0xFF;
        }
        break;
    case 0xFF: // RESET
        nand.nand_row = 0;
        nand.nand_col = 0;
        nand.nand_area_pointer = 0;
        // fallthrough
    case 0x70: // STATUS
    case 0x90: // READID
        nand.nand_addr_state = 6;
        nand.state = command;
        break;
    default:
    unknown:
        warn("Unknown NAND command %02X", command);
    }
}

void nand_write_address_byte(uint8_t byte)
{
    if (nand.nand_addr_state >= 6)
        return;

    switch (nand.nand_addr_state++)
    {
    case 0:
        if (nand.metrics.page_size < 0x800)
        {
            nand.nand_col = nand.nand_area_pointer << 8;
            nand.nand_addr_state = 2;
            nand.nand_area_pointer &= ~1;
        }
        nand.nand_col = (nand.nand_col & ~0xFFu) | byte;
        break;
    case 1:
        nand.nand_col = (nand.nand_col & 0xFFu) | (uint32_t(byte) << 8);
        break;
    default:
    {
        int bit = (nand.nand_addr_state - 3) * 8;
        nand.nand_row = (nand.nand_row & ~(0xFFu << bit)) | (uint32_t(byte) << bit);
        nand.nand_row &= nand.metrics.num_pages - 1;
        break;
    }
    }
}

uint8_t nand_read_data_byte()
{
    switch (nand.state)
    {
    case 0x00:
        if (nand.nand_col >= nand.metrics.page_size)
            return 0;
        return nand_data[nand.nand_row * nand.metrics.page_size + nand.nand_col++];
    case 0x70:
        return 0x40 | (nand.nand_writable << 7); // Status register
    case 0x90:
        nand.state++;
        return nand.metrics.chip_manuf;
    case 0x90 + 1:
        if (nand.metrics.chip_model == 0xA1)
            nand.state++;
        else
            nand.state = 0xFF;
        return nand.metrics.chip_model;
    case 0x90 + 2:
        nand.state++;
        return 1; // bits per cell: SLC
    case 0x90 + 3:
        nand.state++;
        return 0x15; // extid: erase size: 128 KiB, page size: 2048, OOB size: 64, 8-bit
    case 0x90 + 4:
        nand.state = 0xFF;
        return 0;
    default:
        return 0;
    }
}

uint32_t nand_read_data_word()
{
    switch (nand.state)
    {
    case 0x00:
        if (nand.nand_col + 4 > nand.metrics.page_size)
            return 0;
        return *(uint32_t *)&nand_data[nand.nand_row * nand.metrics.page_size + (nand.nand_col += 4) - 4];
    case 0x70:
        return 0x40 | (nand.nand_writable << 7);
    case 0x90:
        nand.state = 0xFF;
        return (nand.metrics.chip_model << 8) | nand.metrics.chip_manuf;
    default:
        return 0;
    }
}

void nand_write_data_byte(uint8_t value)
{
    switch (nand.state)
    {
    case 0x80:
        if (nand.nand_buffer_pos + nand.nand_col >= nand.metrics.page_size)
            warn("NAND write past end of page");
        else
            nand.nand_buffer[nand.nand_buffer_pos++] = value;
        return;
    default:
        warn("NAND write in state %02X", nand.state);
        return;
    }
}

void nand_write_data_word(uint32_t value)
{
    switch (nand.state)
    {
    case 0x80:
        if (nand.nand_buffer_pos + nand.nand_col + 4 > nand.metrics.page_size)
            warn("NAND write past end of page");
        else
        {
            memcpy(&nand.nand_buffer[nand.nand_buffer_pos], &value, sizeof(value));
            nand.nand_buffer_pos += 4;
        }
        break;
    default:
        warn("NAND write in state %02X", nand.state);
        return;
    }
}

// -------------------- ECC helpers --------------------

static uint32_t parity(uint32_t word)
{
    word ^= word >> 16;
    word ^= word >> 8;
    word ^= word >> 4;
    return (0x6996 >> (word & 15)) & 1;
}

static uint32_t ecc_calculate(uint8_t page[512])
{
    uint32_t ecc = 0;
    uint32_t *in = (uint32_t *)page;
    uint32_t temp[64];
    int i, j;
    uint32_t words;

    for (j = 64; j != 0; j >>= 1)
    {
        words = 0;
        for (i = 0; i < j; i++)
        {
            words ^= in[i];
            temp[i] = in[i] ^ in[i + j];
        }
        ecc = (ecc << 2) | parity(words);
        in = temp;
    }

    words = temp[0];
    ecc = (ecc << 2) | parity(words & 0x0000FFFF);
    ecc = (ecc << 2) | parity(words & 0x00FF00FF);
    ecc = (ecc << 2) | parity(words & 0x0F0F0F0F);
    ecc = (ecc << 2) | parity(words & 0x33333333);
    ecc = (ecc << 2) | parity(words & 0x55555555);
    return (ecc | (ecc << 1)) ^ (parity(words) ? 0x555555 : 0xFFFFFF);
}

// -------------------- Phoenix NAND controller (classic) --------------------

void nand_phx_reset(void)
{
    memset(&nand.phx, 0, sizeof nand.phx);
    nand.nand_writable = 1;
}

uint32_t nand_phx_read_word(uint32_t addr)
{
    switch (addr & 0x3FFFFFF)
    {
    case 0x00:
        return 0;
    case 0x08:
        return 0;
    case 0x34:
        return 0x40;
    case 0x40:
        return 1;
    case 0x44:
        return nand.phx.ecc;
    }
    return bad_read_word(addr);
}

void nand_phx_write_word(uint32_t addr, uint32_t value)
{
    switch (addr & 0x3FFFFFF)
    {
    case 0x00:
        return;
    case 0x04:
        nand.nand_writable = value;
        return;
    case 0x08:
    {
        if (value != 1)
            error("NAND controller: wrote something other than 1 to reg 8");

        uint32_t *addrp = (uint32_t *)nand.phx.address;
        logprintf(LOG_FLASH, "NAND controller: op=%06x addr=%08x size=%08x raddr=%08x\n",
                  nand.phx.operation, *addrp, nand.phx.op_size, nand.phx.ram_address);

        nand_write_command_byte(nand.phx.operation);

        for (uint32_t i = 0; i < (nand.phx.operation >> 8 & 7); i++)
            nand_write_address_byte(nand.phx.address[i]);

        if (nand.phx.operation & 0x400800)
        {
            uint8_t *ptr = (uint8_t *)phys_mem_ptr(nand.phx.ram_address, nand.phx.op_size);
            if (!ptr)
                error("NAND controller: address %x is not in RAM\n", addrp);

            if (nand.phx.operation & 0x000800)
            {
                for (uint32_t i = 0; i < nand.phx.op_size; i++)
                    nand_write_data_byte(ptr[i]);
            }
            else
            {
                for (uint32_t i = 0; i < nand.phx.op_size; i++)
                    ptr[i] = nand_read_data_byte();
            }

            if (nand.phx.op_size >= 0x200)
            {
                if (!memcmp(&nand_data[0x206], "\xFF\xFF\xFF", 3))
                    nand.phx.ecc = 0xFFFFFF;
                else
                    nand.phx.ecc = ecc_calculate(ptr);
            }
        }

        if (nand.phx.operation & 0x100000)
            nand_write_command_byte(nand.phx.operation >> 12);
        return;
    }
    case 0x0C:
        nand.phx.operation = value;
        return;
    case 0x10:
        nand.phx.address[0] = value;
        return;
    case 0x14:
        nand.phx.address[1] = value;
        return;
    case 0x18:
        nand.phx.address[2] = value;
        return;
    case 0x1C:
        nand.phx.address[3] = value;
        return;
    case 0x20:
        return;
    case 0x24:
        nand.phx.op_size = value;
        return;
    case 0x28:
        nand.phx.ram_address = value;
        return;
    case 0x2C:
        return;
    case 0x30:
        return;
    case 0x40:
        return;
    case 0x44:
        return;
    case 0x48:
        return;
    case 0x4C:
        return;
    case 0x50:
        return;
    case 0x54:
        return;
    }
    bad_write_word(addr, value);
}

// "U-Boot" diagnostics expects to access the NAND chip directly at 0x08000000
uint8_t nand_phx_raw_read_byte(uint32_t addr)
{
    if (addr == 0x08000000)
        return nand_read_data_byte();
    return bad_read_byte(addr);
}
void nand_phx_raw_write_byte(uint32_t addr, uint8_t value)
{
    if (addr == 0x08000000)
        return nand_write_data_byte(value);
    if (addr == 0x08040000)
        return nand_write_command_byte(value);
    if (addr == 0x08080000)
        return nand_write_address_byte(value);
    bad_write_byte(addr, value);
}

// -------------------- CX (classic NAND mapped) --------------------

uint8_t nand_cx_read_byte(uint32_t addr)
{
    if ((addr & 0xFF180000) == 0x81080000)
        return nand_read_data_byte();
    return bad_read_byte(addr);
}
uint32_t nand_cx_read_word(uint32_t addr)
{
    if ((addr & 0xFF180000) == 0x81080000)
        return nand_read_data_word();
    return bad_read_word(addr);
}
void nand_cx_write_byte(uint32_t addr, uint8_t value)
{
    if ((addr & 0xFF080000) == 0x81080000)
    {
        nand_write_data_byte(value);
        if (addr & 0x100000)
            nand_write_command_byte(addr >> 11);
        return;
    }
    bad_write_byte(addr, value);
}
void nand_cx_write_word(uint32_t addr, uint32_t value)
{
    static int addr_bytes_remaining = 0;
    if (addr >= 0x81000000 && addr < 0x82000000)
    {
        if (addr & 0x080000)
        {
            if (!(addr & (1 << 21)))
                warn("Doesn't work on HW");
            nand_write_data_word(value);
        }
        else
        {
            int addr_bytes = (addr >> 21) & 7;
            if (addr_bytes_remaining)
            {
                addr_bytes = addr_bytes_remaining;
                addr_bytes_remaining = 0;
            }
            if (addr_bytes > 4)
            {
                addr_bytes_remaining = addr_bytes - 4;
                addr_bytes = 4;
            }
            nand_write_command_byte(addr >> 3);
            for (; addr_bytes != 0; addr_bytes--)
            {
                nand_write_address_byte(value);
                value >>= 8;
            }
        }

        if (addr & 0x100000)
            nand_write_command_byte(addr >> 11);
        return;
    }
    bad_write_word(addr, value);
}

// -------------------- Flash file / partitions --------------------

FILE *flash_file = NULL;

typedef enum Partition
{
    PartitionManuf = 0,
    PartitionBoot2,
    PartitionBootdata,
    PartitionDiags,
    PartitionFilesystem
} Partition;

// Returns offset into nand_data
size_t flash_partition_offset(Partition p, struct nand_metrics *nand_metrics, uint8_t *nand_data_)
{
    static size_t offset_classic[] = {0, 0x4200, 0x15a800, 0x16b000, 0x210000};

    if (nand_metrics->page_size < 0x800)
        return offset_classic[p];

    static size_t parttable_cx[] = {0x870, 0x874, 0x86c, 0x878};
    if (p == PartitionManuf)
        return 0;

    return (*(uint32_t *)(nand_data_ + parttable_cx[p - 1])) / 0x800 * 0x840;
}

// -------------------- Public NAND data access API --------------------

const uint8_t *flash_get_nand_data(void)
{
    return nand_data;
}

size_t flash_get_nand_size(void)
{
    if (!nand_data)
        return 0;
    return (size_t)nand.metrics.page_size * nand.metrics.num_pages;
}

int flash_get_partitions(struct flash_partition_info *parts, int max_parts)
{
    if (!nand_data || max_parts <= 0)
        return 0;

    size_t total_size = flash_get_nand_size();

    // CX II: block-aligned partitions (SPI NAND, page_size=0x840)
    if (nand.metrics.page_size >= 0x800 && (*(uint16_t *)&nand_data[0] & 0xF0FF) == 0x0050)
    {
        uint32_t page_size = nand.metrics.page_size;
        uint32_t pages_per_block = 1u << nand.metrics.log2_pages_per_block;
        uint32_t block_size = page_size * pages_per_block;

        struct { const char *name; int start_block; int end_block; } cx2_parts[] = {
            {"Manufacturing",    0,   0},
            {"Bootloader",       1,   4},
            {"PTT Data",         5,   5},
            {"DevCert",          7,   7},
            {"OS Loader",        8,  10},
            {"Installer",       11,  18},
            {"Other Installer", 19,  26},
            {"OS Data",         27,  28},
            {"Diags",           29,  33},
            {"OS File",         34,  113},
            {"Logging",        114, 200},
            {"Filesystem",     201,  -1},  // -1 = rest of NAND
        };
        int cx2_count = sizeof(cx2_parts) / sizeof(cx2_parts[0]);
        int count = cx2_count < max_parts ? cx2_count : max_parts;
        uint32_t max_block = nand.metrics.num_pages / pages_per_block;

        for (int i = 0; i < count; i++)
        {
            parts[i].name = cx2_parts[i].name;
            parts[i].offset = (size_t)cx2_parts[i].start_block * block_size;
            uint32_t end = cx2_parts[i].end_block < 0
                ? max_block - 1
                : (uint32_t)cx2_parts[i].end_block;
            parts[i].size = ((size_t)(end - cx2_parts[i].start_block + 1)) * block_size;
            if (parts[i].offset + parts[i].size > total_size)
                parts[i].size = total_size > parts[i].offset ? total_size - parts[i].offset : 0;
        }
        return count;
    }

    // Classic/CX: use flash_partition_offset()
    static const char *names[] = {"Manufacturing", "Boot2", "Bootdata", "Diags", "Filesystem"};
    int count = 5 < max_parts ? 5 : max_parts;
    for (int i = 0; i < count; i++)
    {
        parts[i].name = names[i];
        parts[i].offset = flash_partition_offset((Partition)i, &nand.metrics, nand_data);
        if (i + 1 < 5)
        {
            size_t next = flash_partition_offset((Partition)(i + 1), &nand.metrics, nand_data);
            parts[i].size = next > parts[i].offset ? next - parts[i].offset : 0;
        }
        else
        {
            parts[i].size = total_size > parts[i].offset ? total_size - parts[i].offset : 0;
        }
    }
    return count;
}

bool flash_write_raw(size_t offset, const uint8_t *data, size_t size)
{
    if (!nand_data)
        return false;
    size_t total = flash_get_nand_size();
    if (offset + size > total)
        return false;

    memcpy(nand_data + offset, data, size);

    // Mark affected blocks as modified
    uint32_t block_size = nand.metrics.page_size << nand.metrics.log2_pages_per_block;
    uint32_t first_block = offset / block_size;
    uint32_t last_block = (offset + size - 1) / block_size;
    for (uint32_t b = first_block; b <= last_block; b++)
        nand.nand_block_modified[b] = true;

    return true;
}

// -------------------- Flash open / save --------------------

bool flash_open(const char *filename)
{
    bool large = false;
    if (flash_file)
        fclose(flash_file);

    flash_file = fopen_utf8(filename, "r+b");

    if (!flash_file)
    {
        gui_perror(filename);
        return false;
    }
    fseek(flash_file, 0, SEEK_END);
    uint32_t size = ftell(flash_file);

    if (size == 33 * 1024 * 1024)
        large = false;
    else if (size == 132 * 1024 * 1024)
        large = true;
    else
    {
        emuprintf("%s not a flash image (wrong size)\n", filename);
        return false;
    }

    if (!nand_initialize(large, filename))
    {
        fclose(flash_file);
        flash_file = NULL;
        emuprintf("Could not read flash image from %s\n", filename);
        return false;
    }

    return true;
}

bool flash_save_changes()
{
    if (flash_file == NULL)
    {
        gui_status_printf("No flash loaded!");
        return false;
    }
    uint32_t block, count = 0;
    uint32_t block_size = nand.metrics.page_size << nand.metrics.log2_pages_per_block;
    for (block = 0; block < nand.metrics.num_pages; block += 1 << nand.metrics.log2_pages_per_block)
    {
        if (nand.nand_block_modified[block >> nand.metrics.log2_pages_per_block])
        {
            fseek(flash_file, block * nand.metrics.page_size, SEEK_SET);
            fwrite(&nand_data[block * nand.metrics.page_size], block_size, 1, flash_file);
            nand.nand_block_modified[block >> nand.metrics.log2_pages_per_block] = false;
            count++;
        }
    }
    fflush(flash_file);
    gui_status_printf("Flash: Saved %d modified blocks", count);
    return true;
}

int flash_save_as(const char *filename)
{
    FILE *f = fopen_utf8(filename, "wb");
    if (!f)
    {
        emuprintf("NAND flash: could not open ");
        gui_perror(filename);
        return 1;
    }
    emuprintf("Saving flash image %s...", filename);
    if (!fwrite(nand_data, nand.metrics.page_size * nand.metrics.num_pages, 1, f) || fflush(f))
    {
        int saved_errno = errno;
        fclose(f);
        f = NULL;
        remove(filename);
        emuprintf("\n could not write to %s: %s", filename, strerror(saved_errno));
        return 1;
    }
    memset(nand.nand_block_modified, 0, nand.metrics.num_pages >> nand.metrics.log2_pages_per_block);
    if (flash_file)
        fclose(flash_file);

    flash_file = f;
    emuprintf("done\n");
    return 0;
}

static void ecc_fix(uint8_t *nand_data_, struct nand_metrics nand_metrics, int page)
{
    uint8_t *data = &nand_data_[page * nand_metrics.page_size];
    if (nand_metrics.page_size < 0x800)
    {
        uint32_t ecc = ecc_calculate(data);
        data[0x206] = ecc >> 6;
        data[0x207] = ecc >> 14;
        data[0x208] = ecc >> 22 | ecc << 2;
    }
    else
    {
        for (int i = 0; i < 4; i++)
        {
            uint32_t ecc = ecc_calculate(&data[i * 0x200]);
            data[0x808 + i * 0x10] = ecc >> 6;
            data[0x809 + i * 0x10] = ecc >> 14;
            data[0x80A + i * 0x10] = ecc >> 22 | ecc << 2;
        }
    }
}

static uint32_t load_file_part(uint8_t *nand_data_, struct nand_metrics nand_metrics, uint32_t offset, FILE *f, uint32_t length)
{
    uint32_t start = offset;
    uint32_t page_data_size = (nand_metrics.page_size & ~0x7F);
    while (length > 0)
    {
        uint32_t page = offset / page_data_size;
        uint32_t pageoff = offset % page_data_size;
        if (page >= nand_metrics.num_pages)
        {
            printf("Preload image(s) too large\n");
            return 0;
        }

        uint32_t readsize = page_data_size - pageoff;
        if (readsize > length)
            readsize = length;

        int ret = fread(&nand_data_[page * nand_metrics.page_size + pageoff], 1, readsize, f);
        if (ret <= 0)
            break;
        readsize = ret;
        ecc_fix(nand_data_, nand_metrics, page);
        offset += readsize;
        length -= readsize;
    }
    return offset - start;
}

static uint32_t load_file(uint8_t *nand_data_, struct nand_metrics nand_metrics, Partition p, const char *filename, size_t off)
{
    FILE *f = fopen_utf8(filename, "rb");
    if (!f)
    {
        gui_perror(filename);
        return 0;
    }
    size_t offset = flash_partition_offset(p, &nand_metrics, nand_data_);
    offset /= nand_metrics.page_size;
    offset *= nand_metrics.page_size & ~0x7F;
    offset += off;
    uint32_t size = load_file_part(nand_data_, nand_metrics, offset, f, -1);
    fclose(f);
    return size;
}

static void preload(uint8_t *nand_data_, struct nand_metrics nand_metrics, Partition p, const char *name, const char *filename)
{
    uint32_t page = flash_partition_offset(p, &nand_metrics, nand_data_) / nand_metrics.page_size;
    uint32_t manifest_size, image_size;

    if (emulate_casplus && strcmp(name, "IMAGE") == 0)
    {
        assert(false);
        __builtin_unreachable();
    }
    else
    {
        manifest_size = 0;
        image_size = load_file(nand_data_, nand_metrics, p, filename, 32);
        if (!image_size)
            return;
    }

    uint8_t *pagep = &nand_data_[page * nand_metrics.page_size];
    sprintf((char *)&pagep[0], "***PRELOAD_%s***", name);
    *(uint32_t *)&pagep[20] = BSWAP32(0x55F00155);
    *(uint32_t *)&pagep[24] = BSWAP32(manifest_size);
    *(uint32_t *)&pagep[28] = BSWAP32(image_size);
    ecc_fix(nand_data_, nand_metrics, page);
}

struct manuf_data_804
{
    uint16_t product;
    uint16_t revision;
    char locale[8];
    char _unknown_810[8];
    struct manuf_data_ext
    {
        uint32_t signature;
        uint32_t features;
        uint32_t default_keypad;
        uint16_t lcd_width;
        uint16_t lcd_height;
        uint16_t lcd_bpp;
        uint16_t lcd_color;
        uint32_t offset_diags;
        uint32_t offset_boot2;
        uint32_t offset_bootdata;
        uint32_t offset_filesys;
        uint32_t config_clocks;
        uint32_t config_sdram;
        uint32_t lcd_spi_count;
        uint32_t lcd_spi_data[8][2];
        uint16_t lcd_light_min;
        uint16_t lcd_light_max;
        uint16_t lcd_light_default;
        uint16_t lcd_light_incr;
    } ext;
    uint8_t bootgfx_count;
    uint8_t bootgfx_iscompressed;
    uint16_t bootgfx_unknown;
    struct
    {
        uint16_t pos_y;
        uint16_t pos_x;
        uint16_t width;
        uint16_t height;
        uint32_t offset;
    } bootgfx_images[12];
    uint32_t bootgfx_compsize;
    uint32_t bootgfx_rawsize;
    uint32_t bootgfx_certsize;
};

static uint8_t bootdata[] = {
    0xAA,
    0xC6,
    0x8C,
    0x92,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static uint8_t bootdata_cx2[] = {
    'D',
    'A',
    'T',
    'A',
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x05,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static bool load_file_cx2(uint8_t *nand_data_, struct nand_metrics nand_metrics, uint32_t block, uint32_t offset, const char *filename)
{
    FILE *f = fopen_utf8(filename, "rb");
    if (!f)
    {
        gui_perror(filename);
        return false;
    }

    size_t block_offset = block * (nand_metrics.page_size & ~0x7F) << nand_metrics.log2_pages_per_block;
    bool ret = load_file_part(nand_data_, nand_metrics, block_offset + offset, f, -1) != 0;
    fclose(f);
    return ret;
}

bool flash_create_new(bool flag_large_nand, const char **preload_file, unsigned int product, unsigned int features, bool large_sdram, uint8_t **nand_data_ptr, size_t *size)
{
    assert(nand_data_ptr);
    assert(size);

    struct nand_metrics nand_metrics;
    memcpy(&nand_metrics, &chips[flag_large_nand], sizeof(nand_metrics));

    *size = nand_metrics.page_size * nand_metrics.num_pages;
    uint8_t *nand_data_ = *nand_data_ptr = (uint8_t *)malloc(*size);
    if (!nand_data_)
        return false;

    memset(nand_data_, 0xFF, *size);
    gui_debug_printf("product [0x%04x]", product);

    // CX II?
    if (product >= 0x1C0)
    {
        bool ret = true;
        if (preload_file[0])
            ret = load_file_cx2(nand_data_, nand_metrics, 0, 0, preload_file[0]);
        else
            ret = false;

        if (ret && preload_file[1])
            ret = load_file_cx2(nand_data_, nand_metrics, 1, 0, preload_file[1]);
        if (ret && preload_file[2])
            ret = load_file_cx2(nand_data_, nand_metrics, 29, 0, preload_file[2]);
        if (ret && preload_file[3])
            ret = load_file_cx2(nand_data_, nand_metrics, 11, 0, preload_file[3]);

        if (!ret)
            return false;

        size_t bootdata_offset = (27 << nand_metrics.log2_pages_per_block) * nand_metrics.page_size;
        bootdata_offset += nand_metrics.page_size;

        memset(nand_data_ + bootdata_offset, 0xFF, nand_metrics.page_size);
        memset(nand_data_ + bootdata_offset + 1024, 0, 1024);
        memcpy(nand_data_ + bootdata_offset, bootdata_cx2, sizeof(bootdata_cx2));

        return true;
    }

    if (preload_file[0])
    {
        load_file(nand_data_, nand_metrics, PartitionManuf, preload_file[0], 0);

        struct manuf_data_804 *manuf = (struct manuf_data_804 *)&nand_data_[0x844];
        manuf->product = product >> 4;
        manuf->revision = product & 0xF;
        if (product >= 0x0F)
            manuf->ext.features = features;
        ecc_fix(nand_data_, nand_metrics, nand_metrics.page_size < 0x800 ? 4 : 1);
    }
    else if (product != 0x0C0)
    {
        *(uint32_t *)&nand_data_[0] = 0x796EB03C;
        ecc_fix(nand_data_, nand_metrics, 0);

        struct manuf_data_804 *manuf = (struct manuf_data_804 *)&nand_data_[0x844];
        manuf->product = product >> 4;
        manuf->revision = product & 0xF;
        if (manuf->product >= 0x0F)
        {
            manuf->ext.signature = 0x4C9E5F91;
            manuf->ext.features = features;
            manuf->ext.default_keypad = 76;
            manuf->ext.lcd_width = 320;
            manuf->ext.lcd_height = 240;
            manuf->ext.lcd_bpp = 16;
            manuf->ext.lcd_color = 1;
            if (nand_metrics.page_size < 0x800)
            {
                manuf->ext.offset_diags = 0x160000;
                manuf->ext.offset_boot2 = 0x004000;
                manuf->ext.offset_bootdata = 0x150000;
                manuf->ext.offset_filesys = 0x200000;
            }
            else
            {
                manuf->ext.offset_diags = 0x320000;
                manuf->ext.offset_boot2 = 0x020000;
                manuf->ext.offset_bootdata = 0x2C0000;
                manuf->ext.offset_filesys = 0x400000;
            }
            manuf->ext.config_clocks = 0x561002;
            manuf->ext.config_sdram = large_sdram ? 0xFC018012 : 0xFE018011;
            manuf->ext.lcd_spi_count = 0;
            manuf->ext.lcd_light_min = 0x11A;
            manuf->ext.lcd_light_max = 0x1CE;
            manuf->ext.lcd_light_default = 0x16A;
            manuf->ext.lcd_light_incr = 0x14;
            manuf->bootgfx_count = 0;
        }
        ecc_fix(nand_data_, nand_metrics, nand_metrics.page_size < 0x800 ? 4 : 1);
    }

    if (preload_file[1])
        load_file(nand_data_, nand_metrics, PartitionBoot2, preload_file[1], 0);
    size_t bootdata_offset = flash_partition_offset(PartitionBootdata, &nand_metrics, nand_data_);
    memset(nand_data_ + bootdata_offset, 0xFF, nand_metrics.page_size);
    memset(nand_data_ + bootdata_offset, 0, 512);
    memcpy(nand_data_ + bootdata_offset, bootdata, sizeof(bootdata));
    ecc_fix(nand_data_, nand_metrics, bootdata_offset / nand_metrics.page_size);
    if (preload_file[2])
        load_file(nand_data_, nand_metrics, PartitionDiags, preload_file[2], 0);
    if (preload_file[3])
        preload(nand_data_, nand_metrics, PartitionFilesystem, "IMAGE", preload_file[3]);

    return true;
}

bool flash_read_settings(uint32_t *sdram_size, uint32_t *product, uint32_t *features, uint32_t *asic_user_flags)
{
    assert(nand_data);

    *sdram_size = 32 * 1024 * 1024;
    *features = 0;
    *asic_user_flags = 0;

    if (*(uint32_t *)&nand_data[0] == 0xFFFFFFFF)
    {
        *product = 0x0C0;
        return true;
    }

    if ((*(uint16_t *)&nand_data[0] & 0xF0FF) == 0x0050)
    {
        *sdram_size = 64 * 1024 * 1024;

        auto manufField = FieldParser(nand_data, 2048, true);

        auto productField = manufField.subField(0x5100);
        if (!productField.isValid() || productField.sizeOfData() != 2)
        {
            /* CX II format detected but product field missing -- default to CX II */
            *product = 0x1C0;
            emuprintf("CX II manuf detected but product field (0x5100) missing; defaulting product=0x%x\n", *product);
            *features = 1;
            return true;
        }

        *product = (productField.data()[0] << 12) | (productField.data()[1] << 4);

        static const unsigned char flags[] = {1, 0, 2};
        if (*product <= 0x1E0)
            *asic_user_flags = flags[(*product >> 4) - 0x1C];

        auto flagsField = manufField.subField(0x5400);
        if (flagsField.isValid() && flagsField.sizeOfData() == 4)
        {
            auto d = flagsField.data();
            *features = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
        }
        else
        {
            *features = 1;
            emuprintf("Failed to parse hardware flags in CX II manuf; defaulting features=0x%x\n", *features);
        }

        return *product >= 0x1C0;
    }

    struct manuf_data_804 *manuf = (struct manuf_data_804 *)&nand_data[0x844];
    *product = manuf->product << 4 | manuf->revision;

    static const unsigned char flags[] = {1, 0, 0, 1, 0, 3, 2};
    if (manuf->product >= 0x0C && manuf->product <= 0x12)
        *asic_user_flags = flags[manuf->product - 0x0C];

    if (*product >= 0x0F0 && manuf->ext.signature == 0x4C9E5F91)
    {
        uint32_t cfg = manuf->ext.config_sdram;
        int logsize = (cfg & 7) + ((cfg >> 3) & 7);
        if (logsize > 4)
        {
            emuprintf("Invalid SDRAM size in flash\n");
            return false;
        }
        *features = manuf->ext.features;
        *sdram_size = (4 * 1024 * 1024) << logsize;
    }

    return true;
}

std::string flash_read_type(FILE *flash, bool manuf_file)
{
    uint32_t i;
    if (fread(&i, sizeof(i), 1, flash) != 1)
        return "";

    if (i == 0xFFFFFFFF)
        return "CAS+";

    uint32_t product = 0, features = 0, revision = 0;

    if ((i & 0xF0FF) == 0x0050)
    {
        uint8_t manuf[2048];
        if (fseek(flash, 0, SEEK_SET) != 0 || fread(&manuf, sizeof(manuf), 1, flash) != 1)
            return "";

        auto productField = FieldParser(manuf, sizeof(manuf), true).subField(0x5100);
        if (!productField.isValid() || productField.sizeOfData() != 2)
            return "???";

        product = (productField.data()[0] << 8) | productField.data()[1];
        if (product < 0x1C)
            return "???";
    }
    else
    {
        struct manuf_data_804 manuf;
        int offset = manuf_file ? 0x804 : 0x844;
        if ((fseek(flash, offset - sizeof(i), SEEK_CUR) != 0) || (fread(&manuf, sizeof(manuf), 1, flash) != 1))
            return "";

        product = manuf.product;
        if (product >= 0x0F)
            features = manuf.ext.features;
        revision = manuf.revision;
    }

    std::string ret;
    switch (product)
    {
    case 0x0C:
        ret = (revision < 2) ? "Clickpad CAS" : "Touchpad CAS";
        break;
    case 0x0D:
        ret = "Lab Cradle";
        break;
    case 0x0E:
        ret = "Touchpad";
        break;
    case 0x0F:
        ret = "CX CAS";
        break;
    case 0x10:
        ret = "CX";
        break;
    case 0x11:
        ret = "CM CAS";
        break;
    case 0x12:
        ret = "CM";
        break;
    case 0x1C:
        ret = "CX II CAS";
        break;
    case 0x1D:
        ret = "CX II";
        break;
    case 0x1E:
        ret = "CX II-T";
        break;
    default:
        ret = "???";
        break;
    }

    if (product >= 0x0F && product <= 0x12)
    {
        switch (features)
        {
        case 0x05:
            ret += " (HW A)";
            break;
        case 0x85:
            ret += " (HW J)";
            break;
        case 0x185:
            ret += " (HW W)";
            break;
        default:
            ret += " (HW ?)";
            break;
        }
    }

    return ret;
}

static bool convert_version(std::string &version)
{
    auto middle_start = version.find('.');
    if (middle_start == std::string::npos)
        return false;

    auto middle_end = version.find('.', middle_start + 1);
    if (middle_end == std::string::npos)
        return false;

    const auto middle_len = middle_end - middle_start - 1;
    if (middle_len == 1)
        version.insert(middle_start + 1, "0.");
    else if (middle_len == 2)
        version.insert(middle_start + 2, ".");
    return true;
}

bool flash_component_info(FILE *file, std::string &type, std::string &version)
{
    uint8_t header[2048];
    if (fread(&header, sizeof(header), 1, file) != 1)
        return false;

    auto parser = FieldParser(header, sizeof(header), true);
    if (!parser.isValid() || parser.id() != 0x8000)
        return false;

    auto typeField = parser.subField(0x8040),
         versionField = parser.subField(0x8020);
    if (!typeField.isValid() || !versionField.isValid())
        return false;

    type = std::string(reinterpret_cast<const char *>(typeField.data()), typeField.sizeOfData());
    version = std::string(reinterpret_cast<const char *>(versionField.data()), versionField.sizeOfData());

    return convert_version(version);
}

bool flash_os_info(FILE *file, std::string &version)
{
    std::string header;
    {
        uint8_t header_data[1024];
        if (fread(&header_data, sizeof(header_data), 1, file) != 1)
            return false;
        header = std::string(reinterpret_cast<const char *>(header_data), sizeof(header_data));
    }

    auto pos = header.find("TI-Nspire.");
    if (pos == std::string::npos)
        return false;

    auto extstart = pos + sizeof("TI-Nspire.") - 1;

    auto sep = " \r\n";
    pos = header.find_first_of(sep, pos);
    if (pos == std::string::npos)
        return false;

    auto ext = header.substr(extstart, pos - extstart);

    pos = header.find_first_not_of(sep, pos);
    if (pos == std::string::npos)
        return false;

    auto end_pos = header.find_first_of(sep, pos);
    if (pos == std::string::npos)
        return false;

    version = header.substr(pos, end_pos - pos);
    if (!convert_version(version))
        return false;

    if (ext == "tno")
    {
    }
    else if (ext == "tnc")
        version += " CAS";
    else if (ext == "tco")
        version += " CX";
    else if (ext == "tcc")
        version += " CX CAS";
    else if (ext == "tco2")
        version += " CX II";
    else if (ext == "tcc2")
        version += " CX II CAS";
    else if (ext == "tct2")
        version += " CX II-T";
    else
        return false;

    return true;
}

bool flash_suspend(emu_snapshot *snapshot)
{
    if (!snapshot_write(snapshot, &nand, sizeof(nand)))
        return false;

    const size_t num_blocks = nand.metrics.num_pages >> nand.metrics.log2_pages_per_block,
                 block_size = nand.metrics.page_size << nand.metrics.log2_pages_per_block;

    for (unsigned int cur_modified_block_nr = 0; cur_modified_block_nr < num_blocks; ++cur_modified_block_nr)
    {
        if (!nand.nand_block_modified[cur_modified_block_nr])
            continue;

        if (!snapshot_write(snapshot, nand_data + block_size * cur_modified_block_nr, block_size))
            return true;
    }

    return true;
}

bool flash_resume(const emu_snapshot *snapshot)
{
    flash_close();

    if (!flash_open(snapshot->header.path_flash))
        return false;

    if (!snapshot_read(snapshot, &nand, sizeof(nand)))
        return false;

    const size_t num_blocks = nand.metrics.num_pages >> nand.metrics.log2_pages_per_block,
                 block_size = nand.metrics.page_size << nand.metrics.log2_pages_per_block;

    for (unsigned int cur_modified_block_nr = 0; cur_modified_block_nr < num_blocks; ++cur_modified_block_nr)
    {
        if (!nand.nand_block_modified[cur_modified_block_nr])
            continue;

        if (!snapshot_read(snapshot, nand_data + block_size * cur_modified_block_nr, block_size))
            return false;
    }

    return true;
}

void flash_close()
{
    if (flash_file)
    {
        fclose(flash_file);
        flash_file = NULL;
    }

    nand_deinitialize();
}

void flash_set_bootorder(BootOrder order)
{
    assert(nand_data);

    if (order == ORDER_DEFAULT)
        return;

    // CX II
    if ((*(uint16_t *)&nand_data[0] & 0xF0FF) == 0x0050)
    {
        // Find bootdata page by scanning for the "DATA" signature.
        const size_t page_size = nand.metrics.page_size;
        const size_t block_size = page_size << nand.metrics.log2_pages_per_block;
        const uint32_t max_blocks = std::min<uint32_t>(nand.metrics.num_pages >> nand.metrics.log2_pages_per_block, 128);

        bool found = false;
        uint32_t target_blk = 0, target_page = 0;
        size_t target_off = 0;
        for (uint32_t blk = 0; blk < max_blocks; ++blk)
        {
            size_t base = blk * block_size;
            for (uint32_t page = 0; page < (1u << nand.metrics.log2_pages_per_block); ++page)
            {
                size_t off = base + page * page_size;
                if (*(uint32_t *)(nand_data + off) == 0x41544144) // 'DATA'
                {
                    found = true;
                    target_blk = blk;
                    target_page = page;
                    target_off = off;
                }
            }
        }
        if(found)
        {
            uint32_t mode = (order == ORDER_DIAGS) ? 0x02 : 0x01;
            // Known offsets: +4 used on CX II bootdata, +0x10 matches legacy layout
            *(uint32_t *)(nand_data + target_off + 4) = mode;
            *(uint32_t *)(nand_data + target_off + 0x10) = mode;
            nand.nand_block_modified[target_blk] = true;
            gui_debug_printf("Bootdata patched at block %u page %u: mode %u\n", target_blk, target_page, mode);
        }
        else
            gui_debug_printf("Bootdata 'DATA' signature not found, boot order unchanged\n");
        return;
    }

    size_t bootdata_offset = flash_partition_offset(PartitionBootdata, &nand.metrics, nand_data);

    if (*(uint32_t *)(nand_data + bootdata_offset) != 0x928cc6aa)
    {
        memset(nand_data + bootdata_offset, 0xFF, nand.metrics.page_size);
        memset(nand_data + bootdata_offset + 0x62, 0, 414);
        memcpy(nand_data + bootdata_offset, bootdata, sizeof(bootdata));
    }

    while (*(uint32_t *)(nand_data + bootdata_offset) == 0x928cc6aa)
    {
        *(uint32_t *)(nand_data + bootdata_offset + 0x10) = order;
        unsigned int page = bootdata_offset / nand.metrics.page_size;
        nand.nand_block_modified[page >> nand.metrics.log2_pages_per_block] = true;
        ecc_fix(nand_data, nand.metrics, page);
        bootdata_offset += nand.metrics.page_size;
    }
}

// -------------------- SPI NAND (Winbond W25N01GW path used on CX II) --------------------

enum class FlashSPICmd : uint8_t
{
    GET_FEATURES = 0x0F,
    SET_FEATURES = 0x1F,
    JEDEC_ID = 0x9F,
    READ_FROM_CACHE = 0x0B,
    READ_FROM_CACHE_x4 = 0x6B,
    PROGRAM_EXECUTE = 0x10,
    READ_PAGE = 0x13,
    BLOCK_ERASE = 0xD8,
    PROGRAM_LOAD = 0x02,
    PROGRAM_LOAD_x4 = 0x32,
    PROGRAM_LOAD_RANDOM_DATA = 0x84,
    PROGRAM_LOAD_RANDOM_DATA_x4 = 0x34,
    WRITE_DISABLE = 0x04,
    WRITE_ENABLE = 0x06,
};

// ONFI param page (minimal, emulated)
struct flash_param_page_struct
{
    flash_param_page_struct() : signature{'O', 'N', 'F', 'I'},
                                optional_commands{6},
                                manufacturer{'W', 'I', 'N', 'B', 'O', 'N', 'D', ' ', ' ', ' ', ' ', ' '},
                                model{'W', '2', '5', 'N', '0', '1', 'G', 'W', 'Z', 'E', 'I', 'G', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                                manuf_id{0xEF},
                                page_data_size{2048},
                                page_spare_size{64},
                                partial_page_data_size{512},
                                partial_page_spare_size{16},
                                pages_per_block{64},
                                blocks_per_unit{1024},
                                count_logical_units{1},
                                bits_per_cell{1},
                                bad_blocks_per_unit_max{20},
                                block_endurance{0x501},
                                programs_per_page{4},
                                pin_capacitance{10},
                                time_max_prog{900},
                                time_max_erase{10000},
                                time_max_read{100},
                                rev_vendor{1}
    {
    }

    char signature[4];
    uint16_t revision;
    uint16_t features;
    uint16_t optional_commands;
    char reserved0[22];
    char manufacturer[12];
    char model[20];
    uint8_t manuf_id;
    uint16_t date_code;
    uint8_t reserved1[13];
    uint32_t page_data_size;
    uint16_t page_spare_size;
    uint32_t partial_page_data_size;
    uint16_t partial_page_spare_size;
    uint32_t pages_per_block;
    uint32_t blocks_per_unit;
    uint8_t count_logical_units;
    uint8_t address_cycles;
    uint8_t bits_per_cell;
    uint16_t bad_blocks_per_unit_max;
    uint16_t block_endurance;
    uint8_t guaranteed_valid_blocks;
    uint8_t programs_per_page;
    uint8_t toolazytotype[17];
    uint8_t pin_capacitance;
    uint16_t timing[2];
    uint16_t time_max_prog;
    uint16_t time_max_erase;
    uint16_t time_max_read;
    uint8_t toolazy[27];
    uint16_t rev_vendor;
    uint8_t vendor_data[88];
    uint16_t crc;
} __attribute__((packed));

static flash_param_page_struct param_page{};

static uint16_t onfi_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0x4F4; // ONFI polynomial init
    while (len--)
    {
        crc ^= static_cast<uint16_t>(*buf++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
    }
    return crc;
}

void flash_spi_reset()
{
    memset(&nand.spi, 0, sizeof(nand.spi));
    memset(nand.nand_buffer, 0xFF, sizeof(nand.nand_buffer));
    static bool param_page_initialized = false;
    if (!param_page_initialized)
    {
        static_assert(sizeof(param_page) == 256, "");
        param_page.crc = onfi_crc16(reinterpret_cast<const uint8_t *>(&param_page),
                                    sizeof(param_page) - sizeof(param_page.crc));
        param_page_initialized = true;
    }
}

static uint8_t flash_spi_transceive(uint8_t data = 0x00)
{
    uint8_t ret = 0;
    bool bump_address = true;

    switch (nand.state)
    {
    case SPI_COMMAND: // Command cycle
        nand.spi.command = data;

        nand.spi.address = 0;
        nand.nand_addr_state = 0;
        nand.spi.address_cycles_total = 0;
        nand.spi.dummy_cycles_remaining = 0;

        switch (FlashSPICmd(nand.spi.command))
        {
        case FlashSPICmd::GET_FEATURES:
        case FlashSPICmd::SET_FEATURES:
            nand.spi.address_cycles_total = 1;
            break;

        case FlashSPICmd::JEDEC_ID:
            // Spec: 8 dummy clocks => 1 dummy byte.
            nand.spi.dummy_cycles_remaining = 1;
            nand.state = SPI_DUMMY;
            bump_address = false; // Keep address at 0 through dummy + ID
            return ret;

        case FlashSPICmd::READ_FROM_CACHE:
        case FlashSPICmd::READ_FROM_CACHE_x4:
            nand.spi.address_cycles_total = 2;
            nand.spi.dummy_cycles_remaining = 1;
            break;

        case FlashSPICmd::PROGRAM_EXECUTE:
        case FlashSPICmd::READ_PAGE:
        case FlashSPICmd::BLOCK_ERASE:
            nand.spi.address_cycles_total = 3;
            break;

        case FlashSPICmd::PROGRAM_LOAD:
        case FlashSPICmd::PROGRAM_LOAD_x4:
            memset(nand.nand_buffer, 0xFF, sizeof(nand.nand_buffer));
            /* fallthrough */
        case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA:
        case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA_x4:
            nand.spi.address_cycles_total = 2;
            break;

        case FlashSPICmd::WRITE_DISABLE:
            nand.nand_writable = false;
            break;
        case FlashSPICmd::WRITE_ENABLE:
            nand.nand_writable = true;
            break;

        default:
            warn("Unknown flash SPI command %x", data);
        }

        nand.state = SPI_ADDRESS;
        break;

    case SPI_ADDRESS: // Address cycles
        nand.spi.address |= uint32_t(data) << (nand.nand_addr_state * 8);
        nand.nand_addr_state += 1;

        if (nand.nand_addr_state == nand.spi.address_cycles_total)
        {
            nand.state = nand.spi.dummy_cycles_remaining ? SPI_DUMMY : SPI_DATA;
            switch (FlashSPICmd(nand.spi.command))
            {
            case FlashSPICmd::READ_PAGE:
            {
                auto page_size = param_page.page_data_size + param_page.page_spare_size;
                memcpy(nand.nand_buffer, &nand_data[nand.spi.address * page_size], page_size);
                break;
            }
            case FlashSPICmd::BLOCK_ERASE:
            {
                if (!nand.nand_writable)
                    break;

                auto page_size = param_page.page_data_size + param_page.page_spare_size;
                auto block_size = param_page.pages_per_block * page_size;
                auto block_number = nand.spi.address / param_page.pages_per_block;
                memset(nand_data + (block_number * block_size), 0xFF, block_size);
                nand.nand_block_modified[block_number] = true;
                break;
            }
            case FlashSPICmd::PROGRAM_EXECUTE:
            {
                if (!nand.nand_writable)
                    break;

                auto page_size = param_page.page_data_size + param_page.page_spare_size;
                auto page_pointer = &nand_data[page_size * nand.spi.address];
                for (size_t i = 0; i < page_size; ++i)
                    page_pointer[i] &= nand.nand_buffer[i];

                nand.nand_block_modified[nand.spi.address / param_page.pages_per_block] = true;
                break;
            }

            case FlashSPICmd::READ_FROM_CACHE:
            case FlashSPICmd::READ_FROM_CACHE_x4:
            case FlashSPICmd::PROGRAM_LOAD:
            case FlashSPICmd::PROGRAM_LOAD_x4:
            case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA:
            case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA_x4:
                nand.spi.address &= ~0x1000; // ignore plane bit
                break;

            case FlashSPICmd::GET_FEATURES:
            case FlashSPICmd::SET_FEATURES:
                break;

            default:
                warn("Unhandled?");
            }
        }
        break;

    case SPI_DUMMY: // Dummy cycles
        if (--nand.spi.dummy_cycles_remaining == 0)
            nand.state = SPI_DATA;
        if (FlashSPICmd(nand.spi.command) == FlashSPICmd::JEDEC_ID)
            bump_address = false;
        break;

    case SPI_DATA: // Data cycles
        switch (FlashSPICmd(nand.spi.command))
        {
        case FlashSPICmd::PROGRAM_LOAD:
        case FlashSPICmd::PROGRAM_LOAD_x4:
        case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA:
        case FlashSPICmd::PROGRAM_LOAD_RANDOM_DATA_x4:
            if (nand.spi.address < sizeof(nand.nand_buffer))
                nand.nand_buffer[nand.spi.address] = data;
            break;

        case FlashSPICmd::GET_FEATURES:
            switch (nand.spi.address)
            {
            case 0xA0: // block lock
            case 0xB0: // OTP / "feature" used here to toggle param page
                ret = 0;
                break;
            case 0xC0: // status
                ret = nand.nand_writable << 1;
                break;
            default:
                warn("Unknown status register %x\n", nand.spi.address);
            }
            break;

        case FlashSPICmd::READ_FROM_CACHE:
        case FlashSPICmd::READ_FROM_CACHE_x4:
            if (nand.spi.param_page_active)
            {
                auto param_page_raw = reinterpret_cast<const uint8_t *>(&param_page);
                ret = param_page_raw[nand.spi.address & 0xFF];
            }
            else
            {
                if (nand.spi.address < sizeof(nand.nand_buffer))
                    ret = nand.nand_buffer[nand.spi.address];
                else
                    warn("Read past end of page\n");
            }
            break;

        case FlashSPICmd::JEDEC_ID:
        {
            static const uint8_t jedec_id[] = {0xEF, 0xBA, 0x21};
            auto idx = nand.spi.address;
            if (idx < sizeof(jedec_id))
                ret = jedec_id[idx];
            else
                ret = 0;
            bump_address = false; // keep at 0 for the duration of ID readout
            break;
        }

        case FlashSPICmd::SET_FEATURES:
            if (nand.spi.address == 0xB0 && data == 0x00)
                nand.spi.param_page_active = false;
            else if (nand.spi.address == 0xB0 && data == 0x40)
                nand.spi.param_page_active = true;
            else
                warn("Unknown SET FEATURE request %x at %x", data, nand.spi.address);
            break;

        default:
            warn("Data cycle with unknown command");
            break;
        }

        if (bump_address)
            nand.spi.address += 1;
        break;
    }

    return ret;
}

static void flash_spi_cs(bool enabled)
{
    if (enabled)
        return;

    if (nand.nand_addr_state < nand.spi.address_cycles_total || nand.spi.dummy_cycles_remaining)
        warn("CS disabled before command complete");

    nand.state = SPI_COMMAND;
}

// -------------------- CX II SPI-NAND controller wrapper --------------------

struct nand_cx2_state nand_cx2_state;

static uint8_t spinand_cx2_transceive(uint8_t data = 0x00)
{
    switch (nand_cx2_state.active_cs)
    {
    case 1: // NAND
        return flash_spi_transceive(data);
    case 0: // Not connected
    case 2:
    case 3:
        return 0;
    default:
        warn("Unknown chip select %d\n", nand_cx2_state.active_cs);
        return 0;
    case 0xFF:
        warn("Transmission without chip select active\n");
        return 0;
    }
}

static void spinand_cx2_set_cs(uint8_t cs, bool state)
{
    nand_cx2_state.active_cs = state ? cs : 0xFF;

    switch (cs)
    {
    case 1: // NAND
        return flash_spi_cs(state);
    case 0:
    case 2:
    case 3:
        return;
    default:
        warn("Unknown chip select %d\n", cs);
        return;
    }
}

uint32_t spinand_cx2_read_word(uint32_t addr)
{
    switch (addr & 0xFFFF)
    {
    case 0x000:
        return nand_cx2_state.addr; // REG_CMD0
    case 0x004:
        return nand_cx2_state.cycl; // REG_CMD1
    case 0x008:
        return nand_cx2_state.len; // REG_CMD2
    case 0x00c:
        return nand_cx2_state.cmd; // REG_CMD3
    case 0x010:
        return nand_cx2_state.ctrl; // REG_CTRL
    case 0x018:
        return 0b11; // REG_STR
    case 0x020:
        return nand_cx2_state.icr; // REG_ICR
    case 0x024:
        return nand_cx2_state.isr; // REG_ISR
    case 0x028:
        return nand_cx2_state.rdsr; // REG_RDST
    case 0x054:
        return 0x02022020; // REG_FEA
    case 0x100:            // REG_DATA
    {
        if (nand_cx2_state.cmd & 0x2)
            return 0;

        uint32_t data = 0;
        for (int i = 0; i < 4 && nand_cx2_state.len_cur; i++, nand_cx2_state.len_cur--)
            data |= uint32_t(spinand_cx2_transceive(0)) << (i << 3);

        if (!nand_cx2_state.len_cur)
        {
            spinand_cx2_set_cs(nand_cx2_state.active_cs, false);
            nand_cx2_state.isr |= 1;
        }
        return data;
    }
    }

    return bad_read_word(addr);
}

uint8_t spinand_cx2_read_byte(uint32_t addr)
{
    return spinand_cx2_read_word(addr);
}

void spinand_cx2_write_word(uint32_t addr, uint32_t value)
{
    switch (addr & 0xFFFF)
    {
    case 0x000:
        nand_cx2_state.addr = value;
        return; // REG_CMD0
    case 0x004:
        nand_cx2_state.cycl = value;
        return; // REG_CMD1
    case 0x008:
        nand_cx2_state.len_cur = nand_cx2_state.len = value;
        return; // REG_CMD2
    case 0x00c: // REG_CMD3
    {
        nand_cx2_state.cmd = value;
        uint8_t cs = (nand_cx2_state.cmd >> 8) & 0x3;

        // Toggle CS
        spinand_cx2_set_cs(cs, false);
        spinand_cx2_set_cs(cs, true);

        uint8_t cmd = nand_cx2_state.cmd >> 24;

        unsigned int cycl;
        for (cycl = 0; cycl < std::min((nand_cx2_state.cycl >> 24) & 3, 2u); cycl++)
            spinand_cx2_transceive(cmd);

        for (cycl = 0; cycl < std::min(nand_cx2_state.cycl & 7, 4u); cycl++)
            spinand_cx2_transceive(nand_cx2_state.addr >> (cycl << 3));

        for (cycl = 0; cycl < ((nand_cx2_state.cycl >> 19) & 0x1F); cycl++)
            spinand_cx2_transceive(0);

        if ((nand_cx2_state.cmd & 0x6) == 0x4)
        {
            do
            {
                nand_cx2_state.rdsr = spinand_cx2_transceive();
                if (nand_cx2_state.cmd & 0x8)
                    break;
            } while (nand_cx2_state.rdsr & (1 << nand_cx2_state.wip));
        }

        if (nand_cx2_state.len_cur == 0)
            spinand_cx2_set_cs(cs, false);

        nand_cx2_state.isr |= 1;
        return;
    }
    case 0x010: // REG_CTRL
        nand_cx2_state.ctrl = value & 0x70013;
        nand_cx2_state.wip = (value >> 16) & 0x7;
        return;

    case 0x020:
        nand_cx2_state.icr = value;
        return; // REG_ICR
    case 0x024:
        nand_cx2_state.isr &= ~value;
        return; // REG_ISR
    case 0x100: // REG_DATA
        if ((nand_cx2_state.cmd & 0x2) == 0)
            return;

        for (int i = 0; i < 4 && nand_cx2_state.len_cur > 0; i++, nand_cx2_state.len_cur--)
            spinand_cx2_transceive(value >> (i << 3));

        if (nand_cx2_state.len_cur == 0)
        {
            spinand_cx2_set_cs(nand_cx2_state.active_cs, false);
            nand_cx2_state.isr |= 1;
        }
        return;
    }

    return bad_write_word(addr, value);
}

void spinand_cx2_write_byte(uint32_t addr, uint8_t value)
{
    return spinand_cx2_write_word(addr, value);
}
