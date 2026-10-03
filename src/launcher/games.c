#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "games.h"
#include "platform.h"

#define LUMP_ENTRY 16
#define MAX_LUMPS 100000

static const struct
{
    const char *file;
    game_kind_t kind;
    const char *title;
} known[] = {
    {"doomu.wad", GAME_ULTIMATE, "THE ULTIMATE DOOM"},
    {"doom.wad", GAME_DOOM, "DOOM"},
    {"doom2.wad", GAME_DOOM2, "DOOM II: HELL ON EARTH"},
    {"doom2f.wad", GAME_DOOM2_FRENCH, "DOOM II: L'ENFER SUR TERRE"},
    {"tnt.wad", GAME_TNT, "FINAL DOOM: TNT EVILUTION"},
    {"plutonia.wad", GAME_PLUTONIA, "FINAL DOOM: THE PLUTONIA EXPERIMENT"},
    {"freedoom1.wad", GAME_FREEDOOM1, "FREEDOOM: PHASE 1"},
    {"freedoom2.wad", GAME_FREEDOOM2, "FREEDOOM: PHASE 2"},
    {"doom1.wad", GAME_SHAREWARE, "DOOM SHAREWARE: EPISODE 1"},
};

#define KNOWN (int)(sizeof(known) / sizeof(known[0]))

typedef struct
{
    game_list_t *list;
    const char *dir;
} scan_t;

const char *games_known_file(const char *name)
{
    int i;

    for (i = 0; i < KNOWN; i++)
        if (!strcasecmp(name, known[i].file))
            return known[i].file;
    return NULL;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);

    return length > suffix_length && !strcasecmp(name + length - suffix_length, suffix);
}

int games_is_archive(const char *name)
{
    return has_suffix(name, ".zip") || has_suffix(name, ".7z") || has_suffix(name, ".rar");
}

static uint32_t read_le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int games_check_wad(const char *path, int *has_episode4)
{
    uint8_t header[12];
    uint8_t *directory = NULL;
    uint32_t lumps, offset;
    off_t size;
    int fd = open(path, O_RDONLY), ok = 0;

    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, header, sizeof(header)) == sizeof(header) &&
        !memcmp(header, "IWAD", 4))
    {
        lumps = read_le32(header + 4);
        offset = read_le32(header + 8);
        ok = lumps > 0 && lumps < MAX_LUMPS && (off_t)offset + (off_t)lumps * LUMP_ENTRY <= size;
    }
    if (ok && has_episode4)
    {
        uint32_t i;

        *has_episode4 = 0;
        directory = malloc((size_t)lumps * LUMP_ENTRY);
        ok = directory && lseek(fd, offset, SEEK_SET) == (off_t)offset &&
             read(fd, directory, (size_t)lumps * LUMP_ENTRY) == (ssize_t)lumps * LUMP_ENTRY;
        for (i = 0; ok && i < lumps; i++)
            if (!strncmp((const char *)directory + i * LUMP_ENTRY + 8, "E4M1", 8))
                *has_episode4 = 1;
        free(directory);
    }
    close(fd);
    return ok ? 0 : -1;
}

static void add_game(game_list_t *list, int index, const char *path)
{
    game_kind_t kind = known[index].kind;
    const char *title = known[index].title;
    int episode4 = 0, i;

    if (list->count == MAX_GAMES)
        return;
    if (games_check_wad(path, kind == GAME_DOOM ? &episode4 : NULL))
    {
        plat_log("launcher: %s is not a usable IWAD\n", path);
        return;
    }
    if (episode4)
    {
        kind = GAME_ULTIMATE;
        title = known[0].title;
    }
    for (i = 0; i < list->count; i++)
        if (list->games[i].kind == kind)
            return;
    list->games[list->count].kind = kind;
    list->games[list->count].file = known[index].file;
    list->games[list->count].title = title;
    snprintf(list->games[list->count].path, GAME_PATH, "%s", path);
    list->count++;
}

static void add_archive(game_list_t *list, const char *path)
{
    int fd;

    if (list->archive_count == MAX_ARCHIVES || (fd = open(path, O_RDONLY)) < 0)
        return;
    list->archive_sizes[list->archive_count] = lseek(fd, 0, SEEK_END);
    close(fd);
    snprintf(list->archives[list->archive_count], GAME_PATH, "%s", path);
    list->archive_count++;
}

static void on_entry(const char *name, void *user)
{
    scan_t *scan = user;
    char path[GAME_PATH];
    int i;

    snprintf(path, sizeof(path), "%s/%s", scan->dir, name);
    for (i = 0; i < KNOWN; i++)
        if (!strcasecmp(name, known[i].file))
            add_game(scan->list, i, path);
    if (games_is_archive(name))
        add_archive(scan->list, path);
}

static void probe_known(scan_t *scan)
{
    char path[GAME_PATH];
    int i, upper, fd;

    for (i = 0; i < KNOWN; i++)
        for (upper = 0; upper < 2; upper++)
        {
            char *c;

            snprintf(path, sizeof(path), "%s/%s", scan->dir, known[i].file);
            for (c = path + strlen(scan->dir) + 1; upper && *c; c++)
                *c = (char)toupper((unsigned char)*c);
            if ((fd = open(path, O_RDONLY)) >= 0)
            {
                close(fd);
                add_game(scan->list, i, path);
                break;
            }
        }
}

static int by_kind(const void *a, const void *b)
{
    return (int)((const game_t *)a)->kind - (int)((const game_t *)b)->kind;
}

void games_scan(game_list_t *list)
{
    const char *const *dir;
    scan_t scan;

    memset(list, 0, sizeof(*list));
    scan.list = list;
    for (dir = plat_wad_dirs(); *dir; dir++)
    {
        scan.dir = *dir;
        if (plat_list_dir(*dir, on_entry, &scan))
            probe_known(&scan);
    }
    qsort(list->games, (size_t)list->count, sizeof(list->games[0]), by_kind);
}

const game_t *games_find(const game_list_t *list, const char *file)
{
    int i;

    for (i = 0; file && i < list->count; i++)
        if (!strcasecmp(list->games[i].file, file))
            return &list->games[i];
    return NULL;
}
