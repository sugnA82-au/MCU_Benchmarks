// ATmega328P bring-up: prove the board boots, that it is really running from
// the 16 MHz crystal and not the 8 MHz internal RC, and that GPIO works in
// both directions.
//
// Bare metal: no Arduino core, no bootloader library, no HAL. The only
// dependency is avr-libc's register definitions in <avr/io.h>, which are the
// toolchain's own headers rather than a vendor abstraction layer -- the AVR
// equivalent of hand-writing addresses out of the datasheet, minus the typos.
//
// Everything printed below is measured on the chip, not assumed. The clock in
// particular is checked three independent ways (see README).

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdio.h>

#define F_CPU_HZ    16000000UL
#define TIMER_HZ     2000000UL     // Timer1, /8 prescale off the system clock

// On-board LED, Arduino D13.
#define LED_BIT     PB5
// Arduino D2. No user button on an Uno-form-factor board, so this is driven
// from the pin's own output driver to prove the input path (see README).
#define IN_BIT      PD2

// ---------------------------------------------------------------------------
// Timebase: Timer1 free-running at 2 MHz, extended past its 16 bits by the
// overflow ISR. Everything timed here is timed against this.
// ---------------------------------------------------------------------------
static volatile uint16_t g_ovf;

ISR(TIMER1_OVF_vect) { g_ovf++; }

static void timer_init(void)
{
    TCCR1A = 0;
    TCCR1B = _BV(CS11);            // /8 -> 16 MHz / 8 = 2 MHz
    TCNT1  = 0;
    TIFR1  = _BV(TOV1);
    TIMSK1 = _BV(TOIE1);
}

// Reading the counter and the overflow count is a two-part read, so handle the
// case where the counter wrapped but the ISR has not run yet.
static uint32_t ticks32(void)
{
    uint8_t  s = SREG;
    cli();
    uint16_t hi = g_ovf;
    uint16_t lo = TCNT1;
    if (TIFR1 & _BV(TOV1)) { lo = TCNT1; hi++; }
    SREG = s;
    return ((uint32_t)hi << 16) | lo;
}

static void delay_ticks(uint32_t n)
{
    uint32_t t0 = ticks32();
    while ((ticks32() - t0) < n) { }
}

#define MS_TO_TICKS(ms) ((uint32_t)(ms) * (TIMER_HZ / 1000UL))

// ---------------------------------------------------------------------------
// Console: UART0 at 115200 8N1, polled. U2X0 doubles the sampling rate so
// UBRR0=16 lands 2.1% off 115200, which is what the on-board USB-serial bridge
// expects and is well inside its tolerance. At the reset-default 8 MHz this
// divisor would give 57600 and the output would be unreadable garbage -- so a
// legible line is itself weak evidence the crystal is running (see README).
// ---------------------------------------------------------------------------
static int uart_putc(char c, FILE *f)
{
    if (c == '\n') uart_putc('\r', f);
    while (!(UCSR0A & _BV(UDRE0))) { }
    UDR0 = c;
    return 0;
}

static FILE g_stdout = FDEV_SETUP_STREAM(uart_putc, NULL, _FDEV_SETUP_WRITE);

static void uart_init(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = 16;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    stdout = &g_stdout;
}

// ---------------------------------------------------------------------------
// Clock cross-check against the watchdog's own oscillator.
//
// The WDT runs from a dedicated 128 kHz internal RC, completely independent of
// the system clock. Timing one WDT period with Timer1 therefore measures the
// system clock against something that is not the system clock. The RC is only
// good to about +/-10%, but the question it has to answer is 16 MHz vs 8 MHz,
// which is a factor of two.
// ---------------------------------------------------------------------------
static volatile uint8_t g_wdt_fired;

ISR(WDT_vect) { g_wdt_fired = 1; }

static uint32_t measure_wdt_period_ticks(void)
{
    g_wdt_fired = 0;

    uint8_t s = SREG;
    cli();
    __asm__ __volatile__ ("wdr");
    MCUSR  &= (uint8_t)~_BV(WDRF);
    WDTCSR  = _BV(WDCE) | _BV(WDE);                    // timed change sequence
    WDTCSR  = _BV(WDIE) | _BV(WDP2) | _BV(WDP1);       // 128K cycles ~ 1.0 s
    SREG    = s;
    sei();

    uint32_t t0 = ticks32();
    while (!g_wdt_fired) { }
    uint32_t t1 = ticks32();

    s = SREG;
    cli();
    __asm__ __volatile__ ("wdr");
    MCUSR  &= (uint8_t)~_BV(WDRF);
    WDTCSR  = _BV(WDCE) | _BV(WDE);
    WDTCSR  = 0;
    SREG    = s;

    return t1 - t0;
}

// ---------------------------------------------------------------------------
// GPIO. Output is the LED; the input path is proved by driving D2 low from its
// own output driver and reading it back through PIND, then releasing it to the
// internal pull-up. That exercises the same input synchroniser a real button
// would, and needs no wiring.
// ---------------------------------------------------------------------------
static uint8_t read_in_pin(void)
{
    __asm__ __volatile__ ("nop");   // PINx is synchronised; one cycle to settle
    return (PIND & _BV(IN_BIT)) ? 1 : 0;
}

int main(void)
{
    uint8_t reset_src = MCUSR;
    MCUSR = 0;

    DDRB  |= _BV(LED_BIT);
    PORTB |= _BV(LED_BIT);          // LED on: we got as far as main()

    // Fuse and lock bits are read with the SPMCSR/LPM sequence, which is two
    // instructions that must stay adjacent -- so this runs before sei().
    uint8_t lfuse = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    uint8_t hfuse = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    uint8_t efuse = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
    uint8_t lock  = boot_lock_fuse_bits_get(GET_LOCK_BITS);

    uart_init();
    timer_init();
    sei();

    delay_ticks(MS_TO_TICKS(200));

    printf_P(PSTR("\n\n=============================================\n"));
    printf_P(PSTR("  ATmega328P BRING-UP\n"));
    printf_P(PSTR("=============================================\n"));
    printf_P(PSTR("  compiler      : GCC %s\n"), __VERSION__);
    // Optiboot reads MCUSR into r2 and clears it before jumping to the
    // application, so on a bootloader board this is 0 every time. Not a fault
    // -- just a thing you cannot observe from application code here.
    printf_P(PSTR("  MCUSR (reset) : 0x%02x  (%s)\n"), reset_src,
             (reset_src == 0)          ? "cleared by Optiboot before app start" :
             (reset_src & _BV(EXTRF))  ? "external reset" :
             (reset_src & _BV(PORF))   ? "power-on"       :
             (reset_src & _BV(WDRF))   ? "watchdog"       :
             (reset_src & _BV(BORF))   ? "brown-out"      : "unknown");

    printf_P(PSTR("\n  -- fuses, read from the running chip --\n"));
    printf_P(PSTR("  low fuse      : 0x%02x  CKSEL=%01x SUT=%01x CKDIV8=%c\n"),
             lfuse, lfuse & 0x0F, (lfuse >> 4) & 0x03,
             (lfuse & 0x80) ? '1' : '0');
    printf_P(PSTR("  high fuse     : 0x%02x\n"), hfuse);
    printf_P(PSTR("  ext fuse      : 0x%02x\n"), efuse);
    printf_P(PSTR("  lock bits     : 0x%02x\n"), lock);
    // CKSEL 1111 selects the low-power crystal oscillator, 8 MHz and up.
    // CKDIV8 unprogrammed (bit 7 = 1) means no divide-by-8 on the way in.
    printf_P(PSTR("  -> clock src  : %s, CKDIV8 %s\n"),
             ((lfuse & 0x0F) >= 0x08) ? "external crystal 8 MHz+"
                                      : "internal RC / external clock",
             (lfuse & 0x80) ? "off (full speed)" : "ON (clock/8)");

    printf_P(PSTR("\n  -- Timer1 vs the watchdog's own RC oscillator --\n"));
    uint32_t wdt_ticks = measure_wdt_period_ticks();
    // WDP[3:0]=0110 is 128K = 131072 cycles of the watchdog RC. Reported the
    // way round that can actually be checked: given a 16 MHz system clock,
    // what frequency must that RC be running at? The part's own spec puts it
    // near 128 kHz nominal and well over 100 kHz at 5 V. An 8 MHz system clock
    // would need it to be half that, which is not a rate this oscillator runs.
    uint32_t wdt_hz = (uint32_t)((131072ULL * (uint64_t)TIMER_HZ) / wdt_ticks);
    printf_P(PSTR("  131072 WDT clk: %lu Timer1 ticks\n"), wdt_ticks);
    printf_P(PSTR("  -> WDT RC at   %lu Hz, if the system clock is 16 MHz\n"), wdt_hz);
    printf_P(PSTR("     (would be   %lu Hz at 8 MHz -- not a rate this RC runs)\n"),
             wdt_hz / 2UL);
    printf_P(PSTR("  [%s] system clock is the 16 MHz crystal, not the 8 MHz RC\n"),
             (wdt_hz > 80000UL && wdt_hz < 180000UL) ? " OK " : "FAIL");

    printf_P(PSTR("\n  -- GPIO --\n"));
    DDRD  &= (uint8_t)~_BV(IN_BIT);
    PORTD |=  _BV(IN_BIT);
    delay_ticks(MS_TO_TICKS(1));
    uint8_t v_pullup = read_in_pin();

    PORTD &= (uint8_t)~_BV(IN_BIT);
    DDRD  |=  _BV(IN_BIT);
    delay_ticks(MS_TO_TICKS(1));
    uint8_t v_driven = read_in_pin();

    DDRD  &= (uint8_t)~_BV(IN_BIT);
    PORTD |=  _BV(IN_BIT);
    delay_ticks(MS_TO_TICKS(1));
    uint8_t v_release = read_in_pin();

    printf_P(PSTR("  DDRB          : 0x%02x  (PB5 output, on-board LED)\n"), DDRB);
    printf_P(PSTR("  D2 pulled up  : %u  (expect 1)\n"), v_pullup);
    printf_P(PSTR("  D2 driven low : %u  (expect 0)\n"), v_driven);
    printf_P(PSTR("  D2 released   : %u  (expect 1)\n"), v_release);
    printf_P(PSTR("  [%s] GPIO input path reads both levels\n"),
             (v_pullup == 1 && v_driven == 0 && v_release == 1) ? " OK " : "FAIL");

    printf_P(PSTR("\n  -- blinking at 1 Hz, one line per 5 s --\n"));
    printf_P(PSTR("  Compare the us column against wall-clock time on the host:\n"));
    printf_P(PSTR("  that is the only check here that does not trust the chip.\n\n"));

    // 1 Hz blink: toggle every 500 ms. Print every tenth toggle so the host
    // has a 5 s grid to measure against.
    uint32_t next = ticks32();
    uint32_t t_start = next;
    for (uint16_t step = 0; ; step++) {
        next += MS_TO_TICKS(500);
        while ((int32_t)(ticks32() - next) < 0) { }

        PINB = _BV(LED_BIT);        // writing PINx toggles PORTx on this part

        if ((step % 10) == 9) {
            uint32_t el = ticks32() - t_start;
            printf_P(PSTR("  step %3u  elapsed %lu us  (expect %lu)\n"),
                     (unsigned)(step + 1), el / 2UL,
                     (uint32_t)(step + 1) * 500000UL / 1000UL * 1000UL);
        }
    }
}
