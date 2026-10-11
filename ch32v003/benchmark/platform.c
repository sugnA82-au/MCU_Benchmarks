#include <stdio.h>
#include "platform.h"

extern unsigned char _end;
extern unsigned char _data_lma, _data_vma, _edata;

unsigned g_fail_count, g_check_count;

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
void pr_ratio(uint32_t num, uint32_t den, unsigned decimals)
{
    uint32_t whole = num / den, rem = num % den;
    uint8_t dig[10];
    for (unsigned i = 0; i <= decimals; i++) {       // one guard digit for rounding
        rem *= 10u;
        dig[i] = (uint8_t)(rem / den);
        rem %= den;
    }
    if (dig[decimals] >= 5) {
        int i = (int)decimals - 1;
        for (; i >= 0; i--) { if (++dig[i] < 10) break; dig[i] = 0; }
        if (i < 0) whole++;
    }
    printf("%lu", (unsigned long)whole);
    if (decimals) {
        char s[10];
        for (unsigned i = 0; i < decimals; i++) s[i] = (char)('0' + dig[i]);
        s[decimals] = 0;
        printf(".%s", s);
    }
}

void pr_cycles(uint32_t ticks, uint32_t n, unsigned decimals)
{
    if (ticks > 0x1FFFFFFFu) pr_ratio(ticks, n / CYC_PER_TICK, decimals);
    else                     pr_ratio(ticks * CYC_PER_TICK, n, decimals);
}

void rule(void)
{
    printf("--------------------------------------------------------------\n");
}

void section(const char *title)
{
    printf("\n");
    rule();
    printf("  %s\n", title);
    rule();
}

void report(int ok, const char *what)
{
    g_check_count++;
    if (!ok) g_fail_count++;
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

uint32_t print_system_info(const char *title)
{
    section(title);

    uint32_t ctlr = RCC->CTLR, cfgr = RCC->CFGR0, actlr = FLASH->ACTLR;
    uint32_t esig_kb = *(volatile uint16_t *)0x1FFFF7E0u;
    volatile uint32_t *uid = (volatile uint32_t *)0x1FFFF7E8u;
    uint32_t image = (uint32_t)(uintptr_t)&_data_lma + (uint32_t)(&_edata - &_data_vma);

    printf("  Target     : CH32V003F4P6, 1 x RV32EC (no M ext: no hw mul/div, no FPU)\n");
    printf("  Console    : WCH-Link debug channel on SWIO (PD1)\n");
    printf("  Compiler   : GCC %s\n", __VERSION__);
    printf("  sys clock  : %lu Hz (24 MHz HSI x2, PLL)   timer: %lu Hz (SysTick, HCLK/8)\n",
           (unsigned long)F_CPU_HZ, (unsigned long)TIMER_HZ);
    printf("  Flash/SRAM : %lu / %lu bytes   UID (64-bit) %08lx %08lx\n",
           (unsigned long)FLASH_BYTES, (unsigned long)SRAM_BYTES,
           (unsigned long)uid[0], (unsigned long)uid[1]);
    printf("  this image : %lu bytes flash, %lu bytes static SRAM\n",
           (unsigned long)image, (unsigned long)ram_static_bytes());
    printf("  RCC_CTLR=%08lx RCC_CFGR0=%08lx FLASH_ACTLR=%08lx SYSTICK_CTLR=%08lx\n",
           (unsigned long)ctlr, (unsigned long)cfgr, (unsigned long)actlr,
           (unsigned long)SysTick->CTLR);

    report(((cfgr >> 2) & 3u) == 2u, "RCC_CFGR0.SWS: system clock is the PLL");
    report((ctlr & (1u << 25)) != 0, "RCC_CTLR.PLLRDY set");
    report((actlr & 3u) == 1u, "FLASH_ACTLR.LATENCY = 1 wait state (needed above 24 MHz)");
    report(esig_kb == 16u, "ESIG flash capacity reads 16 KB");

    // PORTING.md rule: verify one known value by eye before trusting a whole
    // log. This printf has no %f and no 64-bit support: check what it does
    // support, and the fixed-point helper used everywhere.
    char buf[40];
    snprintf(buf, sizeof buf, "%lu %08lx %03lu", 123456789ul, 0x89abcdeful, 7ul);
    printf("  printf check      : %s  (must read 123456789 89abcdef 007)\n", buf);
    printf("  fixed-point check : ");
    pr_ratio(314159, 100000, 5);
    printf("  (must read 3.14159)\n");
    static const char expect[] = "123456789 89abcdef 007";
    int same = 1;
    for (unsigned i = 0; i < sizeof expect; i++) if (buf[i] != expect[i]) same = 0;
    report(same, "printf format specifiers behave (no silent mangling)");
    return image;
}

// ---------------------------------------------------------------------------
// Flash driver
// ---------------------------------------------------------------------------
// Fast page mode as documented in the CH32V003 reference manual and used by
// the ch32fun flashtest example. The CPU keeps executing from flash while the
// controller is busy: instruction fetches simply stall until BSY clears, so no
// RAM-resident copy of this driver is needed on this part. (The sweep passing
// byte-for-byte is the evidence that this holds; see README.)
static uint32_t wait_idle(void)
{
    uint32_t guard = 4000000u;                    // a page op is ~3 ms
    while ((FLASH->STATR & FLASH_STATR_BSY) && --guard) { }
    return guard ? 0u : 0x100u;
}

static uint32_t take_status(void)
{
    uint32_t s = FLASH->STATR;
    FLASH->STATR = FLASH_STATR_WRPRTERR | FLASH_STATR_EOP;   // write-1-to-clear
    return s & FLASH_STATR_WRPRTERR;
}

void flash_unlock(void)
{
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;      // unlocks fast page mode
    FLASH->MODEKEYR = FLASH_KEY2;
}

void flash_lock(void)
{
    FLASH->CTLR = CR_LOCK_Set;
    FLASH->CTLR |= FLASH_CTLR_FLOCK;
}

uint32_t flash_erase_page(uint32_t addr)
{
    FLASH->STATR = FLASH_STATR_WRPRTERR | FLASH_STATR_EOP;
    FLASH->CTLR = CR_PAGE_ER;
    FLASH->ADDR = addr;
    FLASH->CTLR = CR_STRT_Set | CR_PAGE_ER;
    uint32_t err = wait_idle();
    FLASH->CTLR = 0;
    return err | take_status();
}

uint32_t flash_program_page(uint32_t addr, const uint32_t words[FLASH_PAGE / 4])
{
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    FLASH->STATR = FLASH_STATR_WRPRTERR | FLASH_STATR_EOP;
    FLASH->CTLR = CR_PAGE_PG;
    FLASH->CTLR = CR_BUF_RST | CR_PAGE_PG;
    FLASH->ADDR = addr;
    uint32_t err = wait_idle();
    for (unsigned i = 0; i < FLASH_PAGE / 4u; i++) {
        dst[i] = words[i];
        FLASH->CTLR = CR_PAGE_PG | FLASH_CTLR_BUF_LOAD;
        err |= wait_idle();
    }
    FLASH->CTLR = CR_PAGE_PG | CR_STRT_Set;
    err |= wait_idle();
    FLASH->CTLR = 0;
    return err | take_status();
}

// ---------------------------------------------------------------------------
// RAM canary
// ---------------------------------------------------------------------------
static unsigned char *canary_top;

void ram_canary_paint(void)
{
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    canary_top = (unsigned char *)(sp - 32);       // stay clear of this frame
    for (volatile unsigned char *p = &_end; p < canary_top; ++p) *p = 0xA5;
}

uint32_t ram_untouched_bytes(void)
{
    unsigned char *p = &_end;
    while (p < canary_top && *p == 0xA5) ++p;
    return (uint32_t)(p - &_end);
}

uint32_t ram_static_bytes(void)
{
    return (uint32_t)((uintptr_t)&_end - SRAM_BASE_ADDR);
}

uint32_t ram_stack_highwater_bytes(void)
{
    return (SRAM_BASE_ADDR + SRAM_BYTES) - ((uintptr_t)&_end + ram_untouched_bytes());
}
