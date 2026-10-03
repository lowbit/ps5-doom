#ifndef IMPORT_H
#define IMPORT_H

#include "listing.h"

#define MAX_SOURCES MAX_LINKS

typedef enum
{
    IMPORT_IDLE,
    IMPORT_RUNNING,
    IMPORT_LISTING,
    IMPORT_DONE,
    IMPORT_FAILED,
    IMPORT_CANCELLED,
} import_state_t;

typedef struct
{
    import_state_t state;
    long long done, total;
    int source_index, source_count;
    char source[LINK_NAME];
    char current[32];
    char found[192];
    int found_count;
    char message[192];
} import_status_t;

void import_start(char sources[][LINK_URL], int count, const char *dest);
void import_status(import_status_t *status);
const listing_t *import_listing(void);
void import_cancel(void);
void import_finish(void);

#endif
