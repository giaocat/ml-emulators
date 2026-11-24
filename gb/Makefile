MODULE_NAME = gb

MODULE_OBJS = \
    $(BUILD_DIR)/gb550d.o \
    $(BUILD_DIR)/gb_core.o \
    $(BUILD_DIR)/retro_gb.o \
    $(BUILD_DIR)/gb_audio_ml.o \
    $(BUILD_DIR)/gb_rom.o \
    $(BUILD_DIR)/gb_config.o \
    $(BUILD_DIR)/walnut/minigb_apu.o

TOP_DIR = ../..
include $(TOP_DIR)/modules/Makefile

CFLAGS += -I. -Iwalnut -Wno-error
CFLAGS += -DAUDIO_SAMPLE_RATE=48000 -DMINIGB_APU_AUDIO_FORMAT_S16SYS
CFLAGS += -DWALNUT_GB_HIGH_LCD_ACCURACY=0

GB_OPT ?= -O3

ifdef GB_6D
CFLAGS += -DGB_6D
endif

$(BUILD_DIR)/walnut:
	@mkdir -p $@

$(BUILD_DIR)/walnut/minigb_apu.o: walnut/minigb_apu.c | $(BUILD_DIR)/walnut
	$(CC) $(CFLAGS) $(GB_OPT) -c -o $@ $<

# Walnut-GB is a single-header core; it is compiled inside gb_core.c.
$(BUILD_DIR)/gb_core.o: CFLAGS += $(GB_OPT)
$(BUILD_DIR)/retro_gb.o: CFLAGS += $(GB_OPT) -mno-long-calls
$(BUILD_DIR)/gb550d.o: CFLAGS += $(GB_OPT) -mno-long-calls

.DEFAULT_GOAL := $(BUILD_DIR)/module_complete
