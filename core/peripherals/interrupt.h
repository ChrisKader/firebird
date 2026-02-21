/* Declarations for interrupt.c */

#ifndef _H_INTERRUPT
#define _H_INTERRUPT

#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* PL190/VIC has 32 interrupt input lines (0..31). */
#define INT_IRQ0   0
#define INT_IRQ1   1
#define INT_IRQ2   2
#define INT_IRQ3   3
#define INT_IRQ4   4
#define INT_IRQ5   5
#define INT_IRQ6   6
#define INT_IRQ7   7
#define INT_IRQ8   8
#define INT_IRQ9   9
#define INT_IRQ10  10
#define INT_IRQ11  11
#define INT_IRQ12  12
#define INT_IRQ13  13
#define INT_IRQ14  14
#define INT_IRQ15  15
#define INT_IRQ16  16
#define INT_IRQ17  17
#define INT_IRQ18  18
#define INT_IRQ19  19
#define INT_IRQ20  20
#define INT_IRQ21  21
#define INT_IRQ22  22
#define INT_IRQ23  23
#define INT_IRQ24  24
#define INT_IRQ25  25
#define INT_IRQ26  26
#define INT_IRQ27  27
#define INT_IRQ28  28
#define INT_IRQ29  29
#define INT_IRQ30  30
#define INT_IRQ31  31

/* Named aliases for lines currently modeled in code paths. */
#define INT_SERIAL   INT_IRQ1
#define INT_WATCHDOG INT_IRQ3
#define INT_GPIO     INT_IRQ7
#define INT_USB      INT_IRQ8
#define INT_ADC      INT_IRQ11
#define INT_ADC_ALT  INT_IRQ13
#define INT_POWER    INT_IRQ15
#define INT_KEYPAD   INT_IRQ16
#define INT_TIMER0   INT_IRQ17
#define INT_TIMER1   INT_IRQ18
#define INT_TIMER2   INT_IRQ19
#define INT_LCD      INT_IRQ21

typedef struct interrupt_state {
	uint32_t active;
	uint32_t raw_status;         // .active ^ ~.noninverted
	uint32_t sticky_status;      // set on rising transition of .raw_status
	uint32_t status;             // +x04: mixture of bits from .raw_status and .sticky_status
                                 //       (determined by .sticky)
	uint32_t mask[2];            // +x08: enabled interrupts
    uint8_t  protection;         // +x20 on CX: only privileged
	uint8_t  prev_pri_limit[2];  // +x28: saved .priority_limit from reading +x24
	uint8_t  priority_limit[2];  // +x2C: interrupts with priority >= this value are disabled
	uint32_t noninverted;        // +200: which interrupts not to invert in .raw_status
	uint32_t sticky;             // +204: which interrupts to use .sticky_status
	uint8_t  priority[32];       // +3xx: priority per interrupt (0=max, 7=min)

	// CX, PL190 vectored interrupt handling
	uint32_t irq_handler_cur;    // +x30: address of the current IRQ handler
	uint32_t irq_handler_def;    // +x34: address of the default IRQ handler
	uint32_t irq_addr_vect[16];  // +100: address of the vectored IRQ handler
	uint8_t  irq_ctrl_vect[16];  // +200: configuration for the vector
} interrupt_state;

extern interrupt_state intr;

uint32_t int_read_word(uint32_t addr);
void int_write_word(uint32_t addr, uint32_t value);
uint32_t int_cx_read_word(uint32_t addr);
void int_cx_write_word(uint32_t addr, uint32_t value);
void int_set(uint32_t int_num, bool on);
void int_reset();
typedef struct emu_snapshot emu_snapshot;
bool interrupt_suspend(emu_snapshot *snapshot);
bool interrupt_resume(const emu_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
