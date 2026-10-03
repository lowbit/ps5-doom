#include <stdlib.h>

#include "doomstat.h"
#include "d_net.h"
#include "i_net.h"
#include "i_system.h"

void I_InitNetwork(void)
{
    doomcom = calloc(1, sizeof(*doomcom));
    if (!doomcom)
        I_Error("I_InitNetwork: out of memory");

    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->ticdup = 1;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}

void I_NetCmd(void)
{
}
