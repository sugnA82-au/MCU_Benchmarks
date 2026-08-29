# ATmega328P baremetal CPU / SRAM / flash / EEPROM benchmark

Self-contained diagnostic firmware for an Arduino Uno-compatible board:
exercises the CPU, sweeps SRAM for correctness, measures the Harvard split
between program and data memory, erases and reprograms the entire EEPROM, and
runs CoreMark as a portable baseline — with a caveat about the last one that
the firmware prints itself.

**No Arduino core, no RTOS, no vendor HAL.** avr-libc's register definitions
and the ATmega328P datasheet, built with `avr-gcc` and flashed through the
board's stock Optiboot bootloader. Nothing from the Arduino IDE is linked in
and none of its startup code runs.

## What it tests

**CPU** (`main.c`):

- Integer ALU throughput — a deterministic mul/add/xor/shift mix, identical to
  the one every other chip in this repo runs. Its checksum doubles as a
  cross-chip correctness check: 20,000,000 iterations must produce
  `f2c53d7b` on any conforming implementation.
- **Software 32-bit divide.** The AVR has no divide instruction at all, so
  this measures avr-libc's `__udivmodsi4` rather than a hardware divider.
- **Software single-precision float.** This part has no FPU, so every
  operation is a call into `__mulsf3`/`__addsf3`/`__gtsf2`. The reference
  suite measures a hardware FPU against software double precision to price the
  FPU; there is no such comparison to make here, and avr-gcc makes `double` a
  32-bit alias of `float`, so there is no second path at all. The firmware says
  so in its own output rather than printing an unlabelled number.
- A **determinism check** — the same workload run twice must produce identical
  checksums, a correctness signal independent of speed.
- Every CPU row also reports **cycles per iteration**, which on an 8-bit core
  says more than a rate does.
- **CoreMark**, vendored unmodified from the EEMBC source (`coremark/`,
  Apache 2.0, see `coremark/LICENSE.md`) except for one line exposing the final
  score. **This chip cannot run a conformant CoreMark** — see below.

**Memory**: write/read/copy bandwidth plus four address/pattern integrity
sweeps (address, ~address, `0xAA`, `0x55`) over 1200 of the 2048 bytes of
SRAM. The untested remainder is `.data`/`.bss`, the printf staging buffer and
the stack: state the testing code is itself running on top of. That same 1200
bytes is CoreMark's working set — on a part with 2 KB of RAM, sharing it is
the difference between the suite fitting and not, and the two phases never run
at the same time. A canary painted across free RAM at `.init3` and checked at
the end is what proves the budget actually held.

**Flash**: sequential read throughput through `LPM`, against the identical
loop reading SRAM. This part is Harvard — program memory is a separate address
space that a plain C pointer cannot reach at all — so the gap between those two
rows is the architecture measured rather than quoted.

**Flash program/erase is not possible on this part from application code**, and
is not faked. `SPM` only executes from the boot loader section (datasheet
§27.4), and this board's stock Optiboot 4.4 exposes no `SPM` entry point —
later Optiboot builds do, but 4.4 is what an Uno ships with, and replacing it
needs an ISP programmer this suite does not assume. `avrdude -v` reports the
bootloader as HW 3 / FW 4.4, which is where that claim comes from.

**EEPROM** stands in for it, per the repo's substitution rule: it is the
on-chip non-volatile memory this part *can* erase and program from where the
benchmark runs. The full 1024 bytes are erased, programmed with a
page-and-offset-dependent pattern (so a fault that aliases one address onto
another shows up, not just stuck bits), verified byte by byte, and erased back
to blank `0xFF`. Erase-only and write-only are separate 1.8 ms operations on
this part, which is what lets erase and program be timed independently, exactly
as the flash sweep does on chips that have one.

## Building

```
make            # -> atmega328p_bench.hex
make flash      # avrdude -c arduino, through the stock Optiboot bootloader
make console    # screen /dev/cu.usbmodemXXXX 115200
make clean
```

Needs `avr-gcc` and `avrdude`. Override the port with
`make flash PORT=/dev/cu.usbmodemXXXX`. No SDK, no CMake, no vendor libraries.

## Running and viewing output

Console is UART0 at 115200 8N1 through the board's on-board USB-serial bridge —
no debugger needed, and no probe involved at any point.

```
make flash
make console      # or any serial terminal at 115200
```

Opening the port asserts DTR, which resets the board, so a terminal opened
after flashing catches the run from its first line.

**A full run takes about seven minutes**, almost all of it CPU: the integer
mix alone is 20,000,000 iterations of 32-bit arithmetic on an 8-bit core.

The on-board LED (D13) mirrors progress with no console attached: on at start,
toggling at each section boundary, then a slow 1 Hz heartbeat at the end if
every check passed and a fast 5 Hz blink if any failed.

**The EEPROM sweep overwrites all 1024 bytes of EEPROM** and leaves it erased
to `0xFF`. If the board is holding anything there, save it first.

## Results

ATmega328P, 16 MHz, 32 KB flash, 2 KB SRAM, 1 KB EEPROM.

**15/15 checks passed, 0 failures.** Full per-test breakdown in
[`results.md`](results.md), raw output in [`serial_log.txt`](serial_log.txt).

| Test | Result |
|---|---|
| Integer ALU | 0.0936 Mops/s — **171 cycles/iter** |
| 32-bit divide (software `__udivmodsi4`) | 0.0224 Mops/s — **713 cycles/iter** |
| Float (software, no FPU) | 0.0476 Mops/s — **336 cycles/iter** |
| **CoreMark** | **39.44 iter/s, 2.465/MHz — reduced 1200-byte dataset, not conformant** |
| SRAM write / read / copy | 3.043 / 3.551 / 1.996 MB/s |
| SRAM integrity | 4 pattern sweeps, 0 mismatches |
| Flash byte read (`LPM`) vs SRAM (`LD`) | 1.333 / 1.599 MB/s — 12.00 vs 10.01 cyc/byte (1.20×) |
| EEPROM erase / program | 0.56 / 0.56 KB/s — 1781 / 1781 µs per byte |
| EEPROM full sweep | 1024 B erased + programmed + verified, 0 mismatches |
| RAM headroom | **112 bytes of 2048** never touched by the stack |

Integer checksum came out `f2c53d7b` — the cross-chip reference value — so the
same arithmetic ran here as on the 32-bit parts in this repo. All three CPU
timings reproduced to the microsecond across two full runs.

### Reading these numbers

- **CoreMark here is not a CoreMark score, and the margin was measured.** The
  conformant run needs a 2000-byte working set; built standalone with nothing
  else linked in, CoreMark's own peak stack on this part is 265 bytes. That is
  2265 bytes needed against the 2048 the chip has, before any harness at all.
  Stripping the whole benchmark suite frees 31 bytes, because the suite already
  shares its memory-test region with CoreMark's working set. The 1200-byte
  profile-run configuration was used instead — CoreMark validates it against
  its own known CRCs, so the arithmetic is proven correct, but less data per
  iteration means less work per iteration. Not comparable with published
  CoreMark 1.0 numbers or with the other chips here. The firmware prints that
  warning above the number itself.
- **The conformant build links cleanly and then does nothing.** avr-gcc does
  not enforce the data region: `avr-size` reports `Data: 2388 bytes (116.6%
  Full)` and emits a valid `.hex`. Flashed, the chip prints not one byte —
  `.bss` ends past `RAMEND` and the stack sits inside the data block before
  `main()` runs. Overflowing SRAM on this toolchain is a number in `avr-size`
  output, not an error.
- **Cycles per iteration is the number worth carrying away.** Mops/s figures
  are awkwardly small at 16 MHz and they fold in the clock. 171 cycles for an
  integer round, 713 for a 32-bit divide-and-modulo, 336 for a software float
  multiply-add — those are what distinguish this architecture.
- **The flash/SRAM ratio is 1.20×, and getting there took a bug fix.** Both
  loops compile to a load, a 32-bit accumulate and loop control, and the
  measured 12.00 and 10.01 cycles per byte match the disassembly exactly. An
  earlier version measured 2.00× because the SRAM side's accumulator was dead
  code and GCC deleted it — see below.
- **The EEPROM controller is running to spec.** 1781 µs per byte for both
  erase-only and write-only, against a datasheet 1.8 ms.
- **112 bytes of SRAM headroom is not a formality on this part.** It is measured
  with a canary painted across free RAM at `.init3`, and it is the check that
  says whether everything above it can be trusted.

## Bugs found and fixed during development (worth knowing if you extend this)

- **`printf_P` with `%f` silently prints `?`.** On AVR, `-lprintf_flt` is what
  gives `printf` float support — but it overrides `vfprintf` *only*, and never
  `vfprintf_P`, which is what `printf_P` actually calls. So a format string
  kept in flash gets the non-float formatter, with no diagnostic at compile
  time, link time or run time. Every `%f` becomes `?`. Fixed with `pprintf()`
  in `platform.c`, which stages the flash string into a RAM buffer and calls
  the float-capable `vfprintf` directly. Nothing here calls `printf_P`. This is
  the same class of trap as newlib-nano's missing `long long`: a formatting
  failure that looks like a data failure.
- **CoreMark's format strings alone would have used half the SRAM.** String
  literals live in `.data` on this part unless explicitly placed in flash, and
  CoreMark's come to roughly a kilobyte against a 2048-byte budget. Fixed
  without touching a single CoreMark `.c` file, by making `ee_printf` a
  variadic macro in `core_portme.h` that wraps the format in `PSTR()` — the
  format string is always a literal at the call site, so the macro can reach
  it.
- **A dead accumulator quietly invalidated the flash-vs-SRAM comparison.** The
  SRAM side computed a checksum that was never used, so GCC deleted the 32-bit
  accumulate while keeping the `volatile` loads — leaving the SRAM loop four
  cycles a byte cheaper than the flash loop it was supposed to be compared
  against, and overstating the cost of flash by 2×. The numbers looked
  perfectly plausible; it was only visible in the disassembly. Fixed by
  printing the checksum, which is the same reason the reference implementation
  assigns its read accumulator to a `volatile` sink. **If two loops are meant
  to be compared, check the emitted code, not just the output.**
- **The format-truncation guard false-positived on every line.** It tested the
  byte one past the end of the staging buffer, which for a short format string
  is just whatever flash happens to sit there. Fixed with `strnlen_P`.
- **`ee_ptr_int` must be 16-bit and `ee_u32` must be `long`.** `int` is 16 bits
  on AVR and pointers are 16 bits, so the reference port's `typedef ee_u32
  ee_ptr_int` is wrong here in both directions. CoreMark's own
  `check_data_types()` catches it, which is a good argument for leaving that
  check in.
- **The memory write loop's multiply does not survive `-O2`.** GCC
  strength-reduces `i * 2654435761u` into a running add — the emitted loop is
  four `st Z+` and one 32-bit add, with no call to `__mulsi3`. That makes the
  write row an honest memory figure, but it is worth knowing that the workload
  as written is not measuring a multiply on any of the chips in this repo.

## Files

- `main.c` — the benchmark suite
- `platform.c/h` — Timer1 timebase, UART console, `pprintf`, LED, EEPROM
  driver, RAM canary
- `Makefile` — build, flash through Optiboot, console
- `coremark/` — vendored EEMBC CoreMark plus this port's `core_portme.c/h`
- `results.md` — full per-test results
- `serial_log.txt` — raw console output from the run in `results.md`
