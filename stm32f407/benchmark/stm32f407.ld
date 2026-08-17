/* STM32F407VGT6: 1 MB flash, 128 KB SRAM, 64 KB CCM. */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.exidx : { *(.ARM.exidx*) } >FLASH

  _siramfunc = LOADADDR(.ramfunc);
  .ramfunc : {
    . = ALIGN(4);
    _sramfunc = .;
    *(.ramfunc*)
    . = ALIGN(4);
    _eramfunc = .;
  } >RAM AT>FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

  /* Highest flash address this binary occupies. .data is the last section
   * with a flash load address, so its LOAD end is the end of the image.
   * The flash sweep uses this to decide which sectors it must not erase. */
  _flash_binary_end = LOADADDR(.data) + SIZEOF(.data);

  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* Heap + stack headroom in SRAM. This sits after .bss, so if the SRAM test
   * buffer ever grows past what fits, the reservation pushes the region over
   * 128 KB and the link fails outright - rather than the buffer silently
   * overlapping the stack at run time. */
  . = ALIGN(8);
  PROVIDE(end  = .);
  PROVIDE(_end = .);
  _heap_start = .;

  _min_heap  = 0x800;    /* 2 KB */
  _min_stack = 0x1000;   /* 4 KB */

  .heap_stack_guard (NOLOAD) : {
    . = . + _min_heap;
    . = . + _min_stack;
  } >RAM

  /* CCM: 64 KB of zero-wait-state core-coupled RAM. Not reachable by DMA,
   * and not initialised by startup - the benchmark writes before it reads. */
  .ccm (NOLOAD) : {
    . = ALIGN(4);
    _sccm = .;
    *(.ccmram*)
    . = ALIGN(4);
    _eccm = .;
  } >CCM

  /DISCARD/ : { *(.ARM.attributes) }
}
