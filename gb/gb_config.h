#ifndef GB550D_CONFIG_H
#define GB550D_CONFIG_H

#ifdef GB550D_CONFIG_DEFINE
#include <config.h>
CONFIG_INT("games.gb550d.rom", gb550d_config_rom_id, 0);
CONFIG_INT("games.gb550d.video", gb550d_config_video, 0);
CONFIG_INT("games.gb550d.buffer", gb550d_config_buffer, 0);
CONFIG_INT("games.gb550d.audio", gb550d_config_audio, 1);
CONFIG_INT("games.gb550d.volume", gb550d_config_volume, 5);
CONFIG_INT("games.gb550d.debug", gb550d_config_debug, 0);
#else
extern int gb550d_config_rom_id;
extern int gb550d_config_video;
extern int gb550d_config_buffer;
extern int gb550d_config_audio;
extern int gb550d_config_volume;
extern int gb550d_config_debug;
#endif

void gb550d_config_reset(void);

#endif
