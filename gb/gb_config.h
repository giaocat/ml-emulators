#ifndef GB6D_CONFIG_H
#define GB6D_CONFIG_H

#ifdef GB6D_CONFIG_DEFINE
#include <config.h>
CONFIG_INT("games.gb6d.rom", gb6d_config_rom_id, 0);
CONFIG_INT("games.gb6d.video", gb6d_config_video, 0);
CONFIG_INT("games.gb6d.audio", gb6d_config_audio, 1);
CONFIG_INT("games.gb6d.volume", gb6d_config_volume, 5);
CONFIG_INT("games.gb6d.debug", gb6d_config_debug, 0);
#else
extern int gb6d_config_rom_id;
extern int gb6d_config_video;
extern int gb6d_config_audio;
extern int gb6d_config_volume;
extern int gb6d_config_debug;
#endif

void gb6d_config_reset(void);

#endif


