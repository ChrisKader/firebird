/*
 * dump_pmic_regs.c — Ndless program for TI-Nspire CX II
 *
 * Dumps TG2989 PMIC and Aladdin PMU registers to /documents/pmic_dump.txt.
 *
 * Build:
 *   nspire-gcc -O2 -Wall -o dump_pmic_regs.o dump_pmic_regs.c
 *   genzehn --input dump_pmic_regs.o --output dump_pmic_regs.tns \
 *           --compress --name "pmic_dump"
 */

#include <os.h>
#include <stdio.h>

#define REG32(addr)  (*(volatile unsigned long *)(uintptr_t)(addr))

int main(void)
{
    FILE *f = fopen("/documents/pmic_dump.txt", "w");
    if (!f)
        return 1;

    unsigned long off;

    /* TG2989 PMIC at 0x90100000: 64 word registers */
    fprintf(f, "=== TG2989 PMIC ===\n");
    for (off = 0x00; off <= 0xFC; off += 4)
        fprintf(f, "%03lX=%08lX\n", off, REG32(0x90100000u + off));

    /* Aladdin PMU at 0x90140000: low page (config) */
    fprintf(f, "\n=== Aladdin PMU low ===\n");
    for (off = 0x00; off <= 0xFC; off += 4)
        fprintf(f, "%03lX=%08lX\n", off, REG32(0x90140000u + off));

    /* Aladdin PMU high page (status/IRQ at +0x800) */
    fprintf(f, "\n=== Aladdin PMU high ===\n");
    for (off = 0x800; off <= 0x8FC; off += 4)
        fprintf(f, "%03lX=%08lX\n", off, REG32(0x90140000u + off));

    /* GPIO: just read the data/input registers (safe, no side effects) */
    fprintf(f, "\n=== GPIO data ===\n");
    {
        int s;
        for (s = 0; s < 8; s++) {
            unsigned long base = 0x90000000u + (unsigned)s * 0x40u;
            fprintf(f, "s%d: dat=%02lX dir=%02lX out=%02lX\n", s,
                    REG32(base + 0x00) & 0xFF,
                    REG32(base + 0x10) & 0xFF,
                    REG32(base + 0x14) & 0xFF);
        }
    }

    /* ADC sample bank at 0x900B0000 (read-only sample slots, safe) */
    fprintf(f, "\n=== ADC samples ===\n");
    for (off = 0x00; off <= 0x1C; off += 4)
        fprintf(f, "%03lX=%08lX\n", off, REG32(0x900B0000u + off));

    fclose(f);
    return 0;
}
