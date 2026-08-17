# STM32F407 benchmark results

Board: STM32F4 Discovery, STM32F407VGT6 @ 168 MHz (HSE 8 MHz → PLL), 1 MB
flash, 128 KB SRAM + 64 KB CCM. `DBGMCU_IDCODE 0x10016413` (dev `0x413`,
rev `0x1001`). Built with GCC 15.3.1, `-O2`, hard-float ABI.

**17/17 checks passed. 0 failures.**

Full raw output for this run is in [`semihost_log.txt`](semihost_log.txt);
every figure below is taken directly from it.

## CPU

| Test | Work | Time | Result |
|---|---:|---:|---:|
| Integer ALU | 20,000,000 iters | 1,309,525 µs | **15.27 Mops/s** |
| Hardware divider (native `UDIV`) | 5,000,000 iters | 615,779 µs | **8.12 Mops/s** |
| Float — hardware FPU | 2,000,000 iters | 142,858 µs | **14.00 Mops/s** |
| Double — software emulated | 2,000,000 iters | 2,821,430 µs | **0.71 Mops/s** |

**FPU speedup: 19.75×.** The M4F FPU is single-precision only, so the
double-precision loop — identical in shape — falls back to
`__aeabi_dmul`/`__aeabi_dadd`.

Integer determinism check passed: the same workload run twice produced
identical checksums.

## CoreMark

| | |
|---|---|
| Score | **378.21 iterations/sec** |
| **CoreMark/MHz** | **2.251** |
| Iterations | 6000 |
| Measured time | 15.86 s (self-calibrated) |
| Compiler | GCC 15.3.1, `-O2 cortex-m4f` |
| Memory location | STACK |

Self-validated: `Correct operation validated`, seedcrc `0xe9f5`, crcfinal
`0xa14c`.

## Memory

| Region | Write | Read | Copy | Integrity |
|---|---:|---:|---:|---|
| Main SRAM (96 KiB tested) | **134.3 MB/s** | **95.8 MB/s** | **27.9 MB/s** | 0 mismatches |
| CCM (all 64 KiB) | **134.3 MB/s** | **96.1 MB/s** | **28.0 MB/s** | 0 mismatches |

Integrity covers four full address/pattern sweeps per region — address,
~address, `0xAAAAAAAA`, `0x55555555` — plus a copy verify. All clean.

The untested 32 KiB of main SRAM is live `.data`/`.bss`, the heap/stack
reservation and CoreMark's working set: state the testing code is itself
running on top of.

## Flash

| Test | Result |
|---|---:|
| Sequential read, ART on | **26.6 MB/s** |
| Sequential read, ART off | **9.2 MB/s** |
| ART speedup | **2.90×** |
| Erase (aggregate) | **126.1 KB/s** |
| Program (aggregate) | **283.8 KB/s** |

**Full-chip sweep: sectors 3–11, 976 KiB erased, programmed and verified —
0 mismatches, all flash accessible.** Sectors 0–2 hold the program itself
and are left untouched. The swept region was erased back to blank `0xFF`
afterwards and verified blank.

Wait states were held at 5 for both ART measurements, so the 2.90× isolates
the accelerator rather than the wait-state setting.

## Two notes on reading these numbers

**The ART-off figure is code-placement sensitive.** Treat it as an order of
magnitude, not three significant figures. With the prefetch buffer and
I-cache disabled, instruction fetch is directly exposed to the 5 wait states
the flash needs at 168 MHz, so throughput depends on exactly where the read
loop lands relative to flash line boundaries. During development an edit
that only removed three lines of unrelated `printf` moved this figure by
11%, with the measurement loop itself untouched. The ART-*on* figure barely
moved — which is precisely what the accelerator is for.

**The copy row measures newlib-nano's `memcpy`,** not the memory system. It
is markedly more conservative than a hand-tuned routine, which is why it
lands below the plain C write loop. Write and read reflect the memory path
more directly.

## Reproducing

```
make
st-util --semihosting                     # console appears in st-util's stdout
arm-none-eabi-gdb -q stm32f407_bench.elf \
  -ex "target extended-remote :4242" -ex "continue"
```
