# CH32V003 baremetal CPU / SRAM / flash / CoreMark benchmark

Self-contained diagnostic firmware for a CH32V003F4P6 board: exercises the CPU,
sweeps SRAM for correctness, measures the flash read path against SRAM, erases
and reprograms every page of flash past the end of its own program image, and
runs CoreMark as a portable baseline — with a caveat about the last one that
the firmware prints itself.

**No RTOS, no vendor HAL.** `ch32fun`'s register headers, startup code and SWIO
debug-print channel, the CH32V003 reference manual, built with WCH's RISC-V GCC
and flashed with `wlink` through a WCH-LinkE. The platform and framework are
pinned to exact commits in `platformio.ini`.

## Two images, not one

16 KB of flash does not hold CoreMark and the rest of the suite and still leave
room for a flash sweep. The first, combined build **overflowed flash by 10,468
bytes** at link time. So the suite is two PlatformIO environments sharing one
platform layer (`platform.c/h`):

| Environment | What it is | Flash | Static SRAM |
|---|---|---:|---:|
| `suite` (default) | CPU, SRAM, flash read, flash sweep — `main.c` | 12,800 B | 1,212 B |
| `coremark` | CoreMark, 1200-byte profile run — `coremark_main.c` | 14,460 B | 1,256 B |
| `conformant` | CoreMark, 2000-byte dataset — **does not link** | — | 2,056 B |

Splitting the images costs nothing in comparability: every workload and constant
is the same, and each image prints its own hardware checks.

## What it tests

**CPU** (`main.c`):

- Integer ALU throughput — a deterministic mul/add/xor/shift mix. Its checksum
  doubles as a cross-chip correctness check: 20,000,000 iterations must produce
  `f2c53d7b` on any conforming implementation.
- **Software 32-bit divide.** RV32EC has no M extension, so this measures
  libgcc's `__udivsi3`/`__umodsi3` rather than a hardware divider.
- **Software single-precision float.** This part has no FPU, so every operation
  is a call into `__mulsf3`/`__addsf3`/`__gesf2`. The reference suite prices an
  FPU by running the same loop in a precision the FPU does not cover; with no
  FPU there is no second path to compare against, and the firmware says so in
  its own output instead of printing an unlabelled number.
- A **determinism check** — the same workload run twice must produce identical
  checksums, a correctness signal independent of speed.
- Every CPU row also reports **cycles per iteration**, which is independent of
  the clock's exact frequency (see "Reading these numbers").
- **CoreMark** (`coremark_main.c`), vendored unmodified from the EEMBC source
  (`coremark/`, Apache 2.0, see `coremark/LICENSE.md`). **This chip cannot run a
  conformant CoreMark** — see below.

**Memory**: write/read/copy bandwidth plus four address/pattern integrity sweeps
(address, ~address, `0xAAAAAAAA`, `0x55555555`) over 1200 of the 2048 bytes of
SRAM. The untested remainder is `.data`/`.bss` and the stack: state the testing
code is itself running on top of. A canary painted across free RAM at start-up
and checked at the end is what proves the budget actually held.

**Flash read**: sequential word reads from flash against the identical loop
reading SRAM. Both loops fetch their own instructions from flash, so the gap
between the rows is data and instruction fetch sharing the one flash port.

**Flash sweep**: every 64-byte page from the first page fully past this image
to the end of flash is erased, checked blank, programmed with a page-and-offset
dependent pattern, verified byte for byte, and erased back to blank. The start
page is computed from linker symbols (`_data_lma` + the size of `.data`), never
hardcoded, so the sweep cannot touch the running program. Errors come from the
flash controller's own status register (`FLASH_STATR.WRPRTERR`, `BSY` timeout),
not just from the read-back comparison. The CPU keeps executing from flash while
the controller is busy — fetches stall until `BSY` clears — so the driver does
not need to run from RAM; the sweep verifying is the evidence.

## Building

```
pio run -e suite           # -> .pio/build/suite/firmware.elf
pio run -e coremark
pio run -e conformant      # fails at link, on purpose (see below)
pio run -e suite -t upload # wlink through the WCH-LinkE
```

Needs PlatformIO. No Make, no vendor SDK download — the framework comes from the
pinned `ch32fun` commit.

## Running and viewing output

The console is the WCH-Link debug channel on the SWIO pin (PD1), so the probe has
to stay attached and a host has to be draining it:

```
pio run -e suite -t upload
python3 capture.py --output serial_log.txt --done SUITE_DONE
python3 wallclock_check.py serial_log.timing.json
```

`capture.py` runs `minichlink -b -T` (reboot, then attach the terminal) so it
catches the run from its first line, saves the raw output, and records when every
`@BEGIN`/`@END` marker line arrived on the host. `wallclock_check.py` compares
those host timestamps with the chip's own timing — a clock that does not depend
on the chip.

A full suite run takes a bit over two minutes, almost all of it CPU: 20
million integer iterations, 5 million software divides and 2 million software
float steps on a core with no multiplier. The CoreMark image takes about half a
minute. There is no LED status convention: the console pin is the programming
pin, so the console is the only diagnostic.

**The flash sweep erases and reprograms the 3,584 bytes after the program image**
and leaves them blank (`0xFF`). The program image itself is never touched.

## Results

CH32V003F4P6, 48 MHz, 16 KB flash, 2 KB SRAM.

**Suite 20/20 checks passed, CoreMark 9/9, 0 failures.** Full per-test breakdown
in [`results.md`](results.md), raw output in [`serial_log.txt`](serial_log.txt)
and [`coremark_log.txt`](coremark_log.txt).

| Test | Result |
|---|---|
| Integer ALU | 1.4118 Mops/s — **34.00 cycles/iter** |
| 32-bit divide (software) | 0.0636 Mops/s — **755.27 cycles/iter** |
| Float (software, no FPU) | 0.0624 Mops/s — **769.00 cycles/iter** |
| **CoreMark** | **115.858 iter/s, 2.4131/MHz — reduced 1200-byte dataset, not conformant** |
| SRAM write / read / copy | 23.920 / 23.940 / 15.859 MB/s |
| SRAM integrity | 4 pattern sweeps, 0 mismatches |
| Flash word read vs SRAM | 19.198 / 23.939 MB/s — 10.00 vs 8.02 cyc/word (1.25×) |
| Flash erase / program | 20.46 / 22.23 KB/s — 3,128 / 2,879 µs per 64-byte page |
| Flash sweep | 3,584 B erased + programmed + verified, 0 mismatches, 0 controller errors |
| RAM headroom | **356 bytes of 2048** never touched by the stack (suite image) |

Integer checksum came out `f2c53d7b` — the cross-chip reference value. All three
CPU timings reproduced to the microsecond across two full runs, and CoreMark's
103,575,400 ticks reproduced exactly across two.

### Reading these numbers

- **CoreMark here is not a CoreMark score, and the margin was measured.** The
  conformant 2000-byte configuration needs 2,056 bytes of static SRAM — 8 more
  than the chip has, before any stack — and `ld` says so: `region 'RAM'
  overflowed by 8 bytes`. CoreMark's own stack high-water in the 1200-byte run
  is 392 bytes, so 2,056 + 392 = 2,448 needed against 2,048. The 1200-byte
  profile-run configuration was used instead; CoreMark validates it against its
  own known CRCs, so the arithmetic is proven correct, but less data per
  iteration means less work per iteration. Not comparable with published
  CoreMark 1.0 numbers or with 2000-byte runs elsewhere. The firmware prints that
  warning above the number itself.
- **CoreMark's own `Iterations/Sec` line says 117; the score is 115.858.**
  CoreMark times in whole seconds on a part without floating point, so it divides
  2000 iterations by 17 s. The figure here is 2000 × 6,000,000 / 103,575,400
  ticks — see "bugs" below.
- **Cycles per iteration is the number worth carrying away.** The chip's clock
  reads 0.41–0.45% long against the host's wall clock under load (+0.28% idle):
  the 24 MHz HSI is a factory-trimmed RC, ±1%, and it is running a little fast.
  Mops/s and CoreMark/MHz are per *nominal* 48 MHz and inherit that; cycles per
  iteration do not, because both sides of the ratio are counted in the same
  ticks.
- **Multiplies are free of libgcc here, divides are not.** GCC at `-O2` expands a
  multiply by a constant into shifts and adds — no `__mulsi3` call appears in the
  integer, divide or memory loops — which is why the integer round is 34 cycles.
  The divide loop makes two libgcc calls per iteration (`__udivsi3`,
  `__umodsi3`), which is where its 755 cycles go.
- **The copy row is a hand-written word loop**, not a libc `memcpy`: the build
  links no libc, so it says more about the loop than about the silicon.
- **The flash/SRAM ratio is 1.25× between two loops that both run from flash.**
  It is not "flash is 1.25× slower than SRAM" in isolation.
- **There is no flash cache or accelerator to switch.** The only timing control
  is `FLASH_ACTLR.LATENCY`, which must be 1 at 48 MHz, so there is no
  on-versus-off row.

## Bugs found and fixed during development (worth knowing if you extend this)

- **The combined build did not fit in flash.** CoreMark's six objects come to
  ~9 KB, `ch32fun`'s printf and start-up to ~5 KB, and the first version of this
  suite — 64-bit ratio arithmetic and long explanatory `printf` prose — compiled
  to a 12 KB `main.o` on its own (about 4.9 KB of it code, the rest mostly
  strings). `ld` reported `region FLASH overflowed by 10468 bytes`. Fixed by
  splitting into two images, doing all fixed-point work in 32-bit integers (long
  division, no `uint64_t`), and moving the prose into these READMEs. Check
  `Flash:` in the PlatformIO summary after every build; on a 16 KB part it is the
  number that matters.
- **`printf("literal\n")` becomes `puts()` and the console dies.** Without
  `-fno-builtin` GCC makes that substitution, and the next `printf` line printed
  as `RCC_CTLR= 0x<NUL>000000a2 0x<NUL>00001880 …` with nothing after it, ever.
  The same source with only `-fno-builtin` added was fine. Reproducible, cause not
  tracked down; both images (and `../bringup`) build with the flag.
- **CoreMark's score was off by 1%, and it was integer division.** With
  `HAS_FLOAT 0` CoreMark's `time_in_secs()` is an integer, so 17.263 s becomes 17
  and `Iterations/Sec` becomes 2000/17 = 117 instead of 115.858. Fixed without
  touching a single CoreMark `.c` file: `core_portme.h` points `ee_printf` at
  `coremark_printf()`, which forwards to `vprintf` and captures the `Total ticks`
  and `Iterations` values CoreMark prints; the harness then does the division in
  32-bit fixed point. The vendored sources are byte-identical to upstream.
- **A subtraction underflowed in the "does it fit" message.** The first CoreMark
  run printed "leaving 4294967288 bytes for the stack". The conformant
  configuration is 8 bytes *over* the SRAM before any stack exists; the unsigned
  `2048 - 2056` wrapped. Fixed by printing the signed overage — and it turned out
  to be a useful cross-check, because it agrees with the linker's own number.
- **This printf has no `%f`.** `ch32fun`'s `mini_vsnprintf` handles `%d %u %x
  %X %c %s`, with `l` and zero-padded widths — it has no float conversion, and
  nothing here relies on `long long` or `*` widths. Rates are therefore printed by
  `pr_ratio()` in `platform.c`, a 32-bit long division that rounds to nearest,
  and the firmware prints a known value (`3.14159`, and `123456789 89abcdef 007`
  through `snprintf`) at start-up and checks it, so a mangled format cannot
  silently corrupt a whole log.
- **A self-check that checked nothing was removed from the bring-up.** It
  compared SysTick's elapsed count with the number of ticks the loop had just
  waited for on SysTick. The honest check is the host wall clock, and that is
  what `wallclock_check.py` does.
- **Two timebase facts that look like bugs but are not.** SysTick is 32-bit and
  wraps every ~715 s, so every timed region is an unsigned subtraction and the
  longest one here (the software divide, 78.7 s) is far inside the wrap. And
  `pio run -t upload` flashes the ELF without regenerating `firmware.bin`, so a
  `.bin` in the build directory can be stale — compare disassembly, not `.bin`
  files, when asking "is this the image that ran?".

## Files

- `main.c` — the suite image: CPU, SRAM, flash read, flash sweep
- `coremark_main.c` — the CoreMark image
- `platform.c/h` — fixed-point printing, flash page driver, RAM canary, shared
  system-info checks
- `platformio.ini`, `build_options.py` — the three environments (the script
  strips `ch32fun`'s LTO so the benchmark translation units stay separate)
- `capture.py`, `wallclock_check.py` — host-side capture and clock cross-check
- `coremark/` — vendored EEMBC CoreMark plus this port's `core_portme.c/h`
- `results.md` — full per-test results
- `serial_log.txt`, `coremark_log.txt` — raw console output from the runs in
  `results.md`; `*.timing.json` — host arrival times of the marker lines
