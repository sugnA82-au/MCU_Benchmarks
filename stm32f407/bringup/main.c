/* STM32F4 Discovery bring-up demo.
 *
 * Exercises: clock tree (HSE->PLL 168 MHz), flash wait states, GPIO out,
 * GPIO in, SysTick interrupt, .data/.bss init, FPU.
 *
 * Behaviour:
 *   Idle     - the four LEDs chase clockwise, ~100 ms per step.
 *   Button   - hold USER (blue, PA0) to switch to all four blinking together.
 *   Clock    - if HSE fails to start we fall back to HSI and the chase runs
 *              visibly slower (16 MHz vs 168 MHz), which is the diagnostic.
 */
#include "stm32f407.h"

static volatile uint32_t g_ticks;          /* .bss  - must init to 0 */
static const uint8_t     g_chase[4] = { LED_GREEN, LED_ORANGE, LED_RED, LED_BLUE };
static uint32_t          g_core_hz = 16000000UL;   /* .data - must init to 16e6 */

void SysTick_Handler(void) { g_ticks++; }

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_ticks;
    while ((g_ticks - start) < ms) { __asm__ volatile ("wfi"); }
}

/* HSE 8 MHz (from ST-Link MCO) -> PLL -> 168 MHz.
 * Returns 1 on success, 0 if HSE never came up (then we stay on HSI). */
static int clock_init_168mhz(void)
{
    /* Voltage scale 1 is required above 144 MHz. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR      |= PWR_CR_VOS;

    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t t = 0; !(RCC->CR & RCC_CR_HSERDY); t++)
        if (t > 0x20000) { RCC->CR &= ~RCC_CR_HSEON; return 0; }

    /* 5 wait states + caches, needed before we speed up. */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |
                 FLASH_ACR_DCEN   | FLASH_ACR_LATENCY_5WS;

    /* AHB 168, APB1 42 (max), APB2 84 (max) */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* M=4 -> 2 MHz VCO in; N=168 -> 336 MHz VCO; P=2 -> 168 MHz; Q=7 -> 48 MHz */
    RCC->PLLCFGR = 4UL | (168UL << 6) | (0UL << 16) |
                   RCC_PLLCFGR_SRC_HSE | (7UL << 24);

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) { }

    g_core_hz = 168000000UL;
    return 1;
}

static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOAEN;

    /* PD12..PD15 -> push-pull output, no pull */
    for (int i = 0; i < 4; i++) {
        uint32_t pin = g_chase[i];
        GPIOD->MODER   &= ~(3UL << (pin * 2));
        GPIOD->MODER   |=  (1UL << (pin * 2));    /* 01 = output */
        GPIOD->OTYPER  &= ~(1UL << pin);          /* push-pull */
        GPIOD->PUPDR   &= ~(3UL << (pin * 2));
    }

    /* PA0 -> input. The board already pulls it low, so no internal pull. */
    GPIOA->MODER &= ~(3UL << (BTN_USER * 2));
    GPIOA->PUPDR &= ~(3UL << (BTN_USER * 2));
}

static inline void leds_write(uint32_t mask_on)
{
    /* BSRR: low half sets, high half resets - atomic, no read-modify-write. */
    uint32_t all = (1UL << LED_GREEN) | (1UL << LED_ORANGE) |
                   (1UL << LED_RED)   | (1UL << LED_BLUE);
    GPIOD->BSRR = mask_on | ((all & ~mask_on) << 16);
}

static inline int button_pressed(void)
{
    return (GPIOA->IDR & (1UL << BTN_USER)) != 0;   /* active high */
}

int main(void)
{
    /* Must come before the first WFI, or the debugger loses the target. */
    DBGMCU_CR |= DBGMCU_CR_SLEEP | DBGMCU_CR_STOP | DBGMCU_CR_STANDBY;

    clock_init_168mhz();
    gpio_init();

    /* 1 kHz tick from the core clock, whichever source we ended up on. */
    SysTick->LOAD = (g_core_hz / 1000UL) - 1UL;
    SysTick->VAL  = 0;
    SysTick->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT |
                    SYSTICK_CTRL_ENABLE;

    uint32_t step = 0;
    for (;;) {
        if (button_pressed()) {
            leds_write((1UL << LED_GREEN) | (1UL << LED_ORANGE) |
                       (1UL << LED_RED)   | (1UL << LED_BLUE));
            delay_ms(100);
            leds_write(0);
            delay_ms(100);
        } else {
            leds_write(1UL << g_chase[step & 3]);
            step++;
            delay_ms(100);
        }
    }
}
