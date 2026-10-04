#ifndef UPLOAD_H
#define UPLOAD_H

// A small HTTP server for sending game files from a browser on the same network: it serves an
// upload page, stores each file it receives in a folder as <name>.upload and hands the finished
// ones to the caller. It runs only between upload_start and upload_stop.

// The first port tried. A title's sandbox refuses some ports (8666 and 50000 on FW 13.00) and
// others may be taken, so the next few are tried after it.
#define UPLOAD_PORT 9666
#define UPLOAD_PORT_TRIES 8
#define UPLOAD_NAME 64
#define UPLOAD_PATH 512

typedef struct
{
    int receiving;
    char name[UPLOAD_NAME];
    long long done, total;
} upload_progress_t;

// Starts listening; 0 on success. The address is "" when the console has no network address.
int upload_start(const char *dir);
void upload_stop(void);
const char *upload_address(void);

void upload_progress(upload_progress_t *progress);

// Hands over the oldest finished upload: its path and the name it was sent with. 1 when there was one.
int upload_take(char *path, int path_size, char *name, int name_size);

// Adds a line to the log the page shows, for what the console did with a file.
void upload_note(const char *text, int ok);

#endif
