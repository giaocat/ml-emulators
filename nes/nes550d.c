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

struct nes550d_bmp_vram_info
{
    uint8_t *vram0;
    uint32_t off_0x04;
    uint8_t *vram2;
};
extern struct nes550d_bmp_vram_info bmp_vram_info[];

#include "nes_core.h"
#include "nes_audio_ml.h"
#include "nes_rom.h"
#define NES550D_CONFIG_DEFINE
#include "nes_config.h"

/*
 * Standalone module builds do not define CONFIG_6D.  Build the 6D keymap with
 * "make NES_6D=1"; CONFIG_6D is also accepted when built in a 6D tree.
 */
#if defined(NES_6D) || defined(CONFIG_6D)
#define NES_TARGET_6D 1
#endif

/* 60 fps mode: Direct BMP writes, render every logic frame.  The old
 * Double/Flip experiments are kept out of the active path. */
#define NES550D_LEGACY_RENDER 0

#ifndef ASIF_MAX_VOL
#if defined(NES_TARGET_6D)
#define ASIF_MAX_VOL 10
#else
#define ASIF_MAX_VOL 5
#endif
#endif

#define NES550D_SCREEN_W 720
#define NES550D_SCREEN_H 480
#define NES550D_VIEW_X 104
#define NES550D_VIEW_W 512
#define NES550D_QUEUE_SIZE 64
#define NES550D_MAX_RENDER_GAP 8
#define NES550D_LOG_PATH "ML/LOGS/NES550D.LOG"
#define NES550D_RAW_LOG_PATH "ML/LOGS/NESRAW.LOG"
#define NES550D_RAW_TRACE_MAX 512
#define NES550D_MAX_LOG_FILES 32

extern int menu_redraw_blocked;

struct nes550d_input_event
{
    unsigned char button;
    unsigned char pressed;
};

struct nes550d_raw_trace_entry
{
    uint32_t ms;
    uint32_t type;
    uint32_t param;
    uint32_t arg;
    uint32_t obj;
};

static volatile int nes550d_module_alive;
static volatile int nes550d_running;
static volatile int nes550d_task_active;
static volatile unsigned int nes550d_queue_write;
static volatile unsigned int nes550d_queue_read;
static struct nes550d_input_event nes550d_queue[NES550D_QUEUE_SIZE];
static unsigned char nes550d_button_state[NES550D_BUTTON_COUNT];
static uint32_t nes550d_release_at[NES550D_BUTTON_COUNT];
#if defined(NES_TARGET_6D)
static unsigned char nes550d_dir_mask;
#endif
static struct nes550d_rom_list nes550d_roms;
static int nes550d_roms_scanned;
static char nes550d_requested_rom[FIO_MAX_PATH_LENGTH];
static char nes550d_requested_name[FIO_MAX_PATH_LENGTH];
static char nes550d_last_input[48] = "READY";
static uint32_t nes550d_saved_palette[256];
static int nes550d_palette_saved;
static uint16_t nes550d_stretch_xmap[NES550D_SCREEN_W];
static int nes550d_stretch_xmap_ready;
static struct nes550d_raw_trace_entry
    nes550d_raw_trace[NES550D_RAW_TRACE_MAX];
static volatile unsigned int nes550d_raw_trace_count;
static volatile unsigned int nes550d_raw_trace_dropped;
static volatile unsigned int nes550d_stop_key;
static volatile unsigned int nes550d_vsync_count;
static unsigned int nes550d_last_vsync;
static int nes550d_vsync_seen;
static int nes550d_frame_ready;
static int nes550d_pace_phase;
static uint8_t *nes550d_bmp_vram0_original;
static uint8_t *nes550d_bmp_vram2_original;
static int nes550d_bmp_flipped;
static char nes550d_log_path[FIO_MAX_PATH_LENGTH] = NES550D_LOG_PATH;
static char nes550d_raw_log_path[FIO_MAX_PATH_LENGTH] = NES550D_RAW_LOG_PATH;

static void nes550d_set_session_log_paths(const char *rom_name)
{
    char stem[25];
    unsigned int source = 0, target = 0;
    while (rom_name && rom_name[source] && rom_name[source] != '.' &&
           target + 1 < sizeof(stem))
    {
        int c = (unsigned char)rom_name[source++];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            stem[target++] = (char)c;
        else if (target && stem[target - 1] != '-')
            stem[target++] = '-';
    }
    if (!target)
    {
        memcpy(stem, "unknown", 7);
        target = 7;
    }
    stem[target] = '\0';
    snprintf(nes550d_log_path, sizeof(nes550d_log_path),
             "ML/LOGS/NES-%s.LOG", stem);
    snprintf(nes550d_raw_log_path, sizeof(nes550d_raw_log_path),
             "ML/LOGS/NESR-%s.LOG", stem);
}

struct nes550d_profile
{
    uint32_t started_ms;
    uint32_t frames;
    uint32_t rendered_frames, skipped_renders;
    uint32_t cpu_total_ms, cpu_peak_ms;
    uint32_t ppu_total_ms, ppu_peak_ms;
    uint32_t display_total_ms, display_peak_ms;
    uint32_t late_frames;
    uint32_t forced_renders, clock_resyncs, max_late_ms;
    uint32_t start_presses, last_start_frame, last_start_pc, last_start_mode;
};

static struct nes550d_profile nes550d_profile;

static void nes550d_log(const char *message)
{
    FILE *file;
    if (!nes550d_config_debug)
        return;
    file = FIO_OpenFile(nes550d_log_path, O_RDWR | O_SYNC);
    if (!file)
        file = FIO_CreateFile(nes550d_log_path);
    if (!file)
        return;
    FIO_SeekSkipFile(file, 0, SEEK_END);
    FIO_WriteFile(file, message, strlen(message));
    FIO_CloseFile(file);
}

static void nes550d_log_reset(void)
{
    FILE *file;
    static const char header[] =
        "NES550D emulator log\n"
        "===================\n";
    if (!nes550d_config_debug) return;
    file = FIO_CreateFile(nes550d_log_path);
    if (!file) return;
    FIO_WriteFile(file, header, sizeof(header) - 1);
    FIO_CloseFile(file);
}

static void nes550d_log_checkpoint(const char *where)
{
    char line[224];
    int length;
    if (!nes550d_config_debug) return;
    length = snprintf(
        line, sizeof(line),
        "CHECK %s ms=%d frames=%d running=%d module=%d shutdown=%d "
        "queue=%d raw=%d dropped=%d\n",
        where, (unsigned int)get_ms_clock(),
        (unsigned int)nes550d_profile.frames, (int)nes550d_running,
        (int)nes550d_module_alive, (int)ml_shutdown_requested,
        (unsigned int)((nes550d_queue_write + NES550D_QUEUE_SIZE -
                        nes550d_queue_read) % NES550D_QUEUE_SIZE),
        (unsigned int)nes550d_raw_trace_count,
        (unsigned int)nes550d_raw_trace_dropped);
    if (length > 0)
    {
        line[sizeof(line) - 1] = '\0';
        nes550d_log(line);
    }
}

static void nes550d_raw_trace_add(const struct event *event)
{
    unsigned int index;
    if (!nes550d_config_debug || !event)
        return;
    index = nes550d_raw_trace_count;
    if (index >= NES550D_RAW_TRACE_MAX)
    {
        nes550d_raw_trace_dropped++;
        return;
    }
    nes550d_raw_trace[index].ms = (uint32_t)get_ms_clock();
    nes550d_raw_trace[index].type = (uint32_t)event->type;
    nes550d_raw_trace[index].param = (uint32_t)event->param;
    nes550d_raw_trace[index].arg = (uint32_t)event->arg;
    nes550d_raw_trace[index].obj = (uint32_t)(uintptr_t)event->obj;
    nes550d_raw_trace_count = index + 1;
}

static void nes550d_raw_trace_dump(void)
{
    FILE *file;
    char line[160];
    unsigned int i;
    static const char header[] = "NES550D raw key trace\n";
    if (!nes550d_config_debug)
        return;
    file = FIO_CreateFile(nes550d_raw_log_path);
    if (!file)
        return;
    FIO_WriteFile(file, header, sizeof(header) - 1);
    for (i = 0; i < nes550d_raw_trace_count; i++)
    {
        int length = snprintf(
            line, sizeof(line),
            "%d type=0x%x param=0x%x arg=0x%x obj=0x%x\n",
            (unsigned int)nes550d_raw_trace[i].ms,
            (unsigned int)nes550d_raw_trace[i].type,
            (unsigned int)nes550d_raw_trace[i].param,
            (unsigned int)nes550d_raw_trace[i].arg,
            (unsigned int)nes550d_raw_trace[i].obj);
        if (length > 0)
            FIO_WriteFile(file, line,
                length < (int)sizeof(line) ? (unsigned int)length :
                                             (unsigned int)sizeof(line) - 1);
    }
    {
        int length = snprintf(line, sizeof(line),
            "events=%d dropped=%d\n",
            (unsigned int)nes550d_raw_trace_count,
            (unsigned int)nes550d_raw_trace_dropped);
        if (length > 0) FIO_WriteFile(file, line, (unsigned int)length);
    }
    FIO_CloseFile(file);
}

static void nes550d_profile_add(uint32_t *total, uint32_t *peak,
                               uint32_t elapsed)
{
    *total += elapsed;
    if (elapsed > *peak) *peak = elapsed;
}

static void nes550d_profile_dump(void)
{
    char line[1024];
    uint32_t elapsed;
    if (!nes550d_config_debug) return;
    elapsed = (uint32_t)get_ms_clock() - nes550d_profile.started_ms;
    uint32_t fps_tenths = elapsed ?
        (nes550d_profile.frames * 10000u) / elapsed : 0;
    int length = snprintf(
        line, sizeof(line),
        "PERF logic_frames=%d rendered=%d skipped=%d elapsed_ms=%d "
        "logic_fps=%d.%d late=%d\n"
        "PERF cpu_total=%d cpu_peak=%d ppu_total=%d ppu_peak=%d "
        "display_total=%d display_peak=%d\n"
        "PERF audio_callbacks=%d audio_total=%d audio_peak=%d "
        "audio_misses=%d\n"
        "PERF controller_reads=%d controller_latches=%d "
        "controller_observed=0x%x\n"
        "PERF start_presses=%d start_frame=%d start_pc=0x%x "
        "start_mode=0x%x\n"
        "PERF forced_renders=%d clock_resyncs=%d max_late_ms=%d "
        "ppu_ctrl=0x%x ppu_mask=0x%x ppu_dirty=%d scroll=0x%x "
        "frame_scroll=0x%x mmc3_split=%d\n"
        "PERF vsync=%d\n",
        (unsigned int)nes550d_profile.frames,
        (unsigned int)nes550d_profile.rendered_frames,
        (unsigned int)nes550d_profile.skipped_renders,
        (unsigned int)elapsed,
        (unsigned int)(fps_tenths / 10), (unsigned int)(fps_tenths % 10),
        (unsigned int)nes550d_profile.late_frames,
        (unsigned int)nes550d_profile.cpu_total_ms,
        (unsigned int)nes550d_profile.cpu_peak_ms,
        (unsigned int)nes550d_profile.ppu_total_ms,
        (unsigned int)nes550d_profile.ppu_peak_ms,
        (unsigned int)nes550d_profile.display_total_ms,
        (unsigned int)nes550d_profile.display_peak_ms,
        nes550d_audio_ml_callback_count(),
        nes550d_audio_ml_render_total_ms(),
        nes550d_audio_ml_render_peak_ms(),
        nes550d_audio_ml_deadline_misses(),
        nes550d_core_controller_reads(),
        nes550d_core_controller_latches(),
        nes550d_core_observed_buttons(),
        (unsigned int)nes550d_profile.start_presses,
        (unsigned int)nes550d_profile.last_start_frame,
        (unsigned int)nes550d_profile.last_start_pc,
        (unsigned int)nes550d_profile.last_start_mode,
        (unsigned int)nes550d_profile.forced_renders,
        (unsigned int)nes550d_profile.clock_resyncs,
        (unsigned int)nes550d_profile.max_late_ms,
        nes550d_core_debug_ppu_ctrl(), nes550d_core_debug_ppu_mask(),
        nes550d_core_debug_ppu_dirty(), nes550d_core_debug_ppu_scroll(),
        nes550d_core_debug_frame_scroll(),
        nes550d_core_debug_mmc3_split(),
        (int)nes550d_vsync_count);
    if (length > 0)
    {
        FILE *file = FIO_OpenFile(nes550d_log_path, O_RDWR | O_SYNC);
        if (!file) file = FIO_CreateFile(nes550d_log_path);
        if (file)
        {
            FIO_SeekSkipFile(file, 0, SEEK_END);
            FIO_WriteFile(file, line,
                length < (int)sizeof(line) ? (unsigned int)length :
                                             (unsigned int)sizeof(line) - 1);
            FIO_CloseFile(file);
        }
    }
}

static int nes550d_ascii_lower(int character)
{
    return character >= 'A' && character <= 'Z' ?
           character + ('a' - 'A') : character;
}

static int nes550d_is_log_name(const char *name)
{
    unsigned int length;
    if (!name)
        return 0;
    length = strlen(name);
    return length >= 7 &&
           nes550d_ascii_lower(name[0]) == 'n' &&
           nes550d_ascii_lower(name[1]) == 'e' &&
           nes550d_ascii_lower(name[2]) == 's' &&
           name[length - 4] == '.' &&
           nes550d_ascii_lower(name[length - 3]) == 'l' &&
           nes550d_ascii_lower(name[length - 2]) == 'o' &&
           nes550d_ascii_lower(name[length - 1]) == 'g';
}

static void nes550d_save_palette(void)
{
    int i;
    if (nes550d_palette_saved)
        return;
    for (i = 0; i < 256; i++)
        nes550d_saved_palette[i] = LCD_Palette[i * 3 + 2];
    nes550d_palette_saved = 1;
}

static void nes550d_restore_palette(void)
{
    int i;
    if (!nes550d_palette_saved)
        return;
    for (i = 0; i < 256; i++)
    {
        EngDrvOut(LCD_Palette[i * 3], nes550d_saved_palette[i]);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], nes550d_saved_palette[i]);
    }
    nes550d_palette_saved = 0;
}

static int nes550d_clamp(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static uint32_t nes550d_rgb_to_yuv(int red, int green, int blue)
{
    int y = (77 * red + 150 * green + 29 * blue) >> 8;
    int u = (-43 * red - 85 * green + 128 * blue) >> 8;
    int v = (128 * red - 107 * green - 21 * blue) >> 8;
    y = nes550d_clamp(y);
    u = nes550d_clamp(u + 128) - 128;
    v = nes550d_clamp(v + 128) - 128;
    return (3u << 24) | ((uint32_t)y << 16) |
           ((uint32_t)(u & 0xff) << 8) | (uint32_t)(v & 0xff);
}

static void nes550d_install_palette(void)
{
    static const uint8_t rgb[64][3] =
    {
        {84,84,84},{0,30,116},{8,16,144},{48,0,136},{68,0,100},{92,0,48},{84,4,0},{60,24,0},
        {32,42,0},{8,58,0},{0,64,0},{0,60,0},{0,50,60},{0,0,0},{0,0,0},{0,0,0},
        {152,150,152},{8,76,196},{48,50,236},{92,30,228},{136,20,176},{160,20,100},{152,34,32},{120,60,0},
        {84,90,0},{40,114,0},{8,124,0},{0,118,40},{0,102,120},{0,0,0},{0,0,0},{0,0,0},
        {236,238,236},{76,154,236},{120,124,236},{176,98,236},{228,84,236},{236,88,180},{236,106,100},{212,136,32},
        {160,170,0},{116,196,0},{76,208,32},{56,204,108},{56,180,204},{60,60,60},{0,0,0},{0,0,0},
        {236,238,236},{168,204,236},{188,188,236},{212,178,236},{236,174,236},{236,174,212},{236,180,176},{228,196,144},
        {204,210,120},{180,222,120},{168,226,144},{152,226,180},{160,214,228},{160,162,160},{0,0,0},{0,0,0}
    };
    int i;
    nes550d_save_palette();
    for (i = 0; i < 64; i++)
    {
        uint32_t color = nes550d_rgb_to_yuv(rgb[i][0], rgb[i][1], rgb[i][2]);
        EngDrvOut(LCD_Palette[i * 3], color);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], color);
    }
}

static void nes550d_clear_screen(void)
{
    uint8_t *vram = bmp_vram();
    int y;
    if (!vram)
        return;
    for (y = 0; y < NES550D_SCREEN_H; y++)
        memset(vram + y * BMPPITCH, COLOR_TRANSPARENT_GRAY,
               NES550D_SCREEN_W);
}

static void nes550d_draw_test_screen(void)
{
    uint8_t *vram = bmp_vram();
    int y;
    if (!vram)
        return;

    for (y = 0; y < NES550D_SCREEN_H; y++)
    {
        memset(vram + y * BMPPITCH, COLOR_BLACK, NES550D_SCREEN_W);
        memset(vram + y * BMPPITCH + NES550D_VIEW_X, COLOR_GRAY(20),
               NES550D_VIEW_W);
    }
    bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_GRAY(20)), 284, 105,
               "NES550D");
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_GRAY(20)), 155, 195,
               "ROM: %.44s", nes550d_requested_name);
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_GRAY(20)), 151, 250,
               "EMULATOR CORE NOT YET IMPLEMENTED");
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_GRAY(20)), 205, 330,
               "INPUT: %-22s", nes550d_last_input);
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_GRAY(20)), 190, 420,
               "Arrows D-pad | SET A | PLAY B | DISP Select");
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_GRAY(20)), 235, 445,
               "MENU Start | Q exit");
}

static void nes550d_draw_frame(void)
{
    const uint8_t *source = nes550d_core_framebuffer();
    uint8_t *vram = bmp_vram();
    static uint16_t expand[256];
    static int expand_ready;
    int y, x;
    if (!source || !vram)
        return;
    if (!expand_ready)
    {
        for (x = 0; x < 256; x++)
        {
            uint16_t color = (uint16_t)(x & 0x3f);
            expand[x] = (uint16_t)(color | (color << 8));
        }
        expand_ready = 1;
    }
    if (nes550d_config_video == 1 && !nes550d_stretch_xmap_ready)
    {
        for (x = 0; x < NES550D_SCREEN_W; x++)
            nes550d_stretch_xmap[x] =
                (uint16_t)((x * 256) / NES550D_SCREEN_W);
        nes550d_stretch_xmap_ready = 1;
    }
    for (y = 0; y < 240; y++)
    {
        uint8_t *row0 = vram + (y * 2) * BMPPITCH +
                        (nes550d_config_video == 1 ? 0 : NES550D_VIEW_X);
        uint8_t *row1 = row0 + BMPPITCH;
        if (nes550d_config_video == 1)
        {
            for (x = 0; x < NES550D_SCREEN_W; x++)
                row0[x] = source[y * 256 + nes550d_stretch_xmap[x]] & 0x3f;
            memcpy(row1, row0, NES550D_SCREEN_W);
        }
        else
        {
            const uint8_t *src = source + y * 256;
            uint32_t *out = (uint32_t *)row0;
            for (x = 0; x < 128; x++)
                out[x] = (uint32_t)expand[src[x * 2]] |
                         ((uint32_t)expand[src[x * 2 + 1]] << 16);
            memcpy(row1, row0, NES550D_VIEW_W);
        }
    }
}

static void nes550d_copy_idle_to_real(void)
{
    uint8_t *dst = bmp_vram_real();
    uint8_t *src = bmp_vram_idle();
    int width = nes550d_config_video == 1 ? NES550D_SCREEN_W : NES550D_VIEW_W;
    int x = nes550d_config_video == 1 ? 0 : NES550D_VIEW_X;
    int y;
    if (!dst || !src)
        return;
    for (y = 0; y < 480; y++)
        memcpy(dst + y * BMPPITCH + x, src + y * BMPPITCH + x, width);
}

static void nes550d_prepare_game_screen(void)
{
    uint8_t *vram = bmp_vram();
    int y;
    if (!vram) return;
    for (y = 0; y < NES550D_SCREEN_H; y++)
        memset(vram + y * BMPPITCH, 0x0d, NES550D_SCREEN_W);
}

static void nes550d_queue_input(enum nes550d_button button, int pressed)
{
    unsigned int next;
    if (button >= NES550D_BUTTON_COUNT)
        return;
    pressed = pressed ? 1 : 0;
    if (nes550d_button_state[button] == pressed)
        return;
    next = (nes550d_queue_write + 1) % NES550D_QUEUE_SIZE;
    if (next == nes550d_queue_read)
    {
        if (pressed)
            return;
        nes550d_queue_read = (nes550d_queue_read + 1) % NES550D_QUEUE_SIZE;
    }
    nes550d_button_state[button] = pressed;
    nes550d_queue[nes550d_queue_write].button = button;
    nes550d_queue[nes550d_queue_write].pressed = pressed;
    nes550d_queue_write = next;
}

static void nes550d_pulse_input(enum nes550d_button button,
                                uint32_t duration_ms)
{
    nes550d_queue_input(button, 1);
    nes550d_release_at[button] = (uint32_t)get_ms_clock() + duration_ms;
}

#if defined(NES_TARGET_6D)
/*
 * The 6D sends one combined UNPRESS_UDLR event (0x36) instead of four
 * per-direction releases.  Keep all directions pressed until that event, and
 * support the 6D's eight-direction (diagonal) press codes.
 */
static void nes550d_release_directions(void)
{
    nes550d_queue_input(NES550D_BUTTON_UP, 0);
    nes550d_queue_input(NES550D_BUTTON_DOWN, 0);
    nes550d_queue_input(NES550D_BUTTON_LEFT, 0);
    nes550d_queue_input(NES550D_BUTTON_RIGHT, 0);
    nes550d_dir_mask = 0;
}

static void nes550d_set_directions(unsigned char mask)
{
    if (mask == nes550d_dir_mask)
        return;

    nes550d_release_directions();
    if (mask & 0x01) nes550d_queue_input(NES550D_BUTTON_UP, 1);
    if (mask & 0x02) nes550d_queue_input(NES550D_BUTTON_DOWN, 1);
    if (mask & 0x04) nes550d_queue_input(NES550D_BUTTON_LEFT, 1);
    if (mask & 0x08) nes550d_queue_input(NES550D_BUTTON_RIGHT, 1);
    nes550d_dir_mask = mask;
}
#endif

static void nes550d_release_all(void)
{
    int i;
    for (i = 0; i < NES550D_BUTTON_COUNT; i++)
    {
        nes550d_release_at[i] = 0;
        nes550d_queue_input((enum nes550d_button)i, 0);
    }
#if defined(NES_TARGET_6D)
    nes550d_dir_mask = 0;
#endif
}

static const char *nes550d_input_name(enum nes550d_button button, int pressed)
{
    static const char *names[NES550D_BUTTON_COUNT] =
    {
        "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT",
        "A", "B", "SELECT", "START"
    };
    static char text[32];
    snprintf(text, sizeof(text), "%s %s", names[button],
             pressed ? "PRESSED" : "RELEASED");
    return text;
}

static void nes550d_process_input(void)
{
    uint32_t now = (uint32_t)get_ms_clock();
    int i;
    for (i = 0; i < NES550D_BUTTON_COUNT; i++)
        if (nes550d_release_at[i] &&
            (int32_t)(now - nes550d_release_at[i]) >= 0)
        {
            nes550d_release_at[i] = 0;
            nes550d_queue_input((enum nes550d_button)i, 0);
        }

    while (nes550d_queue_read != nes550d_queue_write)
    {
        struct nes550d_input_event event =
            nes550d_queue[nes550d_queue_read];
        nes550d_queue_read = (nes550d_queue_read + 1) % NES550D_QUEUE_SIZE;
        nes550d_core_set_button((enum nes550d_button)event.button,
                                event.pressed);
        if (event.button == NES550D_BUTTON_START && event.pressed)
        {
            nes550d_profile.start_presses++;
            nes550d_profile.last_start_frame = nes550d_profile.frames;
            nes550d_profile.last_start_pc = nes550d_core_debug_pc();
            nes550d_profile.last_start_mode = nes550d_core_debug_ram(0x59);
        }
        snprintf(nes550d_last_input, sizeof(nes550d_last_input), "%s",
                 nes550d_input_name((enum nes550d_button)event.button,
                                    event.pressed));
    }
}

static void nes550d_reset_input_queue(void)
{
    nes550d_queue_read = nes550d_queue_write = 0;
    memset(nes550d_queue, 0, sizeof(nes550d_queue));
    memset(nes550d_button_state, 0, sizeof(nes550d_button_state));
    memset(nes550d_release_at, 0, sizeof(nes550d_release_at));
#if defined(NES_TARGET_6D)
    nes550d_dir_mask = 0;
#endif
}

static void nes550d_scan_roms(void)
{
    nes550d_rom_scan(&nes550d_roms);
    nes550d_roms_scanned = 1;
}

static int nes550d_selected_index(void)
{
    return nes550d_rom_find_id(&nes550d_roms,
                               (unsigned int)nes550d_config_rom_id);
}

static void nes550d_show_message(const char *title, const char *detail)
{
    menu_redraw_blocked = 1;
    clrscr();
    bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK), 100, 160,
               "%s", title);
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 100, 235,
               "%s", detail);
    msleep(3500);
    clrscr();
    menu_redraw_blocked = 0;
}

static uint8_t *nes550d_load_rom(const char *path, unsigned int *size_out)
{
    uint32_t size;
    uint32_t offset = 0;
    uint8_t *data;
    FILE *file;
    /* Leave room for larger licensed/homebrew cartridges and future mappers.
     * Mapper compatibility is checked separately by the emulator core. */
    if (FIO_GetFileSize(path, &size) != 0 || size < 16 || size > 1048576)
        return 0;
    data = malloc(size);
    if (!data)
        return 0;
    file = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!file)
    {
        free(data);
        return 0;
    }
    while (offset < size)
    {
        uint32_t remaining = size - offset;
        uint32_t chunk = remaining > 8192 ? 8192 : remaining;
        if (FIO_ReadFile(file, data + offset, chunk) != (int)chunk)
        {
            FIO_CloseFile(file);
            free(data);
            return 0;
        }
        offset += chunk;
    }
    FIO_CloseFile(file);
    *size_out = size;
    return data;
}

static void nes550d_task(void *arg)
{
    struct nes550d_core_options options;
    uint8_t *rom_data;
    unsigned int rom_size;
    uint32_t next_frame;
    uint32_t last_render_frame;
    uint32_t measured;
    uint32_t frame_started;
    const char *runtime_error = 0;
    char startup_line[256];
    int audio_started = 0;
    int video_started = 0;
    (void)arg;

    memset(&nes550d_profile, 0, sizeof(nes550d_profile));
    nes550d_raw_trace_count = 0;
    nes550d_raw_trace_dropped = 0;
    nes550d_stop_key = 0;
    nes550d_set_session_log_paths(nes550d_requested_name);
    nes550d_log_reset();
    nes550d_log_checkpoint("task-enter");

    if (!nes550d_requested_rom[0])
    {
        nes550d_log("ERROR no valid ROM selected from ML/NES/\n");
        nes550d_show_message(
            nes550d_roms.directory_found ? "No valid NES ROM found" :
                                           "ML/NES directory missing",
            "Copy an authorized .nes ROM to ML/NES/");
        nes550d_task_active = 0;
        return;
    }
    if (!nes550d_rom_validate_ines(nes550d_requested_rom))
    {
        nes550d_log("ERROR selected ROM missing or invalid\n");
        nes550d_show_message("Selected ROM is missing or invalid",
                            nes550d_requested_name);
        nes550d_task_active = 0;
        return;
    }

    options.video_mode = nes550d_config_video;
    options.audio_enabled = nes550d_config_audio;
    snprintf(startup_line, sizeof(startup_line),
        "ROM name=%s path=%s video=%d audio=%d debug=%d\n",
        nes550d_requested_name, nes550d_requested_rom,
        options.video_mode, options.audio_enabled, nes550d_config_debug);
    nes550d_log(startup_line);
    nes550d_log_checkpoint("rom-load-before");
    rom_data = nes550d_load_rom(nes550d_requested_rom, &rom_size);
    if (!rom_data)
    {
        nes550d_log("ERROR ROM allocation/read failed\n");
        nes550d_show_message("Could not load NES ROM", nes550d_requested_name);
        nes550d_task_active = 0;
        return;
    }
    snprintf(startup_line, sizeof(startup_line),
        "ROM loaded bytes=%d prg_banks=%d chr_banks=%d flags6=0x%x "
        "flags7=0x%x\n", rom_size, rom_data[4], rom_data[5],
        rom_data[6], rom_data[7]);
    nes550d_log(startup_line);
    nes550d_log_checkpoint("core-init-before");
    if (nes550d_core_init(rom_data, rom_size, &options) != 0)
    {
        const char *error = nes550d_core_error();
        snprintf(startup_line, sizeof(startup_line),
            "ERROR core init: %s\n", error ? error : "unknown");
        nes550d_log(startup_line);
        free(rom_data);
        nes550d_show_message("NES core rejected ROM", error ? error : "Unknown error");
        nes550d_task_active = 0;
        return;
    }
    free(rom_data);
    nes550d_log_checkpoint("core-init-after");
    nes550d_install_palette();
    nes550d_bmp_vram0_original = bmp_vram_info[1].vram0;
    nes550d_bmp_vram2_original = bmp_vram_info[1].vram2;
    nes550d_bmp_flipped = 0;
    nes550d_config_buffer = 0;
    bmp_draw_to_idle(0);
    clrscr();
    nes550d_prepare_game_screen();
    menu_redraw_blocked = 1;
    nes550d_reset_input_queue();
    nes550d_raw_trace_count = 0;
    nes550d_raw_trace_dropped = 0;
    memset(&nes550d_profile, 0, sizeof(nes550d_profile));
    nes550d_audio_ml_reset_stats();
    snprintf(nes550d_last_input, sizeof(nes550d_last_input), "READY");
    if (options.audio_enabled)
        audio_started = nes550d_audio_ml_start();
    snprintf(startup_line, sizeof(startup_line),
        "AUDIO requested=%d started=%d\n",
        options.audio_enabled, audio_started);
    nes550d_log(startup_line);
    nes550d_running = 1;
    nes550d_log("NES550D emulator session started\n");
    nes550d_log_checkpoint("loop-enter");
    nes550d_profile.started_ms = (uint32_t)get_ms_clock();
    next_frame = nes550d_profile.started_ms + 17;
    last_render_frame = 0;
    nes550d_vsync_seen = 0;
    nes550d_last_vsync = nes550d_vsync_count;
    nes550d_frame_ready = 0;
    nes550d_pace_phase = 0;

    while (nes550d_module_alive && nes550d_running &&
           !ml_shutdown_requested)
    {
        frame_started = (uint32_t)get_ms_clock();
        nes550d_process_input();
        measured = (uint32_t)get_ms_clock();
        nes550d_core_run_cpu_frame();
        measured = (uint32_t)get_ms_clock() - measured;
        nes550d_profile_add(&nes550d_profile.cpu_total_ms,
                            &nes550d_profile.cpu_peak_ms, measured);
        {
            uint32_t render_now = (uint32_t)get_ms_clock();
            int on_time = (int32_t)(next_frame - render_now) >= 0;
            int ppu_ready = (nes550d_core_debug_ppu_mask() & 0x18) != 0;
            int forced = !video_started ||
                         nes550d_profile.frames - last_render_frame >=
                         NES550D_MAX_RENDER_GAP;
            unsigned int vsync_now = nes550d_vsync_count;
            int vsync_due = !nes550d_vsync_seen ||
                            vsync_now != nes550d_last_vsync;
            int render_due = 1;
#if defined(NES_TARGET_6D)
            if (NES550D_LEGACY_RENDER)
                render_due = !(nes550d_profile.frames & 3);
            else if (nes550d_vsync_seen)
                render_due = vsync_due;
#else
            render_due = !(nes550d_profile.frames & 3);
#endif
        if (ppu_ready &&
            ((render_due && on_time) || forced))
        {
            measured = (uint32_t)get_ms_clock();
            nes550d_core_render_frame();
            measured = (uint32_t)get_ms_clock() - measured;
            nes550d_profile_add(&nes550d_profile.ppu_total_ms,
                                &nes550d_profile.ppu_peak_ms, measured);
            measured = (uint32_t)get_ms_clock();
            BMP_LOCK(
                if (!NES550D_LEGACY_RENDER &&
                    nes550d_config_buffer == 1 && nes550d_frame_ready)
                    nes550d_copy_idle_to_real();
                nes550d_draw_frame();
                if (!NES550D_LEGACY_RENDER &&
                    nes550d_config_buffer == 2)
                {
                    uint8_t *front = bmp_vram_info[1].vram0;
                    uint8_t *back = bmp_vram_info[1].vram2;
                    bmp_vram_info[1].vram0 = back;
                    bmp_vram_info[1].vram2 = front;
                    nes550d_bmp_flipped = !nes550d_bmp_flipped;
                }
            )
            measured = (uint32_t)get_ms_clock() - measured;
            nes550d_profile_add(&nes550d_profile.display_total_ms,
                                &nes550d_profile.display_peak_ms, measured);
            nes550d_profile.rendered_frames++;
            nes550d_frame_ready = 1;
            last_render_frame = nes550d_profile.frames;
            video_started = 1;
            if (vsync_now)
            {
                nes550d_vsync_seen = 1;
                nes550d_last_vsync = vsync_now;
            }
            if (forced && !on_time) nes550d_profile.forced_renders++;
        }
        else
            nes550d_profile.skipped_renders++;
        }
        runtime_error = nes550d_core_error();
        if (runtime_error)
        {
            nes550d_log("NES550D core stopped on an unsupported operation\n");
            break;
        }
        nes550d_profile.frames++;
        if ((uint32_t)get_ms_clock() - frame_started > 17)
            nes550d_profile.late_frames++;
        measured = (uint32_t)get_ms_clock();
        if ((int32_t)(next_frame - measured) > 0)
            msleep(next_frame - measured);
        else
        {
            uint32_t late = measured - next_frame;
            if (late > nes550d_profile.max_late_ms)
                nes550d_profile.max_late_ms = late;
            if (late > 34)
            {
                next_frame = measured;
                nes550d_profile.clock_resyncs++;
            }
        }
        next_frame += (nes550d_pace_phase == 0) ? 16 : 17;
        nes550d_pace_phase = (nes550d_pace_phase + 1) % 3;
        if (nes550d_profile.frames == 1 || nes550d_profile.frames == 10 ||
            nes550d_profile.frames == 60 ||
            (nes550d_profile.frames % 300) == 0)
            nes550d_log_checkpoint("frame-progress");
    }

    if (runtime_error)
    {
        snprintf(startup_line, sizeof(startup_line),
            "ERROR runtime core: %s\n", runtime_error);
        nes550d_log(startup_line);
    }
    snprintf(startup_line, sizeof(startup_line),
        "STOP reason=%s key=0x%x module=%d shutdown=%d running=%d\n",
        runtime_error ? "core-error" :
        ml_shutdown_requested ? "camera-shutdown" :
        !nes550d_module_alive ? "module-deinit" :
        nes550d_stop_key == 0x0f ? "q-key" : "running-cleared",
        (unsigned int)nes550d_stop_key, (int)nes550d_module_alive,
        (int)ml_shutdown_requested, (int)nes550d_running);
    nes550d_log(startup_line);
    nes550d_log_checkpoint("loop-leave");
    bmp_draw_to_idle(0);
    if (nes550d_bmp_vram0_original)
        bmp_vram_info[1].vram0 = nes550d_bmp_vram0_original;
    if (nes550d_bmp_vram2_original)
        bmp_vram_info[1].vram2 = nes550d_bmp_vram2_original;
    nes550d_bmp_flipped = 0;
    nes550d_frame_ready = 0;
    nes550d_profile_dump();
    nes550d_release_all();
    nes550d_process_input();
    nes550d_raw_trace_dump();
    nes550d_audio_ml_shutdown();
    nes550d_log_checkpoint("audio-stopped");
    snprintf(startup_line, sizeof(startup_line),
        "APU DMC control=0x%x sample_address=0x%x sample_length=%d "
        "remaining=%d output=%d\n",
        nes550d_core_debug_dmc_control(), nes550d_core_debug_dmc_address(),
        nes550d_core_debug_dmc_length(), nes550d_core_debug_dmc_remaining(),
        nes550d_core_debug_dmc_output());
    nes550d_log(startup_line);
    nes550d_core_shutdown();
    nes550d_reset_input_queue();
    nes550d_clear_screen();
    nes550d_restore_palette();
    clrscr();
    menu_redraw_blocked = 0;
    nes550d_running = 0;
    nes550d_task_active = 0;
    nes550d_log("NES550D emulator session stopped\n");
    nes550d_log_checkpoint("task-exit");
    if (runtime_error)
        NotifyBox(5000, "NES core error: %s", runtime_error);
}

static MENU_SELECT_FUNC(nes550d_start)
{
    int index;
    if (nes550d_task_active)
        return;
    nes550d_scan_roms();
    index = nes550d_selected_index();
    nes550d_requested_rom[0] = '\0';
    nes550d_requested_name[0] = '\0';
    if (index >= 0)
    {
        snprintf(nes550d_requested_name, sizeof(nes550d_requested_name),
                 "%s", nes550d_roms.names[index]);
        nes550d_rom_make_path(nes550d_requested_rom,
                              sizeof(nes550d_requested_rom),
                              nes550d_requested_name);
    }
    nes550d_task_active = 1;
    task_create("nes550d_task", 0x1c, 0x10000, nes550d_task, 0);
}

static MENU_UPDATE_FUNC(nes550d_rom_update)
{
    int index;
    if (!nes550d_roms_scanned)
        nes550d_scan_roms();
    if (!nes550d_roms.directory_found)
        MENU_SET_VALUE("ML/NES missing");
    else if (!nes550d_roms.count)
        MENU_SET_VALUE("No valid ROMs");
    else if (!nes550d_config_rom_id)
        MENU_SET_VALUE("Auto (%s)", nes550d_roms.names[0]);
    else if ((index = nes550d_selected_index()) >= 0)
        MENU_SET_VALUE("%s", nes550d_roms.names[index]);
    else
        MENU_SET_VALUE("Missing selection");
}

static MENU_SELECT_FUNC(nes550d_rom_select)
{
    int current;
    int position;
    int choices;
    if (!nes550d_roms_scanned)
        nes550d_scan_roms();
    if (!delta || !nes550d_roms.count)
        return;
    current = nes550d_selected_index();
    position = nes550d_config_rom_id && current >= 0 ? current + 1 : 0;
    choices = nes550d_roms.count + 1;
    position = (position + delta) % choices;
    if (position < 0)
        position += choices;
    nes550d_config_rom_id = position == 0 ? 0 :
        (int)nes550d_rom_name_id(nes550d_roms.names[position - 1]);
}

static MENU_SELECT_FUNC(nes550d_reset_settings)
{
    if (nes550d_task_active)
    {
        NotifyBox(3000, "Stop NES before resetting settings");
        return;
    }
    nes550d_config_reset();
    NotifyBox(2500, "NES settings reset");
}

static MENU_SELECT_FUNC(nes550d_logs_clear)
{
    char paths[NES550D_MAX_LOG_FILES][FIO_MAX_PATH_LENGTH];
    struct fio_file *file;
    struct fio_dirent *dirent;
    int found = 0;
    int deleted = 0;
    int failed = 0;
    int i;

    if (nes550d_task_active || nes550d_running)
    {
        NotifyBox(3000, "Stop NES before clearing logs");
        return;
    }
    file = alloc_fio_file();
    if (!file)
    {
        NotifyBox(3000, "Cannot scan ML/LOGS");
        return;
    }
    dirent = FIO_FindFirstEx("ML/LOGS/", file);
    if (!IS_ERROR(dirent))
    {
        do
        {
            struct file_info info = convert_fio_file_info(file);
            int length;
            if (!info.name[0] || (info.mode & ATTR_DIRECTORY) ||
                !nes550d_is_log_name(info.name))
                continue;
            length = snprintf(paths[found], sizeof(paths[found]),
                              "ML/LOGS/%s", info.name);
            if (length < 0 || length >= (int)sizeof(paths[found]))
                continue;
            found++;
        }
        while (found < NES550D_MAX_LOG_FILES &&
               FIO_FindNextEx(dirent, file) == 0);
        FIO_FindClose(dirent);
    }
    free(file);

    for (i = 0; i < found; i++)
        if (FIO_RemoveFile(paths[i]) == 0)
            deleted++;
        else
            failed++;
    nes550d_raw_trace_count = 0;
    if (!found)
        NotifyBox(3000, "No NES logs found");
    else if (failed)
        NotifyBox(4000, "NES logs: %d deleted, %d failed", deleted, failed);
    else
        NotifyBox(3000, "%d NES log%s deleted", deleted,
                  deleted == 1 ? "" : "s");
}

static struct menu_entry nes550d_menu[] =
{
    {
        .name = "NES",
        .select = nes550d_start,
        .help = "Run NES550D with the selected ROM from ML/NES/.",
        .children = (struct menu_entry[])
        {
            {
                .name = "Start NES",
                .select = nes550d_start,
                .help = "Open the NES550D test screen with the selected ROM.",
                .help2 = "Press Q to exit and restore camera display and input.",
            },
            {
                .name = "ROM",
                .priv = &nes550d_config_rom_id,
                .select = nes550d_rom_select,
                .update = nes550d_rom_update,
                .help = "Choose Auto or a valid iNES ROM from ML/NES/.",
                .help2 = "Up to 32 .nes files are sorted alphabetically.",
            },
            {
                .name = "Video",
                .priv = &nes550d_config_video,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "Exact 2x", "Stretch fullscreen" },
                .help = "Choose exact 2x or fill the 720x480 display.",
                .help2 = "Fullscreen stretches 256x240 to 720x480.",
            },
            {
                .name = "Audio",
                .priv = &nes550d_config_audio,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Enable NES APU audio.",
                .help2 = "Turn OFF for a silent, lower-overhead session.",
            },
            {
                .name = "Volume",
                .priv = &nes550d_config_volume,
                .min = 1,
                .max = ASIF_MAX_VOL,
                .help = "NES audio output volume.",
                .help2 = "1 = quiet, ASIF_MAX_VOL (10 on 6D) = loudest.",
            },
            {
                .name = "Debug logging",
                .priv = &nes550d_config_debug,
                .min = 0, .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Write session diagnostics to ML/LOGS.",
                .help2 = "No logging or card I/O occurs in button callbacks.",
            },
            {
                .name = "Clear NES logs",
                .select = nes550d_logs_clear,
                .help = "Delete only ML/LOGS/NES*.LOG files.",
                .help2 = "ROMs and persistent configuration are never removed.",
            },
            {
                .name = "Reset settings",
                .select = nes550d_reset_settings,
                .help = "Restore NES ROM, video, audio and debug defaults.",
                .help2 = "ROM files are never changed or removed.",
            },
            MENU_EOL,
        },
    }
};

#if defined(NES_TARGET_6D)
/* Canon EOS 6D raw GUI event values (platform/6D.116/gui.h). */
#define NES550D_PRESS_UP       0x2e
#define NES550D_PRESS_UP_RIGHT 0x2f
#define NES550D_PRESS_RIGHT    0x30
#define NES550D_PRESS_DOWN_RIGHT 0x31
#define NES550D_PRESS_DOWN     0x32
#define NES550D_PRESS_LEFT     0x34
#define NES550D_PRESS_UP_LEFT  0x35
#define NES550D_PRESS_DOWN_LEFT 0x1c
#define NES550D_UNPRESS_UDLR   0x36
#define NES550D_PRESS_SET      0x04
#define NES550D_RELEASE_SET    0x05
#define NES550D_MENU           0x06
#define NES550D_INFO           0x07
#define NES550D_PLAY           0x0b
#define NES550D_PRESS_ZOOM_IN  0x12
#define NES550D_UNPRESS_ZOOM_IN 0x13
#define NES550D_Q              0x1d
#else
/* Canon EOS 550D firmware 1.0.9 raw GUI event values. */
#define NES550D_PRESS_RIGHT   0x1a
#define NES550D_RELEASE_RIGHT 0x1b
#define NES550D_PRESS_LEFT    0x1c
#define NES550D_RELEASE_LEFT  0x1d
#define NES550D_PRESS_UP      0x1e
#define NES550D_RELEASE_UP    0x1f
#define NES550D_PRESS_DOWN    0x20
#define NES550D_RELEASE_DOWN  0x21
#define NES550D_PRESS_SET     0x04
#define NES550D_RELEASE_SET   0x05
#define NES550D_MENU          0x06
#define NES550D_INFO          0x07
#define NES550D_PLAY          0x09
#define NES550D_Q             0x0f
#endif

static unsigned int nes550d_keypress_raw(unsigned int context)
{
    struct event *event = (struct event *)(uintptr_t)context;
    unsigned int key;
    if (!nes550d_running || !event)
        return 1;
    nes550d_raw_trace_add(event);
    if (event->type != 0)
        return 1;
    key = event->param;
#if defined(NES_TARGET_6D)
    switch (key)
    {
        case NES550D_PRESS_UP:
            nes550d_set_directions(0x01);
            return 0;
        case NES550D_PRESS_DOWN:
            nes550d_set_directions(0x02);
            return 0;
        case NES550D_PRESS_LEFT:
            nes550d_set_directions(0x04);
            return 0;
        case NES550D_PRESS_RIGHT:
            nes550d_set_directions(0x08);
            return 0;
        case NES550D_PRESS_UP_LEFT:
            nes550d_set_directions(0x01 | 0x04);
            return 0;
        case NES550D_PRESS_UP_RIGHT:
            nes550d_set_directions(0x01 | 0x08);
            return 0;
        case NES550D_PRESS_DOWN_LEFT:
            nes550d_set_directions(0x02 | 0x04);
            return 0;
        case NES550D_PRESS_DOWN_RIGHT:
            nes550d_set_directions(0x02 | 0x08);
            return 0;
        case NES550D_UNPRESS_UDLR:
            nes550d_release_directions();
            return 0;
        case NES550D_PRESS_SET:
            nes550d_queue_input(NES550D_BUTTON_A, 1);
            return 0;
        case NES550D_RELEASE_SET:
            nes550d_queue_input(NES550D_BUTTON_A, 0);
            return 0;
        case NES550D_PLAY:
            /* Do not let the short pulse release a B held on the magnify
             * button. */
            if (!nes550d_button_state[NES550D_BUTTON_B])
                nes550d_pulse_input(NES550D_BUTTON_B, 140);
            return 0;
        case NES550D_PRESS_ZOOM_IN:
            /* 6D has no dedicated B button; hold magnify for B (run/fire). */
            nes550d_release_at[NES550D_BUTTON_B] = 0;
            nes550d_queue_input(NES550D_BUTTON_B, 1);
            return 0;
        case NES550D_UNPRESS_ZOOM_IN:
            nes550d_queue_input(NES550D_BUTTON_B, 0);
            return 0;
        case NES550D_INFO:
            nes550d_pulse_input(NES550D_BUTTON_SELECT, 300);
            return 0;
        case NES550D_MENU:
            nes550d_pulse_input(NES550D_BUTTON_START, 400);
            return 0;
        case NES550D_Q:
            nes550d_stop_key = key;
            nes550d_running = 0;
            return 0;
        default:
            return 1;
    }
#else
    switch (key)
    {
        case NES550D_PRESS_UP: nes550d_queue_input(NES550D_BUTTON_UP, 1); return 0;
        case NES550D_RELEASE_UP: nes550d_queue_input(NES550D_BUTTON_UP, 0); return 0;
        case NES550D_PRESS_DOWN: nes550d_queue_input(NES550D_BUTTON_DOWN, 1); return 0;
        case NES550D_RELEASE_DOWN: nes550d_queue_input(NES550D_BUTTON_DOWN, 0); return 0;
        case NES550D_PRESS_LEFT: nes550d_queue_input(NES550D_BUTTON_LEFT, 1); return 0;
        case NES550D_RELEASE_LEFT: nes550d_queue_input(NES550D_BUTTON_LEFT, 0); return 0;
        case NES550D_PRESS_RIGHT: nes550d_queue_input(NES550D_BUTTON_RIGHT, 1); return 0;
        case NES550D_RELEASE_RIGHT: nes550d_queue_input(NES550D_BUTTON_RIGHT, 0); return 0;
        case NES550D_PRESS_SET: nes550d_queue_input(NES550D_BUTTON_A, 1); return 0;
        case NES550D_RELEASE_SET: nes550d_queue_input(NES550D_BUTTON_A, 0); return 0;
        case NES550D_PLAY: nes550d_pulse_input(NES550D_BUTTON_B, 140); return 0;
        case NES550D_INFO: nes550d_pulse_input(NES550D_BUTTON_SELECT, 300); return 0;
        case NES550D_MENU: nes550d_pulse_input(NES550D_BUTTON_START, 400); return 0;
        case NES550D_Q:
            nes550d_stop_key = key;
            nes550d_running = 0;
            return 0;
        default: return 1;
    }
#endif
}

static unsigned int nes550d_vsync_cbr(unsigned int context)
{
    (void)context;
    nes550d_vsync_count++;
    return 0;
}

static unsigned int nes550d_init(void)
{
    nes550d_module_alive = 1;
    nes550d_running = 0;
    nes550d_task_active = 0;
    nes550d_roms_scanned = 0;
    menu_add("Games", nes550d_menu, COUNT(nes550d_menu));
#if defined(NES_TARGET_6D)
    printf("nes550d: NES menu registered (6D keymap)\n");
#else
    printf("nes550d: NES menu registered (550D.109 only)\n");
#endif
    return 0;
}

static unsigned int nes550d_deinit(void)
{
    nes550d_module_alive = 0;
    nes550d_running = 0;
    nes550d_audio_ml_shutdown();
    msleep(100);
    nes550d_release_all();
    nes550d_reset_input_queue();
    nes550d_core_shutdown();
    nes550d_clear_screen();
    nes550d_restore_palette();
    menu_redraw_blocked = 0;
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(nes550d_init)
    MODULE_DEINIT(nes550d_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS_RAW, nes550d_keypress_raw, 0)
    MODULE_CBR(CBR_VSYNC, nes550d_vsync_cbr, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(nes550d_config_rom_id)
    MODULE_CONFIG(nes550d_config_video)
    MODULE_CONFIG(nes550d_config_audio)
    MODULE_CONFIG(nes550d_config_debug)
MODULE_CONFIGS_END()
