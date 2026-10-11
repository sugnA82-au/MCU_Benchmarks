// CH32V003 bring-up: prove the chip boots, prove it is really running at 48 MHz
// from the PLL, and prove GPIO works in both directions.
//
// No RTOS, no vendor HAL - ch32fun's register headers and startup code only.
//
// Console is the WCH-Link debug channel on SWIO (PD1); capture it with
//   python3 ../benchmark/capture.py --output serial_log.txt --done BRINGUP_DONE
// PD4 is driven as a 1 Hz square wave (and read back); PD6 is the input pin.

#include "ch32fun.h"
#include <stdio.h>
#include <stdint.h>

#define F_CPU_HZ   48000000u
#define TIMER_HZ   6000000u            // SysTick = HCLK/8
#define OUT_PIN    4                    // PD4
#define IN_PIN     6                    // PD6
#define UPTIME_S   60u

static unsigned g_checks, g_fails;

static void check(int ok, const char *what)
{
    g_checks++;
    if (!ok) g_fails++;
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

static void pin_mode(int pin, uint32_t nibble)
{
    GPIOD->CFGLR = (GPIOD->CFGLR & ~(0xFu << (4 * pin))) | (nibble << (4 * pin));
}
static int pin_read(int pin) { return (GPIOD->INDR >> pin) & 1u; }

// Busy-wait on SysTick so the cadence is exact and independent of printf.
static void wait_ticks(uint32_t t0, uint32_t n) { while (SysTick->CNT - t0 < n) { } }

int main(void)
{
    SystemInit();
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;
    Delay_Ms(3000);                      // let the host attach

    printf("\n\nCH32V003 BRING-UP\n");
    printf("RCC_CTLR=%08lx RCC_CFGR0=%08lx FLASH_ACTLR=%08lx SYSTICK_CTLR=%08lx\n",
           (unsigned long)RCC->CTLR, (unsigned long)RCC->CFGR0,
           (unsigned long)FLASH->ACTLR, (unsigned long)SysTick->CTLR);
    printf("ESIG flash=%lu KB  UID (64-bit) %08lx %08lx\n",
           (unsigned long)*(volatile uint16_t *)0x1FFFF7E0u,
           (unsigned long)*(volatile uint32_t *)0x1FFFF7E8u,
           (unsigned long)*(volatile uint32_t *)0x1FFFF7ECu);

    check(((RCC->CFGR0 >> 2) & 3u) == 2u, "RCC_CFGR0.SWS = PLL is the system clock");
    check((RCC->CTLR & (1u << 25)) != 0, "RCC_CTLR.PLLRDY");
    check((RCC->CTLR & (1u << 1)) != 0, "RCC_CTLR.HSIRDY");
    check((FLASH->ACTLR & 3u) == 1u, "FLASH_ACTLR.LATENCY = 1 wait state");

    // GPIO input: PD6 with the internal pull-up, then the internal pull-down.
    pin_mode(IN_PIN, 0x8);                       // input with pull-up/down
    GPIOD->BSHR = 1u << IN_PIN;                  // OUTDR=1 -> pull-up
    for (volatile int i = 0; i < 1000; i++) { }
    int up = pin_read(IN_PIN);
    GPIOD->BCR = 1u << IN_PIN;                   // OUTDR=0 -> pull-down
    for (volatile int i = 0; i < 1000; i++) { }
    int down = pin_read(IN_PIN);
    printf("PD6 pulled up reads %d, pulled down reads %d\n", up, down);
    check(up == 1 && down == 0, "GPIO input path reads both levels (PD6 internal pulls)");

    // GPIO output: PD4 push-pull, read back through INDR at each edge.
    pin_mode(OUT_PIN, 0x1);                      // push-pull, 10 MHz
    unsigned readback_bad = 0, edges = 0;

    printf("@BEGIN uptime\n");
    uint32_t t_start = SysTick->CNT, t = t_start;
    for (uint32_t half = 0; half < UPTIME_S * 2u; half++) {
        int level = (half & 1u) == 0;
        if (level) GPIOD->BSHR = 1u << OUT_PIN; else GPIOD->BCR = 1u << OUT_PIN;
        edges++;
        if (pin_read(OUT_PIN) != level) readback_bad++;
        t += TIMER_HZ / 2u;
        wait_ticks(t_start, t - t_start);
    }
    uint32_t elapsed = SysTick->CNT - t_start;
    printf("@END uptime us=%lu\n", (unsigned long)(elapsed / (TIMER_HZ / 1000000u)));
    GPIOD->BCR = 1u << OUT_PIN;

    printf("PD4 driven %u edges, read back wrong %u times\n", edges, readback_bad);
    check(readback_bad == 0, "GPIO output path: PD4 read back matches at every edge");
    printf("SysTick elapsed %lu ticks for %lu half-periods of 500 ms\n",
           (unsigned long)elapsed, (unsigned long)(UPTIME_S * 2u));
    // No self-check on elapsed: the loop waits on SysTick, so comparing it with
    // SysTick would be circular. The host wall-clock comparison is the evidence.

    printf("\nBRINGUP_DONE failures=%u checks=%u\n", g_fails, g_checks);
    // keep blinking so an LED on PD4, if there is one, shows the chip alive
    for (;;) {
        GPIOD->BSHR = 1u << OUT_PIN; Delay_Ms(500);
        GPIOD->BCR = 1u << OUT_PIN;  Delay_Ms(500);
    }
}
