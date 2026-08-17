TARGET   = stm32f407_bench
LDSCRIPT = stm32f407.ld

SRCS = main.c platform.c startup.c \
       coremark/core_list_join.c coremark/core_main.c coremark/core_matrix.c \
       coremark/core_state.c coremark/core_util.c coremark/core_portme.c

CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

CPU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# ITERATIONS=0 lets CoreMark self-calibrate to a ~10s run regardless of the
# target's speed, per the official CoreMark reporting rules.
DEFS = -DITERATIONS=0 -DPERFORMANCE_RUN=1 -DFLAGS_STR="\"-O2 cortex-m4f\""

# -Wno-override-init: the vector table deliberately fills every slot with
# Default_Handler via a range initialiser, then overrides the few it names.
CFLAGS = $(CPU) -std=c11 -O2 -g3 -Wall -Wextra -Wno-override-init -ffreestanding \
         -ffunction-sections -fdata-sections -Icoremark -I. $(DEFS)

# -u _printf_float: newlib-nano drops float formatting from printf unless the
# symbol is forced in, and every result line here uses %f.
LDFLAGS = $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
          -u _printf_float -Wl,--gc-sections -Wl,-Map=$(TARGET).map -lm

OBJS = $(SRCS:.c=.o)

all: $(TARGET).bin

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

flash: $(TARGET).bin
	st-flash --connect-under-reset --reset write $(TARGET).bin 0x08000000

# Capture the ITM/SWO console. Run this in one shell, then `make flash`
# (or reset the board) in another.
trace:
	st-trace --clock=168m --trace=2m

probe:
	st-info --probe

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all flash trace probe clean
