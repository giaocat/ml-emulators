#ifndef NES550D_AUDIO_ML_H
#define NES550D_AUDIO_ML_H

int nes550d_audio_ml_start(void);
void nes550d_audio_ml_shutdown(void);
void nes550d_audio_ml_reset_stats(void);
unsigned int nes550d_audio_ml_callback_count(void);
unsigned int nes550d_audio_ml_render_total_ms(void);
unsigned int nes550d_audio_ml_render_peak_ms(void);
unsigned int nes550d_audio_ml_deadline_misses(void);

#endif
