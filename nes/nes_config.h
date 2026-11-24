#ifndef NES6D_CONFIG_H
#define NES6D_CONFIG_H

#ifdef NES6D_CONFIG_DEFINE
#include <config.h>
CONFIG_INT("games.nes6d.rom", nes6d_config_rom_id, 0);
CONFIG_INT("games.nes6d.video", nes6d_config_video, 0);
CONFIG_INT("games.nes6d.buffer", nes6d_config_buffer, 0);
CONFIG_INT("games.nes6d.audio", nes6d_config_audio, 1);
CONFIG_INT("games.nes6d.volume", nes6d_config_volume, 5);
CONFIG_INT("games.nes6d.debug", nes6d_config_debug, 0);
#else
extern int nes6d_config_rom_id;
extern int nes6d_config_video;
extern int nes6d_config_buffer;
extern int nes6d_config_audio;
extern int nes6d_config_volume;
extern int nes6d_config_debug;
#endif

void nes6d_config_reset(void);

#endif


