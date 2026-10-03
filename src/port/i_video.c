#include <string.h>

#include "doomdef.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"

#include "platform.h"

static uint32_t palette[256];

void I_InitGraphics(void)
{
    if (plat_video_init(SCREENWIDTH, SCREENHEIGHT))
        I_Error("I_InitGraphics: no video output");
}

void I_ShutdownGraphics(void)
{
    plat_video_shutdown();
}

void I_SetPalette(byte *rgb)
{
    const byte *gamma = gammatable[usegamma];
    int i;

    for (i = 0; i < 256; i++, rgb += 3)
        palette[i] = 0xff000000u | (uint32_t)gamma[rgb[0]] << 16 |
                     (uint32_t)gamma[rgb[1]] << 8 | gamma[rgb[2]];
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    plat_video_present(screens[0], palette);
}

void I_WaitVBL(int count)
{
    plat_sleep_us(count * 1000000 / 70);
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
