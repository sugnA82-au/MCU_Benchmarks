# MCU Benchmarks

*A running log of how fast, and how correctly my various microcontrollers actually perform, benchmarked on real hardware.*

Baremetal bring-up and benchmark firmware for every MCU that ends up on my desk. No RTOS, or vendor HAL bloat where it can be avoided. *Juuuust* enough code to prove a chip boots and runs correctly, then push it to see what it can actually do.

## CoreMark leaderboard

CoreMark is the one benchmark here that's directly comparable across vendors, so it's tracked centrally. Everything else like my own memory bandwidth, flash throughput, integer/float microbenchmarks will end up being MCU dependent, not really apples to apples, so lives in each MCU's own folder; CoreMark/MHz is what's worth comparing at a glance as its the calibrated baseline.

| MCU | Cores | Clock | CoreMark | CoreMark/MHz | Folder |
|---|---|---|---|---|---|
| RP2040 | 2x Cortex-M0+ | 125 MHz | 226.5 | 1.81 | [`rp2040/`](rp2040/) |
| STM32F407 | 1x Cortex-M4F | 168 MHz | 378.2 | 2.25 | [`stm32f407/`](stm32f407/) |

New row each time a new chip gets benchmarked with CoreMark.

## Layout

Each MCU gets its own folder with whatever bring-up and benchmark code that chip needed:

```
<mcu>/
├── bringup/      minimal known-good starting point, classic flashy LED
├── benchmark/    the CPU / memory / flash / CoreMark test suite
└── README.md     full results and chip-specific notes
```

Not every folder will look identical, bring-up varies by chip, and the benchmark suite gets adapted to whatever a given MCU is actually packing.

## Methodology

A few things kept consistent across chips so the numbers mean something next to each other:

- Baremetal, no RTOS — whatever the vendor's minimal SDK provides, and nothing more.
- CoreMark uses the standard 2000-byte "performance run" dataset and self-calibrates its iteration count to a measured run of at least 10 seconds, per the official reporting rules.
- Compiler and flags are recorded in each MCU's folder, since CoreMark scores are somewhat compiler-sensitive, treat cross-compiler comparisons with the appropriate grain of salt.

## License

This repo's own firmware is free to reuse, see individual folders for specifics. Vendored third-party benchmark source (e.g. EEMBC CoreMark) keeps its own upstream license, included alongside it.
