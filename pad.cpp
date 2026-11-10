// ============================================================================
//  pad.cpp - Xbox-style gamepads, through Windows' XInput
//
//  XInput comes in a DLL that's part of Windows. We load it by name, so the
//  game still runs (with the keyboard) on a PC that hasn't got it.
//
//  D-pad or left stick: walk, and climb ropes     A: jump
//  Start: start / carry on (Enter)    B: pause (P)    Back: Esc
// ============================================================================

#include "cavern.h"

// The parts of XInput we use, written out here so no extra header is needed
typedef struct
{
    WORD  buttons;
    BYTE  leftTrigger, rightTrigger;
    SHORT thumbLX, thumbLY, thumbRX, thumbRY;
} PadButtons;

typedef struct
{
    DWORD      packet;
    PadButtons pad;
} PadState;

typedef DWORD (WINAPI *XInputGetStateFn)(DWORD user, PadState *state);

#define PAD_UP      0x0001
#define PAD_DOWN    0x0002
#define PAD_LEFT    0x0004
#define PAD_RIGHT   0x0008
#define PAD_START   0x0010
#define PAD_BACK    0x0020
#define PAD_A       0x1000
#define PAD_B       0x2000
#define STICK_DEAD  12000               // how far the stick must move to count

static XInputGetStateFn getState;
static BOOL  tried;                     // have we looked for the DLL yet?
static int   padUser = -1;              // which of the four pads is in use, or -1
static int   searchTimer;
static WORD  held, lastHeld;            // buttons (and stick directions) now and last frame

static void LoadXInput(void)
{
    static const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    tried = TRUE;
    for (int i = 0; i < 3 && getState == NULL; i++)
    {
        HMODULE dll = LoadLibrary(dlls[i]);
        if (dll)
            getState = (XInputGetStateFn)(void *)GetProcAddress(dll, "XInputGetState");
    }
}

void PadPoll(void)
{
    if (!tried)
        LoadXInput();
    lastHeld = held;
    held = 0;
    if (getState == NULL)
        return;

    // Asking about a pad that isn't there is slow, so only look for one
    // every couple of seconds until one turns up
    PadState ps;
    if (padUser < 0)
    {
        if (--searchTimer > 0)
            return;
        searchTimer = 120;
        for (int u = 0; u < 4 && padUser < 0; u++)
            if (getState(u, &ps) == ERROR_SUCCESS)
                padUser = u;
        if (padUser < 0)
            return;
    }

    if (getState(padUser, &ps) != ERROR_SUCCESS)
    {
        padUser = -1;                   // unplugged
        return;
    }

    // The stick counts as the d-pad
    held = ps.pad.buttons;
    if (ps.pad.thumbLX < -STICK_DEAD) held |= PAD_LEFT;
    if (ps.pad.thumbLX >  STICK_DEAD) held |= PAD_RIGHT;
    if (ps.pad.thumbLY >  STICK_DEAD) held |= PAD_UP;
    if (ps.pad.thumbLY < -STICK_DEAD) held |= PAD_DOWN;
}

// Is the pad doing what this key does?
BOOL PadHeld(int vk)
{
    switch (vk)
    {
    case VK_LEFT:  return (held & PAD_LEFT)  != 0;
    case VK_RIGHT: return (held & PAD_RIGHT) != 0;
    case VK_UP:    return (held & PAD_UP)    != 0;
    case VK_DOWN:  return (held & PAD_DOWN)  != 0;
    case VK_SPACE: return (held & PAD_A)     != 0;
    }
    return FALSE;
}

BOOL PadAnyPressed(void)
{
    return (held & ~lastHeld) != 0;
}

// A key for a button pressed since last frame, or 0
int PadPressed(void)
{
    WORD pressed = held & ~lastHeld;
    if (pressed & PAD_START) return VK_RETURN;
    if (pressed & PAD_BACK)  return VK_ESCAPE;
    if (pressed & PAD_B)     return 'P';
    return 0;
}
