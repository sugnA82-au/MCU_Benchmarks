/* Platform layer for the STM32F407 benchmark: clocks, ITM/SWO console,
 * 64-bit microsecond timer, and a RAM-resident flash program/erase driver.
 *
 * The flash routines MUST execute from RAM: on STM32F4 any read of the flash
 * array while a program/erase is in progress stalls the bus until it
 * completes (RM0090 3.5.1), which would both stall the code doing the
 * erasing and destroy the per-sector timings we are trying to measure.
 */
#include "stm32f407.h"
#include "platform.h"

#include <stddef.h>

uint32_t g_core_hz = 16000000UL;
static volatile uint32_t g_tim2_ovf;

/* ------------------------------------------------------------------ clocks */

int clock_init_168mhz(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR      |= PWR_CR_VOS;              /* scale 1, required >144 MHz */

    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t t = 0; !(RCC->CR & RCC_CR_HSERDY); t++)
        if (t > 0x40000) { RCC->CR &= ~RCC_CR_HSEON; return 0; }

    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |
                 FLASH_ACR_DCEN   | FLASH_ACR_LATENCY_5WS;

    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* HSE 8 MHz: M=4 -> 2 MHz, N=168 -> 336 MHz VCO, P=2 -> 168 MHz, Q=7 -> 48 */
    RCC->PLLCFGR = 4UL | (168UL << 6) | (0UL << 16) |
                   RCC_PLLCFGR_SRC_HSE | (7UL << 24);

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) { }

    g_core_hz = 168000000UL;
    return 1;
}

/* ------------------------------------------------------------------- timer */
/* TIM2 is 32-bit on F4. APB1 is /4, so its timer clock is 2x PCLK1 = 84 MHz;
 * prescaling by 84 gives an exact 1 MHz tick, so every duration printed by
 * the benchmark is in microseconds with no conversion. */
void timer_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1  = 0;
    TIM2->PSC  = (g_core_hz / 2U / 1000000U) - 1U;   /* 84-1 at 168 MHz */
    TIM2->ARR  = 0xFFFFFFFFUL;
    TIM2->EGR  = TIM_EGR_UG;          /* latch PSC */
    TIM2->SR   = 0;
    TIM2->DIER = TIM_DIER_UIE;
    g_tim2_ovf = 0;
    NVIC_ISER0[TIM2_IRQn / 32] = (1UL << (TIM2_IRQn % 32));
    TIM2->CR1  = TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        g_tim2_ovf++;
    }
}

uint64_t time_us_64(void)
{
    uint32_t ovf = g_tim2_ovf;
    uint32_t cnt = TIM2->CNT;
    /* If the counter wrapped but the ISR has not run yet - including the case
     * where interrupts are masked inside a flash critical section - fold the
     * pending overflow in by hand rather than reporting a time that jumps
     * backwards. */
    if (TIM2->SR & TIM_SR_UIF) {
        cnt = TIM2->CNT;
        ovf++;
    }
    return ((uint64_t)ovf << 32) | (uint64_t)cnt;
}

void delay_ms(uint32_t ms)
{
    uint64_t target = time_us_64() + (uint64_t)ms * 1000ULL;
    while (time_us_64() < target) { }
}

/* --------------------------------------------------------------- DWT cycles */

void dwt_init(void)
{
    DEMCR   |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;
}

/* ------------------------------------------------------------ ITM/SWO console */
/* Output goes out the SWO pin (PB3) to the ST-Link, captured on the host by
 * `st-trace`. Unlike semihosting this never halts the core, so it cannot
 * distort the timings being measured. Writes are dropped (never blocking
 * forever) when nothing is draining the FIFO, so the firmware still runs
 * standalone with no debugger attached. */
void itm_init(uint32_t swo_hz)
{
    DEMCR |= DEMCR_TRCENA;
    DBGMCU_CR |= DBGMCU_CR_TRACE_IOEN;    /* hands PB3 to the trace unit */

    TPIU_ACPR = (g_core_hz / swo_hz) - 1U;   /* 83 for 2 MHz from 168 MHz */
    TPIU_SPPR = 2UL;                          /* SWO, NRZ (UART-like) */
    TPIU_FFCR = 0x100UL;                      /* formatter off */

    ITM_LAR = ITM_LAR_KEY;
    ITM_TCR = (1UL << 0)      /* ITMENA  */
            | (1UL << 2)      /* SYNCENA */
            | (1UL << 3)      /* TXENA   */
            | (1UL << 16);    /* TraceBusID = 1 */
    ITM_TPR = 0;              /* all ports unprivileged-accessible */
    ITM_TER = 1UL;            /* stimulus port 0 */
}

static void itm_putc(char c)
{
    if (!(ITM_TCR & 1UL) || !(ITM_TER & 1UL)) return;
    /* Small guard: if nothing is draining the FIFO, drop the byte rather than
     * spin. A large guard here would cost milliseconds *per byte* on a board
     * whose SWO pin nothing is listening to. */
    uint32_t guard = 0;
    while (ITM_STIM0 == 0UL) {
        if (++guard > 256U) return;
    }
    *(volatile uint8_t *)&ITM_STIM0 = (uint8_t)c;
}

/* ---------------------------------------------------------- semihosting */
/* The ST-LINK/V2 on this board cannot deliver SWO to the host (st-trace
 * fails with "read_trace insufficient buffer length"), so the working console
 * is semihosting over SWD: the debugger services each write.
 *
 * SYS_WRITE0 is used rather than SYS_WRITE because it needs no file handle
 * and so no SYS_OPEN(":tt") handshake - fewer moving parts on the host side.
 *
 * A BKPT #0xAB with no debugger attached is a HardFault, so every call is
 * gated on DHCSR.C_DEBUGEN. Standalone, output is simply dropped and the
 * benchmark still runs to completion (watch the LEDs). */
#define DHCSR (*(__IO uint32_t *) 0xE000EDF0UL)
#define DHCSR_C_DEBUGEN (1UL << 0)
#define SYS_WRITE0 0x04

static int g_semihosting = 0;

static inline void semihost_write0(const char *s)
{
    register int   r0 __asm__("r0") = SYS_WRITE0;
    register const char *r1 __asm__("r1") = s;
    __asm__ volatile ("bkpt #0xAB" : "+r"(r0) : "r"(r1) : "memory", "cc");
}

void console_init(void)
{
    g_semihosting = (DHCSR & DHCSR_C_DEBUGEN) ? 1 : 0;
}

int console_is_semihosted(void) { return g_semihosting; }

/* Retarget newlib's stdout/stderr. A strong definition here wins over the
 * weak stub in libnosys. */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    if (g_semihosting) {
        /* SYS_WRITE0 needs a NUL-terminated string; newlib hands us a
         * counted buffer, so chunk it through a small staging buffer. */
        char tmp[65];
        int i = 0;
        while (i < len) {
            int n = len - i;
            if (n > 64) n = 64;
            for (int j = 0; j < n; j++) tmp[j] = buf[i + j];
            tmp[n] = '\0';
            semihost_write0(tmp);
            i += n;
        }
        return len;
    }

    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') itm_putc('\r');
        itm_putc(buf[i]);
    }
    return len;
}

/* ------------------------------------------------------------------- flash */
/* STM32F407VG sector map (RM0090 Table 5) - deliberately non-uniform. */
const flash_sector_t g_flash_sectors[FLASH_NUM_SECTORS] = {
    { 0x08000000UL,  16UL * 1024UL }, { 0x08004000UL,  16UL * 1024UL },
    { 0x08008000UL,  16UL * 1024UL }, { 0x0800C000UL,  16UL * 1024UL },
    { 0x08010000UL,  64UL * 1024UL },
    { 0x08020000UL, 128UL * 1024UL }, { 0x08040000UL, 128UL * 1024UL },
    { 0x08060000UL, 128UL * 1024UL }, { 0x08080000UL, 128UL * 1024UL },
    { 0x080A0000UL, 128UL * 1024UL }, { 0x080C0000UL, 128UL * 1024UL },
    { 0x080E0000UL, 128UL * 1024UL },
};

RAMFUNC static void flash_wait_idle(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { }
}

RAMFUNC void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

RAMFUNC void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

RAMFUNC uint32_t flash_erase_sector(uint32_t sector)
{
    flash_wait_idle();
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;

    FLASH->CR &= ~(FLASH_CR_SNB_MASK | FLASH_CR_PSIZE_MASK);
    FLASH->CR |= FLASH_CR_PSIZE_X32 | FLASH_CR_SER |
                 (sector << FLASH_CR_SNB_SHIFT);
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait_idle();

    uint32_t err = FLASH->SR & FLASH_SR_ERRORS;
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_MASK);
    return err;
}

RAMFUNC uint32_t flash_program_words(uint32_t addr, const uint32_t *src, uint32_t nwords)
{
    flash_wait_idle();
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;

    FLASH->CR &= ~FLASH_CR_PSIZE_MASK;
    FLASH->CR |= FLASH_CR_PSIZE_X32 | FLASH_CR_PG;

    uint32_t err = 0;
    for (uint32_t i = 0; i < nwords; i++) {
        *(volatile uint32_t *)(addr + i * 4U) = src[i];
        flash_wait_idle();
        err |= FLASH->SR & FLASH_SR_ERRORS;
        if (err) break;
    }

    FLASH->CR &= ~FLASH_CR_PG;
    return err;
}

/* ------------------------------------------------------------------- board */

void board_leds_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOAEN;
    const int pins[4] = { LED_GREEN, LED_ORANGE, LED_RED, LED_BLUE };
    for (int i = 0; i < 4; i++) {
        int p = pins[i];
        GPIOD->MODER  &= ~(3UL << (p * 2));
        GPIOD->MODER  |=  (1UL << (p * 2));
        GPIOD->OTYPER &= ~(1UL << p);
        GPIOD->PUPDR  &= ~(3UL << (p * 2));
    }
    GPIOA->MODER &= ~(3UL << (BTN_USER * 2));
    GPIOA->PUPDR &= ~(3UL << (BTN_USER * 2));
}
