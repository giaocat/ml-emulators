#ifndef GB550D_CORE_H
#define GB550D_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gb550d_button
{
    GB550D_BUTTON_UP,
    GB550D_BUTTON_DOWN,
    GB550D_BUTTON_LEFT,
    GB550D_BUTTON_RIGHT,
    GB550D_BUTTON_A,
    GB550D_BUTTON_B,
    GB550D_BUTTON_SELECT,
    GB550D_BUTTON_START,
    GB550D_BUTTON_COUNT
};

struct gb550d_core_options
{
    int audio_enabled;
};

int gb550d_core_init(const uint8_t *rom_data, unsigned int rom_size,
                     const struct gb550d_core_options *options);
void gb550d_core_set_button(enum gb550d_button button, int pressed);
void gb550d_core_set_frameskip(int enabled);
void gb550d_core_run_frame(void);
const uint8_t *gb550d_core_framebuffer(void);
void gb550d_core_render_audio(int16_t *samples, unsigned int count);
const char *gb550d_core_error(void);
unsigned int gb550d_core_frame_count(void);
int gb550d_core_is_cgb(void);
unsigned int gb550d_core_audio_underruns(void);
unsigned int gb550d_core_audio_overflows(void);
void gb550d_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
