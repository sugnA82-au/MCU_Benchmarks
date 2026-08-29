/*
ATmega328P / baremetal port of CoreMark's core_portme.h, adapted from the
upstream "barebones" template (Apache License, Version 2.0, see LICENSE.md).

Three things here are AVR-specific and none of them are optional:

  1. int is 16 bits on this target, so ee_s32/ee_u32 must be long, and
     pointers are 16 bits, so ee_ptr_int must be short. Getting either wrong
     compiles cleanly and produces wrong CRCs.

  2. ee_printf is a macro that wraps the format string in PSTR(). String
     literals live in SRAM on this part unless explicitly placed in flash, and
     CoreMark's format strings come to roughly a kilobyte -- half the total
     SRAM on the chip. Wrapping at the macro leaves every CoreMark .c file
     untouched.

  3. MEM_MALLOC with a portable_malloc() that hands back a fixed buffer owned
     by main.c. The chip has 2 KB of SRAM; the benchmark's memory-test region
     and CoreMark's working set are the same buffer, used one at a time.
*/
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>
#include <avr/pgmspace.h>

#ifndef HAS_FLOAT
#define HAS_FLOAT 1
#endif
#ifndef HAS_TIME_H
#define HAS_TIME_H 0
#endif
#ifndef USE_CLOCK
#define USE_CLOCK 0
#endif
#ifndef HAS_STDIO
#define HAS_STDIO 1
#endif
/* 0 so coremark.h does not alias ee_printf to printf: see the macro below. */
#ifndef HAS_PRINTF
#define HAS_PRINTF 0
#endif

#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "unknown"
#endif
#endif
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS FLAGS_STR
#endif
#ifndef MEM_LOCATION
#define MEM_LOCATION "STATIC"
#endif

typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed long    ee_s32;   /* int is 16-bit on AVR */
typedef double         ee_f32;   /* 32-bit on this toolchain: see README */
typedef unsigned char  ee_u8;
typedef unsigned long  ee_u32;   /* int is 16-bit on AVR */
typedef ee_u16         ee_ptr_int;  /* pointers are 16-bit on AVR */
typedef size_t         ee_size_t;
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;

#ifndef SEED_METHOD
#define SEED_METHOD SEED_VOLATILE
#endif

#ifndef MEM_METHOD
#define MEM_METHOD MEM_MALLOC
#endif

#ifndef MULTITHREAD
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0
#endif

#ifndef MAIN_HAS_NOARGC
#define MAIN_HAS_NOARGC 1
#endif

#ifndef MAIN_HAS_NORETURN
#define MAIN_HAS_NORETURN 1
#endif

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) \
    && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

/* Keeps CoreMark's ~1 KB of format strings out of a 2 KB SRAM. pprintf()
 * stages the flash string into a RAM buffer and calls the float-capable
 * vfprintf; see platform.c for why printf_P cannot be used here. */
int pprintf(const char *fmt_P, ...);
#define ee_printf(fmt, ...) pprintf(PSTR(fmt), ##__VA_ARGS__)

/* Renamed so this file's coremark_run() can be called from our own main()
 * rather than colliding with the C runtime entry point. */
#define main coremark_run

#endif /* CORE_PORTME_H */
