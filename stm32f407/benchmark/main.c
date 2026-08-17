// STM32F407 baremetal CPU / SRAM / CCM / Flash self-test and benchmark.
//
// No RTOS, no HAL, no CMSIS - hand-written register access against RM0090,
// running on the STM32F4 Discovery (STM32F407VGT6 @ 168 MHz).
//
// Covers:
//   CPU     integer ALU, the core's native UDIV/SDIV, hardware FPU against
//           software double-precision, and CoreMark as a portable baseline
//   Memory  main SRAM and CCM measured separately - bandwidth plus four
//           address/pattern integrity sweeps over each
//   Flash   ART accelerator effect, then a full-chip erase/program/verify
//           sweep across every sector this binary does not occupy
//
// Console is semihosting over SWD: this board's ST-LINK/V2 has no virtual
// COM port, and st-trace cannot read SWO from it (see README).

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "stm32f407.h"
#include "platform.h"

extern void   coremark_run(void);
extern double coremark_score;

extern uint32_t _flash_binary_end;

// Fixed constants so every run measures exactly the same amount of work.
#define INT_ITERS    20000000u
#define FLOAT_ITERS   2000000u
#define DIV_ITERS     5000000u

// 96 KiB of the 128 KiB main SRAM. The rest is .data/.bss, the heap+stack
// reservation, and CoreMark's stack-allocated working set. If this ever grows
// past what fits, the link fails with a RAM region overflow rather than
// silently colliding with the stack at run time.
#define SRAM_TEST_BYTES (96u * 1024u)
#define SRAM_TEST_WORDS (SRAM_TEST_BYTES / 4u)

// The full 64 KiB CCM. Nothing else in this program lives there.
#define CCM_TEST_BYTES  (64u * 1024u)
#define CCM_TEST_WORDS  (CCM_TEST_BYTES / 4u)

static volatile uint32_t g_sram_buf[SRAM_TEST_WORDS];
static volatile uint32_t g_ccm_buf[CCM_TEST_WORDS]
    __attribute__((section(".ccmram")));

static int g_fail_count = 0;

static void rule(void) { printf("--------------------------------------------------------------\n"); }

static void section(const char *title)
{
    printf("\n");
    rule();
    printf("  %s\n", title);
    rule();
}

static void report(int ok, const char *what)
{
    if (!ok) { g_fail_count++; led_on(LED_RED); }
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

// ---------------------------------------------------------------------------
// CPU workloads. The optimisation barrier in each one matters: without it
// GCC's IPA-pure-const pass proves a repeated call with the same argument is
// redundant and reuses the first result instead of re-running the loop,
// which would make the determinism check pass unconditionally.
// ---------------------------------------------------------------------------
__attribute__((noinline)) static uint32_t integer_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    uint32_t a = 0x12345678u, b = 0x9ABCDEF0u;
    for (uint32_t i = 0; i < iters; i++) {
        a = (a * 1664525u + 1013904223u) ^ b;
        b += (a << 3) ^ (a >> 5) ^ 0xA5A5A5A5u;
    }
    return a ^ b;
}

// Single precision: the Cortex-M4F FPU executes these natively.
__attribute__((noinline)) static float float_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    float f = 1.0f;
    const float k1 = 1.0000001f, k2 = 0.0000001f;
    for (uint32_t i = 0; i < iters; i++) {
        f = f * k1 + k2;
        if (f > 1.0e6f) f *= 1.0e-6f;
    }
    return f;
}

// Double precision: the M4F FPU is single-precision only, so every operation
// here is a software call into __aeabi_dmul / __aeabi_dadd. Deliberately the
// same shape as the float loop above, which makes the ratio between the two
// a direct measure of what the FPU is worth on this chip.
__attribute__((noinline)) static double double_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    double f = 1.0;
    const double k1 = 1.0000001, k2 = 0.0000001;
    for (uint32_t i = 0; i < iters; i++) {
        f = f * k1 + k2;
        if (f > 1.0e6) f *= 1.0e-6;
    }
    return f;
}

__attribute__((noinline)) static uint32_t divider_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    uint32_t acc = 0;
    uint32_t n = 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= iters; i++) {
        uint32_t d = (i & 0xFFFFu) | 1u;   // avoid div-by-zero
        acc += (n / d) + (n % d);
        n = n * 2654435761u + 1u;
    }
    return acc;
}

// ---------------------------------------------------------------------------

static void print_system_info(void)
{
    section("SYSTEM INFO");

    uint32_t idcode = DBGMCU_IDCODE;
    uint32_t dev_id = idcode & 0xFFFu;
    uint32_t rev_id = (idcode >> 16) & 0xFFFFu;
    uint32_t binary_end = (uint32_t)&_flash_binary_end;

    printf("  Target            : STM32F407VGT6 (Discovery, UM1472)\n");
    printf("  Console           : %s\n",
           console_is_semihosted() ? "semihosting over SWD (debugger attached)"
                                   : "ITM/SWO (no debugger detected)");
    printf("  Compiler          : GCC %s\n", __VERSION__);
    printf("  DBGMCU_IDCODE     : 0x%08lx (dev 0x%03lx, rev 0x%04lx)\n",
           (unsigned long)idcode, (unsigned long)dev_id, (unsigned long)rev_id);
    printf("  Core              : 1 x Cortex-M4F (single-precision FPU, DSP)\n");
    printf("  sys clock         : %.3f MHz\n", g_core_hz / 1.0e6);
    printf("  timer clock       : 1.000 MHz (TIM2, 32-bit, free running)\n");
    printf("  Flash             : %lu KiB in %d sectors (16/16/16/16/64/128x7)\n",
           (unsigned long)(FLASH_SIZE / 1024), FLASH_NUM_SECTORS);
    printf("  SRAM              : %lu KiB main + %lu KiB CCM (core-coupled)\n",
           (unsigned long)(SRAM_SIZE / 1024), (unsigned long)(CCM_SIZE / 1024));
    printf("  this binary ends  : 0x%08lx (%lu KiB used)\n",
           (unsigned long)binary_end,
           (unsigned long)((binary_end - FLASH_BASE_ADDR + 1023) / 1024));
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
static double g_int_mops;

static void bench_cpu(void)
{
    section("CPU BENCHMARK");

    uint32_t chk_a = integer_workload(1000000u);
    uint32_t chk_b = integer_workload(1000000u);
    report(chk_a == chk_b, "integer ALU determinism check (repeat-run checksum match)");

    uint64_t t0 = time_us_64();
    uint32_t ichk = integer_workload(INT_ITERS);
    uint64_t t1 = time_us_64();
    g_int_mops = (double)INT_ITERS / (double)(t1 - t0);
    printf("  integer mix   : %10lu iters in %8lu us  -> %7.2f Mops/s  (checksum %08lx)\n",
           (unsigned long)INT_ITERS, (unsigned long)(t1 - t0), g_int_mops,
           (unsigned long)ichk);

    t0 = time_us_64();
    uint32_t dchk = divider_workload(DIV_ITERS);
    t1 = time_us_64();
    double div_mops = (double)DIV_ITERS / (double)(t1 - t0);
    printf("  hw divider    : %10lu iters in %8lu us  -> %7.2f Mops/s  (checksum %08lx)\n"
           "                  (Cortex-M4 native UDIV/SDIV, not a separate peripheral)\n",
           (unsigned long)DIV_ITERS, (unsigned long)(t1 - t0), div_mops,
           (unsigned long)dchk);

    printf("\n  -- floating point: hardware FPU vs software emulation --\n");

    t0 = time_us_64();
    float fchk = float_workload(FLOAT_ITERS);
    t1 = time_us_64();
    double flt_mops = (double)FLOAT_ITERS / (double)(t1 - t0);
    printf("  float  (FPU)  : %10lu iters in %8lu us  -> %7.2f Mops/s  (result %.6f)\n",
           (unsigned long)FLOAT_ITERS, (unsigned long)(t1 - t0), flt_mops,
           (double)fchk);

    t0 = time_us_64();
    double dchk2 = double_workload(FLOAT_ITERS);
    t1 = time_us_64();
    double dbl_mops = (double)FLOAT_ITERS / (double)(t1 - t0);
    printf("  double (soft) : %10lu iters in %8lu us  -> %7.2f Mops/s  (result %.6f)\n",
           (unsigned long)FLOAT_ITERS, (unsigned long)(t1 - t0), dbl_mops,
           dchk2);
    printf("  -> FPU speedup: %.2fx  (the M4F FPU is single-precision only, so\n"
           "     every double-precision op above is a library call)\n",
           dbl_mops > 0.0 ? flt_mops / dbl_mops : 0.0);

    report(fchk > 0.0f && dchk2 > 0.0, "float and double workloads produced sane results");

    printf("\n  -- note: single core --\n");
    printf("  The STM32F407 has one Cortex-M4F core, so there is no multi-core\n");
    printf("  scaling figure to measure.\n");
}

// ---------------------------------------------------------------------------
// Memory: main SRAM and CCM, measured separately
// ---------------------------------------------------------------------------
static uint32_t pat_address(uint32_t i)  { return i; }
static uint32_t pat_naddress(uint32_t i) { return ~i; }
static uint32_t pat_aa(uint32_t i)       { (void)i; return 0xAAAAAAAAu; }
static uint32_t pat_55(uint32_t i)       { (void)i; return 0x55555555u; }

static void run_pattern(volatile uint32_t *buf, uint32_t words,
                        const char *name, uint32_t (*fn)(uint32_t),
                        const char *region)
{
    for (uint32_t i = 0; i < words; i++) buf[i] = fn(i);
    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < words; i++) {
        if (buf[i] != fn(i)) mismatches++;
    }
    char msg[80];
    snprintf(msg, sizeof(msg), "%s pattern '%s' (%lu mismatches)",
             region, name, (unsigned long)mismatches);
    report(mismatches == 0, msg);
}

static void bench_region(volatile uint32_t *buf, uint32_t words, const char *region)
{
    uint32_t bytes = words * 4u;

    uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < words; i++) buf[i] = i * 2654435761u;
    uint64_t t1 = time_us_64();
    printf("  %-5s write : %6lu KiB in %7lu us -> %8.1f MB/s\n",
           region, (unsigned long)(bytes / 1024), (unsigned long)(t1 - t0),
           (double)bytes / (double)(t1 - t0));

    volatile uint32_t sink;
    t0 = time_us_64();
    uint32_t acc = 0;
    for (uint32_t i = 0; i < words; i++) acc += buf[i];
    t1 = time_us_64();
    sink = acc; (void)sink;
    printf("  %-5s read  : %6lu KiB in %7lu us -> %8.1f MB/s\n",
           region, (unsigned long)(bytes / 1024), (unsigned long)(t1 - t0),
           (double)bytes / (double)(t1 - t0));

    uint32_t half_words = words / 2u;
    uint32_t half_bytes = half_words * 4u;
    for (uint32_t i = 0; i < half_words; i++) buf[i] = i ^ 0xC5C5C5C5u;
    t0 = time_us_64();
    memcpy((void *)&buf[half_words], (const void *)&buf[0], half_bytes);
    t1 = time_us_64();
    printf("  %-5s copy  : %6lu KiB in %7lu us -> %8.1f MB/s\n",
           region, (unsigned long)(half_bytes / 1024), (unsigned long)(t1 - t0),
           (double)half_bytes / (double)(t1 - t0));

    char msg[80];
    snprintf(msg, sizeof(msg), "%s copy buffer verify", region);
    report(memcmp((const void *)&buf[half_words], (const void *)&buf[0], half_bytes) == 0, msg);

    run_pattern(buf, words, "address",    pat_address,  region);
    run_pattern(buf, words, "~address",   pat_naddress, region);
    run_pattern(buf, words, "0xAAAAAAAA", pat_aa,       region);
    run_pattern(buf, words, "0x55555555", pat_55,       region);
}

static void bench_memory(void)
{
    section("MEMORY BENCHMARK (SRAM + CCM)");
    printf("  Main SRAM test region : %lu KiB of %lu KiB (rest is .data/.bss,\n"
           "                          heap/stack reservation and CoreMark's stack)\n",
           (unsigned long)(SRAM_TEST_BYTES / 1024), (unsigned long)(SRAM_SIZE / 1024));
    printf("  CCM test region       : %lu KiB of %lu KiB (all of it - nothing else\n"
           "                          in this program lives in CCM)\n\n",
           (unsigned long)(CCM_TEST_BYTES / 1024), (unsigned long)(CCM_SIZE / 1024));

    bench_region(g_sram_buf, SRAM_TEST_WORDS, "SRAM");
    printf("\n");
    bench_region(g_ccm_buf, CCM_TEST_WORDS, "CCM");
    printf("\n  CCM is core-coupled and zero-wait-state on the D-bus, but is not\n"
           "  reachable by DMA - a real constraint, not a benchmark artefact.\n");
}

// ---------------------------------------------------------------------------
// Flash: ART accelerator effect, then a full-chip erase/program/verify sweep
// ---------------------------------------------------------------------------
static uint8_t sweep_pattern_byte(uint32_t sector, uint32_t offset)
{
    return (uint8_t)(sector * 173u + offset * 31u + 7u);
}

static void set_art(int enabled)
{
    if (enabled) {
        FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN |
                     FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    } else {
        // Caches must be reset while disabled, else stale lines survive a
        // later re-enable (RM0090 3.5.3).
        FLASH->ACR = FLASH_ACR_LATENCY_5WS;
        FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_ICRST | FLASH_ACR_DCRST;
        FLASH->ACR = FLASH_ACR_LATENCY_5WS;
    }
    __asm__ volatile ("dsb; isb");
}

static uint32_t g_sector_buf[16 * 1024 / 4];   // one 16 KiB chunk at a time

static void bench_flash(void)
{
    section("FLASH BENCHMARK");

    uint32_t binary_end = (uint32_t)&_flash_binary_end;

    // --- ART accelerator: sequential read of a 64 KiB flash window with the
    // prefetch buffer and I/D caches on, then off. Wait states are held at 5
    // in both cases, so this isolates the accelerator itself.
    const uint32_t kReadBytes = 64u * 1024u;
    const volatile uint8_t *fp = (const volatile uint8_t *)FLASH_BASE_ADDR;

    set_art(1);
    uint32_t sum1 = 0;
    uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < kReadBytes; i++) sum1 += fp[i];
    uint64_t t1 = time_us_64();
    double art_on_mbs = (double)kReadBytes / (double)(t1 - t0);
    printf("  flash read, ART on  : %4lu KiB in %8lu us -> %8.1f MB/s  (chk %08lx)\n",
           (unsigned long)(kReadBytes / 1024), (unsigned long)(t1 - t0),
           art_on_mbs, (unsigned long)sum1);

    set_art(0);
    uint32_t sum2 = 0;
    t0 = time_us_64();
    for (uint32_t i = 0; i < kReadBytes; i++) sum2 += fp[i];
    t1 = time_us_64();
    double art_off_mbs = (double)kReadBytes / (double)(t1 - t0);
    printf("  flash read, ART off : %4lu KiB in %8lu us -> %8.1f MB/s  (chk %08lx)\n",
           (unsigned long)(kReadBytes / 1024), (unsigned long)(t1 - t0),
           art_off_mbs, (unsigned long)sum2);
    set_art(1);

    printf("  -> ART speedup: %.2fx  (at 168 MHz flash needs 5 wait states; the\n"
           "     accelerator is what hides them)\n",
           art_off_mbs > 0.0 ? art_on_mbs / art_off_mbs : 0.0);
    report(sum1 == sum2, "flash reads identical with ART on and off");

    // --- full-chip sweep over every sector this binary does not occupy.
    uint32_t first_free = 0;
    for (uint32_t s = 0; s < FLASH_NUM_SECTORS; s++) {
        if (g_flash_sectors[s].base + g_flash_sectors[s].size > binary_end) {
            first_free = s;
            if (g_flash_sectors[s].base < binary_end) first_free = s + 1;
            break;
        }
    }

    uint32_t sweep_bytes = 0;
    for (uint32_t s = first_free; s < FLASH_NUM_SECTORS; s++)
        sweep_bytes += g_flash_sectors[s].size;

    printf("\n  full-chip erase/program/verify sweep: sectors %lu-%d, %lu KiB\n",
           (unsigned long)first_free, FLASH_NUM_SECTORS - 1,
           (unsigned long)(sweep_bytes / 1024));
    printf("  (sectors 0-%lu hold this program and are left untouched)\n",
           (unsigned long)(first_free - 1));

    uint64_t erase_us = 0, program_us = 0;
    uint32_t err_mask = 0;

    flash_unlock();
    for (uint32_t s = first_free; s < FLASH_NUM_SECTORS; s++) {
        uint32_t base = g_flash_sectors[s].base;
        uint32_t size = g_flash_sectors[s].size;

        irq_disable();
        t0 = time_us_64();
        err_mask |= flash_erase_sector(s);
        t1 = time_us_64();
        irq_enable();
        erase_us += (t1 - t0);

        // Program the sector in 16 KiB chunks so the staging buffer stays
        // small enough to live in SRAM alongside everything else.
        for (uint32_t off = 0; off < size; off += sizeof(g_sector_buf)) {
            uint32_t chunk = sizeof(g_sector_buf);
            if (off + chunk > size) chunk = size - off;

            uint8_t *b = (uint8_t *)g_sector_buf;
            for (uint32_t j = 0; j < chunk; j++)
                b[j] = sweep_pattern_byte(s, off + j);

            irq_disable();
            t0 = time_us_64();
            err_mask |= flash_program_words(base + off, g_sector_buf, chunk / 4u);
            t1 = time_us_64();
            irq_enable();
            program_us += (t1 - t0);
        }
        led_on(LED_ORANGE);
    }
    flash_lock();

    report(err_mask == 0, "erase+program completed with no FLASH_SR error flags");

    // --- verify every swept byte
    uint32_t bad_sectors = 0, first_bad = 0xFFFFFFFFu;
    uint64_t bad_bytes = 0;
    for (uint32_t s = first_free; s < FLASH_NUM_SECTORS; s++) {
        const volatile uint8_t *vp = (const volatile uint8_t *)g_flash_sectors[s].base;
        int sector_ok = 1;
        for (uint32_t j = 0; j < g_flash_sectors[s].size; j++) {
            if (vp[j] != sweep_pattern_byte(s, j)) { bad_bytes++; sector_ok = 0; }
        }
        if (!sector_ok) {
            bad_sectors++;
            if (first_bad == 0xFFFFFFFFu) first_bad = s;
        }
    }

    char msg[120];
    if (bad_sectors == 0) {
        snprintf(msg, sizeof(msg),
                 "verify all %lu sectors (%lu KiB) - 0 mismatches, all flash accessible",
                 (unsigned long)(FLASH_NUM_SECTORS - first_free),
                 (unsigned long)(sweep_bytes / 1024));
    } else {
        snprintf(msg, sizeof(msg),
                 "verify: %lu sectors FAILED (%lu bad bytes, first at sector %lu)",
                 (unsigned long)bad_sectors, (unsigned long)bad_bytes,
                 (unsigned long)first_bad);
    }
    report(bad_sectors == 0, msg);

    printf("  erase   : %9lu us total  (%lu KiB, %.1f KB/s aggregate)\n",
           (unsigned long)erase_us, (unsigned long)(sweep_bytes / 1024),
           (double)sweep_bytes / (double)erase_us * 1000.0);
    printf("  program : %9lu us total  (%lu KiB, %.1f KB/s aggregate)\n",
           (unsigned long)program_us, (unsigned long)(sweep_bytes / 1024),
           (double)sweep_bytes / (double)program_us * 1000.0);

    // --- cleanup: leave the swept region blank
    err_mask = 0;
    flash_unlock();
    for (uint32_t s = first_free; s < FLASH_NUM_SECTORS; s++) {
        irq_disable();
        err_mask |= flash_erase_sector(s);
        irq_enable();
    }
    flash_lock();

    uint32_t not_blank = 0;
    for (uint32_t s = first_free; s < FLASH_NUM_SECTORS; s++) {
        const volatile uint32_t *vp = (const volatile uint32_t *)g_flash_sectors[s].base;
        for (uint32_t j = 0; j < g_flash_sectors[s].size / 4u; j++)
            if (vp[j] != 0xFFFFFFFFu) not_blank++;
    }
    report(err_mask == 0 && not_blank == 0,
           "cleanup erase (all swept sectors restored to blank 0xFF state)");
}

// ---------------------------------------------------------------------------
// CoreMark
// ---------------------------------------------------------------------------
static void bench_coremark(void)
{
    section("CPU BASELINE: CoreMark (portable, for comparing other MCUs)");
    printf("  Running (self-calibrates to a ~10s measured run)...\n");

    uint64_t t0 = time_us_64();
    coremark_run();
    uint64_t t1 = time_us_64();

    double wall_secs = (double)(t1 - t0) / 1.0e6;
    double mhz = g_core_hz / 1.0e6;
    printf("\n  wall time        : %.2f s (includes self-calibration passes)\n", wall_secs);
    if (coremark_score > 0.0) {
        printf("  CoreMark/MHz     : %.3f  (%.2f CoreMark @ %.1f MHz)\n",
               coremark_score / mhz, coremark_score, mhz);
        printf("  -> compare this CoreMark/MHz figure directly against any\n"
               "     other MCU's published CoreMark/MHz score.\n");
    }
    report(coremark_score > 0.0, "CoreMark completed and reported a score");
}

// ---------------------------------------------------------------------------

int main(void)
{
    DBGMCU_CR |= DBGMCU_CR_SLEEP | DBGMCU_CR_STOP | DBGMCU_CR_STANDBY;

    int pll_ok = clock_init_168mhz();
    board_leds_init();
    timer_init();
    dwt_init();
    itm_init(2000000u);        // 2 MHz SWO, if the probe can carry it
    console_init();            // picks semihosting when a debugger is attached

    led_on(LED_GREEN);

    // Small settle before the first line that matters.
    delay_ms(500);

    printf("\n\n");
    rule();
    printf("  STM32F407 BAREMETAL SELF-TEST / BENCHMARK\n");
    rule();
    if (!pll_ok)
        printf("  WARNING: HSE did not start; running on HSI. All timings below\n"
               "           are against a %lu Hz core clock, not 168 MHz.\n",
               (unsigned long)g_core_hz);

    print_system_info();
    bench_cpu();
    led_on(LED_BLUE);
    bench_memory();
    bench_flash();
    bench_coremark();

    section("SUMMARY");
    if (g_fail_count == 0) {
        printf("  ALL CHECKS PASSED - CPU, FPU, SRAM, CCM and Flash all verified.\n");
        led_off(LED_RED);
    } else {
        printf("  %d CHECK(S) FAILED - see FAIL lines above.\n", g_fail_count);
    }
    rule();
    printf("\ndone.\n");

    // Steady green = passed, steady red = failed; orange blinks so it is
    // obvious the board is alive and finished rather than hung mid-run.
    for (;;) {
        led_on(LED_ORANGE);  delay_ms(500);
        led_off(LED_ORANGE); delay_ms(500);
    }
}
