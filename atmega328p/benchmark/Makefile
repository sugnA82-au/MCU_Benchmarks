TARGET = atmega328p_bench
MCU    = atmega328p
F_CPU  = 16000000

PORT  ?= /dev/cu.usbmodem313301
BAUD  ?= 115200

SRCS = main.c platform.c \
       coremark/core_list_join.c coremark/core_main.c coremark/core_matrix.c \
       coremark/core_state.c coremark/core_util.c coremark/core_portme.c

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size

# ITERATIONS=0 lets CoreMark self-calibrate to a ~10s run regardless of the
# target's speed, per the official CoreMark reporting rules.
#
# TOTAL_DATA_SIZE=1200 with PROFILE_RUN seeds is NOT the conformant 2000-byte
# performance run. It is used here because the conformant run needs a
# 2000-byte working set and this chip has 2048 bytes of SRAM in total. 1200 is
# a size CoreMark still holds known CRCs for, so the result is validated even
# though it is not reportable. The firmware says so in its own output.
DEFS = -DITERATIONS=0 -DPROFILE_RUN=1 -DTOTAL_DATA_SIZE=1200 \
       -DFLAGS_STR="\"-O2 avr5 TOTAL_DATA_SIZE=1200\""

CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL -std=gnu11 -O2 -g3 -Wall -Wextra \
         -ffunction-sections -fdata-sections -Icoremark -I. $(DEFS)

# -u vfprintf + -lprintf_flt: avr-libc's default printf has no float support,
# and the failure mode is a silent "?" in place of every %f. Note this only
# ever overrides vfprintf, never vfprintf_P - which is why nothing here calls
# printf_P. See platform.c.
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
          -Wl,-u,vfprintf -lprintf_flt -lm

OBJS = $(SRCS:.c=.o)

all: $(TARGET).hex

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) --format=avr --mcu=$(MCU) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

flash: $(TARGET).hex
	avrdude -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -D -U flash:w:$(TARGET).hex:i

probe:
	avrdude -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -v

console:
	screen $(PORT) $(BAUD)

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).hex $(TARGET).map

.PHONY: all flash probe console clean
