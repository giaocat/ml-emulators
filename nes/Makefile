MODULE_NAME = nes

QNES_DIR = qnes
QNES_SRC = $(wildcard $(QNES_DIR)/*.cpp)
QNES_OBJS = $(patsubst $(QNES_DIR)/%.cpp,$(BUILD_DIR)/qnes/%.o,$(QNES_SRC))

MODULE_OBJS = \
    $(BUILD_DIR)/nes6d.o \
    $(BUILD_DIR)/nes_core_qnes.o \
    $(BUILD_DIR)/nes_audio_ml.o \
    $(BUILD_DIR)/nes_rom.o \
    $(BUILD_DIR)/nes_config.o \
    $(QNES_OBJS)

TOP_DIR = ../..
include $(TOP_DIR)/modules/Makefile

CXX = $(CROSS_COMPILE)g++
QNES_LIBM := $(shell $(CC) -print-file-name=libm.a)
QNES_LIBGCC := $(shell $(CC) -print-libgcc-file-name)
CXXFLAGS_QNES = -std=gnu++11 -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-use-cxa-atexit \
    -DBLARGG_NEW=new \
    -Wno-error -Wno-unused-variable -Wno-sign-compare \
    -Wno-write-strings -Wno-narrowing -Wno-multichar \
    -Wno-unused-but-set-variable

CFLAGS += -I. -I$(QNES_DIR) -Wno-error

$(BUILD_DIR)/qnes:
	@mkdir -p $@

$(BUILD_DIR)/qnes/%.o: $(QNES_DIR)/%.cpp | $(BUILD_DIR)/qnes
	$(CXX) $(CXXFLAGS) $(CFLAGS) $(CXXFLAGS_QNES) -c -o $@ $<

$(BUILD_DIR)/qnes/%.o: CFLAGS += -O3

$(BUILD_DIR)/nes_core_qnes.o: nes_core_qnes.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CFLAGS) $(CXXFLAGS_QNES) -c -o $@ $<

# The interpreter and tile renderer are the camera's measured hot path.
$(BUILD_DIR)/nes6d.o: CFLAGS += -O3

# QuickNES' Blip_Buffer equalizer needs libm; link it into the relocatable
# module (the camera .sym does not export cos/exp/pow).
$(BUILD_DIR)/$(MODULE_NAME).mo: $(MODULE_OBJS)
	$(LD) -r -o $@.tmp $(MODULE_OBJS) $(QNES_LIBM) $(QNES_LIBGCC)
	$(STRIP) --strip-debug --strip-unneeded --discard-all --discard-locals $@.tmp
	$(MV) $@.tmp $@

.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

