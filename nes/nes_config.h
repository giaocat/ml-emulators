#ifndef NES550D_CONFIG_H
#define NES550D_CONFIG_H

#ifdef NES550D_CONFIG_DEFINE
#include <config.h>
CONFIG_INT("games.nes550d.rom", nes550d_config_rom_id, 0);
CONFIG_INT("games.nes550d.video", nes550d_config_video, 0);
CONFIG_INT("games.nes550d.buffer", nes550d_config_buffer, 2);
CONFIG_INT("games.nes550d.audio", nes550d_config_audio, 1);
CONFIG_INT("games.nes550d.volume", nes550d_config_volume, 5);
CONFIG_INT("games.nes550d.debug", nes550d_config_debug, 0);
#else
extern int nes550d_config_rom_id;
extern int nes550d_config_video;
extern int nes550d_config_buffer;
extern int nes550d_config_audio;
extern int nes550d_config_volume;
extern int nes550d_config_debug;
#endif

void nes550d_config_reset(void);

#endif
