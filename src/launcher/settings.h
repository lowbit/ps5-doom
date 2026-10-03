#ifndef SETTINGS_H
#define SETTINGS_H

#include "games.h"

#define SETTINGS_URL 512
#define MAX_IMPORTED 32

typedef struct
{
    char game[64];
    char url[SETTINGS_URL];
    int accepted;
    char imported[MAX_IMPORTED][GAME_PATH + 24];
    int imported_count;
} settings_t;

void settings_load(settings_t *settings);
void settings_save(const settings_t *settings);
int settings_was_imported(const settings_t *settings, const char *path, long long size);
void settings_mark_imported(settings_t *settings, const char *path, long long size);

#endif
