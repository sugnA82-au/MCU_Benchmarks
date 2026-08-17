/* Register definitions for STM32F407VGT6, hand-written from RM0090.
 * Only what this benchmark touches. No CMSIS dependency. */
#ifndef STM32F407_H
#define STM32F407_H

#include <stdint.h>

#define __IO volatile

/* ---- Bases (RM0090 Table 1) ---- */
#define PERIPH_BASE   0x40000000UL
#define APB1PERIPH    (PERIPH_BASE)
#define AHB1PERIPH    (PERIPH_BASE + 0x00020000UL)

#define TIM2_BASE     (APB1PERIPH + 0x0000UL)
#define PWR_BASE      (APB1PERIPH + 0x7000UL)
#define GPIOA_BASE    (AHB1PERIPH + 0x0000UL)
#define GPIOD_BASE    (AHB1PERIPH + 0x0C00UL)
#define RCC_BASE      (AHB1PERIPH + 0x3800UL)
#define FLASH_R_BASE  (AHB1PERIPH + 0x3C00UL)

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

/* RM0090 3.9: embedded flash interface */
typedef struct {
    __IO uint32_t ACR, KEYR, OPTKEYR, SR, CR, OPTCR;
} FLASH_TypeDef;

typedef struct { __IO uint32_t CR, CSR; } PWR_TypeDef;

typedef struct {
    __IO uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2;
    __IO uint32_t CCER, CNT, PSC, ARR;
} TIM_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)  GPIOA_BASE)
#define GPIOD   ((GPIO_TypeDef *)  GPIOD_BASE)
#define RCC     ((RCC_TypeDef *)   RCC_BASE)
#define FLASH   ((FLASH_TypeDef *) FLASH_R_BASE)
#define PWR     ((PWR_TypeDef *)   PWR_BASE)
#define TIM2    ((TIM_TypeDef *)   TIM2_BASE)

/* ---- Cortex-M4 core ---- */
#define SCB_CPACR  (*(__IO uint32_t *) 0xE000ED88UL)
#define SCB_AIRCR  (*(__IO uint32_t *) 0xE000ED0CUL)
#define NVIC_ISER0 ( (__IO uint32_t *) 0xE000E100UL)

/* Debug / trace (ARMv7-M ARM, ch. C1) */
#define DEMCR      (*(__IO uint32_t *) 0xE000EDFCUL)
#define DEMCR_TRCENA (1UL << 24)

#define DWT_CTRL   (*(__IO uint32_t *) 0xE0001000UL)
#define DWT_CYCCNT (*(__IO uint32_t *) 0xE0001004UL)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

#define ITM_STIM0  (*(__IO uint32_t *) 0xE0000000UL)
#define ITM_TER    (*(__IO uint32_t *) 0xE0000E00UL)
#define ITM_TPR    (*(__IO uint32_t *) 0xE0000E40UL)
#define ITM_TCR    (*(__IO uint32_t *) 0xE0000E80UL)
#define ITM_LAR    (*(__IO uint32_t *) 0xE0000FB0UL)
#define ITM_LAR_KEY 0xC5ACCE55UL

#define TPIU_SPPR  (*(__IO uint32_t *) 0xE00400F0UL)
#define TPIU_ACPR  (*(__IO uint32_t *) 0xE0040010UL)
#define TPIU_FFCR  (*(__IO uint32_t *) 0xE0040304UL)

#define DBGMCU_IDCODE (*(__IO uint32_t *) 0xE0042000UL)
#define DBGMCU_CR     (*(__IO uint32_t *) 0xE0042004UL)
#define DBGMCU_CR_SLEEP     (1UL << 0)
#define DBGMCU_CR_STOP      (1UL << 1)
#define DBGMCU_CR_STANDBY   (1UL << 2)
#define DBGMCU_CR_TRACE_IOEN (1UL << 5)

/* ---- RCC bits ---- */
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
#define RCC_AHB1ENR_GPIOBEN (1UL << 1)
#define RCC_AHB1ENR_GPIODEN (1UL << 3)
#define RCC_APB1ENR_TIM2EN  (1UL << 0)
#define RCC_APB1ENR_PWREN   (1UL << 28)

#define PWR_CR_VOS          (1UL << 14)

/* ---- Flash ACR (the "ART accelerator") ---- */
#define FLASH_ACR_LATENCY_MASK (7UL << 0)
#define FLASH_ACR_LATENCY_5WS  (5UL << 0)
#define FLASH_ACR_PRFTEN       (1UL << 8)
#define FLASH_ACR_ICEN         (1UL << 9)
#define FLASH_ACR_DCEN         (1UL << 10)
#define FLASH_ACR_ICRST        (1UL << 11)
#define FLASH_ACR_DCRST        (1UL << 12)

/* ---- Flash program/erase (RM0090 3.6) ---- */
#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL

#define FLASH_SR_EOP    (1UL << 0)
#define FLASH_SR_OPERR  (1UL << 1)
#define FLASH_SR_WRPERR (1UL << 4)
#define FLASH_SR_PGAERR (1UL << 5)
#define FLASH_SR_PGPERR (1UL << 6)
#define FLASH_SR_PGSERR (1UL << 7)
#define FLASH_SR_BSY    (1UL << 16)
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_PGPERR | FLASH_SR_PGSERR)

#define FLASH_CR_PG     (1UL << 0)
#define FLASH_CR_SER    (1UL << 1)
#define FLASH_CR_SNB_SHIFT 3
#define FLASH_CR_SNB_MASK  (0x1FUL << 3)
#define FLASH_CR_PSIZE_X32 (2UL << 8)
#define FLASH_CR_PSIZE_MASK (3UL << 8)
#define FLASH_CR_STRT   (1UL << 16)
#define FLASH_CR_LOCK   (1UL << 31)

/* ---- TIM ---- */
#define TIM_CR1_CEN     (1UL << 0)
#define TIM_DIER_UIE    (1UL << 0)
#define TIM_SR_UIF      (1UL << 0)
#define TIM_EGR_UG      (1UL << 0)
#define TIM2_IRQn       28

/* ---- Memory map ---- */
#define FLASH_BASE_ADDR 0x08000000UL
#define FLASH_SIZE      (1024UL * 1024UL)
#define SRAM_BASE_ADDR  0x20000000UL
#define SRAM_SIZE       (128UL * 1024UL)
#define CCM_BASE_ADDR   0x10000000UL
#define CCM_SIZE        (64UL * 1024UL)

/* ---- Discovery board (UM1472) ---- */
#define LED_GREEN   12
#define LED_ORANGE  13
#define LED_RED     14
#define LED_BLUE    15
#define BTN_USER    0

static inline void led_on(int pin)  { GPIOD->BSRR = (1UL << pin); }
static inline void led_off(int pin) { GPIOD->BSRR = (1UL << (pin + 16)); }

#endif /* STM32F407_H */
