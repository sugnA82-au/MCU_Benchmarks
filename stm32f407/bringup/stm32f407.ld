/* STM32F407VGT6: 1 MB flash, 128 KB SRAM (+64 KB CCM, not used here). */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* full descending stack */

SECTIONS
{
  .isr_vector : {
    KEEP(*(.isr_vector))
  } >FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.exidx : { *(.ARM.exidx*) } >FLASH

  _sidata = LOADADDR(.data);

  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* newlib's _sbrk grows the heap from `end` upward; the stack grows down
   * from _estack. Reserve headroom so the two cannot silently collide. */
  . = ALIGN(8);
  PROVIDE(end  = .);
  PROVIDE(_end = .);
  _heap_start = .;

  _min_heap  = 0x400;   /* 1 KB */
  _min_stack = 0x800;   /* 2 KB */

  .heap_stack_guard (NOLOAD) : {
    . = . + _min_heap;
    . = . + _min_stack;
  } >RAM

  /DISCARD/ : { *(.ARM.attributes) }
}
