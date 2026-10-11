# CH32V003 bring-up

Minimal known-good starting point for a CH32V003F4P6 board: prove the chip
boots, prove it is really running at 48 MHz from the PLL rather than at the
24 MHz reset default, and prove GPIO works in both directions. The benchmark
suite is built on the same foundations.

Bare metal — **no RTOS, no vendor HAL**. The only dependency is `ch32fun`'s
register headers, startup code and SWIO debug-print channel. 3676 bytes of
flash, 8 bytes of SRAM.

## What it does

PD4 is driven as a 1 Hz square wave for 60 seconds while the SWIO console
prints an evidence table at startup and a timed `@BEGIN`/`@END` pair around the
60 seconds. After that it keeps blinking forever.

That exercises, in order:

- **Clock source and rate** — `RCC_CTLR`, `RCC_CFGR0` and `FLASH_ACTLR` are
  read back from the running chip, so the clock configuration is reported rather
  than assumed.
- **SysTick** — free-running 32-bit counter at HCLK/8 = 6 MHz, cross-checked
  against host wall-clock time over 60 s.
- **GPIO input** — PD6 with the internal pull-up and then the internal
  pull-down, read through `INDR` at each.
- **GPIO output** — PD4 push-pull, read back through `INDR` at every one of 120
  edges.
- **Console** — the WCH-Link debug channel on SWIO (PD1), captured on the host
  with `minichlink -T`.
- **Startup** — `ch32fun`'s vector table, `.data` copy, `.bss` zero and
  `SystemInit` (HSI, PLL x2, flash wait state).

## Building and flashing

```
pio run                       # -> .pio/build/bringup/firmware.elf
pio run -t upload             # wlink through the WCH-LinkE
python3 ../benchmark/capture.py --output serial_log.txt --done BRINGUP_DONE
python3 ../benchmark/wallclock_check.py serial_log.timing.json
```

Needs PlatformIO. The platform and framework are pinned to exact commits in
`platformio.ini`, so a fresh checkout builds the same code.

## Verified on hardware

Read back live from the running chip, not assumed. Full capture in
[`serial_log.txt`](serial_log.txt). **6/6 checks passed.**

| Check | Reading | Meaning |
|---|---|---|
| Chip ID (probe) | `00-30-05-10` | CH32V003F4P6, 16 KB flash (also read from ESIG: 16 KB) |
| `RCC_CTLR` | `0x03084E83` | `HSION`+`HSIRDY`, `PLLON`+`PLLRDY`; `HSITRIM` 16 (default), `HSICAL` `0x4E` |
| `RCC_CFGR0` | `0x0000000A` | `SW`=`SWS`=2 → **PLL is the system clock**; `HPRE`=0 (no divide), `PLLSRC`=0 (HSI) |
| `FLASH_ACTLR` | `0x00000001` | `LATENCY` = 1 wait state, required above 24 MHz |
| `SYSTICK_CTLR` | `0x00000001` | enabled, `STCLK`=0 → HCLK/8 → 6 MHz |
| Host wall-clock | chip 60.000000 s vs host 59.8336 s | **+0.278%** over 60 s |
| PD6 pulled up / pulled down | `1` / `0` | Input path reads both levels |
| PD4 | 120 edges, 0 read-back mismatches | Output driver and input sampler agree |

### How the clock is checked

Everything the benchmark reports is a rate, and every rate is divided by the
clock. Two checks, and they are deliberately not the same kind:

1. **Register read-back** says what the hardware believes it selected: PLL is
   the system clock, HCLK is not divided, PLL source is the HSI, and the PLL
   reports locked. That proves *configuration*, not *frequency*.
2. **Host wall-clock** is independent of the chip. The firmware prints
   `@BEGIN uptime` and, 60 SysTick-seconds later, `@END uptime`;
   `capture.py` timestamps each line as it arrives and `wallclock_check.py`
   compares the two clocks. Timing SysTick against SysTick would prove nothing,
   and the 60-second loop in the firmware deliberately has no such self-check.

**The chip's clock runs about 0.3–0.45% fast.** Its 60 s is 59.83 s on the
host, and under full benchmark load the same comparison reads +0.41% to +0.45%
(see `../benchmark/results.md`). That is the 24 MHz HSI doing what an internal
RC oscillator does — its factory trim is ±1% and it moves with temperature —
so the part is running at roughly 48.1–48.2 MHz, not exactly 48.000. Rates
quoted per second are per *nominal* second of the chip's own clock, and
**cycles per iteration are unaffected**, since both sides of that ratio are
counted in the same ticks.

## Not verified

- **No LED was observed.** PD4 toggles and reads back correctly, but nothing
  here confirms that an LED is wired to it on any particular board. The 1 Hz
  square wave is there for a scope or a LED you attach.
- **Frequency was not checked against anything on the chip itself**
  (no crystal, no second oscillator in use) — only against the host clock.

## Board quirks worth knowing before you start

**`-fno-builtin` is required for the console to work.** Without it GCC rewrites
`printf("literal\n")` into `puts()`, and in this setup the next `printf` line
came out as `RCC_CTLR= 0x<NUL>000000a2 0x<NUL>00001880 …` followed by silence —
the console never recovered. The same source with only `-fno-builtin` added
printed every line correctly. The root cause was not tracked down further than
that; the flag is in `platformio.ini` and both images in `../benchmark` use it
too.

**The console is the programming pin.** SWIO (PD1) carries both the programmer
and the debug-print channel, so the probe has to stay attached and a host has to
be draining it. This firmware never touches PD1 or the AFIO remap, so the part
can always be reprogrammed. The first 3 s after reset is a deliberate delay so
the host can attach before the first line is printed.

**A console byte costs about half a millisecond.** The channel moves a few
bytes per host poll, so keep prints out of anything timed.
