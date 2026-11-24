#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "libretro.h"
#include "gb_core.h"

static retro_environment_t environment_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static int16_t audio_buffer[799 * 2];
static int loaded;
static const char *last_error;

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t cb) { environment_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB332;
    if (environment_cb)
        environment_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    loaded = 0;
    last_error = NULL;
}

void retro_deinit(void)
{
    if (loaded)
    {
        gb550d_core_shutdown();
        loaded = 0;
    }
}

void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name = "Walnut-CGB";
    info->library_version = "1.0";
    info->valid_extensions = "gb|gbc";
    info->need_fullpath = false;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = 160;
    info->geometry.base_height = 144;
    info->geometry.max_width = 160;
    info->geometry.max_height = 144;
    info->geometry.aspect_ratio = 160.0f / 144.0f;
    info->timing.fps = 4194304.0 / 70224.0;
    info->timing.sample_rate = 48000.0;
}

bool retro_load_game(const struct retro_game_info *game)
{
    struct gb550d_core_options options;
    if (!game || !game->data || game->size < 0x150)
    {
        last_error = "invalid game data";
        return false;
    }
    options.audio_enabled = 1;
    if (gb550d_core_init((const uint8_t *)game->data,
                         (unsigned int)game->size, &options) != 0)
    {
        last_error = gb550d_core_error();
        return false;
    }
    loaded = 1;
    return true;
}

void retro_unload_game(void)
{
    if (loaded)
    {
        gb550d_core_shutdown();
        loaded = 0;
    }
}

void retro_run(void)
{
    static const unsigned ids[8] = {
        RETRO_DEVICE_ID_JOYPAD_UP,
        RETRO_DEVICE_ID_JOYPAD_DOWN,
        RETRO_DEVICE_ID_JOYPAD_LEFT,
        RETRO_DEVICE_ID_JOYPAD_RIGHT,
        RETRO_DEVICE_ID_JOYPAD_A,
        RETRO_DEVICE_ID_JOYPAD_B,
        RETRO_DEVICE_ID_JOYPAD_SELECT,
        RETRO_DEVICE_ID_JOYPAD_START,
    };
    int i;
    int16_t mono[803];

    if (!loaded)
        return;

    if (input_poll_cb) input_poll_cb();
    for (i = 0; i < 8; i++)
    {
        int pressed = 0;
        if (input_state_cb)
            pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, ids[i]) ? 1 : 0;
        gb550d_core_set_button((enum gb550d_button)i, pressed);
    }

    gb550d_core_run_frame();
    if (video_cb)
        video_cb(gb550d_core_framebuffer(), 160, 144, 160);

    gb550d_core_render_audio(mono, 803);
    for (i = 0; i < 799; i++)
    {
        audio_buffer[i * 2] = mono[i];
        audio_buffer[i * 2 + 1] = mono[i];
    }
    if (audio_cb)
        audio_cb(audio_buffer, 799);
}

void retro_reset(void)
{
    /* Walnut can reset in place, but the frontend re-loads the ROM for
     * this module; keep the API entry point. */
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

const char *retro_gb_error(void)
{
    return last_error ? last_error : gb550d_core_error();
}

void retro_gb_set_frameskip(int enabled)
{
    gb550d_core_set_frameskip(enabled);
}
