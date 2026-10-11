# CH32V003 benchmark results

Board: CH32V003F4P6 (QingKe V2A, **RV32EC**) on a WCH-LinkE, **48 MHz**
(24 MHz HSI x2 through the PLL, HCLK = SYSCLK), **16 KB flash**, **2 KB SRAM**.
Probe-reported chip ID `00-30-05-10`, WCH-LinkE firmware 2.18. Built with WCH
RISC-V GCC 12.2.0, `-O2 -fno-builtin -fno-lto -msave-restore -march=rv32ecxw
-mabi=ilp32e`, PlatformIO platform `8cf3b51`, `ch32fun` `72957fb`.

The suite is **two images**, because 16 KB of flash cannot hold CoreMark and
the rest of the suite and still leave room for a flash sweep (see the
benchmark README):

| Image | Flash | Static SRAM | Log |
|---|---:|---:|---|
| `suite` — CPU, SRAM, flash read, flash sweep | 12,800 B | 1,212 B | [`serial_log.txt`](serial_log.txt) |
| `coremark` — CoreMark, 1200-byte profile run | 14,460 B | 1,256 B | [`coremark_log.txt`](coremark_log.txt) |

**Suite: 20/20 checks passed. CoreMark: 9/9 checks passed. 0 failures.**

Every figure below is taken directly from those two logs.

## CPU

| Test | Work | Time | Rate | Cycles/iter |
|---|---:|---:|---:|---:|
| Integer ALU | 20,000,000 iters | 14,166,667 µs | **1.4118 Mops/s** | **34.00** |
| 32-bit divide — software | 5,000,000 iters | 78,673,998 µs | **0.0636 Mops/s** | **755.27** |
| Float — software | 2,000,000 iters | 32,041,668 µs | **0.0624 Mops/s** | **769.00** |

Integer checksum `f2c53d7b` — **the cross-chip reference value**. Divide
checksum `dcefbeaa`. Float result bits `3fbd0900`. Determinism check passed: the
same workload run twice produced identical checksums.

The CPU timings were identical **to the microsecond** between two full runs of
the suite (the second run is the one in the log; the first was a build that
differed only in message text).

There is **no hardware divider, no multiplier and no FPU**: RV32EC has no M
extension. At `-O2` GCC expands multiplies by a constant into inline shifts and
adds — disassembly shows no `__mulsi3` call in the integer, divide or memory
loops — so the integer row is 34 cycles, not hundreds. The divide row is two
libgcc calls per iteration, `__udivsi3` and `__umodsi3`, so its 755 cycles are
a division and a modulo plus the LCG step. The float row is libgcc's
`__mulsf3`/`__addsf3`/`__gesf2` (the soft multiply itself calls `__mulsi3`);
because there is no FPU, there is no second precision to compare it against.

## CoreMark

> **This is not a conformant CoreMark score.** See the note below the table.

| | |
|---|---|
| Iterations/sec | **115.858** |
| Per MHz | **2.4131** |
| Iterations | 2000 |
| Measured time | 17.263 s (self-calibrated, 103,575,400 ticks at 6 MHz) |
| Dataset | **1200 bytes — profile run, not the 2000-byte performance run** |
| Compiler | WCH GCC 12.2.0, `-O2 -fno-builtin -fno-lto -msave-restore -march=rv32ecxw -mabi=ilp32e` |
| Memory location | STATIC, in the same 2048 bytes as the stack |

Self-validated: `Correct operation validated`, seedcrc `0x4eaf`, crclist
`0x6a79`, crcmatrix `0x5608`, crcstate `0xe5a4`, crcfinal `0xf34a`.

CoreMark's own `Iterations/Sec` line prints **117**: its time is whole seconds
(`17`), so it divides 2000 by 17. The figure above is computed from the exact
tick count instead (2000 × 6,000,000 / 103,575,400) by a shim in
`core_portme.c` that records the `Total ticks` and `Iterations` values CoreMark
prints. The CoreMark sources are unmodified.

**Why it is not conformant, measured rather than argued.** A conformant
performance run operates on a 2000-byte working set; this chip has 2048 bytes
of SRAM *in total*.

| Conformant (2000-byte) configuration | |
|---|---|
| Static SRAM | **2,056 B** (this image's 1,256 B + the 800 extra dataset bytes) |
| Linker | `region 'RAM' overflowed by 8 bytes` — **it does not link** |
| CoreMark's own stack high-water | **392 B**, canary-measured (in the 1200-byte run) |
| Needed vs available | 2,056 + 392 = **2,448 B** vs **2,048 B** |

The linker's 8 bytes agree exactly with the 2,056 − 2,048 the firmware computes
at run time. Unlike some toolchains, this one *does* refuse to link a data
region that does not fit, so the failure shows up at build time rather than as
a chip that silently prints nothing. Even with the 8 bytes found, the stack
would be 400 bytes short.

So: less data per iteration means less work per iteration. The figure is real,
it is reproducible — **103,575,400 ticks in two separate runs** — and it is
**not** comparable with published CoreMark 1.0 numbers or with 2000-byte runs
elsewhere. The firmware prints that warning immediately above the number so a
stray copy of the log cannot be misread. `CoreMark/MHz` here is per nominal
48 MHz; see the clock note below.

## Memory — SRAM

1200 bytes of the 2048 tested, 2000 passes per row.

| Test | Work | Time | Rate | Cost |
|---|---:|---:|---:|---:|
| Write (32-bit) | 2,400,000 B | 100,333 µs | **23.920 MB/s** | 8.03 cyc/word |
| Read (32-bit) | 2,400,000 B | 100,250 µs | **23.940 MB/s** | 8.02 cyc/word |
| Copy (word loop) | 1,200,000 B | 75,666 µs | **15.859 MB/s** | 12.11 cyc/word |

Integrity: four full address/pattern sweeps — address, ~address, `0xAAAAAAAA`,
`0x55555555` — plus a copy verify. **0 mismatches.**

The untested 848 bytes are `.data`/`.bss` and the stack: state the testing
code is itself running on top of.

**RAM headroom: 356 bytes of 2048 never touched by the stack** in the suite
image, measured with a `0xA5` canary painted from the end of `.bss` to just
below the stack pointer at start-up and checked at the end of the run. In the
CoreMark image the same canary reads 400 bytes.

## Flash — read path

| Test | Work | Time | Rate | Cost |
|---|---:|---:|---:|---:|
| Flash word read | 262,144 B | 13,655 µs | **19.198 MB/s** | 10.00 cyc/word |
| SRAM word read | 261,600 B | 10,927 µs | **23.939 MB/s** | 8.02 cyc/word |

**A word read from flash costs 1.25× a word read from SRAM.** Both rows run the
identical loop (`read_words`) and both fetch their instructions from flash, so
the flash row is data and instruction fetch sharing one flash port. There is no
flash cache or prefetch switch on this part to measure on and off — the only
timing control is `FLASH_ACTLR.LATENCY`, which has to be 1 at 48 MHz.

## Flash — page erase / program / verify sweep

The swept region is everything from the first 64-byte page fully past the end of
the running image (computed from linker symbols, not hardcoded) to the end of
flash.

| | |
|---|---|
| Program image | 12,800 B (`0x3200`), ending exactly on the page-200 boundary |
| Swept region | pages 200–255, `0x3200`–`0x3FFF`, **56 pages, 3,584 bytes** |

| Test | Work | Time | Rate | Per page |
|---|---:|---:|---:|---:|
| Erase (64-byte fast page erase) | 3,584 B | 175,165 µs | **20.46 KB/s** | 3,128 µs |
| Program (64-byte fast page program) | 3,584 B | 161,221 µs | **22.23 KB/s** | 2,879 µs |

**Full sweep: all 3,584 bytes erased, programmed and verified — 0 mismatches**,
then erased back to blank `0xFFFFFFFF` and verified blank. `FLASH_STATR`
reported **no write-protect error** and `BSY` always cleared. The sweep is
small because the program image takes 12,800 of the 16,384 bytes; that is the
cost of keeping the suite in one image next to its own console code.

The pattern depends on **both** page and offset (`page * 173 + offset * 31 + 7`),
so a fault that aliases one address onto another shows up, not just stuck bits.

The CPU keeps executing from flash while the controller is busy — instruction
fetches simply stall until `BSY` clears — so, unlike parts where the array
cannot be read during an operation, the driver does not need to run from RAM.
The sweep verifying byte for byte is the evidence for that.

## Clock and timebase

| Region | Chip | Host wall-clock | Difference |
|---|---:|---:|---:|
| Integer ALU | 14.1667 s | 14.1083 s | +0.413% |
| Divide | 78.6740 s | 78.3202 s | +0.452% |
| Float | 32.0417 s | 31.9057 s | +0.426% |
| CoreMark (whole run) | 27.0048 s | 26.8920 s | +0.419% |

Measured by `wallclock_check.py` from the marker lines in the two logs; host
times come from `serial_log.timing.json` / `coremark_log.timing.json`. The
chip's clock reads consistently **0.41–0.45% long**, which is the 24 MHz HSI
running a little fast (see `../bringup/README.md`). Practical consequences:

- Rates quoted **per second** are per second of the chip's own clock, i.e. per
  nominal 48 MHz. Per host-clock second they would be about 0.4% higher.
- **Cycles per iteration are unaffected** — numerator and denominator are both
  counted in the same ticks. That is the figure to carry away.
- Register read-back (`RCC_CFGR0.SWS` = PLL, `PLLRDY` set, `HPRE` = 0) confirms
  the *configuration*; the host comparison is what bounds the *frequency*.

## Notes on reading these numbers

**Cycles per iteration is the figure worth carrying away.** 34 cycles for an
integer mul/add/xor/shift round, 755 for a 32-bit divide-and-modulo, 769 for a
software float multiply-add. Those distinguish this architecture; the Mops/s
figures fold in a clock that is itself good to about half a percent.

**The write row's multiply is expanded inline, not a libgcc call.** Write and
read both land at 8 cycles a word, which is what you would expect from a
memory-limited loop, so they are honest memory figures.

**The copy row is a hand-written word loop, not a libc `memcpy`.** The build
links no libc, so it says more about that loop than about the silicon.

**The flash/SRAM ratio is a ratio of two loops that both run from flash.** It
is 1.25×, not "flash is 1.25× slower than SRAM" in isolation.

**The console is slow and sits outside every timed region.** A byte costs about
half a millisecond on the SWIO channel, so nothing is printed between a region's
start and end timestamps. The only prints inside the CoreMark wall-clock window
are CoreMark's own, which is why that row's host-vs-chip comparison is over the
whole run, not the scored run.

## Reproducing

```
pio run -e suite -t upload
python3 capture.py --output serial_log.txt --done SUITE_DONE
python3 wallclock_check.py serial_log.timing.json

pio run -e coremark -t upload
python3 capture.py --output coremark_log.txt --done SUITE_DONE
python3 wallclock_check.py coremark_log.timing.json

pio run -e conformant        # expected to FAIL: region `RAM' overflowed by 8 bytes
```

A suite run takes a bit over two minutes, including a 3 s attach delay.
**The flash sweep erases and reprograms the 3,584 bytes after the program
image** and leaves them blank (`0xFF`).
