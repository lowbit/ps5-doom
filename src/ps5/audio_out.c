#include <pthread.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"

#define GRAIN 256

static int port = -1;
static pthread_t thread;
static volatile int running;
static audio_render_fn render;
static void *render_user;

static void *output_loop(void *arg)
{
    int16_t block[GRAIN * 2];

    (void)arg;
    while (running)
    {
        render(block, GRAIN, render_user);
        sceAudioOutOutput(port, block);
    }
    return NULL;
}

int plat_audio_init(int rate, audio_render_fn fn, void *user)
{
    sceAudioOutInit();
    port = sceAudioOutOpen(ps5_user(), SCE_AUDIO_PORT_MAIN, 0, GRAIN, rate, SCE_AUDIO_S16_STEREO);
    if (port < 0)
        port = sceAudioOutOpen(SCE_USER_SYSTEM, SCE_AUDIO_PORT_MAIN, 0, GRAIN, rate,
                               SCE_AUDIO_S16_STEREO);
    if (port < 0)
    {
        plat_log("ps5: audio open failed %#x\n", port);
        return -1;
    }

    render = fn;
    render_user = user;
    running = 1;
    plat_log("audio: output open at %d Hz\n", rate);
    if (pthread_create(&thread, NULL, output_loop, NULL))
    {
        running = 0;
        sceAudioOutClose(port);
        port = -1;
        return -1;
    }
    return 0;
}

void plat_audio_shutdown(void)
{
    if (!running)
        return;
    running = 0;
    pthread_join(thread, NULL);
    sceAudioOutClose(port);
    port = -1;
}
