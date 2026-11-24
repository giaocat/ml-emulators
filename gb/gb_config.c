#include "gb_config.h"

void gb6d_config_reset(void)
{
    gb6d_config_rom_id = 0;
    gb6d_config_video = 0;
    gb6d_config_buffer = 0;
    gb6d_config_audio = 1;
    gb6d_config_volume = 5;
    gb6d_config_debug = 0;
}

