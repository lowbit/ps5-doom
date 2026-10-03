#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "settings.h"

static void settings_path(char *path, size_t size)
{
    snprintf(path, size, "%s/launcher.cfg", plat_save_dir());
}

void settings_load(settings_t *settings)
{
    char path[GAME_PATH], line[GAME_PATH + 64];
    FILE *file;

    memset(settings, 0, sizeof(*settings));
    settings_path(path, sizeof(path));
    if (!(file = fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "game ", 5))
            snprintf(settings->game, sizeof(settings->game), "%s", line + 5);
        else if (!strncmp(line, "url ", 4))
            snprintf(settings->url, sizeof(settings->url), "%s", line + 4);
        else if (!strncmp(line, "accepted ", 9))
            settings->accepted = atoi(line + 9);
        else if (!strncmp(line, "imported ", 9) && settings->imported_count < MAX_IMPORTED)
            snprintf(settings->imported[settings->imported_count++], sizeof(settings->imported[0]),
                     "%s", line + 9);
    }
    fclose(file);
}

void settings_save(const settings_t *settings)
{
    char path[GAME_PATH];
    FILE *file;
    int i;

    settings_path(path, sizeof(path));
    if (!(file = fopen(path, "w")))
    {
        plat_log("launcher: cannot write %s\n", path);
        return;
    }
    fprintf(file, "game %s\nurl %s\naccepted %d\n", settings->game, settings->url, settings->accepted);
    for (i = 0; i < settings->imported_count; i++)
        fprintf(file, "imported %s\n", settings->imported[i]);
    fclose(file);
}

int settings_was_imported(const settings_t *settings, const char *path, long long size)
{
    char key[GAME_PATH + 24];
    int i;

    snprintf(key, sizeof(key), "%lld %s", size, path);
    for (i = 0; i < settings->imported_count; i++)
        if (!strcmp(settings->imported[i], key))
            return 1;
    return 0;
}

void settings_mark_imported(settings_t *settings, const char *path, long long size)
{
    if (settings_was_imported(settings, path, size))
        return;
    if (settings->imported_count == MAX_IMPORTED)
    {
        memmove(settings->imported, settings->imported + 1,
                sizeof(settings->imported[0]) * (MAX_IMPORTED - 1));
        settings->imported_count--;
    }
    snprintf(settings->imported[settings->imported_count++], sizeof(settings->imported[0]),
             "%lld %s", size, path);
}
