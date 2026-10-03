#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"
#include "test_plan.h"

#define SYSMODULE_IME_DIALOG 0x0096
#define COMMON_DIALOG_READY 0x80b80002u
#define IME_TYPE_URL 2
#define IME_ENTER_GO 3
#define IME_NO_AUTO_CAPITALS 0x0002
#define IME_ALIGN_CENTER 1
#define IME_RUNNING 1
#define IME_FINISHED 2
#define IME_ACCEPTED 0
#define MAX_TEXT 255
#define START_GRACE_US 1000000

static uint16_t buffer[MAX_TEXT + 1];
static uint16_t title_text[64];
static int loaded, forced;
static uint64_t opened_at;

static void to_utf16(uint16_t *out, int capacity, const char *text)
{
    int i;

    for (i = 0; text[i] && i < capacity - 1; i++)
        out[i] = (uint8_t)text[i];
    out[i] = 0;
}

static int load(void)
{
    int result;

    if (loaded)
        return 0;
    result = sceCommonDialogInitialize();
    if (result < 0 && (uint32_t)result != COMMON_DIALOG_READY)
    {
        plat_log("text: common dialog unavailable (0x%08x)\n", (unsigned)result);
        return -1;
    }
    result = sceSysmoduleLoadModule(SYSMODULE_IME_DIALOG);
    if (result < 0)
    {
        plat_log("text: keyboard module unavailable (0x%08x)\n", (unsigned)result);
        return -1;
    }
    loaded = 1;
    return 0;
}

int plat_text_open(const char *title, const char *text)
{
    SceImeDialogParam param;
    int user = ps5_user(), result;

    if (test_plan_text())
    {
        forced = 1;
        return 0;
    }
    if (load())
        return -1;
    sceUserServiceGetForegroundUser(&user);
    to_utf16(buffer, MAX_TEXT + 1, text);
    to_utf16(title_text, 64, title);

    memset(&param, 0, sizeof(param));
    param.user = user;
    param.type = IME_TYPE_URL;
    param.enter_label = IME_ENTER_GO;
    param.option = IME_NO_AUTO_CAPITALS;
    param.max_length = MAX_TEXT;
    param.text = buffer;
    param.horizontal_alignment = IME_ALIGN_CENTER;
    param.vertical_alignment = IME_ALIGN_CENTER;
    param.title = title_text;
    result = sceImeDialogInit(&param, NULL);
    if (result < 0)
    {
        plat_log("text: keyboard did not open (0x%08x)\n", (unsigned)result);
        return -1;
    }
    opened_at = plat_ticks_us();
    return 0;
}

int plat_text_poll(char *text, int size)
{
    SceImeDialogResult result;
    int status, i;

    if (forced)
    {
        forced = 0;
        strncpy(text, test_plan_text(), size - 1);
        text[size - 1] = 0;
        return 1;
    }
    status = sceImeDialogGetStatus();
    if (status == IME_RUNNING || (status != IME_FINISHED && plat_ticks_us() - opened_at < START_GRACE_US))
        return 0;

    memset(&result, 0, sizeof(result));
    if (status != IME_FINISHED || sceImeDialogGetResult(&result) < 0 || result.outcome != IME_ACCEPTED)
    {
        sceImeDialogTerm();
        return -1;
    }
    for (i = 0; buffer[i] && i < size - 1; i++)
        text[i] = buffer[i] < 0x80 ? (char)buffer[i] : '?';
    text[i] = 0;
    sceImeDialogTerm();
    return 1;
}
