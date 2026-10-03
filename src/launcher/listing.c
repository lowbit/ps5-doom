#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "games.h"
#include "listing.h"

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

static void decode_entities(char *text)
{
    static const struct
    {
        const char *entity;
        char c;
    } entities[] = {{"&amp;", '&'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&lt;", '<'}, {"&gt;", '>'}};
    char *read = text, *write = text;
    size_t i;

    while (*read)
    {
        for (i = 0; i < sizeof(entities) / sizeof(entities[0]); i++)
            if (!strncmp(read, entities[i].entity, strlen(entities[i].entity)))
                break;
        if (i < sizeof(entities) / sizeof(entities[0]))
        {
            *write++ = entities[i].c;
            read += strlen(entities[i].entity);
        }
        else
            *write++ = *read++;
    }
    *write = 0;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);

    return length > suffix_length && !strcasecmp(name + length - suffix_length, suffix);
}

static void add_link(listing_t *listing, const char *href)
{
    link_t *link;
    char url[LINK_URL];
    size_t base_length = strlen(listing->base);
    int i;

    if (!*href || strchr("?#", href[0]) || strchr(href, '?') || !strncasecmp(href, "mailto:", 7) ||
        !strncasecmp(href, "javascript:", 11))
        return;
    url_resolve(url, sizeof(url), listing->base, href);
    if (strstr(url, "/../") || strstr(url, "/./"))
        return;
    if (strncmp(url, listing->base, base_length) || strlen(url) == base_length ||
        listing->count == MAX_LINKS)
        return;
    for (i = 0; i < listing->count; i++)
        if (!strcmp(listing->links[i].url, url))
            return;

    link = &listing->links[listing->count];
    snprintf(link->url, sizeof(link->url), "%s", url);
    url_file_name(link->name, sizeof(link->name), url);
    link->directory = url[strlen(url) - 1] == '/';
    if (link->directory || has_suffix(link->name, ".wad") || games_is_archive(link->name))
        listing->count++;
}

void listing_parse(listing_t *listing, const char *base, const char *html, size_t length)
{
    const char *p = html, *end = html + length;

    memset(listing, 0, sizeof(*listing));
    snprintf(listing->base, sizeof(listing->base), "%s", base);
    while (p + 5 < end)
    {
        char href[LINK_URL];
        size_t used = 0;
        char quote = 0;

        if (strncasecmp(p, "href", 4))
        {
            p++;
            continue;
        }
        p += 4;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end || *p != '=')
            continue;
        p++;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p < end && (*p == '"' || *p == '\''))
            quote = *p++;
        while (p < end && used + 1 < sizeof(href) &&
               (quote ? *p != quote : !isspace((unsigned char)*p) && *p != '>'))
            href[used++] = *p++;
        href[used] = 0;
        decode_entities(href);
        add_link(listing, href);
    }
}
