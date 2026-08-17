TARGET  = blinky
SRCS    = main.c startup.c
LDSCRIPT = stm32f407.ld

CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  = $(CPU) -std=c11 -Og -g3 -Wall -Wextra -ffreestanding \
          -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -T$(LDSCRIPT) -nostdlib -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map

OBJS = $(SRCS:.c=.o)

all: $(TARGET).bin

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

flash: $(TARGET).bin
	st-flash --reset write $(TARGET).bin 0x08000000

erase:
	st-flash erase

probe:
	st-info --probe

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all flash erase probe clean
