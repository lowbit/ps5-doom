#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

#include "platform.h"

int main(int argc, char **argv)
{
    if (plat_init(argc, argv))
        plat_exit(1);

    myargc = argc;
    myargv = argv;
    D_DoomMain();
    return 0;
}
