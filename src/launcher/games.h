#ifndef GAMES_H
#define GAMES_H

#define MAX_GAMES 16
#define MAX_ARCHIVES 16
#define GAME_PATH 512

typedef enum
{
    GAME_ULTIMATE,
    GAME_DOOM,
    GAME_DOOM2,
    GAME_DOOM2_FRENCH,
    GAME_TNT,
    GAME_PLUTONIA,
    GAME_FREEDOOM1,
    GAME_FREEDOOM2,
    GAME_SHAREWARE,
} game_kind_t;

typedef struct
{
    game_kind_t kind;
    const char *file;
    const char *title;
    char path[GAME_PATH];
} game_t;

typedef struct
{
    game_t games[MAX_GAMES];
    int count;
    char archives[MAX_ARCHIVES][GAME_PATH];
    long long archive_sizes[MAX_ARCHIVES];
    int archive_count;
} game_list_t;

const char *games_known_file(const char *name);
int games_is_archive(const char *name);
int games_check_wad(const char *path, int *has_episode4);
void games_scan(game_list_t *list);
const game_t *games_find(const game_list_t *list, const char *file);

#endif
