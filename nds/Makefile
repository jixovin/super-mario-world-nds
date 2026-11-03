#---------------------------------------------------------------------------------
# Super Mario World DS - ARM9 build (based on the official devkitPro NDS template)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. Install the devkitPro NDS Development package.")
endif

# Name and icon shown in DS menus (TWiLight Menu++, the DS menu).
# icon/icon.bmp is made by icon/make_icon.py.
GAME_TITLE     := Super Mario World DS
GAME_SUBTITLE1 := Native port for Nintendo DS
GAME_SUBTITLE2 := jixovin, snesrev & Claude
GAME_ICON      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))icon/icon.bmp)

include $(DEVKITARM)/ds_rules

TARGET   := smw-nds
BUILD    := build
# Game code shared with the desktop test harness lives outside nds/.
SOURCES  := source ../port ../src ../src/snes
INCLUDES := include ../port ../src ..
# data/smw_assets.bin is made from your own ROM by build.bat. It is never
# committed; without it the game reads smw_assets.dat from the SD card.
DATA     := data

# Defined once at the top level, then inherited by the build-directory pass.
ifneq ($(BUILD),$(notdir $(CURDIR)))
ifneq ($(wildcard $(CURDIR)/data/smw_assets.bin),)
export ASSET_DEFS := -DHAVE_EMBEDDED_ASSETS
endif
endif

ARCH     := -marm -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing \
            -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
            -Wno-parentheses -Wno-sign-compare -Wno-maybe-uninitialized \
            $(ARCH) $(INCLUDE) -DARM9 -DSMW_NO_SDL $(ASSET_DEFS) $(EXTRA_DEFS)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS     := -lfilesystem -lfat -lnds9 -lm
LIBDIRS  := $(LIBNDS) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

ifeq ($(strip $(CPPFILES)),)
  export LD := $(CC)
else
  export LD := $(CXX)
endif

export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
