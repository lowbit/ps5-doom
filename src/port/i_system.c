#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>

#include "doomdef.h"
#include "d_net.h"
#include "g_game.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_input.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_video.h"

#include "launcher.h"
#include "platform.h"

#define ZONE_SIZE (64 * 1024 * 1024)
#define POLL_INTERVAL_US 1000

static uint64_t base_time;
static uint64_t last_poll;

void I_Init(void)
{
    I_InitSound();
}

byte *I_ZoneBase(int *size)
{
    byte *zone = mmap(NULL, ZONE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

    if (zone == MAP_FAILED)
        I_Error("I_ZoneBase: cannot map %d bytes", ZONE_SIZE);
    *size = ZONE_SIZE;
    return zone;
}

int I_GetTime(void)
{
    uint64_t now = plat_ticks_us();

    if (!base_time)
        base_time = now;
    return (int)((now - base_time) * TICRATE / 1000000);
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    uint64_t now = plat_ticks_us();

    if (now - last_poll < POLL_INTERVAL_US)
    {
        plat_sleep_us(POLL_INTERVAL_US - (uint32_t)(now - last_poll));
        now = plat_ticks_us();
    }
    last_poll = now;
    I_PollInput();
}

ticcmd_t *I_BaseTiccmd(void)
{
    return I_InputTiccmd();
}

void I_Tactile(int on, int off, int total)
{
    (void)on;
    (void)off;
    I_InputRumble(total);
}

byte *I_AllocLow(int length)
{
    byte *mem = calloc(1, length);

    if (!mem)
        I_Error("I_AllocLow: cannot allocate %d bytes", length);
    return mem;
}

char *I_GetSaveDir(void)
{
    return (char *)plat_save_dir();
}

// Adds a WAD to the -file parameters the engine reads in D_DoomMain: after the ones already given,
// so the launcher's choice loads last, or as a new -file group at the end.
static void add_file_parameter(const char *path)
{
    char **next = malloc(sizeof(char *) * ((size_t)myargc + 3));
    int group = M_CheckParm("-file"), i, n = 0;

    if (!next)
        return;
    if (group)
    {
        int end = group;

        while (++end < myargc && myargv[end][0] != '-')
            ;
        for (i = 0; i < end; i++)
            next[n++] = myargv[i];
        next[n++] = (char *)path;
        for (; i < myargc; i++)
            next[n++] = myargv[i];
    }
    else
    {
        for (i = 0; i < myargc; i++)
            next[n++] = myargv[i];
        next[n++] = "-file";
        next[n++] = (char *)path;
    }
    next[n] = NULL;
    myargv = next;
    myargc = n;
}

iwad_t *I_ChooseIwad(void)
{
    static const struct
    {
        game_kind_t kind;
        GameMode_t mode;
        GameMission_t mission;
        Language_t language;
    } modes[] = {
        {GAME_ULTIMATE, retail, doom, english},
        {GAME_DOOM, registered, doom, english},
        {GAME_DOOM2, commercial, doom2, english},
        {GAME_DOOM2_FRENCH, commercial, doom2, french},
        {GAME_TNT, commercial, pack_tnt, english},
        {GAME_PLUTONIA, commercial, pack_plut, english},
        {GAME_FREEDOOM1, retail, doom, english},
        {GAME_FREEDOOM2, commercial, doom2, english},
        {GAME_SHAREWARE, shareware, doom, english},
    };
    static iwad_t iwad;
    static char savename[GAME_PATH];
    const game_t *game = launcher_choose();
    const char *addon, *base;
    char suffix[32] = "";
    size_t i, length;

    if (!game)
        return NULL;
    if ((addon = launcher_addon()))
    {
        base = strrchr(addon, '/') ? strrchr(addon, '/') + 1 : addon;
        length = strlen(base);
        if (length > 4 && !strcasecmp(base + length - 4, ".wad"))
            length -= 4;
        if (length > 24)
            length = 24;
        snprintf(suffix, sizeof(suffix), "-%.*s", (int)length, base);
        add_file_parameter(addon);
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
        if (modes[i].kind == game->kind)
        {
            iwad.mode = modes[i].mode;
            iwad.mission = modes[i].mission;
            iwad.language = modes[i].language;
        }
    snprintf(savename, sizeof(savename), "%s/%.*s%ssav", plat_save_dir(), (int)(strlen(game->file) - 4),
             game->file, suffix);
    iwad.path = (char *)game->path;
    iwad.savename = savename;
    if (addon)
        plat_log("launcher: starting %s from %s with %s\n", game->title, game->path, addon);
    else
        plat_log("launcher: starting %s from %s\n", game->title, game->path);
    return &iwad;
}

static void shutdown_all(void)
{
    I_ShutdownMusic();
    I_ShutdownSound();
    I_ShutdownGraphics();
    plat_shutdown();
}

void I_Quit(void)
{
    D_QuitNetGame();
    M_SaveDefaults();
    shutdown_all();
    plat_exit(0);
}

void I_Error(char *error, ...)
{
    static int recursive;
    char message[512];
    va_list args;

    va_start(args, error);
    vsnprintf(message, sizeof(message), error, args);
    va_end(args);

    plat_log("Error: %s\n", message);
    if (recursive++)
        plat_exit(1);
    plat_alert(message);

    if (demorecording)
        G_CheckDemoStatus();
    D_QuitNetGame();
    shutdown_all();
    plat_exit(1);
}
