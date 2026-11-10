// ============================================================================
//  checker.cpp - can this cavern be finished?  (V in the cavern editor)
//
//  This plays the cavern "in its head" using the game's own physics. From
//  every place the player can stand or hang on a rope, it tries every move -
//  a step left or right, a jump straight up, left or right, climbing up or
//  down, jumping off a rope - and sees where each one ends up. Repeating
//  that until nothing new turns up finds every reachable place.
//
//  Monsters and the boss are left out (they can be dodged), and a moving
//  platform is treated as a floor along the whole of its track, since you
//  can ride it anywhere it goes.
//
//  The results are drawn by the editor: green marks where you can stand,
//  orange where you can't get back from, and a red box round any key,
//  switch or exit that can't be reached.
// ============================================================================

#include "cavern.h"

BOOL          checkDone;
unsigned char checkMark[MAP_H][MAP_W];
char          checkSummary[120];
BOOL          checkAllGood;

#define MAX_NODES   60000               // places the player can be
#define HASH_SIZE   131072              // for finding a place quickly (a power of two)
#define MAX_MOVES   5                   // moves tried from each place
#define MAX_SPOTS   64                  // keys, switches and the exit

typedef struct
{
    PlayerState s;
    int         next[MAX_MOVES];        // where each move leads (-1 = nowhere / died)
    unsigned long long moveTouches[MAX_MOVES];  // spots each move touches on the way
} Node;

typedef struct
{
    int  tx, ty;
    char c;                             // 'k', 's' or 'X'
} Spot;

static Node *nodes;
static int  *hashTable;
static int   nodeCount;
static Spot  spots[MAX_SPOTS];
static int   spotCount;
static BOOL  missed[MAX_SPOTS];         // can't be reached

// ============================================================================
//  Remembering places
// ============================================================================

// A place is the player's position and what they're doing
static unsigned long long KeyOf(const PlayerState *s)
{
    return ((unsigned long long)(unsigned)s->px << 32) | ((unsigned)(int)s->py << 12) |
           (s->climbing << 11) | (s->grabDelay << 4) | (s->standingOn + 1);
}

static int FindOrAdd(const PlayerState *s, BOOL *isNew)
{
    unsigned long long key = KeyOf(s);
    unsigned h = (unsigned)((key * 0x9E3779B97F4A7C15ULL) >> 47) & (HASH_SIZE - 1);
    *isNew = FALSE;
    while (hashTable[h] >= 0)
    {
        if (KeyOf(&nodes[hashTable[h]].s) == key)
            return hashTable[h];
        h = (h + 1) & (HASH_SIZE - 1);
    }
    if (nodeCount >= MAX_NODES)
        return -1;                      // too many: stop exploring here

    Node *n = &nodes[nodeCount];
    n->s = *s;
    for (int m = 0; m < MAX_MOVES; m++)
    {
        n->next[m] = -1;
        n->moveTouches[m] = 0;
    }
    hashTable[h] = nodeCount;
    *isNew = TRUE;
    return nodeCount++;
}

// ============================================================================
//  Trying a move
// ============================================================================

static BOOL Overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// Which spots is the player touching now?
static unsigned long long Touching(void)
{
    unsigned long long t = 0;
    for (int i = 0; i < spotCount; i++)
    {
        int h = (spots[i].c == 'X') ? 2 * TILE : TILE;
        if (Overlaps(px, (int)py, PLAYER_W, PLAYER_H, spots[i].tx * TILE, spots[i].ty * TILE, TILE, h))
            t |= 1ULL << i;
    }
    return t;
}

// Move m from place 'from', until the player is standing or hanging again.
// Standing: 0 step left, 1 step right, 2 jump up, 3 jump left, 4 jump right.
// On a rope: 0 climb up, 1 climb down, 2 let go, 3 jump off left, 4 jump off right.
static BOOL TryMove(const PlayerState *from, int m, PlayerState *to, unsigned long long *touched)
{
    static unsigned char keys[256];
    BOOL rope = from->climbing;
    int  hold = (rope && m <= 1) ? 8 : 1;          // climb for 8 frames at a time

    SetPlayerState(from);
    state = ST_PLAYING;
    memset(crumble, 0, sizeof crumble);            // crumbling floors are new each time
    *touched = 0;

    for (int f = 0; f < 400; f++)
    {
        memset(keys, 0, sizeof keys);
        if (f < hold)
        {
            if (!rope)
            {
                if (m == 0 || m == 3) keys[VK_LEFT]  = 1;
                if (m == 1 || m == 4) keys[VK_RIGHT] = 1;
                if (m >= 2)           keys[VK_SPACE] = 1;
            }
            else
            {
                if (m == 0) keys[VK_UP]   = 1;
                if (m == 1) keys[VK_DOWN] = 1;
                if (m >= 2) keys[VK_SPACE] = 1;
                if (m == 3) keys[VK_LEFT]  = 1;
                if (m == 4) keys[VK_RIGHT] = 1;
            }
        }
        simKeys = keys;
        UpdatePlayer();
        *touched |= Touching();

        if (state != ST_PLAYING)
            return FALSE;                           // died
        BOOL justJumped = (!rope && m >= 2 && f == 0);
        if (f >= hold - 1 && (onGround || climbing) && !justJumped)
        {
            GetPlayerState(to);
            return TRUE;
        }
    }
    return FALSE;                                   // still falling after 400 frames
}

// ============================================================================
//  Exploring
// ============================================================================

// Explore everywhere reachable from 'start', building the list of places
// (nodes) and the moves between them. Returns the spots touched.
static unsigned long long BuildGraph(const PlayerState *start)
{
    nodeCount = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        hashTable[i] = -1;

    BOOL isNew;
    FindOrAdd(start, &isNew);
    unsigned long long reached = 0;

    // Every place is tried in turn; new places are added to the end of the list
    for (int n = 0; n < nodeCount; n++)
    {
        for (int m = 0; m < MAX_MOVES; m++)
        {
            PlayerState to;
            unsigned long long t;
            PlayerState from = nodes[n].s;
            if (!TryMove(&from, m, &to, &t))
                continue;
            int k = FindOrAdd(&to, &isNew);
            if (k < 0)
                continue;
            nodes[n].next[m] = k;
            nodes[n].moveTouches[m] = t;
            reached |= t;
        }
    }
    return reached;
}

// Explore, then mark in checkMark where the player can stand (1) or can't
// get back from (2). Returns the spots touched.
static unsigned long long Explore(const PlayerState *start)
{
    unsigned long long reached = BuildGraph(start);

    // Which places can get back to where we started? Begin with the start
    // itself (give or take a step), then keep adding any place with a move
    // into one we already know, until nothing changes.
    unsigned char *back = (unsigned char *)calloc(nodeCount, 1);
    if (back == NULL)
        return reached;
    for (int n = 0; n < nodeCount; n++)
        if (!nodes[n].s.climbing && (int)nodes[n].s.py == (int)start->py && abs(nodes[n].s.px - start->px) <= 3)
            back[n] = 1;

    BOOL changed = TRUE;
    while (changed)
    {
        changed = FALSE;
        for (int n = 0; n < nodeCount; n++)
            if (!back[n])
                for (int m = 0; m < MAX_MOVES; m++)
                    if (nodes[n].next[m] >= 0 && back[nodes[n].next[m]])
                    {
                        back[n] = 1;
                        changed = TRUE;
                        break;
                    }
    }

    // Mark the tile each place stands in (where the player's feet are)
    for (int n = 0; n < nodeCount; n++)
    {
        int tx = (nodes[n].s.px + PLAYER_W / 2) / TILE;
        int ty = ((int)nodes[n].s.py + PLAYER_H - 1) / TILE;
        if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H)
            continue;
        int mark = back[n] ? 1 : 2;
        if (checkMark[ty][tx] == 0 || mark == 1)    // standing-and-safe wins
            checkMark[ty][tx] = (unsigned char)mark;
    }
    free(back);
    return reached;
}

// Lay a floor along the whole track of each moving platform
static void FloorUnderMovers(void)
{
    for (int i = 0; i < numMovers; i++)
    {
        int ty    = movers[i].y / TILE;
        int left  = (int)movers[i].x / TILE;
        int right = ((int)movers[i].x + movers[i].w - 1) / TILE;
        while (left > 0 && !IsFloor(map[ty][left - 1]))
            left--;
        while (right < MAP_W - 1 && !IsFloor(map[ty][right + 1]))
            right++;
        for (int tx = left; tx <= right; tx++)
            map[ty][tx] = '=';
    }
}

// Set up the cavern for exploring, and find where the player first lands
static void Prepare(BOOL doorsOpen, PlayerState *start)
{
    static const unsigned char none[256] = { 0 };

    LoadLevel(cavNum);
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
        {
            char c = map[ty][tx];
            if (c == 'k' || c == 's')
                map[ty][tx] = ' ';      // noted as spots instead, so nothing gets picked up
            if (c == 'D' && doorsOpen)
                map[ty][tx] = ' ';
        }
    FloorUnderMovers();
    numMovers = 0;                      // their tracks are floor now

    simKeys  = none;
    onGround = FALSE;
    for (int f = 0; f < 300 && !onGround && state == ST_PLAYING; f++)
        UpdatePlayer();
    for (int f = 0; f < 3; f++)
        UpdatePlayer();
    GetPlayerState(start);
}

void CheckCavern(void)
{
    int   n       = cavNum;
    float keepCam = camX;

    nodes     = (Node *)malloc(MAX_NODES * sizeof(Node));
    hashTable = (int *)malloc(HASH_SIZE * sizeof(int));
    if (nodes == NULL || hashTable == NULL)
    {
        free(nodes);
        free(hashTable);
        lstrcpy(checkSummary, "Not enough memory to check this cavern");
        checkDone = TRUE;
        checkAllGood = FALSE;
        return;
    }

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    simulating = TRUE;
    memset(checkMark, 0, sizeof checkMark);

    // The keys, switches and exit, as they are in the cavern
    spotCount = 0;
    BOOL hasSwitch = FALSE;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
        {
            char c = caverns[n].rows[ty][tx];
            if ((c == 'k' || c == 's' || c == 'X') && spotCount < MAX_SPOTS)
            {
                spots[spotCount].tx = tx;
                spots[spotCount].ty = ty;
                spots[spotCount].c  = c;
                spotCount++;
            }
            if (c == 's')
                hasSwitch = TRUE;
        }

    // First with the doors shut. If a switch can be reached, go round again
    // with them open.
    PlayerState start;
    Prepare(FALSE, &start);
    unsigned long long reached = Explore(&start);

    BOOL switchReached = FALSE;
    for (int i = 0; i < spotCount; i++)
        if (spots[i].c == 's' && (reached & (1ULL << i)))
            switchReached = TRUE;
    if (switchReached)
    {
        memset(checkMark, 0, sizeof checkMark);
        Prepare(TRUE, &start);
        reached |= Explore(&start);
    }

    simKeys    = NULL;
    simulating = FALSE;
    free(nodes);
    free(hashTable);

    // Sum up
    int keysMissed = 0, oneWay = 0;
    BOOL exitMissed = FALSE, switchMissed = FALSE;
    for (int i = 0; i < spotCount; i++)
    {
        missed[i] = !(reached & (1ULL << i));
        if (missed[i] && spots[i].c == 'k') keysMissed++;
        if (missed[i] && spots[i].c == 'X') exitMissed = TRUE;
        if (missed[i] && spots[i].c == 's') switchMissed = TRUE;
    }
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (checkMark[ty][tx] == 2)
                oneWay++;

    checkAllGood = (keysMissed == 0 && !exitMissed);
    if (checkAllGood)
        lstrcpy(checkSummary, "All the keys and the exit can be reached");
    else
    {
        int len = wsprintf(checkSummary, "Can't reach");
        if (keysMissed)
            len += wsprintf(checkSummary + len, " %d key%s", keysMissed, keysMissed == 1 ? "" : "s");
        if (exitMissed)
            len += wsprintf(checkSummary + len, "%s the exit", keysMissed ? " or" : "");
        lstrcat(checkSummary, " (red boxes)");
    }
    if (hasSwitch && switchMissed)
        lstrcat(checkSummary, "; a switch can't be reached");
    if (oneWay)
        lstrcat(checkSummary, ". Orange: no way back");

    // Put the cavern back as it was for editing
    LoadLevel(n);
    camX = keepCam;
    SetState(ST_EDIT);
    checkDone = TRUE;
    SetCursor(oldCursor);
}

BOOL CheckMissed(int tx, int ty)
{
    if (!checkDone)
        return FALSE;
    for (int i = 0; i < spotCount; i++)
        if (missed[i] && spots[i].tx == tx && spots[i].ty == ty)
            return TRUE;
    return FALSE;
}

// ============================================================================
//  Planning a route, for the demo
//
//  Build the map of places, then repeatedly find the shortest list of moves
//  (a "breadth-first search") to the nearest key not yet collected, and
//  finally to the exit. Returns how many steps, or 0 if there's no route.
// ============================================================================

static void FindSpots(int n)
{
    spotCount = 0;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
        {
            char c = caverns[n].rows[ty][tx];
            if ((c == 'k' || c == 's' || c == 'X') && spotCount < MAX_SPOTS)
            {
                spots[spotCount].tx = tx;
                spots[spotCount].ty = ty;
                spots[spotCount].c  = c;
                spotCount++;
            }
        }
}

// Shortest way from node 'from' to a move that touches one of 'targets'.
// Adds the steps to the route; returns the node it ends at, or -1.
static int Search(int from, unsigned long long targets, DemoStep *steps, int *count, int max,
                  unsigned long long *have, int *prevNode, int *prevMove, int *queue)
{
    for (int i = 0; i < nodeCount; i++)
        prevNode[i] = -2;               // not visited yet
    prevNode[from] = -1;
    int head = 0, tail = 0;
    queue[tail++] = from;

    while (head < tail)
    {
        int u = queue[head++];
        for (int m = 0; m < MAX_MOVES; m++)
        {
            int v = nodes[u].next[m];
            if (v < 0)
                continue;
            if (nodes[u].moveTouches[m] & targets)
            {
                // Found it: walk back along the path, then write it out in order
                int path[2000], moves[2000], len = 0;
                path[len] = u; moves[len] = m; len++;
                for (int w = u; prevNode[w] >= 0 && len < 2000; w = prevNode[w])
                {
                    path[len] = prevNode[w];
                    moves[len] = prevMove[w];
                    len++;
                }
                for (int i = len - 1; i >= 0 && *count < max; i--)
                {
                    int at = path[i], mv = moves[i];
                    DemoStep *st = &steps[(*count)++];
                    st->move  = mv;
                    st->rope  = nodes[at].s.climbing;
                    st->after = nodes[nodes[at].next[mv]].s;
                    *have |= nodes[at].moveTouches[mv];
                }
                return v;
            }
            if (prevNode[v] == -2)
            {
                prevNode[v] = u;
                prevMove[v] = m;
                queue[tail++] = v;
            }
        }
    }
    return -1;
}

int PlanRoute(DemoStep *steps, int max)
{
    int n = cavNum, count = 0;
    nodes     = (Node *)malloc(MAX_NODES * sizeof(Node));
    hashTable = (int *)malloc(HASH_SIZE * sizeof(int));
    int *prevNode = (int *)malloc(MAX_NODES * sizeof(int));
    int *prevMove = (int *)malloc(MAX_NODES * sizeof(int));
    int *queue    = (int *)malloc(MAX_NODES * sizeof(int));

    if (nodes && hashTable && prevNode && prevMove && queue)
    {
        simulating = TRUE;
        FindSpots(n);
        PlayerState start;
        Prepare(FALSE, &start);
        BuildGraph(&start);

        unsigned long long keys = 0, exitSpot = 0, have = 0;
        for (int i = 0; i < spotCount; i++)
        {
            if (spots[i].c == 'k') keys |= 1ULL << i;
            if (spots[i].c == 'X') exitSpot |= 1ULL << i;
        }

        int at = 0;                     // the start
        while (at >= 0 && (have & keys) != keys)
            at = Search(at, keys & ~have, steps, &count, max, &have, prevNode, prevMove, queue);
        if (at >= 0)
            at = Search(at, exitSpot, steps, &count, max, &have, prevNode, prevMove, queue);
        if (at < 0 || count >= max)
            count = 0;                  // no complete route

        simKeys    = NULL;
        simulating = FALSE;
    }
    free(nodes);
    free(hashTable);
    free(prevNode);
    free(prevMove);
    free(queue);
    return count;
}
