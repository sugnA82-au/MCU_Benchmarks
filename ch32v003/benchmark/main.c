// CH32V003 baremetal CPU / SRAM / flash self-test and benchmark.
//
// No RTOS, no vendor HAL - the ch32fun register headers and startup code, the
// CH32V003 reference manual, built with WCH's RISC-V GCC and flashed through a
// WCH-LinkE. CoreMark lives in a separate image (coremark_main.c): with CoreMark
// linked in, 16 KB of flash leaves no room for the flash sweep.
//
// Covers:
//   CPU     integer ALU, software 32-bit divide, software single-precision
//           float (this part has no FPU and no hardware multiply or divide)
//   Memory  the 2 KB SRAM: bandwidth plus four address/pattern integrity sweeps
//   Flash   read throughput against SRAM, and a full page erase / program /
//           verify / erase sweep of everything past the end of this image
//
// Console is the WCH-Link debug channel on the SWIO pin (PD1): the probe must
// stay attached and a host must be draining it (minichlink -T, see capture.py).
// Console writes are kept out of every timed region.

#include "ch32fun.h"
#include <stdio.h>
#include <stdint.h>

#include "platform.h"

// Fixed constants so every run measures exactly the same amount of work.
#define INT_ITERS    20000000u
#define FLOAT_ITERS   2000000u
#define DIV_ITERS     5000000u

#define SCRATCH_BYTES 1200u
#define SCRATCH_WORDS (SCRATCH_BYTES / 4u)
#define MEM_PASSES        2000u
#define FLASH_READ_PASSES   16u
#define SRAM_READ_PASSES   218u         // 218 * 1200 ~= 16 * 16384, so MB/s compare

static uint32_t scratch[SCRATCH_WORDS];

// ---------------------------------------------------------------------------
// CPU workloads. Copied byte-for-byte from the reference implementation; the
// optimisation barrier in each one matters. Without it GCC's IPA-pure-const
// pass proves a repeated call with the same argument is redundant and reuses
// the first result instead of re-running the loop, which would make the
// determinism check pass unconditionally.
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

// No FPU on this part: every operation below is a call into libgcc's
// __mulsf3 / __addsf3 / __gesf2.
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

__attribute__((noinline)) static uint32_t divider_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    uint32_t acc = 0, n = 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= iters; i++) {
        uint32_t d = (i & 0xFFFFu) | 1u;   // avoid div-by-zero
        acc += (n / d) + (n % d);
        n = n * 2654435761u + 1u;
    }
    return acc;
}

// "  -> R Mops/s, C cyc/iter" for a timed region of `iters` iterations.
static void print_rate(uint32_t iters, uint32_t ticks)
{
    printf("                  -> ");
    pr_ratio(iters * TICKS_PER_US, ticks, 4);       // iterations per microsecond
    printf(" Mops/s, ");
    pr_cycles(ticks, iters, 2);
    printf(" cyc/iter");
}

static void bench_cpu(void)
{
    section("CPU BENCHMARK");

    uint32_t chk_a = integer_workload(1000000u);
    uint32_t chk_b = integer_workload(1000000u);
    report(chk_a == chk_b, "integer ALU determinism check (repeat-run checksum match)");

    printf("@BEGIN integer\n");
    uint32_t t0 = ticks_now();
    uint32_t ichk = integer_workload(INT_ITERS);
    uint32_t dt = ticks_since(t0);
    printf("@END integer us=%lu\n", (unsigned long)ticks_to_us(dt));
    printf("  integer mix   : %lu iters in %lu us\n", (unsigned long)INT_ITERS,
           (unsigned long)ticks_to_us(dt));
    print_rate(INT_ITERS, dt);
    printf(", checksum %08lx\n", (unsigned long)ichk);
    report(ichk == 0xf2c53d7bu, "integer checksum matches the cross-chip reference f2c53d7b");

    printf("@BEGIN divide\n");
    t0 = ticks_now();
    uint32_t dchk = divider_workload(DIV_ITERS);
    dt = ticks_since(t0);
    printf("@END divide us=%lu\n", (unsigned long)ticks_to_us(dt));
    printf("  divide (soft) : %lu iters in %lu us\n", (unsigned long)DIV_ITERS,
           (unsigned long)ticks_to_us(dt));
    print_rate(DIV_ITERS, dt);
    printf(", checksum %08lx\n", (unsigned long)dchk);
    printf("                  No hardware divider (RV32EC has no M extension): this is\n");
    printf("                  libgcc's software divide; constant multiplies are\n");
    printf("                  expanded to shifts/adds inline (no __mulsi3 call)\n");

    printf("\n  -- floating point: software only, this part has no FPU --\n");
    printf("@BEGIN float\n");
    t0 = ticks_now();
    float fchk = float_workload(FLOAT_ITERS);
    dt = ticks_since(t0);
    printf("@END float us=%lu\n", (unsigned long)ticks_to_us(dt));
    union { float f; uint32_t u; } fb = { fchk };
    printf("  float  (soft) : %lu iters in %lu us\n", (unsigned long)FLOAT_ITERS,
           (unsigned long)ticks_to_us(dt));
    print_rate(FLOAT_ITERS, dt);
    printf(", result bits %08lx\n", (unsigned long)fb.u);
    report(fchk > 0.0f, "float workload produced a sane result");
    printf("  No FPU, so the float row IS the software row: nothing to price it against.\n");
    printf("  Single core: no multi-core scaling to measure.\n");
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
static uint32_t pat_address(uint32_t i)  { return i; }
static uint32_t pat_naddress(uint32_t i) { return ~i; }
static uint32_t pat_aa(uint32_t i)       { (void)i; return 0xAAAAAAAAu; }
static uint32_t pat_55(uint32_t i)       { (void)i; return 0x55555555u; }

static void run_pattern(volatile uint32_t *buf, uint32_t words, const char *name,
                        uint32_t (*fn)(uint32_t))
{
    for (uint32_t i = 0; i < words; i++) buf[i] = fn(i);
    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < words; i++) if (buf[i] != fn(i)) mismatches++;
    char line[64];
    snprintf(line, sizeof line, "SRAM pattern '%s' (%lu mismatches)", name,
             (unsigned long)mismatches);
    report(mismatches == 0, line);
}

// A plain word-copy loop. This build links no libc (nostdlib), so the copy row
// measures this loop, not a library memcpy.
__attribute__((noinline)) static void copy_words(volatile uint32_t *dst,
                                                 const volatile uint32_t *src, uint32_t n)
{
    while (n--) *dst++ = *src++;
}

// Identical read loop for flash and SRAM so the ratio means something.
__attribute__((noinline)) static uint32_t read_words(const volatile uint32_t *p,
                                                     uint32_t words, uint32_t passes)
{
    uint32_t sum = 0;
    for (uint32_t k = 0; k < passes; k++)
        for (uint32_t i = 0; i < words; i++) sum += p[i];
    return sum;
}

static void print_bw(const char *label, uint32_t bytes, uint32_t ticks)
{
    printf("  %s : %lu bytes in %lu us -> ", label, (unsigned long)bytes,
           (unsigned long)ticks_to_us(ticks));
    pr_ratio(bytes * TICKS_PER_US, ticks, 3);       // bytes per microsecond = MB/s
    printf(" MB/s, ");
    pr_cycles(ticks, bytes / 4u, 2);
    printf(" cyc/word");
}

static void bench_memory(void)
{
    section("MEMORY BENCHMARK (SRAM)");

    volatile uint32_t *buf = scratch;
    const uint32_t total = SCRATCH_BYTES * MEM_PASSES;
    printf("  Test region : %lu of %lu bytes of SRAM; the rest is .data/.bss and the\n",
           (unsigned long)SCRATCH_BYTES, (unsigned long)SRAM_BYTES);
    printf("                stack - state this code is itself running on top of.\n\n");

    uint32_t t0 = ticks_now();
    for (uint32_t p = 0; p < MEM_PASSES; p++)
        for (uint32_t i = 0; i < SCRATCH_WORDS; i++) buf[i] = i * 2654435761u;
    print_bw("SRAM write", total, ticks_since(t0));
    printf("\n");

    t0 = ticks_now();
    uint32_t acc = read_words(buf, SCRATCH_WORDS, MEM_PASSES);
    print_bw("SRAM read ", total, ticks_since(t0));
    printf(" (chk %08lx)\n", (unsigned long)acc);

    uint32_t half = SCRATCH_WORDS / 2u;
    for (uint32_t i = 0; i < half; i++) buf[i] = i ^ 0xC5C5C5C5u;
    t0 = ticks_now();
    for (uint32_t p = 0; p < MEM_PASSES; p++) copy_words(&buf[half], &buf[0], half);
    print_bw("SRAM copy ", half * 4u * MEM_PASSES, ticks_since(t0));
    printf("\n");
    uint32_t cmis = 0;
    for (uint32_t i = 0; i < half; i++) if (buf[half + i] != buf[i]) cmis++;
    report(cmis == 0, "SRAM copy buffer verify");

    run_pattern(buf, SCRATCH_WORDS, "address",    pat_address);
    run_pattern(buf, SCRATCH_WORDS, "~address",   pat_naddress);
    run_pattern(buf, SCRATCH_WORDS, "0xAAAAAAAA", pat_aa);
    run_pattern(buf, SCRATCH_WORDS, "0x55555555", pat_55);
    printf("  The write loop's constant multiply is expanded inline (no libgcc call), so\n");
    printf("  write and read are both memory-limited figures. The copy row is a\n");
    printf("  hand-written word loop, not a libc memcpy.\n");
}

// ---------------------------------------------------------------------------
// Flash
// ---------------------------------------------------------------------------
static uint8_t sweep_pattern_byte(uint32_t sector, uint32_t offset)
{
    return (uint8_t)(sector * 173u + offset * 31u + 7u);
}

static void sweep_pattern_page(uint32_t page, uint32_t words[FLASH_PAGE / 4u])
{
    for (uint32_t w = 0; w < FLASH_PAGE / 4u; w++) {
        uint32_t v = 0;
        for (uint32_t b = 0; b < 4u; b++)
            v |= (uint32_t)sweep_pattern_byte(page, w * 4u + b) << (8u * b);
        words[w] = v;
    }
}

static void bench_flash_read(void)
{
    section("FLASH BENCHMARK (read path)");

    const volatile uint32_t *fl = (const volatile uint32_t *)FLASH_BASE_ADDR;
    uint32_t fl_bytes = FLASH_BYTES * FLASH_READ_PASSES;
    uint32_t t0 = ticks_now();
    uint32_t fsum = read_words(fl, FLASH_BYTES / 4u, FLASH_READ_PASSES);
    uint32_t dt_fl = ticks_since(t0);
    print_bw("flash read", fl_bytes, dt_fl);
    printf(" (chk %08lx)\n", (unsigned long)fsum);

    uint32_t sr_bytes = SCRATCH_BYTES * SRAM_READ_PASSES;
    t0 = ticks_now();
    uint32_t ssum = read_words(scratch, SCRATCH_WORDS, SRAM_READ_PASSES);
    uint32_t dt_sr = ticks_since(t0);
    print_bw("SRAM read ", sr_bytes, dt_sr);
    printf(" (chk %08lx)\n", (unsigned long)ssum);

    uint32_t c_fl = dt_fl * 800u / (fl_bytes / 4u);      // cycles/word x100
    uint32_t c_sr = dt_sr * 800u / (sr_bytes / 4u);
    printf("  -> a word read from flash costs ");
    pr_ratio(c_fl, c_sr, 2);
    printf("x a word read from SRAM.\n");
    printf("  Both loops fetch instructions from flash too, so the flash row shares\n");
    printf("  the one flash port between data and instruction fetch.\n");
    report(fsum != 0, "flash read returned data");
    printf("  No flash cache/accelerator to switch: the only control is the\n");
    printf("  wait-state field (FLASH_ACTLR.LATENCY), which must be 1 at 48 MHz.\n");
}

extern unsigned char _data_lma, _data_vma, _edata;

static void bench_flash_sweep(void)
{
    section("FLASH SWEEP (page erase / program / verify / erase)");

    uint32_t image_end = (uint32_t)(uintptr_t)&_data_lma + (uint32_t)(&_edata - &_data_vma);
    uint32_t first = (image_end + FLASH_PAGE - 1u) / FLASH_PAGE;     // first page fully past the image
    uint32_t last = FLASH_BYTES / FLASH_PAGE;                        // exclusive
    uint32_t pages = last - first;
    uint32_t sweep_bytes = pages * FLASH_PAGE;

    printf("  Program image : %lu bytes (0x%04lx), ends in page %lu\n",
           (unsigned long)image_end, (unsigned long)image_end,
           (unsigned long)(image_end / FLASH_PAGE));
    printf("  Swept region  : pages %lu..%lu = 0x%04lx..0x%04lx, %lu pages, %lu bytes\n",
           (unsigned long)first, (unsigned long)(last - 1u),
           (unsigned long)(first * FLASH_PAGE), (unsigned long)(last * FLASH_PAGE - 1u),
           (unsigned long)pages, (unsigned long)sweep_bytes);
    report(first * FLASH_PAGE >= image_end,
           "sweep starts at the first 64-byte page fully past the program image");

    uint32_t hw_errs = 0, bad_words = 0, blank_bad = 0;
    uint32_t erase_ticks = 0, prog_ticks = 0, t;
    uint32_t pat[FLASH_PAGE / 4u];
    volatile uint32_t *fl = (volatile uint32_t *)FLASH_BASE_ADDR;

    flash_unlock();

    for (uint32_t pg = first; pg < last; pg++) {
        t = ticks_now();
        hw_errs += flash_erase_page(FLASH_BASE_ADDR + pg * FLASH_PAGE) != 0;
        erase_ticks += ticks_since(t);
    }
    for (uint32_t i = first * FLASH_PAGE / 4u; i < last * FLASH_PAGE / 4u; i++)
        if (fl[i] != 0xFFFFFFFFu) blank_bad++;
    report(blank_bad == 0, "every swept word reads 0xFFFFFFFF after erase");

    for (uint32_t pg = first; pg < last; pg++) {
        sweep_pattern_page(pg, pat);
        t = ticks_now();
        hw_errs += flash_program_page(FLASH_BASE_ADDR + pg * FLASH_PAGE, pat) != 0;
        prog_ticks += ticks_since(t);
    }
    for (uint32_t pg = first; pg < last; pg++) {
        sweep_pattern_page(pg, pat);
        for (uint32_t w = 0; w < FLASH_PAGE / 4u; w++)
            if (fl[pg * FLASH_PAGE / 4u + w] != pat[w]) bad_words++;
    }
    char line[64];
    snprintf(line, sizeof line, "verify %lu bytes against the pattern (%lu word mismatches)",
             (unsigned long)sweep_bytes, (unsigned long)bad_words);
    report(bad_words == 0, line);

    printf("  erase   : %lu bytes in %lu us -> ", (unsigned long)sweep_bytes,
           (unsigned long)ticks_to_us(erase_ticks));
    pr_ratio(sweep_bytes * TICKS_PER_US * 1000u, erase_ticks, 2);
    printf(" KB/s (");
    pr_ratio(erase_ticks, pages * TICKS_PER_US, 0);
    printf(" us/page)\n");
    printf("  program : %lu bytes in %lu us -> ", (unsigned long)sweep_bytes,
           (unsigned long)ticks_to_us(prog_ticks));
    pr_ratio(sweep_bytes * TICKS_PER_US * 1000u, prog_ticks, 2);
    printf(" KB/s (");
    pr_ratio(prog_ticks, pages * TICKS_PER_US, 0);
    printf(" us/page)\n");
    report(hw_errs == 0, "FLASH_STATR: no write-protect error, BSY always cleared");

    uint32_t cleanup_errs = 0, cleanup_bad = 0;
    for (uint32_t pg = first; pg < last; pg++)
        cleanup_errs += flash_erase_page(FLASH_BASE_ADDR + pg * FLASH_PAGE) != 0;
    for (uint32_t i = first * FLASH_PAGE / 4u; i < last * FLASH_PAGE / 4u; i++)
        if (fl[i] != 0xFFFFFFFFu) cleanup_bad++;
    flash_lock();
    report(cleanup_errs == 0 && cleanup_bad == 0,
           "cleanup erase: swept region restored to blank 0xFF");
    printf("  The CPU keeps executing from flash while the controller is busy\n");
    printf("  (fetches stall until BSY clears): no RAM-resident driver needed.\n");
}

// ---------------------------------------------------------------------------
int main(void)
{
    SystemInit();
    Delay_Ms(3000);                      // let the host attach to the debug channel
    ram_canary_paint();
    printf("\n\n");
    rule();
    printf("  CH32V003 BAREMETAL SELF-TEST / BENCHMARK (suite image)\n");
    rule();
    printf("BENCH_START\n");

    print_system_info("SYSTEM INFO");
    bench_cpu();
    bench_memory();
    bench_flash_read();
    bench_flash_sweep();

    section("SUMMARY");
    uint32_t untouched = ram_untouched_bytes();
    printf("  RAM never touched by the stack : %lu bytes of %lu\n",
           (unsigned long)untouched, (unsigned long)SRAM_BYTES);
    report(untouched > 64u, "SRAM headroom left over the whole run (canary intact)");
    if (g_fail_count == 0)
        printf("  ALL %u CHECKS PASSED - CPU, SRAM, flash read and flash sweep verified.\n",
               g_check_count);
    else
        printf("  %u of %u CHECK(S) FAILED - see FAIL lines above.\n", g_fail_count,
               g_check_count);
    printf("  CoreMark is a separate image (not run in this one).\n");
    rule();
    printf("\nSUITE_DONE failures=%u checks=%u\n", g_fail_count, g_check_count);
    for (;;) { __asm__ volatile("nop"); }
}
