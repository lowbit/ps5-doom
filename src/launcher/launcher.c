#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "games.h"
#include "glyphs.h"
#include "import.h"
#include "launcher.h"
#include "listing.h"
#include "platform.h"
#include "screen.h"
#include "settings.h"
#include "test_plan.h"
#include "upload.h"
#include "url.h"

#define FRAME_US 16667
#define REPEAT_DELAY 18
#define REPEAT_RATE 5
#define STICK 16000
#define ROW 12
#define SKULL_FRAMES 14
#define GAME_ROWS 7
#define BROWSE_ROWS 12
#define FOOTER_Y 188
#define MB (1024.0 * 1024.0)
#define SEND_LINES 4

typedef enum
{
    VIEW_GAMES,
    VIEW_ADD,
    VIEW_NOTICE,
    VIEW_BROWSE,
    VIEW_IMPORT,
    VIEW_SEND,
} view_t;

static game_list_t games;
static settings_t settings;
static listing_t browse;
static char import_dir[GAME_PATH];
static char status_line[192];
static ink_t status_ink;
static char pending[MAX_ARCHIVES][LINK_URL];
static long long pending_sizes[MAX_ARCHIVES];
static int pending_count;
static view_t view, import_return;
static int game_row, add_row, notice_row, browse_row;
static int frame, typing, understood, auto_import;
static uint32_t held, pressed;
static int repeat_timer, input_locked = 1;
static const game_t *chosen;
static int send_ready, send_added;
static char send_lines[SEND_LINES][192];
static int send_line_ok[SEND_LINES], send_line_count;
static char send_archive[GAME_PATH + UPLOAD_NAME];

static void start_send(void);

static void read_input(void)
{
    pad_state_t pad;
    uint32_t now = 0;

    if (plat_pad_read(&pad) == 0)
    {
        now = pad.buttons;
        if (pad.ly < -STICK)
            now |= PAD_UP;
        if (pad.ly > STICK)
            now |= PAD_DOWN;
    }
    pressed = 0;
    if (input_locked)
    {
        input_locked = now != 0;
        held = now;
        return;
    }
    pressed = now & ~held;
    if (now & (PAD_UP | PAD_DOWN))
    {
        if (pressed & (PAD_UP | PAD_DOWN))
            repeat_timer = REPEAT_DELAY;
        else if (--repeat_timer <= 0)
        {
            pressed |= now & (PAD_UP | PAD_DOWN);
            repeat_timer = REPEAT_RATE;
        }
    }
    held = now;
}

static void move(int *row, int count)
{
    if (count <= 0)
        return;
    if (pressed & PAD_UP)
        *row = (*row + count - 1) % count;
    if (pressed & PAD_DOWN)
        *row = (*row + 1) % count;
}

static void present(void)
{
    static uint64_t next;
    uint64_t now;

    screen_present();
    now = plat_ticks_us();
    if (next > now)
        plat_sleep_us((uint32_t)(next - now));
    next = (next > now ? next : now) + FRAME_US;
    frame++;
}

static void draw_row(int x, int y, const char *text, int selected, int enabled)
{
    if (selected)
        screen_skull(x - 14, y - 2, frame / SKULL_FRAMES);
    screen_text(x, y, text, !enabled ? INK_GRAY : selected ? INK_WHITE : INK_RED, 1);
}

static void set_status(const char *text, ink_t ink)
{
    snprintf(status_line, sizeof(status_line), "%s", text);
    status_ink = ink;
}

static void draw_status(int y, int wrap)
{
    if (!status_line[0])
        return;
    if (wrap)
        screen_wrap(16, y, SCREEN_WIDTH - 32, status_line, status_ink);
    else
        screen_text_fit(16, y, SCREEN_WIDTH - 32, status_line, status_ink, 0);
}

static void choose_import_dir(void)
{
    const char *const *dir;
    char probe[GAME_PATH + 16];
    int fd;

    for (dir = plat_wad_dirs(); *dir; dir++)
    {
        snprintf(probe, sizeof(probe), "%s/.write-test", *dir);
        if ((fd = open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0)
        {
            close(fd);
            unlink(probe);
            snprintf(import_dir, sizeof(import_dir), "%s", *dir);
            plat_log("launcher: imports go to %s\n", import_dir);
            return;
        }
    }
    snprintf(import_dir, sizeof(import_dir), "%s", plat_save_dir());
    plat_log("launcher: no writable game folder, imports go to %s\n", import_dir);
}

static void start_import(char sources[][LINK_URL], int count, view_t back, int automatic)
{
    import_start(sources, count, import_dir);
    import_return = back;
    auto_import = automatic;
    view = VIEW_IMPORT;
}

static void start_auto_import(void)
{
    int i;

    pending_count = 0;
    for (i = 0; i < games.archive_count; i++)
        if (!settings_was_imported(&settings, games.archives[i], games.archive_sizes[i]))
        {
            snprintf(pending[pending_count], LINK_URL, "%s", games.archives[i]);
            pending_sizes[pending_count++] = games.archive_sizes[i];
        }
    if (pending_count)
        start_import(pending, pending_count, VIEW_GAMES, 1);
}

static void games_view(void)
{
    int count = games.count + 1, width = 0, first, x, i;

    move(&game_row, count);
    if (pressed & PAD_CROSS)
    {
        if (game_row < games.count)
        {
            chosen = &games.games[game_row];
            snprintf(settings.game, sizeof(settings.game), "%s", chosen->file);
            settings_save(&settings);
            return;
        }
        add_row = 0;
        view = VIEW_ADD;
    }
    if (pressed & PAD_CIRCLE)
        plat_exit(0);

    screen_background();
    screen_logo(4);
    for (i = 0; i < games.count; i++)
        if (screen_text_width(games.games[i].title, 1) > width)
            width = screen_text_width(games.games[i].title, 1);
    x = (SCREEN_WIDTH - width) / 2 > 28 ? (SCREEN_WIDTH - width) / 2 : 28;
    first = game_row >= GAME_ROWS ? game_row - GAME_ROWS + 1 : 0;
    for (i = first; i < count && i < first + GAME_ROWS; i++)
        draw_row(x, 76 + (i - first) * ROW, i < games.count ? games.games[i].title : "Add games...",
                 i == game_row, 1);
    if (status_line[0])
        draw_status(160, 1);
    else if (games.count == 1 && games.games[0].kind == GAME_SHAREWARE)
    {
        screen_text_center(160, "Own Doom, Doom II, TNT or Plutonia?", INK_GRAY, 1);
        screen_text_center(170, "Add them under Add games.", INK_GRAY, 1);
    }
    screen_text_center(FOOTER_Y, GLYPH_CROSS " start    " GLYPH_CIRCLE " quit", INK_GRAY, 1);
}

static void add_view(void)
{
    char text[SETTINGS_URL], line[SETTINGS_URL + 8];
    int y, result;

    if (typing)
    {
        result = plat_text_poll(text, sizeof(text));
        if (result != 0)
        {
            typing = 0;
            input_locked = 1;
            if (result > 0 && text[0])
            {
                url_clean(settings.url, sizeof(settings.url), text);
                settings_save(&settings);
                add_row = 2;
            }
        }
    }
    else
    {
        move(&add_row, 4);
        if ((pressed & PAD_CROSS) && add_row == 0)
            start_send();
        else if ((pressed & PAD_CROSS) && add_row == 1)
        {
            if (plat_text_open("Link to a WAD, ZIP, 7Z or RAR file, or a folder",
                               settings.url[0] ? settings.url : "http://") == 0)
                typing = 1;
            else
                set_status("The keyboard is not available", INK_RED);
        }
        else if ((pressed & PAD_CROSS) && add_row == 2 && settings.url[0])
        {
            if (settings.accepted)
            {
                char source[1][LINK_URL];

                snprintf(source[0], LINK_URL, "%s", settings.url);
                start_import(source, 1, VIEW_ADD, 0);
            }
            else
            {
                understood = 0;
                notice_row = 0;
                view = VIEW_NOTICE;
            }
        }
        else if (((pressed & PAD_CROSS) && add_row == 3) || (pressed & PAD_CIRCLE))
            view = VIEW_GAMES;
    }

    screen_background();
    screen_text_center(6, "Add games", INK_GOLD, 2);
    y = screen_wrap(16, 28, SCREEN_WIDTH - 32,
                    "DOOM.WAD, DOOM2.WAD, TNT.WAD, PLUTONIA.WAD or Freedoom, on their own or inside "
                    "ZIP, 7Z or RAR files.", INK_GRAY);
    draw_row(30, y + 6, "Send from PC or phone", add_row == 0, 1);
    y = screen_wrap(16, y + 6 + ROW + 6, SCREEN_WIDTH - 32,
                    "Or download a file, or a folder your PC shares over HTTP:", INK_RED);
    snprintf(line, sizeof(line), "Link: %s", settings.url[0] ? settings.url : "not set");
    if (add_row == 1)
        screen_skull(16, y + 4, frame / SKULL_FRAMES);
    screen_text_fit(30, y + 6, SCREEN_WIDTH - 46, line, add_row == 1 ? INK_WHITE : INK_RED, 1);
    draw_row(30, y + 6 + ROW, "Download", add_row == 2, settings.url[0] != 0);
    draw_row(30, y + 6 + ROW * 2, "Back", add_row == 3, 1);
    y = screen_wrap(16, y + 6 + ROW * 3 + 4, SCREEN_WIDTH - 32,
                    "Or copy them over FTP or USB to this folder; Doom adds them when it starts:", INK_RED);
    screen_text_fit(16, y + 1, SCREEN_WIDTH - 32, plat_wad_folder(), INK_GOLD, 1);
    draw_status(FOOTER_Y - 11, 0);
    screen_text_center(FOOTER_Y, add_row == 1 ? GLYPH_CROSS " edit link    " GLYPH_CIRCLE " back"
                                              : GLYPH_CROSS " select    " GLYPH_CIRCLE " back", INK_GRAY, 1);
}

static void notice_view(void)
{
    int y;

    move(&notice_row, 3);
    if (pressed & PAD_CROSS)
    {
        if (notice_row == 0)
            understood = !understood;
        else if (notice_row == 1 && understood)
        {
            char source[1][LINK_URL];

            settings.accepted = 1;
            settings_save(&settings);
            snprintf(source[0], LINK_URL, "%s", settings.url);
            start_import(source, 1, VIEW_ADD, 0);
            return;
        }
        else if (notice_row == 2)
            view = VIEW_ADD;
    }
    if (pressed & PAD_CIRCLE)
        view = VIEW_ADD;

    screen_background();
    screen_text_center(10, "Before you download", INK_GOLD, 2);
    y = screen_wrap(20, 40, SCREEN_WIDTH - 40,
                    "Only download game files you own, or files that are free to share such as "
                    "Freedoom. You are responsible for what you download and for following the "
                    "laws where you live.", INK_RED);
    draw_row(52, y + 16, "I understand", notice_row == 0, 1);
    screen_box(40, y + 15, 8, 8, 0);
    if (understood)
        screen_box(42, y + 17, 4, 4, 1);
    draw_row(40, y + 16 + ROW, "Continue", notice_row == 1, understood);
    draw_row(40, y + 16 + ROW * 2, "Back", notice_row == 2, 1);
    screen_text_center(FOOTER_Y, GLYPH_CROSS " select    " GLYPH_CIRCLE " back", INK_GRAY, 1);
}

static int browse_files(void)
{
    int files = 0, i;

    for (i = 0; i < browse.count; i++)
        files += !browse.links[i].directory;
    return files;
}

static void browse_view(void)
{
    static char sources[MAX_LINKS][LINK_URL];
    int files = browse_files(), offset = files ? 1 : 0, count = browse.count + offset, first, i;

    move(&browse_row, count);
    if ((pressed & PAD_CROSS) && count)
    {
        int n = 0;

        if (offset && browse_row == 0)
        {
            for (i = 0; i < browse.count; i++)
                if (!browse.links[i].directory)
                    snprintf(sources[n++], LINK_URL, "%s", browse.links[i].url);
        }
        else
            snprintf(sources[n++], LINK_URL, "%s", browse.links[browse_row - offset].url);
        start_import(sources, n, VIEW_BROWSE, 0);
        return;
    }
    if (pressed & PAD_CIRCLE)
        view = VIEW_ADD;

    screen_background();
    screen_text_fit(16, 8, SCREEN_WIDTH - 32, browse.base, INK_GOLD, 1);
    if (!count)
        screen_text_center(60, "No WAD, ZIP, 7Z or RAR files here", INK_RED, 1);
    first = browse_row >= BROWSE_ROWS ? browse_row - BROWSE_ROWS + 1 : 0;
    for (i = first; i < count && i < first + BROWSE_ROWS; i++)
    {
        char line[LINK_NAME + 32];
        int y = 28 + (i - first) * ROW;

        if (offset && i == 0 && files == 1)
            snprintf(line, sizeof(line), "Import this file");
        else if (offset && i == 0)
            snprintf(line, sizeof(line), "Import all %d files", files);
        else
            snprintf(line, sizeof(line), "%s%s", browse.links[i - offset].name,
                     browse.links[i - offset].directory ? "/" : "");
        if (i == browse_row)
            screen_skull(16, y - 2, frame / SKULL_FRAMES);
        screen_text_fit(30, y, SCREEN_WIDTH - 46, line, i == browse_row ? INK_WHITE : INK_RED, 0);
    }
    draw_status(FOOTER_Y - 11, 0);
    screen_text_center(FOOTER_Y, GLYPH_CROSS " open    " GLYPH_CIRCLE " back", INK_GRAY, 1);
}

static void finish_import(const import_status_t *status)
{
    char text[256];
    int i;

    if (auto_import && status->state == IMPORT_DONE)
        for (i = 0; i < pending_count; i++)
            settings_mark_imported(&settings, pending[i], pending_sizes[i]);
    settings_save(&settings);
    import_finish();
    games_scan(&games);
    if (status->found_count)
    {
        snprintf(text, sizeof(text), "Added %s", status->found);
        set_status(text, INK_GREEN);
    }
    else if (status->message[0])
        set_status(status->message, INK_RED);
    else if (status->state == IMPORT_DONE)
        set_status("No game WADs found to add", INK_RED);
    view = status->found_count ? VIEW_GAMES : import_return;
    game_row = 0;
    auto_import = 0;
}

static void draw_bar(int y, long long done, long long total)
{
    char line[64];

    screen_box(20, y, SCREEN_WIDTH - 40, 10, 0);
    if (total > 0)
    {
        if (done > total)
            done = total;
        screen_box(22, y + 2, (int)((SCREEN_WIDTH - 44) * done / total), 6, 1);
        snprintf(line, sizeof(line), "%.1f / %.1f MB", done / MB, total / MB);
    }
    else
        snprintf(line, sizeof(line), "%.1f MB", done / MB);
    screen_text(20, y + 16, line, INK_RED, 1);
}

static void draw_progress(const import_status_t *status)
{
    char line[160];
    int y = 112;

    screen_text_center(20, auto_import ? "Adding your games" : "Downloading", INK_GOLD, 2);
    screen_text_fit(20, 52, SCREEN_WIDTH - 40, status->source, INK_WHITE, 0);
    if (status->source_count > 1)
    {
        snprintf(line, sizeof(line), "File %d of %d", status->source_index + 1, status->source_count);
        screen_text(20, 64, line, INK_GRAY, 1);
    }
    draw_bar(80, status->done, status->total);
    if (status->current[0])
    {
        snprintf(line, sizeof(line), "Writing %s", status->current);
        screen_text(20, y, line, INK_WHITE, 1);
        y += LINE_HEIGHT + 2;
    }
    if (status->found[0])
    {
        snprintf(line, sizeof(line), "Found: %s", status->found);
        screen_wrap(20, y, SCREEN_WIDTH - 40, line, INK_GREEN);
    }
    screen_text_center(FOOTER_Y, GLYPH_CIRCLE " cancel", INK_GRAY, 1);
}

static void import_view(void)
{
    import_status_t status;

    import_status(&status);
    if (status.state == IMPORT_LISTING)
    {
        browse = *import_listing();
        import_finish();
        browse_row = 0;
        view = VIEW_BROWSE;
        return;
    }
    if (status.state != IMPORT_RUNNING)
    {
        finish_import(&status);
        return;
    }
    if (pressed & PAD_CIRCLE)
        import_cancel();
    screen_background();
    draw_progress(&status);
}

// Reports what happened to a sent file, on the screen and on the page.
static void note(const char *text, int ok)
{
    plat_log("launcher: %s\n", text);
    upload_note(text, ok);
    if (send_line_count == SEND_LINES)
    {
        memmove(send_lines, send_lines + 1, sizeof(send_lines[0]) * (SEND_LINES - 1));
        memmove(send_line_ok, send_line_ok + 1, sizeof(send_line_ok[0]) * (SEND_LINES - 1));
        send_line_count--;
    }
    snprintf(send_lines[send_line_count], sizeof(send_lines[0]), "%s", text);
    send_line_ok[send_line_count++] = ok;
}

static void start_send(void)
{
    send_ready = upload_start(import_dir) == 0;
    send_added = 0;
    send_line_count = 0;
    send_archive[0] = 0;
    status_line[0] = 0;
    view = VIEW_SEND;
}

static void leave_send(void)
{
    if (send_archive[0])
    {
        import_cancel();
        import_finish();
        unlink(send_archive);
        send_archive[0] = 0;
    }
    upload_stop();
    if (send_added)
    {
        char text[64];

        snprintf(text, sizeof(text), "Added %d game%s", send_added, send_added == 1 ? "" : "s");
        set_status(text, INK_GREEN);
        game_row = 0;
    }
    view = send_added ? VIEW_GAMES : VIEW_ADD;
}

static void add_uploaded_wad(const char *path, const char *name)
{
    char target[GAME_PATH + 16], text[192];
    const char *file = games_known_file(name);
    const game_t *game;

    if (!file || games_check_wad(path, NULL))
    {
        snprintf(text, sizeof(text), file ? "%s is not a valid game WAD" : "%s is not a game Doom knows",
                 name);
        note(text, 0);
        unlink(path);
        return;
    }
    snprintf(target, sizeof(target), "%s/%s", import_dir, file);
    unlink(target);
    if (rename(path, target))
    {
        snprintf(text, sizeof(text), "Cannot store %s on the console", name);
        note(text, 0);
        unlink(path);
        return;
    }
    games_scan(&games);
    game = games_find(&games, file);
    snprintf(text, sizeof(text), "Added %s", game ? game->title : file);
    note(text, 1);
    send_added++;
}

// Takes the next file the upload server finished. Game WADs are moved into place; archives go
// through the importer under their own name and are deleted once read.
static void take_upload(void)
{
    char path[UPLOAD_PATH], name[UPLOAD_NAME], text[192], source[1][LINK_URL];
    uint8_t magic[4] = {0};
    int fd;

    if (send_archive[0] || !upload_take(path, sizeof(path), name, sizeof(name)))
        return;
    if ((fd = open(path, O_RDONLY)) >= 0)
    {
        if (read(fd, magic, sizeof(magic)) != (ssize_t)sizeof(magic))
            memset(magic, 0, sizeof(magic));
        close(fd);
    }
    if (!memcmp(magic, "IWAD", 4))
        add_uploaded_wad(path, name);
    else if (!memcmp(magic, "PWAD", 4))
    {
        snprintf(text, sizeof(text), "%s is an add-on (pwad), not a game", name);
        note(text, 0);
        unlink(path);
    }
    else
    {
        snprintf(send_archive, sizeof(send_archive), "%s/%s", import_dir, name);
        if (rename(path, send_archive))
        {
            snprintf(text, sizeof(text), "Cannot store %s on the console", name);
            note(text, 0);
            unlink(path);
            send_archive[0] = 0;
            return;
        }
        snprintf(source[0], LINK_URL, "%s", send_archive);
        import_start(source, 1, import_dir);
    }
}

static void check_archive_import(void)
{
    import_status_t status;
    char text[256];
    const char *name;

    if (!send_archive[0])
        return;
    import_status(&status);
    if (status.state == IMPORT_RUNNING)
        return;
    import_finish();
    name = strrchr(send_archive, '/') + 1;
    if (status.found_count)
        snprintf(text, sizeof(text), "Added %s from %s", status.found, name);
    else if (status.message[0])
        snprintf(text, sizeof(text), "%s", status.message);
    else
        snprintf(text, sizeof(text), "No game WADs found in %s", name);
    note(text, status.found_count > 0);
    send_added += status.found_count;
    unlink(send_archive);
    send_archive[0] = 0;
    games_scan(&games);
}

static void send_view(void)
{
    const char *address = upload_address();
    upload_progress_t progress;
    import_status_t status;
    char line[UPLOAD_NAME + 32];
    int y, x, size, i;

    take_upload();
    check_archive_import();
    if (pressed & PAD_CIRCLE)
    {
        leave_send();
        return;
    }
    upload_progress(&progress);

    screen_background();
    screen_text_center(6, "Send games", INK_GOLD, 2);
    if (!send_ready || !address[0])
        y = screen_wrap(16, 34, SCREEN_WIDTH - 32,
                        !send_ready ? "The console could not start receiving files. Go back and try again."
                                    : "The console has no network address. Connect it to your network, "
                                      "then open this screen again.",
                        INK_RED) + 8;
    else
    {
        size = screen_qr(16, 30, address, 3);
        x = 16 + size + 12;
        y = screen_wrap(x, 32, SCREEN_WIDTH - x - 12,
                        "On a PC or phone on the same network, open this address or scan the code:", INK_RED);
        screen_text_fit(x, y + 3, SCREEN_WIDTH - x - 12, address, INK_GOLD, 0);
        y = screen_wrap(x, y + 18, SCREEN_WIDTH - x - 12,
                        "Then drop your WAD, ZIP, 7Z or RAR files on the page.", INK_RED);
        y = y > 30 + size ? y + 8 : 30 + size + 8;
    }

    if (progress.receiving)
    {
        snprintf(line, sizeof(line), "Receiving %s", progress.name);
        screen_text_fit(20, y, SCREEN_WIDTH - 40, line, INK_WHITE, 0);
        draw_bar(y + 12, progress.done, progress.total);
    }
    else if (send_archive[0])
    {
        import_status(&status);
        snprintf(line, sizeof(line), "Adding games from %s", strrchr(send_archive, '/') + 1);
        screen_text_fit(20, y, SCREEN_WIDTH - 40, line, INK_WHITE, 0);
        draw_bar(y + 12, status.done, status.total);
    }
    else
        for (i = 0; i < send_line_count; i++)
            screen_text_fit(20, y + i * LINE_HEIGHT, SCREEN_WIDTH - 40, send_lines[i],
                            send_line_ok[i] ? INK_GREEN : INK_RED, 0);
    screen_text_center(FOOTER_Y, progress.receiving || send_archive[0] ? GLYPH_CIRCLE " stop and go back"
                                                                        : GLYPH_CIRCLE " back", INK_GRAY, 1);
}

static const game_t *preselected(void)
{
    const game_t *game = games_find(&games, test_plan_game());

    if (test_plan_game() && !game)
        plat_log("launcher: test plan game %s is not installed\n", test_plan_game());
    return game;
}

const game_t *launcher_choose(void)
{
    const game_t *assets, *game;
    int i;

    games_scan(&games);
    settings_load(&settings);
    if ((game = preselected()))
        return game;
    assets = games_find(&games, "doom1.wad");
    if (!assets && games.count)
        assets = &games.games[0];
    if (!assets || screen_init(assets->path) || plat_video_init(SCREEN_WIDTH, SCREEN_HEIGHT))
    {
        plat_alert("No game data found. Reinstall DOOM.");
        return NULL;
    }
    choose_import_dir();
    for (i = 0; i < games.count; i++)
        if (!strcasecmp(games.games[i].file, settings.game))
            game_row = i;
    view = VIEW_GAMES;
    start_auto_import();

    while (!chosen)
    {
        read_input();
        if (view != VIEW_IMPORT && view != VIEW_SEND && pressed)
            status_line[0] = 0;
        switch (view)
        {
        case VIEW_GAMES:
            games_view();
            break;
        case VIEW_ADD:
            add_view();
            break;
        case VIEW_NOTICE:
            notice_view();
            break;
        case VIEW_BROWSE:
            browse_view();
            break;
        case VIEW_IMPORT:
            import_view();
            break;
        case VIEW_SEND:
            send_view();
            break;
        }
        if (!chosen)
            present();
    }
    return chosen;
}
