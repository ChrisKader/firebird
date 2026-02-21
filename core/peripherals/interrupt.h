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
#define INT_IRQ1   1 // UART1
#define INT_IRQ2   2 // DMA_CONTROLLER
#define INT_IRQ3   3 // WATCHDOG
#define INT_IRQ4   4 // RTC
#define INT_IRQ5   5
#define INT_IRQ6   6
#define INT_IRQ7   7 // GPIO
#define INT_IRQ8   8 // USB_OTG
#define INT_IRQ9   9 // USB_HOST
#define INT_IRQ10  10
#define INT_IRQ11  11 // ADC
#define INT_IRQ12  12
#define INT_IRQ13  13 // SD_HOST_CONTROLLER
#define INT_IRQ14  14 // HDQ_1WIRE/INT_LCD_CONTRAST
#define INT_IRQ15  15 // POWER_MANAGEMENT
#define INT_IRQ16  16 // KEYPAD
#define INT_IRQ17  17 // FAST_TIMER
#define INT_IRQ18  18 // FIRST_TIMER
#define INT_IRQ19  19 // SECOND_TIMER
#define INT_IRQ20  20 // I2C
#define INT_IRQ21  21 // LCD_CONTROLLER
#define INT_IRQ22  22
#define INT_IRQ23  23
#define INT_IRQ24  24 // TOUCHPAD_IRQ0
#define INT_IRQ25  25 // TOUCHPAD_IRQ1
#define INT_IRQ26  26
#define INT_IRQ27  27
#define INT_IRQ28  28
#define INT_IRQ29  29
#define INT_IRQ30  30
#define INT_IRQ31  31

/* Known IRQ role mappings from firmware analysis. */
#define INT_SERIAL_UART             INT_IRQ1
#define INT_WATCHDOG_TIMER          INT_IRQ3
#define INT_RTC                     INT_IRQ4
#define INT_GPIO                    INT_IRQ7
#define INT_USB_OTG                 INT_IRQ8
#define INT_USB_HOST                INT_IRQ9
#define INT_ADC                     INT_IRQ11
#define INT_SD_HOST_CONTROLLER      INT_IRQ13
#define INT_HDQ_1WIRE               INT_IRQ14
#define INT_LCD_CONTRAST            INT_IRQ14
#define INT_HDQ_1WIRE_LCD_CONTRAST  INT_IRQ14
#define INT_POWER_MANAGEMENT        INT_IRQ15
#define INT_KEYPAD                  INT_IRQ16
#define INT_FAST_TIMER              INT_IRQ17
#define INT_FIRST_TIMER             INT_IRQ18
#define INT_SECOND_TIMER            INT_IRQ19
#define INT_I2C                     INT_IRQ20
#define INT_LCD_CONTROLLER          INT_IRQ21

/* Resolved from firmware callsites / MMIO correlation. */
#define INT_DMA_CONTROLLER          INT_IRQ2  /* logical ID 3; 0xBC000000 block */
#define INT_TOUCHPAD_IRQ0           INT_IRQ24 /* logical ID 25; 0x90050000 path */
#define INT_TOUCHPAD_IRQ1           INT_IRQ25 /* logical ID 26; 0x90050000 path */

/* Still unresolved roles from TI-Nspire.bin IRQ map (0x1132A188). */
#define INT_UNKNOWN_IRQ0            INT_IRQ0  /* logical ID 0 */
#define INT_UNKNOWN_IRQ5            INT_IRQ5  /* logical ID 6 */
#define INT_UNKNOWN_IRQ6            INT_IRQ6  /* logical ID 7 */
#define INT_UNKNOWN_IRQ10           INT_IRQ10 /* logical ID 11 */
#define INT_UNKNOWN_IRQ22           INT_IRQ22 /* logical ID 23 */
#define INT_UNKNOWN_IRQ23           INT_IRQ23 /* logical ID 24 */
#define INT_UNKNOWN_IRQ26           INT_IRQ26 /* logical ID 27 */

/* Backward-compat aliases for recently resolved names. */
#define INT_UNKNOWN_IRQ2            INT_DMA_CONTROLLER
#define INT_UNKNOWN_IRQ24           INT_TOUCHPAD_IRQ0
#define INT_UNKNOWN_IRQ25           INT_TOUCHPAD_IRQ1

/* Backward-compatible aliases used by existing code. */
#define INT_SERIAL       INT_SERIAL_UART
#define INT_WATCHDOG     INT_WATCHDOG_TIMER
#define INT_USB          INT_USB_OTG
#define INT_POWER        INT_POWER_MANAGEMENT
#define INT_TIMER_FAST   INT_FAST_TIMER
#define INT_TIMER_FIRST  INT_FIRST_TIMER
#define INT_TIMER_SECOND INT_SECOND_TIMER
#define INT_TIMER0       INT_FAST_TIMER
#define INT_TIMER1       INT_FIRST_TIMER
#define INT_TIMER2       INT_SECOND_TIMER
#define INT_LCD          INT_LCD_CONTROLLER
/* Historical alias retained for existing ADC mirror code paths. */
#define INT_ADC_ALT      INT_SD_HOST_CONTROLLER

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
