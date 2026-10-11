/* CH32V003 port; upstream kernels remain unmodified. */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H
#include <stddef.h>
#define HAS_FLOAT 0
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 1
/* ee_printf goes through coremark_printf (core_portme.c), which forwards to the
   board printf and records the final tick and iteration counts so the harness
   can report an exact score instead of CoreMark's whole-second integer one. */
#define HAS_PRINTF 0
int coremark_printf(const char *fmt, ...);
#define ee_printf coremark_printf
#define COMPILER_VERSION "WCH GCC " __VERSION__
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS "-O2 -fno-builtin -fno-lto -msave-restore -march=rv32ecxw -mabi=ilp32e"
#endif
#define MEM_LOCATION "SRAM (static dataset, stack in same 2048 bytes)"
typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef unsigned int ee_u32;
typedef unsigned char ee_u8;
typedef double ee_f32;
typedef unsigned int ee_ptr_int;
typedef size_t ee_size_t;
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))
#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;
#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0
extern ee_u32 default_num_contexts;
/* Filled in by coremark_printf from the final report. */
extern unsigned long coremark_total_ticks;
extern unsigned long coremark_iterations;
extern unsigned long coremark_validated;
typedef struct { ee_u8 portable_id; } core_portable;
void portable_init(core_portable *, int *, char *[]);
void portable_fini(core_portable *);
#define main coremark_run
#endif
