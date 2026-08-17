/* Minimal register definitions for STM32F407VGT6.
 * Only what the demo touches - see RM0090 for the full map. */
#ifndef STM32F407_H
#define STM32F407_H

#include <stdint.h>

#define __IO volatile

/* ---- Peripheral bases (RM0090 Table 1) ---- */
#define PERIPH_BASE   0x40000000UL
#define APB1PERIPH    (PERIPH_BASE)
#define AHB1PERIPH    (PERIPH_BASE + 0x00020000UL)

#define GPIOA_BASE    (AHB1PERIPH + 0x0000UL)
#define GPIOD_BASE    (AHB1PERIPH + 0x0C00UL)
#define RCC_BASE      (AHB1PERIPH + 0x3800UL)
#define FLASH_R_BASE  (AHB1PERIPH + 0x3C00UL)
#define PWR_BASE      (APB1PERIPH + 0x7000UL)

typedef struct {
    __IO uint32_t MODER, OTYPER, OSPEEDR, PUPDR;
    __IO uint32_t IDR, ODR, BSRR, LCKR, AFR[2];
} GPIO_TypeDef;

typedef struct {
    __IO uint32_t CR, PLLCFGR, CFGR, CIR;
    __IO uint32_t AHB1RSTR, AHB2RSTR, AHB3RSTR, RESERVED0;
    __IO uint32_t APB1RSTR, APB2RSTR, RESERVED1[2];
    __IO uint32_t AHB1ENR, AHB2ENR, AHB3ENR, RESERVED2;
    __IO uint32_t APB1ENR, APB2ENR;
} RCC_TypeDef;

typedef struct { __IO uint32_t ACR, KEYR, OPTKEYR, SR, CR; } FLASH_TypeDef;
typedef struct { __IO uint32_t CR, CSR; } PWR_TypeDef;
typedef struct { __IO uint32_t CTRL, LOAD, VAL, CALIB; } SysTick_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)  GPIOA_BASE)
#define GPIOD   ((GPIO_TypeDef *)  GPIOD_BASE)
#define RCC     ((RCC_TypeDef *)   RCC_BASE)
#define FLASH   ((FLASH_TypeDef *) FLASH_R_BASE)
#define PWR     ((PWR_TypeDef *)   PWR_BASE)
#define SysTick ((SysTick_TypeDef *) 0xE000E010UL)

#define SCB_CPACR (*(__IO uint32_t *) 0xE000ED88UL)

/* Keep the debug interface alive while the core sleeps. Without this, WFI
 * makes the target invisible to st-info/st-util/gdb (chipid reads 0x000). */
#define DBGMCU_CR (*(__IO uint32_t *) 0xE0042004UL)
#define DBGMCU_CR_SLEEP   (1UL << 0)
#define DBGMCU_CR_STOP    (1UL << 1)
#define DBGMCU_CR_STANDBY (1UL << 2)

/* ---- Bit definitions ---- */
#define RCC_CR_HSION        (1UL << 0)
#define RCC_CR_HSIRDY       (1UL << 1)
#define RCC_CR_HSEON        (1UL << 16)
#define RCC_CR_HSERDY       (1UL << 17)
#define RCC_CR_PLLON        (1UL << 24)
#define RCC_CR_PLLRDY       (1UL << 25)

#define RCC_CFGR_SW_PLL     (2UL << 0)
#define RCC_CFGR_SWS_MASK   (3UL << 2)
#define RCC_CFGR_SWS_PLL    (2UL << 2)
#define RCC_CFGR_HPRE_DIV1  (0UL << 4)
#define RCC_CFGR_PPRE1_DIV4 (5UL << 10)
#define RCC_CFGR_PPRE2_DIV2 (4UL << 13)

#define RCC_PLLCFGR_SRC_HSE (1UL << 22)
#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_AHB1ENR_GPIODEN (1UL << 3)
#define RCC_APB1ENR_PWREN   (1UL << 28)

#define PWR_CR_VOS          (1UL << 14)

#define FLASH_ACR_LATENCY_5WS (5UL << 0)
#define FLASH_ACR_PRFTEN      (1UL << 8)
#define FLASH_ACR_ICEN        (1UL << 9)
#define FLASH_ACR_DCEN        (1UL << 10)

#define SYSTICK_CTRL_ENABLE    (1UL << 0)
#define SYSTICK_CTRL_TICKINT   (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)

/* ---- Discovery board mapping (UM1472) ---- */
#define LED_GREEN   12   /* PD12 */
#define LED_ORANGE  13   /* PD13 */
#define LED_RED     14   /* PD14 */
#define LED_BLUE    15   /* PD15 */
#define BTN_USER    0    /* PA0, active HIGH */

#endif /* STM32F407_H */
