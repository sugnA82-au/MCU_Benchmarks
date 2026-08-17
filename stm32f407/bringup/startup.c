/* Vector table + reset handler for STM32F407VGT6. */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);

void Reset_Handler(void)
{
    /* .data: copy initialisers from flash into SRAM */
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* .bss: zero */
    for (dst = &_sbss; dst < &_ebss; dst++) *dst = 0;

    /* FPU on (CP10/CP11 full access) - we build with -mfloat-abi=hard,
     * so this must happen before any float code runs. */
    *(volatile uint32_t *)0xE000ED88UL |= (0xFUL << 20);

    main();
    while (1) { }            /* main() must not return */
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
ALIAS void SysTick_Handler(void);   /* weak: main.c overrides it */

/* Cortex-M4 core vectors only; the 82 STM32 IRQs below them are unused
 * by this demo and left out deliberately. */
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
};
