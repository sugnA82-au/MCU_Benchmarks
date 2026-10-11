# CH32V003

**CH32V003F4P6** (TSSOP-20) on a WCH-LinkE programmer — a QingKe V2A core
implementing **RV32EC** @ 48 MHz, **16 KB flash**, **2 KB SRAM**, no hardware
multiply, divide or FPU.

Bare metal: **no RTOS, no vendor HAL**. The only dependency is `ch32fun`'s
register headers, startup code and SWIO debug-print channel. The toolchain is
WCH's RISC-V GCC under PlatformIO, flashed with `wlink`.

## Headline result

**CoreMark 115.858 iterations/sec — 2.413 CoreMark/MHz, on a reduced 1200-byte
dataset, so not a conformant score.** CoreMark self-validated
(`Correct operation validated`).

**This chip cannot run a conformant CoreMark**, and it is short by a measured
margin rather than an argued one: the standard 2000-byte performance run needs
2,056 bytes of static SRAM and the chip has 2,048 — the linker refuses with
`region 'RAM' overflowed by 8 bytes` — and CoreMark's own stack adds 392 bytes on
top. The 1200-byte profile run is the largest that fits beside the stack.

All other checks pass: **suite 20/20, CoreMark image 9/9, bring-up 6/6.**

## Layout

```
bringup/     minimal known-good starting point - 1 Hz GPIO blink plus a
             hardware-verified clock and GPIO evidence table, 3676 bytes
benchmark/   the CPU / SRAM / flash / CoreMark test suite, as two images
             because 16 KB of flash cannot hold CoreMark and the rest
```

Each folder has its own README with build instructions and full detail.

## Results summary

Full per-test breakdown and the raw logs in
[`benchmark/results.md`](benchmark/results.md).

| Test | Result |
|---|---|
| Integer ALU | 1.4118 Mops/s — **34.00 cycles/iter** |
| 32-bit divide (software, libgcc) | 0.0636 Mops/s — **755.27 cycles/iter** |
| Float (software, no FPU) | 0.0624 Mops/s — **769.00 cycles/iter** |
| **CoreMark** | **115.858 iter/s — 2.4131/MHz, reduced dataset, not conformant** |
| SRAM write / read / copy | 23.920 / 23.940 / 15.859 MB/s |
| SRAM integrity | 4 pattern sweeps, 0 mismatches |
| Flash word read vs SRAM | 19.198 / 23.939 MB/s — 10.00 vs 8.02 cyc/word (1.25×) |
| Flash page erase / program | 20.46 / 22.23 KB/s — 3,128 / 2,879 µs per 64-byte page |
| Flash sweep | 3,584 B erased + programmed + verified, 0 mismatches |
| RAM headroom | **356 bytes of 2048** never touched by the stack |

Integer checksum came out `f2c53d7b`, the cross-chip reference value, so the
same arithmetic ran here as the reference prescribes. All CPU timings and the
CoreMark tick count reproduced exactly across two runs.

## Chip-specific notes

**16 KB of flash and 2 KB of SRAM are the defining constraints, and they shape
everything.** SRAM is why CoreMark cannot run conformantly and why the memory
test region and CoreMark's working set are the same size. Flash is why the suite
is two images: the combined build overflowed by 10,468 bytes, and even the split
`suite` image leaves only 3,584 bytes after itself for the flash sweep.

**No M extension.** RV32EC has no multiplier or divider. GCC at `-O2` turns a
multiply by a constant into inline shifts and adds, so the integer round costs
34 cycles; a 32-bit divide-and-modulo is two libgcc calls and costs 755. There is
no FPU either, so the float row is software (769 cycles for a multiply-add and a
compare) and has no hardware counterpart to price it against.

**Single core.** One core, so there is no multi-core scaling to report.

**Flash programming is a 64-byte fast page mode and the CPU can keep running
from flash during it.** Page erase takes 3,128 µs and page program 2,879 µs, about
3 ms each as the reference manual says. Fetches simply stall until `BSY` clears,
so the driver does not have to live in RAM.

**There is no flash cache or prefetch control.** A word read from flash costs
10 cycles against 8 from SRAM, and the only timing knob is the wait-state field,
which must be 1 at 48 MHz.

**The internal oscillator is the clock reference, and it runs fast.** The
24 MHz HSI x2 PLL gave a timebase that read 0.28% long against the host's wall
clock at idle and 0.41–0.45% long under benchmark load, so the part runs at
roughly 48.1–48.2 MHz. That is within the ±1% factory trim, but it means rates
per second carry that uncertainty. **Cycles per iteration do not.**

## Board quirks worth knowing before you start

**The console is the programming pin.** The debug-print channel shares SWIO
(PD1), so the probe must stay attached and a host must be draining it with
`minichlink -T`. Each byte costs about half a millisecond, nothing is printed
inside a timed region, and the first 3 s after reset is a deliberate delay so the
host can attach before the first line. The firmware never touches PD1 or the AFIO
remap, so the part can always be reprogrammed.

**`-fno-builtin` is required for the console to work.** Without it GCC rewrites
`printf("literal\n")` into `puts()`, and the following `printf` line came out as
`RCC_CTLR= 0x<NUL>000000a2 …` followed by silence. The same source with only the
flag added printed everything. Cause not tracked down.

**The linker enforces the SRAM limit.** The conformant CoreMark build fails with
`region 'RAM' overflowed by 8 bytes` rather than linking into a chip that prints
nothing. Read the `RAM:` and `Flash:` percentages after every build anyway; on
this part they are the two numbers that matter.

**`pio run -t upload` flashes the ELF and does not regenerate `firmware.bin`.** A
`.bin` in the build directory can be stale; compare disassembly when asking
whether a binary is the one that ran.

**No LED was observed.** The bring-up drives PD4 at 1 Hz and reads it back, but
nothing here confirms an LED on any pin of any particular board, and the suite
has no LED status convention — the console is the only diagnostic.

## Toolchain

```
WCH RISC-V GCC 12.2.0 (toolchain-riscv 1.120200.220829), -O2 -fno-builtin -fno-lto -msave-restore
        -march=rv32ecxw -mabi=ilp32e
PlatformIO Core 6.1.19, platform-ch32v @ 8cf3b51, framework-ch32v003fun @ 72957fb
wlink 0.23 (flash), minichlink @ 7c76843 (console), WCH-LinkE firmware 2.18
```

Built and verified on macOS. No Make, no CMake, no vendor libraries beyond
`ch32fun`. EEMBC CoreMark is vendored unmodified (Apache 2.0).
