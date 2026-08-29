# ATmega328P

**ATmega328P** on an Arduino Uno-compatible board — 8-bit AVR @ 16 MHz, 32 KB
flash, **2 KB SRAM**, 1 KB EEPROM. The board on the bench here enumerates as
`26BA:0003` (Freetronics) rather than as a genuine Arduino, but the chip,
the 16 MHz crystal, the fuse set and the stock Optiboot 4.4 bootloader are all
the standard Uno configuration, and nothing in this folder depends on the
brand.

Bare metal in the strictest sense the platform allows: **no Arduino core, no
RTOS, no vendor HAL**. The only dependency is avr-libc — the toolchain's own
headers and runtime, which is what `<avr/io.h>` is. The whole toolchain is
`avr-gcc` plus `avrdude`, and firmware goes on through the bootloader the
board already has, so no programmer hardware is involved at any point.

## Headline result

**15/15 checks passed.** Integer ALU at **171 cycles per iteration**, SRAM and
EEPROM fully swept and verified clean, and the whole suite fits in 2 KB of RAM
with **92 bytes to spare**.

There is no headline CoreMark figure, and that is the finding. **This chip
cannot run a conformant CoreMark**: the standard performance run needs a
2000-byte working set and the part has 2048 bytes of SRAM in total, so that
configuration cannot even be linked. The reduced 1200-byte profile run scores
**39.44 iterations/sec, 2.465 per MHz** — CRC-validated by CoreMark itself, but
not comparable with the other chips in this repo or with any published
CoreMark 1.0 number.

## Layout

```
bringup/     minimal known-good starting point - 1 Hz blink plus a
             hardware-verified clock and GPIO evidence table, 5122 bytes
benchmark/   the CPU / SRAM / flash / EEPROM / CoreMark test suite
```

Each folder has its own README with build instructions and full detail.

## Results summary

**15/15 checks passed, 0 failures.** Full per-test breakdown and the raw log in
[`benchmark/results.md`](benchmark/results.md).

| Test | Result |
|---|---|
| Integer ALU | 0.0936 Mops/s — 171 cycles/iter |
| 32-bit divide — software | 0.0224 Mops/s — 713 cycles/iter |
| Float — software, no FPU | 0.0476 Mops/s — 336 cycles/iter |
| **CoreMark** | **39.44 iter/s — 2.465/MHz, reduced dataset, not conformant** |
| SRAM write / read / copy | 3.043 / 3.551 / 1.996 MB/s |
| Flash read (`LPM`) vs SRAM (`LD`) | 1.333 / 1.599 MB/s — 12.00 vs 10.01 cyc/byte |
| EEPROM erase / program | 1785 / 1784 µs per byte |
| EEPROM full sweep | 1024 B erased + programmed + verified, 0 mismatches |
| RAM headroom | 92 bytes of 2048 |

## Chip-specific notes

**2 KB of SRAM is the defining constraint, and it shapes everything.** It is
why CoreMark cannot run conformantly, why the memory test region and CoreMark's
working set are the same buffer, why every string in the firmware lives in
flash rather than RAM, and why the suite paints a canary across free RAM to
prove the budget held. On the other chips here, RAM is something you spend
without thinking; on this one it is the design.

**No FPU, no divider, and no second precision to compare against.** The
reference suite prices an FPU by running an identical loop in a precision the
FPU does not cover. Here there is no FPU, and avr-gcc makes `double` a 32-bit
alias of `float` — so both halves of that comparison are the same code path.
One number, labelled software, and nothing to price it against. A 32-bit
divide-and-modulo costs **713 cycles** on this part because the instruction set
has no divide at all.

**Flash is a separate address space.** This is a Harvard machine: program
memory is reachable only by `LPM` and a plain C pointer cannot see it. Byte
reads from flash cost 1.20× what they cost from SRAM — 12.00 against 10.01
cycles per byte, matching the disassembly exactly. The practical consequence is
bigger than the ratio: anything you want in flash has to be put there
deliberately and read with different instructions.

**The EEPROM is the only non-volatile memory this part can write from
application code**, which is why it stands in for the flash sweep. Erase-only
and write-only are separate 1.8 ms operations, so erase and program can be
timed independently exactly as a flash sweep does. Both measured within a
tenth of a percent of the datasheet figure.

**Single core.** One AVR core, so there is no multi-core scaling figure to
report.

## Board quirks worth knowing before you start

**Flash program/erase is off the table from application code.** `SPM` only
executes from the boot loader section (datasheet §27.4), and stock Optiboot
4.4 — which is what an Uno ships with, and what `avrdude -v` reports for this
board as HW 3 / FW 4.4 — exposes no `SPM` entry point. Later Optiboot builds
add one. Without it there is no way to write flash from where a benchmark
runs, and replacing the bootloader needs an ISP programmer. The suite
substitutes the EEPROM and says so in its own output rather than quietly
dropping the test.

**`printf_P` with `%f` prints `?`, silently.** `-lprintf_flt` overrides
`vfprintf` but never `vfprintf_P`, so keeping a format string in flash — which
on a 2 KB part you must — costs you float formatting with no diagnostic
anywhere. Stage the flash string into RAM and call `vfprintf` yourself.

**String literals are the real SRAM budget.** They land in `.data` on this
part unless explicitly placed in flash. CoreMark's format strings alone are
about a kilobyte against a 2048-byte budget. Everything printed by this
firmware goes through `PSTR()`.

**`MCUSR` is always zero under a bootloader.** Optiboot reads and clears it
before jumping to the application, so the reset source is not observable from
application code.

**`avrdude` through Optiboot cannot read fuses** — the STK500v1 bootloader
protocol has no path for it. The chip can read its own, though: the
`SPMCSR`/`LPM` sequence behind `boot_lock_fuse_bits_get()` works from the
application section on this part, and both the bring-up and the benchmark
report the fuses that way.

**There is no exact 1 MHz timebase.** The AVR prescaler has taps at
/1, /8, /64, /256 and /1024 — no /16 — so a 16 MHz part cannot produce a 1 MHz
timer tick without an interrupt every 16 cycles. Timer1 at /8 gives 2 MHz,
which divides cleanly to microseconds and is better resolution anyway.

## Toolchain

```
avr-gcc 7.3.0 / avr-libc, avrdude 8.1
```

Built and verified on macOS with `-O2`. No SDK, no CMake, no vendor libraries,
and no programmer hardware — firmware goes on through the bootloader the board
already has.
