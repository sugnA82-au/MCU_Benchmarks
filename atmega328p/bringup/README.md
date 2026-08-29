# ATmega328P bring-up

Minimal known-good starting point for an Arduino Uno-compatible board: prove
the chip boots, prove it is really running from the 16 MHz crystal rather than
the 8 MHz internal RC, and prove GPIO works in both directions. The benchmark
suite is built on the same foundations.

Bare metal — **no Arduino core, no bootloader library, no vendor HAL**. The
only dependency is avr-libc's register definitions in `<avr/io.h>`, which are
the toolchain's own headers rather than an abstraction layer over the part.
5122 bytes of flash, 211 bytes of SRAM.

## What it does

The on-board LED (PB5, Arduino D13) blinks at 1 Hz off a Timer1 timebase,
and the UART prints an evidence table at startup followed by a timestamp
every 5 seconds.

That exercises, in order:

- **Clock source** — the low fuse is read back from the running chip through
  the `SPMCSR`/`LPM` sequence, so the clock configuration is reported rather
  than assumed.
- **Clock rate** — Timer1 at 2 MHz (16 MHz / 8), cross-checked against the
  watchdog's own RC oscillator and against host wall-clock time.
- **Timer1** — free-running 16-bit counter extended by its overflow ISR.
- **UART0** — 115200 8N1 with `U2X0`, polled, through the board's USB-serial
  bridge.
- **GPIO output** — PB5, and the `PINx`-write toggle trick.
- **GPIO input** — PD2, read through the pin synchroniser at both levels.
- **Startup** — avr-libc's `crtatmega328p.o`: vector table, `.data` copy,
  `.bss` zero.

## Building and flashing

```
make          # -> blinky.hex
make flash    # avrdude -c arduino, through the stock Optiboot bootloader
make console  # screen /dev/cu.usbmodemXXXX 115200
make probe    # avrdude -v, confirms the bootloader answers and the signature
make clean
```

Needs `avr-gcc` and `avrdude`. Override the port with
`make flash PORT=/dev/cu.usbmodemXXXX`.

## Verified on hardware

Read back live from the running chip, not assumed. Full capture in
[`serial_log.txt`](serial_log.txt).

| Check | Reading | Meaning |
|---|---|---|
| Device signature | `1E 95 0F` | ATmega328P (read by `avrdude -v`) |
| Low fuse | `0xFF` | `CKSEL=1111` → low-power crystal, 8 MHz and up |
| ” | `CKDIV8=1` | Unprogrammed → **no** divide-by-8 on the way in |
| High / ext fuse | `0xDE` / `0xFD` | Stock Uno set: 512-byte boot section, BOD 2.7 V |
| Lock bits | `0xCF` | Bootloader section write-protected |
| WDT RC cross-check | 2,420,679 Timer1 ticks per 131,072 WDT cycles | → WDT RC at 108.3 kHz for a 16 MHz system clock |
| Host wall-clock | chip 49.999999 s vs host 49.9763 s | **0.048%** apart over 50 s |
| `DDRB` | `0x20` | PB5 configured as output |
| D2 pulled up / driven low / released | `1` / `0` / `1` | Input path reads both levels |
| `MCUSR` | `0x00` | Cleared by Optiboot before the app runs — see below |

### Why the clock is checked three ways

Everything the benchmark reports is a rate, and every rate is divided by the
clock. One check is not enough, and the obvious one is circular — timing
Timer1 against Timer1 proves nothing.

1. **The fuse** says which oscillator is selected. It is the chip's own
   configuration, but it says nothing about whether the crystal actually
   started.
2. **The watchdog RC** is a physically separate oscillator, so timing one WDT
   period with Timer1 measures the system clock against something that is not
   the system clock. It resolves the question that matters — 16 MHz or
   8 MHz — because those differ by 2×, far more than this RC's spread. It is
   not precise: `131072` WDT cycles came out at 2,420,679 Timer1 ticks, which
   puts the RC at 108.3 kHz against a 128 kHz nominal. That is the RC being
   slow, not the crystal being wrong, and the datasheet's own typical-
   characteristics curves for this oscillator are that loose. An 8 MHz system
   clock would require the RC to be running at 54 kHz, which is not a rate it
   runs at.
3. **The host clock** is the precise one. The firmware reports elapsed
   microseconds every 5 s; over a 50 s window the chip's timebase and the
   host's agreed to **0.048%**. The floor on that method is the ±105 ms of
   line-delivery jitter through USB CDC, so read it as "correct to a few
   tenths of a percent", not as a 480 ppm crystal measurement.

Together: the right oscillator is selected, it is running at roughly the right
speed against an independent reference, and it keeps accurate time against a
good one.

## Gotchas worth knowing

**`MCUSR` is always 0 under a bootloader.** Optiboot reads the reset-source
register into `r2` and clears it before jumping to the application, so
application code cannot see why the chip reset. Not a fault — just not
observable from here. The firmware says so rather than printing "unknown".

**There is no user button on an Uno-form-factor board.** Only RESET. The
input path is proved instead by driving D2 low from its own output driver and
reading it back through `PIND`, then releasing it to the internal pull-up.
That exercises the same input synchroniser a button would and needs no wiring.
`PINx` is read through a synchroniser, so it needs one instruction of settling
after `PORTx`/`DDRx` change before the read is meaningful.

**Getting the fuses out.** `avrdude` talking to Optiboot cannot read fuses —
the STK500v1 bootloader protocol has no path for it, and an ISP programmer is
needed for that. But the chip can read its own: the `SPMCSR`/`LPM` sequence
behind `boot_lock_fuse_bits_get()` works from the application section on this
part. The two instructions must stay adjacent, so it runs before `sei()`.

**Baud rate is itself weak clock evidence.** `UBRR0=16` with `U2X0` gives
115200 only at 16 MHz. At the 8 MHz internal RC the same divisor produces
57600 and the console is unreadable garbage rather than a wrong number — a
failure mode that is at least obvious.

## Files

- `main.c` — clock checks, GPIO checks, UART console, blink loop
- `Makefile` — build, flash through Optiboot, console, probe
- `serial_log.txt` — raw console capture backing the table above
