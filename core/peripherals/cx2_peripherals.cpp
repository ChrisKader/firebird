#include "emu.h"

#include "memory/mem.h"
#include "soc/cx2.h"
#include "peripherals/misc.h"

static int cx2_peripheral_clamp_int(int value, int min, int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

/* 90130000: ??? for LCD backlight */
static struct cx2_backlight_state cx2_backlight;

static uint8_t cx2_backlight_contrast_from_pwm(void)
{
	/* Per Hackspire: period=255, value 0 (brightest) to 225 (darkest). */
	if (cx2_backlight.pwm_period == 0)
		return 0;
	int contrast = LCD_CONTRAST_MAX
		- (int)((cx2_backlight.pwm_value * LCD_CONTRAST_MAX) / cx2_backlight.pwm_period);
	return (uint8_t)cx2_peripheral_clamp_int(contrast, 0, LCD_CONTRAST_MAX);
}

void cx2_backlight_refresh_lcd_contrast(void)
{
	hdq1w.lcd_contrast = cx2_backlight_contrast_from_pwm();
}

void cx2_backlight_reset()
{
	/* Default to brightest setting on cold boot. */
	cx2_backlight.pwm_period = 255;
	cx2_backlight.pwm_value = 0;
	const int16_t lcd_override = hw_override_get_lcd_contrast();
	if (lcd_override >= 0)
		hdq1w.lcd_contrast = (uint8_t)cx2_peripheral_clamp_int(lcd_override, 0, LCD_CONTRAST_MAX);
	else
		hdq1w.lcd_contrast = LCD_CONTRAST_MAX;
}

uint32_t cx2_backlight_read(uint32_t addr)
{
	switch (addr & 0xFFF)
	{
	case 0x014: return cx2_backlight.pwm_value;
	case 0x018: return cx2_backlight.pwm_period;
	case 0x020: return 0;
	}
	return bad_read_word(addr);
}

void cx2_backlight_write(uint32_t addr, uint32_t value)
{
	auto offset = addr & 0xFFF;
	if(offset == 0x014)
		cx2_backlight.pwm_value = value;
	else if(offset == 0x018)
		cx2_backlight.pwm_period = value;
	else if(offset != 0x020)
		bad_write_word(addr, value);

	/* Mirror PWM duty cycle to the rendered LCD brightness unless GUI override
	 * is active. */
	if(hw_override_get_lcd_contrast() < 0)
		cx2_backlight_refresh_lcd_contrast();
}

/* 90040000: FTSSP010 SPI controller connected to the LCD panel.
 *
 * Register layout (as used by CX II firmware):
 *   +0x00  CR0    Control register 0 (bits[3:0] = frame_size - 1)
 *   +0x04  CR1    Control register 1 (bit 1 = SSP enable)
 *   +0x08/+0x18 DATA   TX/RX data register (full-duplex FIFO)
 *   +0x0C  STATUS Bit1=TX not full, Bit2=RX not empty, Bit4=Busy
 *
 * The LCD panel responds to MIPI DCS read commands over 9-bit SPI:
 *   0xDA -> 0x06 (Display ID1)    \  Together these identify
 *   0xDB -> 0x85 (Display ID2)    /  "GP IPS" panel (index 0xD)
 * Response is encoded in 9-bit frame as (byte << 1).
 */
static struct cx2_lcd_spi_state cx2_lcd_spi;
static uint32_t lcd_spi_cr0;
static uint32_t lcd_spi_cr1;
static uint8_t  lcd_spi_last_cmd;

static uint32_t lcd_spi_rx_fifo[16];
static int lcd_spi_rx_head;
static int lcd_spi_rx_count;
static uint16_t lcd_spi_pending_words[4];
static int lcd_spi_pending_len;
static int lcd_spi_pending_pos;

void cx2_lcd_spi_reset(void)
{
	cx2_lcd_spi.busy = false;
	lcd_spi_cr0 = 0;
	lcd_spi_cr1 = 0;
	lcd_spi_last_cmd = 0;
	lcd_spi_rx_head = 0;
	lcd_spi_rx_count = 0;
	lcd_spi_pending_len = 0;
	lcd_spi_pending_pos = 0;
}

static uint8_t lcd_spi_panel_response_byte(uint8_t cmd)
{
	switch (cmd) {
	case 0xDA: return 0x06; /* Display ID1 */
	case 0xDB: return 0x85; /* Display ID2 */
	case 0xDC: return 0x4A; /* Display ID3 */
	default:   return 0x00;
	}
}

static bool lcd_spi_extract_id_cmd(uint16_t frame, uint8_t *out_cmd)
{
	/* 9-bit SPI frame: bit8 is D/C (0=command, 1=data). */
	if (frame & 0x100u)
		return false;

	uint8_t raw = (uint8_t)(frame & 0xFFu);
	uint8_t shifted = (uint8_t)((frame >> 1) & 0xFFu);
	if (raw == 0x04 || (raw >= 0xDA && raw <= 0xDF)) {
		*out_cmd = raw;
		return true;
	}
	if (shifted == 0x04 || (shifted >= 0xDA && shifted <= 0xDF)) {
		*out_cmd = shifted;
		return true;
	}
	return false;
}

static void lcd_spi_prepare_id_response(uint8_t cmd)
{
	lcd_spi_pending_len = 0;
	lcd_spi_pending_pos = 0;

	/* Read Display ID command returns 3 bytes on this panel family.
	 * The bootloader unpacks 9-bit words with overlapping bit windows.
	 * These packed words decode to 06,85,4A ("GP IPS", index 0xD). */
	if (cmd == 0x04) {
		/* One leading dummy keeps alignment with the bootloader's RX priming
		 * behavior before it decodes bytes from the transfer buffer. */
		lcd_spi_pending_words[0] = 0x000;
		lcd_spi_pending_words[1] = 0x006;
		lcd_spi_pending_words[2] = 0x10A;
		lcd_spi_pending_words[3] = 0x128;
		lcd_spi_pending_len = 4;
		return;
	}

	if (cmd >= 0xDA && cmd <= 0xDC) {
		/* Single-byte reads go through a different path (value >> 1). */
		lcd_spi_pending_words[0] = (uint16_t)lcd_spi_panel_response_byte(cmd) << 1;
		lcd_spi_pending_len = 1;
	}
}

static void lcd_spi_rx_push(uint32_t value)
{
	if (lcd_spi_rx_count < 16) {
		int tail = (lcd_spi_rx_head + lcd_spi_rx_count) % 16;
		lcd_spi_rx_fifo[tail] = value;
		lcd_spi_rx_count++;
	}
}

static void lcd_spi_rx_clear(void)
{
	lcd_spi_rx_head = 0;
	lcd_spi_rx_count = 0;
}

static uint32_t lcd_spi_rx_pop(void)
{
	if (lcd_spi_rx_count > 0) {
		uint32_t data = lcd_spi_rx_fifo[lcd_spi_rx_head];
		lcd_spi_rx_head = (lcd_spi_rx_head + 1) % 16;
		lcd_spi_rx_count--;
		return data;
	}
	return 0;
}

uint32_t cx2_lcd_spi_read(uint32_t addr)
{
	uint32_t offset = addr & 0xFFF;
	switch (offset)
	{
	case 0x00: return lcd_spi_cr0;
	case 0x04: return lcd_spi_cr1;
	case 0x08:
	case 0x18:
		return lcd_spi_rx_pop();
	case 0x0C:
	{
		/* FTSSP010 transfer loop in bootloader:
		 *   - checks bit1 (0x2) before TX writes
		 *   - checks bits[9:4] (0x3F0) before RX reads
		 * Expose RX availability only when FIFO actually has data. */
		uint32_t rx_level = (uint32_t)(lcd_spi_rx_count & 0x3F) << 4;
		uint32_t status = 0x02u | rx_level;
		cx2_lcd_spi.busy = false;
		return status;
	}
	default:
		return 0;
	}
}

void cx2_lcd_spi_write(uint32_t addr, uint32_t value)
{
	uint32_t offset = addr & 0xFFF;
	switch (offset)
	{
	case 0x00: lcd_spi_cr0 = value; break;
	case 0x04: lcd_spi_cr1 = value; break;
	case 0x08:
	case 0x18:
	{
		/* Each TX write clocks one 9-bit full-duplex SPI frame.
		 * D/C is bit8, payload is bits[7:0]. Panel-ID probes are DCS
		 * read commands sent with D/C=0, followed by a data phase where
		 * the panel returns one byte. */
		uint16_t frame = value & 0x1FF;
		uint8_t cmd = 0;
		uint16_t response_word = 0;

		if (lcd_spi_extract_id_cmd(frame, &cmd)) {
			lcd_spi_last_cmd = cmd;
			/* Drop stale full-duplex garbage from prior non-read traffic so
			 * ID decode consumes only this command's response stream. */
			lcd_spi_rx_clear();
			lcd_spi_prepare_id_response(cmd);
			/* Command phase clocks in a dummy word (response_word = 0). */
		} else if (lcd_spi_pending_pos < lcd_spi_pending_len) {
			response_word = lcd_spi_pending_words[lcd_spi_pending_pos++];
		}
		/* Full-duplex: every TX frame produces one RX frame. */
		lcd_spi_rx_push(response_word);
		cx2_lcd_spi.busy = true;
		break;
	}
	default:
		break;
	}
}

/* BC000000: An FTDMAC020 */
static dma_state dma;

void dma_cx2_reset()
{
	memset(&dma, 0, sizeof(dma));
}

enum class DMAMemDir {
	INC=0,
	DEC=1,
	FIX=2
};

static void dma_cx2_update()
{
	if(!(dma.csr & 1)) // Enabled?
		return;

	if(dma.csr & 0b110) // Big-endian?
		return;

	for(auto &channel : dma.channels)
	{
		if(!(channel.control & 1)) // Start?
			continue;

		if((channel.control & 0b110) != 0b110) { // AHB1?
			warn("DMA: unsupported bus config 0x%x", channel.control);
			channel.control &= ~1;
			continue;
		}

		if(channel.control & (1 << 15)) { // Abort
			channel.control &= ~((1 << 15) | 1);
			continue;
		}

		auto dstdir = DMAMemDir((channel.control >> 3) & 3),
			 srcdir = DMAMemDir((channel.control >> 5) & 3);

		if(srcdir != DMAMemDir::INC || dstdir != DMAMemDir::INC) {
			warn("DMA: unsupported direction src=%d dst=%d", (int)srcdir, (int)dstdir);
			channel.control &= ~1;
			continue;
		}

		auto dstwidth = (channel.control >> 8) & 7,
			 srcwidth = (channel.control >> 11) & 7;

		if(dstwidth != srcwidth || dstwidth > 2) {
			warn("DMA: unsupported width src=%d dst=%d", srcwidth, dstwidth);
			channel.control &= ~1;
			continue;
		}

		// Convert to bytes
		srcwidth = 1 << srcwidth;

		size_t total_len = channel.len * srcwidth;

		void *srcp = phys_mem_ptr(channel.src, total_len),
			 *dstp = phys_mem_ptr(channel.dest, total_len);

		if(!srcp || !dstp) {
			warn("DMA: invalid transfer src=%08x dst=%08x len=%zu", channel.src, channel.dest, total_len);
			channel.control &= ~1;
			continue;
		}

		/* Doesn't trigger any read or write actions, but on HW
		 * special care has to be taken anyway regarding caches
		 * and so on, so should be fine. */
		memcpy(dstp, srcp, total_len);

		channel.control &= ~1; // Clear start bit
	}
}

uint32_t dma_cx2_read_word(uint32_t addr)
{
	switch (addr & 0x3FFFFFF) {
		case 0x00C: return 0;
		case 0x01C: return 0;
		case 0x024: return dma.csr;
		case 0x100: return dma.channels[0].control;
		case 0x104: return dma.channels[0].config;
	}
	return bad_read_word(addr);
}

void dma_cx2_write_word(uint32_t addr, uint32_t value)
{
	switch (addr & 0x3FFFFFF) {
		case 0x024: dma.csr = value; return;
		case 0x100:
			dma.channels[0].control = value;
			dma_cx2_update();
			return;
		case 0x104: dma.channels[0].config = value; return;
		case 0x108: dma.channels[0].src = value; return;
		case 0x10C: dma.channels[0].dest = value; return;
		case 0x114: dma.channels[0].len = value & 0x003fffff; return;
	}
	bad_write_word(addr, value);
}

bool cx2_peripherals_suspend(emu_snapshot *snapshot)
{
	return snapshot_write(snapshot, &cx2_backlight, sizeof(cx2_backlight))
		&& snapshot_write(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
		&& snapshot_write(snapshot, &dma, sizeof(dma));
}

bool cx2_peripherals_resume(const emu_snapshot *snapshot)
{
	return snapshot_read(snapshot, &cx2_backlight, sizeof(cx2_backlight))
		&& snapshot_read(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
		&& snapshot_read(snapshot, &dma, sizeof(dma));
}
