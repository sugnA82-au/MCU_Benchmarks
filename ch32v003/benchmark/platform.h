#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include "ch32fun.h"

// Rated clock: 24 MHz HSI x2 through the PLL. Verified at run time against the
// RCC registers and against host wall-clock time (see README).
#define F_CPU_HZ     48000000u

// SysTick is clocked from HCLK/8 (FUNCONF_SYSTICK_USE_HCLK = 0), so it counts
// at 6 MHz exactly. It is a free-running 32-bit counter that keeps counting
// with interrupts masked and wraps every ~715 s; every timed region here is
// measured with an unsigned subtraction and is far shorter than that.
#define TIMER_HZ     6000000u
#define TICKS_PER_US (TIMER_HZ / 1000000u)
#define CYC_PER_TICK (F_CPU_HZ / TIMER_HZ)

#define FLASH_BASE_ADDR 0x08000000u
#define FLASH_BYTES     16384u
#define FLASH_PAGE      64u        // fast page erase / program granularity
#define SRAM_BYTES      2048u
#define SRAM_BASE_ADDR  0x20000000u

static inline uint32_t ticks_now(void) { return SysTick->CNT; }
static inline uint32_t ticks_since(uint32_t t0) { return SysTick->CNT - t0; }
static inline uint32_t ticks_to_us(uint32_t t) { return t / TICKS_PER_US; }

// --- console helpers -------------------------------------------------------
// This part's printf (ch32fun's mini_vsnprintf) has no %f and no 64-bit
// support, so rates are done in 32-bit integer arithmetic.
//
// Prints num/den to `decimals` places (0..8), rounded to nearest. Requires
// den < 4.29e8.
void pr_ratio(uint32_t num, uint32_t den, unsigned decimals);
// Prints (ticks * CYC_PER_TICK) / n, without overflowing for long runs.
void pr_cycles(uint32_t ticks, uint32_t n, unsigned decimals);

void rule(void);
void section(const char *title);
void report(int ok, const char *what);     // prints "[ OK ]"/"[FAIL]", counts it
extern unsigned g_fail_count, g_check_count;

// SYSTEM INFO section shared by both images, with the hardware checks.
// Returns the program image size in bytes (flash used).
uint32_t print_system_info(const char *title);

// --- flash page driver (CH32V003 fast page mode, 64-byte pages) ------------
// Each returns 0 on success, or non-zero with FLASH_STATR_WRPRTERR (0x10) set
// if the controller itself reported a write-protect error, or 0x100 if BSY
// never cleared.
void     flash_unlock(void);
void     flash_lock(void);
uint32_t flash_erase_page(uint32_t addr);
uint32_t flash_program_page(uint32_t addr, const uint32_t words[FLASH_PAGE / 4]);

// --- RAM canary -------------------------------------------------------------
// Paint every byte between the end of .bss and just below the current stack
// pointer with 0xA5 and count what survives. On a 2 KB part this is the check
// that says whether the numbers can be trusted at all.
void     ram_canary_paint(void);
uint32_t ram_untouched_bytes(void);
uint32_t ram_static_bytes(void);
uint32_t ram_stack_highwater_bytes(void);   // from the top of RAM down to the deepest write

#endif
