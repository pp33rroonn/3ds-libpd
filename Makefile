#---------------------------------------------------------------------------------
# 3DS libpd Homebrew – Makefile
# Toolchain : devkitARM + libctru
# Audio     : NDSP (DSP firmware, dual-buffer)
# Sync      : Right-channel 2ppqn pulse generator (sync_clock module)
#---------------------------------------------------------------------------------

#— Sanity check ---------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
  $(error "DEVKITPRO is not set. Source /etc/profile.d/devkit-env.sh or set it manually.")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "DEVKITARM is not set. Is devkitARM installed under DEVKITPRO?")
endif

#— Toolchain paths -------------------------------------------------------------
DEVKITARM  := $(DEVKITPRO)/devkitARM
LIBCTRU    := $(DEVKITPRO)/libctru
PREFIX     := $(DEVKITARM)/bin/arm-none-eabi-

CC         := $(PREFIX)gcc
CXX        := $(PREFIX)g++
AR         := $(PREFIX)ar
OBJCOPY    := $(PREFIX)objcopy
STRIP      := $(PREFIX)strip

#— Project identity -----------------------------------------------------------
TARGET     := libpd_3ds
BUILD      := build
SOURCES    := source \
	source/audio \
	source/input \
	source/sync \
	source/pd_bridge \
	source/fs
INCLUDES   := source \
	source/audio \
	source/input \
	source/sync \
	source/pd_bridge \
	source/fs \
	lib/libpd/include \
	lib/libpd/include/pd \
	$(LIBCTRU)/include
DATA       := resources

# romfs: Ordner der als romfs:/ ins 3DSX/CIA eingebettet wird
# Patches und Assets werden hier abgelegt (romfs/patches/main.pd etc.)
ROMFS_DIR  := romfs
OUTPUT_DIR := output

#— Static libraries to link ---------------------------------------------------
# lib/libpd/libpd.a  : pre-built with arm-none-eabi-gcc (see docs/build_libpd.md)
LIBPD      := $(CURDIR)/lib/libpd/libpd.a
LIBDIRS    := $(LIBCTRU)/lib
LIBS       := -lctru -lm

#— 3DS ARM11 CPU flags --------------------------------------------------------
ARCH       := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

#— Compiler flags -------------------------------------------------------------
# -DNDSP_BUFCOUNT / _BUFSIZE defined here so audio.c and sync.c stay in sync
NDSP_BUFCOUNT  := 2
NDSP_BUFSIZE   := 1024          # samples per channel per buffer (power of 2)
SAMPLERATE     := 44100
PD_BLOCK_TICKS := 8             # libpd ticks per NDSP callback (ticks × 64 smp)

CFLAGS := $(ARCH) \
	-mword-relocations \
	-ffunction-sections \
	-fdata-sections \
	-fno-strict-aliasing \
	-Wall -Wextra \
	-O2 \
	-D__3DS__ \
	-DNDSP_BUFCOUNT=$(NDSP_BUFCOUNT) \
	-DNDSP_BUFSIZE=$(NDSP_BUFSIZE) \
	-DSAMPLERATE=$(SAMPLERATE) \
	-DPD_BLOCK_TICKS=$(PD_BLOCK_TICKS) \
	-DSYNC_ENABLE \
	-DSYNC_PPQN=2

# Pure Data / libpd internals expect these:
CFLAGS += -DPD -DUSEAPI_DUMMY -DHAVE_UNISTD_H

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions

LDFLAGS  := $(ARCH) \
	-specs=$(DEVKITARM)/arm-none-eabi/lib/3dsx.specs \
	-Wl,--gc-sections \
	-Wl,--strip-all \
	-L$(CURDIR)/lib/libpd \
	$(foreach dir,$(LIBDIRS),-L$(dir))

#— Banner / SMDH metadata -----------------------------------------------------
APP_TITLE   := libpd 3DS
APP_DESC    := Pure Data Vanilla on 3DS
APP_AUTHOR  := YourName
ICON        := resources/icon.png          # 48×48 PNG (create or remove line)

#— Source discovery -----------------------------------------------------------
CFILES      := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
CXXFILES    := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
OFILES      := $(addprefix $(BUILD)/, \
	$(CFILES:.c=.o) \
	$(CXXFILES:.cpp=.o))

INCLUDE_DIRS := $(foreach dir,$(INCLUDES),-I$(dir))

#— Output targets -------------------------------------------------------------
ELF         := $(OUTPUT_DIR)/$(TARGET).elf
STRIPPED    := $(OUTPUT_DIR)/$(TARGET)_strip.elf
_3DSX       := $(OUTPUT_DIR)/$(TARGET).3dsx
CIA_TEMP    := $(OUTPUT_DIR)/$(TARGET).cia    # requires makerom (optional)

#— Default target -------------------------------------------------------------
.PHONY: all clean 3dsx cia strip

all: dirs $(_3DSX)

dirs:
	@mkdir -p $(BUILD) $(OUTPUT_DIR) $(ROMFS_DIR)/patches
	@$(foreach dir,$(SOURCES), mkdir -p $(BUILD)/$(dir);)

#— Linking --------------------------------------------------------------------
$(ELF): $(OFILES) $(LIBPD)
	@echo "  LD   $@"
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPD) $(foreach l,$(LIBS),$(l)) -o $@

#— Strip (optional hardened output) ------------------------------------------
strip: $(ELF)
	@echo "  STRIP $<"
	$(STRIP) --strip-unneeded -o $(STRIPPED) $(ELF)

#— 3DSX homebrew package ------------------------------------------------------
$(_3DSX): $(ELF) $(OUTPUT_DIR)/$(TARGET).smdh
	@echo "  3DSX $@"
	@3dsxtool $< $@ --smdh=$(OUTPUT_DIR)/$(TARGET).smdh --romfs=$(ROMFS_DIR)

$(OUTPUT_DIR)/$(TARGET).smdh: $(ICON)
	@bannertool makesmdh \
	-s "$(APP_TITLE)" \
	-l "$(APP_DESC)" \
	-p "$(APP_AUTHOR)" \
	-i $(ICON) \
	-o $@

3dsx: $(_3DSX)

#— CIA (optional, needs makerom) ----------------------------------------------
# makerom: https://github.com/3DSGuy/Project_CTR
# Installieren: im PATH verfuegbar machen.
# Banner (banner.bnr) und Icon (icon.icn) koennen mit bannertool erzeugt werden.
# Minimales CIA ohne Banner funktioniert fuer FBI-Installation auf CFW.
cia: $(_3DSX) $(OUTPUT_DIR)/$(TARGET).smdh
	@echo "  CIA  $(CIA_TEMP)"
	@bannertool makebanner \
	-i $(ICON) \
	-a resources/banner.wav \
	-o $(OUTPUT_DIR)/banner.bnr 2>/dev/null || \
	bannertool makebanner -i $(ICON) -o $(OUTPUT_DIR)/banner.bnr
	@makerom -f cia \
	-o $(CIA_TEMP) \
	-rsf resources/app.rsf \
	-target t \
	-exefslogo \
	-elf $(ELF) \
	-icon $(OUTPUT_DIR)/$(TARGET).smdh \
	-banner $(OUTPUT_DIR)/banner.bnr \
	-romfs $(ROMFS_DIR)
	@echo "  CIA fertig: $(CIA_TEMP)"
	@echo "  -> Mit FBI auf dem 3DS installieren"

#— Compile rules --------------------------------------------------------------
$(BUILD)/%.o: %.c
	@echo "  CC   $<"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp
	@echo "  CXX  $<"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -MMD -MP -c $< -o $@

#— Dependency tracking --------------------------------------------------------
-include $(OFILES:.o=.d)

#— Clean ----------------------------------------------------------------------
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD) $(OUTPUT_DIR)
