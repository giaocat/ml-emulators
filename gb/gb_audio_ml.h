#ifndef GB550D_AUDIO_ML_H
#define GB550D_AUDIO_ML_H

int GB550D_AUDIO_ml_start(void);
void GB550D_AUDIO_ml_shutdown(void);
void GB550D_AUDIO_ml_reset_stats(void);
unsigned int GB550D_AUDIO_ml_callback_count(void);
unsigned int GB550D_AUDIO_ml_render_total_ms(void);
unsigned int GB550D_AUDIO_ml_render_peak_ms(void);
unsigned int GB550D_AUDIO_ml_deadline_misses(void);

#endif

