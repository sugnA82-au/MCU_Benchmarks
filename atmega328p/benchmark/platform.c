// ATmega328P platform layer: timebase, console, LED, EEPROM driver.
//
// Bare metal. The only dependency is avr-libc's register definitions, which
// are the toolchain's own headers rather than a vendor abstraction layer.
// No Arduino core is linked in and none of its startup code runs.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "platform.h"

// ---------------------------------------------------------------------------
// Timebase
//
// Timer1 free-running at 2 MHz, extended past 16 bits by the overflow ISR.
// The counter is hardware and keeps running with interrupts masked, which the
// EEPROM driver relies on; the ISR does not, so a read has to cope with the
// counter having wrapped while the ISR had not yet run.
//
// Overflow every 32.768 ms means the ISR costs roughly 30 cycles 30 times a
// second: about 0.006% of the CPU, which is below the resolution of anything
// reported here.
// ---------------------------------------------------------------------------
static volatile uint32_t g_ovf;

ISR(TIMER1_OVF_vect) { g_ovf++; }

void timer_init(void)
{
    TCCR1A = 0;
    TCCR1B = _BV(CS11);          // /8
    TCNT1  = 0;
    TIFR1  = _BV(TOV1);
    TIMSK1 = _BV(TOIE1);
}

uint64_t time_us_64(void)
{
    uint8_t  s = SREG;
    cli();
    uint32_t hi = g_ovf;
    uint16_t lo = TCNT1;
    if (TIFR1 & _BV(TOV1)) { lo = TCNT1; hi++; }
    SREG = s;
    // 2 MHz -> microseconds is a shift, not a divide.
    return (((uint64_t)hi << 16) | lo) >> 1;
}

void delay_ms(uint16_t ms)
{
    uint64_t t0 = time_us_64();
    while ((uint32_t)(time_us_64() - t0) < (uint32_t)ms * 1000UL) { }
}

// ---------------------------------------------------------------------------
// Console: UART0, 115200 8N1, polled.
//
// Polled rather than interrupt-driven on purpose: nothing is printed inside a
// timed region, and a TX interrupt firing mid-measurement would show up in the
// numbers. U2X0 doubles the sampling rate so UBRR0=16 lands 2.1% off 115200,
// inside the tolerance of the on-board USB-serial bridge.
// ---------------------------------------------------------------------------
static int uart_putc(char c, FILE *f)
{
    if (c == '\n') uart_putc('\r', f);
    while (!(UCSR0A & _BV(UDRE0))) { }
    UDR0 = c;
    return 0;
}

static FILE g_stdout = FDEV_SETUP_STREAM(uart_putc, NULL, _FDEV_SETUP_WRITE);

void console_init(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = 16;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    stdout = &g_stdout;
}

#define FMT_BUF 112
static char g_fmt[FMT_BUF];

int pprintf(const char *fmt_P, ...)
{
    strncpy_P(g_fmt, fmt_P, FMT_BUF - 1);
    g_fmt[FMT_BUF - 1] = '\0';

    // Loud, not silent: a format string longer than the staging buffer would
    // otherwise drop conversions off the end of the line and quietly
    // desynchronise the arguments. Measured with strnlen_P rather than by
    // probing the byte past the end, which for a short string is just
    // whatever flash happens to sit there.
    uint8_t truncated = (strnlen_P(fmt_P, FMT_BUF) >= (FMT_BUF - 1));

    va_list ap;
    va_start(ap, fmt_P);
    int n = vfprintf(stdout, g_fmt, ap);
    va_end(ap);

    if (truncated) fputs_P(PSTR("  <<FORMAT TRUNCATED>>\r\n"), stdout);
    return n;
}

// ---------------------------------------------------------------------------
// LED: the on-board one on PB5 (Arduino D13). Writing a 1 to PINx toggles
// PORTx on this part, so a toggle is one instruction and needs no read.
// ---------------------------------------------------------------------------
void led_init(void)   { DDRB |= _BV(PB5); }
void led_on(void)     { PORTB |= _BV(PB5); }
void led_off(void)    { PORTB &= (uint8_t)~_BV(PB5); }
void led_toggle(void) { PINB = _BV(PB5); }

// ---------------------------------------------------------------------------
// EEPROM
//
// This part's EEPROM controller has no error status register -- EECR carries
// mode, enable and ready bits and nothing else. So "did it work" is answered
// two ways: EEPE must clear inside a generous guard time, and every byte is
// read back. Both are reported.
//
// The EEMPE -> EEPE sequence has a four-cycle deadline, so interrupts are
// masked across it. Timer1 is hardware and keeps counting regardless, which is
// exactly why the timebase is not derived from an interrupt.
// ---------------------------------------------------------------------------
#define EE_GUARD 200000UL       // ~50 ms at this loop cost; writes take 1.8 ms

uint8_t ee_wait_idle(void)
{
    uint32_t guard = EE_GUARD;
    while (EECR & _BV(EEPE)) {
        if (--guard == 0) return 1;
    }
    return 0;
}

uint8_t ee_program_byte(uint16_t addr, uint8_t val, uint8_t mode)
{
    if (ee_wait_idle()) return 1;

    EEAR = addr;
    EEDR = val;

    uint8_t s = SREG;
    cli();
    EECR = mode;                 // clears EEMPE as a side effect, which is fine
    EECR |= _BV(EEMPE);          // sbi
    EECR |= _BV(EEPE);           // sbi, two cycles later - inside the deadline
    SREG = s;
    return 0;
}

uint8_t ee_read_byte(uint16_t addr)
{
    while (EECR & _BV(EEPE)) { }
    EEAR = addr;
    EECR |= _BV(EERE);
    return EEDR;
}

// ---------------------------------------------------------------------------
// RAM canary
//
// .init3 runs after the stack pointer is set up and before .data is copied, so
// filling from __heap_start upwards cannot disturb anything. Whatever is still
// 0xC5 later on is RAM the stack never reached.
// ---------------------------------------------------------------------------
extern uint8_t __heap_start;

__attribute__((naked, used, section(".init3")))
static void ram_paint(void)
{
    uint8_t *p   = &__heap_start;
    uint8_t *top = (uint8_t *)(SP - 32);
    while (p < top) *p++ = 0xC5;
}

uint16_t ram_untouched_bytes(void)
{
    const uint8_t *p = &__heap_start;
    uint16_t n = 0;
    while (p <= (const uint8_t *)RAMEND && *p == 0xC5) { n++; p++; }
    return n;
}
