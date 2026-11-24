/*
 * QuickNES adapter for NES6D.
 *
 * Keeps the small NES6D ML shell (menu, ROM list, display, input and audio)
 * but replaces its experimental CPU/PPU/APU with QuickNES (GPL-2.0).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <new>

#include "nes_core.h"
#include "Nes_Emu.h"
#include "abstract_file.h"

/* Magic Lantern heap. */
extern "C" void *__mem_malloc(size_t len, unsigned int flags,
                              const char *file, unsigned int line);
extern "C" void __mem_free(void *buf);

/*
 * QuickNES uses malloc/free/realloc and new/delete.  Route them to the ML
 * module heap.  A small header lets realloc know the old size.
 */
struct qnes_alloc_header
{
    void *raw;
    size_t size;
};

extern "C" void *malloc(size_t size)
{
    struct qnes_alloc_header *h;
    unsigned char *raw;
    uintptr_t aligned;

    if (!size)
        size = 1;

    /* ML's module heap is only guaranteed 4-byte aligned.  QuickNES objects
     * contain doubles, so hand back an 8-byte aligned pointer and remember
     * the raw heap pointer for free(). */
    raw = (unsigned char *)
        __mem_malloc(size + sizeof(*h) + 7, 0, __FILE__, __LINE__);
    if (!raw)
        return 0;

    aligned = ((uintptr_t)raw + sizeof(*h) + 7) & ~(uintptr_t)7;
    h = ((struct qnes_alloc_header *)aligned) - 1;
    h->raw = raw;
    h->size = size;
    return (void *)aligned;
}

extern "C" void free(void *ptr)
{
    if (ptr)
    {
        struct qnes_alloc_header *h =
            ((struct qnes_alloc_header *)ptr) - 1;
        __mem_free(h->raw);
    }
}

extern "C" void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

extern "C" void *realloc(void *ptr, size_t size)
{
    struct qnes_alloc_header *h;
    void *p;
    size_t old;

    if (!ptr)
        return malloc(size);
    if (!size)
    {
        free(ptr);
        return 0;
    }
    h = ((struct qnes_alloc_header *)ptr) - 1;
    old = h->size;
    p = malloc(size);
    if (!p)
        return 0;
    memcpy(p, ptr, old < size ? old : size);
    free(ptr);
    return p;
}

void *operator new(size_t size) { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }
void operator delete(void *ptr) noexcept { free(ptr); }
void operator delete[](void *ptr) noexcept { free(ptr); }
void operator delete(void *ptr, size_t) noexcept { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { free(ptr); }
extern "C" void __cxa_pure_virtual(void) { }

#define QNES_AUDIO_RING 8192
#define QNES_VIDEO_HEIGHT 260

static unsigned char g_emu_storage[sizeof(Nes_Emu)] __attribute__((aligned(8)));
static Nes_Emu *g_emu;
static uint8_t g_video[Nes_Emu::buffer_width * QNES_VIDEO_HEIGHT];
static uint8_t g_framebuffer[256 * 240];
static int16_t g_audio_ring[QNES_AUDIO_RING];
static volatile unsigned g_audio_write;
static volatile unsigned g_audio_read;
static short g_audio_frame[4096];
static unsigned g_pad;
static const char *g_error;
static unsigned g_frame_count;
static unsigned g_latch_count;

static void qnes_audio_push(short sample)
{
    unsigned next = (g_audio_write + 1) % QNES_AUDIO_RING;
    if (next == g_audio_read)
        return; /* overflow: drop the newest sample */
    g_audio_ring[g_audio_write] = sample;
    g_audio_write = next;
}

static void qnes_audio_clear(void)
{
    g_audio_write = 0;
    g_audio_read = 0;
}

extern "C" int nes6d_core_init(const uint8_t *rom_data,
                                 unsigned int rom_size,
                                 const struct nes6d_core_options *options)
{
    const char *err;
    (void)options;

    qnes_audio_clear();
    g_pad = 0;
    g_frame_count = 0;
    g_latch_count = 0;
    g_error = 0;

    if (!rom_data || rom_size < 16)
    {
        g_error = "ROM too small";
        return -1;
    }

    /* The ML module loader does not execute C++ .init_array constructors.
     * Construct QuickNES explicitly instead of relying on a global object. */
    if (!g_emu)
        g_emu = new (g_emu_storage) Nes_Emu();

    printf("qnes: sample rate\n");
    /* QuickNES connects the APU outputs in set_cart(); set the sample rate
     * before loading so the default buffer is in place. */
    err = g_emu->set_sample_rate(48000);
    if (err)
    {
        g_error = err;
        return -1;
    }

    printf("qnes: load rom\n");
    {
        Mem_File_Reader reader(rom_data, (long)rom_size);
        Auto_File_Reader auto_reader(reader);
        err = g_emu->load_ines(auto_reader);
    }
    if (err)
    {
        g_error = err;
        return -1;
    }

    printf("qnes: video\n");
    g_emu->set_palette_range(0, 64);
    g_emu->set_pixels(g_video, Nes_Emu::buffer_width);
    g_emu->reset(false);
    printf("qnes: ready\n");
    return 0;
}

extern "C" void nes6d_core_set_button(enum nes6d_button button,
                                        int pressed)
{
    static const unsigned bits[NES6D_BUTTON_COUNT] =
    {
        0x10, 0x20, 0x40, 0x80, 0x01, 0x02, 0x04, 0x08
    };
    if (button >= NES6D_BUTTON_COUNT)
        return;
    if (pressed)
        g_pad |= bits[button];
    else
        g_pad &= ~bits[button];
}

extern "C" void nes6d_core_run_cpu_frame(void)
{
    const char *err;
    long count;
    long i;

    err = g_emu->emulate_frame((int)(g_pad & 0xff), 0);
    if (err)
    {
        g_error = err;
        return;
    }

    count = g_emu->read_samples(g_audio_frame, 4096);
    for (i = 0; i < count; i++)
        qnes_audio_push(g_audio_frame[i]);

    g_frame_count++;
    g_latch_count += (unsigned)g_emu->frame().joypad_read_count;
}

extern "C" void nes6d_core_run_frame(void)
{
    nes6d_core_run_cpu_frame();
    nes6d_core_render_frame();
}

extern "C" void nes6d_core_render_frame(void)
{
    const Nes_Emu::frame_t &f = g_emu->frame();
    uint8_t palette_map[256];
    int y, x, i;

    if (!f.pixels)
        return;

    /* QuickNES pixels index frame.palette[], which holds the NES color
     * number; NES6D's palette is indexed by that number.  Build a small
     * byte map once instead of doing the lookup for every pixel. */
    for (i = 0; i < 256; i++)
        palette_map[i] = i < f.palette_size
                             ? (uint8_t)(f.palette[i] & 0x3f)
                             : 0;

    for (y = 0; y < 240; y++)
    {
        const uint8_t *src = f.pixels + y * f.pitch;
        uint8_t *dst = g_framebuffer + y * 256;
        uint32_t *dst32 = (uint32_t *)dst;
        for (x = 0; x < 64; x++)
        {
            uint32_t v = palette_map[src[x * 4]] |
                         ((uint32_t)palette_map[src[x * 4 + 1]] << 8) |
                         ((uint32_t)palette_map[src[x * 4 + 2]] << 16) |
                         ((uint32_t)palette_map[src[x * 4 + 3]] << 24);
            dst32[x] = v;
        }
    }
}

extern "C" const uint8_t *nes6d_core_framebuffer(void)
{
    return g_framebuffer;
}

extern "C" void nes6d_core_render_audio(int16_t *samples,
                                          unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; i++)
    {
        if (g_audio_read != g_audio_write)
        {
            samples[i] = g_audio_ring[g_audio_read];
            g_audio_read = (g_audio_read + 1) % QNES_AUDIO_RING;
        }
        else
        {
            samples[i] = 0;
        }
    }
}

extern "C" const char *nes6d_core_error(void)
{
    return g_error;
}

extern "C" unsigned int nes6d_core_controller_reads(void)
{
    return (unsigned int)g_emu->frame().joypad_read_count;
}

extern "C" unsigned int nes6d_core_controller_latches(void)
{
    return g_latch_count;
}

extern "C" unsigned int nes6d_core_last_latched_buttons(void)
{
    return g_pad;
}

extern "C" unsigned int nes6d_core_observed_buttons(void)
{
    return g_pad;
}

extern "C" unsigned int nes6d_core_state_checksum(void)
{
    const uint8_t *p = g_emu->low_mem();
    unsigned int i, sum = 0;
    for (i = 0; i < 0x800; i++)
        sum = sum * 31 + p[i];
    return sum;
}

extern "C" unsigned int nes6d_core_debug_ram(unsigned int address)
{
    return g_emu->low_mem()[address & 0x7ff];
}

extern "C" unsigned int nes6d_core_debug_pc(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_start_checks(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_return_address(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_outer_return_address(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_ppu_ctrl(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_ppu_mask(void)
{
    /* QuickNES does not expose PPU_MASK; 0x18 keeps NES6D's display
     * scheduler from treating the PPU as disabled. */
    return 0x18;
}

extern "C" unsigned int nes6d_core_debug_ppu_dirty(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_ppu_scroll(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_frame_scroll(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_mmc3_split(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_dmc_control(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_dmc_address(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_dmc_length(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_dmc_remaining(void)
{
    return 0;
}

extern "C" unsigned int nes6d_core_debug_dmc_output(void)
{
    return 0;
}

extern "C" void nes6d_core_reset(void)
{
    if (g_emu)
        g_emu->reset(false);
    g_error = 0;
    g_pad = 0;
    qnes_audio_clear();
}

extern "C" void nes6d_core_shutdown(void)
{
    if (g_emu)
    {
        g_emu->close();
        g_emu->~Nes_Emu();
        g_emu = 0;
    }
    g_pad = 0;
    g_error = 0;
    qnes_audio_clear();
}

