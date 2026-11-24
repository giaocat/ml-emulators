#include "nes_config.h"

void nes550d_config_reset(void)
{
    nes550d_config_rom_id = 0;
    nes550d_config_video = 0;
    nes550d_config_buffer = 2;
    nes550d_config_audio = 1;
    nes550d_config_volume = 5;
    nes550d_config_debug = 0;
}
