#include "emu.h"

#include "mem.h"
#include "cx2.h"
#include "misc.h"
#include "schedule.h"
#include "interrupt.h"
#include "keypad.h"

/* 90140000 */
struct aladdin_pmu_state aladdin_pmu;
static struct {
	uint32_t reg[0x100 / sizeof(uint32_t)];
} tg2989_pmic;

enum {
	TG2989_PMIC_REG_ID_STATUS = 0x04u,
	TG2989_PMIC_ID_READY_BIT = 0x00000001u,
	TG2989_PMIC_ID_MODEL_SHIFT = 20,
	TG2989_PMIC_ID_MODEL_MASK = 0x01F00000u,
	TG2989_PMIC_ID_MODEL_TG2985 = 1u,
	TG2989_PMIC_ID_VARIANT_SIGN = 0x80000000u,
};

static uint32_t tg2989_pmic_id_status_value(void)
{
	/* DIAGS reads 0x90100004 and decodes:
	 *   bits[24:20] -> PMIC model bucket (0/2 => TG2989, 1 => TG2985)
	 *   bit31 sign  -> variant suffix selection.
	 * For our CX II target image, a non-negative value yields "...E".
	 *   bit0        -> "ready" polling bit.
	 * Default to TG2985E + ready.
	 */
	return TG2989_PMIC_ID_READY_BIT
		| (TG2989_PMIC_ID_MODEL_TG2985 << TG2989_PMIC_ID_MODEL_SHIFT);
}

// Wakeup reason at PMU+0x00. Value 0x040000 = wakeupOnKey.
static uint32_t wakeup_reason = 0x040000;
// PMU+0x08: firmware performs ~100 R/W cycles; preserve writes.
// Not in the snapshot struct to avoid breaking snapshot compatibility.
static uint32_t aladdin_pmu_ctrl_08 = 0x2000;
enum {
	PMU_IRQ_MASK_INDEX = 0x50 >> 2,   // PMU+0x850
	PMU_IRQ_PEND_INDEX = 0x54 >> 2,   // PMU+0x854
	PMU_IRQ_ADC_BIT = 0x08000000u,
};

void aladdin_pmu_set_wakeup_reason(uint32_t reason) {
	wakeup_reason = reason;
}

void aladdin_pmu_set_adc_pending(bool on) {
	if (on) {
		aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] |= PMU_IRQ_ADC_BIT;
		int_set(INT_POWER, true);
	}
}

void aladdin_pmu_reset(void) {
	memset(&aladdin_pmu, 0, sizeof(aladdin_pmu));
	aladdin_pmu.clocks = 0x21020303;
	wakeup_reason = 0x040000;
	aladdin_pmu_ctrl_08 = 0x2000;
	aladdin_pmu.disable[0] = 0;
	aladdin_pmu.noidea[0] = 0x1A;
	/* Keep PMU status free of low-power sticky flags at reset. */
	aladdin_pmu.noidea[1] = 0x1;
	/* Observed reads from 0x9014080C expect this bit high. */
	aladdin_pmu.noidea[3] = 0x00100000;
	// Special cases for 0x808-0x810, see aladdin_pmu_read
	//aladdin_pmu.noidea[4] = 0x111;
	aladdin_pmu.noidea[5] = 0x1;
	aladdin_pmu.noidea[6] = 0x100;
	aladdin_pmu.noidea[7] = 0x10;
	aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX] = 0xFFFFFFFFu;
	aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] = 0;

	uint32_t cpu = 396000000;
	sched.clock_rates[CLOCK_CPU] = cpu;
	sched.clock_rates[CLOCK_AHB] = cpu / 2;
	sched.clock_rates[CLOCK_APB] = cpu / 4;
}

/* 90100000: TG2989 PMIC (minimal model for DIAGS/boot polling) */
void tg2989_pmic_reset(void)
{
	memset(&tg2989_pmic, 0, sizeof(tg2989_pmic));
	/* 0x04 is the PMIC ID/status word used by DIAGS and early boot code. */
	tg2989_pmic.reg[TG2989_PMIC_REG_ID_STATUS >> 2] = tg2989_pmic_id_status_value();
}

uint32_t tg2989_pmic_read(uint32_t addr)
{
	uint32_t offset = addr & 0xFFFFu;
	if (offset == TG2989_PMIC_REG_ID_STATUS)
		return tg2989_pmic_id_status_value();
	if (offset < 0x100u)
		return tg2989_pmic.reg[offset >> 2];
	return bad_read_word(addr);
}

void tg2989_pmic_write(uint32_t addr, uint32_t value)
{
	uint32_t offset = addr & 0xFFFFu;
	if (offset < 0x100u) {
		tg2989_pmic.reg[offset >> 2] = value;
		if (offset == TG2989_PMIC_REG_ID_STATUS) {
			/* Keep identity bits stable while still letting firmware store
			 * any scratch/status bits in the remaining fields.
			 */
			const uint32_t fixed = TG2989_PMIC_ID_READY_BIT
				| TG2989_PMIC_ID_MODEL_MASK
				| TG2989_PMIC_ID_VARIANT_SIGN;
			uint32_t dynamic = tg2989_pmic.reg[offset >> 2] & ~fixed;
			tg2989_pmic.reg[offset >> 2] = dynamic | tg2989_pmic_id_status_value();
		}
		return;
	}
	bad_write_word(addr, value);
}

static int cx2_clamp_int(int value, int min, int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static void aladdin_pmu_update_int()
{
	uint32_t pending = aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX];
	if (intr.active & ((1u << INT_ADC) | (1u << 13)))
		pending |= PMU_IRQ_ADC_BIT;
	bool on = (aladdin_pmu.int_state != 0)
		|| ((pending & aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX]) != 0);
	int_set(INT_POWER, on);
}


static uint32_t aladdin_pmu_disable2_read_value()
{
	uint32_t value = aladdin_pmu.disable[2];
	charger_state_t charger = cx2_effective_charger_state();
	/* PMU+0x60 packs a 10-bit battery field in [15:6]. */
	uint32_t batt_field = adc_cx2_effective_battery_code() & 0x03FFu;
	value &= ~0x0000FFC0u;
	value |= batt_field << 6;

	/* Keep charger state deterministic from the override/live USB model while
	 * preserving firmware-owned bits outside the explicit charger field. */
	value &= ~0x00030000u;
	switch (charger) {
	case CHARGER_CONNECTED_NOT_CHARGING:
		value |= 0x00010000u;
		break;
	case CHARGER_CHARGING:
		value |= 0x00030000u;
		break;
	default:
		break;
	}
	return value;
}

static uint32_t aladdin_pmu_disable0_read_value()
{
	uint32_t value = aladdin_pmu.disable[0];
	/* CX II UI paths treat 0x400 as battery-present and 0x100 as external
	 * source present. Setting only 0x100 while disconnected leads to
	 * "--%" in status while still showing a charge icon on home. */
	value |= 0x00000400u;
	if (cx2_effective_charger_state() == CHARGER_DISCONNECTED)
		value &= ~0x00000100u;
	else
		value |= 0x00000100u;
	return value;
}

static uint32_t aladdin_pmu_usb_phy_status_read_value()
{
	if (cx2_effective_charger_state() == CHARGER_DISCONNECTED)
		return 0;

	/* PHY is ready when enabled via PMU+0x20 bit 10 (0x400). */
	return (aladdin_pmu.disable[0] & 0x400) ? 0x3C : 0;
}

uint32_t aladdin_pmu_read(uint32_t addr)
{
	uint32_t offset = addr & 0xFFFF;
	if(offset < 0x100)
	{
		switch (addr & 0xFF)
		{
		case 0x00: return wakeup_reason;
		case 0x04: return 0;
		case 0x08: return aladdin_pmu_ctrl_08;
		case 0x20: return aladdin_pmu_disable0_read_value();
		case 0x24: return aladdin_pmu.int_state;
		case 0x30: return aladdin_pmu.clocks;
		case 0x50: return aladdin_pmu.disable[1];
		case 0x60: return aladdin_pmu_disable2_read_value();
		case 0xC4: return aladdin_pmu.int_enable;
		}
	}
	else if(offset >= 0x800 && offset < 0x900)
	{
		if(offset == 0x808)
			return 0x010C9231; //0x0021DB19; // efuse
		else if(offset == 0x80C)
			return asic_user_flags << 20;
		else if(offset == 0x810)
		{
			adc_cx2_background_step();
			return 0x11 | ((keypad.key_map[0] & 1<<9) ? 0 : 0x100);
		}
		else if(offset == 0x850)
			return aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX];
		else if(offset == 0x854)
		{
			adc_cx2_background_step();
			uint32_t pending = aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX];
			if (intr.active & ((1u << INT_ADC) | (1u << 13)))
				pending |= PMU_IRQ_ADC_BIT;
			return pending & aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX];
		}
		else if(offset == 0x858)
		{
			adc_cx2_background_step();
			return aladdin_pmu_usb_phy_status_read_value();
		}
		else
			return aladdin_pmu.noidea[(offset & 0xFF) >> 2];
	}

	return bad_read_word(addr);
}

void aladdin_pmu_write(uint32_t addr, uint32_t value)
{
	uint32_t offset = addr & 0xFFFF;
	if(offset < 0x100)
	{
		switch (offset & 0xFF)
		{
		case 0x00: return;
		case 0x04: return; // No idea
		case 0x08: aladdin_pmu_ctrl_08 = value; return;
		case 0x20:
			if(value & 2)
			{
				/* enter sleep, jump to 0 when On pressed. */
				cpu_events |= EVENT_SLEEP;
				event_clear(SCHED_TIMERS);
				event_clear(SCHED_TIMER_FAST);
				// Reset clocks but preserve int_enable for wake-on-key
				uint32_t saved_int_enable = aladdin_pmu.int_enable;
				aladdin_pmu_reset();
				aladdin_pmu.int_enable = saved_int_enable;
			}
			else
				aladdin_pmu.disable[0] = value;

			return;
		case 0x24:
			aladdin_pmu.int_state &= ~value;
			aladdin_pmu_update_int();
			return;
		case 0x30:
		{
			aladdin_pmu.clocks = value;
			/* Recalculate clock rates from PLL multiplier.
			 * Reset value 0x21020303: upper byte 0x21 = 33, and 33 * 12 MHz = 396 MHz.
			 * Extract multiplier and compute new rates. */
			uint32_t mult = (value >> 24) & 0x3F;
			if (mult > 0) {
				uint32_t base = mult * 12000000u;
				uint32_t new_rates[3];
				new_rates[CLOCK_CPU] = base;
				new_rates[CLOCK_AHB] = base / 2;
				new_rates[CLOCK_APB] = base / 4;
				sched_set_clocks(3, new_rates);
			}
			aladdin_pmu.int_state |= 1;
			aladdin_pmu_update_int();
			return;
		}
		case 0x50: aladdin_pmu.disable[1] = value; return;
		case 0x60: aladdin_pmu.disable[2] = value; return;
		case 0xC4: aladdin_pmu.int_enable = value; return;
		}
	}
	else if(offset == 0x80C || offset == 0x810)
		return bad_write_word(addr, value);
	else if(offset >= 0x800 && offset < 0x900)
	{
		if (offset == 0x850) {
			aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX] = value;
			return;
		}
		if (offset == 0x854) {
			/* 0xFFFFFFFF is used as a pre-write before read; only zero clears. */
			if (value == 0) {
				aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] &= ~PMU_IRQ_ADC_BIT;
				aladdin_pmu_update_int();
			}
			return;
		}
		aladdin_pmu.noidea[(offset & 0xFF) >> 2] = value;
		return;
	}

	bad_write_word(addr, value);
}

/* 90120000: FTDDR3030 DDR memory controller */
static bool ddr_initialized = false;

void memc_ddr_reset(void)
{
	ddr_initialized = false;
}

uint32_t memc_ddr_read(uint32_t addr)
{
	switch(addr & 0xFFFF)
	{
	case 0x04:
		// Return 0 if not initialized yet, 0x102 after initialization
		return ddr_initialized ? 0x102 : 0;
	case 0x10:
		return 3; // Size
	case 0x28:
		return 0;
	case 0x74:
		return 0;
	}
	return bad_read_word(addr);
}

void memc_ddr_write(uint32_t addr, uint32_t value)
{
	uint16_t offset = addr;
	if(offset < 0x40) {
		// Config data write - mark DDR as initialized
		ddr_initialized = true;
		return;
	}

	switch(addr & 0xFFFF)
	{
	case 0x074:
	case 0x0A8:
	case 0x0AC:
	case 0x138:
		return;
	}
	bad_write_word(addr, value);
}

/* 90130000: ??? for LCD backlight */
static struct cx2_backlight_state cx2_backlight;

void cx2_backlight_reset()
{
	/* Default to brightest setting on cold boot. */
	cx2_backlight.pwm_period = 255;
	cx2_backlight.pwm_value = 0;
	if (lcd_contrast_override >= 0)
		hdq1w.lcd_contrast = (uint8_t)cx2_clamp_int(lcd_contrast_override, 0, LCD_CONTRAST_MAX);
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

	// Mirror the PWM duty cycle to hdq1w.lcd_contrast (unless GUI override is active).
	// Per Hackspire: period=255, value 0 (brightest) to 225 (darkest).
	// Map to [0, LCD_CONTRAST_MAX] range for rendering.
	if(lcd_contrast_override < 0) {
		if(cx2_backlight.pwm_period == 0)
			hdq1w.lcd_contrast = 0;
		else
			hdq1w.lcd_contrast = LCD_CONTRAST_MAX - (cx2_backlight.pwm_value * LCD_CONTRAST_MAX) / cx2_backlight.pwm_period;
	}
}

/* 90040000: FTSSP010 SPI controller connected to the LCD.
   Only the bare minimum to get the OS to boot is implemented. */
struct cx2_lcd_spi_state cx2_lcd_spi;

uint32_t cx2_lcd_spi_read(uint32_t addr)
{
	switch (addr & 0xFFF)
	{
	case 0x0C: // REG_SR
	{
		uint32_t ret = 0x10 | (cx2_lcd_spi.busy ? 0x04 : 0x02);
		cx2_lcd_spi.busy = false;
		return ret;
	}
	default:
		// Ignored.
		return 0;
	}
}

void cx2_lcd_spi_write(uint32_t addr, uint32_t value)
{
	(void) value;

	switch (addr & 0xFF)
	{
	case 0x18: // REG_DR
		cx2_lcd_spi.busy = true;
		break;
	default:
		// Ignored
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

bool cx2_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
            && snapshot_write(snapshot, &cx2_backlight, sizeof(cx2_backlight))
            && snapshot_write(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
            && snapshot_write(snapshot, &dma, sizeof(dma));
}

bool cx2_resume(const emu_snapshot *snapshot)
{
    return snapshot_read(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
            && snapshot_read(snapshot, &cx2_backlight, sizeof(cx2_backlight))
            && snapshot_read(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
            && snapshot_read(snapshot, &dma, sizeof(dma));
}
