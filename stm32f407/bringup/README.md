# STM32F407 bring-up

Minimal known-good starting point for the STM32F4 Discovery: prove the chip
boots, the clock tree comes up at its rated speed, and GPIO in/out work.
The benchmark suite builds on the same foundations.

Baremetal in the strictest sense — no HAL, no CMSIS, no SDK. Register
definitions are hand-written from RM0090 in `stm32f407.h`. 620 bytes of
flash.

## What it does

The four LEDs chase clockwise (green → orange → red → blue) at ~100 ms per
step. Hold the blue USER button (PA0) and all four blink together instead.

That exercises, in order:

- **Clock tree** — HSE 8 MHz (from the ST-Link MCO) → PLL → 168 MHz, with
  flash wait states and the ART accelerator configured to match. If HSE
  fails to start it falls back to HSI and the chase runs visibly slower,
  which is the diagnostic.
- **Flash wait states** — 5 WS, required at 168 MHz.
- **SysTick** — 1 kHz tick driven from the core clock, used for timing.
- **GPIO output** — PD12–15, driven via `BSRR` (atomic, no read-modify-write).
- **GPIO input** — PA0, active high.
- **Startup** — vector table, `.data` copy, `.bss` zero, FPU enable.

## Building and flashing

```
make          # -> blinky.bin
make flash    # st-flash --reset write blinky.bin 0x08000000
make probe    # st-info --probe, check the ST-Link sees the target
make clean
```

Needs `arm-none-eabi-gcc` and `stlink` (`brew install stlink`).

## Verified on hardware

Read back live from the running chip over SWD, not assumed:

| Check | Reading | Meaning |
|---|---|---|
| `HSERDY` | 1 | External 8 MHz running |
| `PLLRDY` | 1 | PLL locked |
| `RCC_CFGR.SWS` | 2 | PLL **is** the system clock → 168 MHz |
| `FLASH_ACR` latency | 5 WS | Correct for 168 MHz @ 3.3 V |
| `SysTick LOAD` | 167999 | 168000−1 → exactly 1 kHz |
| tick delta | 0 → 300 over 3 LED steps | Timing accurate to the millisecond |
| `GPIOD_MODER` | `0x55000000` | PD12–15 all outputs |
| `PA0` | 0 | Button reads released |

The tick count is the strongest single result: 3 chase steps at 100 ms
produced exactly 300 ticks, so clock tree, wait states, SysTick and the
vector table are all provably correct.

## Note: this board has no virtual COM port

The on-board debug probe is **ST-LINK/V2** (USB PID `0x3748`), which exposes
a single vendor-specific interface for SWD and no USB CDC. There is no
`/dev/cu.usbmodem*` and there never will be — only ST-LINK/V2-1 (`0x374B`)
and V3 carry a VCP. For console output see the options in
`../benchmark/stm32f407/README.md`; this bring-up uses LEDs alone and needs
no console.

## Gotcha worth knowing: `WFI` and the debugger

`delay_ms()` uses `WFI`. On STM32F4 the debug interface stops responding
during sleep unless `DBGMCU_CR.DBG_SLEEP` is set — without it, `st-info
--probe` reports `chipid 0x000` and the board looks bricked after the first
flash. This code sets the sleep/stop/standby bits in `main()` before the
first `WFI`.

If you ever hit that state (with this or any other firmware), recover with:

```
st-flash --connect-under-reset --reset write <file>.bin 0x08000000
```

## Files

- `main.c` — clock init, GPIO, the chase/blink loop
- `startup.c` — vector table, `.data`/`.bss` init, FPU enable
- `stm32f407.h` — hand-written register definitions (no CMSIS)
- `stm32f407.ld` — linker script, including heap/stack reservation so
  newlib's `malloc`/`printf` link cleanly if you extend this
