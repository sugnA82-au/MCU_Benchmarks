# STM32F407

**STM32F407VGT6** on the STM32F4 Discovery board (STM32F407G-DISC1, UM1472).
Cortex-M4F @ 168 MHz, 1 MB flash, 128 KB SRAM + 64 KB CCM.

Baremetal in the strictest sense: **no HAL, no CMSIS, no vendor SDK**. Every
register definition is hand-written from RM0090. The entire toolchain is
`arm-none-eabi-gcc` plus `stlink` — nothing downloaded from ST.

## Headline result

**CoreMark 378.21 iterations/sec — 2.251 CoreMark/MHz** (self-validated).

CoreMark/MHz is the figure worth quoting: it is normalised for clock speed
and directly comparable against published scores for any other MCU.

## Layout

```
bringup/     minimal known-good starting point - LED chase + button,
             620 bytes, proves the clock tree and GPIO work
benchmark/
  stm32f407/ the CPU / FPU / SRAM / CCM / flash / CoreMark test suite
```

Each folder has its own README with build instructions and full detail.

## Results summary

**17/17 checks passed, 0 failures.** Full per-test breakdown and the raw log
in [`benchmark/stm32f407/results.md`](benchmark/stm32f407/results.md).

| Test | Result |
|---|---|
| Integer ALU | 15.27 Mops/s |
| Hardware divider (native `UDIV`) | 8.12 Mops/s |
| Float — hardware FPU | 14.00 Mops/s |
| Double — software emulated | 0.71 Mops/s |
| **CoreMark** | **378.21 iter/s — 2.251 CoreMark/MHz** |
| SRAM write / read / copy | 134.3 / 95.8 / 27.9 MB/s |
| CCM write / read / copy | 134.3 / 96.1 / 28.0 MB/s |
| Flash read, ART on / off | 26.6 / 9.2 MB/s (2.90×) |
| Flash erase / program | 126.1 / 283.8 KB/s |
| Flash full-chip sweep | 976 KiB erased + programmed + verified, 0 mismatches |

## Chip-specific notes

**The FPU is worth 19.75×.** The M4F FPU is single-precision only, so a
double-precision workload of identical shape falls back to
`__aeabi_dmul`/`__aeabi_dadd`. That ratio is the direct measure of what the
FPU buys you on this part.

**SRAM and CCM measure the same** (within 0.3%). Both are zero-wait-state,
and these loops are issue-limited rather than memory-limited. CCM's
advantage only appears under DMA bus contention, which this suite doesn't
create. The reason to care about CCM is that **DMA cannot reach it**.

**Flash erase is slow in absolute terms** (126.1 KB/s) because STM32F4
erases in 16 KB–128 KB sectors at roughly 1 s per 128 KB sector, by
construction — the memory technology, not a defect. Read throughput is a
different matter: the flash is on-die behind the ART accelerator, which is
worth 2.90× on a sequential read.

**Single core.** The STM32F407 has one Cortex-M4F, so there is no multi-core
scaling figure to report.

## Board quirks worth knowing before you start

**No virtual COM port.** The on-board probe is ST-LINK/V2 (USB PID `0x3748`)
— a single vendor-specific SWD interface, no USB CDC. There is no
`/dev/cu.usbmodem*`. Only ST-LINK/V2-1 (`0x374B`) and V3 carry a VCP.

**SWO doesn't work either.** ITM/SWO output is implemented in
`benchmark/stm32f407/platform.c`, but `st-trace` fails against this probe
with `read_trace insufficient buffer length`. The working console is
**semihosting over SWD**, which is what the benchmark uses.

**`WFI` kills the debug interface** unless `DBGMCU_CR.DBG_SLEEP` is set —
`st-info --probe` reports `chipid 0x000` and the board looks bricked.
Recover with `st-flash --connect-under-reset`.

**newlib-nano's `printf` has no `long long`.** `%llu` prints literally as
`lu` *and* desynchronises the varargs, so every following `%f` on that line
prints `0.00` — timings look like hard zeros rather than a format bug.
`-u _printf_float` enables float formatting; there is no equivalent switch
for `long long`. Use `%lu` with 32-bit casts.

Each of these cost real debugging time and is written up in full in the
relevant folder's README.

## Toolchain

```
brew install stlink          # st-flash, st-util, st-info, st-trace
# arm-none-eabi-gcc from the Arm GNU Toolchain
```

Built and verified with GCC 15.3.1, `-O2`, hard-float ABI, stlink 1.8.0, on
macOS. No CMake, no SDK, no vendor libraries.
