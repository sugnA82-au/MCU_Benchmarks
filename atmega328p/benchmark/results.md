# ATmega328P benchmark results

Board: Arduino Uno-compatible, **ATmega328P @ 16 MHz** (external crystal,
`CKDIV8` off), 32 KB flash (512 bytes of it the bootloader), **2 KB SRAM**,
1 KB EEPROM. Device signature `1E 95 0F`, fuses L/H/E `FF`/`DE`/`FD`,
`OSCCAL 0xB6`, stock Optiboot 4.4. Built with avr-gcc 7.3.0 / avr-libc,
`-O2`. Binary: 28,172 bytes flash, 1,619 bytes static SRAM.

**15/15 checks passed. 0 failures.**

Full raw output for this run is in [`serial_log.txt`](serial_log.txt); every
figure below is taken directly from it.

## CPU

| Test | Work | Time | Rate | Cycles/iter |
|---|---:|---:|---:|---:|
| Integer ALU | 20,000,000 iters | 213,774,889 µs | **0.0936 Mops/s** | **171** |
| 32-bit divide — software | 5,000,000 iters | 223,067,281 µs | **0.0224 Mops/s** | **713** |
| Float — software | 2,000,000 iters | 42,004,880 µs | **0.0476 Mops/s** | **336** |

Integer checksum `f2c53d7b` — **the cross-chip reference value**, so the same
arithmetic ran here as on every 32-bit chip in this repo, on a core whose
widest arithmetic instruction is 8 bits. Divide checksum `dcefbeaa`, also
matching. Determinism check passed: the same workload run twice produced
identical checksums.

All three CPU timings reproduced **to the microsecond** across two full runs
of the suite.

There is **no hardware divider and no FPU**. The divide row is avr-libc's
`__udivmodsi4`; the float row is `__mulsf3`/`__addsf3`/`__gtsf2`. And avr-gcc
makes `double` a 32-bit alias of `float` (`sizeof(double) == 4`, printed in
the log), so unlike the other chips here there is no second precision to run
the same loop in — the float row *is* the software row, and there is nothing
to price it against.

## CoreMark

> **This is not a conformant CoreMark score and does not belong next to the
> other chips' numbers.** See the note below the table.

| | |
|---|---|
| Iterations/sec | **39.437** |
| Per MHz | **2.4648** |
| Iterations | 600 |
| Measured time | 15.214 s (self-calibrated) |
| Dataset | **1200 bytes — profile run, not the 2000-byte performance run** |
| Compiler | GCC 7.3.0, `-O2 avr5 TOTAL_DATA_SIZE=1200` |
| Memory location | STATIC |

Self-validated: `Correct operation validated`, seedcrc `0x4eaf`, crclist
`0x6a79`, crcmatrix `0x5608`, crcstate `0xe5a4`, crcfinal `0xee4f`.

**Why it is not conformant.** A conformant CoreMark performance run operates
on a 2000-byte working set. This chip has 2048 bytes of SRAM *in total* —
there is no arrangement of 2000 bytes of data, a stack and a results structure
that fits, so the conformant configuration cannot be linked, never mind run.
What ran instead is the 1200-byte profile-run configuration, one of the sizes
CoreMark holds known CRCs for, so the arithmetic is validated and provably
correct. But less data per iteration means less work per iteration, so
iterations/sec is measuring a smaller job. The figure is real, it is
reproducible, and it is **not** comparable with published CoreMark 1.0 numbers
or with the 2000-byte runs elsewhere in this repo. The firmware prints this
warning immediately above the number so a stray copy of the log cannot be
misread.

## Memory — SRAM

1200 bytes of the 2048 tested, 200 passes per row.

| Test | Work | Time | Rate | Cost |
|---|---:|---:|---:|---:|
| Write (32-bit) | 240,000 B | 78,872 µs | **3.043 MB/s** | 21.0 cyc/word |
| Read (32-bit) | 240,000 B | 67,580 µs | **3.551 MB/s** | 18.0 cyc/word |
| Copy (`memcpy`) | 120,000 B | 60,130 µs | **1.996 MB/s** | 8.0 cyc/byte |

Integrity: four full address/pattern sweeps — address, ~address,
`0xAAAAAAAA`, `0x55555555` — plus a copy verify. **0 mismatches.**

The untested 848 bytes are `.data`/`.bss`, the printf staging buffer and the
stack: state the testing code is itself running on top of. That same 1200-byte
buffer is also CoreMark's working set — on a 2 KB part, sharing it is the
difference between the suite fitting and not, and the two phases never run
concurrently.

**RAM headroom: 92 bytes of 2048 never touched by the stack**, measured with a
`0xC5` canary painted across free RAM at `.init3` and checked at the end of the
run. That is the check that says whether any of the numbers above can be
trusted; on this part it is not a formality.

## Flash — read path

| Test | Work | Time | Rate | Cost |
|---|---:|---:|---:|---:|
| Flash byte read (`LPM`) | 262,144 B | 196,642 µs | **1.333 MB/s** | **12.00 cyc/byte** |
| SRAM byte read (`LD`) | 261,600 B | 163,597 µs | **1.599 MB/s** | **10.01 cyc/byte** |

**Byte reads from flash cost 1.20× what they cost from SRAM.** Both loops
compile to the same shape — a load, a 32-bit accumulate, and loop control —
and the measured cycles per byte match the disassembled instruction counts
exactly (12 and 10). `LPM` is 3 cycles against `LD`'s 2; the remaining cycle
is the address setup a program-memory read needs and a post-incrementing data
pointer does not. Read the ratio as "flash reads cost about 20% more", not as
a pure `LPM`-vs-`LD` figure.

This part is **Harvard**: program memory is a separate address space reachable
only by `LPM`, and a plain C pointer cannot see it at all. That is why string
literals cost SRAM here unless deliberately placed in flash.

## Flash — program/erase: not run, and why

**`SPM` only executes from the boot loader section** (datasheet §27.4). This
board runs stock Optiboot 4.4 — `avrdude -v` reports HW 3 / FW 4.4 — which
exposes no `SPM` entry point; later Optiboot builds do. So an application-space
flash erase/program sweep is not possible here at all, and replacing the
bootloader needs an ISP programmer this suite does not assume. The test is
dropped rather than approximated, and the firmware says so in its own output.

## EEPROM sweep — substituted for the flash sweep

The full 1024 bytes, erased, programmed with a page-and-offset-dependent
pattern, verified byte by byte, and erased back to blank.

| Test | Work | Time | Rate | Per byte |
|---|---:|---:|---:|---:|
| Erase (erase-only mode) | 1024 B | 1,828,522 µs | **0.56 KB/s** | **1785 µs** |
| Program (write-only mode) | 1024 B | 1,827,421 µs | **0.56 KB/s** | **1784 µs** |
| Read | 65,536 B | 127,021 µs | **0.516 MB/s** | — |

**Full sweep: all 1024 bytes erased, programmed and verified — 0 mismatches**,
then erased back to blank `0xFF` and verified blank.

Erase-only and write-only are separate operations on this part, which is what
lets the two be timed independently the way a flash sweep does. Both landed at
1785 µs and 1784 µs per byte against a datasheet figure of 1.8 ms — the
controller is running to spec, and the atomic erase-and-write mode this
deliberately avoids would have taken 3.4 ms and collapsed both rows into one.

The pattern depends on **both** page and offset, so a fault that aliases one
address onto another shows up, not just stuck bits. This part's EEPROM
controller has **no error status register** — `EECR` carries mode, enable and
ready bits and nothing else — so "no errors" means `EEPE` cleared inside a
guard time on every write *and* every byte read back correctly. Both are
checked and both are reported.

## Notes on reading these numbers

**Cycles per iteration is the figure worth carrying away.** Rates in Mops/s
are awkwardly small on a 16 MHz 8-bit part and they fold in the clock. Cycles
per iteration is what actually distinguishes this architecture: 171 cycles for
an integer mul/add/xor/shift round, 713 for one 32-bit divide-and-modulo, 336
for a software float multiply-add.

**The divide row is a software routine, not a divider.** The AVR instruction
set has no divide at all. 713 cycles is the cost of 32-bit division on a core
whose widest arithmetic instruction is 8 bits — the useful thing to know about
this part, but not the same measurement as a chip with a hardware divider.

**The write row's multiply does not survive `-O2`.** GCC strength-reduces
`i * 2654435761u` into a running add, so the emitted loop is four `st Z+` and
one 32-bit add, with no call to `__mulsi3`. Write and read are therefore both
honest memory figures here. Worth knowing that this workload is not measuring
a multiply on any chip in this repo.

**The copy row is avr-libc's `memcpy`,** which is a byte loop at 8 cycles per
byte. It says more about the library than about the silicon; write and read
are plain C loops and reflect the memory path more directly.

## Reproducing

```
make
make flash
make console          # or any serial terminal at 115200
```

A full run takes about seven minutes, almost all of it CPU. **The EEPROM sweep
overwrites all 1024 bytes of EEPROM** and leaves it erased to `0xFF`.
