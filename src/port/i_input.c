#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_items.h"
#include "d_main.h"
#include "i_input.h"

#include "platform.h"

extern int key_right, key_left, key_up, key_down;
extern int key_fire, key_use, key_strafe, key_speed;
extern int mouseSensitivity;
extern int messageToPrint;
extern boolean messageNeedsInput;
extern int saveStringEnter;

#define STICK_DEADZONE 0.2f
#define STICK_AS_DPAD 0.5f
#define FORWARD_MAX 50
#define SIDE_MAX 40
#define TURN_MIN 640
#define TURN_PER_SENSITIVITY 128
#define BFG_CELLS 40
#define REPEAT_DELAY_US 350000
#define REPEAT_RATE_US 80000
#define RUMBLE_US 200000

#define DPAD (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

typedef enum
{
    CONTEXT_MENU,
    CONTEXT_GAME,
    CONTEXT_MAP
} context_t;

static pad_state_t pad;
static uint32_t held;
static int sent_key[32];
static uint64_t next_repeat;
static int weapon_step;
static uint64_t rumble_until;
static ticcmd_t basecmd;

static const weapontype_t weapon_order[] = {
    wp_fist, wp_chainsaw, wp_pistol, wp_shotgun, wp_supershotgun,
    wp_chaingun, wp_missile, wp_plasma, wp_bfg,
};

#define NUM_ORDERED (int)(sizeof(weapon_order) / sizeof(weapon_order[0]))

static context_t current_context(void)
{
    if (menuactive || messageToPrint)
        return CONTEXT_MENU;
    if (gamestate == GS_LEVEL && automapactive && !demoplayback)
        return CONTEXT_MAP;
    return CONTEXT_GAME;
}

static int menu_key(uint32_t button)
{
    int answer = messageToPrint && messageNeedsInput;

    switch (button)
    {
    case PAD_UP: return KEY_UPARROW;
    case PAD_DOWN: return KEY_DOWNARROW;
    case PAD_LEFT: return KEY_LEFTARROW;
    case PAD_RIGHT: return KEY_RIGHTARROW;
    case PAD_CROSS: return answer ? 'y' : KEY_ENTER;
    case PAD_CIRCLE: return answer ? 'n' : saveStringEnter ? KEY_ESCAPE : KEY_BACKSPACE;
    case PAD_OPTIONS: return KEY_ESCAPE;
    default: return 0;
    }
}

static int game_key(uint32_t button, context_t context)
{
    switch (button)
    {
    case PAD_UP: return key_up;
    case PAD_DOWN: return key_down;
    case PAD_LEFT: return key_left;
    case PAD_RIGHT: return key_right;
    case PAD_R2: return key_fire;
    case PAD_CROSS: return key_use;
    case PAD_SQUARE: return context == CONTEXT_MAP ? 'f' : key_use;
    case PAD_CIRCLE: return key_strafe;
    case PAD_L2: return key_speed;
    case PAD_TRIANGLE:
    case PAD_TOUCHPAD: return KEY_TAB;
    case PAD_OPTIONS: return KEY_ESCAPE;
    case PAD_R1: return context == CONTEXT_MAP ? '=' : 0;
    case PAD_L1: return context == CONTEXT_MAP ? '-' : 0;
    default: return 0;
    }
}

static void post_key(evtype_t type, int key)
{
    event_t event = {type, key, 0, 0};

    D_PostEvent(&event);
}

static float axis(int16_t raw)
{
    float value = raw / 32767.0f;
    float magnitude = value < 0 ? -value : value;

    if (magnitude < STICK_DEADZONE)
        return 0;
    if (magnitude > 1)
        magnitude = 1;
    magnitude = (magnitude - STICK_DEADZONE) / (1 - STICK_DEADZONE);
    return value < 0 ? -magnitude : magnitude;
}

static uint32_t stick_as_dpad(void)
{
    float x = axis(pad.lx), y = axis(pad.ly);
    uint32_t buttons = 0;

    if (y < -STICK_AS_DPAD)
        buttons |= PAD_UP;
    if (y > STICK_AS_DPAD)
        buttons |= PAD_DOWN;
    if (x < -STICK_AS_DPAD)
        buttons |= PAD_LEFT;
    if (x > STICK_AS_DPAD)
        buttons |= PAD_RIGHT;
    return buttons;
}

static void press(uint32_t button, int bit, context_t context, uint64_t now)
{
    int key;

    if (context != CONTEXT_MENU)
    {
        if (button == PAD_R1)
            weapon_step = context == CONTEXT_GAME ? 1 : 0;
        if (button == PAD_L1)
            weapon_step = context == CONTEXT_GAME ? -1 : 0;
    }

    key = context == CONTEXT_MENU ? menu_key(button) : game_key(button, context);
    sent_key[bit] = key;
    if (key)
        post_key(ev_keydown, key);
    if (context == CONTEXT_MENU && (button & DPAD))
        next_repeat = now + REPEAT_DELAY_US;
}

static void release(int bit)
{
    if (sent_key[bit])
        post_key(ev_keyup, sent_key[bit]);
    sent_key[bit] = 0;
}

static void repeat_menu_keys(uint32_t buttons, uint64_t now)
{
    int bit;

    if (!(buttons & DPAD) || now < next_repeat)
        return;
    for (bit = 0; bit < 4; bit++)
        if ((buttons & (1u << bit)) && sent_key[bit])
            post_key(ev_keydown, sent_key[bit]);
    next_repeat = now + REPEAT_RATE_US;
}

void I_PollInput(void)
{
    context_t context = current_context();
    uint64_t now = plat_ticks_us();
    uint32_t buttons, changed;
    int bit;

    if (plat_pad_read(&pad))
        memset(&pad, 0, sizeof(pad));

    buttons = pad.buttons;
    if (context == CONTEXT_MENU)
        buttons |= stick_as_dpad();

    changed = buttons ^ held;
    for (bit = 0; bit < 32; bit++)
    {
        uint32_t button = 1u << bit;

        if (!(changed & button))
            continue;
        if (buttons & button)
            press(button, bit, context, now);
        else
            release(bit);
    }
    held = buttons;

    if (context == CONTEXT_MENU)
        repeat_menu_keys(buttons, now);

    if (rumble_until && now >= rumble_until)
    {
        plat_pad_rumble(0, 0);
        rumble_until = 0;
    }
}

static int usable(const player_t *player, weapontype_t weapon)
{
    ammotype_t ammo = weaponinfo[weapon].ammo;
    int needed = weapon == wp_bfg ? BFG_CELLS : weapon == wp_supershotgun ? 2 : 1;

    if (!player->weaponowned[weapon])
        return 0;
    if (weapon == wp_supershotgun && gamemode != commercial)
        return 0;
    if ((weapon == wp_plasma || weapon == wp_bfg) && gamemode == shareware)
        return 0;
    return ammo == am_noammo || player->ammo[ammo] >= needed;
}

static int cycle_weapon(int step)
{
    const player_t *player = &players[consoleplayer];
    weapontype_t current = player->pendingweapon != wp_nochange
                               ? player->pendingweapon
                               : player->readyweapon;
    int index = 0, tries;

    while (index < NUM_ORDERED && weapon_order[index] != current)
        index++;

    for (tries = 1; tries < NUM_ORDERED; tries++)
    {
        weapontype_t weapon =
            weapon_order[(index + step * tries + NUM_ORDERED * tries) % NUM_ORDERED];

        if (usable(player, weapon))
            return weapon == wp_supershotgun ? wp_shotgun : weapon;
    }
    return -1;
}

ticcmd_t *I_InputTiccmd(void)
{
    float turn;
    int slot;

    memset(&basecmd, 0, sizeof(basecmd));
    if (menuactive || gamestate != GS_LEVEL || demoplayback)
    {
        weapon_step = 0;
        return &basecmd;
    }

    turn = axis(pad.rx);
    turn *= turn < 0 ? -turn : turn;
    basecmd.forwardmove = (signed char)(-axis(pad.ly) * FORWARD_MAX);
    basecmd.sidemove = (signed char)(axis(pad.lx) * SIDE_MAX);
    basecmd.angleturn = (short)(-turn * (TURN_MIN + mouseSensitivity * TURN_PER_SENSITIVITY));

    if (weapon_step)
    {
        slot = cycle_weapon(weapon_step);
        if (slot >= 0)
            basecmd.buttons |= BT_CHANGE | slot << BT_WEAPONSHIFT;
        weapon_step = 0;
    }
    return &basecmd;
}

void I_InputRumble(int strength)
{
    int strong = strength > 255 ? 255 : strength;

    plat_pad_rumble((uint8_t)strong, (uint8_t)(strong / 2));
    rumble_until = plat_ticks_us() + RUMBLE_US;
}
