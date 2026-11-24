#ifndef NES6D_CORE_H
#define NES6D_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nes6d_button
{
    NES6D_BUTTON_UP,
    NES6D_BUTTON_DOWN,
    NES6D_BUTTON_LEFT,
    NES6D_BUTTON_RIGHT,
    NES6D_BUTTON_A,
    NES6D_BUTTON_B,
    NES6D_BUTTON_SELECT,
    NES6D_BUTTON_START,
    NES6D_BUTTON_COUNT
};

struct nes6d_core_options
{
    int video_mode;
    int audio_enabled;
};

int nes6d_core_init(const uint8_t *rom_data, unsigned int rom_size,
                      const struct nes6d_core_options *options);
void nes6d_core_set_button(enum nes6d_button button, int pressed);
void nes6d_core_run_frame(void);
void nes6d_core_run_cpu_frame(void);
void nes6d_core_render_frame(void);
const uint8_t *nes6d_core_framebuffer(void);
void nes6d_core_render_audio(int16_t *samples, unsigned int count);
const char *nes6d_core_error(void);
unsigned int nes6d_core_controller_reads(void);
unsigned int nes6d_core_controller_latches(void);
unsigned int nes6d_core_last_latched_buttons(void);
unsigned int nes6d_core_observed_buttons(void);
unsigned int nes6d_core_state_checksum(void);
unsigned int nes6d_core_debug_ram(unsigned int address);
unsigned int nes6d_core_debug_pc(void);
unsigned int nes6d_core_debug_start_checks(void);
unsigned int nes6d_core_debug_return_address(void);
unsigned int nes6d_core_debug_outer_return_address(void);
unsigned int nes6d_core_debug_ppu_ctrl(void);
unsigned int nes6d_core_debug_ppu_mask(void);
unsigned int nes6d_core_debug_ppu_dirty(void);
unsigned int nes6d_core_debug_ppu_scroll(void);
unsigned int nes6d_core_debug_frame_scroll(void);
unsigned int nes6d_core_debug_mmc3_split(void);
unsigned int nes6d_core_debug_dmc_control(void);
unsigned int nes6d_core_debug_dmc_address(void);
unsigned int nes6d_core_debug_dmc_length(void);
unsigned int nes6d_core_debug_dmc_remaining(void);
unsigned int nes6d_core_debug_dmc_output(void);
void nes6d_core_reset(void);
void nes6d_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

