// ATmega328P baremetal CPU / SRAM / flash / EEPROM self-test and benchmark.
//
// No Arduino core, no RTOS, no vendor HAL - avr-libc's register definitions
// and the ATmega328P datasheet, built with avr-gcc and flashed through the
// board's stock Optiboot bootloader.
//
// Covers:
//   CPU     integer ALU, avr-libc's software 32-bit divide, software
//           single-precision float (this part has no FPU), and CoreMark as a
//           portable baseline - with the caveat printed in that section
//   Memory  the 2 KB SRAM: bandwidth plus four address/pattern integrity
//           sweeps, over the largest window that leaves the suite room to run
//   Flash   sequential read throughput through LPM, against the same read out
//           of SRAM, which is the Harvard split measured directly
//   EEPROM  a full 1 KB erase / program / verify / erase sweep, standing in
//           for the flash sweep this part cannot do from application code
//
// Console is UART0 at 115200 through the board's USB-serial bridge.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "platform.h"

extern void   coremark_run(void);
extern double coremark_score;

// avr-libc linker symbols: end of the flash image, end of statically
// allocated SRAM.
extern char __data_load_end;
extern char __bss_end;

// Fixed constants so every run measures exactly the same amount of work.
#define INT_ITERS    20000000UL
#define FLOAT_ITERS   2000000UL
#define DIV_ITERS     5000000UL

// The memory test region: 1200 of the 2048 bytes of SRAM. The rest is the
// printf staging buffer, .data/.bss and the stack. This buffer is also
// CoreMark's working set - the two never run at the same time, and on a part
// with 2 KB of RAM total that sharing is the difference between the suite
// fitting and not. The RAM canary at the end of the run is what proves the
// budget held.
#define SCRATCH_BYTES 1200u
#define SCRATCH_WORDS (SCRATCH_BYTES / 4u)

uint8_t g_scratch[SCRATCH_BYTES];   // not static: core_portme.c hands it out

// Repeat counts, chosen so each timed region is long enough that the 0.5 us
// timebase is not the limiting factor.
#define MEM_PASSES         200u
#define FLASH_READ_PASSES    8u
#define SRAM_READ_PASSES   218u     // 218 * 1200 ~= 8 * 32768, so MB/s compare

// EEPROM has no sector structure - it is byte-erasable. The sweep pattern is
// still indexed by a 64-byte page and an offset within it, so the pattern
// depends on both and an address that aliases onto another still shows up.
#define EE_PAGE 64u

static uint8_t g_fail_count;

static void rule(void)
{
    PR("--------------------------------------------------------------\n");
}

static void section(const char *title_P)
{
    PR("\n");
    rule();
    PR("  %S\n", title_P);
    rule();
    led_toggle();
}

static void report(uint8_t ok, const char *what_P)
{
    if (!ok) g_fail_count++;
    PR("  [%s] %S\n", ok ? " OK " : "FAIL", what_P);
}

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

// No FPU on this part: every operation below is a call into avr-libc's
// __mulsf3 / __addsf3 / __gtsf2. There is no hardware path to compare it
// against, which is the whole finding.
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
    uint32_t acc = 0;
    uint32_t n = 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= iters; i++) {
        uint32_t d = (i & 0xFFFFu) | 1u;   // avoid div-by-zero
        acc += (n / d) + (n % d);
        n = n * 2654435761u + 1u;
    }
    return acc;
}

// Cycles per iteration, which on an 8-bit part says more than a rate does.
static uint32_t cycles_per_iter(uint32_t us, uint32_t iters)
{
    return (uint32_t)(((uint64_t)us * (F_CPU_HZ / 1000000UL)) / iters);
}

// ---------------------------------------------------------------------------

static void print_system_info(void)
{
    section(PSTR("SYSTEM INFO"));

    uint8_t s0 = boot_signature_byte_get(0x0000);
    uint8_t s2 = boot_signature_byte_get(0x0002);
    uint8_t s4 = boot_signature_byte_get(0x0004);
    uint8_t osccal = boot_signature_byte_get(0x0001);

    uint8_t lfuse = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    uint8_t hfuse = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    uint8_t efuse = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);

    uint16_t flash_used = (uint16_t)(uintptr_t)&__data_load_end;
    uint16_t sram_used  = (uint16_t)(uintptr_t)&__bss_end - 0x0100u;

    PR("  Target            : ATmega328P, Arduino Uno-compatible board\n");
    PR("  Device signature  : %02X %02X %02X  (expect 1E 95 0F)\n", s0, s2, s4);
    PR("  Fuses  L/H/E      : %02X / %02X / %02X   OSCCAL %02X\n",
       lfuse, hfuse, efuse, osccal);
    PR("  Console           : UART0 115200 8N1 via on-board USB-serial\n");
    PR("  Compiler          : GCC %s\n", __VERSION__);
    PR("  Core              : 1 x AVR (8-bit, Harvard, no FPU, no divider)\n");
    PR("  sys clock         : %lu Hz (16 MHz crystal, CKDIV8 off)\n",
       (unsigned long)F_CPU_HZ);
    PR("  timer clock       : %lu Hz (Timer1, /8, free running)\n",
       (unsigned long)TIMER_HZ);
    PR("  Flash             : %lu bytes (512 of them are the bootloader)\n",
       (unsigned long)FLASH_SIZE);
    PR("  SRAM              : %u bytes\n", SRAM_SIZE);
    PR("  EEPROM            : %u bytes\n", EEPROM_SIZE);
    PR("  this binary uses  : %u bytes flash, %u bytes static SRAM\n",
       flash_used, sram_used);

    // PORTING.md rule: verify one known value by eye before trusting a whole
    // log of them. printf float support is a link-time decision on AVR and
    // fails silently when it is wrong (see README).
    PR("  sizeof(double)    : %u bytes  (32-bit on this toolchain)\n",
       (unsigned)sizeof(double));
    PR("  printf float check: %.5f  (must read 3.14159)\n", 3.14159);
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
static void bench_cpu(void)
{
    section(PSTR("CPU BENCHMARK"));

    uint32_t chk_a = integer_workload(1000000UL);
    uint32_t chk_b = integer_workload(1000000UL);
    report(chk_a == chk_b,
           PSTR("integer ALU determinism check (repeat-run checksum match)"));

    uint64_t t0 = time_us_64();
    uint32_t ichk = integer_workload(INT_ITERS);
    uint64_t t1 = time_us_64();
    uint32_t dt = (uint32_t)(t1 - t0);
    PR("  integer mix   : %lu iters in %lu us\n", INT_ITERS, dt);
    PR("                  -> %.4f Mops/s, %lu cyc/iter, checksum %08lx\n",
       (double)INT_ITERS / (double)dt, cycles_per_iter(dt, INT_ITERS),
       (unsigned long)ichk);
    report(ichk == 0xf2c53d7bUL,
           PSTR("integer checksum matches the cross-chip reference f2c53d7b"));

    t0 = time_us_64();
    uint32_t dchk = divider_workload(DIV_ITERS);
    t1 = time_us_64();
    dt = (uint32_t)(t1 - t0);
    PR("  divide (soft) : %lu iters in %lu us\n", DIV_ITERS, dt);
    PR("                  -> %.4f Mops/s, %lu cyc/iter, checksum %08lx\n",
       (double)DIV_ITERS / (double)dt, cycles_per_iter(dt, DIV_ITERS),
       (unsigned long)dchk);
    PR("                  AVR has no divide instruction: this is avr-libc's\n");
    PR("                  __udivmodsi4, not a hardware divider\n");

    PR("\n  -- floating point: software only, this part has no FPU --\n");
    t0 = time_us_64();
    float fchk = float_workload(FLOAT_ITERS);
    t1 = time_us_64();
    dt = (uint32_t)(t1 - t0);
    PR("  float  (soft) : %lu iters in %lu us\n", FLOAT_ITERS, dt);
    PR("                  -> %.4f Mops/s, %lu cyc/iter, result %.6f\n",
       (double)FLOAT_ITERS / (double)dt, cycles_per_iter(dt, FLOAT_ITERS),
       (double)fchk);
    report(fchk > 0.0f, PSTR("float workload produced a sane result"));

    PR("\n  -- note: no double-precision path to compare against --\n");
    PR("  The reference suite measures a hardware FPU against software\n");
    PR("  double precision. This chip has no FPU at all, and avr-gcc makes\n");
    PR("  double an alias for 32-bit float, so there is no second path to\n");
    PR("  measure. The float row above IS the software row.\n");

    PR("\n  -- note: single core --\n");
    PR("  One AVR core, so there is no multi-core scaling to measure.\n");
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
static uint32_t pat_address(uint32_t i)  { return i; }
static uint32_t pat_naddress(uint32_t i) { return ~i; }
static uint32_t pat_aa(uint32_t i)       { (void)i; return 0xAAAAAAAAu; }
static uint32_t pat_55(uint32_t i)       { (void)i; return 0x55555555u; }

static void run_pattern(volatile uint32_t *buf, uint16_t words,
                        const char *name_P, uint32_t (*fn)(uint32_t))
{
    for (uint16_t i = 0; i < words; i++) buf[i] = fn(i);
    uint16_t mismatches = 0;
    for (uint16_t i = 0; i < words; i++) {
        if (buf[i] != fn(i)) mismatches++;
    }
    if (mismatches) g_fail_count++;
    PR("  [%s] SRAM pattern '%S' (%u mismatches)\n",
       mismatches == 0 ? " OK " : "FAIL", name_P, mismatches);
}

static void bench_memory(void)
{
    section(PSTR("MEMORY BENCHMARK (SRAM)"));

    volatile uint32_t *buf = (volatile uint32_t *)g_scratch;
    const uint16_t words = SCRATCH_WORDS;

    PR("  Test region : %u of %u bytes of SRAM. The rest is .data/.bss, the\n",
       SCRATCH_BYTES, SRAM_SIZE);
    PR("                printf staging buffer and the stack - state this code\n");
    PR("                is itself running on top of.\n\n");

    uint32_t total = (uint32_t)SCRATCH_BYTES * MEM_PASSES;

    uint64_t t0 = time_us_64();
    for (uint16_t p = 0; p < MEM_PASSES; p++)
        for (uint16_t i = 0; i < words; i++) buf[i] = (uint32_t)i * 2654435761u;
    uint64_t t1 = time_us_64();
    uint32_t dt = (uint32_t)(t1 - t0);
    PR("  SRAM write : %lu bytes in %lu us -> %.3f MB/s\n",
       total, dt, (double)total / (double)dt);

    volatile uint32_t sink;
    t0 = time_us_64();
    uint32_t acc = 0;
    for (uint16_t p = 0; p < MEM_PASSES; p++)
        for (uint16_t i = 0; i < words; i++) acc += buf[i];
    t1 = time_us_64();
    sink = acc; (void)sink;
    dt = (uint32_t)(t1 - t0);
    PR("  SRAM read  : %lu bytes in %lu us -> %.3f MB/s\n",
       total, dt, (double)total / (double)dt);

    uint16_t half = words / 2u;
    uint16_t half_bytes = half * 4u;
    for (uint16_t i = 0; i < half; i++) buf[i] = (uint32_t)i ^ 0xC5C5C5C5u;
    uint32_t copy_total = (uint32_t)half_bytes * MEM_PASSES;
    t0 = time_us_64();
    for (uint16_t p = 0; p < MEM_PASSES; p++)
        memcpy((void *)&buf[half], (const void *)&buf[0], half_bytes);
    t1 = time_us_64();
    dt = (uint32_t)(t1 - t0);
    PR("  SRAM copy  : %lu bytes in %lu us -> %.3f MB/s\n",
       copy_total, dt, (double)copy_total / (double)dt);

    report(memcmp((const void *)&buf[half], (const void *)&buf[0],
                  half_bytes) == 0, PSTR("SRAM copy buffer verify"));

    run_pattern(buf, words, PSTR("address"),    pat_address);
    run_pattern(buf, words, PSTR("~address"),   pat_naddress);
    run_pattern(buf, words, PSTR("0xAAAAAAAA"), pat_aa);
    run_pattern(buf, words, PSTR("0x55555555"), pat_55);

    PR("\n  The write loop's 32-bit multiply does not survive -O2: GCC\n");
    PR("  strength-reduces i*K into a running add, so the emitted loop is\n");
    PR("  four st Z+ and one 32-bit add. Write and read are both honest\n");
    PR("  memory figures here. Copy is avr-libc's memcpy, a byte loop, and\n");
    PR("  says more about the library than about the silicon.\n");
}

// ---------------------------------------------------------------------------
// Flash and EEPROM
// ---------------------------------------------------------------------------
static uint8_t sweep_pattern_byte(uint32_t sector, uint32_t offset)
{
    return (uint8_t)(sector * 173u + offset * 31u + 7u);
}

static void bench_flash(void)
{
    section(PSTR("FLASH BENCHMARK (read path)"));

    uint32_t flash_bytes = FLASH_SIZE * (uint32_t)FLASH_READ_PASSES;
    uint32_t sum = 0;
    uint64_t t0 = time_us_64();
    for (uint8_t p = 0; p < FLASH_READ_PASSES; p++)
        for (uint16_t a = 0; a < FLASH_SIZE; a++)
            sum += pgm_read_byte(a);
    uint64_t t1 = time_us_64();
    uint32_t dt_flash = (uint32_t)(t1 - t0);
    double mbs_flash = (double)flash_bytes / (double)dt_flash;
    PR("  flash read (LPM) : %lu bytes in %lu us -> %.3f MB/s\n",
       flash_bytes, dt_flash, mbs_flash);
    PR("                     checksum %08lx over %u passes\n",
       (unsigned long)sum, FLASH_READ_PASSES);

    // Same loop shape out of SRAM. LPM is 3 cycles, LD is 2, and the gap
    // between these two rows is the Harvard split measured rather than quoted.
    uint32_t sram_bytes = (uint32_t)SCRATCH_BYTES * SRAM_READ_PASSES;
    volatile uint8_t *sp = (volatile uint8_t *)g_scratch;
    uint32_t sum2 = 0;
    t0 = time_us_64();
    for (uint16_t p = 0; p < SRAM_READ_PASSES; p++)
        for (uint16_t a = 0; a < SCRATCH_BYTES; a++)
            sum2 += sp[a];
    t1 = time_us_64();
    uint32_t dt_sram = (uint32_t)(t1 - t0);
    double mbs_sram = (double)sram_bytes / (double)dt_sram;
    PR("  SRAM read (LD)   : %lu bytes in %lu us -> %.3f MB/s\n",
       sram_bytes, dt_sram, mbs_sram);
    // Printing this checksum is not decoration: without a live use, GCC drops
    // the 32-bit accumulate out of the loop while the volatile loads stay,
    // and the SRAM side gets four cycles a byte cheaper than the flash side.
    // The two loops have to be the same shape for the ratio to mean anything.
    PR("                     checksum %08lx over %u passes\n",
       (unsigned long)sum2, SRAM_READ_PASSES);
    PR("  -> byte reads from flash cost %.2fx what they cost from SRAM.\n",
       mbs_sram / mbs_flash);
    PR("     LPM is 3 cycles against LD's 2; the rest of the gap is the\n");
    PR("     address setup a program-memory read needs and a post-\n");
    PR("     incrementing data pointer does not. This part is Harvard:\n");
    PR("     flash is a separate address space and a plain C pointer\n");
    PR("     cannot see it at all.\n");
    report(sum != 0, PSTR("flash read returned data"));

    PR("\n  -- flash program/erase: not possible from application code --\n");
    PR("  SPM only executes from the boot loader section (datasheet 27.4).\n");
    PR("  This board runs stock Optiboot 4.4, which exposes no SPM entry\n");
    PR("  point, so an application-space erase/program sweep cannot be done\n");
    PR("  without replacing the bootloader - which needs an ISP programmer\n");
    PR("  this suite does not assume. Substituted below: the 1 KB EEPROM,\n");
    PR("  the on-chip non-volatile memory this part CAN write from here.\n");
}

static void bench_eeprom(void)
{
    section(PSTR("EEPROM SWEEP (substitutes for the flash sweep)"));

    uint8_t  err = 0;
    uint16_t bad = 0;

    // --- erase the whole array. Erase-only is a distinct 1.8 ms operation on
    // this part, so erase and program can be timed independently.
    uint64_t t0 = time_us_64();
    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        err |= ee_program_byte(a, 0xFF, EE_MODE_ERASE);
    err |= ee_wait_idle();
    uint64_t t1 = time_us_64();
    uint32_t erase_us = (uint32_t)(t1 - t0);

    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        if (ee_read_byte(a) != 0xFF) bad++;
    report(bad == 0, PSTR("all 1024 bytes read 0xFF after erase"));

    // --- program the sweep pattern
    t0 = time_us_64();
    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        err |= ee_program_byte(a, sweep_pattern_byte(a / EE_PAGE, a % EE_PAGE),
                               EE_MODE_WRITE);
    err |= ee_wait_idle();
    t1 = time_us_64();
    uint32_t prog_us = (uint32_t)(t1 - t0);

    bad = 0;
    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        if (ee_read_byte(a) != sweep_pattern_byte(a / EE_PAGE, a % EE_PAGE))
            bad++;
    if (bad == 0)
        report(1, PSTR("verify all 1024 bytes against pattern - 0 mismatches"));
    else {
        g_fail_count++;
        PR("  [FAIL] verify: %u bytes mismatched\n", bad);
    }

    // --- read throughput
    uint32_t rd_total = (uint32_t)EEPROM_SIZE * 64UL;
    uint32_t rsum = 0;
    t0 = time_us_64();
    for (uint8_t p = 0; p < 64; p++)
        for (uint16_t a = 0; a < EEPROM_SIZE; a++) rsum += ee_read_byte(a);
    t1 = time_us_64();
    uint32_t read_us = (uint32_t)(t1 - t0);

    PR("  erase   : %u bytes in %lu us -> %.2f KB/s (%lu us/byte)\n",
       EEPROM_SIZE, erase_us,
       (double)EEPROM_SIZE * 1000.0 / (double)erase_us,
       erase_us / (uint32_t)EEPROM_SIZE);
    PR("  program : %u bytes in %lu us -> %.2f KB/s (%lu us/byte)\n",
       EEPROM_SIZE, prog_us,
       (double)EEPROM_SIZE * 1000.0 / (double)prog_us,
       prog_us / (uint32_t)EEPROM_SIZE);
    PR("  read    : %lu bytes in %lu us -> %.3f MB/s (chk %08lx)\n",
       rd_total, read_us, (double)rd_total / (double)read_us,
       (unsigned long)rsum);

    report(err == 0, PSTR("EEPE cleared inside the guard time on every write"));
    PR("  This part's EEPROM controller has no error status register, so\n");
    PR("  'no errors' means EEPE cleared in time and every byte read back.\n");

    // --- cleanup: leave the array blank, as the flash sweep would
    err = 0;
    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        err |= ee_program_byte(a, 0xFF, EE_MODE_ERASE);
    err |= ee_wait_idle();
    bad = 0;
    for (uint16_t a = 0; a < EEPROM_SIZE; a++)
        if (ee_read_byte(a) != 0xFF) bad++;
    report(err == 0 && bad == 0,
           PSTR("cleanup erase (EEPROM restored to blank 0xFF)"));
}

// ---------------------------------------------------------------------------
// CoreMark
// ---------------------------------------------------------------------------
static void bench_coremark(void)
{
    section(PSTR("CPU BASELINE: CoreMark - REDUCED DATASET, SEE WARNING"));

    PR("  WARNING: this is NOT a conformant CoreMark score.\n");
    PR("  A conformant run needs a 2000-byte working set and this chip has\n");
    PR("  2048 bytes of SRAM in total. Measured rather than assumed: built\n");
    PR("  standalone, with no suite around it, CoreMark's own peak stack on\n");
    PR("  this part is 265 bytes. 2000 + 265 = 2265 bytes before a single\n");
    PR("  byte of harness - 217 more than the part has. Built anyway,\n");
    PR("  avr-gcc links it without complaint at 116.6%% of SRAM, and the\n");
    PR("  chip then prints nothing at all: .bss ends past RAMEND and the\n");
    PR("  stack is inside the data block before main() runs.\n");
    PR("  What follows is the 1200-byte profile-run configuration, which\n");
    PR("  CoreMark still validates against its own known CRCs, so the\n");
    PR("  arithmetic is proven correct - but the dataset is smaller than\n");
    PR("  the reporting rules require, so the score below is NOT comparable\n");
    PR("  with published CoreMark 1.0 numbers, nor with the 2000-byte runs\n");
    PR("  from other chips. Read it as this chip's own figure, and only\n");
    PR("  ever next to that caveat.\n\n");
    PR("  Running (self-calibrates to a ~10s measured run)...\n");

    uint64_t t0 = time_us_64();
    coremark_run();
    uint64_t t1 = time_us_64();

    double wall = (double)(uint32_t)(t1 - t0) / 1.0e6;
    double mhz  = (double)F_CPU_HZ / 1.0e6;
    PR("\n  wall time        : %.2f s (includes self-calibration passes)\n",
       wall);
    if (coremark_score > 0.0) {
        PR("  iterations/sec   : %.3f at %.1f MHz\n", coremark_score, mhz);
        PR("  per MHz          : %.4f  (reduced dataset - not a CoreMark/MHz\n",
           coremark_score / mhz);
        PR("                     figure in the reportable sense)\n");
    }
    report(coremark_score > 0.0, PSTR("CoreMark completed and reported a score"));
}

// ---------------------------------------------------------------------------

int main(void)
{
    led_init();
    led_on();
    console_init();
    timer_init();
    sei();

    delay_ms(300);

    PR("\n\n");
    rule();
    PR("  ATmega328P BAREMETAL SELF-TEST / BENCHMARK\n");
    rule();

    print_system_info();
    bench_cpu();
    bench_memory();
    bench_flash();
    bench_eeprom();
    bench_coremark();

    section(PSTR("SUMMARY"));
    uint16_t untouched = ram_untouched_bytes();
    PR("  RAM never touched by the stack : %u bytes of %u\n",
       untouched, SRAM_SIZE);
    report(untouched > 64,
           PSTR("SRAM headroom left over the whole run (canary intact)"));

    if (g_fail_count == 0)
        PR("  ALL CHECKS PASSED - CPU, SRAM, flash read and EEPROM verified.\n");
    else
        PR("  %u CHECK(S) FAILED - see FAIL lines above.\n", g_fail_count);
    rule();
    PR("\ndone.\n");

    // One LED on this board, so: slow heartbeat means finished and clean,
    // fast blink means at least one check failed.
    uint16_t period = (g_fail_count == 0) ? 500 : 100;
    for (;;) {
        led_toggle();
        delay_ms(period);
    }
}
