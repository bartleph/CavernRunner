// ============================================================================
//  demo.cpp - the game playing itself, when the title screen is left alone
//
//  It picks a cavern, asks the checker to plan a route (the shortest set of
//  moves to each key in turn, then the exit), and then plays those moves
//  through the real game, pressing pretend keys. Because the game's physics
//  always does the same thing for the same keys, the player ends up exactly
//  where the plan says - unless something gets in the way.
//
//  The planner doesn't know about monsters, so before each move the demo
//  rehearses the next few moves in a scratch copy of the game, monsters and
//  all, and waits if they'd end badly. It doesn't always manage - which
//  makes it look like a real player.
// ============================================================================

#include "cavern.h"

#define MAX_ROUTE   3000
#define MAX_WAIT    240                 // longest wait for a monster (frames)

BOOL demoMode;
unsigned char demoKeys[256];            // what the demo is "pressing"
const char   *demoEnd = "";             // why the last demo ended (handy when testing)

static DemoStep route[MAX_ROUTE];
static int      routeLen, routePos;
static int      stepFrame;              // frames into the current move
static int      waitFrames;             // frames spent waiting for monsters
static BOOL     waiting;

// Caverns the demo can play: no moving platforms or doors, which the
// planner treats more simply than the game does
static BOOL DemoCanPlay(int n)
{
    return CavernProblem(n) == NULL && !CavernHasTile(&caverns[n], '~') &&
           !CavernHasTile(&caverns[n], 'D') && CavernHasTile(&caverns[n], 'k');
}

BOOL StartDemo(void)
{
    int list[MAX_CAVERNS], count = 0;
    for (int i = 0; i < numCaverns; i++)
        if (DemoCanPlay(i))
            list[count++] = i;

    // Try them in a random order until one has a route
    while (count > 0)
    {
        int pick = rand() % count;
        int n = list[pick];
        list[pick] = list[--count];

        cavNum = n;
        LoadLevel(n);
        routeLen = PlanRoute(route, MAX_ROUTE);
        if (routeLen > 0)
        {
            testMode  = FALSE;
            LoadLevel(n);               // a fresh start
            demoMode  = TRUE;
            bossState = BOSS_NONE;      // no boss in the demo
            score     = 0;
            lives     = 1;
            routePos  = stepFrame = waitFrames = 0;
            waiting   = FALSE;
            return TRUE;
        }
    }
    ShowTitle();                        // nothing the demo can play
    return FALSE;
}

static void EndDemo(const char *why)
{
    demoEnd = why;
    StopDemo();
}

void StopDemo(void)
{
    demoMode = FALSE;
    memset(demoKeys, 0, sizeof demoKeys);
    StopSounds();
    ShowTitle();
}

static int HoldFrames(const DemoStep *st)
{
    return (st->rope && st->move <= 1) ? 8 : 1;     // climbs last 8 frames
}

// Press the keys for frame 'f' of a move
static void SetMoveKeys(const DemoStep *st, int f)
{
    memset(demoKeys, 0, sizeof demoKeys);
    if (f >= HoldFrames(st))
        return;
    if (!st->rope)
    {
        if (st->move == 0 || st->move == 3) demoKeys[VK_LEFT]  = 1;
        if (st->move == 1 || st->move == 4) demoKeys[VK_RIGHT] = 1;
        if (st->move >= 2)                  demoKeys[VK_SPACE] = 1;
    }
    else
    {
        if (st->move == 0) demoKeys[VK_UP]    = 1;
        if (st->move == 1) demoKeys[VK_DOWN]  = 1;
        if (st->move >= 2) demoKeys[VK_SPACE] = 1;
        if (st->move == 3) demoKeys[VK_LEFT]  = 1;
        if (st->move == 4) demoKeys[VK_RIGHT] = 1;
    }
}

// Has the move finished after frame 'f'? (The same rule the planner uses.)
static BOOL MoveDone(const DemoStep *st, int f)
{
    BOOL justJumped = (!st->rope && st->move >= 2 && f == 0);
    return f >= HoldFrames(st) - 1 && (onGround || climbing) && !justJumped;
}

// Rehearse: play the next few moves of the route (or, with moves = 0, just
// stand still for a while) in a scratch copy of the game, monsters and all.
// Would the player die?
static BOOL WouldDie(int moves, int idleFrames)
{
    SnapshotSave();
    simulating = TRUE;                  // no sounds

    BOOL died = FALSE;
    int  pos = routePos, f = 0, frames = 0;
    while (frames < 300 && state == ST_PLAYING)
    {
        if (moves == 0)
        {
            if (frames >= idleFrames)
                break;
            memset(demoKeys, 0, sizeof demoKeys);
        }
        else
        {
            if (pos >= routeLen || pos >= routePos + moves)
                break;
            SetMoveKeys(&route[pos], f);
        }
        SimulateFrame();
        frames++;
        if (moves > 0)
        {
            if (MoveDone(&route[pos], f))
            {
                pos++;
                f = 0;
            }
            else
                f++;
        }
    }
    died = (state != ST_PLAYING);

    simulating = FALSE;
    SnapshotRestore();
    memset(demoKeys, 0, sizeof demoKeys);
    return died;
}

// Standing still somewhere that moves or wears away?
static BOOL BadPlaceToWait(void)
{
    if (climbing)
        return FALSE;
    int ty = ((int)py + PLAYER_H) / TILE;
    for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
    {
        char c = TileAt(tx, ty);
        if (c == '<' || c == '>' || c == '-')
            return TRUE;
    }
    return FALSE;
}

// Set the pretend keys for this frame
void DemoInput(void)
{
    memset(demoKeys, 0, sizeof demoKeys);
    if (routePos >= routeLen)
        return;

    const DemoStep *st = &route[routePos];

    // Wait until we've landed at the start of the cavern
    waiting = FALSE;
    if (routePos == 0 && stepFrame == 0 && !onGround && !climbing)
    {
        waiting = TRUE;
        return;
    }

    // At the start of a move, rehearse the next few moves. If they'd end in
    // death, and standing still a while is safe, wait (but not for ever).
    if (stepFrame == 0 && waitFrames < MAX_WAIT && !BadPlaceToWait() &&
        WouldDie(4, 0) && !WouldDie(0, 20))
    {
        waitFrames++;
        waiting = TRUE;
        return;
    }

    SetMoveKeys(st, stepFrame);
}

// After the player has moved: has this move finished?
void DemoAfter(void)
{
    if (waiting || routePos >= routeLen || state != ST_PLAYING)
        return;

    const DemoStep *st = &route[routePos];
    if (MoveDone(st, stepFrame))
    {
        // Did we end up where the plan said? If not (a floor crumbled under
        // us, say), the rest of the plan is no use: end the demo.
        if (px != st->after.px || (int)py != (int)st->after.py || climbing != st->after.climbing)
        {
            EndDemo("went off the planned route");
            return;
        }
        routePos++;
        stepFrame  = 0;
        waitFrames = 0;
    }
    else if (++stepFrame > 400)
        EndDemo("a move never finished");
}

