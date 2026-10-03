#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_plan.h"

#define MAX_STEPS 128
#define MAX_CAPTURES 64

typedef struct
{
    int start, count;
    uint32_t buttons;
    int16_t lx, ly, rx, ry;
} step_t;

static const struct
{
    const char *name;
    uint32_t bit;
} names[] = {
    {"UP", PAD_UP}, {"DOWN", PAD_DOWN}, {"LEFT", PAD_LEFT}, {"RIGHT", PAD_RIGHT},
    {"CROSS", PAD_CROSS}, {"CIRCLE", PAD_CIRCLE}, {"SQUARE", PAD_SQUARE},
    {"TRIANGLE", PAD_TRIANGLE}, {"L1", PAD_L1}, {"R1", PAD_R1}, {"L2", PAD_L2},
    {"R2", PAD_R2}, {"L3", PAD_L3}, {"R3", PAD_R3}, {"OPTIONS", PAD_OPTIONS},
    {"TOUCHPAD", PAD_TOUCHPAD},
};

static step_t steps[MAX_STEPS];
static int step_count;
static int captures[MAX_CAPTURES];
static int capture_count;
static int frame_limit;
static char game[64];
static char text[512];

static void parse_token(step_t *step, const char *token)
{
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(token, names[i].name))
            step->buttons |= names[i].bit;
    if (!strncmp(token, "LX=", 3))
        step->lx = (int16_t)atoi(token + 3);
    if (!strncmp(token, "LY=", 3))
        step->ly = (int16_t)atoi(token + 3);
    if (!strncmp(token, "RX=", 3))
        step->rx = (int16_t)atoi(token + 3);
    if (!strncmp(token, "RY=", 3))
        step->ry = (int16_t)atoi(token + 3);
}

void test_plan_input(const char *text)
{
    char *copy, *entry, *save_entry;

    step_count = 0;
    if (!text || !(copy = strdup(text)))
        return;
    for (entry = strtok_r(copy, ";", &save_entry); entry && step_count < MAX_STEPS;
         entry = strtok_r(NULL, ";", &save_entry))
    {
        step_t *step = &steps[step_count];
        char *field, *save_field;
        int index = 0;

        memset(step, 0, sizeof(*step));
        for (field = strtok_r(entry, ":", &save_field); field;
             field = strtok_r(NULL, ":", &save_field), index++)
        {
            if (index == 0)
                step->start = atoi(field);
            else if (index == 1)
            {
                char *token, *save_token;

                for (token = strtok_r(field, "+", &save_token); token;
                     token = strtok_r(NULL, "+", &save_token))
                    parse_token(step, token);
            }
            else
                step->count = atoi(field);
        }
        if (!step->count)
            step->count = 1;
        step_count++;
    }
    free(copy);
}

int test_plan_has_input(void)
{
    return step_count > 0;
}

void test_plan_apply(int frame, pad_state_t *state)
{
    int i;

    for (i = 0; i < step_count; i++)
    {
        const step_t *step = &steps[i];

        if (frame < step->start || frame >= step->start + step->count)
            continue;
        state->buttons |= step->buttons;
        if (step->lx)
            state->lx = step->lx;
        if (step->ly)
            state->ly = step->ly;
        if (step->rx)
            state->rx = step->rx;
        if (step->ry)
            state->ry = step->ry;
    }
}

void test_plan_captures(const char *frames)
{
    capture_count = 0;
    while (frames && *frames && capture_count < MAX_CAPTURES)
    {
        captures[capture_count++] = atoi(frames);
        frames = strchr(frames, ',');
        if (frames)
            frames++;
    }
}

int test_plan_captures_frame(int frame)
{
    int i;

    for (i = 0; i < capture_count; i++)
        if (captures[i] == frame)
            return 1;
    return 0;
}

void test_plan_limit(int frames)
{
    frame_limit = frames;
}

int test_plan_finished(int frame)
{
    return frame_limit && frame >= frame_limit;
}

void test_plan_set_game(const char *file)
{
    snprintf(game, sizeof(game), "%s", file ? file : "");
}

const char *test_plan_game(void)
{
    return game[0] ? game : NULL;
}

void test_plan_set_text(const char *value)
{
    snprintf(text, sizeof(text), "%s", value ? value : "");
}

const char *test_plan_text(void)
{
    return text[0] ? text : NULL;
}
