#include "emu.h"
#include "memory/mem.h"
#include "usb_cx2.h"

uint8_t usb_cx2_read_byte(uint32_t addr)
{
    switch(addr & 0xFFF)
    {
    case 0x00: // CAPLENGTH
        return 0x10;
    }
    return bad_read_byte(addr);
}

uint16_t usb_cx2_read_half(uint32_t addr)
{
    switch(addr & 0xFFF)
    {
    case 0x02: // HCIVERSION
        return 0x100;
    }
    return bad_read_half(addr);
}

bool usb_cx2_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &usb_cx2, sizeof(usb_cx2));
}

bool usb_cx2_resume(const emu_snapshot *snapshot)
{
    return snapshot_read(snapshot, &usb_cx2, sizeof(usb_cx2));
}
