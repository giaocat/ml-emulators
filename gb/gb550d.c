#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <config.h>
#include <fio-ml.h>
#include <menu.h>
#include <timer.h>

#include "gb_core.h"
#include "gb_audio_ml.h"
#include "gb_rom.h"
#include "libretro.h"
#define GB550D_CONFIG_DEFINE
#include "gb_config.h"

#define GB550D_SCREEN_W 720
#define GB550D_SCREEN_H 480
#define GB550D_VIEW_W (160 * 3)
#define GB550D_VIEW_H (144 * 3)
#define GB550D_VIEW_X ((GB550D_SCREEN_W - GB550D_VIEW_W) / 2)
#define GB550D_VIEW_Y ((GB550D_SCREEN_H - GB550D_VIEW_H) / 2)
#define GB550D_QUEUE_SIZE 64
#define GB550D_LOG_PATH "ML/LOGS/GB550D.LOG"

extern int menu_redraw_blocked;
const char *retro_gb_error(void);
void gb550d_core_set_frameskip(int enabled);
void retro_host_audio_read(int16_t *samples, unsigned int count);

struct gb550d_bmp_vram_info
{
    uint8_t *vram0;
    uint32_t off_0x04;
    uint8_t *vram2;
};
extern struct gb550d_bmp_vram_info bmp_vram_info[];
static uint8_t *gb_bmp_vram0_original;
static uint8_t *gb_bmp_vram2_original;

#if defined(GB_6D) || defined(CONFIG_6D)
#define GB_TARGET_6D 1
#endif

#ifndef ASIF_MAX_VOL
#if defined(GB_TARGET_6D)
#define ASIF_MAX_VOL 10
#else
#define ASIF_MAX_VOL 5
#endif
#endif

typedef struct
{
    unsigned char button;
    unsigned char pressed;
} gb_input_event;

static volatile int gb_module_alive;
static volatile int gb_running;
static volatile int gb_task_active;
static volatile unsigned int gb_queue_write;
static volatile unsigned int gb_queue_read;
static gb_input_event gb_queue[GB550D_QUEUE_SIZE];
static unsigned char gb_button_state[GB550D_BUTTON_COUNT];
static uint32_t gb_release_at[GB550D_BUTTON_COUNT];
static unsigned char gb_dir_mask;
static struct gb550d_rom_list gb_roms;
static int gb_roms_scanned;
static char gb_requested_rom[FIO_MAX_PATH_LENGTH];
static char gb_requested_name[FIO_MAX_PATH_LENGTH];
static uint32_t gb_saved_palette[256];
static int gb_palette_saved;
static int gb_video_started;
static unsigned int gb_frames;
static uint32_t gb_started_ms;

#define GB_HOST_AUDIO_RING 8192
static uint8_t retro_frame[160 * 144];
static int16_t host_audio_ring[GB_HOST_AUDIO_RING];
static volatile unsigned host_audio_write;
static volatile unsigned host_audio_read;
static unsigned host_audio_underruns;
static unsigned host_audio_overflows;
static uint8_t retro_input[GB550D_BUTTON_COUNT];
static enum retro_pixel_format host_pixel_format = RETRO_PIXEL_FORMAT_RGB332;

static void host_audio_push(int16_t sample)
{
    unsigned next = (host_audio_write + 1) % GB_HOST_AUDIO_RING;
    if (next == host_audio_read)
    {
        host_audio_overflows++;
        return;
    }
    host_audio_ring[host_audio_write] = sample;
    host_audio_write = next;
}

static size_t retro_host_audio_batch(const int16_t *data, size_t frames)
{
    size_t i;
    for (i = 0; i < frames; i++)
    {
        int mixed = ((int)data[i * 2] + (int)data[i * 2 + 1]) / 2;
        host_audio_push((int16_t)mixed);
    }
    return frames;
}

void retro_host_audio_read(int16_t *samples, unsigned int count)
{
    gb550d_core_render_audio(samples, count);
}

static bool retro_host_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            if (!data)
                return false;
            host_pixel_format = *(const enum retro_pixel_format *)data;
            return host_pixel_format == RETRO_PIXEL_FORMAT_RGB332 ||
                   host_pixel_format == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            if (data) *(const char **)data = "ML/GB/";
            return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if (data) *(const char **)data = "ML/SAVES/";
            return true;
        default:
            return false;
    }
}

static void retro_host_video(const void *data, unsigned width,
                             unsigned height, size_t pitch)
{
    unsigned x, y;
    (void)pitch;
    if (!data || width > 160 || height > 144)
        return;

    if (host_pixel_format == RETRO_PIXEL_FORMAT_RGB332)
    {
        memcpy(retro_frame, data, 160 * 144);
        return;
    }

    if (host_pixel_format == RETRO_PIXEL_FORMAT_RGB565)
    {
        const uint16_t *src = (const uint16_t *)data;
        for (y = 0; y < height; y++)
        {
            for (x = 0; x < width; x++)
            {
                uint16_t color = src[y * 160 + x];
                unsigned r = (color >> 11) & 0x1f;
                unsigned g = (color >> 5) & 0x3f;
                unsigned b = color & 0x1f;
                retro_frame[y * 160 + x] =
                    (uint8_t)(((r >> 2) << 5) | ((g >> 3) << 2) | (b >> 3));
            }
        }
    }
}

static void retro_host_input_poll(void)
{
}

static int16_t retro_host_input_state(unsigned port, unsigned device,
                                      unsigned index, unsigned id)
{
    if (port || device != RETRO_DEVICE_JOYPAD || index)
        return 0;
    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_UP:
            return retro_input[GB550D_BUTTON_UP] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:
            return retro_input[GB550D_BUTTON_DOWN] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:
            return retro_input[GB550D_BUTTON_LEFT] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:
            return retro_input[GB550D_BUTTON_RIGHT] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_A:
            return retro_input[GB550D_BUTTON_A] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_B:
            return retro_input[GB550D_BUTTON_B] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT:
            return retro_input[GB550D_BUTTON_SELECT] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START:
            return retro_input[GB550D_BUTTON_START] ? 1 : 0;
        default:
            return 0;
    }
}

static void retro_host_reset_audio(void)
{
    host_audio_write = 0;
    host_audio_read = 0;
    host_audio_underruns = 0;
    host_audio_overflows = 0;
}

static int gb_clamp(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static uint32_t gb_rgb_to_yuv(int red, int green, int blue)
{
    int y = (77 * red + 150 * green + 29 * blue) >> 8;
    int u = (-43 * red - 85 * green + 128 * blue) >> 8;
    int v = (128 * red - 107 * green - 21 * blue) >> 8;
    y = gb_clamp(y);
    u = gb_clamp(u + 128) - 128;
    v = gb_clamp(v + 128) - 128;
    return (3u << 24) | ((uint32_t)y << 16) |
           ((uint32_t)(u & 0xff) << 8) | (uint32_t)(v & 0xff);
}

static void gb_save_palette(void)
{
    int i;
    if (gb_palette_saved) return;
    for (i = 0; i < 256; i++)
        gb_saved_palette[i] = LCD_Palette[i * 3 + 2];
    gb_palette_saved = 1;
}

static void gb_restore_palette(void)
{
    int i;
    if (!gb_palette_saved) return;
    for (i = 0; i < 256; i++)
    {
        EngDrvOut(LCD_Palette[i * 3], gb_saved_palette[i]);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], gb_saved_palette[i]);
    }
    gb_palette_saved = 0;
}

static void gb_install_palette(void)
{
    int i;
    gb_save_palette();
    for (i = 0; i < 256; i++)
    {
        int r = ((i >> 5) & 7) * 255 / 7;
        int g = ((i >> 2) & 7) * 255 / 7;
        int b = (i & 3) * 255 / 3;
        uint32_t color = gb_rgb_to_yuv(r, g, b);
        EngDrvOut(LCD_Palette[i * 3], color);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], color);
    }
}

static void gb_clear_screen(void)
{
    uint8_t *vram = bmp_vram();
    int y;
    if (!vram) return;
    for (y = 0; y < GB550D_SCREEN_H; y++)
        memset(vram + y * BMPPITCH, 0x00, GB550D_SCREEN_W);
}

static void gb_copy_idle_to_real(void)
{
    uint8_t *src = bmp_vram_idle();
    uint8_t *dst = bmp_vram();
    int y;
    if (!src || !dst)
        return;
    for (y = 0; y < GB550D_SCREEN_H; y++)
        memcpy(dst + y * BMPPITCH, src + y * BMPPITCH, GB550D_SCREEN_W);
}

static void gb_draw_frame(void)
{
    const uint8_t *src = retro_frame;
    uint8_t *vram = bmp_vram();
    uint8_t row[720];
    int x, y, dy;

    if (!src || !vram)
        return;

    if (gb550d_config_video == 1)
    {
        /* 4x, crop 12 lines top/bottom -> 640x480. */
        for (y = 0; y < 120; y++)
        {
            const uint8_t *s = src + (y + 12) * 160;
            for (x = 0; x < 160; x++)
            {
                uint8_t color = s[x];
                row[x * 4 + 0] = color;
                row[x * 4 + 1] = color;
                row[x * 4 + 2] = color;
                row[x * 4 + 3] = color;
            }
            for (dy = 0; dy < 4; dy++)
                memcpy(vram + (y * 4 + dy) * BMPPITCH + 40, row, 640);
        }
    }
    else if (gb550d_config_video == 2)
    {
        /* Stretch 160x144 -> 720x480. */
        for (y = 0; y < 480; y++)
        {
            const uint8_t *s = src + (y * 144 / 480) * 160;
            for (x = 0; x < 720; x++)
                row[x] = s[x * 160 / 720];
            memcpy(vram + y * BMPPITCH, row, 720);
        }
    }
    else
    {
        /* 3x centered: 480x432 at (120,24). */
        for (y = 0; y < 144; y++)
        {
            const uint8_t *s = src + y * 160;
            for (x = 0; x < 160; x++)
            {
                uint8_t color = s[x];
                row[x * 3 + 0] = color;
                row[x * 3 + 1] = color;
                row[x * 3 + 2] = color;
            }
            for (dy = 0; dy < 3; dy++)
                memcpy(vram + (24 + y * 3 + dy) * BMPPITCH + 120,
                       row, 480);
        }
    }
}

static void gb_log(const char *text)
{
    FILE *file;
    if (!gb550d_config_debug) return;
    file = FIO_OpenFile(GB550D_LOG_PATH, O_RDWR | O_SYNC);
    if (!file)
        file = FIO_CreateFile(GB550D_LOG_PATH);
    if (!file) return;
    FIO_SeekSkipFile(file, 0, SEEK_END);
    FIO_WriteFile(file, text, strlen(text));
    FIO_CloseFile(file);
}

static void gb_show_message(const char *title, const char *detail)
{
    menu_redraw_blocked = 1;
    clrscr();
    bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK), 90, 170, "%s", title);
    if (detail)
        bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 90, 230, "%s", detail);
    msleep(3000);
    clrscr();
    menu_redraw_blocked = 0;
}

static void gb_queue_input(enum gb550d_button button, int pressed)
{
    unsigned int next;
    if (button >= GB550D_BUTTON_COUNT) return;
    pressed = pressed ? 1 : 0;
    if (gb_button_state[button] == pressed) return;
    next = (gb_queue_write + 1) % GB550D_QUEUE_SIZE;
    if (next == gb_queue_read)
    {
        if (pressed) return;
        gb_queue_read = (gb_queue_read + 1) % GB550D_QUEUE_SIZE;
    }
    gb_button_state[button] = pressed;
    gb_queue[gb_queue_write].button = button;
    gb_queue[gb_queue_write].pressed = pressed;
    gb_queue_write = next;
}

static void gb_pulse_input(enum gb550d_button button, uint32_t duration_ms)
{
    gb_queue_input(button, 1);
    gb_release_at[button] = (uint32_t)get_ms_clock() + duration_ms;
}

static void gb_release_directions(void)
{
    gb_queue_input(GB550D_BUTTON_UP, 0);
    gb_queue_input(GB550D_BUTTON_DOWN, 0);
    gb_queue_input(GB550D_BUTTON_LEFT, 0);
    gb_queue_input(GB550D_BUTTON_RIGHT, 0);
    gb_dir_mask = 0;
}

static void gb_set_directions(unsigned char mask)
{
    if (mask == gb_dir_mask) return;
    gb_release_directions();
    if (mask & 0x01) gb_queue_input(GB550D_BUTTON_UP, 1);
    if (mask & 0x02) gb_queue_input(GB550D_BUTTON_DOWN, 1);
    if (mask & 0x04) gb_queue_input(GB550D_BUTTON_LEFT, 1);
    if (mask & 0x08) gb_queue_input(GB550D_BUTTON_RIGHT, 1);
    gb_dir_mask = mask;
}

static void gb_release_all(void)
{
    int i;
    for (i = 0; i < GB550D_BUTTON_COUNT; i++)
    {
        gb_release_at[i] = 0;
        gb_queue_input((enum gb550d_button)i, 0);
    }
    gb_dir_mask = 0;
}

static void gb_reset_input_queue(void)
{
    gb_queue_read = gb_queue_write = 0;
    memset(gb_queue, 0, sizeof(gb_queue));
    memset(gb_button_state, 0, sizeof(gb_button_state));
    memset(gb_release_at, 0, sizeof(gb_release_at));
    gb_dir_mask = 0;
}

static void gb_process_input(void)
{
    uint32_t now = (uint32_t)get_ms_clock();
    int i;
    for (i = 0; i < GB550D_BUTTON_COUNT; i++)
    {
        if (gb_release_at[i] &&
            (int32_t)(now - gb_release_at[i]) >= 0)
        {
            gb_release_at[i] = 0;
            gb_queue_input((enum gb550d_button)i, 0);
        }
    }
    while (gb_queue_read != gb_queue_write)
    {
        gb_input_event event = gb_queue[gb_queue_read];
        gb_queue_read = (gb_queue_read + 1) % GB550D_QUEUE_SIZE;
        retro_input[event.button] = event.pressed;
        gb550d_core_set_button((enum gb550d_button)event.button,
                               event.pressed);
    }
}

#if defined(GB_TARGET_6D)
#define GB_PRESS_UP         0x2e
#define GB_PRESS_UP_RIGHT   0x2f
#define GB_PRESS_RIGHT      0x30
#define GB_PRESS_DOWN_RIGHT 0x31
#define GB_PRESS_DOWN       0x32
#define GB_PRESS_LEFT       0x34
#define GB_PRESS_UP_LEFT    0x35
#define GB_PRESS_DOWN_LEFT  0x1c
#define GB_UNPRESS_UDLR     0x36
#define GB_PRESS_SET        0x04
#define GB_RELEASE_SET      0x05
#define GB_MENU             0x06
#define GB_INFO             0x07
#define GB_PLAY             0x0b
#define GB_PRESS_ZOOM_IN    0x12
#define GB_UNPRESS_ZOOM_IN  0x13
#define GB_Q                0x1d
#else
#define GB_PRESS_RIGHT      0x1a
#define GB_RELEASE_RIGHT    0x1b
#define GB_PRESS_LEFT       0x1c
#define GB_RELEASE_LEFT     0x1d
#define GB_PRESS_UP         0x1e
#define GB_RELEASE_UP       0x1f
#define GB_PRESS_DOWN       0x20
#define GB_RELEASE_DOWN     0x21
#define GB_PRESS_SET        0x04
#define GB_RELEASE_SET      0x05
#define GB_MENU             0x06
#define GB_INFO             0x07
#define GB_PLAY             0x09
#define GB_Q                0x0f
#endif

static unsigned int gb_keypress_raw(unsigned int context)
{
    struct event *event = (struct event *)(uintptr_t)context;
    unsigned int key;
    if (!gb_running || !event) return 1;
    if (event->type != 0) return 1;
    key = event->param;
#if defined(GB_TARGET_6D)
    switch (key)
    {
        case GB_PRESS_UP: gb_set_directions(0x01); return 0;
        case GB_PRESS_DOWN: gb_set_directions(0x02); return 0;
        case GB_PRESS_LEFT: gb_set_directions(0x04); return 0;
        case GB_PRESS_RIGHT: gb_set_directions(0x08); return 0;
        case GB_PRESS_UP_LEFT: gb_set_directions(0x01 | 0x04); return 0;
        case GB_PRESS_UP_RIGHT: gb_set_directions(0x01 | 0x08); return 0;
        case GB_PRESS_DOWN_LEFT: gb_set_directions(0x02 | 0x04); return 0;
        case GB_PRESS_DOWN_RIGHT: gb_set_directions(0x02 | 0x08); return 0;
        case GB_UNPRESS_UDLR: gb_release_directions(); return 0;
        case GB_PRESS_SET: gb_queue_input(GB550D_BUTTON_A, 1); return 0;
        case GB_RELEASE_SET: gb_queue_input(GB550D_BUTTON_A, 0); return 0;
        case GB_PLAY: gb_pulse_input(GB550D_BUTTON_B, 140); return 0;
        case GB_PRESS_ZOOM_IN:
            gb_release_at[GB550D_BUTTON_B] = 0;
            gb_queue_input(GB550D_BUTTON_B, 1);
            return 0;
        case GB_UNPRESS_ZOOM_IN:
            gb_queue_input(GB550D_BUTTON_B, 0);
            return 0;
        case GB_INFO: gb_pulse_input(GB550D_BUTTON_SELECT, 300); return 0;
        case GB_MENU: gb_pulse_input(GB550D_BUTTON_START, 400); return 0;
        case GB_Q:
            gb_running = 0;
            return 0;
        default: return 1;
    }
#else
    switch (key)
    {
        case GB_PRESS_UP: gb_queue_input(GB550D_BUTTON_UP, 1); return 0;
        case GB_RELEASE_UP: gb_queue_input(GB550D_BUTTON_UP, 0); return 0;
        case GB_PRESS_DOWN: gb_queue_input(GB550D_BUTTON_DOWN, 1); return 0;
        case GB_RELEASE_DOWN: gb_queue_input(GB550D_BUTTON_DOWN, 0); return 0;
        case GB_PRESS_LEFT: gb_queue_input(GB550D_BUTTON_LEFT, 1); return 0;
        case GB_RELEASE_LEFT: gb_queue_input(GB550D_BUTTON_LEFT, 0); return 0;
        case GB_PRESS_RIGHT: gb_queue_input(GB550D_BUTTON_RIGHT, 1); return 0;
        case GB_RELEASE_RIGHT: gb_queue_input(GB550D_BUTTON_RIGHT, 0); return 0;
        case GB_PRESS_SET: gb_queue_input(GB550D_BUTTON_A, 1); return 0;
        case GB_RELEASE_SET: gb_queue_input(GB550D_BUTTON_A, 0); return 0;
        case GB_PLAY: gb_pulse_input(GB550D_BUTTON_B, 140); return 0;
        case GB_INFO: gb_pulse_input(GB550D_BUTTON_SELECT, 300); return 0;
        case GB_MENU: gb_pulse_input(GB550D_BUTTON_START, 400); return 0;
        case GB_Q: gb_running = 0; return 0;
        default: return 1;
    }
#endif
}

static uint8_t *gb_load_rom(const char *path, unsigned int *size_out)
{
    uint32_t size;
    uint32_t offset = 0;
    uint8_t *data;
    FILE *file;

    if (FIO_GetFileSize(path, &size) != 0 || size < 0x150 || size > 8388608)
        return NULL;
    data = malloc(size);
    if (!data) return NULL;
    file = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!file) { free(data); return NULL; }
    while (offset < size)
    {
        uint32_t remaining = size - offset;
        uint32_t chunk = remaining > 8192 ? 8192 : remaining;
        if (FIO_ReadFile(file, data + offset, chunk) != (int)chunk)
        {
            FIO_CloseFile(file);
            free(data);
            return NULL;
        }
        offset += chunk;
    }
    FIO_CloseFile(file);
    *size_out = size;
    return data;
}

static void gb_task(void *arg)
{
    struct gb550d_core_options options;
    uint8_t *rom_data;
    unsigned int rom_size;
    uint32_t next_frame;
    int pace = 0;
    uint32_t core_total = 0, display_total = 0;
    uint32_t core_peak = 0, display_peak = 0;
    int skip_active = 0;
    int skip_hold = 0;
    char perf[160];
    (void)arg;

    gb_log("GB550D session start\n");
    rom_data = gb_load_rom(gb_requested_rom, &rom_size);
    if (!rom_data)
    {
        gb_show_message("Could not load GB ROM", gb_requested_name);
        gb_task_active = 0;
        return;
    }
    options.audio_enabled = gb550d_config_audio;
    if (gb550d_core_init(rom_data, rom_size, &options) != 0)
    {
        const char *error = gb550d_core_error();
        free(rom_data);
        gb_show_message("GB core rejected ROM", error ? error : "Unknown error");
        gb_task_active = 0;
        return;
    }
    free(rom_data);

    gb_install_palette();
    gb_bmp_vram0_original = bmp_vram_info[1].vram0;
    gb_bmp_vram2_original = bmp_vram_info[1].vram2;
    gb550d_config_buffer = 0;
    bmp_draw_to_idle(0);
    clrscr();
    gb_clear_screen();
    menu_redraw_blocked = 1;
    gb_reset_input_queue();
    retro_host_reset_audio();
    gb550d_core_set_frameskip(0);
    gb_frames = 0;
    gb_started_ms = (uint32_t)get_ms_clock();
    next_frame = gb_started_ms + 17;
    gb_video_started = 0;
    if (gb550d_config_audio)
        GB550D_AUDIO_ml_start();
    gb_running = 1;
    gb_log("GB550D running\n");

    while (gb_module_alive && gb_running && !ml_shutdown_requested)
    {
        uint32_t now;
        uint32_t mark;
        uint32_t elapsed;
        int drawn;
        gb_process_input();
        mark = (uint32_t)get_ms_clock();
        gb550d_core_run_frame();
        memcpy(retro_frame, gb550d_core_framebuffer(), 160 * 144);
        elapsed = (uint32_t)get_ms_clock() - mark;
        core_total += elapsed;
        if (elapsed > core_peak) core_peak = elapsed;

        /* Complex effects can push one emulated frame past the 60Hz
         * budget.  Skip LCD drawing for a short burst instead of letting
         * the whole loop fall to ~40fps.  CPU/APU still run every frame. */
        if (elapsed > 17)
        {
            skip_active = 1;
            skip_hold = 90;
            gb550d_core_set_frameskip(1);
        }
        else if (skip_active && --skip_hold <= 0)
        {
            skip_active = 0;
            gb550d_core_set_frameskip(0);
        }

        mark = (uint32_t)get_ms_clock();
        drawn = (!skip_active || (gb_frames & 1) == 0);
        if (drawn)
            gb_draw_frame();
        if (drawn && gb550d_config_buffer == 1)
            gb_copy_idle_to_real();
        if (drawn && gb550d_config_buffer == 2)
        {
            uint8_t *front = bmp_vram_info[1].vram0;
            uint8_t *back = bmp_vram_info[1].vram2;
            bmp_vram_info[1].vram0 = back;
            bmp_vram_info[1].vram2 = front;
        }
        elapsed = (uint32_t)get_ms_clock() - mark;
        display_total += elapsed;
        if (elapsed > display_peak) display_peak = elapsed;
        gb_video_started = 1;
        if (gb550d_core_error())
        {
            gb_log("GB550D core error\n");
            break;
        }
        gb_frames++;
        now = (uint32_t)get_ms_clock();
        if ((gb_frames % 300) == 0)
        {
            uint32_t total_ms = now - gb_started_ms;
            uint32_t fps10 = total_ms ? (gb_frames * 10000u) / total_ms : 0;
            snprintf(perf, sizeof(perf),
                     "PERF frames=%d fps=%d.%d core=%d/%d display=%d/%d "
                     "audio_under=%d audio_over=%d\n",
                     (int)gb_frames, (int)(fps10 / 10), (int)(fps10 % 10),
                     (int)core_total, (int)core_peak,
                     (int)display_total, (int)display_peak,
                     (int)host_audio_underruns,
                     (int)host_audio_overflows);
            gb_log(perf);
        }
        if ((int32_t)(next_frame - now) > 0)
            msleep(next_frame - now);
        else
        {
            uint32_t late = now - next_frame;
            if (late > 34) next_frame = now;
        }
        next_frame += (pace < 3) ? 16 : 17;
        pace = (pace + 1) % 4;
    }

    gb_running = 0;
    gb550d_core_set_frameskip(0);
    {
        uint32_t total_ms = (uint32_t)get_ms_clock() - gb_started_ms;
        uint32_t fps10 = total_ms ? (gb_frames * 10000u) / total_ms : 0;
        snprintf(perf, sizeof(perf),
                 "PERF final frames=%d fps=%d.%d core=%d/%d display=%d/%d "
                 "audio_under=%d audio_over=%d\n",
                 (int)gb_frames, (int)(fps10 / 10), (int)(fps10 % 10),
                 (int)core_total, (int)core_peak,
                 (int)display_total, (int)display_peak,
                 (int)host_audio_underruns,
                 (int)host_audio_overflows);
        gb_log(perf);
    }
    GB550D_AUDIO_ml_shutdown();
    gb_release_all();
    gb550d_core_shutdown();
    bmp_draw_to_idle(0);
    if (gb_bmp_vram0_original)
        bmp_vram_info[1].vram0 = gb_bmp_vram0_original;
    if (gb_bmp_vram2_original)
        bmp_vram_info[1].vram2 = gb_bmp_vram2_original;
    gb_clear_screen();
    gb_restore_palette();
    clrscr();
    menu_redraw_blocked = 0;
    gb_task_active = 0;
    gb_log("GB550D session stop\n");
}

static int gb_selected_index(void)
{
    if (!gb_roms_scanned)
        gb550d_rom_scan(&gb_roms);
    if (!gb_roms.count)
        return -1;
    if (gb550d_config_rom_id)
    {
        int index = gb550d_rom_find_id(&gb_roms, (unsigned int)gb550d_config_rom_id);
        if (index >= 0) return index;
    }
    return 0;
}

static MENU_SELECT_FUNC(gb_start)
{
    int index;
    (void)priv; (void)delta;
    if (gb_task_active) return;
    index = gb_selected_index();
    if (index < 0)
    {
        NotifyBox(3000, "No .gb/.gbc ROMs in ML/GB");
        return;
    }
    snprintf(gb_requested_name, sizeof(gb_requested_name), "%s",
             gb_roms.names[index]);
    gb550d_rom_make_path(gb_requested_rom, sizeof(gb_requested_rom),
                         gb_requested_name);
    gb_task_active = 1;
    task_create("gb_task", 0x1c, 0x10000, gb_task, 0);
}

static MENU_SELECT_FUNC(gb_rom_select)
{
    int index;
    if (gb_task_active || !delta) return;
    if (!gb_roms_scanned) gb550d_rom_scan(&gb_roms);
    if (!gb_roms.count) return;
    index = gb_selected_index();
    if (index < 0) index = 0;
    index += delta;
    while (index < 0) index += gb_roms.count;
    while (index >= gb_roms.count) index -= gb_roms.count;
    gb550d_config_rom_id = (int)gb550d_rom_name_id(gb_roms.names[index]);
}

static MENU_UPDATE_FUNC(gb_rom_update)
{
    int index;
    if (!gb_roms_scanned) gb550d_rom_scan(&gb_roms);
    index = gb_selected_index();
    if (index >= 0)
        MENU_SET_VALUE("%s", gb_roms.names[index]);
    else
        MENU_SET_VALUE("No ROMs in ML/GB");
}

static MENU_SELECT_FUNC(gb_logs_clear)
{
    (void)priv; (void)delta;
    FIO_RemoveFile(GB550D_LOG_PATH);
    NotifyBox(2000, "GB logs cleared");
}

static MENU_SELECT_FUNC(gb_reset_settings)
{
    (void)priv; (void)delta;
    gb550d_config_reset();
    NotifyBox(2000, "GB settings reset");
}

static struct menu_entry gb_menu[] =
{
    {
        .name = "GB",
        .select = gb_start,
        .help = "Run Game Boy / Game Boy Color from ML/GB.",
        .children = (struct menu_entry[])
        {
            {
                .name = "Start GB",
                .select = gb_start,
                .help = "Start the selected .gb/.gbc ROM.",
            },
            {
                .name = "ROM",
                .priv = &gb550d_config_rom_id,
                .select = gb_rom_select,
                .update = gb_rom_update,
                .help = "Choose a ROM from ML/GB.",
            },
            {
                .name = "Video",
                .priv = &gb550d_config_video,
                .min = 0, .max = 2,
                .choices = (const char *[]) { "3x", "4x crop", "Stretch" },
                .help = "3x centered, 4x cropped, or stretch to 720x480.",
            },
            {
                .name = "Audio",
                .priv = &gb550d_config_audio,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Enable GB APU audio.",
            },
            {
                .name = "Volume",
                .priv = &gb550d_config_volume,
                .min = 1, .max = ASIF_MAX_VOL,
                .help = "GB audio output volume.",
            },
            {
                .name = "Debug logging",
                .priv = &gb550d_config_debug,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Write GB diagnostics to ML/LOGS/GB550D.LOG.",
            },
            {
                .name = "Clear GB logs",
                .select = gb_logs_clear,
                .help = "Delete ML/LOGS/GB550D.LOG.",
            },
            {
                .name = "Reset settings",
                .select = gb_reset_settings,
                .help = "Restore GB audio/volume/debug defaults.",
            },
            MENU_EOL,
        },
    }
};

static unsigned int gb_init(void)
{
    gb_module_alive = 1;
    gb_running = 0;
    gb_task_active = 0;
    gb_roms_scanned = 0;
    retro_host_reset_audio();
    retro_set_environment(retro_host_environment);
    retro_set_video_refresh(retro_host_video);
    retro_set_audio_sample_batch(retro_host_audio_batch);
    retro_set_input_poll(retro_host_input_poll);
    retro_set_input_state(retro_host_input_state);
    retro_init();
    menu_add("Games", gb_menu, COUNT(gb_menu));
#if defined(GB_TARGET_6D)
    printf("gb550d: GB menu registered (6D keymap)\n");
#else
    printf("gb550d: GB menu registered\n");
#endif
    return 0;
}

static unsigned int gb_deinit(void)
{
    gb_module_alive = 0;
    gb_running = 0;
    GB550D_AUDIO_ml_shutdown();
    msleep(100);
    gb_release_all();
    gb_reset_input_queue();
    gb550d_core_shutdown();
    retro_deinit();
    gb_clear_screen();
    gb_restore_palette();
    menu_redraw_blocked = 0;
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(gb_init)
    MODULE_DEINIT(gb_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS_RAW, gb_keypress_raw, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(gb550d_config_rom_id)
    MODULE_CONFIG(gb550d_config_video)
    MODULE_CONFIG(gb550d_config_audio)
    MODULE_CONFIG(gb550d_config_volume)
    MODULE_CONFIG(gb550d_config_debug)
MODULE_CONFIGS_END()

