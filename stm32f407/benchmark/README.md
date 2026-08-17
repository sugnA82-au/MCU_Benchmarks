# STM32F407 baremetal CPU / SRAM / CCM / Flash benchmark

Self-contained diagnostic firmware for the STM32F4 Discovery: exercises the
CPU and FPU, sweeps main SRAM, CCM and the entire flash array for
correctness, and runs CoreMark as a portable baseline. No RTOS, no HAL, no
CMSIS — hand-written register access against RM0090, built with nothing but
`arm-none-eabi-gcc` and `stlink`.

## What it tests

**CPU** (`main.c`):

- Integer ALU throughput — a deterministic mul/add/xor/shift mix
- The core's **native hardware divider** (`UDIV`/`SDIV`, in-core on Cortex-M4,
  not a separate peripheral)
- **Hardware FPU vs software emulation.** The M4F FPU is single-precision
  only, so the single-precision loop executes natively while the
  double-precision loop of identical shape falls back to
  `__aeabi_dmul`/`__aeabi_dadd`. The ratio between them is a direct measure
  of what the FPU is worth on this chip.
- A **determinism check** — the same workload run twice must produce
  identical checksums, a correctness signal independent of raw speed.
- **CoreMark**, vendored unmodified from the EEMBC source (`coremark/`,
  Apache 2.0, see `coremark/LICENSE.md`) except for one line exposing the
  final score for reporting. This is the one figure here directly comparable
  against published numbers for any other MCU; the custom loops above only
  mean anything relative to themselves.

**Memory**: write/read/copy bandwidth plus four address/pattern integrity
tests (address, ~address, 0xAA, 0x55), run separately over:

- **96 KiB of the 128 KiB main SRAM.** The untested 32 KiB is live
  `.data`/`.bss`, the heap/stack reservation and CoreMark's working set —
  state the testing code is itself running on top of.
- **All 64 KiB of CCM**, the core-coupled RAM at `0x10000000`. Nothing else
  in this program lives there, so all of it is testable. CCM is
  zero-wait-state on the D-bus but **not reachable by DMA**, which is the
  real reason the distinction matters.

**Flash**:

- **ART accelerator effect** — the same 64 KiB read with the prefetch buffer
  and I/D caches on, then off. Wait states are held at 5 in both cases, so
  this isolates the accelerator rather than the wait-state setting.
- **Full-chip sweep** — erases, programs with a sector-and-offset-dependent
  pattern (so a fault that aliases one address onto another shows up, not
  just stuck bits), and verifies **every sector this binary does not
  occupy** — 976 KiB of the 1 MB — then erases it all back to blank. A real
  write/read test across the whole chip, not a sample.

The flash program/erase routines run from RAM (`.ramfunc`, copied at
startup) with interrupts masked, because on STM32F4 any read of the flash
array while it is being programmed or erased stalls the bus (RM0090 3.5.1).

## Building

```
make            # -> stm32f407_bench.bin
make flash      # st-flash --connect-under-reset
make clean
```

Needs `arm-none-eabi-gcc` and `stlink` (`brew install stlink`). Nothing else
— no SDK, no CMake, no vendor libraries.

## Running and viewing output

The board's ST-LINK/V2 has **no virtual COM port**, so output goes over
semihosting, which needs a debugger attached:

```
# terminal 1 - the console appears in st-util's own stdout
st-util --semihosting

# terminal 2
arm-none-eabi-gdb -q stm32f407_bench.elf \
  -ex "target extended-remote :4242" -ex "continue"
```

Full run is about 60 s, dominated by the flash sweep (~11.5 s of erase +
program, plus the same again for cleanup) and CoreMark (self-calibrates to
a ~10 s measured run regardless of chip speed).

LEDs mirror progress without a debugger: green on at start, blue once the
CPU phase is done, orange pulsing at the end means finished, red means at
least one check failed.

## Results

STM32F407VGT6, 168 MHz, 1 MB flash, 128 KB SRAM + 64 KB CCM.

**17/17 checks passed, 0 failures.** Full per-test breakdown and the raw
log are in [`results.md`](results.md) and `semihost_log.txt`.

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
| Flash full-chip sweep | 9 sectors, 976 KiB erased+programmed+verified, 0 mismatches |
| Flash erase / program | 126.1 KB/s / 283.8 KB/s |

**All 17 checks passed** — CPU, FPU, SRAM, CCM and flash all verified
working and fully accessible.

### Reading these numbers

- **The FPU is worth 19.75×** (14.00 vs 0.71 Mops/s). That gap is entirely
  the difference between native single-precision VFP instructions and
  library calls for double precision — same loop shape either way.
- **SRAM and CCM measure the same**, within 0.3%. Expected: both are
  zero-wait-state and these loops are issue-limited rather than
  memory-limited. CCM's advantage shows up under DMA bus contention, which
  this suite doesn't create. The reason to care about CCM is that DMA
  cannot reach it at all.
- **The copy row measures newlib-nano's `memcpy`**, not the memory system —
  it is markedly more conservative than a hand-tuned routine. Write and read
  are plain C loops and reflect the memory path more directly.
- **The ART-off figure is code-placement sensitive** — treat it as an
  order of magnitude, not three significant figures. With the prefetch
  buffer and I-cache off, instruction fetch is directly exposed to the 5
  wait states, so throughput depends on where the read loop lands relative
  to flash line boundaries. An edit that only removed three lines of
  unrelated `printf` once moved it 11%, with the measurement loop untouched.
  The ART-*on* figure barely moves, which is precisely the point of the
  accelerator. See `results.md` for the full note.
- **Flash erase is slow in absolute terms** (126.1 KB/s) because STM32F4
  erases in 16 KB–128 KB sectors at roughly 1 s per 128 KB sector, by
  construction. That is the memory technology, not a fault. Read throughput
  is a different story — the flash is on-die behind the ART accelerator.

## Bugs found and fixed during development (worth knowing if you extend this)

- **`WFI` kills the debug interface.** The first version's delay loop used
  `WFI`; after flashing, `st-info --probe` reported `chipid 0x000` and the
  board looked bricked. On STM32F4 the debug interface stops responding
  during sleep unless `DBGMCU_CR.DBG_SLEEP` is set. Fixed by setting the
  sleep/stop/standby bits before the first `WFI`. Recovery, if you hit this:
  `st-flash --connect-under-reset`.
- **newlib-nano's `printf` has no `long long` support.** Every `%llu`
  printed literally as `lu` and, worse, desynchronised the varargs so every
  following `%f` on the same line printed `0.00` — the timings looked like
  hard zeros rather than a format bug. `-u _printf_float` enables float
  formatting but there is no equivalent switch for `long long`. Fixed by
  using `%lu` with 32-bit casts throughout; TIM2 wraps at 71 minutes so
  microsecond durations fit fine.
- **SWO/ITM output does not work on this board.** ITM was implemented first
  and is still in `platform.c`, but `st-trace` fails with `read_trace
  insufficient buffer length` against this on-board ST-LINK/V2. Semihosting
  over SWD was used instead. The ITM path is left in and is selected
  automatically when no debugger is attached, where it harmlessly drops
  bytes rather than blocking.
- **`st-util` resets the target on every attach.** Early debugging looked
  like the firmware was hanging in the startup delay; it was actually
  restarting each time GDB connected. Registers read straight after attach
  are reset values, not the running state — a stale `st-util` also returned
  pure garbage for `pc` until restarted.
- **Flash program/erase must run from RAM.** On STM32F4 any read of the
  flash array while it is being programmed or erased stalls the bus
  (RM0090 3.5.1). The erase/program routines are placed in `.ramfunc` and
  copied to SRAM at startup; interrupts are masked around each operation.

## Files

- `main.c` — the benchmark suite
- `platform.c/h` — clocks, TIM2 timebase, semihosting + ITM console,
  RAM-resident flash driver
- `startup.c` — vector table, `.data`/`.bss`/`.ramfunc` init, FPU enable
- `stm32f407.h` — hand-written register definitions (no CMSIS)
- `stm32f407.ld` — linker script: flash, SRAM, CCM, `.ramfunc`, heap/stack guard
- `coremark/` — vendored EEMBC CoreMark plus this port's `core_portme.c/h`
- `results.md` — full per-test results
- `semihost_log.txt` — raw console output from the run in `results.md`
