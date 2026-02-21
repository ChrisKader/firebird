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
	TG2989_PMIC_REG_PWR_STATUS0 = 0x08u,
	TG2989_PMIC_REG_PWR_STATUS1 = 0x0Cu,
	TG2989_PMIC_REG_PWR_STATUS2 = 0x10u,
	TG2989_PMIC_REG_PWR_MODE = 0x30u,
	TG2989_PMIC_REG_PWR_FLAGS = 0x48u,
	TG2989_PMIC_ID_READY_BIT = 0x00000001u,
	TG2989_PMIC_ID_MODEL_SHIFT = 20,
	TG2989_PMIC_ID_MODEL_MASK = 0x01F00000u,
	TG2989_PMIC_ID_MODEL_TG2985 = 1u,
	TG2989_PMIC_ID_VARIANT_SIGN = 0x80000000u,
	TG2989_PMIC_PWR_STATUS0_BATT = 0x10044300u,
	TG2989_PMIC_PWR_STATUS0_USB = 0x10044F00u,
	TG2989_PMIC_PWR_MODE_BATT = 0x21020303u,
	TG2989_PMIC_PWR_MODE_USB = 0x18020303u,
	TG2989_PMIC_PWR_FLAGS_BATT = 0x00000003u,
	TG2989_PMIC_PWR_FLAGS_USB = 0x0000000Fu,
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

static bool tg2989_external_power_present(void)
{
	return cx2_external_power_present();
}

static void tg2989_pmic_refresh_power_status(void)
{
	static int last_ext_present = -1;
	const bool ext_present = tg2989_external_power_present();
	tg2989_pmic.reg[TG2989_PMIC_REG_PWR_STATUS0 >> 2] =
		ext_present ? TG2989_PMIC_PWR_STATUS0_USB : TG2989_PMIC_PWR_STATUS0_BATT;
	tg2989_pmic.reg[TG2989_PMIC_REG_PWR_MODE >> 2] =
		ext_present ? TG2989_PMIC_PWR_MODE_USB : TG2989_PMIC_PWR_MODE_BATT;
	tg2989_pmic.reg[TG2989_PMIC_REG_PWR_FLAGS >> 2] =
		ext_present ? TG2989_PMIC_PWR_FLAGS_USB : TG2989_PMIC_PWR_FLAGS_BATT;

	if (last_ext_present != (int)ext_present) {
		last_ext_present = ext_present ? 1 : 0;
		cx2_power_rails_t rails = {};
		cx2_get_power_rails(&rails);
		fprintf(stderr,
			"[FBDBG] PMIC ext_present=%d vbus_mv=%d vsled_mv=%d battery_mv=%d present=%d charger_state=%d\n",
			last_ext_present,
			rails.vbus_mv,
			rails.vsled_mv,
			rails.battery_mv,
			rails.battery_present ? 1 : 0,
			(int)rails.charger_state);
		fflush(stderr);
	}
}

/* PMU+0x00 is not read-only wakeup state: TI-Nspire uses it as a live
 * bitfield in command handlers (for example 0x3EF/0x3F0 paths). Keep an
 * initial wakeup-on-key value, but allow firmware read/write ownership.
 */
static uint32_t wakeup_reason = 0x040000;
/* PMU+0x04 is written by PMU helper paths (mirror/status scratch). */
static uint32_t aladdin_pmu_reg_04 = 0;
// PMU+0x08: firmware performs ~100 R/W cycles; preserve writes.
// Not in the snapshot struct to avoid breaking snapshot compatibility.
static uint32_t aladdin_pmu_ctrl_08 = 0x2000;
enum {
	PMU_IRQ_MASK_INDEX = 0x50 >> 2,   // PMU+0x850
	PMU_IRQ_PEND_INDEX = 0x54 >> 2,   // PMU+0x854
	PMU_IRQ_ONKEY_BIT = 0x00000001u,
	PMU_INT_WAKE_BIT = 0x00000002u,   // PMU+0x24 wake-cause latch bit
	PMU_IRQ_ADC_BIT = 0x08000000u,
};
static void aladdin_pmu_update_int();

static uint32_t aladdin_pmu_pend_with_live_sources(void)
{
	uint32_t pending = aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX];
	if (intr.active & ((1u << INT_ADC) | (1u << 13)))
		pending |= PMU_IRQ_ADC_BIT;
	return pending;
}

void aladdin_pmu_set_wakeup_reason(uint32_t reason) {
	wakeup_reason = reason;
}

void aladdin_pmu_set_adc_pending(bool on) {
	if (on)
		aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] |= PMU_IRQ_ADC_BIT;
	else
		aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] &= ~PMU_IRQ_ADC_BIT;
	aladdin_pmu_update_int();
}

void aladdin_pmu_on_key_wakeup(void)
{
	const bool sleeping = (cpu_events & EVENT_SLEEP) != 0;
	aladdin_pmu_latch_onkey_wake(sleeping);
}

void aladdin_pmu_on_key_release(void)
{
	/* Real PMU wake causes are latched until firmware acknowledges them
	 * through PMU W1C registers. Do not clear on raw key release. */
	aladdin_pmu_update_int();
}

void aladdin_pmu_latch_onkey_wake(bool from_sleep)
{
	/* Keep wake reason in sync with ON-key wake behavior. */
	wakeup_reason = 0x040000;
	/* Latch ON wake in both PMU status paths:
	 * - int_state (PMU+0x24), acknowledged via W1C write to +0x24
	 * - pending bitmap (PMU+0x854), acknowledged via W1C write to +0x854
	 * ROM/OS low-power code polls +0x24 during wake bring-up. */
	aladdin_pmu.int_state |= PMU_INT_WAKE_BIT;
	aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] |= PMU_IRQ_ONKEY_BIT;
	/* During deep sleep wake, firmware polls PMU wake-cause state first.
	 * Avoid forcing an immediate IRQ exception into low-power stubs, which can
	 * vector into uninitialized/default handlers. */
	if (from_sleep)
		int_set(INT_POWER, false);
	else
		aladdin_pmu_update_int();
}

void aladdin_pmu_reset(void) {
	memset(&aladdin_pmu, 0, sizeof(aladdin_pmu));
	aladdin_pmu.clocks = 0x21020303;
	wakeup_reason = 0x040000;
	aladdin_pmu_reg_04 = 0;
	aladdin_pmu_ctrl_08 = 0x2000;
	aladdin_pmu.disable[0] = 0;
	aladdin_pmu.noidea[0] = 0x1A;
	/* Keep PMU status free of low-power sticky flags at reset. */
	aladdin_pmu.noidea[1] = 0x1;
	/* Observed reads from 0x9014080C expect this bit high. */
	aladdin_pmu.noidea[3] = 0x00100000;
	aladdin_pmu.noidea[4] = 0x111;
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
	/* +0x00 mirrors the efuse/ID word on real TG2985E hardware. */
	tg2989_pmic.reg[0] = 0x010C9231u;
	/* +0x04 is the PMIC ID/status word used by DIAGS and early boot code. */
	tg2989_pmic.reg[TG2989_PMIC_REG_ID_STATUS >> 2] = tg2989_pmic_id_status_value();
	/* Initialize power-status domain from observed battery-only dump values. */
	tg2989_pmic_refresh_power_status();
}

uint32_t tg2989_pmic_read(uint32_t addr)
{
	uint32_t offset = addr & 0xFFFFu;
	if (offset == TG2989_PMIC_REG_ID_STATUS)
		return tg2989_pmic_id_status_value();
	if (offset < 0x100u) {
		tg2989_pmic_refresh_power_status();
		return tg2989_pmic.reg[offset >> 2];
	}
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
	uint32_t pending = aladdin_pmu_pend_with_live_sources();
	/* ADC completion has dedicated VIC lines (11/13). Keep its PMU pending bit
	 * visible to firmware, but do not mirror it onto INT_POWER. Otherwise the
	 * power IRQ can stay asserted through sleep and break ON-key wake flow. */
	pending &= ~PMU_IRQ_ADC_BIT;
	/* PMU+0x24 wake bit (0x2) is status-only for ROM wake polling; it should
	 * not by itself level-assert INT_POWER. */
	bool on = ((aladdin_pmu.int_state & ~PMU_INT_WAKE_BIT) != 0)
		|| ((pending & aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX]) != 0);
	int_set(INT_POWER, on);
}

static uint32_t aladdin_pmu_status_80c_read_value(void)
{
	uint32_t value = aladdin_pmu.noidea[3];
	/* PMU+0x80C model bucket in bits[24:20] is polled during boot.
	 * Preserve firmware-owned bits, but keep a sane default bucket (1). */
	uint32_t model = asic_user_flags & 0x1Fu;
	if (model == 0)
		model = 1;
	value &= ~0x01F00000u;
	value |= model << 20;
	return value;
}

static uint32_t aladdin_pmu_status_810_read_value(void)
{
	uint32_t value = aladdin_pmu.noidea[4];
	/* Keep mandatory status bits stable while exposing physical ON-key state at
	 * bit8. Firmware wake paths can wait for ON release, so do not force this
	 * bit low from latched wake-cause state alone. */
	value |= 0x11u;
	if (keypad.key_map[0] & (1 << 9))
		value &= ~0x100u;
	else
		value |= 0x100u;
	return value;
}


static uint32_t aladdin_pmu_disable2_read_value()
{
	/* TI-Nspire/OSLoader/DIAGS all contain helpers that set/clear control bits
	 * in the HIGH halfword of 0x90140050/0x90140060. Keep those firmware-owned
	 * bits intact and only synthesize the low battery/charger fields. */
	uint32_t value = aladdin_pmu.disable[2];

	cx2_power_rails_t rails = {};
	cx2_get_power_rails(&rails);

	/* PMU battery field consumed by TI-OS stats is a different code domain than
	 * the DIAGS raw ADC channel. Keep DIAGS LBAT raw in misc.c and synthesize
	 * PMU code separately so BattInfo tracks the configured battery voltage.
	 *
	 * Empirical guest path:
	 *   code ~704 -> ~3010mV, code ~885 -> ~3782mV.
	 * Invert that scale so a 4000mV override maps near the expected guest value.
	 */
	uint32_t batt_code = 0u;
	if (rails.battery_present) {
		int mv = cx2_clamp_int(rails.battery_mv, 0, 5500);
		int code = (mv * 704 + 1500) / 3000;
		batt_code = (uint32_t)cx2_clamp_int(code, 0, 0x3FF);
	}

	/* Charger state is explicitly encoded in [17:16]:
	 *   00 = disconnected, 01 = connected/not charging, 11 = charging. */
	uint32_t charger_bits = 0u;
	switch (rails.charger_state) {
	case CHARGER_CHARGING:
		charger_bits = 0x3u;
		break;
	case CHARGER_CONNECTED_NOT_CHARGING:
		charger_bits = 0x1u;
		break;
	case CHARGER_DISCONNECTED:
	default:
		charger_bits = 0x0u;
		break;
	}

	value &= ~((0x3FFu << 6) | (0x3u << 16));
	value |= batt_code << 6;
	value |= charger_bits << 16;
	return value;
}

static uint32_t aladdin_pmu_disable1_read_value()
{
	/* Source-voltage channel used by guest battery stats. Keep it synthesized
	 * from external rails so USB transitions cannot leak stale scratch bits
	 * into absurd source readings (e.g. 917698mV). */
	uint32_t value = aladdin_pmu.disable[1] & 0x3Fu;

	int src_mv = cx2_external_source_mv();

	uint32_t src_code = (uint32_t)((src_mv * 1008 + 1650) / 3300);
	if (src_code > 0x0FFFu)
		src_code = 0x0FFFu;

	value &= ~0x0003FFC0u;
	value |= (src_code & 0x0FFFu) << 6;
	return value;
}

static uint32_t aladdin_pmu_disable0_read_value()
{
	uint32_t value = aladdin_pmu.disable[0];
	/* Bit 0x400 = battery present, bit 0x100 = external source present.
	 * Firmware checks these during boot to determine power state. */
	if (cx2_effective_battery_present())
		value |= 0x00000400u;
	else
		value &= ~0x00000400u;
	if (cx2_external_power_present())
		value |= 0x00000100u;
	else
		value &= ~0x00000100u;
	return value;
}

static uint32_t aladdin_pmu_usb_phy_status_read_value()
{
	/* Observed on hardware dumps:
	 *   battery/no-USB: 0x2
	 *   USB attached:   0xE
	 */
	uint32_t value = 0x2u;
	if (cx2_external_power_present()
			&& (aladdin_pmu.disable[0] & 0x400))
		value |= 0xCu;
	return value;
}

uint32_t aladdin_pmu_read(uint32_t addr)
{
	uint32_t offset = addr & 0xFFFF;
	if(offset < 0x100)
	{
		switch (addr & 0xFF)
		{
		case 0x00: return wakeup_reason;
		case 0x04: return aladdin_pmu_reg_04;
		case 0x08: return aladdin_pmu_ctrl_08;
		case 0x20: return aladdin_pmu_disable0_read_value();
		case 0x24: return aladdin_pmu.int_state;
		case 0x30: return aladdin_pmu.clocks;
		case 0x50: return aladdin_pmu_disable1_read_value();
		case 0x60: return aladdin_pmu_disable2_read_value();
		case 0xC4: return aladdin_pmu.int_enable;
		}
	}
	else if(offset >= 0x800 && offset < 0x900)
	{
		if(offset == 0x808)
			return 0x010C9231;
		else if(offset == 0x80C)
			return aladdin_pmu_status_80c_read_value();
		else if(offset == 0x810)
		{
			adc_cx2_background_step();
			return aladdin_pmu_status_810_read_value();
		}
		else if(offset == 0x850)
			return aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX];
		else if(offset == 0x854)
		{
			adc_cx2_background_step();
			uint32_t pending = aladdin_pmu_pend_with_live_sources();
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
		case 0x00:
			/* Live firmware bitfield (also carries wakeup reason at boot). */
			wakeup_reason = value;
			return;
		case 0x04:
			aladdin_pmu_reg_04 = value;
			return;
		case 0x08: aladdin_pmu_ctrl_08 = value; return;
		case 0x20:
			if(value & 2) {
				/* Sleep transition should leave only ON-key wake path active. */
				keypad_release_all_keys();
				cpu_events |= EVENT_SLEEP;
				event_clear(SCHED_TIMERS);
				event_clear(SCHED_TIMER_FAST);
				/* Reset PMU so bootrom sees correct clock/PMU state on wake. */
				aladdin_pmu_reset();
			} else {
				aladdin_pmu.disable[0] = value;
			}
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
		case 0xC4:
			aladdin_pmu.int_enable = value;
			aladdin_pmu_update_int();
			return;
		}
	}
	else if(offset >= 0x800 && offset < 0x900)
	{
		if (offset == 0x80C) {
			aladdin_pmu.noidea[3] = value;
			return;
		}
		if (offset == 0x810) {
			aladdin_pmu.noidea[4] = value;
			return;
		}
			if (offset == 0x850) {
				aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX] = value;
				aladdin_pmu_update_int();
				return;
			}
			if (offset == 0x854) {
				/* W1C: writing 1 clears corresponding pending bits. */
				aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX] &= ~value;
				aladdin_pmu_update_int();
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

static uint8_t cx2_backlight_contrast_from_pwm(void)
{
	/* Per Hackspire: period=255, value 0 (brightest) to 225 (darkest). */
	if (cx2_backlight.pwm_period == 0)
		return 0;
	int contrast = LCD_CONTRAST_MAX
		- (int)((cx2_backlight.pwm_value * LCD_CONTRAST_MAX) / cx2_backlight.pwm_period);
	return (uint8_t)cx2_clamp_int(contrast, 0, LCD_CONTRAST_MAX);
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
		hdq1w.lcd_contrast = (uint8_t)cx2_clamp_int(lcd_override, 0, LCD_CONTRAST_MAX);
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
struct cx2_lcd_spi_state cx2_lcd_spi;

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

bool cx2_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
            && snapshot_write(snapshot, &wakeup_reason, sizeof(wakeup_reason))
            && snapshot_write(snapshot, &aladdin_pmu_ctrl_08, sizeof(aladdin_pmu_ctrl_08))
            && snapshot_write(snapshot, &tg2989_pmic, sizeof(tg2989_pmic))
            && snapshot_write(snapshot, &cx2_backlight, sizeof(cx2_backlight))
            && snapshot_write(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
            && snapshot_write(snapshot, &dma, sizeof(dma));
}

bool cx2_resume(const emu_snapshot *snapshot)
{
    bool ok = snapshot_read(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
            && snapshot_read(snapshot, &wakeup_reason, sizeof(wakeup_reason))
            && snapshot_read(snapshot, &aladdin_pmu_ctrl_08, sizeof(aladdin_pmu_ctrl_08))
            && snapshot_read(snapshot, &tg2989_pmic, sizeof(tg2989_pmic))
            && snapshot_read(snapshot, &cx2_backlight, sizeof(cx2_backlight))
            && snapshot_read(snapshot, &cx2_lcd_spi, sizeof(cx2_lcd_spi))
            && snapshot_read(snapshot, &dma, sizeof(dma));
    if (ok)
        aladdin_pmu_update_int();
    return ok;
}
