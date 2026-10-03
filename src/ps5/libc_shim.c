#include <stddef.h>

int _Getmbcurmax(void);

/* The SDK's FreeBSD headers expand MB_CUR_MAX to this; the console's C library names it _Getmbcurmax. */
size_t ___mb_cur_max(void)
{
    return (size_t)_Getmbcurmax();
}
