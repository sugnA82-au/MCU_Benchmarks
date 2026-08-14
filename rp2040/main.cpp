// RP2040 baremetal CPU / SRAM / Flash self-test and benchmark.
//
// No RTOS. Runs directly on pico-sdk's minimal runtime (crt0 + SDK HAL),
// talks over USB CDC so it works on any RP2040 board regardless of which
// GPIO (if any) is wired to a UART.

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/sio.h"

// Provided by the pico-sdk linker script; must stay at global scope so it
// binds to the real linker symbol rather than getting internal linkage from
// the anonymous namespace below.
extern uint32_t __flash_binary_end;

// CoreMark's core_main.c is compiled with "main" renamed to "coremark_run"
// (see coremark/core_portme.h) so it can be called from our own main().
// coremark_score is set by our small hook in coremark/core_main.c.
extern "C" void coremark_run(void);
extern "C" double coremark_score;

namespace {

// Fixed RP2040 memory map (datasheet, not board-specific).
constexpr uint32_t kSramBase = 0x20000000u;
constexpr uint32_t kXipBase  = 0x10000000u;
constexpr uint32_t kSysinfoChipIdAddr = 0x40000000u;

constexpr uint32_t kIntIters   = 20'000'000u;
constexpr uint32_t kFloatIters = 2'000'000u;
constexpr uint32_t kDivIters   = 5'000'000u;

// 230 KiB of the chip's 264 KiB SRAM: as much as can safely be claimed as a
// static test buffer while leaving headroom for both cores' stacks, TinyUSB
// CDC buffers, and other runtime statics -- that remainder is actively in
// use by the code doing the testing, so it can't be tested by it.
constexpr size_t kMemTestWords = 230 * 1024 / sizeof(uint32_t);
constexpr size_t kMemTestBytes = kMemTestWords * sizeof(uint32_t);
constexpr size_t kSramTotalBytes = 264 * 1024;

volatile uint32_t g_membuf[kMemTestWords];

volatile uint64_t g_core1_checksum = 0;
volatile uint64_t g_core1_us = 0;

int g_fail_count = 0;

void rule() { printf("--------------------------------------------------------------\n"); }

void section(const char *title) {
    printf("\n");
    rule();
    printf("  %s\n", title);
    rule();
}

void report(bool ok, const char *what) {
    if (!ok) g_fail_count++;
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

// ---------------------------------------------------------------------------
// CPU workload: deterministic integer mix (mul/add/xor/shift). noinline so
// core0 and core1 get identical codegen for a fair comparison, and the
// return value is used so the loop can't be optimised away.
// ---------------------------------------------------------------------------
__attribute__((noinline)) uint32_t integer_workload(uint32_t iters) {
    // Optimisation barrier: without this, GCC's IPA-pure-const pass proves
    // this function is pure and silently reuses the result of an earlier
    // call made with the same `iters`, instead of re-executing the loop.
    asm volatile("" ::: "memory");
    uint32_t a = 0x12345678u, b = 0x9ABCDEF0u;
    for (uint32_t i = 0; i < iters; i++) {
        a = (a * 1664525u + 1013904223u) ^ b;
        b += (a << 3) ^ (a >> 5) ^ 0xA5A5A5A5u;
    }
    return a ^ b;
}

__attribute__((noinline)) float float_workload(uint32_t iters) {
    asm volatile("" ::: "memory");
    float f = 1.0f;
    const float k1 = 1.0000001f, k2 = 0.0000001f;
    for (uint32_t i = 0; i < iters; i++) {
        f = f * k1 + k2;
        if (f > 1.0e6f) f *= 1.0e-6f;
    }
    return f;
}

__attribute__((noinline)) uint32_t divider_workload(uint32_t iters) {
    asm volatile("" ::: "memory");
    uint32_t acc = 0;
    uint32_t n = 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= iters; i++) {
        uint32_t d = (i & 0xFFFFu) | 1u;   // avoid div-by-zero
        acc += (n / d) + (n % d);
        n = n * 2654435761u + 1u;
    }
    return acc;
}

void core1_entry() {
    // Signal "started" immediately so core0 can launch its own workload
    // right away instead of waiting for us to finish -- that's what makes
    // this an actual concurrent dual-core test rather than two sequential
    // runs.
    multicore_fifo_push_blocking(0xC0FFEEu);

    uint64_t t0 = time_us_64();
    uint32_t chk = integer_workload(kIntIters);
    uint64_t t1 = time_us_64();

    g_core1_checksum = chk;
    g_core1_us = t1 - t0;

    // Hand results back, then register as a lockout victim so core0 can
    // safely park us in RAM during flash erase/program later.
    multicore_fifo_push_blocking(0xBEEFu);
    multicore_lockout_victim_init();
    while (true) {
        tight_loop_contents();
    }
}

// ---------------------------------------------------------------------------

void print_system_info() {
    section("SYSTEM INFO");

    uint32_t chip_id = *reinterpret_cast<volatile uint32_t *>(kSysinfoChipIdAddr);
    uint32_t manufacturer = chip_id & 0xFFFu;
    uint32_t part = (chip_id >> 12) & 0xFFFFu;
    uint32_t revision = (chip_id >> 28) & 0xFu;

    printf("  SDK version      : %s\n", PICO_SDK_VERSION_STRING);
    printf("  Compiler          : %s\n", __VERSION__);
    printf("  Chip ID (SYSINFO) : 0x%08lx (mfr 0x%03lx, part 0x%04lx, rev %lu)\n",
           (unsigned long)chip_id, (unsigned long)manufacturer,
           (unsigned long)part, (unsigned long)revision);
    printf("  Cores             : 2 x Cortex-M0+ (this core = cpuid %lu)\n",
           (unsigned long)sio_hw->cpuid);
    printf("  sys clock         : %.3f MHz\n", clock_get_hz(clk_sys) / 1.0e6);
    printf("  usb clock         : %.3f MHz\n", clock_get_hz(clk_usb) / 1.0e6);
    printf("  Flash size        : %u KiB (configured)\n", PICO_FLASH_SIZE_BYTES / 1024);
    printf("  SRAM (total)      : 264 KiB\n");
}

// ---------------------------------------------------------------------------
// CPU benchmark
// ---------------------------------------------------------------------------
void bench_cpu() {
    section("CPU BENCHMARK");

    // Determinism / correctness check: same workload run twice must produce
    // an identical checksum.
    uint32_t chk_a = integer_workload(1'000'000u);
    uint32_t chk_b = integer_workload(1'000'000u);
    report(chk_a == chk_b, "integer ALU determinism check (repeat-run checksum match)");

    printf("\n  -- single-core (core0) --\n");
    uint64_t t0 = time_us_64();
    uint32_t ichk = integer_workload(kIntIters);
    uint64_t t1 = time_us_64();
    double int_mops = (double)kIntIters / (double)(t1 - t0);
    printf("  integer mix   : %10lu iters in %8llu us  -> %7.2f Mops/s  (checksum %08lx)\n",
           (unsigned long)kIntIters, (unsigned long long)(t1 - t0), int_mops,
           (unsigned long)ichk);

    t0 = time_us_64();
    uint32_t dchk = divider_workload(kDivIters);
    t1 = time_us_64();
    double div_mops = (double)kDivIters / (double)(t1 - t0);
    printf("  hw divider    : %10lu iters in %8llu us  -> %7.2f Mops/s  (checksum %08lx)\n",
           (unsigned long)kDivIters, (unsigned long long)(t1 - t0), div_mops,
           (unsigned long)dchk);

    t0 = time_us_64();
    float fchk = float_workload(kFloatIters);
    t1 = time_us_64();
    double flt_mops = (double)kFloatIters / (double)(t1 - t0);
    printf("  soft-float    : %10lu iters in %8llu us  -> %7.2f Mops/s  (result %.6f, no FPU)\n",
           (unsigned long)kFloatIters, (unsigned long long)(t1 - t0), flt_mops,
           (double)fchk);

    printf("\n  -- dual-core scaling (core0 + core1 concurrently) --\n");
    multicore_launch_core1(core1_entry);
    uint32_t tok = multicore_fifo_pop_blocking();
    report(tok == 0xC0FFEEu, "core1 launch handshake");

    t0 = time_us_64();
    uint32_t ichk2 = integer_workload(kIntIters);
    t1 = time_us_64();
    uint64_t core0_us = t1 - t0;

    // Wait for core1 to actually finish and publish its results.
    uint32_t tok2 = multicore_fifo_pop_blocking();
    report(tok2 == 0xBEEFu, "core1 completion handshake");

    uint64_t wall_us = (core0_us > g_core1_us) ? core0_us : g_core1_us;
    double combined_mops = 2.0 * (double)kIntIters / (double)wall_us;
    printf("  core0: %8llu us   core1: %8llu us   combined: %7.2f Mops/s (%.2fx single-core)\n",
           (unsigned long long)core0_us, (unsigned long long)g_core1_us,
           combined_mops, combined_mops / int_mops);
    report(ichk2 == ichk, "core0 workload result stable under concurrent core1 load");
    report(g_core1_checksum == ichk, "core1 produced identical checksum to core0 (same workload)");
}

// ---------------------------------------------------------------------------
// Memory (SRAM) benchmark + integrity test
// ---------------------------------------------------------------------------
void bench_memory() {
    section("MEMORY (SRAM) BENCHMARK");
    printf("  Test region: %u KiB of %u KiB total chip SRAM (remainder is\n"
           "  live stack/USB/runtime state and can't be tested from code\n"
           "  running on top of it)\n",
           (unsigned)(kMemTestBytes / 1024), (unsigned)(kSramTotalBytes / 1024));

    // --- bandwidth: write ---
    uint64_t t0 = time_us_64();
    for (size_t i = 0; i < kMemTestWords; i++) g_membuf[i] = (uint32_t)i * 2654435761u;
    uint64_t t1 = time_us_64();
    double write_mbs = (double)kMemTestBytes / (double)(t1 - t0);
    printf("  write : %6u KiB in %7llu us -> %8.1f MB/s\n",
           (unsigned)(kMemTestBytes / 1024), (unsigned long long)(t1 - t0), write_mbs);

    // --- bandwidth: read ---
    volatile uint32_t sink = 0;
    t0 = time_us_64();
    uint32_t acc = 0;
    for (size_t i = 0; i < kMemTestWords; i++) acc += g_membuf[i];
    t1 = time_us_64();
    sink = acc;
    (void)sink;
    double read_mbs = (double)kMemTestBytes / (double)(t1 - t0);
    printf("  read  : %6u KiB in %7llu us -> %8.1f MB/s\n",
           (unsigned)(kMemTestBytes / 1024), (unsigned long long)(t1 - t0), read_mbs);

    // --- bandwidth: copy (first half of the test buffer -> second half) ---
    size_t half_words = kMemTestWords / 2;
    size_t half_bytes = half_words * sizeof(uint32_t);
    for (size_t i = 0; i < half_words; i++) g_membuf[i] = (uint32_t)i ^ 0xC5C5C5C5u;
    t0 = time_us_64();
    memcpy((void *)&g_membuf[half_words], (const void *)&g_membuf[0], half_bytes);
    t1 = time_us_64();
    double copy_mbs = (double)half_bytes / (double)(t1 - t0);
    printf("  copy  : %6u KiB in %7llu us -> %8.1f MB/s\n",
           (unsigned)(half_bytes / 1024), (unsigned long long)(t1 - t0), copy_mbs);
    bool copy_ok = memcmp((const void *)&g_membuf[half_words], (const void *)&g_membuf[0],
                           half_bytes) == 0;
    report(copy_ok, "copy buffer verify");

    // --- integrity: pattern tests over the whole test region ---
    printf("\n  pattern integrity tests (%u KiB region):\n", (unsigned)(kMemTestBytes / 1024));

    auto run_pattern = [](const char *name, uint32_t (*pattern_fn)(uint32_t)) {
        for (size_t i = 0; i < kMemTestWords; i++) g_membuf[i] = pattern_fn((uint32_t)i);
        size_t mismatches = 0;
        for (size_t i = 0; i < kMemTestWords; i++) {
            if (g_membuf[i] != pattern_fn((uint32_t)i)) mismatches++;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "pattern '%s' (%u mismatches)", name, (unsigned)mismatches);
        report(mismatches == 0, msg);
    };

    run_pattern("address",  [](uint32_t i) -> uint32_t { return i; });
    run_pattern("~address", [](uint32_t i) -> uint32_t { return ~i; });
    run_pattern("0xAAAAAAAA", [](uint32_t) -> uint32_t { return 0xAAAAAAAAu; });
    run_pattern("0x55555555", [](uint32_t) -> uint32_t { return 0x55555555u; });
}

// ---------------------------------------------------------------------------
// Flash benchmark: XIP read bandwidth (cached vs streaming), then a full
// sweep that erases/programs/verifies every spare sector on the chip (all
// flash except the ~40 KiB this program itself occupies, which obviously
// can't be erased while it's the code doing the erasing). Done safely with
// core1 parked via flash_safe_execute (interrupts off, core1 in RAM stub).
// ---------------------------------------------------------------------------
struct FlashSweepParams {
    uint32_t start_offset;
    uint32_t num_sectors;
    uint64_t erase_us;
    uint64_t program_us;
};

// Depends on both sector index and in-sector offset so that a fault which
// aliases one address onto another (not just a stuck bit) will show up as a
// mismatch during verify.
uint8_t sweep_pattern_byte(uint32_t sector_index, uint32_t offset) {
    return (uint8_t)(sector_index * 173u + offset * 31u + 7u);
}

void flash_sweep_program_cb(void *param) {
    auto *p = static_cast<FlashSweepParams *>(param);
    static uint8_t buf[FLASH_SECTOR_SIZE];
    uint64_t erase_total = 0, program_total = 0;
    for (uint32_t s = 0; s < p->num_sectors; s++) {
        uint32_t offset = p->start_offset + s * FLASH_SECTOR_SIZE;
        for (uint32_t j = 0; j < FLASH_SECTOR_SIZE; j++) buf[j] = sweep_pattern_byte(s, j);

        uint64_t t0 = time_us_64();
        flash_range_erase(offset, FLASH_SECTOR_SIZE);
        uint64_t t1 = time_us_64();
        flash_range_program(offset, buf, FLASH_SECTOR_SIZE);
        uint64_t t2 = time_us_64();

        erase_total += (t1 - t0);
        program_total += (t2 - t1);
    }
    p->erase_us = erase_total;
    p->program_us = program_total;
}

void flash_sweep_erase_cb(void *param) {
    auto *p = static_cast<FlashSweepParams *>(param);
    for (uint32_t s = 0; s < p->num_sectors; s++) {
        uint32_t offset = p->start_offset + s * FLASH_SECTOR_SIZE;
        flash_range_erase(offset, FLASH_SECTOR_SIZE);
    }
}

void bench_flash() {
    section("FLASH BENCHMARK");

    uint32_t binary_end_offset = (uint32_t)&__flash_binary_end - kXipBase;

    // --- cached read: repeatedly re-read a small block already resident in
    // the 16 KiB XIP cache (our own code). ---
    const volatile uint8_t *code_ptr =
        reinterpret_cast<const volatile uint8_t *>(kXipBase + 0x100);
    constexpr size_t kCachedBlock = 4096;
    constexpr int kCachedPasses = 200;
    uint32_t csum = 0;
    uint64_t t0 = time_us_64();
    for (int pass = 0; pass < kCachedPasses; pass++) {
        for (size_t i = 0; i < kCachedBlock; i++) csum += code_ptr[i];
    }
    uint64_t t1 = time_us_64();
    size_t cached_bytes = kCachedBlock * kCachedPasses;
    double cached_mbs = (double)cached_bytes / (double)(t1 - t0);
    printf("  XIP cached read    : %6u KiB (repeat) in %8llu us -> %8.1f MB/s  (chk %08lx)\n",
           (unsigned)(cached_bytes / 1024), (unsigned long long)(t1 - t0), cached_mbs,
           (unsigned long)csum);

    // --- streaming read: quick bandwidth sample past our own code, read
    // once sequentially -> mostly cache misses, reflects real flash
    // throughput. That region is unprogrammed (erased/0xFF) flash at this
    // point -- reading it is always safe and never alters flash state.
    uint32_t stream_start = ((binary_end_offset + FLASH_SECTOR_SIZE - 1) /
                              FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    size_t max_stream = (PICO_FLASH_SIZE_BYTES > stream_start)
                             ? (PICO_FLASH_SIZE_BYTES - stream_start)
                             : 0;
    size_t stream_bytes = 512 * 1024;
    if (stream_bytes > max_stream) stream_bytes = max_stream;

    const volatile uint8_t *stream_ptr =
        reinterpret_cast<const volatile uint8_t *>(kXipBase + stream_start);
    uint32_t ssum = 0;
    t0 = time_us_64();
    for (size_t i = 0; i < stream_bytes; i++) ssum += stream_ptr[i];
    t1 = time_us_64();
    double stream_mbs = (double)stream_bytes / (double)(t1 - t0);
    printf("  XIP streaming read : %6u KiB (once)   in %8llu us -> %8.1f MB/s  (chk %08lx)\n",
           (unsigned)(stream_bytes / 1024), (unsigned long long)(t1 - t0), stream_mbs,
           (unsigned long)ssum);

    // --- full-chip sweep: erase + program + verify every spare sector,
    // i.e. all of flash except the region this program itself occupies
    // (which can't be erased out from under the code that's running it).
    // This is the real "make sure it's all accessible and writable" test.
    uint32_t sweep_start = stream_start;
    uint32_t num_sectors = (PICO_FLASH_SIZE_BYTES - sweep_start) / FLASH_SECTOR_SIZE;
    uint32_t sweep_bytes = num_sectors * FLASH_SECTOR_SIZE;

    printf("\n  full-chip erase/program/verify sweep: %lu sectors, %lu KiB"
           " (offset 0x%06lx-0x%06lx)\n",
           (unsigned long)num_sectors, (unsigned long)(sweep_bytes / 1024),
           (unsigned long)sweep_start, (unsigned long)PICO_FLASH_SIZE_BYTES);
    printf("  (own program occupies the first %lu KiB, left untouched)\n",
           (unsigned long)((sweep_start + 1023) / 1024));

    FlashSweepParams sweep{sweep_start, num_sectors, 0, 0};

    stdio_flush();
    sleep_ms(20);
    int rc = flash_safe_execute(flash_sweep_program_cb, &sweep, 8000);
    report(rc == PICO_OK, "flash_safe_execute full-chip erase+program (core1 parked safely)");

    uint32_t bad_sectors = 0;
    uint32_t first_bad_sector = 0xFFFFFFFFu;
    uint64_t bad_bytes = 0;
    for (uint32_t s = 0; s < num_sectors; s++) {
        uint32_t offset = sweep_start + s * FLASH_SECTOR_SIZE;
        const volatile uint8_t *vp =
            reinterpret_cast<const volatile uint8_t *>(kXipBase + offset);
        bool sector_ok = true;
        for (uint32_t j = 0; j < FLASH_SECTOR_SIZE; j++) {
            if (vp[j] != sweep_pattern_byte(s, j)) {
                bad_bytes++;
                sector_ok = false;
            }
        }
        if (!sector_ok) {
            bad_sectors++;
            if (first_bad_sector == 0xFFFFFFFFu) first_bad_sector = s;
        }
    }
    char msg[112];
    if (bad_sectors == 0) {
        snprintf(msg, sizeof(msg),
                 "verify all %lu sectors (%lu KiB) - 0 mismatches, all flash accessible",
                 (unsigned long)num_sectors, (unsigned long)(sweep_bytes / 1024));
    } else {
        snprintf(msg, sizeof(msg),
                 "verify: %lu/%lu sectors FAILED (%llu bad bytes, first at sector %lu)",
                 (unsigned long)bad_sectors, (unsigned long)num_sectors,
                 (unsigned long long)bad_bytes, (unsigned long)first_bad_sector);
    }
    report(bad_sectors == 0, msg);

    double sweep_kib = sweep_bytes / 1024.0;
    double erase_kbs = (double)sweep_bytes / (double)sweep.erase_us * 1000.0;
    double program_kbs = (double)sweep_bytes / (double)sweep.program_us * 1000.0;
    printf("  erase   : %9llu us total  (%.0f KiB, %.1f KB/s aggregate)\n",
           (unsigned long long)sweep.erase_us, sweep_kib, erase_kbs);
    printf("  program : %9llu us total  (%.0f KiB, %.1f KB/s aggregate)\n",
           (unsigned long long)sweep.program_us, sweep_kib, program_kbs);

    // cleanup: restore blank (erased) state so nothing is left behind
    stdio_flush();
    sleep_ms(20);
    rc = flash_safe_execute(flash_sweep_erase_cb, &sweep, 8000);
    report(rc == PICO_OK, "cleanup erase (all swept sectors restored to blank 0xFF state)");
}

// ---------------------------------------------------------------------------
// CPU baseline: CoreMark. Unlike the custom workloads above, CoreMark's
// score is directly comparable against published figures for any other
// MCU, which is the point of running it here.
// ---------------------------------------------------------------------------
void bench_coremark() {
    section("CPU BASELINE: CoreMark (portable, for comparing other MCUs)");
    printf("  Running (self-calibrates to a ~10s measured run)...\n");
    stdio_flush();
    sleep_ms(20);

    uint64_t t0 = time_us_64();
    coremark_run();
    uint64_t t1 = time_us_64();

    double wall_secs = (double)(t1 - t0) / 1.0e6;
    double mhz = clock_get_hz(clk_sys) / 1.0e6;
    printf("\n  wall time        : %.2f s (includes self-calibration passes)\n", wall_secs);
    if (coremark_score > 0.0) {
        printf("  CoreMark/MHz     : %.3f  (%.2f CoreMark @ %.1f MHz)\n",
               coremark_score / mhz, coremark_score, mhz);
        printf("  -> compare this CoreMark/MHz figure directly against any\n"
               "     other MCU's published CoreMark/MHz score.\n");
    }
    report(coremark_score > 0.0, "CoreMark completed and reported a score");
}

} // namespace

int main() {
    stdio_init_all();

    // Give a host-side terminal a chance to attach; bounded so we never
    // hang forever if nothing is listening yet.
    for (int i = 0; i < 200 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(400);

    printf("\n\n");
    rule();
    printf("  RP2040 BAREMETAL SELF-TEST / BENCHMARK\n");
    rule();

    print_system_info();
    bench_cpu();
    bench_memory();
    bench_flash();
    bench_coremark();

    section("SUMMARY");
    if (g_fail_count == 0) {
        printf("  ALL CHECKS PASSED - CPU, SRAM and Flash all verified working.\n");
    } else {
        printf("  %d CHECK(S) FAILED - see FAIL lines above.\n", g_fail_count);
    }
    rule();
    printf("\ndone. (send any byte over serial to re-enter the USB bootloader)\n");

    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            reset_usb_boot(0, 0);
        }
        sleep_ms(50);
    }
}
