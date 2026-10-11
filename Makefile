TARGET  := CatchStars
TITLE   := Snake Cubos
DESC    := Snake de cubos con modos
AUTHOR  := Me

ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO, e.g. export DEVKITPRO=/opt/devkitpro)
endif

DEVKITARM ?= $(DEVKITPRO)/devkitARM
CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc
CTRULIB   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds
TOOLS     := $(DEVKITPRO)/tools/bin

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections \
           $(ARCH) -D__3DS__ -std=gnu11 -I$(CTRULIB)/include -I$(PORTLIBS)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections \
           -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))

.PHONY: all cia clean

all: $(TARGET).3dsx

cia: $(TARGET).cia

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).3dsx: $(TARGET).elf
	$(TOOLS)/smdhtool --create "$(TITLE)" "$(DESC)" "$(AUTHOR)" icon.png build/$(TARGET).smdh
	$(TOOLS)/3dsxtool $< $@ --smdh=build/$(TARGET).smdh

build/icon.icn: icon.png
	@mkdir -p build
	bannertool makesmdh -s "$(TITLE)" -l "$(DESC)" -p "$(AUTHOR)" -i icon.png -o $@

build/banner.bnr: banner.png audio.wav
	@mkdir -p build
	bannertool makebanner -i banner.png -a audio.wav -o $@

$(TARGET).cia: $(TARGET).elf cia.rsf build/icon.icn build/banner.bnr
	makerom -f cia -o $@ -elf $(TARGET).elf -rsf cia.rsf \
	  -icon build/icon.icn -banner build/banner.bnr -exefslogo -target t

clean:
	rm -rf build $(TARGET).elf $(TARGET).3dsx $(TARGET).cia
