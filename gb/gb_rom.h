#ifndef GB550D_ROM_H
#define GB550D_ROM_H

#include <fio-ml.h>

#define GB550D_ROM_DIRECTORY "ML/GB/"
#define GB550D_MAX_ROMS 32

struct gb550d_rom_list
{
    char names[GB550D_MAX_ROMS][FIO_MAX_PATH_LENGTH];
    int count;
    int directory_found;
};

int gb550d_rom_scan(struct gb550d_rom_list *list);
int gb550d_rom_validate(const char *path);
unsigned int gb550d_rom_name_id(const char *name);
int gb550d_rom_find_id(const struct gb550d_rom_list *list, unsigned int id);
int gb550d_rom_make_path(char *path, unsigned int size, const char *name);

#endif

