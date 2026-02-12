#ifndef CX2_H
#define CX2_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emu_snapshot emu_snapshot;

typedef struct aladdin_pmu_state {
	uint32_t clocks;
	uint32_t disable[3];
	uint32_t int_state; // Actual bit assignments not known
	uint32_t int_enable; // 0xC4: Interrupt enable (bit 0 = ON key?)
	uint32_t noidea[0x100 / sizeof(uint32_t)];
} aladdin_pmu_state;

extern struct aladdin_pmu_state aladdin_pmu;

void aladdin_pmu_write(uint32_t addr, uint32_t value);
uint32_t aladdin_pmu_read(uint32_t addr);
void aladdin_pmu_reset(void);
void aladdin_pmu_set_wakeup_reason(uint32_t reason);
void aladdin_pmu_set_adc_pending(bool on);
void aladdin_pmu_latch_onkey_wake(bool from_sleep);
void aladdin_pmu_on_key_wakeup(void);
void aladdin_pmu_on_key_release(void);

uint32_t tg2989_pmic_read(uint32_t addr);
void tg2989_pmic_write(uint32_t addr, uint32_t value);
void tg2989_pmic_reset(void);

uint32_t memc_ddr_read(uint32_t addr);
void memc_ddr_write(uint32_t addr, uint32_t value);
void memc_ddr_reset(void);

typedef struct cx2_backlight_state {
    uint32_t pwm_period, pwm_value;
} cx2_backlight_state;

uint32_t cx2_backlight_read(uint32_t addr);
void cx2_backlight_write(uint32_t addr, uint32_t value);
void cx2_backlight_reset();

typedef struct cx2_lcd_spi_state {
	bool busy;
} cx2_lcd_spi_state;

uint32_t cx2_lcd_spi_read(uint32_t addr);
void cx2_lcd_spi_write(uint32_t addr, uint32_t value);

typedef struct dma_state {
	uint32_t csr; // 0x24
	struct {
		uint32_t control;
		uint32_t config;
		uint32_t src;
		uint32_t dest;
		uint32_t len;
	} channels[1]; // 0x100+
} dma_state;

void dma_cx2_reset();
uint32_t dma_cx2_read_word(uint32_t addr);
void dma_cx2_write_word(uint32_t addr, uint32_t value);

// The peripherals in cx2.cpp have trivial suspend/resume ops, so don't need
// separate functions each.
bool cx2_suspend(emu_snapshot *snapshot);
bool cx2_resume(const emu_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif // CX2_H
