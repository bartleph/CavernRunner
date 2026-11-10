// ============================================================================
//  game.cpp - the rules of the game
//
//  Update() runs 60 times a second. It moves everything one frame along:
//  the player, the monsters, the moving platforms and the boss, and checks
//  what the player is touching.
// ============================================================================

#include "cavern.h"

Cavern       *cav;
int           cavNum;
char          map[MAP_H][MAP_W + 1];
unsigned char crumble[MAP_H][MAP_W];

int   px;
float py, vy;
BOOL  onGround, climbing;
int   airDir;                           // direction locked in while in the air
int   facing = 1;                       // 1 = right, -1 = left
int   walkAnim;                         // counts up while walking or climbing

Enemy enemies[MAX_ENEMIES];
int   numEnemies;
Mover movers[MAX_MOVERS];
int   numMovers;

int   exitX, exitY, keysLeft, keysTotal, air;
int   score, lives;
BOOL  switchOn;                         // has a switch been touched (doors open)?
int   state, stateTimer, frameCount;
float camX;
BOOL  paused, active, testMode;
int   bossState;
float bossX, bossY, bossVX;
int   savedTimer;
int   titlePage;

char     message[64];
COLORREF messageColour;
int      messageTimer;

const unsigned char *simKeys;
BOOL  simulating;

static int   startX, startY;            // player start, in pixels
static int   standingOn = -1;           // the moving platform underfoot, or -1
static int   grabDelay;                 // frames before a rope can be grabbed again
static int   nextLife;                  // score for the next extra life
static int   bossTimer;
static float titlePan = 0.7f;           // camera drift speed on the title screen

// ============================================================================
//  Small helpers
// ============================================================================

void SetState(int s)
{
    state      = s;
    stateTimer = 0;
}

void ShowMessage(const char *text, COLORREF colour, int frames)
{
    lstrcpyn(message, text, sizeof message);
    messageColour = colour;
    messageTimer  = frames;
}

char TileAt(int tx, int ty)
{
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H)
        return '#';                     // outside the map counts as wall
    return map[ty][tx];
}

// Walls block from every side. A closed door is a wall until a switch opens it.
BOOL IsWall(char c)  { return c == '#' || c == 'D'; }

// Floors can be stood on (and jumped up through, unless they're walls)
BOOL IsFloor(char c) { return IsWall(c) || c == '=' || c == '-' || c == '<' || c == '>'; }

static BOOL IsRopeAt(int x, int y)
{
    return TileAt(x / TILE, y / TILE) == '|';
}

// Does a box overlap any wall tile?
static BOOL BoxHitsWall(int x, int y, int w, int h)
{
    for (int ty = y / TILE; ty <= (y + h - 1) / TILE; ty++)
        for (int tx = x / TILE; tx <= (x + w - 1) / TILE; tx++)
            if (IsWall(TileAt(tx, ty)))
                return TRUE;
    return FALSE;
}

static BOOL BoxesOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// Is this key held? (Or the gamepad doing the same thing, or - while the
// cavern checker is running - its pretend keyboard.)
BOOL KeyDown(int vk)
{
    if (simKeys)
        return simKeys[vk & 255] != 0;
    if (demoMode)
    {
        extern unsigned char demoKeys[256];
        return demoKeys[vk & 255] != 0;
    }
    return active && ((GetAsyncKeyState(vk) & 0x8000) || PadHeld(vk));
}

void GetPlayerState(PlayerState *s)
{
    s->px = px;               s->py = py;               s->vy = vy;
    s->onGround = onGround;   s->climbing = climbing;   s->airDir = airDir;
    s->facing = facing;       s->grabDelay = grabDelay; s->standingOn = standingOn;
}

void SetPlayerState(const PlayerState *s)
{
    px = s->px;               py = s->py;               vy = s->vy;
    onGround = s->onGround;   climbing = s->climbing;   airDir = s->airDir;
    facing = s->facing;       grabDelay = s->grabDelay; standingOn = s->standingOn;
}

float MaxCamera(void)
{
    return (float)(MAP_W * TILE - VIEW_W);
}

// Points, with an extra life every EXTRA_LIFE points
static void AddPoints(int n)
{
    score += n;
    while (!testMode && score >= nextLife)
    {
        nextLife += EXTRA_LIFE;
        if (lives < MAX_LIVES)
        {
            lives++;
            Play(SND_EXTRA);
            ShowMessage("EXTRA LIFE!", RGB(120, 255, 255), 120);
        }
    }
}

// ============================================================================
//  Starting a cavern, a game, the title screen
// ============================================================================

// Set an enemy off according to how its kind moves
static void StartEnemy(Enemy *e, int n)
{
    float dir = (n % 2) ? 1.0f : -1.0f;
    e->vx = e->vy = 0;
    switch (cav->move[e->kind])
    {
    case MOVE_WALK:   e->vx = 1.5f * dir;                  break;
    case MOVE_RUN:    e->vx = 2.6f * dir;                  break;
    case MOVE_LIFT:   e->vy = -1.2f;                       break;
    case MOVE_FLY:    e->vx = 1.8f * dir;                  break;
    case MOVE_BOUNCE: e->vx = 1.4f * dir; e->vy = 1.4f;    break;
    }
}

void LoadLevel(int n)
{
    cavNum     = n;
    cav        = &caverns[n];
    numEnemies = 0;
    numMovers  = 0;
    keysTotal  = 0;

    for (int ty = 0; ty < MAP_H; ty++)
    {
        for (int tx = 0; tx < MAP_W; tx++)
        {
            char c = cav->rows[ty][tx];
            crumble[ty][tx] = 0;

            switch (c)
            {
            case 'P':                           // player start
                startX = tx * TILE + (TILE - PLAYER_W) / 2;
                startY = (ty + 1) * TILE - PLAYER_H;
                c = ' ';
                break;

            case 'X':                           // exit door, two tiles tall
                exitX = tx * TILE;
                exitY = ty * TILE;
                c = ' ';
                break;

            case 'E':                           // monsters, feet on the tile below
            case 'F':
            case 'G':
                if (numEnemies < MAX_ENEMIES)
                {
                    Enemy *e = &enemies[numEnemies++];
                    e->kind = c - 'E';
                    e->x    = (float)(tx * TILE + (TILE - ENEMY_W) / 2);
                    e->y    = (float)((ty + 1) * TILE - ENEMY_H);
                    StartEnemy(e, numEnemies);
                }
                c = ' ';
                break;

            case '~':                           // a run of ~ is one moving platform
                if ((tx == 0 || cav->rows[ty][tx - 1] != '~') && numMovers < MAX_MOVERS)
                {
                    Mover *m = &movers[numMovers++];
                    int len = 0;
                    while (tx + len < MAP_W && cav->rows[ty][tx + len] == '~')
                        len++;
                    m->x  = (float)(tx * TILE);
                    m->y  = ty * TILE;
                    m->w  = len * TILE;
                    m->vx = (numMovers % 2) ? MOVER_SPEED : -MOVER_SPEED;
                    m->dx = 0;
                }
                c = ' ';
                break;

            case 'k':
                keysTotal++;                    // keys stay in the map until taken
                break;
            }
            map[ty][tx] = c;
        }
        map[ty][MAP_W] = 0;
    }

    keysLeft   = keysTotal;
    px         = startX;
    py         = (float)startY;
    vy         = 0;
    onGround   = FALSE;                 // drop onto the floor first
    climbing   = FALSE;
    standingOn = -1;
    grabDelay  = 0;
    airDir     = 0;
    facing     = 1;
    air        = AIR_MAX;
    switchOn   = FALSE;
    camX       = 0;
    paused     = FALSE;
    messageTimer = 0;
    SetState(ST_PLAYING);

    // Will the boss turn up this time? Half the time, 15 to 45 seconds in.
    // When testing from the editor it always comes, after 5 seconds.
    bossState = BOSS_NONE;
    if (cav->boss && testMode)
    {
        bossState = BOSS_WAITING;
        bossTimer = 60 * 5;
    }
    else if (cav->boss && rand() % 2 == 0)
    {
        bossState = BOSS_WAITING;
        bossTimer = 60 * (15 + rand() % 31);
    }
}

void ShowTitle(void)
{
    int first = NextPlayable(0);
    testMode = FALSE;
    LoadLevel(first >= 0 ? first : 0);  // a cavern scrolls past behind the title
    SetState(ST_TITLE);
}

void NewGame(void)
{
    int first = NextPlayable(0);
    if (first < 0)
    {
        MessageBox(hwndMain, "None of the caverns can be played yet.\n"
                             "Press E to fix them in the editor.",
                   "Cavern Runner", MB_ICONWARNING);
        return;
    }
    score    = 0;
    lives    = START_LIVES;
    nextLife = EXTRA_LIFE;
    testMode = FALSE;
    LoadLevel(first);
    Play(SND_START);
}

// The game is over (or abandoned): offer the high-score table
void EndGame(void)
{
    static char name[16] = "";          // remembered, to save typing next time
    StopSounds();

    if (!testMode && ScoreQualifies(score))
    {
        char prompt[80];
        wsprintf(prompt, "A high score: %d!  Type your name:", score);
        if (InputBox(hwndMain, "High Score", prompt, name, sizeof name))
            AddScore(score, name);
        ShowTitle();
        titlePage = 1;                  // show the table straight away
        return;
    }
    ShowTitle();
}

// ============================================================================
//  The player
// ============================================================================

static void KillPlayer(void)
{
    if (state != ST_PLAYING)            // spikes and a monster in the same frame
        return;                         // only count once
    Play(SND_DIE);
    SetState(ST_DYING);
}

static void OpenDoors(void)
{
    switchOn = TRUE;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (map[ty][tx] == 'D')
                map[ty][tx] = ' ';
    Play(SND_SWITCH);
    ShowMessage("THE DOORS OPEN", RGB(255, 230, 80), 90);
}

// Keys, spikes, plants and switches in any tile the player overlaps
static void CheckTouches(void)
{
    for (int ty = (int)py / TILE; ty <= ((int)py + PLAYER_H - 1) / TILE; ty++)
    {
        for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
        {
            char c = TileAt(tx, ty);
            switch (c)
            {
            case 'k':
                map[ty][tx] = ' ';
                keysLeft--;
                AddPoints(100);
                Play(keysLeft == 0 ? SND_OPEN : SND_KEY);   // last key: the exit opens
                break;

            case '^':                   // only the pointed lower part is deadly
                if (BoxesOverlap(px, (int)py, PLAYER_W, PLAYER_H,
                                 tx * TILE + 4, ty * TILE + 14, TILE - 8, TILE - 14))
                    KillPlayer();
                break;

            case '*':                   // poison plants: most of the tile
                if (BoxesOverlap(px, (int)py, PLAYER_W, PLAYER_H,
                                 tx * TILE + 6, ty * TILE + 8, TILE - 12, TILE - 8))
                    KillPlayer();
                break;

            case 's':
                if (!switchOn)
                    OpenDoors();
                break;
            }
        }
    }
}

// Hanging on a rope: Up and Down climb, Space jumps off (with Left or Right
// to jump sideways)
static void UpdateClimbing(void)
{
    int cx = px + PLAYER_W / 2;

    if (KeyDown(VK_SPACE))
    {
        int dir  = (KeyDown(VK_RIGHT) ? 1 : 0) - (KeyDown(VK_LEFT) ? 1 : 0);
        climbing  = FALSE;
        grabDelay = 20;                 // so we don't grab the same rope straight away
        vy        = -JUMP_SPEED * 0.75f;
        airDir    = dir;
        if (dir) facing = dir;
        Play(SND_JUMP);
        return;
    }

    float ny = py;
    if (KeyDown(VK_UP))   ny -= CLIMB_SPEED;
    if (KeyDown(VK_DOWN)) ny += CLIMB_SPEED;

    // Can't climb above the top of the rope, or into a wall
    if (ny < py && (!IsRopeAt(cx, (int)ny + 6) || BoxHitsWall(px, (int)ny, PLAYER_W, PLAYER_H)))
        ny = py;

    if (ny > py)
    {
        // Climbing down onto a floor: step off
        int oldFeet = (int)py + PLAYER_H, newFeet = (int)ny + PLAYER_H;
        int top = (newFeet / TILE) * TILE;
        if (top >= oldFeet && top <= newFeet)
            for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
                if (IsFloor(TileAt(tx, top / TILE)))
                {
                    py = (float)(top - PLAYER_H);
                    climbing = FALSE;
                    onGround = TRUE;
                    vy = 0;
                    return;
                }
    }

    if (ny != py)
        walkAnim++;
    py = ny;

    // Climbed down past the end of the rope: let go and fall
    if (!IsRopeAt(cx, (int)py + 6))
    {
        climbing  = FALSE;
        grabDelay = 20;
        airDir    = 0;
        vy        = 0;
    }
}

void UpdatePlayer(void)
{
    if (grabDelay > 0)
        grabDelay--;

    if (climbing)
    {
        UpdateClimbing();
        CheckTouches();
        return;
    }

    // Carried along by a moving platform
    if (onGround && standingOn >= 0)
    {
        int carry = (int)movers[standingOn].dx;
        int step  = (carry > 0) ? 1 : -1;
        for (int i = 0; i != carry; i += step)
        {
            if (BoxHitsWall(px + step, (int)py, PLAYER_W, PLAYER_H))
                break;
            px += step;
        }
    }

    BOOL wasOnGround = onGround;
    BOOL jumped      = FALSE;

    // On the ground you can steer and jump. In the air, the direction is
    // fixed: like the 8-bit classics, there's no changing your mind mid-jump.
    if (onGround)
    {
        int dir = (KeyDown(VK_RIGHT) ? 1 : 0) - (KeyDown(VK_LEFT) ? 1 : 0);
        if (dir != 0)
        {
            facing = dir;
            walkAnim++;
        }
        airDir = dir;

        if (KeyDown(VK_SPACE) || KeyDown(VK_UP))
        {
            vy       = -JUMP_SPEED;
            onGround = FALSE;
            jumped   = TRUE;
            Play(SND_JUMP);
        }
    }

    // Horizontal: move one pixel at a time so we stop exactly at a wall
    for (int i = 0; i < WALK_SPEED && airDir != 0; i++)
    {
        if (BoxHitsWall(px + airDir, (int)py, PLAYER_W, PLAYER_H))
            break;
        px += airDir;
    }

    // A conveyor under your feet carries you along
    if (onGround && standingOn < 0)
    {
        int ty = ((int)py + PLAYER_H) / TILE;
        for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
        {
            char c = TileAt(tx, ty);
            if (c == '<' || c == '>')
            {
                int push = (c == '>') ? BELT_SPEED : -BELT_SPEED;
                if (!BoxHitsWall(px + push, (int)py, PLAYER_W, PLAYER_H))
                    px += push;
                break;
            }
        }
    }

    // Vertical
    vy += GRAVITY;
    if (vy > MAX_FALL) vy = MAX_FALL;
    float newY = py + vy;

    if (vy < 0)
    {
        // Rising: only walls block your head. Platforms let you jump through.
        if (BoxHitsWall(px, (int)newY, PLAYER_W, PLAYER_H))
        {
            int headRow = (int)newY / TILE;
            newY = (float)((headRow + 1) * TILE);
            vy   = 0;
        }
        py = newY;
        onGround   = FALSE;
        standingOn = -1;
    }
    else
    {
        // Falling: look for a floor whose top we pass through this frame -
        // a floor tile, or the top of a moving platform
        int  oldFeet = (int)py + PLAYER_H;
        int  newFeet = (int)newY + PLAYER_H;
        BOOL landed  = FALSE;
        standingOn = -1;

        for (int ty = oldFeet / TILE; ty <= newFeet / TILE && !landed; ty++)
        {
            int top = ty * TILE;
            if (top < oldFeet || top > newFeet)
                continue;
            for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
                if (IsFloor(TileAt(tx, ty)))
                {
                    py = (float)(top - PLAYER_H);
                    landed = TRUE;
                    break;
                }
        }

        for (int i = 0; i < numMovers && !landed; i++)
        {
            Mover *m = &movers[i];
            if (m->y >= oldFeet && m->y <= newFeet && px < (int)m->x + m->w && (int)m->x < px + PLAYER_W)
            {
                py = (float)(m->y - PLAYER_H);
                landed = TRUE;
                standingOn = i;
            }
        }

        if (landed)
            vy = 0;
        else
            py = newY;
        onGround = landed;
    }

    // Walked off an edge: drop straight down, like Manic Miner
    if (wasOnGround && !onGround && !jumped)
        airDir = 0;

    // Crumbling platforms wear away while you stand on them
    if (onGround && standingOn < 0)
    {
        int ty = ((int)py + PLAYER_H) / TILE;
        for (int tx = px / TILE; tx <= (px + PLAYER_W - 1) / TILE; tx++)
            if (TileAt(tx, ty) == '-' && ++crumble[ty][tx] >= CRUMBLE_TIME)
                map[ty][tx] = ' ';
    }

    // In the air and touching a rope: grab it
    int cx = px + PLAYER_W / 2;
    if (!onGround && grabDelay == 0 && IsRopeAt(cx, (int)py + 6))
    {
        int ropeX = (cx / TILE) * TILE + TILE / 2 - PLAYER_W / 2;
        if (!BoxHitsWall(ropeX, (int)py, PLAYER_W, PLAYER_H))
            px = ropeX;
        climbing = TRUE;
        vy       = 0;
        airDir   = 0;
    }

    CheckTouches();
}

// ============================================================================
//  Moving platforms, monsters and the boss
// ============================================================================

static void UpdateMovers(void)
{
    for (int i = 0; i < numMovers; i++)
    {
        Mover *m = &movers[i];
        float nx = m->x + m->vx;

        // Turn round at walls, doors and other floors in its row, or the map's edge
        int  left = (int)nx, right = (int)nx + m->w - 1, ty = m->y / TILE;
        BOOL blocked = (nx < 0 || right >= MAP_W * TILE);
        for (int tx = left / TILE; tx <= right / TILE && !blocked; tx++)
            if (IsFloor(TileAt(tx, ty)))
                blocked = TRUE;

        if (blocked)
        {
            m->vx = -m->vx;
            m->dx = 0;
        }
        else
        {
            m->dx = (float)((int)nx - (int)m->x);   // whole pixels moved, for carrying
            m->x  = nx;
        }
    }
}

// Would a monster at (x, y) hit something? Flyers only mind walls;
// lifts and bouncers turn at floors too.
static BOOL EnemyHits(float x, float y, BOOL floorsToo)
{
    int x0 = (int)x, y0 = (int)y;
    if (x < 0 || y < 0 || x0 + ENEMY_W > MAP_W * TILE || y0 + ENEMY_H > MAP_H * TILE)
        return TRUE;
    for (int ty = y0 / TILE; ty <= (y0 + ENEMY_H - 1) / TILE; ty++)
        for (int tx = x0 / TILE; tx <= (x0 + ENEMY_W - 1) / TILE; tx++)
        {
            char c = TileAt(tx, ty);
            if (IsWall(c) || (floorsToo && IsFloor(c)))
                return TRUE;
        }
    return FALSE;
}

static void UpdateEnemies(BOOL canKill)
{
    for (int i = 0; i < numEnemies; i++)
    {
        Enemy *e = &enemies[i];

        switch (cav->move[e->kind])
        {
        case MOVE_WALK:
        case MOVE_RUN:
        {
            // Walk along, turning at walls, spikes, plants or the edge of the floor
            float nx    = e->x + e->vx;
            int   front = (e->vx > 0) ? (int)nx + ENEMY_W - 1 : (int)nx;
            int   tx    = front / TILE;
            int   body  = ((int)e->y + ENEMY_H - 1) / TILE;
            char  ahead = TileAt(tx, body);
            if (IsWall(ahead) || ahead == '^' || ahead == '*' || !IsFloor(TileAt(tx, body + 1)))
                e->vx = -e->vx;
            else
                e->x = nx;
            break;
        }

        case MOVE_FLY:                  // straight across, turning at walls
            if (EnemyHits(e->x + e->vx, e->y, FALSE))
                e->vx = -e->vx;
            else
                e->x += e->vx;
            break;

        case MOVE_LIFT:                 // up and down, turning at walls and floors
            if (EnemyHits(e->x, e->y + e->vy, TRUE))
                e->vy = -e->vy;
            else
                e->y += e->vy;
            break;

        case MOVE_BOUNCE:               // diagonally, bouncing off walls and floors
            if (EnemyHits(e->x + e->vx, e->y, TRUE))
                e->vx = -e->vx;
            else
                e->x += e->vx;
            if (EnemyHits(e->x, e->y + e->vy, TRUE))
                e->vy = -e->vy;
            else
                e->y += e->vy;
            break;
        }

        // Touching a monster is fatal. Shrink the boxes a little to be fair.
        if (canKill &&
            BoxesOverlap(px + 3, (int)py + 3, PLAYER_W - 6, PLAYER_H - 6,
                         (int)e->x + 3, (int)e->y + 3, ENEMY_W - 6, ENEMY_H - 6))
            KillPlayer();
    }
}

static void UpdateBoss(void)
{
    switch (bossState)
    {
    case BOSS_WAITING:
        if (--bossTimer > 0)
            break;
        // Appear near the top, on the side of the screen away from the player
        bossX  = (px - camX < VIEW_W / 2) ? camX + VIEW_W - BOSS_W - 24 : camX + 24;
        bossY  = 40;
        bossVX = 0;
        bossState = BOSS_WARNING;       // two seconds of warning before it moves
        bossTimer = 120;
        Play(SND_BOSSWARN);
        break;

    case BOSS_WARNING:
        if (--bossTimer <= 0)
        {
            bossState = BOSS_HUNTING;
            bossTimer = BOSS_STAY;
        }
        break;

    case BOSS_HUNTING:
    {
        // Drift straight towards the player, through walls, with a slow wobble
        float dx  = (px + PLAYER_W / 2.0f) - (bossX + BOSS_W / 2.0f);
        float dy  = (py + PLAYER_H / 2.0f) - (bossY + BOSS_W / 2.0f);
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1)
        {
            bossVX = dx / len * BOSS_SPEED;
            bossX += bossVX;
            bossY += dy / len * BOSS_SPEED;
        }
        bossY += sinf(frameCount * 0.08f) * 0.6f;

        int edge = (BOSS_W - BOSS_HIT) / 2;
        if (BoxesOverlap(px + 3, (int)py + 3, PLAYER_W - 6, PLAYER_H - 6,
                         (int)bossX + edge, (int)bossY + edge, BOSS_HIT, BOSS_HIT))
            KillPlayer();

        if (state == ST_PLAYING && --bossTimer <= 0)
        {
            char s[40];
            bossState = BOSS_LEAVING;   // you survived it
            AddPoints(BOSS_BONUS);
            wsprintf(s, "GUARDIAN GONE!  +%d", BOSS_BONUS);
            ShowMessage(s, RGB(120, 255, 120), 150);
            Play(SND_BOSSGONE);
        }
        break;
    }

    case BOSS_LEAVING:
        bossY -= 3;                     // flies up and away
        if (bossY < -BOSS_W)
            bossState = BOSS_NONE;
        break;
    }
}

static void UpdateCamera(void)
{
    // Ease the view towards the player, so the scrolling is smooth
    float target = (float)(px + PLAYER_W / 2 - VIEW_W / 2);
    if (target < 0) target = 0;
    if (target > MaxCamera()) target = MaxCamera();
    camX += (target - camX) * 0.12f;
}

// ============================================================================
//  Looking ahead, for the demo: save everything that a frame can change,
//  try some frames, then put it all back
// ============================================================================

static struct
{
    char          map[MAP_H][MAP_W + 1];
    unsigned char crumble[MAP_H][MAP_W];
    PlayerState   player;
    int           walkAnim;
    Enemy         enemies[MAX_ENEMIES];
    Mover         movers[MAX_MOVERS];
    int           keysLeft, score, lives, nextLife, air, state, stateTimer;
    BOOL          switchOn;
    char          message[64];
    COLORREF      messageColour;
    int           messageTimer;
} snap;

void SnapshotSave(void)
{
    memcpy(snap.map, map, sizeof map);
    memcpy(snap.crumble, crumble, sizeof crumble);
    GetPlayerState(&snap.player);
    snap.walkAnim = walkAnim;
    memcpy(snap.enemies, enemies, sizeof enemies);
    memcpy(snap.movers, movers, sizeof movers);
    snap.keysLeft = keysLeft;   snap.score = score;         snap.lives = lives;
    snap.nextLife = nextLife;   snap.air = air;             snap.state = state;
    snap.stateTimer = stateTimer;
    snap.switchOn = switchOn;
    memcpy(snap.message, message, sizeof message);
    snap.messageColour = messageColour;
    snap.messageTimer  = messageTimer;
}

void SnapshotRestore(void)
{
    memcpy(map, snap.map, sizeof map);
    memcpy(crumble, snap.crumble, sizeof crumble);
    SetPlayerState(&snap.player);
    walkAnim = snap.walkAnim;
    memcpy(enemies, snap.enemies, sizeof enemies);
    memcpy(movers, snap.movers, sizeof movers);
    keysLeft = snap.keysLeft;   score = snap.score;         lives = snap.lives;
    nextLife = snap.nextLife;   air = snap.air;             state = snap.state;
    stateTimer = snap.stateTimer;
    switchOn = snap.switchOn;
    memcpy(message, snap.message, sizeof message);
    messageColour = snap.messageColour;
    messageTimer  = snap.messageTimer;
}

// One frame of play, without the boss or the air running out
void SimulateFrame(void)
{
    UpdateMovers();
    UpdatePlayer();
    UpdateEnemies(TRUE);
}

// ============================================================================
//  One frame
// ============================================================================

void Update(void)
{
    frameCount++;
    stateTimer++;
    if (savedTimer > 0) savedTimer--;

    if (paused || !active)
        return;
    if (messageTimer > 0) messageTimer--;

    switch (state)
    {
    case ST_TITLE:
        // The camera drifts back and forth across the first cavern, and the
        // title panel swaps between instructions and high scores
        UpdateMovers();
        UpdateEnemies(FALSE);
        camX += titlePan;
        if (camX < 0 || camX > MaxCamera())
        {
            titlePan = -titlePan;
            camX += titlePan;
        }
        if (stateTimer % 420 == 0)
            titlePage = !titlePage;
        if (stateTimer == 60 * 20)      // left alone for 20 seconds: play a demo
            StartDemo();
        break;

    case ST_EDIT:
        // Arrow keys scroll the editor's view
        if (KeyDown(VK_LEFT))  camX -= 12;
        if (KeyDown(VK_RIGHT)) camX += 12;
        if (camX < 0) camX = 0;
        if (camX > MaxCamera()) camX = MaxCamera();
        break;

    case ST_PLAYING:
        if (demoMode)
        {
            DemoInput();                // the demo decides what to press
            if (stateTimer > 60 * 80)   // and gives up after 80 seconds
            {
                extern const char *demoEnd;
                demoEnd = "ran out of time";
                StopDemo();
                break;
            }
        }
        UpdateMovers();
        UpdatePlayer();
        if (demoMode)
        {
            DemoAfter();
            if (!demoMode)
                break;                  // the demo ended
        }
        UpdateEnemies(TRUE);
        if (state == ST_PLAYING)
            UpdateBoss();

        if (state == ST_PLAYING && --air <= 0)
            KillPlayer();

        // Reaching the open exit completes the cavern
        if (state == ST_PLAYING && keysLeft == 0 &&
            BoxesOverlap(px, (int)py, PLAYER_W, PLAYER_H, exitX, exitY, TILE, 2 * TILE))
        {
            Play(SND_CLEARED);
            SetState(ST_TALLY);
        }
        UpdateCamera();
        break;

    case ST_DYING:
        if (stateTimer >= 70)
        {
            if (demoMode)
            {
                extern const char *demoEnd;
                demoEnd = "killed";
                StopDemo();             // back to the title
            }
            else if (testMode)
                LoadLevel(cavNum);      // testing: unlimited lives
            else if (--lives <= 0)
            {
                Play(SND_GAMEOVER);
                SetState(ST_GAMEOVER);
            }
            else
                LoadLevel(cavNum);      // the cavern resets, as in the original
        }
        break;

    case ST_TALLY:
        // Count the remaining air into the score, as Manic Miner did
        if (stateTimer > 30)
        {
            int chunk = (air < 90) ? air : 90;
            air -= chunk;
            AddPoints(chunk / 6);
            if (air <= 0)
                SetState(ST_CLEARED);
        }
        break;

    case ST_CLEARED:
        if (stateTimer >= 150)          // move on by itself after 2.5 seconds
        {
            int next = NextPlayable(cavNum + 1);
            if (demoMode)
            {
                extern const char *demoEnd;
                demoEnd = "CLEARED THE CAVERN";
                StopDemo();
            }
            else if (testMode)
                EnterEditor(cavNum);
            else if (next >= 0)
                LoadLevel(next);
            else
            {
                Play(SND_CLEARED);
                SetState(ST_VICTORY);
            }
        }
        break;
    }
}
