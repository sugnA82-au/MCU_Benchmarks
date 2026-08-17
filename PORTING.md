# Porting the benchmark suite to a new MCU

A checklist for adding a chip to this repo so its numbers mean something
next to the others. Written after doing it for the STM32F407; the
`stm32f407/` folder is the reference implementation of everything below.

The single rule everything else serves: **the workloads must be identical
across chips, and anything that can't be must be labelled, not fudged.**

---

## 1. Directory layout

Each MCU gets one top-level folder named after the chip, lowercase, no
vendor prefix or suffix — `rp2040/`, `stm32f407/`, `nrf52840/`.

```
<mcu>/
├── README.md            chip-level: headline result, results summary,
│                        chip-specific notes, board quirks, toolchain
├── bringup/
│   ├── README.md        what it proves, plus hardware-verified evidence
│   ├── .gitignore
│   └── <sources>        minimal blink/GPIO, sources directly in this folder
└── benchmark/
    ├── README.md        what it tests, how to build/run, results, bugs found
    ├── results.md       the full per-test result table
    ├── <log file>       raw console capture backing results.md
    ├── .gitignore
    ├── coremark/        vendored EEMBC source + this chip's core_portme.c/h
    └── <sources>        sources directly in this folder - no nested subfolder
```

Two things people get wrong:

- **No nested chip folder inside `benchmark/`.** Sources sit directly in
  `<mcu>/benchmark/`. (If your local working copy has
  `benchmark/<mcu>/...` because the build tool needs a sibling `build/`
  directory, flatten it when you upload.)
- **No build artifacts.** `.gitignore` in each buildable folder:

  ```
  build/
  .DS_Store
  *.o
  *.elf
  *.bin
  *.hex
  *.map
  ```

Everything committed should be source, docs, or captured results.

---

## 2. Order of work

Do these in order. Each phase de-risks the next, and skipping ahead is how
you end up debugging three things at once.

### Phase 0 — Before writing code

- Get the **reference manual** and the **board manual**. These are the
  source of truth, not blog posts. For STM32F407 that was RM0090 and UM1472.
- Identify the **debug probe** and how you will get text off the board.
  Do this now — it is the single most likely thing to derail you (§4).
- Confirm your **flash tool** can see the chip before writing any firmware.

### Phase 1 — Bring-up (`bringup/`)

Prove the board works before trying to measure it.

1. Blink an LED at the chip's **rated clock**, not its reset default. Getting
   the PLL up is most of the work and everything downstream depends on it.
2. Read a button, so you have an input path too.
3. **Verify against hardware, not assumption.** Attach a debugger and read
   the clock registers back. For the STM32 that meant confirming
   `RCC_CFGR.SWS` said the PLL really was the system clock, flash wait
   states matched, and the tick count was exactly right over a known number
   of LED steps. Put that evidence table in `bringup/README.md`.

Do not move on until the clock is confirmed. Every rate you report later is
divided by it.

### Phase 2 — Platform layer

Three things the benchmark needs from the chip:

**A timebase.** Requirements:

- Free-running, independent of the core clock, ideally **1 MHz** so printed
  durations are microseconds with no conversion.
- Wide enough not to wrap mid-run (32-bit at 1 MHz = 71 minutes; fine).
- **Must keep counting when interrupts are masked**, because the flash
  driver masks them. If you extend a 32-bit counter to 64 with an overflow
  ISR, handle the case where the counter wrapped but the ISR has not run:

  ```c
  uint64_t time_us_64(void) {
      uint32_t ovf = g_ovf, cnt = TIMER->CNT;
      if (TIMER->SR & OVERFLOW_FLAG) { cnt = TIMER->CNT; ovf++; }
      return ((uint64_t)ovf << 32) | cnt;
  }
  ```

**A console.** See §4 — this is chip- and board-specific.

**A flash program/erase driver.** See §5.

### Phase 3 — The benchmark suite

Copy `stm32f407/benchmark/main.c` and adapt. Keep §3 invariants intact.

### Phase 4 — CoreMark

Vendor the EEMBC source unmodified. Write only `core_portme.c/h`.

### Phase 5 — Run, capture, document

Run to completion, capture the raw console output to a file, write
`results.md` from that file, write the two READMEs, add the leaderboard row.

---

## 2a. If the chip needs a vendor SDK

The STM32F407 port is bare-metal with a hand-written Makefile because it
could be. Plenty of chips can't — RP2040 needs pico-sdk and CMake, ESP32
needs ESP-IDF, nRF52 effectively needs its SDK for anything involving the
softdevice. **That is fine**, and it is what the repo's methodology means by
"whatever the vendor's minimal SDK provides, and nothing more."

What changes:

- **Build system.** CMake instead of Make; the folder holds
  `CMakeLists.txt` and whatever SDK import shim is required.
  `.gitignore` needs `build/` (it already does).
- **The platform layer gets shorter.** Most SDKs already give you the three
  things §2 asks for:

  | You need | SDK usually provides |
  |---|---|
  | 1 MHz-ish free-running timebase | a `time_us_*()` style call — check it is hardware-backed and survives IRQs being masked |
  | console | USB CDC / UART stdio, usually a one-liner to enable |
  | flash program/erase | an erase/program API, often with a "run this safely" wrapper that parks other cores and handles the XIP/cache problem for you |

  Use them. Re-implementing an SDK's flash driver from registers adds risk
  and measures nothing extra.
- **Language.** The RP2040 port is C++; this one is C. Either is fine —
  keep CoreMark compiled as C.

What does **not** change: everything in §3, the layout in §1, the
substitution policy in §6, and the documentation requirements in §8. The
invariants are the whole point; the build system is an implementation
detail.

One caution: an SDK's `memcpy`, `printf` and startup code differ from
newlib's, and those differences show up in the results (see the `memcpy`
note in §6). Record the SDK version alongside the compiler version.

---

## 2b. What this guide can't do for you

Be realistic about where the time goes. No document removes these — they
are per-chip by nature and come out of the reference manual:

- Register definitions, if going bare-metal (or the SDK's equivalent calls)
- Clock tree bring-up to the rated speed — usually the single biggest chunk
- Flash geometry: sector map, program granularity, wait states, any
  execute-from-RAM requirement
- Which timer peripheral to use and how to derive an exact 1 MHz tick
- Which console transport that specific *board* actually supports

Expect bring-up (Phase 1) and the flash driver (Phase 2) to dominate.
Phases 3–5 are largely copy-and-adapt once those two work.

---

## 3. The invariants — do not change these

These are what make cross-chip numbers comparable. Copy them verbatim.

### Iteration counts

```c
#define INT_ITERS    20000000u
#define FLOAT_ITERS   2000000u
#define DIV_ITERS     5000000u
```

### CPU workloads

Copy byte-for-byte. The `asm volatile("" ::: "memory")` barrier is **not
optional** — without it GCC's IPA-pure-const pass proves a repeated call
with the same argument is redundant and reuses the first result, which
silently skips the work and makes the determinism check pass
unconditionally.

```c
__attribute__((noinline)) static uint32_t integer_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    uint32_t a = 0x12345678u, b = 0x9ABCDEF0u;
    for (uint32_t i = 0; i < iters; i++) {
        a = (a * 1664525u + 1013904223u) ^ b;
        b += (a << 3) ^ (a >> 5) ^ 0xA5A5A5A5u;
    }
    return a ^ b;
}

__attribute__((noinline)) static float float_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    float f = 1.0f;
    const float k1 = 1.0000001f, k2 = 0.0000001f;
    for (uint32_t i = 0; i < iters; i++) {
        f = f * k1 + k2;
        if (f > 1.0e6f) f *= 1.0e-6f;
    }
    return f;
}

__attribute__((noinline)) static uint32_t divider_workload(uint32_t iters)
{
    __asm__ volatile("" ::: "memory");
    uint32_t acc = 0, n = 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= iters; i++) {
        uint32_t d = (i & 0xFFFFu) | 1u;   // avoid div-by-zero
        acc += (n / d) + (n % d);
        n = n * 2654435761u + 1u;
    }
    return acc;
}
```

The integer workload's checksum is also a **cross-chip correctness check**:
for 20,000,000 iterations it must come out `f2c53d7b` on any conforming
implementation. A different value means the arithmetic differs (wrong
integer width, a compiler doing something exotic) and the score is not
comparable.

### Memory patterns

Four full sweeps per region, write-then-verify: `address`, `~address`,
`0xAAAAAAAA`, `0x55555555`.

### Flash sweep pattern

```c
uint8_t sweep_pattern_byte(uint32_t sector, uint32_t offset) {
    return (uint8_t)(sector * 173u + offset * 31u + 7u);
}
```

Depending on **both** sector and offset is the point: a fault that aliases
one address onto another shows up, not just stuck bits.

### CoreMark build settings

```
-DITERATIONS=0 -DPERFORMANCE_RUN=1
```

`ITERATIONS=0` self-calibrates to a ≥10 s measured run, per the official
reporting rules. Record the compiler version and flags — CoreMark is
compiler-sensitive and cross-compiler comparisons need that caveat.

### Determinism check

Run one workload twice; identical checksums required. This catches broken
hardware and broken optimisation independently of speed.

---

## 4. Getting text off the board

Decide this early. Options, best first:

| Option | Use when | Cost |
|---|---|---|
| **USB CDC** | chip has native USB, or probe has a VCP | none — best case |
| **UART → USB-serial adapter** | you have the adapter and free pins | extra hardware |
| **Semihosting over SWD/JTAG** | a debugger is attached anyway | debugger must stay attached; halts core per write |
| **ITM/SWO trace** | probe genuinely supports SWO capture | verify the *host tool* works, not just the pin |

Hard-won specifics:

- **Check for a VCP before assuming one.** ST-LINK/V2 (USB PID `0x3748`)
  has no USB CDC at all — only V2-1 (`0x374B`) and V3 do. On the STM32F4
  Discovery there is no `/dev/cu.usbmodem*` and never will be.
- **SWO working on paper ≠ SWO working.** ITM was implemented, the pin was
  configured, and `st-trace` still failed with `read_trace insufficient
  buffer length`. Budget time to test the host capture tool early.
- **Semihosting is the reliable fallback** but has two rules: it needs the
  debugger attached, and a semihosting trap with **no** debugger attached is
  a hard fault. Gate every call:

  ```c
  if (DHCSR & C_DEBUGEN) { /* safe to trap */ }
  ```

- **Never let a console write block forever.** If the transport isn't
  draining, drop the byte after a small bounded spin. A large guard costs
  milliseconds *per byte* when nothing is listening.
- **Keep console writes out of timed regions.** Semihosting halts the core
  while the timer keeps running; a `printf` inside a measurement inflates
  it. Print only between measurements.

Also give the board an **LED status convention** so it is diagnosable with
no console at all — e.g. green at start, another colour per phase, one
pulsing at the end for "finished", red for "a check failed".

---

## 5. The flash sweep

The most valuable test and the most dangerous. Rules:

1. **Never erase the sectors holding the running program.** Compute the
   binary's extent from a linker symbol and start the sweep at the first
   sector fully past it. Do not hardcode.
2. **Handle non-uniform sector maps.** STM32F407 is 4×16 KB, 1×64 KB,
   7×128 KB. A fixed sector size is a wrong assumption on many chips.
3. **Run program/erase code from RAM if the architecture requires it.** On
   STM32F4 any read of the flash array during program/erase stalls the bus,
   so the driver lives in a `.ramfunc` section copied to RAM at startup,
   with interrupts masked around each operation. Check your chip's manual.
4. **Verify every byte**, then **erase back to blank and verify blank.**
   Leaving the chip dirty makes the next run's numbers a lie.
5. **Report errors from the hardware's own status register**, not just from
   the readback comparison.

---

## 6. What is allowed to differ

Silicon differs. When a test has no counterpart, **replace it with the
closest architectural equivalent and say so in the output itself** — never
report a number that looks like the others but isn't.

Worked examples:

| Situation | Wrong | Right |
|---|---|---|
| Chip is single-core, suite has a dual-core test | Report the single-core figure as "combined" | Drop the test; print "this chip has one core, no multi-core scaling to measure" |
| Chip has no FPU | Report the software float number unlabelled | Label it as software emulation |
| Chip has an FPU | Report float only | Also measure the software path (e.g. `double` on a single-precision FPU) so the FPU's value is quantified |
| Different cache/accelerator | Silently omit | Measure it on/off with everything else held constant |
| One memory pool vs two | Merge them | Measure separately and note the constraint (e.g. CCM is not DMA-reachable) |

If a number is sensitive to something other than the chip, say so where it
is published. Two real cases from the STM32 port:

- The **copy** row measures the C library's `memcpy`, not the memory system.
  A hand-tuned assembly `memcpy` will beat a conservative one by 5×, which
  says nothing about the silicon.
- The **accelerator-off** flash read is **code-placement sensitive** — an
  edit that removed three lines of unrelated `printf` moved it 11%, because
  with no prefetch or cache the instruction fetch is directly exposed to
  wait states. Quote it as an order of magnitude, not 3 significant figures.

---

## 7. Traps that cost real time

Generalised from the STM32F407 port; most apply broadly.

- **`printf` in a size-optimised libc may silently mangle format
  specifiers.** newlib-nano drops `long long` support: `%llu` printed
  literally as `lu` *and* desynchronised the varargs so every following
  `%f` on that line printed `0.00`. The timings looked like hard zeros, not
  a format bug. `-u _printf_float` re-enables float formatting; there is no
  equivalent for `long long`. Use `%lu` with 32-bit casts. **Verify one
  known value by eye before trusting a whole log.**
- **`WFI`/sleep can kill the debug interface.** On STM32F4 the probe stops
  responding during sleep unless `DBGMCU_CR.DBG_SLEEP` is set — the board
  reads as `chipid 0x000` and looks bricked. Most vendors have an
  equivalent debug-during-sleep bit. Set it before the first sleep, and
  know your tool's connect-under-reset escape hatch.
- **Debug servers usually reset the target on attach.** Registers read
  straight after attaching are reset values, not running state. Early on
  this looked like a hang in a startup delay; the firmware was restarting
  every time GDB connected. Use a breakpoint and let it run, rather than
  reading registers on attach.
- **A stale debug server returns plausible-looking garbage.** A `pc` outside
  any valid memory region that *changes between reads* means the tool is
  broken, not the chip. Restart it before debugging further.
- **Enable the FPU before any float code runs**, in startup, before `main`.
- **Reserve heap and stack in the linker script.** newlib's `_sbrk` needs an
  `end` symbol; without it `malloc`/`printf` fail to *link*. Size test
  buffers so an overflow is a link error, not a run-time stack collision.

---

## 8. Documenting the result

### `results.md`

One run, one clean table. Every figure traceable to the committed raw log —
someone should be able to diff the two.

- Header: exact board, chip, clock, memory sizes, chip ID, compiler + flags.
- **Checks passed / failed**, stated plainly.
- Results grouped by subsystem: CPU, CoreMark, Memory, Flash.
- For CPU tests, include the raw work and time columns (iterations and
  microseconds) beside each rate so numbers are checkable, not asserted.
- CoreMark: score, **CoreMark/MHz**, iterations, measured time, compiler,
  and the self-validation line (`Correct operation validated` + CRCs).
- A short "reading these numbers" section for anything that would otherwise
  mislead. No cross-chip comparisons — the leaderboard is for that.

### `<mcu>/README.md`

Headline result, layout, results summary table, chip-specific notes, board
quirks, toolchain versions. Anything that cost you a day belongs in "board
quirks" — that is the part future-you actually rereads.

### `benchmark/README.md`

What it tests and why, build instructions, run instructions, the results
table, and a **"bugs found and fixed"** section. That section is worth as
much as the numbers.

### Leaderboard

Add one row to the repo-root `README.md`:

```markdown
| <MCU> | <n>x <core> | <clock> | <CoreMark> | <CoreMark/MHz> | [`<mcu>/`](<mcu>/) |
```

**CoreMark/MHz is the figure worth comparing** — it is the only one
normalised for clock speed. Everything else mixes architecture with how
fast you happened to clock the part.

---

## 9. Pre-upload checklist

- [ ] Builds clean from scratch (`make clean && make`) with no warnings
- [ ] No build artifacts committed; `.gitignore` in each buildable folder
- [ ] Sources directly in `<mcu>/benchmark/`, no nested chip subfolder
- [ ] Raw log committed, and every figure in `results.md` matches it
- [ ] Iteration counts and workloads unmodified (§3)
- [ ] Integer checksum is `f2c53d7b` for 20,000,000 iterations
- [ ] CoreMark reports `Correct operation validated`
- [ ] Flash sweep erased back to blank and verified blank
- [ ] Every substituted or omitted test is labelled in the firmware's own
      output, not only in the docs
- [ ] Compiler version and flags recorded
- [ ] No references to other chips in this folder — each reads standalone
- [ ] Leaderboard row added to the repo-root README
```
