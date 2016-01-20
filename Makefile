#
# Makefile for rBoot
# https://github.com/raburton/esp8266
#

ESPTOOL2 ?= C:\Users\Erik\Documents\Arduino\hardware\esp8266\2.0.0-rc1\tools\esptool2
SDK_BASE   ?= C:\Users\Erik\Documents\Arduino\hardware\esp8266\2.0.0-rc1\tools\sdk
XTENSA_BINDIR ?= C:\Users\Erik\Documents\Arduino\hardware\esp8266\2.0.0-rc1\tools\xtensa-lx106-elf-gcc\bin
OAKBOOT_BUILD_BASE ?= build
OAKBOOT_FW_BASE    ?= 

ifndef XTENSA_BINDIR
CC := xtensa-lx106-elf-gcc
LD := xtensa-lx106-elf-gcc
SIZE := xtensa-lx106-elf-size
else
CC := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
LD := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
SIZE := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-size)
endif


CFLAGS    = -Os -O3 -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH
LDFLAGS   = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -L $(SDK_BASE)/ld/
LD_SCRIPT = oakboot.ld

E2_OPTS = -quiet -bin -boot0

ifeq ($(OAKBOOT_BIG_FLASH),1)
	CFLAGS += -DBOOT_BIG_FLASH
endif
ifeq ($(SPI_SIZE), 256K)
	E2_OPTS += -256
else ifeq ($(SPI_SIZE), 512K)
	E2_OPTS += -512
else ifeq ($(SPI_SIZE), 1M)
	E2_OPTS += -1024
else ifeq ($(SPI_SIZE), 2M)
	E2_OPTS += -2048
else ifeq ($(SPI_SIZE), 4M)
	E2_OPTS += -4096
endif

.SECONDARY:

#all: $(OAKBOOT_BUILD_BASE) $(OAKBOOT_FW_BASE) $(OAKBOOT_FW_BASE)/oakboot.bin $(OAKBOOT_FW_BASE)/testload1.bin $(OAKBOOT_FW_BASE)/testload2.bin
#all: $(OAKBOOT_BUILD_BASE) $(OAKBOOT_FW_BASE) $(OAKBOOT_FW_BASE)/oak_bootloader.bin
all: $(OAKBOOT_BUILD_BASE) oakboot.bin

$(OAKBOOT_BUILD_BASE):
	_mkdir -p $@

#$(OAKBOOT_FW_BASE):
#	_mkdir -p $@

$(OAKBOOT_BUILD_BASE)/oakboot-stage2a.o: oakboot-stage2a.c oakboot-private.h oakboot.h
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
	
$(OAKBOOT_BUILD_BASE)/oakboot-stage2a.elf: $(OAKBOOT_BUILD_BASE)/oakboot-stage2a.o
	@echo "LD $@"
	@$(LD) -Toakboot-stage2a.ld $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@

$(OAKBOOT_BUILD_BASE)/oakboot-hex2a.h: $(OAKBOOT_BUILD_BASE)/oakboot-stage2a.elf
	@echo "FW $@"
	@$(ESPTOOL2) -quiet -header $< $@ .text

$(OAKBOOT_BUILD_BASE)/oakboot.o: oakboot.c oakboot-private.h oakboot.h $(OAKBOOT_BUILD_BASE)/oakboot-hex2a.h
	@echo "CC $<"
	@$(CC) $(CFLAGS) -I$(OAKBOOT_BUILD_BASE) -c $< -o $@

$(OAKBOOT_BUILD_BASE)/%.o: %.c %.h
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OAKBOOT_BUILD_BASE)/%.elf: $(OAKBOOT_BUILD_BASE)/%.o
	@echo "LD $@"
	@echo $(LD)
	@$(LD) -T$(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@
	@$(SIZE) $@

%.bin: $(OAKBOOT_BUILD_BASE)/%.elf
	@echo "FW $@"
	@$(ESPTOOL2) $(E2_OPTS) $< $@ .text .rodata

clean:
	@echo "RM $(OAKBOOT_BUILD_BASE)"
	#" $(OAKBOOT_FW_BASE)"
	@rm -rf $(OAKBOOT_BUILD_BASE)
	@rm oakboot.bin
