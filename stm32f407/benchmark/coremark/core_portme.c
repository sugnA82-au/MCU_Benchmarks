/*
STM32F407 / baremetal port of CoreMark's core_portme.c, adapted from the
upstream "barebones" template (Apache License, Version 2.0, see LICENSE.md).
Timing uses TIM2 as a free-running 1 MHz counter (see platform.c), which is
independent of the core clock and keeps counting regardless of what the CPU
is doing.
*/
#include "coremark.h"
#include "core_portme.h"
#include "../platform.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

static CORETIMETYPE start_time_val, stop_time_val;

void
start_time(void)
{
    start_time_val = (CORETIMETYPE)time_us_64();
}

void
stop_time(void)
{
    stop_time_val = (CORETIMETYPE)time_us_64();
}

CORE_TICKS
get_time(void)
{
    /* Unsigned subtraction is correct modular arithmetic even across a
     * single 32-bit wraparound of the microsecond counter. */
    return (CORE_TICKS)(stop_time_val - start_time_val);
}

secs_ret
time_in_secs(CORE_TICKS ticks)
{
    return ((secs_ret)ticks) / (secs_ret)1000000;
}

ee_u32 default_num_contexts = 1;

/* -- added for this port: see core_main.c -- */
double coremark_score = 0.0;

void
portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    p->portable_id = 1;
}

void
portable_fini(core_portable *p)
{
    p->portable_id = 0;
}
