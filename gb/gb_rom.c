#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <module.h>
#include <dryos.h>
#include <fio-ml.h>

#include "gb_rom.h"

static int gb550d_ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int gb550d_name_compare(const char *a, const char *b)
{
    while (*a && *b)
    {
        int difference = gb550d_ascii_lower(*a) - gb550d_ascii_lower(*b);
        if (difference)
            return difference;
        a++;
        b++;
    }
    return gb550d_ascii_lower(*a) - gb550d_ascii_lower(*b);
}

static int gb550d_has_extension(const char *name)
{
    unsigned int length = strlen(name);
    if (length > 3 && name[length - 3] == '.' &&
        gb550d_ascii_lower(name[length - 2]) == 'g' &&
        gb550d_ascii_lower(name[length - 1]) == 'b')
        return 1;
    return length > 4 && name[length - 4] == '.' &&
           gb550d_ascii_lower(name[length - 3]) == 'g' &&
           gb550d_ascii_lower(name[length - 2]) == 'b' &&
           gb550d_ascii_lower(name[length - 1]) == 'c';
}

int gb550d_rom_make_path(char *path, unsigned int size, const char *name)
{
    int length = snprintf(path, size, "%s%s", GB550D_ROM_DIRECTORY, name);
    return length >= 0 && (unsigned int)length < size;
}

int gb550d_rom_validate(const char *path)
{
    unsigned char header[0x150];
    FILE *file = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    int read;

    if (!file)
        return 0;
    read = FIO_ReadFile(file, header, sizeof(header));
    FIO_CloseFile(file);

    /* Minimal sanity check: enough bytes for a GB header and the
     * Nintendo logo at 0x104. */
    return read == (int)sizeof(header) &&
           header[0x104] == 0xce && header[0x105] == 0xed &&
           header[0x106] == 0x66 && header[0x107] == 0x66;
}

unsigned int gb550d_rom_name_id(const char *name)
{
    unsigned int hash = 2166136261u;
    while (*name)
    {
        hash ^= (unsigned int)gb550d_ascii_lower((unsigned char)*name++);
        hash *= 16777619u;
    }
    return hash ? hash : 1;
}

int gb550d_rom_find_id(const struct gb550d_rom_list *list, unsigned int id)
{
    int i;
    if (!id)
        return list->count ? 0 : -1;
    for (i = 0; i < list->count; i++)
        if (gb550d_rom_name_id(list->names[i]) == id)
            return i;
    return -1;
}

int gb550d_rom_scan(struct gb550d_rom_list *list)
{
    struct fio_file *file;
    struct fio_dirent *dirent;
    int i;
    int j;

    memset(list, 0, sizeof(*list));
    file = alloc_fio_file();
    if (!file)
        return -1;

    dirent = FIO_FindFirstEx(GB550D_ROM_DIRECTORY, file);
    if (!IS_ERROR(dirent))
    {
        list->directory_found = 1;
        do
        {
            struct file_info info = convert_fio_file_info(file);
            char path[FIO_MAX_PATH_LENGTH];
            if (!info.name[0] || (info.mode & ATTR_DIRECTORY) ||
                !gb550d_has_extension(info.name) ||
                !gb550d_rom_make_path(path, sizeof(path), info.name) ||
                !gb550d_rom_validate(path))
                continue;
            snprintf(list->names[list->count], sizeof(list->names[0]),
                     "%s", info.name);
            list->count++;
        }
        while (list->count < GB550D_MAX_ROMS &&
               FIO_FindNextEx(dirent, file) == 0);
        FIO_FindClose(dirent);
    }
    free(file);

    for (i = 0; i < list->count - 1; i++)
        for (j = i + 1; j < list->count; j++)
            if (gb550d_name_compare(list->names[i], list->names[j]) > 0)
            {
                char temporary[FIO_MAX_PATH_LENGTH];
                memcpy(temporary, list->names[i], sizeof(temporary));
                memcpy(list->names[i], list->names[j], sizeof(temporary));
                memcpy(list->names[j], temporary, sizeof(temporary));
            }
    return list->count;
}


