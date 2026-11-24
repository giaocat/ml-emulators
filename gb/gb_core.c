#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "gb_core.h"
#include "walnut/minigb_apu.h"

extern void *__mem_malloc(size_t len, unsigned int flags,
                          const char *file, unsigned int line);
extern void __mem_free(void *buf);

struct gb_alloc_header
{
    void *raw;
    size_t size;
};

void *malloc(size_t size)
{
    struct gb_alloc_header *h;
    unsigned char *raw;
    uintptr_t aligned;

    if (!size) size = 1;
    raw = (unsigned char *)
        __mem_malloc(size + sizeof(*h) + 7, 0, __FILE__, __LINE__);
    if (!raw) return NULL;
    aligned = ((uintptr_t)raw + sizeof(*h) + 7) & ~(uintptr_t)7;
    h = ((struct gb_alloc_header *)aligned) - 1;
    h->raw = raw;
    h->size = size;
    return (void *)aligned;
}

void free(void *ptr)
{
    if (ptr)
        __mem_free(((struct gb_alloc_header *)ptr) - 1);
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    struct gb_alloc_header *h;
    void *p;
    size_t old;

    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return NULL; }
    h = ((struct gb_alloc_header *)ptr) - 1;
    old = h->size;
    p = malloc(size);
    if (!p) return NULL;
    memcpy(p, ptr, old < size ? old : size);
    free(ptr);
    return p;
}

void *memchr(const void *ptr, int value, size_t size)
{
    const unsigned char *p = (const unsigned char *)ptr;
    while (size--)
    {
        if (*p == (unsigned char)value)
            return (void *)p;
        p++;
    }
    return NULL;
}

#define GB_AUDIO_RING 8192
#define GB_CART_RAM_SIZE (128 * 1024)

static struct gb_s *gb;
static struct minigb_apu_ctx apu;
static uint8_t *rom_data;
static size_t rom_size;
static uint8_t cart_ram[GB_CART_RAM_SIZE];
static uint8_t framebuffer[160 * 144];
static uint8_t cgb_palette332[64];
static int16_t audio_ring[GB_AUDIO_RING];
static volatile unsigned ring_write;
static volatile unsigned ring_read;
static unsigned audio_underruns;
static unsigned audio_overflows;
static int16_t audio_frame[AUDIO_SAMPLES_TOTAL];
static const char *error_text;
static unsigned frame_count;
static int audio_enabled;
static int is_cgb;

static uint8_t gb_audio_read(uint16_t addr)
{
    return minigb_apu_audio_read(&apu, addr);
}

static void gb_audio_write(uint16_t addr, uint8_t val)
{
    minigb_apu_audio_write(&apu, addr, val);
}

#define audio_read gb_audio_read
#define audio_write gb_audio_write
#include "walnut/walnut_cgb.h"

static uint8_t rom_read(struct gb_s *unused, const uint_fast32_t addr)
{
    (void)unused;
    return addr < rom_size ? rom_data[addr] : 0xff;
}

static uint16_t rom_read16(struct gb_s *unused, const uint_fast32_t addr)
{
    return (uint16_t)(rom_read(unused, addr) |
                      (rom_read(unused, addr + 1) << 8));
}

static uint32_t rom_read32(struct gb_s *unused, const uint_fast32_t addr)
{
    return (uint32_t)rom_read(unused, addr) |
           ((uint32_t)rom_read(unused, addr + 1) << 8) |
           ((uint32_t)rom_read(unused, addr + 2) << 16) |
           ((uint32_t)rom_read(unused, addr + 3) << 24);
}

static uint8_t cart_read(struct gb_s *unused, const uint_fast32_t addr)
{
    (void)unused;
    return addr < GB_CART_RAM_SIZE ? cart_ram[addr] : 0xff;
}

static void cart_write(struct gb_s *unused, const uint_fast32_t addr,
                       const uint8_t value)
{
    (void)unused;
    if (addr < GB_CART_RAM_SIZE)
        cart_ram[addr] = value;
}

static void gb_error(struct gb_s *unused, const enum gb_error_e code,
                     const uint16_t addr)
{
    (void)unused; (void)code; (void)addr;
    error_text = "GB core error";
}

static void lcd_draw_line(struct gb_s *context, const uint8_t *pixels,
                          const uint_fast8_t line)
{
    uint8_t *out = framebuffer + line * 160;
    int x;

    if (context->cgb.cgbMode)
    {
        for (x = 0; x < 160; x++)
            out[x] = cgb_palette332[pixels[x] & 0x3f];
    }
    else
    {
        /* Game Boy green, mapped to RGB332. */
        static const uint8_t shades[4] = { 0x94, 0x6a, 0x2c, 0x04 };
        for (x = 0; x < 160; x++)
            out[x] = shades[pixels[x] & 3];
    }
}

static void update_cgb_palette(void)
{
    int i;
    if (!gb || !gb->cgb.cgbMode)
        return;
    for (i = 0; i < 64; i++)
    {
        uint16_t color = gb->cgb.fixPalette[i];
        unsigned r = (color >> 11) & 0x1f;
        unsigned g = (color >> 5) & 0x3f;
        unsigned b = color & 0x1f;
        cgb_palette332[i] = (uint8_t)(((r >> 2) << 5) |
                                      ((g >> 3) << 2) |
                                      (b >> 3));
    }
}

static void audio_clear(void)
{
    ring_write = 0;
    ring_read = 0;
    audio_underruns = 0;
    audio_overflows = 0;
}

static void audio_push(int16_t sample)
{
    unsigned next = (ring_write + 1) % GB_AUDIO_RING;
    if (next == ring_read)
    {
        audio_overflows++;
        return;
    }
    audio_ring[ring_write] = sample;
    ring_write = next;
}

int gb550d_core_init(const uint8_t *data, unsigned int size,
                     const struct gb550d_core_options *options)
{
    size_t padded;
    enum gb_init_error_e result;
    int i;

    error_text = NULL;
    gb = NULL;
    rom_data = NULL;
    rom_size = 0;
    frame_count = 0;
    audio_enabled = options ? options->audio_enabled : 1;
    audio_clear();
    memset(cart_ram, 0, sizeof(cart_ram));

    if (!data || size < 0x150)
    {
        error_text = "ROM too small";
        return -1;
    }

    padded = (size + 0x7fff) & ~(size_t)0x7fff;
    rom_data = (uint8_t *)malloc(padded);
    if (!rom_data)
    {
        error_text = "Out of memory";
        return -1;
    }
    memset(rom_data, 0, padded);
    memcpy(rom_data, data, size);
    rom_size = padded;

    gb = (struct gb_s *)calloc(1, sizeof(struct gb_s));
    if (!gb)
    {
        error_text = "gb_s allocation failed";
        free(rom_data);
        rom_data = NULL;
        return -1;
    }

    minigb_apu_audio_init(&apu);
    result = gb_init(gb, rom_read, rom_read16, rom_read32,
                     cart_read, cart_write, gb_error, NULL);
    if (result != GB_INIT_NO_ERROR)
    {
        error_text = "gb_init failed";
        free(gb);
        gb = NULL;
        free(rom_data);
        rom_data = NULL;
        return -1;
    }

    gb_init_lcd(gb, lcd_draw_line);
    gb_reset(gb);
    gb->direct.joypad = 0xff;
    is_cgb = gb->cgb.cgbMode ? 1 : 0;

    /* The APU needs its register file cleared after reset too. */
    minigb_apu_audio_init(&apu);
    for (i = 0; i < 0x30; i++)
        minigb_apu_audio_write(&apu, (uint16_t)(0xff10 + i), 0);
    return 0;
}

void gb550d_core_set_frameskip(int enabled)
{
    if (gb)
        gb->direct.frame_skip = enabled ? true : false;
}

void gb550d_core_set_button(enum gb550d_button button, int pressed)
{    static uint8_t buttons;
    uint8_t mask;

    switch (button)
    {
        case GB550D_BUTTON_A: mask = JOYPAD_A; break;
        case GB550D_BUTTON_B: mask = JOYPAD_B; break;
        case GB550D_BUTTON_SELECT: mask = JOYPAD_SELECT; break;
        case GB550D_BUTTON_START: mask = JOYPAD_START; break;
        case GB550D_BUTTON_RIGHT: mask = JOYPAD_RIGHT; break;
        case GB550D_BUTTON_LEFT: mask = JOYPAD_LEFT; break;
        case GB550D_BUTTON_UP: mask = JOYPAD_UP; break;
        case GB550D_BUTTON_DOWN: mask = JOYPAD_DOWN; break;
        default: return;
    }
    if (pressed)
        buttons |= mask;
    else
        buttons &= (uint8_t)~mask;
    if (gb)
        gb->direct.joypad = (uint8_t)~buttons;
}

void gb550d_core_run_frame(void)
{
    int i;

    if (!gb)
        return;
    update_cgb_palette();
    gb_run_frame_dualfetch(gb);
    frame_count++;

    if (audio_enabled)
    {
        minigb_apu_audio_callback(&apu, audio_frame);
        for (i = 0; i < AUDIO_SAMPLES; i++)
        {
            int mixed = ((int)audio_frame[i * 2] +
                         (int)audio_frame[i * 2 + 1]) / 2;
            audio_push((int16_t)mixed);
        }
    }
}

const uint8_t *gb550d_core_framebuffer(void)
{
    return framebuffer;
}

void gb550d_core_render_audio(int16_t *samples, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; i++)
    {
        if (ring_read != ring_write)
        {
            samples[i] = audio_ring[ring_read];
            ring_read = (ring_read + 1) % GB_AUDIO_RING;
        }
        else
        {
            samples[i] = 0;
            audio_underruns++;
        }
    }
}

const char *gb550d_core_error(void)
{
    return error_text;
}

unsigned int gb550d_core_frame_count(void)
{
    return frame_count;
}

int gb550d_core_is_cgb(void)
{
    return is_cgb;
}

unsigned int gb550d_core_audio_underruns(void)
{
    return audio_underruns;
}

unsigned int gb550d_core_audio_overflows(void)
{
    return audio_overflows;
}

void gb550d_core_shutdown(void)
{
    if (gb)
    {
        free(gb);
        gb = NULL;
    }
    if (rom_data)
    {
        free(rom_data);
        rom_data = NULL;
    }
    rom_size = 0;
    frame_count = 0;
    audio_clear();
    error_text = NULL;
}

