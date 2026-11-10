// ============================================================================
//  files.cpp - reading and writing caverns.txt and sprites.txt
//
//  Both files live next to the .exe. If one is missing, it is created from
//  the standard caverns or artwork in defaults.cpp.
// ============================================================================

#include "cavern.h"

Cavern caverns[MAX_CAVERNS];
int    numCaverns;
Sprite sprites[NUM_SPRITES];
char   spriteIds[NUM_SPRITES][16];
DesignItem items[NUM_ITEMS];

const char *moveNames[NUM_MOVES] = { "WALK", "RUN", "LIFT", "FLY", "BOUNCE" };

// Background colours: sky, and its faint stripes
const COLORREF schemes[NUM_SCHEMES][2] =
{
    { RGB(12, 12, 32), RGB(22, 22, 52) },
    { RGB(28, 14, 10), RGB(48, 26, 18) },
    { RGB(6, 22, 30),  RGB(14, 40, 52) },
    { RGB(36, 6, 6),   RGB(60, 14, 10) },
    { RGB(10, 18, 40), RGB(24, 36, 70) },
    { RGB(6, 24, 10),  RGB(14, 42, 18) },
};

// The 16 colours, written as 0-9 and A-F in sprites.txt ('.' is transparent)
const COLORREF palette[16] =
{
    RGB(0, 0, 0),       RGB(28, 42, 110),   RGB(122, 46, 142),  RGB(30, 122, 60),
    RGB(138, 75, 36),   RGB(74, 74, 85),    RGB(168, 168, 180), RGB(244, 244, 244),
    RGB(224, 48, 58),   RGB(240, 138, 36),  RGB(248, 216, 56),  RGB(88, 208, 64),
    RGB(64, 200, 240),  RGB(58, 100, 216),  RGB(240, 112, 184), RGB(244, 196, 156),
};

const char *colourNames[16] =
{
    "Black", "Navy", "Purple", "Dark green", "Brown", "Dark grey", "Light grey", "White",
    "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Pink", "Skin"
};

// ============================================================================
//  Sprite names and the designer's list
// ============================================================================

static const char *tileIds[TILES_PER_SET] =
{
    "WALL", "PLATFORM", "CRUMBLE", "CONVEYOR", "SPIKES", "PLANT",
    "SWITCHOFF", "SWITCHON", "DOOR", "ROPE", "MOVER"
};

static void SetItem(int i, const char *name, int first, int frames, int tab)
{
    lstrcpyn(items[i].name, name, sizeof items[i].name);
    items[i].first  = first;
    items[i].frames = frames;
    items[i].tab    = tab;
}

void InitSpriteTables(void)
{
    lstrcpy(spriteIds[SPR_PLAYER1], "PLAYER1");
    lstrcpy(spriteIds[SPR_PLAYER2], "PLAYER2");
    lstrcpy(spriteIds[SPR_KEY],     "KEY");
    lstrcpy(spriteIds[SPR_BOSS1],   "BOSS1");
    lstrcpy(spriteIds[SPR_BOSS2],   "BOSS2");

    SetItem(CHAR_PLAYER, "Player", SPR_PLAYER1, 2, 0);
    SetItem(CHAR_KEY,    "Key",    SPR_KEY,     1, 0);
    SetItem(CHAR_BOSS,   "Boss",   SPR_BOSS1,   2, 0);

    for (int m = 0; m < NUM_MONSTERS; m++)
    {
        char name[24];
        wsprintf(spriteIds[SPR_MONSTER + m * 2],     "MONSTER%dA", m + 1);
        wsprintf(spriteIds[SPR_MONSTER + m * 2 + 1], "MONSTER%dB", m + 1);
        wsprintf(name, "Monster %d", m + 1);
        SetItem(CHAR_MONSTER1 + m, name, SPR_MONSTER + m * 2, 2, 0);
    }

    // Each tile set: ten items (the switch has two frames: off and on)
    static const char *tileNames[TILE_ITEMS] =
        { "Wall", "Platform", "Crumbling", "Conveyor", "Spikes", "Poison plant",
          "Switch", "Door", "Rope", "Moving platform" };
    static const int tileFirst[TILE_ITEMS] =
        { T_WALL, T_PLATFORM, T_CRUMBLE, T_CONVEYOR, T_SPIKES, T_PLANT,
          T_SWITCH_OFF, T_DOOR, T_ROPE, T_MOVER };

    for (int s = 0; s < NUM_TILESETS; s++)
    {
        for (int t = 0; t < TILES_PER_SET; t++)
            wsprintf(spriteIds[TileSprite(s, t)], "SET%d_%s", s + 1, tileIds[t]);
        for (int i = 0; i < TILE_ITEMS; i++)
            SetItem(NUM_CHARS + s * TILE_ITEMS + i, tileNames[i], TileSprite(s, tileFirst[i]),
                    tileFirst[i] == T_SWITCH_OFF ? 2 : 1, s + 1);
    }
}

int SpriteSize(int n)
{
    return (n == SPR_BOSS1 || n == SPR_BOSS2) ? BOSS_SPR : SPR;
}

BOOL SpriteBlank(int n)
{
    for (int y = 0; y < SpriteSize(n); y++)
        for (int x = 0; x < SpriteSize(n); x++)
            if (sprites[n].px[y][x] >= 0)
                return FALSE;
    return TRUE;
}

// ============================================================================
//  Small file and text helpers
// ============================================================================

// Full path of a file in the same folder as the .exe
void ExePath(const char *fileName, char *path)
{
    GetModuleFileName(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    char *name  = slash ? slash + 1 : path;
    if ((name - path) + lstrlen(fileName) < MAX_PATH)
        lstrcpy(name, fileName);
}

// Read a whole file into a new zero-terminated buffer. Free it with free().
char *ReadTextFile(const char *path)
{
    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    DWORD size = GetFileSize(h, NULL);
    char *buf = NULL;
    if (size != INVALID_FILE_SIZE && size < 4 * 1024 * 1024)
    {
        buf = (char *)malloc(size + 1);
        DWORD got = 0;
        if (buf && ReadFile(h, buf, size, &got, NULL) && got == size)
            buf[size] = 0;
        else
        {
            free(buf);
            buf = NULL;
        }
    }
    CloseHandle(h);
    return buf;
}

BOOL WriteTextFile(const char *path, const char *text)
{
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD len = (DWORD)strlen(text), written = 0;
    BOOL ok = WriteFile(h, text, len, &written, NULL) && written == len;
    CloseHandle(h);
    return ok;
}

// Does the line start with this word? (not case-sensitive)
BOOL StartsWith(const char *line, const char *word)
{
    while (*word)
    {
        char a = *line++, b = *word++;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (a != b) return FALSE;
    }
    return TRUE;
}

// Copy the next line of text into 'line' (without its line ending).
// Returns where the following line starts.
const char *NextLine(const char *text, char *line, int size)
{
    int len = 0;
    while (*text && *text != '\n' && *text != '\r')
    {
        if (len < size - 1) line[len++] = *text;
        text++;
    }
    if (*text == '\r') text++;
    if (*text == '\n') text++;
    line[len] = 0;
    return text;
}

static const char *SkipSpaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// ============================================================================
//  caverns.txt
//
//      CAVERN The Old Workings
//      COLOURS 0C0C20 161634       sky and its stripes, as RRGGBB  (optional)
//      TILESET 1                   which tile set, 1-4             (optional)
//      MUSIC 1                     which tune, 1-3                 (optional)
//      MONSTER 1 2 3               designs for E, F and G, 1-16    (optional)
//      MOVES WALK FLY LIFT         how E, F and G move             (optional)
//      BOSS YES                    may the boss turn up?           (optional)
//      MAP
//      ...20 lines of the map...
//      END
// ============================================================================

void SetScheme(Cavern *cv, int s)
{
    cv->scheme   = s;
    cv->sky      = schemes[s][0];
    cv->skyLines = schemes[s][1];
}

// A blank cavern. 'index' picks starting choices, so new caverns vary.
void ClearCavern(Cavern *cv, const char *name, int index)
{
    lstrcpyn(cv->name, name, sizeof cv->name);
    SetScheme(cv, index % NUM_SCHEMES);
    cv->tileset = index % NUM_TILESETS;
    cv->music   = index % 2;            // The Entertainer and Deep Down in turn
    for (int k = 0; k < MONSTER_KINDS; k++)
    {
        cv->monster[k] = (index + k) % 8;
        cv->move[k]    = MOVE_WALK;
    }
    cv->boss = TRUE;
    for (int ty = 0; ty < MAP_H; ty++)
    {
        memset(cv->rows[ty], ' ', MAP_W);
        cv->rows[ty][MAP_W] = 0;
    }
}

// Read the sky colours as RRGGBB hex numbers. Older files had seven
// colours on this line; only the first two are used now.
static void ParseColours(const char *s, Cavern *cv)
{
    COLORREF *slot[2] = { &cv->sky, &cv->skyLines };
    for (int i = 0; i < 2; i++)
    {
        s = SkipSpaces(s);
        DWORD v = 0;
        int digits = 0;
        for (;; s++, digits++)
        {
            char c = *s;
            int d = (c >= '0' && c <= '9') ? c - '0' :
                    (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) break;
            v = (v << 4) | (DWORD)d;
        }
        if (digits != 6)
            return;
        *slot[i] = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }
}

// Fill caverns[] from the text of a caverns file. Returns how many were found.
static int ParseCaverns(const char *text, char *warn, int warnSize)
{
    int n = 0, mapRow = -1;
    BOOL badChars = FALSE, tooMany = FALSE;
    Cavern *cv = NULL;
    char line[256];

    warn[0] = 0;
    while (*text)
    {
        text = NextLine(text, line, sizeof line);
        int len = lstrlen(line);

        if (mapRow >= 0)                // inside a MAP block: this is a map row
        {
            for (int x = 0; x < MAP_W; x++)
            {
                char c = (x < len) ? line[x] : ' ';
                if (c == 0 || strchr(TILE_CHARS, c) == NULL)
                {
                    c = ' ';
                    badChars = TRUE;
                }
                cv->rows[mapRow][x] = c;
            }
            if (++mapRow == MAP_H)
                mapRow = -1;
            continue;
        }

        if (StartsWith(line, "CAVERN"))
        {
            if (n == MAX_CAVERNS)
            {
                tooMany = TRUE;
                cv = NULL;
                continue;
            }
            const char *name = SkipSpaces(line + 6);
            cv = &caverns[n];
            ClearCavern(cv, *name ? name : "Unnamed", n);
            n++;
        }
        else if (cv == NULL)
            continue;                   // comments and blank lines before the first cavern
        else if (StartsWith(line, "COLOURS"))
            ParseColours(line + 7, cv);
        else if (StartsWith(line, "COLORS"))
            ParseColours(line + 6, cv);
        else if (StartsWith(line, "TILESET"))
        {
            int t = atoi(line + 7);
            if (t >= 1 && t <= NUM_TILESETS)
                cv->tileset = t - 1;
        }
        else if (StartsWith(line, "MUSIC"))
        {
            int t = atoi(line + 5);
            if (t >= 1 && t <= NUM_TUNES)
                cv->music = t - 1;
        }
        else if (StartsWith(line, "MONSTER"))
        {
            // Up to three numbers: the designs used for E, F and G
            char *p = line + 7;
            for (int k = 0; k < MONSTER_KINDS; k++)
            {
                char *end;
                long m = strtol(p, &end, 10);
                if (end == p)
                    break;
                if (m >= 1 && m <= NUM_MONSTERS)
                    cv->monster[k] = (int)m - 1;
                p = end;
            }
        }
        else if (StartsWith(line, "MOVES"))
        {
            // Up to three words: how E, F and G move
            const char *p = line + 5;
            for (int k = 0; k < MONSTER_KINDS; k++)
            {
                p = SkipSpaces(p);
                if (*p == 0)
                    break;
                for (int m = 0; m < NUM_MOVES; m++)
                    if (StartsWith(p, moveNames[m]))
                        cv->move[k] = m;
                while (*p && *p != ' ' && *p != '\t') p++;
            }
        }
        else if (StartsWith(line, "BOSS"))
        {
            const char *v = SkipSpaces(line + 4);
            cv->boss = !(StartsWith(v, "NO") || StartsWith(v, "OFF"));
        }
        else if (StartsWith(line, "MAP"))
            mapRow = 0;
        // Anything else (END, comments, blank lines) is ignored
    }

    if (badChars)
        lstrcpyn(warn, "Some map characters weren't recognised and were replaced by spaces.\n", warnSize);
    if (tooMany)
    {
        char s[100];
        wsprintf(s, "Only the first %d caverns were loaded.\n", MAX_CAVERNS);
        if (lstrlen(warn) + lstrlen(s) < warnSize) lstrcat(warn, s);
    }
    return n;
}

void LoadCaverns(void)
{
    char path[MAX_PATH], warn[300];
    ExePath(CAVERN_FILE, path);

    char *text = ReadTextFile(path);
    if (text == NULL)
    {
        // First run: create the file with the standard caverns
        WriteTextFile(path, defaultCaverns);
        numCaverns = ParseCaverns(defaultCaverns, warn, sizeof warn);
        return;
    }

    numCaverns = ParseCaverns(text, warn, sizeof warn);
    free(text);

    if (numCaverns == 0)
    {
        MessageBox(hwndMain, "No caverns were found in " CAVERN_FILE ",\n"
                             "so the standard ones are being used.",
                   "Cavern Runner", MB_ICONWARNING);
        numCaverns = ParseCaverns(defaultCaverns, warn, sizeof warn);
    }
    else if (warn[0])
        MessageBox(hwndMain, warn, CAVERN_FILE, MB_ICONWARNING);
}

// Add any standard caverns that aren't in the list yet (matched by name),
// leaving your own caverns as they are. Returns how many were added.
int ImportDefaultCaverns(void)
{
    char warn[300];
    Cavern *mine = (Cavern *)malloc(sizeof caverns);
    if (mine == NULL)
        return 0;
    memcpy(mine, caverns, sizeof caverns);
    int myCount = numCaverns;

    // Read the standard caverns into caverns[], then put yours back in front
    int std = ParseCaverns(defaultCaverns, warn, sizeof warn);
    Cavern *standard = (Cavern *)malloc(std * sizeof(Cavern));
    if (standard == NULL)
    {
        memcpy(caverns, mine, sizeof caverns);
        free(mine);
        return 0;
    }
    memcpy(standard, caverns, std * sizeof(Cavern));
    memcpy(caverns, mine, sizeof caverns);
    numCaverns = myCount;

    int added = 0;
    for (int i = 0; i < std && numCaverns < MAX_CAVERNS; i++)
    {
        BOOL have = FALSE;
        for (int j = 0; j < myCount; j++)
            if (lstrcmpi(caverns[j].name, standard[i].name) == 0)
                have = TRUE;
        if (!have)
        {
            caverns[numCaverns++] = standard[i];
            added++;
        }
    }
    free(standard);
    free(mine);
    return added;
}

BOOL SaveCaverns(void)
{
    int   size = 4000 + numCaverns * (MAP_H * (MAP_W + 2) + 400);
    char *buf  = (char *)malloc(size);
    if (buf == NULL)
        return FALSE;

    // Keep the comment block from the standard file at the top
    const char *first = strstr(defaultCaverns, "CAVERN");
    int n = first ? (int)(first - defaultCaverns) : 0;
    memcpy(buf, defaultCaverns, n);

    for (int i = 0; i < numCaverns; i++)
    {
        Cavern *cv = &caverns[i];
        n += wsprintf(buf + n, "CAVERN %s\n", cv->name);
        n += wsprintf(buf + n, "COLOURS %02X%02X%02X %02X%02X%02X\n",
                      GetRValue(cv->sky), GetGValue(cv->sky), GetBValue(cv->sky),
                      GetRValue(cv->skyLines), GetGValue(cv->skyLines), GetBValue(cv->skyLines));
        n += wsprintf(buf + n, "TILESET %d\n", cv->tileset + 1);
        n += wsprintf(buf + n, "MUSIC %d\n", cv->music + 1);
        n += wsprintf(buf + n, "MONSTER %d %d %d\n",
                      cv->monster[0] + 1, cv->monster[1] + 1, cv->monster[2] + 1);
        n += wsprintf(buf + n, "MOVES %s %s %s\n",
                      moveNames[cv->move[0]], moveNames[cv->move[1]], moveNames[cv->move[2]]);
        n += wsprintf(buf + n, "BOSS %s\nMAP\n", cv->boss ? "YES" : "NO");
        for (int ty = 0; ty < MAP_H; ty++)
        {
            memcpy(buf + n, cv->rows[ty], MAP_W);
            n += MAP_W;
            buf[n++] = '\n';
        }
        n += wsprintf(buf + n, "END\n\n");
    }
    buf[n] = 0;

    char path[MAX_PATH];
    ExePath(CAVERN_FILE, path);
    BOOL ok = WriteTextFile(path, buf);
    free(buf);

    if (!ok)
        MessageBox(hwndMain, "Couldn't save " CAVERN_FILE ".", "Cavern Runner", MB_ICONERROR);
    return ok;
}

BOOL CavernHasTile(const Cavern *cv, char c)
{
    for (int ty = 0; ty < MAP_H; ty++)
        if (memchr(cv->rows[ty], c, MAP_W))
            return TRUE;
    return FALSE;
}

// Is this cavern playable? Returns NULL if so, or what's wrong with it.
const char *CavernProblem(int i)
{
    int starts = 0, exits = 0;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
        {
            if (caverns[i].rows[ty][tx] == 'P') starts++;
            if (caverns[i].rows[ty][tx] == 'X') exits++;
        }
    if (starts == 0) return "it has no player start (P)";
    if (starts > 1)  return "it has more than one player start (P)";
    if (exits == 0)  return "it has no exit (X)";
    if (exits > 1)   return "it has more than one exit (X)";
    return NULL;
}

// The first playable cavern at or after 'from', or -1 if there isn't one
int NextPlayable(int from)
{
    for (int i = from; i < numCaverns; i++)
        if (CavernProblem(i) == NULL)
            return i;
    return -1;
}

int CountProblems(void)
{
    int bad = 0;
    for (int i = 0; i < numCaverns; i++)
        if (CavernProblem(i)) bad++;
    return bad;
}

// ============================================================================
//  sprites.txt
//
//      SPRITE PLAYER1
//      ...16 lines of 16 characters (32 of 32 for the boss):
//         . for transparent, 0-9 / A-F for colours
//      END
// ============================================================================

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;                          // '.', or anything else, is transparent
}

// Apply any sprites found in the text on top of the ones already loaded
static void ParseSprites(const char *text)
{
    int  cur = -1, row = 0;
    char line[256];

    while (*text)
    {
        text = NextLine(text, line, sizeof line);
        int len = lstrlen(line);

        if (cur >= 0)                   // inside a sprite: this is a row of pixels
        {
            for (int x = 0; x < SpriteSize(cur); x++)
                sprites[cur].px[row][x] = (signed char)HexValue(x < len ? line[x] : '.');
            if (++row == SpriteSize(cur))
                cur = -1;
            continue;
        }

        if (StartsWith(line, "SPRITE"))
        {
            char *id = (char *)SkipSpaces(line + 6);
            for (char *e = id + lstrlen(id); e > id && (e[-1] == ' ' || e[-1] == '\t'); )
                *--e = 0;               // trim trailing spaces
            cur = -1;
            row = 0;
            for (int i = 0; i < NUM_SPRITES; i++)
                if (lstrcmpi(id, spriteIds[i]) == 0)
                    cur = i;
            if (lstrcmpi(id, "ENEMY1") == 0) cur = SPR_MONSTER;         // names used by
            if (lstrcmpi(id, "ENEMY2") == 0) cur = SPR_MONSTER + 1;     // the first version
        }
    }
}

void LoadSprites(void)
{
    char path[MAX_PATH];
    memset(sprites, -1, sizeof sprites);    // anything not in a file stays transparent
    ParseSprites(defaultSprites);           // start from the standard designs...
    ExePath(SPRITE_FILE, path);

    char *text = ReadTextFile(path);
    if (text == NULL)
    {
        WriteTextFile(path, defaultSprites);    // first run: create the file
        return;
    }
    ParseSprites(text);                     // ...then use whatever the file changes
    free(text);
}

BOOL SaveSprites(void)
{
    char *buf = (char *)malloc(8000 + NUM_SPRITES * (SPR_MAX * (SPR_MAX + 1) + 40));
    if (buf == NULL)
        return FALSE;

    // Keep the comment block from the standard file at the top
    const char *first = strstr(defaultSprites, "SPRITE");
    int n = first ? (int)(first - defaultSprites) : 0;
    memcpy(buf, defaultSprites, n);

    for (int i = 0; i < NUM_SPRITES; i++)
    {
        // A comment before each monster and tile set, to help find your way
        if (i >= SPR_MONSTER && i < SPR_BOSS1 && (i - SPR_MONSTER) % 2 == 0)
            n += wsprintf(buf + n, "; Monster %d\n", (i - SPR_MONSTER) / 2 + 1);
        if (i >= SPR_TILES && (i - SPR_TILES) % TILES_PER_SET == 0)
            n += wsprintf(buf + n, "; Tile set %d\n", (i - SPR_TILES) / TILES_PER_SET + 1);

        n += wsprintf(buf + n, "SPRITE %s\n", spriteIds[i]);
        for (int y = 0; y < SpriteSize(i); y++)
        {
            for (int x = 0; x < SpriteSize(i); x++)
                buf[n++] = ".0123456789ABCDEF"[sprites[i].px[y][x] + 1];
            buf[n++] = '\n';
        }
        n += wsprintf(buf + n, "END\n\n");
    }
    buf[n] = 0;

    char path[MAX_PATH];
    ExePath(SPRITE_FILE, path);
    BOOL ok = WriteTextFile(path, buf);
    free(buf);

    if (!ok)
        MessageBox(hwndMain, "Couldn't save " SPRITE_FILE ".", "Cavern Runner", MB_ICONERROR);
    return ok;
}
