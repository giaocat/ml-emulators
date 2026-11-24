#include <stdint.h>
#include <string.h>

#include <audio.h>
#include <arm-mcr.h>
#include <dryos.h>
#include <mem.h>

#include "nes_audio_ml.h"
#include "nes_core.h"
#include "nes_config.h"

#define NES550D_AUDIO_RATE 48000
#define NES550D_AUDIO_SAMPLES 512
#define NES550D_AUDIO_VOLUME 5
#ifdef ASIF_MAX_VOL
#define NES550D_AUDIO_VOLUME_MAX ASIF_MAX_VOL
#else
#define NES550D_AUDIO_VOLUME_MAX 10
#endif

extern void StartASIFDMADAC(void *, int, void *, int, void (*)(void *), void *);
extern void SetNextASIFDACBuffer(void *, int);
extern void StopASIFDMADAC(void (*)(void *), int);
extern void SetSamplingRate(int, int);
extern void PowerAudioOutput(void);
extern void SetAudioVolumeOut(int);
extern int beep_playing;

static int16_t nes550d_audio_buffers[2][NES550D_AUDIO_SAMPLES];
static volatile int nes550d_audio_running;
static int nes550d_audio_next_buffer;
static volatile unsigned int nes550d_audio_callbacks;
static volatile unsigned int nes550d_audio_total_ms;
static volatile unsigned int nes550d_audio_peak_ms;
static volatile unsigned int nes550d_audio_misses;

static void nes550d_audio_fill(int16_t *buffer)
{
    unsigned int started = get_ms_clock();
    unsigned int elapsed;
    nes550d_core_render_audio(buffer, NES550D_AUDIO_SAMPLES);
    elapsed = get_ms_clock() - started;
    nes550d_audio_callbacks++;
    nes550d_audio_total_ms += elapsed;
    if (elapsed > nes550d_audio_peak_ms) nes550d_audio_peak_ms = elapsed;
    if (elapsed > 10) nes550d_audio_misses++;
}

static void nes550d_audio_stopped(void *context)
{
    (void)context;
    nes550d_audio_running = 0;
    beep_playing = 0;
    audio_configure(1);
}

static void nes550d_audio_next(void *context)
{
    (void)context;
    if (!nes550d_audio_running)
        return;
    nes550d_audio_fill(nes550d_audio_buffers[nes550d_audio_next_buffer]);
    SetNextASIFDACBuffer(
        nes550d_audio_buffers[nes550d_audio_next_buffer],
        sizeof(nes550d_audio_buffers[nes550d_audio_next_buffer]));
    nes550d_audio_next_buffer = !nes550d_audio_next_buffer;
}

int nes550d_audio_ml_start(void)
{
    if (nes550d_audio_running)
        return 1;
    if (beep_playing)
    {
        printf("nes550d: Canon audio output is already in use\n");
        return 0;
    }
    nes550d_audio_fill(nes550d_audio_buffers[0]);
    nes550d_audio_fill(nes550d_audio_buffers[1]);
    nes550d_audio_next_buffer = 0;
    nes550d_audio_running = 1;
    beep_playing = 1;
    SetSamplingRate(NES550D_AUDIO_RATE, 1);
    MEM(0xC0920210) = 4;
    PowerAudioOutput();
    audio_configure(1);
    {
        int volume = nes550d_config_volume;
        if (volume < 1) volume = 1;
        if (volume > NES550D_AUDIO_VOLUME_MAX)
            volume = NES550D_AUDIO_VOLUME_MAX;
        SetAudioVolumeOut(volume);
    }
    StartASIFDMADAC(
        nes550d_audio_buffers[0], sizeof(nes550d_audio_buffers[0]),
        nes550d_audio_buffers[1], sizeof(nes550d_audio_buffers[1]),
        nes550d_audio_next, 0);
    return 1;
}

void nes550d_audio_ml_reset_stats(void)
{
    nes550d_audio_callbacks = 0;
    nes550d_audio_total_ms = 0;
    nes550d_audio_peak_ms = 0;
    nes550d_audio_misses = 0;
}

unsigned int nes550d_audio_ml_callback_count(void)
{
    return nes550d_audio_callbacks;
}

unsigned int nes550d_audio_ml_render_total_ms(void)
{
    return nes550d_audio_total_ms;
}

unsigned int nes550d_audio_ml_render_peak_ms(void)
{
    return nes550d_audio_peak_ms;
}

unsigned int nes550d_audio_ml_deadline_misses(void)
{
    return nes550d_audio_misses;
}

void nes550d_audio_ml_shutdown(void)
{
    if (nes550d_audio_running)
    {
        StopASIFDMADAC(nes550d_audio_stopped, 0);
        while (nes550d_audio_running)
            msleep(20);
    }
}
