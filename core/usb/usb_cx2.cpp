#include <cassert>
#include <queue>

#include "emu.h"
#include "memory/mem.h"
#include "usb/usb.h"
#include "usb/usb_cx2.h"
#include "usb/usblink_cx2.h"
#include "peripherals/interrupt.h"

usb_cx2_state usb_cx2;

static bool usb_cx2_physical_vbus_present()
{
    if (hw_override_get_usb_otg_cable() > 0)
        return false;

    const int8_t cable = hw_override_get_usb_cable_connected();
    if (cable >= 0) {
        if (cable == 0)
            return false;
        const int vbus_mv_forced = hw_override_get_vbus_mv();
        return vbus_mv_forced >= 4500;
    }

    const int vbus_mv = hw_override_get_vbus_mv();
    if (vbus_mv >= 0)
        return vbus_mv >= 4500;

    return false;
}

static uint32_t usb_cx2_otgcs_idle_value()
{
    /* Values aligned with observed firmware logs:
     *   connected:    otgcsr=b20000
     *   disconnected: otgcsr=b02e00
     */
    if (usb_cx2_physical_vbus_present())
        return 0x00B20000u;
    return 0x00B02E00u;
}

static void usb_cx2_int_check()
{
    // Update device controller group interrupt status
    usb_cx2.gisr_all = 0;

    if (usb_cx2.rxzlp)
        usb_cx2.gisr[2] |= 1 << 6;
    if (usb_cx2.txzlp)
        usb_cx2.gisr[2] |= 1 << 5;
    for(int i = 0; i < 3; ++i)
        if(usb_cx2.gisr[i] & ~usb_cx2.gimr[i])
            usb_cx2.gisr_all |= 1 << i;

    if(usb_cx2.dmasr & ~usb_cx2.dmamr)
        usb_cx2.gisr_all |= 1 << 3; // Device DMA interrupt

    // Update controller interrupt status
    usb_cx2.isr = 0;

    if((usb_cx2.gisr_all & ~usb_cx2.gimr_all) && (usb_cx2.devctrl & 0b100))
        usb_cx2.isr |= 1 << 0; // Device interrupt

    if(usb_cx2.otgisr & usb_cx2.otgier)
        usb_cx2.isr |= 1 << 1; // OTG interrupt

    if(usb_cx2.usbsts & usb_cx2.usbintr)
        usb_cx2.isr |= 1 << 2; // Host interrupt

    int_set(INT_USB, usb_cx2.isr & ~usb_cx2.imr);
}

static void usb_cx2_reassert_fifo_data_irq()
{
    // FOTG210 FIFO OUT interrupt behaves like level while data is pending.
    // Reassert when bytes remain so guest RX does not stall after IRQ clear.
    // Raise both OUT and SPK: guest code may gate reads on either bit.
    for(int fifo = 0; fifo < 4; ++fifo)
        if(usb_cx2.fifo[fifo].size)
            usb_cx2.gisr[1] |= (0b11u << (fifo * 2));
}

struct usb_packet {
    usb_packet(uint16_t size) : size(size) {}
    uint16_t size;
    uint8_t data[1024];
};

std::queue<usb_packet> send_queue;
std::queue<usb_packet> send_queue_ack;

static bool usb_cx2_is_ack_packet(const uint8_t *packet, size_t size)
{
    // NNSE service byte is at offset 1; bit7 marks ACK.
    return size >= 2 && (packet[1] & 0x80) != 0;
}

static bool usb_cx2_real_packet_to_calc(uint8_t ep, const uint8_t *packet, size_t size)
{
    uint8_t fifo = (usb_cx2.epmap[ep > 4] >> (8 * ((ep - 1) & 0b11) + 4)) & 0b11;

    // +1 to adjust for the hack below
    if(size + 1 > sizeof(usb_cx2.fifo[fifo].data) - usb_cx2.fifo[fifo].size)
    {
        warn("usb_cx2_real_packet_to_calc: fifo full ep=%u fifo=%u size=%zu fifo_size=%zu",
             ep, fifo, size, usb_cx2.fifo[fifo].size);
        return false;
    }

    memcpy(&usb_cx2.fifo[fifo].data[usb_cx2.fifo[fifo].size], packet, size);

    /* Hack ahead! Counterpart to the receiving side in usblink_cx2.
     * The nspire code has if(size & 0x3F == 0) ++size; for some reason,
     * so send them that way here as well. It's probably to avoid having to
     * deal with zero-length packets.
     * TODO: Move to usblink_cx2.cpp as NNSE specific. */
    if((size & 0x3F) == 0)
    {
        usb_cx2.fifo[fifo].data[usb_cx2.fifo[fifo].size + size] = 0;
        ++size;
    }

    usb_cx2.fifo[fifo].size += size;
    usb_cx2.gisr[1] |= 1 << (fifo * 2); // FIFO OUT IRQ
    auto packet_size = usb_cx2.epout[(ep - 1) & 7] & 0x7ff;
    if(packet_size == 0 || (size % packet_size)) // Last pkt is short?
        usb_cx2.gisr[1] |= 1 << ((fifo * 2) + 1); // FIFO SPK IRQ
    else // ZLP needed
        error("Sending zero-length packets not implemented");

    usb_cx2_int_check();
    return true;
}

static void usb_cx2_queue_packet(bool is_ack, const uint8_t *packet, size_t size)
{
    if(is_ack)
    {
        send_queue_ack.emplace(size);
        memcpy(send_queue_ack.back().data, packet, size);
    }
    else
    {
        send_queue.emplace(size);
        memcpy(send_queue.back().data, packet, size);
    }
}

static bool usb_cx2_send_one_queued(uint8_t ep)
{
    if(!send_queue_ack.empty())
    {
        if(!usb_cx2_real_packet_to_calc(ep, send_queue_ack.front().data, send_queue_ack.front().size))
            return false;
        send_queue_ack.pop();
        return true;
    }
    if(!send_queue.empty())
    {
        if(!usb_cx2_real_packet_to_calc(ep, send_queue.front().data, send_queue.front().size))
            return false;
        send_queue.pop();
        return true;
    }

    return false;
}

bool usb_cx2_packet_to_calc(uint8_t ep, const uint8_t *packet, size_t size)
{
    if(size > sizeof(usb_cx2.fifo[0].data) || size > sizeof(usb_packet::data))
    {
        warn("usb_cx2_packet_to_calc: oversize ep=%u size=%zu", ep, size);
        return false;
    }

    const bool is_ack = usb_cx2_is_ack_packet(packet, size);

    // Keep ACKs ahead of regular payload if retries are pending.
    if(!is_ack && !send_queue_ack.empty())
    {
        usb_cx2_queue_packet(false, packet, size);
        if(!usb_cx2_send_one_queued(ep))
        {
            return false;
        }
        return true;
    }

    // Preserve packet boundaries: if FIFO is busy, queue and send later.
    uint8_t fifo = (usb_cx2.epmap[ep > 4] >> (8 * ((ep - 1) & 0b11) + 4)) & 0b11;
    if(usb_cx2.fifo[fifo].size)
    {
        usb_cx2_queue_packet(is_ack, packet, size);
        return true;
    }

    return usb_cx2_real_packet_to_calc(ep, packet, size);
}

static void usb_cx2_packet_from_calc(uint8_t ep, uint8_t *packet, size_t size)
{
    if(ep != 1)
        error("Got packet on unknown EP");

    if(!usblink_cx2_handle_packet(packet, size))
        warn("Packet not handled");
}

void usb_cx2_reset()
{
    const bool attached = usb_cx2_physical_vbus_present();
    while(!send_queue.empty())
        send_queue.pop();
    while(!send_queue_ack.empty())
        send_queue_ack.pop();

    usb_cx2 = {};
    usb_cx2.usbcmd = 0x80000;
    usb_cx2.portsc = 0xEC000004;
    // All IRQs masked
    usb_cx2.imr = 0xF;
    usb_cx2.otgier = 0;

    // High speed, B-device, acts as device.
    // OTG connection bits must match physical VBUS/cable state.
    usb_cx2.otgcs = usb_cx2_otgcs_idle_value();

    /* Only raise initial reset-related device IRQs when physically attached.
     * If disconnected, these spuriously trigger jungo attach/enable paths. */
    if (attached) {
        usb_cx2.gisr[1] |= 0b1111 << 16;
        usb_cx2.gisr[2] |= 1;
    }

    usb_cx2_int_check();
}

void usb_cx2_bus_reset_on()
{
    if (!usb_cx2_physical_vbus_present()) {
        usb_cx2.portsc &= ~(0x0C000101u);
        usb_cx2.usbsts &= ~4u;
        usb_cx2.otgisr = 0;
        usb_cx2.otgcs = usb_cx2_otgcs_idle_value();
        usb_cx2.gisr[2] &= ~(1u << 9);
        usb_cx2_int_check();
        return;
    }

    usb_cx2.portsc &= ~1;
    usb_cx2.portsc |= 0x0C000100;
    usb_cx2.usbsts |= 0x40;

    usb_cx2.otgisr = (1 << 11) | (1 << 9) | (1 << 8) | (1 << 6);
    usb_cx2.otgcs = (1 << 21) | 1 << 16;

    usb_cx2_int_check();
    //gui_debug_printf("usb reset on\n");
}

void usb_cx2_bus_reset_off()
{
    const bool attached = usb_cx2_physical_vbus_present();
    usb_cx2.otgcs = usb_cx2_otgcs_idle_value();
    usb_cx2.otgisr = attached ? ((1u << 9) | (1u << 8)) : 0u;

    /* Device-idle IRQ should only be raised when physically attached.
     * Raising it while detached triggers guest Jungo notify callbacks
     * (DEVICE_ENABLE/CONNECT) and can make power logic think USB appeared. */
    if (attached)
        usb_cx2.gisr[2] |= (1u << 9);
    else
        usb_cx2.gisr[2] &= ~(1u << 9);

    usb_cx2.portsc &= ~0x0C000100;
    if (attached) {
        usb_cx2.portsc |= 1;
        usb_cx2.usbsts |= 4;
    } else {
        usb_cx2.portsc &= ~1u;
    }
    usb_cx2_int_check();
    //gui_debug_printf("usb reset off\n");
}

void usb_cx2_receive_setup_packet(const usb_setup *packet)
{
    // Copy data into DMA buffer
    static_assert(sizeof(*packet) == sizeof(usb_cx2.setup_packet), "");
    memcpy(usb_cx2.setup_packet, packet, sizeof(usb_cx2.setup_packet));

    // EP0 Setup packet
    usb_cx2.gisr[0] |= 1;

    usb_cx2_int_check();
}

void usb_cx2_fdma_update(int fdma)
{
    if(!(usb_cx2.fdma[fdma].ctrl & 1))
        return; // DMA disabled

    auto mark_dma_complete = [&](bool error) {
        // Hardware clears active transfer state on completion. Keep driver-
        // visible state consistent so write paths don't report 0-byte sends.
        usb_cx2.fdma[fdma].ctrl &= ~1u;               // DMA enable off
        usb_cx2.fdma[fdma].ctrl &= ~(0x1FFFFu << 8);  // residual length = 0
        if (error)
            usb_cx2.dmasr |= 1u << (fdma + 16); // DMA error
        else
            usb_cx2.dmasr |= 1u << fdma; // DMA done
        usb_cx2_int_check();
    };

    bool fromMemory = usb_cx2.fdma[fdma].ctrl & 0b10;
    size_t length = (usb_cx2.fdma[fdma].ctrl >> 8) & 0x1ffff;
    if (length == 0) {
        mark_dma_complete(false);
        return;
    }
    uint8_t *ptr = nullptr;
    ptr = (uint8_t*) phys_mem_ptr(usb_cx2.fdma[fdma].addr, length);
    if (!ptr) {
        warn("USB FDMA: bad mapping fdma=%d addr=%08x len=%zu", fdma, usb_cx2.fdma[fdma].addr, length);
        mark_dma_complete(true);
        return;
    }

    uint8_t ep = 0;
    int fifo = fdma - 1;
    if(fdma > 0)
        ep = (usb_cx2.fifomap >> (fifo * 8)) & 0xF;

    if(fromMemory)
    {
        if(fdma == 0)
            usb_cx2.cxfifo.size = 0;
        else
            usb_cx2.fifo[fifo].size = 0;

        // This is an entire transfer and can be longer than the FIFO
        usb_cx2_packet_from_calc(ep, ptr, length);

        if(fdma > 0) {
            // Signal FIFO IN completion for this FIFO. Jungo write completion
            // waits on device-I/O IRQs in addition to DMA done status.
            usb_cx2.gisr[1] |= 1u << (16 + fifo);
        }
    }
    else
    {
        if(fdma == 0) {
            warn("USB FDMA: reading from EP0 FIFO is unsupported");
            mark_dma_complete(true);
            return;
        }

        if(length > usb_cx2.fifo[fifo].size) {
            warn("Trying to read more bytes than available on fdma%d\n", fdma);
            length = usb_cx2.fifo[fifo].size;
        }

        if (length != 0)
            memcpy(ptr, usb_cx2.fifo[fifo].data, length);

        // Move the remaining data to the start
        usb_cx2.fifo[fifo].size -= length;
        if(usb_cx2.fifo[fifo].size)
        {
            memmove(usb_cx2.fifo[fifo].data, usb_cx2.fifo[fifo].data + length, usb_cx2.fifo[fifo].size);
            usb_cx2_reassert_fifo_data_irq();
        }
        else
        {
            usb_cx2.gisr[1] &= ~(0b11 << (fifo * 2)); // FIFO0 OUT/SPK

            if(ep == 1)
                usb_cx2_send_one_queued(1);
        }
    }

    // Hardware advances DMA address by bytes actually transferred.
    // Guest write/read helpers use this to compute completed byte count.
    usb_cx2.fdma[fdma].addr += (uint32_t)length;

    mark_dma_complete(false);
}

uint32_t usb_cx2_read_word(uint32_t addr)
{
    uint32_t offset = addr & 0xFFF;
    switch(offset)
    {
    case 0x000: // CAPLENGTH + HCIVERSION
        return 0x01000010;
    case 0x004: // HCSPARAMS
        return 0x00000001;
    case 0x008: // HCCPARAMS
        return 0;
    case 0x010: // USBCMD
        return usb_cx2.usbcmd;
    case 0x014: // USBSTS
        return usb_cx2.usbsts;
    case 0x018: // USBINTR
        return usb_cx2.usbintr;
    case 0x030: // PORTSC (earlier than the spec...)
        return usb_cx2.portsc;
    case 0x040:
        return usb_cx2.miscr;
    case 0x080: // OTGCS
        return usb_cx2.otgcs;
    case 0x084:
        return usb_cx2.otgisr;
    case 0x088:
        return usb_cx2.otgier;
    case 0x0C0:
        return usb_cx2.isr;
    case 0x0C4:
        return usb_cx2.imr;
    case 0x100:
        return usb_cx2.devctrl;
    case 0x104:
        return usb_cx2.devaddr;
    case 0x108:
        return usb_cx2.devtest;
    case 0x114:
        return usb_cx2.phytest;
    case 0x120:
    {
        uint32_t value = usb_cx2.cxfifo.size << 24;
        for (int fifo = 0; fifo < 4; fifo++)
            if (!usb_cx2.fifo[fifo].size)
                value |= 1 << (8 + fifo); // FIFOE
        if (!usb_cx2.cxfifo.size)
            value |= 1 << 5; // CX FIFO empty
        if (usb_cx2.cxfifo.size == sizeof(usb_cx2.cxfifo.data))
            value |= 1 << 4; // CX FIFO full
        return value;
    }
    case 0x130:
        return usb_cx2.gimr_all;
    case 0x134: case 0x138:
    case 0x13c:
        return usb_cx2.gimr[(offset - 0x134) >> 2];
    case 0x140:
        return usb_cx2.gisr_all;
    case 0x144: case 0x148:
    case 0x14C:
        return usb_cx2.gisr[(offset - 0x144) >> 2];
    case 0x150:
        return usb_cx2.rxzlp;
    case 0x154:
        return usb_cx2.txzlp;
    case 0x160: case 0x164:
    case 0x168: case 0x16c:
        return usb_cx2.epin[(offset - 0x160) >> 2];
    case 0x180: case 0x184:
    case 0x188: case 0x18c:
        return usb_cx2.epout[(offset - 0x180) >> 2];
    case 0x1A0: // EP 1-4 map register
    case 0x1A4: // EP 5-8 map register
        return usb_cx2.epmap[(offset - 0x1A0) >> 2];
    case 0x1A8: // FIFO map register
        return usb_cx2.fifomap;
    case 0x1AC: // FIFO config register
        return usb_cx2.fifocfg;
    case 0x1B0: // FIFO 0 status
    case 0x1B4: // FIFO 1 status
    case 0x1B8: // FIFO 2 status
    case 0x1BC: // FIFO 3 status
        return usb_cx2.fifo[(offset - 0x1B0) >> 2].size;
    case 0x1C0:
        return usb_cx2.dmafifo;
    case 0x1C8:
        return usb_cx2.dmactrl;
    case 0x1D0: // CX FIFO PIO register
    {
        uint32_t ret = usb_cx2.setup_packet[0];
        usb_cx2.setup_packet[0] = usb_cx2.setup_packet[1];
        return ret;
    }
    case 0x300:
    case 0x308:
    case 0x310:
    case 0x318:
    case 0x320:
        return usb_cx2.fdma[(offset - 0x300) / 8].ctrl;
    case 0x304:
    case 0x30c:
    case 0x314:
    case 0x31c:
    case 0x324:
        return usb_cx2.fdma[(offset - 0x304) / 8].addr;
    case 0x328:
        return usb_cx2.dmasr;
    case 0x32C:
        return usb_cx2.dmamr;
    case 0x330:
        return 0; // ?
    }
    return bad_read_word(addr);
}

void usb_cx2_write_word(uint32_t addr, uint32_t value)
{
    uint32_t offset = addr & 0xFFF;
    switch(offset)
    {
    case 0x000: // CAPLENGTH + HCIVERSION
        // This is actually read-only, but the OS seems to write a 2 in here.
        // I guess it should've been a write to 0x010 instead.
        return;
    case 0x010: // USBCMD
        if (value & 2) {
            // Reset USB HOST stuff, ignored
            return;
        }
        usb_cx2.usbcmd = value;
        return;
    case 0x014: // USBSTS
        usb_cx2.usbsts &= ~(value & 0x3F);
        usb_cx2_int_check();
        return;
    case 0x018: // USBINTR
        usb_cx2.usbintr = value & 0x030101D7;
        usb_cx2_int_check();
        return;
    case 0x01C: // FRINDEX
    case 0x020: // CTRLDSSEGMENT
    case 0x024: // PERIODICLISTBASE
    case 0x028: // ASYNCLISTADDR
        return; // USB HOST stuff, just ignore
    case 0x040:
        usb_cx2.miscr = value;
        return;
    case 0x080: // OTGCS
        if (!usb_cx2_physical_vbus_present()) {
            /* Keep detached OTG state stable while unplugged.
             * Guest writes here during init; do not let them synthesize
             * attach/session transitions without physical VBUS. */
            usb_cx2.otgcs = usb_cx2_otgcs_idle_value();
            return;
        }
        usb_cx2.otgcs = value;
        return;
    case 0x084: // OTGISR
        usb_cx2.otgisr &= ~value;
        usb_cx2_int_check();
        return;
    case 0x088:
        usb_cx2.otgier = value;
        usb_cx2_int_check();
        return;
    case 0x0C0:
        usb_cx2.isr &= ~value;
        usb_cx2_int_check();
        return;
    case 0x0C4:
        usb_cx2.imr = value & 0b111;
        usb_cx2_int_check();
        return;
    case 0x100:
        usb_cx2.devctrl = value;
        usb_cx2_int_check();
        return;
    case 0x104:
        usb_cx2.devaddr = value;

        /*if(value == 0x81) // Configuration set
            gui_debug_printf("Completed SET_CONFIGURATION\n");*/

        return;
    case 0x108:
        usb_cx2.devtest = value;
        return;
    case 0x110: // SOF mask timer
        return;
    case 0x114:
        usb_cx2.phytest = value;
        return;
    case 0x120:
    {
        if (value & 0b1000) // Clear CX FIFO
            usb_cx2.cxfifo.size = 0;

        if (value & 0b0100) // Stall CX FIFO
            error("control endpoint stall");

        if (value & 0b0010) // Test transfer finished
            error("test transfer finished");

        if (value & 0b0001) // Setup transfer finished
        {
            // Clear EP0 OUT/IN/SETUP packet IRQ
            usb_cx2.gisr[0] &= ~0b111;
            usb_cx2_int_check();

            if (usb_cx2.devaddr == 1)
            {
                struct usb_setup packet = { 0, 9, 1, 0, 0 };
                usb_cx2_receive_setup_packet(&packet);
            }
        }

        return;
    }
    case 0x124: // IDLE counter
        return;
    case 0x130: // GIMR_ALL
        usb_cx2.gimr_all = value & 0b1111;
        usb_cx2_int_check();
        return;
    case 0x134: case 0x138:
    case 0x13c:
        usb_cx2.gimr[(offset - 0x134) >> 2] = value;
        usb_cx2_int_check();
        return;
    case 0x144: case 0x148:
    case 0x14c:
        usb_cx2.gisr[(offset - 0x144) >> 2] &= ~value;
        usb_cx2_reassert_fifo_data_irq();
        usb_cx2_int_check();
        return;
    case 0x150: // Rx zero length pkt
        usb_cx2.rxzlp = value;
        if(value)
            error("Not implemented");
        usb_cx2_int_check();
        return;
    case 0x154: // Tx zero length pkt
        usb_cx2.txzlp = value;
        if(value)
            error("Not implemented");
        usb_cx2_int_check();
        return;
    case 0x160: case 0x164:
    case 0x168: case 0x16c:
        usb_cx2.epin[(offset - 0x160) >> 2] = value;
        return;
    case 0x180: case 0x184:
    case 0x188: case 0x18c:
        usb_cx2.epout[(offset - 0x180) >> 2] = value;
        return;
    case 0x1A0: // EP 1-4 map register
    case 0x1A4: // EP 5-8 map register
        usb_cx2.epmap[(offset - 0x1A0) >> 2] = value;
        return;
    case 0x1A8: // FIFO map register
        usb_cx2.fifomap = value;
        return;
    case 0x1AC: // FIFO config register
        usb_cx2.fifocfg = value;
        return;
    case 0x1B0: // FIFO 0 status
    case 0x1B4: // FIFO 1 status
    case 0x1B8: // FIFO 2 status
    case 0x1BC: // FIFO 3 status
        if(value & (1 << 12))
            usb_cx2.fifo[(offset - 0x1B0) >> 2].size = 0;
        return;
    case 0x1C0:
        usb_cx2.dmafifo = value;
        if(value != 0 && value != 0x10)
            error("Not implemented");
        return;
    case 0x1C8:
        usb_cx2.dmactrl = value;
        if(value != 0)
            error("Not implemented");
        return;
    case 0x300:
    case 0x308:
    case 0x310:
    case 0x318:
    case 0x320:
        usb_cx2.fdma[(offset - 0x300) / 8].ctrl = value;
        usb_cx2_fdma_update((offset - 0x300) / 8);
        return;
    case 0x304:
    case 0x30c:
    case 0x314:
    case 0x31c:
    case 0x324:
        usb_cx2.fdma[(offset - 0x304) / 8].addr = value;
        return;
    case 0x328:
        usb_cx2.dmasr &= ~value;
        usb_cx2_int_check();
        return;
    case 0x32c:
        usb_cx2.dmamr = value;
        usb_cx2_int_check();
        return;
    case 0x330:
        // No idea
        return;
    }
    bad_write_word(addr, value);
}
