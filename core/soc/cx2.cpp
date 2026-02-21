#include "emu.h"

#include "memory/mem.h"
#include "soc/cx2.h"
#include "peripherals/misc.h"
#include "timing/schedule.h"
#include "peripherals/interrupt.h"
#include "peripherals/keypad.h"

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
bool cx2_peripherals_suspend(emu_snapshot *snapshot);
bool cx2_peripherals_resume(const emu_snapshot *snapshot);

static uint32_t aladdin_pmu_pend_with_live_sources(void)
{
	uint32_t pending = aladdin_pmu.noidea[PMU_IRQ_PEND_INDEX];
	if (intr.active & ((1u << INT_ADC) | (1u << INT_ADC_ALT)))
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
	const uint32_t mask = aladdin_pmu.noidea[PMU_IRQ_MASK_INDEX];
	const bool onkey_enabled = (aladdin_pmu.int_enable & 1u) != 0;
	if (!onkey_enabled)
		pending &= ~PMU_IRQ_ONKEY_BIT;
	/* ADC completion has dedicated VIC lines (11/13). Keep its PMU pending bit
	 * visible to firmware, but do not mirror it onto INT_POWER. Otherwise the
	 * power IRQ can stay asserted through sleep and break ON-key wake flow. */
	pending &= ~PMU_IRQ_ADC_BIT;
	/* PMU+0x24 wake bit (0x2) is status-only for ROM wake polling; it should
	 * not by itself level-assert INT_POWER. */
	bool on = ((aladdin_pmu.int_state & ~PMU_INT_WAKE_BIT) != 0)
		|| ((pending & mask) != 0);
	int_set(INT_POWER, on);
	int_set(INT_IRQ30, (pending & mask & PMU_IRQ_ONKEY_BIT) != 0);
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

bool cx2_suspend(emu_snapshot *snapshot)
{
	return snapshot_write(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
		&& snapshot_write(snapshot, &wakeup_reason, sizeof(wakeup_reason))
		&& snapshot_write(snapshot, &aladdin_pmu_ctrl_08, sizeof(aladdin_pmu_ctrl_08))
		&& snapshot_write(snapshot, &tg2989_pmic, sizeof(tg2989_pmic))
		&& cx2_peripherals_suspend(snapshot);
}

bool cx2_resume(const emu_snapshot *snapshot)
{
	bool ok = snapshot_read(snapshot, &aladdin_pmu, sizeof(aladdin_pmu))
		&& snapshot_read(snapshot, &wakeup_reason, sizeof(wakeup_reason))
		&& snapshot_read(snapshot, &aladdin_pmu_ctrl_08, sizeof(aladdin_pmu_ctrl_08))
		&& snapshot_read(snapshot, &tg2989_pmic, sizeof(tg2989_pmic))
		&& cx2_peripherals_resume(snapshot);
	if (ok)
		aladdin_pmu_update_int();
	return ok;
}
