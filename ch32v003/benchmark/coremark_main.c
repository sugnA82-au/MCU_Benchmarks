// CH32V003 CoreMark image. See main.c for why CoreMark is a separate image:
// with CoreMark linked in, 16 KB of flash has no room left for the flash sweep.
//
// Console is the WCH-Link debug channel on SWIO (PD1); see capture.py.

#include "ch32fun.h"
#include <stdio.h>
#include <stdint.h>

#include "platform.h"

extern int coremark_run(void);
extern unsigned long coremark_total_ticks;
extern unsigned long coremark_iterations;
extern unsigned long coremark_validated;

int main(void)
{
    SystemInit();
    Delay_Ms(3000);                      // let the host attach to the debug channel
    ram_canary_paint();
    printf("\n\n");
    rule();
    printf("  CH32V003 COREMARK (CoreMark image)\n");
    rule();
    printf("BENCH_START\n");

    print_system_info("SYSTEM INFO");

    section("CPU BASELINE: CoreMark - REDUCED DATASET, SEE WARNING");
    uint32_t conformant_static = ram_static_bytes() + 2000u - TOTAL_DATA_SIZE;
    printf("  WARNING: this is NOT a conformant CoreMark score.\n");
    printf("  A conformant run needs a 2000-byte working set and this chip has 2048\n");
    printf("  bytes of SRAM in total. The conformant configuration needs %lu bytes\n",
           (unsigned long)conformant_static);
    printf("  of static SRAM before it has any stack at all: %ld bytes more than the\n",
           (long)conformant_static - (long)SRAM_BYTES);
    printf("  %lu the chip has;\n", (unsigned long)SRAM_BYTES);
    printf("  CoreMark's own stack high-water is measured below.\n");
    printf("  What follows is the %u-byte profile-run configuration. CoreMark still\n",
           (unsigned)TOTAL_DATA_SIZE);
    printf("  validates it against its own known CRCs, so the arithmetic is proven\n");
    printf("  correct, but the dataset is smaller than the reporting rules require:\n");
    printf("  NOT comparable with published CoreMark 1.0 numbers or 2000-byte runs.\n\n");
    printf("  Running (self-calibrates to a >=10s measured run)...\n");
    printf("@BEGIN coremark\n");

    uint32_t t_run = ticks_now();
    coremark_run();
    t_run = ticks_since(t_run);

    printf("@END coremark us=%lu\n", (unsigned long)ticks_to_us(t_run));
    uint32_t untouched = ram_untouched_bytes();
    uint32_t stack_hw = ram_stack_highwater_bytes();

    printf("\n  exact ticks      : %lu at %lu Hz = ", coremark_total_ticks,
           (unsigned long)TIMER_HZ);
    pr_ratio(coremark_total_ticks, TIMER_HZ, 3);
    printf(" s\n");
    // iterations/sec = iters * 1e6 / us; iters * 1e6 stays inside 32 bits up to
    // 4294 iterations, which is what the self-calibration lands on here.
    uint32_t us = ticks_to_us(coremark_total_ticks);
    printf("  iterations/sec   : ");
    pr_ratio(coremark_iterations * 1000000u, us, 3);
    printf(" at 48.0 MHz (CoreMark's own line above truncates to whole seconds)\n");
    printf("  per MHz          : ");
    pr_ratio(coremark_iterations * 1000000u, us * (F_CPU_HZ / 1000000u), 4);
    printf("  (reduced dataset - not a reportable CoreMark/MHz)\n");
    printf("  stack high-water : %lu bytes from the top of RAM (canary-measured)\n",
           (unsigned long)stack_hw);
    printf("  conformant fit   : %lu static + %lu stack = %lu bytes needed vs %lu available\n",
           (unsigned long)conformant_static, (unsigned long)stack_hw,
           (unsigned long)(conformant_static + stack_hw), (unsigned long)SRAM_BYTES);
    report(coremark_validated != 0, "CoreMark self-validated (\"Correct operation validated\")");
    report(coremark_total_ticks != 0 && coremark_iterations != 0 &&
           coremark_iterations <= 4000u, "CoreMark completed and reported a score");
    report(conformant_static + stack_hw > SRAM_BYTES,
           "conformant 2000-byte run does not fit in 2048 bytes (measured)");

    section("SUMMARY");
    printf("  RAM never touched by the stack : %lu bytes of %lu\n",
           (unsigned long)untouched, (unsigned long)SRAM_BYTES);
    report(untouched > 64u, "SRAM headroom left over the whole run (canary intact)");
    if (g_fail_count == 0)
        printf("  ALL %u CHECKS PASSED.\n", g_check_count);
    else
        printf("  %u of %u CHECK(S) FAILED - see FAIL lines above.\n", g_fail_count,
               g_check_count);
    rule();
    printf("\nSUITE_DONE failures=%u checks=%u\n", g_fail_count, g_check_count);
    for (;;) { __asm__ volatile("nop"); }
}
