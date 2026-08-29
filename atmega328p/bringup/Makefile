TARGET = blinky
MCU    = atmega328p
F_CPU  = 16000000

# Serial port of the board's USB-serial bridge. Override on the command line:
#   make flash PORT=/dev/cu.usbmodemXXXX
PORT  ?= /dev/cu.usbmodem313301
BAUD  ?= 115200

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size

SRCS = main.c
OBJS = $(SRCS:.c=.o)

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL -std=c11 -O2 -g3 -Wall -Wextra \
          -ffunction-sections -fdata-sections
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map=$(TARGET).map

all: $(TARGET).hex

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) --format=avr --mcu=$(MCU) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Optiboot speaks STK500v1 and is reset into by the DTR pulse avrdude sends.
flash: $(TARGET).hex
	avrdude -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -D -U flash:w:$(TARGET).hex:i

probe:
	avrdude -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -v

console:
	screen $(PORT) $(BAUD)

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).hex $(TARGET).map

.PHONY: all flash probe console clean
