#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "url.h"

static const char *path_start(const char *url)
{
    const char *scheme = strstr(url, "://");
    const char *slash;

    if (!scheme)
        return url;
    slash = strchr(scheme + 3, '/');
    return slash ? slash : url + strlen(url);
}

void url_clean(char *out, size_t size, const char *typed)
{
    size_t used = 0;
    const char *end;

    while (*typed == ' ')
        typed++;
    end = typed + strlen(typed);
    while (end > typed && end[-1] == ' ')
        end--;
    if (!strstr(typed, "://"))
        used = (size_t)snprintf(out, size, "http://");
    for (; typed < end && used + 4 < size; typed++)
    {
        if (*typed == ' ')
            used += (size_t)snprintf(out + used, size - used, "%%20");
        else
            out[used++] = *typed;
    }
    out[used < size ? used : size - 1] = 0;
}

void url_directory(char *out, size_t size, const char *url)
{
    const char *path = path_start(url);
    const char *last = strrchr(path, '/');

    snprintf(out, size, "%s", url);
    if (!*path || (last && last[1] && !strchr(last + 1, '.')))
        strncat(out, "/", size - strlen(out) - 1);
}

void url_resolve(char *out, size_t size, const char *base, const char *href)
{
    const char *path = path_start(base);
    const char *last;

    if (strstr(href, "://"))
        snprintf(out, size, "%s", href);
    else if (href[0] == '/' && href[1] == '/')
        snprintf(out, size, "%.*s%s", (int)(strstr(base, "//") - base), base, href);
    else if (href[0] == '/')
        snprintf(out, size, "%.*s%s", (int)(path - base), base, href);
    else
    {
        last = strrchr(path, '/');
        if (last)
            snprintf(out, size, "%.*s%s", (int)(last + 1 - base), base, href);
        else
            snprintf(out, size, "%s/%s", base, href);
    }
}

static int hex(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

void url_file_name(char *out, size_t size, const char *url)
{
    const char *path = path_start(url);
    const char *end = path + strcspn(path, "?#");
    const char *start;
    size_t used = 0;

    if (end > path && end[-1] == '/')
        end--;
    for (start = end; start > path && start[-1] != '/'; start--)
        ;
    while (start < end && used + 1 < size)
    {
        if (*start == '%' && end - start >= 3 && hex(start[1]) >= 0 && hex(start[2]) >= 0)
        {
            out[used++] = (char)(hex(start[1]) * 16 + hex(start[2]));
            start += 3;
        }
        else
            out[used++] = *start++;
    }
    out[used] = 0;
}
