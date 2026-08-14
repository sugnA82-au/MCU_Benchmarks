# RP2040 baremetal CPU / SRAM / Flash benchmark

Self-contained diagnostic firmware for RP2040 boards: exercises both CPU
cores, sweeps the entire SRAM and flash for correctness, and runs CoreMark
as a portable baseline you can compare against any other MCU. No RTOS —
runs directly on pico-sdk's minimal runtime, output over USB CDC serial.

For basic toolchain/pico-sdk setup, see `../bringup/README.md` — that
covers installing cmake/ninja/arm-none-eabi-gcc and cloning pico-sdk, which
this project also needs and won't repeat here.

## What it tests

**CPU** (`main.cpp`):
- Integer ALU throughput, both single-core and genuinely concurrent
  dual-core (verified via a FIFO handshake, not just launched-and-hoped)
- RP2040's dedicated hardware integer divider
- Software float performance (RP2040's Cortex-M0+ has no FPU — every float
  op is emulated, and this quantifies exactly how expensive that is)
- A determinism check: the same workload run twice must produce identical
  checksums, as a cheap correctness signal independent of raw speed
- **CoreMark** (vendored from the official EEMBC repo, `coremark/`,
  unmodified except one line exposing the final score for reporting — see
  `coremark/LICENSE.md`, Apache 2.0). This is the one score worth comparing
  against other chips' datasheets; the custom loops above only mean
  anything relative to themselves on this board.

**Memory**: write/read/copy bandwidth plus four address/pattern integrity
tests (address, ~address, 0xAA, 0x55), covering 230 KiB of the chip's
264 KiB SRAM. The remaining ~34 KiB is live stack/USB-buffer state that
code running on top of it can't test from the inside.

**Flash**: XIP read bandwidth (cached vs streaming), then a full sweep
that erases, programs (with a sector-and-offset-dependent pattern, to
catch address-aliasing faults, not just stuck bits), and verifies **every
spare sector on the chip** — all of flash except the ~48 KiB this program
itself occupies — then erases it all back to blank afterward. This is a
real write/read test across the whole chip, not a sample.

## Building

```
mkdir build && cd build
cmake -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release ..
ninja
```
Produces `rp2040_bench.uf2`. Full run takes roughly 45–60 seconds once
flashed (dominated by the flash sweep, ~2.3s, and CoreMark, which
self-calibrates to a ~10s+ measured run regardless of the chip's actual
speed).

## Flashing and viewing output

1. BOOTSEL into the bootloader, copy `rp2040_bench.uf2` to the `RPI-RP2`
   drive.
2. Open the new serial port at 115200 baud (e.g. `screen /dev/cu.usbmodemXXXX 115200`
   on macOS) **before** or immediately after it enumerates — the firmware
   waits up to 20s for a terminal to attach, then proceeds regardless, so
   early output is lost if nothing's listening yet.
3. To reflash later: this firmware listens for any byte on serial and
   calls `reset_usb_boot()` to drop back into the bootloader on its own —
   no physical BOOTSEL press needed for iteration.

## Final result (this board)

RP2040 chip rev 2, 125 MHz, 2 MB flash, 264 KB SRAM. Full raw log in
`serial_log.txt`; summary:

| Test | Result |
|---|---|
| Integer ALU (single core) | 8.93 Mops/s |
| Hardware divider | 2.60 Mops/s |
| Soft-float (no FPU) | 0.60 Mops/s |
| Dual-core (concurrent) | 15.62 Mops/s combined — 1.75x single-core scaling |
| **CoreMark** | **226.5 iterations/sec — 1.81 CoreMark/MHz** |
| SRAM write / read / copy | 38.2 / 54.7 / 153.3 MB/s (230 KiB tested, 0 integrity mismatches) |
| Flash XIP cached / streaming read | 12.4 / 5.1 MB/s |
| Flash full-chip sweep | 500 sectors, 2000 KiB erased+programmed+verified, 0 mismatches |
| Flash erase / program (aggregate) | 15.7 MB/s / 1.03 MB/s |

**All checks passed** — CPU (both cores), SRAM, and flash all verified
working and fully accessible.

The 1.75x (not 2x) dual-core scaling and the ~15x gap between integer and
soft-float throughput are both real architectural characteristics of
RP2040, not bugs — see the conversation history / commit notes for the
full explanation of *why* (shared flash bus contention between cores; no
hardware FPU on the M0+).

## Bugs found and fixed during development (worth knowing if you extend this)

- **Compiler CSE silently skipped a repeated benchmark call.** GCC's
  IPA-pure-const pass proved `integer_workload(same_args)` called twice was
  redundant and reused the first result instead of re-executing — this
  would have both invalidated the determinism check (always "passes",
  even on broken hardware) and produced a bogus near-instant dual-core
  timing. Fixed with `asm volatile("" ::: "memory")` inside the workload
  functions.
- **The dual-core test wasn't actually concurrent at first.** core0 waited
  on a FIFO pop for core1's "done" signal *before* starting its own loop —
  meaning the two ran sequentially, not in parallel. Fixed by having core1
  signal "started" immediately instead of "finished".
- **`flash_get_unique_id()` hung the board** after a software-triggered
  bootloader re-entry (via `reset_usb_boot()`) specifically — worked fine
  after a clean power-on-reset. Likely a clone-flash-chip quirk with that
  specific ROM command. Not essential to the benchmark's purpose, so it
  was removed rather than debugged further.

## Files

- `main.cpp` — everything except CoreMark
- `coremark/` — vendored EEMBC CoreMark source (`core_*.c`, `coremark.h`,
  unmodified) plus this project's RP2040 port (`core_portme.c/h`)
- `serial_log.txt` — full raw output from the run summarized above
