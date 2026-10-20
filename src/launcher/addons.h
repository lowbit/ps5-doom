#ifndef ADDONS_H
#define ADDONS_H

#include <stddef.h>

#include "games.h"

// Add-ons are PWADs: maps, graphics or sounds loaded on top of a game with id's -file. They live in
// an addons folder next to the games, or beside them where that folder cannot be made.

#define MAX_ADDONS 32
#define ADDON_NAME 64
#define ADDON_MAPS_DOOM1 1u
#define ADDON_MAPS_DOOM2 2u

typedef struct
{
    char name[ADDON_NAME];
    char path[GAME_PATH];
    unsigned maps; // ADDON_MAPS_* bits for the map numbering the add-on uses; 0 when it has no maps
} addon_t;

typedef struct
{
    addon_t addons[MAX_ADDONS];
    int count;
} addon_list_t;

const char *addons_folder(void);
// Where to store a new add-on under a WAD folder: its addons folder, made if needed, or the WAD
// folder itself where that cannot be made. -1 when the name is too long.
int addons_store_path(const char *dir, const char *name, char *path, size_t size);
// 0 when the file is a usable PWAD; maps receives its ADDON_MAPS_* bits.
int addons_check_wad(const char *path, unsigned *maps);
void addons_scan(addon_list_t *list);
const addon_t *addons_find(const addon_list_t *list, const char *name);
// Whether the add-on can be started on top of the game: its maps use the game's numbering, or it has
// none. Never for the shareware WAD, which id's engine refuses to run with -file.
int addons_fit(const addon_t *addon, const game_t *game);

#endif
