#ifndef NES550D_ROM_H
#define NES550D_ROM_H

#include <fio-ml.h>

#define NES550D_ROM_DIRECTORY "ML/NES/"
#define NES550D_MAX_ROMS 32

struct nes550d_rom_list
{
    char names[NES550D_MAX_ROMS][FIO_MAX_PATH_LENGTH];
    int count;
    int directory_found;
};

int nes550d_rom_scan(struct nes550d_rom_list *list);
int nes550d_rom_validate_ines(const char *path);
unsigned int nes550d_rom_name_id(const char *name);
int nes550d_rom_find_id(const struct nes550d_rom_list *list, unsigned int id);
int nes550d_rom_make_path(char *path, unsigned int size, const char *name);

#endif
