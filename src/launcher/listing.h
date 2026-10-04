#ifndef LISTING_H
#define LISTING_H

#include <stddef.h>

#define MAX_LINKS 128
#define LINK_URL 768
#define LINK_NAME 128

typedef struct
{
    char url[LINK_URL];
    char name[LINK_NAME];
    int directory;
} link_t;

typedef struct
{
    char base[LINK_URL];
    link_t links[MAX_LINKS];
    int count;
} listing_t;

void listing_parse(listing_t *listing, const char *base, const char *html, size_t length);

#endif
