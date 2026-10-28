#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. Install devkitARM with the devkitPro NDS Development package.")
endif

include $(DEVKITARM)/ds_rules

TARGET := smw-nds
BUILD := build
OUTPUT := $(BUILD)/$(TARGET)
GAME_TITLE := Super Mario World DS
GAME_SUBTITLE1 := NDS hardware bring-up
GAME_SUBTITLE2 := Not playable yet

ARCH := -march=armv5te -mtune=arm946e-s -mthumb
CFLAGS := -g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += -Iinclude -I$(LIBNDS)/include -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lfat -lnds9
LIBDIRS = $(LIBNDS)
LIBPATHS = -L$(LIBNDS)/lib
LD := $(CC)

SOURCES := $(wildcard source/*.c)
OFILES := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES))
DEPS := $(OFILES:.o=.d)

.PHONY: all clean
all: $(OUTPUT).nds

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(_EXTRADEFS) -MMD -MP -MF "$(BUILD)/$*.d" -c "$<" -o "$@"

$(OUTPUT).elf: $(OFILES)

$(OUTPUT).nds: $(OUTPUT).elf

clean:
	@echo clean ...
	@rm -rf $(BUILD)

-include $(DEPS)

