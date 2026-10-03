#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define MAPPED_FROM (256 * 1024)
#define HEADER 16

typedef struct
{
    size_t size;
    size_t mapped;
} header_t;

_Static_assert(sizeof(header_t) <= HEADER, "allocation header must fit its slot");

void *import_malloc(size_t size)
{
    header_t *header;

    if (size > (size_t)-1 - HEADER)
        return NULL;
    if (size + HEADER >= MAPPED_FROM)
    {
        header = mmap(NULL, size + HEADER, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (header == MAP_FAILED)
            return NULL;
        header->mapped = 1;
    }
    else
    {
        if (!(header = malloc(size + HEADER)))
            return NULL;
        header->mapped = 0;
    }
    header->size = size;
    return (char *)header + HEADER;
}

void import_free(void *pointer)
{
    header_t *header;

    if (!pointer)
        return;
    header = (header_t *)((char *)pointer - HEADER);
    if (header->mapped)
        munmap(header, header->size + HEADER);
    else
        free(header);
}

void *import_calloc(size_t count, size_t size)
{
    void *pointer;

    if (size && count > (size_t)-1 / size)
        return NULL;
    pointer = import_malloc(count * size);
    if (pointer && count * size + HEADER < MAPPED_FROM)
        memset(pointer, 0, count * size);
    return pointer;
}

void *import_realloc(void *pointer, size_t size)
{
    header_t *header;
    void *grown;

    if (!pointer)
        return import_malloc(size);
    if (!size)
    {
        import_free(pointer);
        return NULL;
    }
    header = (header_t *)((char *)pointer - HEADER);
    if (size <= header->size)
        return pointer;
    if (!(grown = import_malloc(size)))
        return NULL;
    memcpy(grown, pointer, header->size);
    import_free(pointer);
    return grown;
}

char *import_strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = import_malloc(length);

    if (copy)
        memcpy(copy, text, length);
    return copy;
}
