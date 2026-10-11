#include <stdarg.h>
#include <stdio.h>
#include "coremark.h"
#include "ch32fun.h"

#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 8, seed2_volatile = 8, seed3_volatile = 8;
#else
volatile ee_s32 seed1_volatile = 0, seed2_volatile = 0, seed3_volatile = 0x66;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;
unsigned long coremark_total_ticks;
unsigned long coremark_iterations;
unsigned long coremark_validated;
static uint32_t start_ticks, stop_ticks;

/* SysTick runs at HCLK/8 = 6 MHz, free-running and 32-bit. */
void start_time(void) { start_ticks = SysTick->CNT; }
void stop_time(void) { stop_ticks = SysTick->CNT; }
CORE_TICKS get_time(void) { return stop_ticks - start_ticks; }
secs_ret time_in_secs(CORE_TICKS ticks) { return ticks / 6000000u; }

static int has_prefix(const char *s, const char *prefix)
{
    while (*prefix) if (*s++ != *prefix++) return 0;
    return 1;
}

int coremark_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (has_prefix(fmt, "Total ticks      :")) {
        va_list c; va_copy(c, ap);
        coremark_total_ticks = va_arg(c, unsigned long);
        va_end(c);
    } else if (has_prefix(fmt, "Correct operation validated")) {
        coremark_validated = 1;
    } else if (has_prefix(fmt, "Iterations       :")) {
        va_list c; va_copy(c, ap);
        coremark_iterations = va_arg(c, unsigned long);
        va_end(c);
    }
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; (void)argv;
    _Static_assert(sizeof(ee_ptr_int) == sizeof(void *), "pointer size");
    _Static_assert(sizeof(ee_u32) == 4, "integer size");
    p->portable_id = 1;
}
void portable_fini(core_portable *p) { p->portable_id = 0; }
