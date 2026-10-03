#ifndef __I_INPUT__
#define __I_INPUT__

#include "d_ticcmd.h"

void I_PollInput(void);
ticcmd_t *I_InputTiccmd(void);
void I_InputRumble(int strength);

#endif
