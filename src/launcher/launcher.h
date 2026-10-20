#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "games.h"

const game_t *launcher_choose(void);
// The add-on chosen with the game, as a path, or NULL for none.
const char *launcher_addon(void);

#endif
