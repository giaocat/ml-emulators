#include <stdint.h>
#include <string.h>

#include <audio.h>
#include <arm-mcr.h>
#include <dryos.h>
#include <mem.h>

#include "gb_audio_ml.h"
#include "gb_core.h"
#include "gb_config.h"

#define GB6D_AUDIO_RATE 48000
#define GB6D_AUDIO_SAMPLES 512
#define GB6D_AUDIO_VOLUME 5
#ifdef ASIF_MAX_VOL
#define GB6D_AUDIO_VOLUME_MAX ASIF_MAX_VOL
#else
#define GB6D_AUDIO_VOLUME_MAX 10
#endif

extern void StartASIFDMADAC(void *, int, void *, int, void (*)(void *), void *);
extern void SetNextASIFDACBuffer(void *, int);
extern void StopASIFDMADAC(void (*)(void *), int);
extern void SetSamplingRate(int, int);
extern void PowerAudioOutput(void);
extern void SetAudioVolumeOut(int);
extern int beep_playing;

static int16_t GB6D_AUDIO_buffers[2][GB6D_AUDIO_SAMPLES];
static volatile int GB6D_AUDIO_running;
static int GB6D_AUDIO_next_buffer;
static volatile unsigned int GB6D_AUDIO_callbacks;
static volatile unsigned int GB6D_AUDIO_total_ms;
static volatile unsigned int GB6D_AUDIO_peak_ms;
static volatile unsigned int GB6D_AUDIO_misses;

static void GB6D_AUDIO_fill(int16_t *buffer)
{
    unsigned int started = get_ms_clock();
    unsigned int elapsed;
    gb6d_core_render_audio(buffer, GB6D_AUDIO_SAMPLES);
    elapsed = get_ms_clock() - started;
    GB6D_AUDIO_callbacks++;
    GB6D_AUDIO_total_ms += elapsed;
    if (elapsed > GB6D_AUDIO_peak_ms) GB6D_AUDIO_peak_ms = elapsed;
    if (elapsed > 10) GB6D_AUDIO_misses++;
}

static void GB6D_AUDIO_stopped(void *context)
{
    (void)context;
    GB6D_AUDIO_running = 0;
    beep_playing = 0;
    audio_configure(1);
}

static void GB6D_AUDIO_next(void *context)
{
    (void)context;
    if (!GB6D_AUDIO_running)
        return;
    GB6D_AUDIO_fill(GB6D_AUDIO_buffers[GB6D_AUDIO_next_buffer]);
    SetNextASIFDACBuffer(
        GB6D_AUDIO_buffers[GB6D_AUDIO_next_buffer],
        sizeof(GB6D_AUDIO_buffers[GB6D_AUDIO_next_buffer]));
    GB6D_AUDIO_next_buffer = !GB6D_AUDIO_next_buffer;
}

int GB6D_AUDIO_ml_start(void)
{
    if (GB6D_AUDIO_running)
        return 1;
    if (beep_playing)
    {
        printf("gb6d: Canon audio output is already in use\n");
        return 0;
    }
    GB6D_AUDIO_fill(GB6D_AUDIO_buffers[0]);
    GB6D_AUDIO_fill(GB6D_AUDIO_buffers[1]);
    GB6D_AUDIO_next_buffer = 0;
    GB6D_AUDIO_running = 1;
    beep_playing = 1;
    SetSamplingRate(GB6D_AUDIO_RATE, 1);
    MEM(0xC0920210) = 4;
    PowerAudioOutput();
    audio_configure(1);
    {
        int volume = gb6d_config_volume;
        if (volume < 1) volume = 1;
        if (volume > GB6D_AUDIO_VOLUME_MAX)
            volume = GB6D_AUDIO_VOLUME_MAX;
        SetAudioVolumeOut(volume);
    }
    StartASIFDMADAC(
        GB6D_AUDIO_buffers[0], sizeof(GB6D_AUDIO_buffers[0]),
        GB6D_AUDIO_buffers[1], sizeof(GB6D_AUDIO_buffers[1]),
        GB6D_AUDIO_next, 0);
    return 1;
}

void GB6D_AUDIO_ml_reset_stats(void)
{
    GB6D_AUDIO_callbacks = 0;
    GB6D_AUDIO_total_ms = 0;
    GB6D_AUDIO_peak_ms = 0;
    GB6D_AUDIO_misses = 0;
}

unsigned int GB6D_AUDIO_ml_callback_count(void)
{
    return GB6D_AUDIO_callbacks;
}

unsigned int GB6D_AUDIO_ml_render_total_ms(void)
{
    return GB6D_AUDIO_total_ms;
}

unsigned int GB6D_AUDIO_ml_render_peak_ms(void)
{
    return GB6D_AUDIO_peak_ms;
}

unsigned int GB6D_AUDIO_ml_deadline_misses(void)
{
    return GB6D_AUDIO_misses;
}

void GB6D_AUDIO_ml_shutdown(void)
{
    if (GB6D_AUDIO_running)
    {
        StopASIFDMADAC(GB6D_AUDIO_stopped, 0);
        while (GB6D_AUDIO_running)
            msleep(20);
    }
}


