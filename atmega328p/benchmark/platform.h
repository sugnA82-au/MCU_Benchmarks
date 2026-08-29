#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <avr/pgmspace.h>

// Rated clock of an Uno-form-factor board: 16 MHz crystal, CKDIV8 off.
// Verified against the watchdog RC and against host wall-clock time in
// ../bringup. Every rate reported by the suite is divided by this.
#define F_CPU_HZ    16000000UL

// Timer1 with the /8 prescaler. 16 MHz / 8 = 2 MHz exactly; the AVR prescaler
// has no /16 tap, so an exact 1 MHz timebase is not available on this part
// without burning an interrupt every 16 cycles. 2 MHz divides cleanly to
// microseconds and gives 0.5 us of resolution instead of 1.0 us.
#define TIMER_HZ     2000000UL

#define EEPROM_SIZE  1024U
#define FLASH_SIZE   32768UL
#define SRAM_SIZE    2048U

// EEPROM programming modes, datasheet Table 8-1. Erase-only and write-only are
// separate 1.8 ms operations; the atomic mode does both in 3.4 ms. Keeping
// them separate is what lets erase and program be timed independently.
#define EE_MODE_ATOMIC  0x00
#define EE_MODE_ERASE   0x10
#define EE_MODE_WRITE   0x20

void     timer_init(void);
uint64_t time_us_64(void);
void     delay_ms(uint16_t ms);

void     console_init(void);

// printf with the format string kept in flash.
//
// avr-libc's printf_P routes to vfprintf_P, and -lprintf_flt only ever
// overrides plain vfprintf -- so "%f" through printf_P silently prints "?"
// with no diagnostic at compile or link time. This stages the format string
// into a RAM buffer and calls the float-capable vfprintf instead. Truncation
// is reported inline rather than silently mangling a line.
int      pprintf(const char *fmt_P, ...);
#define  PR(fmt, ...) pprintf(PSTR(fmt), ##__VA_ARGS__)

void     led_init(void);
void     led_on(void);
void     led_off(void);
void     led_toggle(void);

// EEPROM: the on-chip non-volatile memory this part can actually erase and
// program from application code. Returns 0 on success, non-zero if the
// controller did not clear EEPE within the guard time.
uint8_t  ee_program_byte(uint16_t addr, uint8_t val, uint8_t mode);
uint8_t  ee_read_byte(uint16_t addr);
uint8_t  ee_wait_idle(void);

// Bytes below the stack still holding the 0xC5 fill painted in at .init3 --
// i.e. RAM that nothing has touched. On a 2 KB part this is the check that
// says whether the numbers above it can be trusted at all.
uint16_t ram_untouched_bytes(void);

#endif /* PLATFORM_H */
