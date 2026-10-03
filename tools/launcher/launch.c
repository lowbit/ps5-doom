#include <stdint.h>
#include <stdio.h>

#define TITLE_ID "PPSA99666"

typedef struct
{
    uint32_t size;
    int32_t user;
    uint32_t options;
    uint64_t crash_report;
    uint32_t check_flag;
} launch_params_t;

int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user);
int sceUserServiceGetInitialUser(int *user);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceKillApp(int app, int how, int reason, int core_dump);
int sceSystemServiceLaunchApp(const char *title, char **argv, launch_params_t *params);
int sceKernelDebugOutText(int channel, const char *text);
int sceKernelUsleep(uint32_t microseconds);

static void report(const char *step, int result)
{
    char line[128];

    snprintf(line, sizeof(line), "doomlaunch: %s %#x\n", step, result);
    sceKernelDebugOutText(0, line);
}

int main(void)
{
    launch_params_t params = {sizeof(params), 0, 0, 0, 0};
    int app, result;

    report("user service", sceUserServiceInitialize(NULL));
    result = sceUserServiceGetForegroundUser(&params.user);
    if (result)
        result = sceUserServiceGetInitialUser(&params.user);
    report("user", result ? result : params.user);

    app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app > 0)
    {
        report("kill running app", sceSystemServiceKillApp(app, -1, 0, 0));
        sceKernelUsleep(3000000);
    }
    report("launch " TITLE_ID, sceSystemServiceLaunchApp(TITLE_ID, NULL, &params));
    return 0;
}
