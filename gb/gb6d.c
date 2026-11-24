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
#define GB6D_CONFIG_DEFINE
#include "gb_config.h"

#define GB6D_SCREEN_W 720
#define GB6D_SCREEN_H 480
#define GB6D_VIEW_W (160 * 3)
#define GB6D_VIEW_H (144 * 3)
#define GB6D_VIEW_X ((GB6D_SCREEN_W - GB6D_VIEW_W) / 2)
#define GB6D_VIEW_Y ((GB6D_SCREEN_H - GB6D_VIEW_H) / 2)
#define GB6D_QUEUE_SIZE 64
#define GB6D_LOG_PATH "ML/LOGS/GB6D.LOG"

extern int menu_redraw_blocked;
void gb6d_core_set_frameskip(int enabled);

#ifndef ASIF_MAX_VOL
#define ASIF_MAX_VOL 10
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
static gb_input_event gb_queue[GB6D_QUEUE_SIZE];
static unsigned char gb_button_state[GB6D_BUTTON_COUNT];
static uint32_t gb_release_at[GB6D_BUTTON_COUNT];
static unsigned char gb_dir_mask;
static struct gb6d_rom_list gb_roms;
static int gb_roms_scanned;
static char gb_requested_rom[FIO_MAX_PATH_LENGTH];
static char gb_requested_name[FIO_MAX_PATH_LENGTH];
static uint32_t gb_saved_palette[256];
static int gb_palette_saved;
static int gb_video_started;
static unsigned int gb_frames;
static uint32_t gb_started_ms;

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
    for (y = 0; y < GB6D_SCREEN_H; y++)
        memset(vram + y * BMPPITCH, 0x00, GB6D_SCREEN_W);
}

static void gb_draw_frame(void)
{
    const uint8_t *src = gb6d_core_framebuffer();
    uint8_t *vram = bmp_vram();
    uint8_t row[720];
    int x, y, dy;

    if (!src || !vram)
        return;

    if (gb6d_config_video == 1)
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
    else if (gb6d_config_video == 2)
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
    if (!gb6d_config_debug) return;
    file = FIO_OpenFile(GB6D_LOG_PATH, O_RDWR | O_SYNC);
    if (!file)
        file = FIO_CreateFile(GB6D_LOG_PATH);
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

static void gb_queue_input(enum gb6d_button button, int pressed)
{
    unsigned int next;
    if (button >= GB6D_BUTTON_COUNT) return;
    pressed = pressed ? 1 : 0;
    if (gb_button_state[button] == pressed) return;
    next = (gb_queue_write + 1) % GB6D_QUEUE_SIZE;
    if (next == gb_queue_read)
    {
        if (pressed) return;
        gb_queue_read = (gb_queue_read + 1) % GB6D_QUEUE_SIZE;
    }
    gb_button_state[button] = pressed;
    gb_queue[gb_queue_write].button = button;
    gb_queue[gb_queue_write].pressed = pressed;
    gb_queue_write = next;
}

static void gb_pulse_input(enum gb6d_button button, uint32_t duration_ms)
{
    gb_queue_input(button, 1);
    gb_release_at[button] = (uint32_t)get_ms_clock() + duration_ms;
}

static void gb_release_directions(void)
{
    gb_queue_input(GB6D_BUTTON_UP, 0);
    gb_queue_input(GB6D_BUTTON_DOWN, 0);
    gb_queue_input(GB6D_BUTTON_LEFT, 0);
    gb_queue_input(GB6D_BUTTON_RIGHT, 0);
    gb_dir_mask = 0;
}

static void gb_set_directions(unsigned char mask)
{
    if (mask == gb_dir_mask) return;
    gb_release_directions();
    if (mask & 0x01) gb_queue_input(GB6D_BUTTON_UP, 1);
    if (mask & 0x02) gb_queue_input(GB6D_BUTTON_DOWN, 1);
    if (mask & 0x04) gb_queue_input(GB6D_BUTTON_LEFT, 1);
    if (mask & 0x08) gb_queue_input(GB6D_BUTTON_RIGHT, 1);
    gb_dir_mask = mask;
}

static void gb_release_all(void)
{
    int i;
    for (i = 0; i < GB6D_BUTTON_COUNT; i++)
    {
        gb_release_at[i] = 0;
        gb_queue_input((enum gb6d_button)i, 0);
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
    for (i = 0; i < GB6D_BUTTON_COUNT; i++)
    {
        if (gb_release_at[i] &&
            (int32_t)(now - gb_release_at[i]) >= 0)
        {
            gb_release_at[i] = 0;
            gb_queue_input((enum gb6d_button)i, 0);
        }
    }
    while (gb_queue_read != gb_queue_write)
    {
        gb_input_event event = gb_queue[gb_queue_read];
        gb_queue_read = (gb_queue_read + 1) % GB6D_QUEUE_SIZE;
        gb6d_core_set_button((enum gb6d_button)event.button,
                               event.pressed);
    }
}

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

static unsigned int gb_keypress_raw(unsigned int context)
{
    struct event *event = (struct event *)(uintptr_t)context;
    unsigned int key;
    if (!gb_running || !event) return 1;
    if (event->type != 0) return 1;
    key = event->param;
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
        case GB_PRESS_SET: gb_queue_input(GB6D_BUTTON_A, 1); return 0;
        case GB_RELEASE_SET: gb_queue_input(GB6D_BUTTON_A, 0); return 0;
        case GB_PLAY: gb_pulse_input(GB6D_BUTTON_B, 140); return 0;
        case GB_PRESS_ZOOM_IN:
            gb_release_at[GB6D_BUTTON_B] = 0;
            gb_queue_input(GB6D_BUTTON_B, 1);
            return 0;
        case GB_UNPRESS_ZOOM_IN:
            gb_queue_input(GB6D_BUTTON_B, 0);
            return 0;
        case GB_INFO: gb_pulse_input(GB6D_BUTTON_SELECT, 300); return 0;
        case GB_MENU: gb_pulse_input(GB6D_BUTTON_START, 400); return 0;
        case GB_Q:
            gb_running = 0;
            return 0;
        default: return 1;
    }
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
    struct gb6d_core_options options;
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

    gb_log("GB6D session start\n");
    rom_data = gb_load_rom(gb_requested_rom, &rom_size);
    if (!rom_data)
    {
        gb_show_message("Could not load GB ROM", gb_requested_name);
        gb_task_active = 0;
        return;
    }
    options.audio_enabled = gb6d_config_audio;
    if (gb6d_core_init(rom_data, rom_size, &options) != 0)
    {
        const char *error = gb6d_core_error();
        free(rom_data);
        gb_show_message("GB core rejected ROM", error ? error : "Unknown error");
        gb_task_active = 0;
        return;
    }
    free(rom_data);

    gb_install_palette();
    clrscr();
    gb_clear_screen();
    menu_redraw_blocked = 1;
    gb_reset_input_queue();
    gb6d_core_set_frameskip(0);
    gb_frames = 0;
    gb_started_ms = (uint32_t)get_ms_clock();
    next_frame = gb_started_ms + 17;
    gb_video_started = 0;
    if (gb6d_config_audio)
        GB6D_AUDIO_ml_start();
    gb_running = 1;
    gb_log("GB6D running\n");

    while (gb_module_alive && gb_running && !ml_shutdown_requested)
    {
        uint32_t now;
        uint32_t mark;
        uint32_t elapsed;
        int drawn;
        gb_process_input();
        mark = (uint32_t)get_ms_clock();
        gb6d_core_run_frame();
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
            gb6d_core_set_frameskip(1);
        }
        else if (skip_active && --skip_hold <= 0)
        {
            skip_active = 0;
            gb6d_core_set_frameskip(0);
        }

        mark = (uint32_t)get_ms_clock();
        drawn = (!skip_active || (gb_frames & 1) == 0);
        if (drawn)
            gb_draw_frame();
        elapsed = (uint32_t)get_ms_clock() - mark;
        display_total += elapsed;
        if (elapsed > display_peak) display_peak = elapsed;
        gb_video_started = 1;
        if (gb6d_core_error())
        {
            gb_log("GB6D core error\n");
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
                     (int)gb6d_core_audio_underruns(),
                     (int)gb6d_core_audio_overflows());
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
    gb6d_core_set_frameskip(0);
    {
        uint32_t total_ms = (uint32_t)get_ms_clock() - gb_started_ms;
        uint32_t fps10 = total_ms ? (gb_frames * 10000u) / total_ms : 0;
        snprintf(perf, sizeof(perf),
                 "PERF final frames=%d fps=%d.%d core=%d/%d display=%d/%d "
                 "audio_under=%d audio_over=%d\n",
                 (int)gb_frames, (int)(fps10 / 10), (int)(fps10 % 10),
                 (int)core_total, (int)core_peak,
                 (int)display_total, (int)display_peak,
                 (int)gb6d_core_audio_underruns(),
                 (int)gb6d_core_audio_overflows());
        gb_log(perf);
    }
    GB6D_AUDIO_ml_shutdown();
    gb_release_all();
    gb6d_core_shutdown();
    gb_clear_screen();
    gb_restore_palette();
    clrscr();
    menu_redraw_blocked = 0;
    gb_task_active = 0;
    gb_log("GB6D session stop\n");
}

static int gb_selected_index(void)
{
    if (!gb_roms_scanned)
        gb6d_rom_scan(&gb_roms);
    if (!gb_roms.count)
        return -1;
    if (gb6d_config_rom_id)
    {
        int index = gb6d_rom_find_id(&gb_roms, (unsigned int)gb6d_config_rom_id);
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
    gb6d_rom_make_path(gb_requested_rom, sizeof(gb_requested_rom),
                         gb_requested_name);
    gb_task_active = 1;
    task_create("gb_task", 0x1c, 0x10000, gb_task, 0);
}

static MENU_SELECT_FUNC(gb_rom_select)
{
    int index;
    if (gb_task_active || !delta) return;
    if (!gb_roms_scanned) gb6d_rom_scan(&gb_roms);
    if (!gb_roms.count) return;
    index = gb_selected_index();
    if (index < 0) index = 0;
    index += delta;
    while (index < 0) index += gb_roms.count;
    while (index >= gb_roms.count) index -= gb_roms.count;
    gb6d_config_rom_id = (int)gb6d_rom_name_id(gb_roms.names[index]);
}

static MENU_UPDATE_FUNC(gb_rom_update)
{
    int index;
    if (!gb_roms_scanned) gb6d_rom_scan(&gb_roms);
    index = gb_selected_index();
    if (index >= 0)
        MENU_SET_VALUE("%s", gb_roms.names[index]);
    else
        MENU_SET_VALUE("No ROMs in ML/GB");
}

static MENU_SELECT_FUNC(gb_logs_clear)
{
    (void)priv; (void)delta;
    FIO_RemoveFile(GB6D_LOG_PATH);
    NotifyBox(2000, "GB logs cleared");
}

static MENU_SELECT_FUNC(gb_reset_settings)
{
    (void)priv; (void)delta;
    gb6d_config_reset();
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
                .priv = &gb6d_config_rom_id,
                .select = gb_rom_select,
                .update = gb_rom_update,
                .help = "Choose a ROM from ML/GB.",
            },
            {
                .name = "Video",
                .priv = &gb6d_config_video,
                .min = 0, .max = 2,
                .choices = (const char *[]) { "3x", "4x crop", "Stretch" },
                .help = "3x centered, 4x cropped, or stretch to 720x480.",
            },
            {
                .name = "Audio",
                .priv = &gb6d_config_audio,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Enable GB APU audio.",
            },
            {
                .name = "Volume",
                .priv = &gb6d_config_volume,
                .min = 1, .max = ASIF_MAX_VOL,
                .help = "GB audio output volume.",
            },
            {
                .name = "Debug logging",
                .priv = &gb6d_config_debug,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Write GB diagnostics to ML/LOGS/GB6D.LOG.",
            },
            {
                .name = "Clear GB logs",
                .select = gb_logs_clear,
                .help = "Delete ML/LOGS/GB6D.LOG.",
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
    menu_add("Games", gb_menu, COUNT(gb_menu));
    printf("gb6d: GB menu registered (6D)\n");
    return 0;
}

static unsigned int gb_deinit(void)
{
    gb_module_alive = 0;
    gb_running = 0;
    GB6D_AUDIO_ml_shutdown();
    msleep(100);
    gb_release_all();
    gb_reset_input_queue();
    gb6d_core_shutdown();
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
    MODULE_CONFIG(gb6d_config_rom_id)
    MODULE_CONFIG(gb6d_config_video)
    MODULE_CONFIG(gb6d_config_audio)
    MODULE_CONFIG(gb6d_config_volume)
    MODULE_CONFIG(gb6d_config_debug)
MODULE_CONFIGS_END()



