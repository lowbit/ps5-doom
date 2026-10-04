#ifndef URL_H
#define URL_H

#include <stddef.h>

// Adds http:// when no scheme was typed and encodes spaces.
void url_clean(char *out, size_t size, const char *typed);
// The folder a link points at, ending in a slash.
void url_directory(char *out, size_t size, const char *url);
// A link from a page or a Location header, made absolute against the address it came from.
void url_resolve(char *out, size_t size, const char *base, const char *href);
// The last part of the path, percent-decoded.
void url_file_name(char *out, size_t size, const char *url);

#endif
