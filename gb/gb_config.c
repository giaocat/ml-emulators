#include "gb_config.h"

void gb550d_config_reset(void)
{
    gb550d_config_rom_id = 0;
    gb550d_config_video = 0;
    gb550d_config_buffer = 0;
    gb550d_config_audio = 1;
    gb550d_config_volume = 5;
    gb550d_config_debug = 0;
}
