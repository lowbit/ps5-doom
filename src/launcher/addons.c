#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "addons.h"
#include "platform.h"

#define LUMP_ENTRY 16
#define MAX_LUMPS 100000

typedef struct
{
    addon_list_t *list;
    char dir[GAME_PATH];
    int beside_games;
} scan_t;

static uint32_t read_le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);

    return length > suffix_length && !strcasecmp(name + length - suffix_length, suffix);
}

const char *addons_folder(void)
{
    return "addons";
}

int addons_store_path(const char *dir, const char *name, char *path, size_t size)
{
    char folder[GAME_PATH];

    if (strlen(name) >= ADDON_NAME)
        return -1;
    snprintf(folder, sizeof(folder), "%s/%s", dir, addons_folder());
    if (mkdir(folder, 0755) == 0 || errno == EEXIST)
        snprintf(path, size, "%s/%s", folder, name);
    else
        snprintf(path, size, "%s/%s", dir, name);
    return 0;
}

static unsigned map_bits(const char *name)
{
    if (name[0] == 'E' && isdigit((unsigned char)name[1]) && name[2] == 'M' && isdigit((unsigned char)name[3]) &&
        name[4] == 0)
        return ADDON_MAPS_DOOM1;
    if (!strncmp(name, "MAP", 3) && isdigit((unsigned char)name[3]) && isdigit((unsigned char)name[4]) &&
        name[5] == 0)
        return ADDON_MAPS_DOOM2;
    return 0;
}

int addons_check_wad(const char *path, unsigned *maps)
{
    uint8_t header[12];
    uint8_t *directory;
    uint32_t lumps = 0, offset = 0, i;
    off_t size;
    int fd = open(path, O_RDONLY), ok;

    if (maps)
        *maps = 0;
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    ok = lseek(fd, 0, SEEK_SET) == 0 && read(fd, header, sizeof(header)) == sizeof(header) &&
         !memcmp(header, "PWAD", 4);
    if (ok)
    {
        lumps = read_le32(header + 4);
        offset = read_le32(header + 8);
        ok = lumps > 0 && lumps < MAX_LUMPS && (off_t)offset + (off_t)lumps * LUMP_ENTRY <= size;
    }
    if (ok && maps)
    {
        directory = malloc((size_t)lumps * LUMP_ENTRY);
        ok = directory && lseek(fd, offset, SEEK_SET) == (off_t)offset &&
             read(fd, directory, (size_t)lumps * LUMP_ENTRY) == (ssize_t)lumps * LUMP_ENTRY;
        for (i = 0; ok && i < lumps; i++)
            *maps |= map_bits((const char *)directory + i * LUMP_ENTRY + 8);
        free(directory);
    }
    close(fd);
    return ok ? 0 : -1;
}

static void add_addon(addon_list_t *list, const char *path, const char *name)
{
    unsigned maps;
    int i;

    if (list->count == MAX_ADDONS || !has_suffix(name, ".wad") || strlen(name) >= ADDON_NAME)
        return;
    for (i = 0; i < list->count; i++)
        if (!strcasecmp(list->addons[i].name, name))
            return;
    if (addons_check_wad(path, &maps))
        return;
    snprintf(list->addons[list->count].name, ADDON_NAME, "%s", name);
    snprintf(list->addons[list->count].path, GAME_PATH, "%s", path);
    list->addons[list->count].maps = maps;
    list->count++;
}

static void on_entry(const char *name, void *user)
{
    scan_t *scan = user;
    char path[GAME_PATH];

    if (scan->beside_games && games_known_file(name))
        return;
    snprintf(path, sizeof(path), "%s/%s", scan->dir, name);
    add_addon(scan->list, path, name);
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const addon_t *)a)->name, ((const addon_t *)b)->name);
}

void addons_scan(addon_list_t *list)
{
    const char *const *dir;
    scan_t scan;

    memset(list, 0, sizeof(*list));
    scan.list = list;
    for (dir = plat_wad_dirs(); *dir; dir++)
    {
        snprintf(scan.dir, sizeof(scan.dir), "%s/%s", *dir, addons_folder());
        scan.beside_games = 0;
        plat_list_dir(scan.dir, on_entry, &scan);
        snprintf(scan.dir, sizeof(scan.dir), "%s", *dir);
        scan.beside_games = 1;
        plat_list_dir(scan.dir, on_entry, &scan);
    }
    qsort(list->addons, (size_t)list->count, sizeof(list->addons[0]), by_name);
}

const addon_t *addons_find(const addon_list_t *list, const char *name)
{
    int i;

    for (i = 0; name && i < list->count; i++)
        if (!strcasecmp(list->addons[i].name, name))
            return &list->addons[i];
    return NULL;
}

int addons_fit(const addon_t *addon, const game_t *game)
{
    unsigned game_maps;

    switch (game->kind)
    {
    case GAME_SHAREWARE:
        return 0;
    case GAME_DOOM2:
    case GAME_DOOM2_FRENCH:
    case GAME_TNT:
    case GAME_PLUTONIA:
    case GAME_FREEDOOM2:
        game_maps = ADDON_MAPS_DOOM2;
        break;
    default:
        game_maps = ADDON_MAPS_DOOM1;
        break;
    }
    return !addon->maps || (addon->maps & game_maps) != 0;
}
