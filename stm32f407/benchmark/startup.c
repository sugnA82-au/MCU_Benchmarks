/* Vector table + reset handler for STM32F407VGT6. */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _siramfunc, _sramfunc, _eramfunc;

int main(void);

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* .data */
    src = &_sidata; dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* .ramfunc - the flash program/erase code, which cannot execute from
     * flash while it is erasing flash. */
    src = &_siramfunc; dst = &_sramfunc;
    while (dst < &_eramfunc) *dst++ = *src++;

    /* .bss */
    for (dst = &_sbss; dst < &_ebss; dst++) *dst = 0;

    /* FPU on (CP10/CP11 full access) before any float code runs. */
    *(volatile uint32_t *)0xE000ED88UL |= (0xFUL << 20);
    __asm__ volatile ("dsb; isb");

    main();
    while (1) { }
}

void Default_Handler(void) { while (1) { } }

#define ALIAS __attribute__((weak, alias("Default_Handler")))
ALIAS void NMI_Handler(void);
ALIAS void HardFault_Handler(void);
ALIAS void MemManage_Handler(void);
ALIAS void BusFault_Handler(void);
ALIAS void UsageFault_Handler(void);
ALIAS void SVC_Handler(void);
ALIAS void DebugMon_Handler(void);
ALIAS void PendSV_Handler(void);
ALIAS void SysTick_Handler(void);
ALIAS void TIM2_IRQHandler(void);

/* 16 core vectors + 82 STM32F407 external interrupts. Everything not named
 * traps in Default_Handler rather than running off into the weeds. */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void)) &_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    [16 ... 97] = Default_Handler,
    [16 + 28]   = TIM2_IRQHandler,     /* TIM2 = IRQ 28 */
};
