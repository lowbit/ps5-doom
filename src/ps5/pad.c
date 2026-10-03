#include <string.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"
#include "test_plan.h"

static const struct
{
    uint32_t sce;
    uint32_t pad;
} button_map[] = {
    {SCE_PAD_UP, PAD_UP},         {SCE_PAD_DOWN, PAD_DOWN},
    {SCE_PAD_LEFT, PAD_LEFT},     {SCE_PAD_RIGHT, PAD_RIGHT},
    {SCE_PAD_CROSS, PAD_CROSS},   {SCE_PAD_CIRCLE, PAD_CIRCLE},
    {SCE_PAD_SQUARE, PAD_SQUARE}, {SCE_PAD_TRIANGLE, PAD_TRIANGLE},
    {SCE_PAD_L1, PAD_L1},         {SCE_PAD_R1, PAD_R1},
    {SCE_PAD_L2, PAD_L2},         {SCE_PAD_R2, PAD_R2},
    {SCE_PAD_L3, PAD_L3},         {SCE_PAD_R3, PAD_R3},
    {SCE_PAD_OPTIONS, PAD_OPTIONS}, {SCE_PAD_TOUCHPAD, PAD_TOUCHPAD},
};

static int handle = -1;

void ps5_pad_open(void)
{
    static const ScePadColor doom_red = {200, 0, 0, 255};

    if (scePadInit() < 0)
    {
        plat_log("ps5: pad init failed\n");
        return;
    }
    handle = scePadOpen(ps5_user(), SCE_PAD_PORT_STANDARD, 0, NULL);
    if (handle < 0)
    {
        plat_log("ps5: pad open failed %#x\n", handle);
        return;
    }
    scePadSetVibrationMode(handle, 2);
    scePadSetLightBar(handle, &doom_red);
    plat_log("pad: open\n");
}

void ps5_pad_close(void)
{
    if (handle < 0)
        return;
    plat_pad_rumble(0, 0);
    scePadClose(handle);
    handle = -1;
}

static int16_t stick(uint8_t raw)
{
    return (int16_t)((raw - 128) * 256);
}

int plat_pad_read(pad_state_t *state)
{
    ScePadData data;
    int connected;
    size_t i;

    memset(state, 0, sizeof(*state));
    connected = handle >= 0 && scePadReadState(handle, &data) >= 0 && data.connected;
    if (connected)
    {
        for (i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++)
            if (data.buttons & button_map[i].sce)
                state->buttons |= button_map[i].pad;
        state->lx = stick(data.lx);
        state->ly = stick(data.ly);
        state->rx = stick(data.rx);
        state->ry = stick(data.ry);
    }
    test_plan_apply(ps5_frame(), state);
    return connected || test_plan_has_input() ? 0 : -1;
}

void plat_pad_rumble(uint8_t strong, uint8_t weak)
{
    ScePadVibration vibration = {strong, weak};

    if (handle >= 0)
        scePadSetVibration(handle, &vibration);
}
