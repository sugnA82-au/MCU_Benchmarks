#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))

#define FLASH_NUM_SECTORS 12

typedef struct { uint32_t base; uint32_t size; } flash_sector_t;
extern const flash_sector_t g_flash_sectors[FLASH_NUM_SECTORS];

extern uint32_t g_core_hz;

int      clock_init_168mhz(void);
void     timer_init(void);
uint64_t time_us_64(void);
void     delay_ms(uint32_t ms);
void     dwt_init(void);
void     itm_init(uint32_t swo_hz);
void     console_init(void);
int      console_is_semihosted(void);
void     board_leds_init(void);

uint32_t flash_erase_sector(uint32_t sector);
uint32_t flash_program_words(uint32_t addr, const uint32_t *src, uint32_t nwords);
void     flash_unlock(void);
void     flash_lock(void);

static inline void irq_disable(void) { __asm__ volatile ("cpsid i" ::: "memory"); }
static inline void irq_enable(void)  { __asm__ volatile ("cpsie i" ::: "memory"); }

#endif /* PLATFORM_H */
