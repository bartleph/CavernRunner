// ============================================================================
//  designer.cpp - the pixel editor for characters and tiles
//  (press C on the title screen)
//
//  Tabs along the top choose between the characters and the four tile sets.
//  It edits sprites[] directly; sprites.txt is only written when you save.
// ============================================================================

#include "cavern.h"

#define GRID_X      40                  // the big editing grid
#define GRID_Y      64
#define GRID_PIX    416                 // its size on screen (16 x 26, or 32 x 13)
#define TAB_X       500                 // the tabs
#define TAB_Y       30
#define TAB_H       26
#define THUMB_X     500                 // the thumbnails, up to three rows of eight
#define THUMB_Y     74
#define THUMB       48
#define THUMB_STEP  56
#define FRAME_Y     252                 // the "Frame 1 / Frame 2" buttons
#define FRAME_W     110
#define SWATCH_X    500                 // the colour swatches
#define SWATCH_Y    320
#define SWATCH      36
#define SWATCH_GAP  6
#define PREVIEW_Y   412

BOOL sprModified;                       // unsaved changes

static int  curTab;                     // 0 = characters, 1-4 = tile sets
static int  tabItem[NUM_TABS] = { 0, NUM_CHARS, NUM_CHARS + TILE_ITEMS,
                                  NUM_CHARS + 2 * TILE_ITEMS, NUM_CHARS + 3 * TILE_ITEMS };
static int  itemSel;                    // the item being edited (an entry in items[])
static int  curFrame;                   // which of its frames
static int  sprSel;                     // the sprite that is: items[itemSel].first + curFrame
static int  sprColour = 8;              // selected colour; -1 = transparent
static int  mouseX = -1, mouseY = -1;
static Sprite sprClip;                  // for copy and paste
static BOOL sprHasClip;

// Undo: a copy of a sprite is saved before every change
#define MAX_UNDO 64
typedef struct
{
    int    sprite;
    Sprite before;
} UndoStep;

static UndoStep undoSteps[MAX_UNDO];
static int      undoCount;

// The items on a tab, in order. Returns how many.
static int TabItems(int tab, int *list)
{
    int n = 0;
    for (int i = 0; i < NUM_ITEMS; i++)
        if (items[i].tab == tab)
            list[n++] = i;
    return n;
}

static void SelectItem(int i, int frame)
{
    itemSel  = i;
    curTab   = items[i].tab;
    tabItem[curTab] = i;
    curFrame = (frame < items[i].frames) ? frame : 0;
    sprSel   = items[i].first + curFrame;
}

static void PushUndo(void)
{
    if (undoCount == MAX_UNDO)          // full: forget the oldest
    {
        memmove(&undoSteps[0], &undoSteps[1], (MAX_UNDO - 1) * sizeof(UndoStep));
        undoCount--;
    }
    undoSteps[undoCount].sprite = sprSel;
    undoSteps[undoCount].before = sprites[sprSel];
    undoCount++;
}

static void Undo(void)
{
    if (undoCount == 0)
        return;
    undoCount--;
    int n = undoSteps[undoCount].sprite;
    sprites[n] = undoSteps[undoCount].before;
    for (int i = 0; i < NUM_ITEMS; i++)         // show the item that changed
        if (n >= items[i].first && n < items[i].first + items[i].frames)
            SelectItem(i, n - items[i].first);
    sprModified = TRUE;
}

static int CellSize(void)
{
    return GRID_PIX / SpriteSize(sprSel);
}

// Which grid pixel is at this position? FALSE if none.
static BOOL GridCell(int mx, int my, int *cx, int *cy)
{
    if (mx < GRID_X || my < GRID_Y)
        return FALSE;
    *cx = (mx - GRID_X) / CellSize();
    *cy = (my - GRID_Y) / CellSize();
    return *cx < SpriteSize(sprSel) && *cy < SpriteSize(sprSel);
}

static void SprSet(int x, int y, int colour)
{
    if (sprites[sprSel].px[y][x] != colour)
    {
        sprites[sprSel].px[y][x] = (signed char)colour;
        sprModified = TRUE;
    }
}

// Fill the area of colour 'from' that includes (x, y) with colour 'to'
static void FloodFill(int x, int y, int from, int to)
{
    int size = SpriteSize(sprSel);
    if (x < 0 || x >= size || y < 0 || y >= size || from == to || sprites[sprSel].px[y][x] != from)
        return;
    sprites[sprSel].px[y][x] = (signed char)to;
    FloodFill(x + 1, y, from, to);
    FloodFill(x - 1, y, from, to);
    FloodFill(x, y + 1, from, to);
    FloodFill(x, y - 1, from, to);
}

static int TabX(int t)       { return TAB_X + (t == 0 ? 0 : 110 + (t - 1) * 82); }
static int TabW(int t)       { return t == 0 ? 104 : 76; }

static void ThumbPos(int i, int *x, int *y)
{
    *x = THUMB_X + (i % 8) * THUMB_STEP;
    *y = THUMB_Y + (i / 8) * THUMB_STEP;
}

// Where colour swatch i is drawn (swatch 0 is "transparent")
static void SwatchPos(int i, int *x, int *y)
{
    *x = SWATCH_X + (i % 9) * (SWATCH + SWATCH_GAP);
    *y = SWATCH_Y + (i / 9) * (SWATCH + SWATCH_GAP);
}

void DesignMouseDown(int mx, int my, BOOL right)
{
    int list[NUM_ITEMS], count = TabItems(curTab, list);

    // A tab
    for (int t = 0; t < NUM_TABS; t++)
        if (mx >= TabX(t) && mx < TabX(t) + TabW(t) && my >= TAB_Y && my < TAB_Y + TAB_H)
        {
            SelectItem(tabItem[t], 0);
            return;
        }

    // A thumbnail picks what to edit
    for (int i = 0; i < count; i++)
    {
        int tx, ty;
        ThumbPos(i, &tx, &ty);
        if (mx >= tx && mx < tx + THUMB && my >= ty && my < ty + THUMB)
        {
            SelectItem(list[i], 0);
            return;
        }
    }

    // The frame buttons
    if (my >= FRAME_Y && my < FRAME_Y + 28)
        for (int f = 0; f < items[itemSel].frames; f++)
            if (mx >= THUMB_X + f * (FRAME_W + 10) && mx < THUMB_X + f * (FRAME_W + 10) + FRAME_W)
            {
                SelectItem(itemSel, f);
                return;
            }

    // A swatch picks the colour
    for (int i = 0; i <= 16; i++)
    {
        int sx, sy;
        SwatchPos(i, &sx, &sy);
        if (mx >= sx && mx < sx + SWATCH && my >= sy && my < sy + SWATCH)
        {
            sprColour = i - 1;
            return;
        }
    }

    int cx, cy;
    if (!GridCell(mx, my, &cx, &cy))
        return;

    int colour = right ? -1 : sprColour;
    if (GetKeyState(VK_CONTROL) < 0)            // Ctrl+click: pick up a colour
    {
        sprColour = sprites[sprSel].px[cy][cx];
        return;
    }

    PushUndo();
    if (GetKeyState(VK_SHIFT) < 0)              // Shift+click: fill an area
    {
        FloodFill(cx, cy, sprites[sprSel].px[cy][cx], colour);
        sprModified = TRUE;
        return;
    }

    SetCapture(hwndMain);                       // keep painting while dragging
    SprSet(cx, cy, colour);
}

void DesignMouseMove(int mx, int my, WPARAM buttons)
{
    int cx, cy;
    mouseX = mx;
    mouseY = my;
    if (GetCapture() != hwndMain || !GridCell(mx, my, &cx, &cy))
        return;
    if (buttons & MK_LBUTTON)
        SprSet(cx, cy, sprColour);
    else if (buttons & MK_RBUTTON)
        SprSet(cx, cy, -1);
}

// Flip with H or V, or slide the picture one pixel with the arrow keys
static void SpriteTransform(WPARAM op)
{
    int size = SpriteSize(sprSel);
    Sprite old = sprites[sprSel];
    PushUndo();
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            int sx = x, sy = y;         // the pixel that moves into (x, y)
            switch (op)
            {
            case 'H':      sx = size - 1 - x;          break;
            case 'V':      sy = size - 1 - y;          break;
            case VK_LEFT:  sx = (x + 1) % size;        break;
            case VK_RIGHT: sx = (x + size - 1) % size; break;
            case VK_UP:    sy = (y + 1) % size;        break;
            case VK_DOWN:  sy = (y + size - 1) % size; break;
            }
            sprites[sprSel].px[y][x] = old.px[sy][sx];
        }
    sprModified = TRUE;
}

void EnterDesigner(void)
{
    undoCount   = 0;
    sprModified = FALSE;
    SelectItem(itemSel, curFrame);
    SetState(ST_SPRITES);
}

// Offer to save. Returns FALSE if the user cancels.
static BOOL AskToSave(void)
{
    if (!sprModified)
        return TRUE;
    int r = MessageBox(hwndMain, "Save your artwork to " SPRITE_FILE "?", "Designer",
                       MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL)
        return FALSE;
    if (r == IDYES && !SaveSprites())
        return FALSE;
    if (r == IDNO)
        LoadSprites();                  // throw the changes away
    sprModified = FALSE;
    return TRUE;
}

BOOL DesignCanClose(void)
{
    return state != ST_SPRITES || AskToSave();
}

void DesignKey(WPARAM key)
{
    if (GetKeyState(VK_CONTROL) < 0)
    {
        switch (key)
        {
        case 'Z':
            Undo();
            break;
        case 'C':
            sprClip    = sprites[sprSel];
            sprHasClip = TRUE;
            break;
        case 'V':                       // between sizes, the top-left corner is kept
            if (sprHasClip)
            {
                PushUndo();
                sprites[sprSel] = sprClip;
                sprModified = TRUE;
            }
            break;
        case 'S':
            if (SaveSprites())
            {
                sprModified = FALSE;
                savedTimer  = 120;
            }
            break;
        }
        return;
    }

    int list[NUM_ITEMS], count = TabItems(curTab, list), pos = 0;
    for (int i = 0; i < count; i++)
        if (list[i] == itemSel)
            pos = i;

    switch (key)
    {
    case VK_ESCAPE:
        if (AskToSave())
            ShowTitle();
        break;
    case VK_TAB:                        // next tab
        SelectItem(tabItem[(curTab + 1) % NUM_TABS], 0);
        break;
    case VK_PRIOR:
        if (pos > 0) SelectItem(list[pos - 1], 0);
        break;
    case VK_NEXT:
        if (pos + 1 < count) SelectItem(list[pos + 1], 0);
        break;
    case 'F':
        SelectItem(itemSel, (curFrame + 1) % items[itemSel].frames);
        break;
    case 'H': case 'V':
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        SpriteTransform(key);
        break;
    case VK_DELETE:
        PushUndo();
        memset(sprites[sprSel].px, -1, sizeof sprites[sprSel].px);
        sprModified = TRUE;
        break;
    }
}

// ============================================================================
//  Drawing the designer
// ============================================================================

// A list of the caverns using a monster design or a tile set, e.g. "1, 4"
static void CavernList(char *list, BOOL (*uses)(const Cavern *, int), int what)
{
    int n = 0, count = 0;
    list[0] = 0;
    for (int i = 0; i < numCaverns && n < 80; i++)
        if (uses(&caverns[i], what))
            n += wsprintf(list + n, "%s%d", count++ ? ", " : "", i + 1);
}

static BOOL UsesMonster(const Cavern *cv, int m)
{
    for (int k = 0; k < MONSTER_KINDS; k++)
        if (cv->monster[k] == m && CavernHasTile(cv, 'E' + k))
            return TRUE;
    return FALSE;
}

static BOOL UsesTileset(const Cavern *cv, int s)
{
    return cv->tileset == s;
}

// The preview: the item in use, at actual size
static void DrawPreview(HDC dc, int boxY)
{
    int boxW = 13 * TILE, boxH = 130;
    int floorY = boxY + boxH - 26;
    const DesignItem *it = &items[itemSel];

    // A cavern background: one that uses this tile set if there is one
    COLORREF sky = cav->sky;
    for (int i = numCaverns - 1; i >= 0; i--)
        if (curTab > 0 && caverns[i].tileset == curTab - 1)
            sky = caverns[i].sky;
    Box(dc, THUMB_X, boxY, boxW, boxH, sky);

    if (curTab > 0)
    {
        // Tiles. Floors fill the bottom row; things that stand on floors
        // stand on a row of this set's platforms.
        int set = curTab - 1, t = it->first - TileSprite(set, 0);
        BOOL floorTile = (t == T_WALL || t == T_PLATFORM || t == T_CRUMBLE ||
                          t == T_CONVEYOR || t == T_MOVER);
        for (int i = 0; i < 13; i++)
        {
            int x = THUMB_X + i * TILE;
            if (t == T_CONVEYOR)
                DrawSpriteEx(dc, it->first, x, floorY, SPR_SCALE, FALSE, frameCount / 3, 0);
            else if (t == T_CRUMBLE)
                DrawSpriteEx(dc, it->first, x, floorY, SPR_SCALE, FALSE, 0, (frameCount / 12 + i) % 16);
            else
                DrawSprite(dc, floorTile ? it->first : TileSprite(set, T_PLATFORM), x, floorY, SPR_SCALE, FALSE);
        }
        if (t == T_ROPE)
            for (int i = 0; i < 3; i++)
                DrawSprite(dc, it->first, THUMB_X + 6 * TILE, boxY + i * TILE, SPR_SCALE, FALSE);
        else if (t == T_SWITCH_OFF)
        {
            DrawSprite(dc, TileSprite(set, T_SWITCH_OFF), THUMB_X + 4 * TILE, floorY - TILE, SPR_SCALE, FALSE);
            DrawSprite(dc, TileSprite(set, T_SWITCH_ON),  THUMB_X + 8 * TILE, floorY - TILE, SPR_SCALE, FALSE);
        }
        else if (t == T_DOOR)
            for (int i = 1; i <= 2; i++)
                DrawSprite(dc, it->first, THUMB_X + 6 * TILE, floorY - i * TILE, SPR_SCALE, FALSE);
        else if (!floorTile)
            for (int i = 0; i < 3; i++)
                DrawSprite(dc, it->first, THUMB_X + (3 + i * 3) * TILE, floorY - TILE, SPR_SCALE, FALSE);
        return;
    }

    // Characters walk back and forth over a platform
    for (int i = 0; i < 13; i++)
        DrawSprite(dc, TileSprite(cav->tileset, T_PLATFORM), THUMB_X + i * TILE, floorY, SPR_SCALE, FALSE);

    int pixels = SpriteSize(it->first) * SPR_SCALE;     // size on screen
    if (itemSel == CHAR_KEY)
    {
        int bob = ((frameCount / 10) % 2) * 2;
        DrawSprite(dc, SPR_KEY, THUMB_X + boxW / 2 - pixels / 2, floorY - pixels - 20 + bob, SPR_SCALE, FALSE);
        return;
    }
    int frame = (frameCount / (itemSel == CHAR_BOSS ? 12 : 8)) % 2;
    int range = boxW - pixels - 20;
    int t     = (frameCount * 2) % (2 * range);
    int x     = (t < range) ? t : 2 * range - t;
    int y     = floorY - pixels;
    if (itemSel == CHAR_BOSS)                           // the boss floats
        y = boxY + 12 + (int)(sinf(frameCount * 0.08f) * 8);
    DrawSprite(dc, it->first + frame, THUMB_X + 10 + x, y, SPR_SCALE, t >= range);
}

void DrawDesigner(HDC dc)
{
    char s[160], list[100];
    COLORREF label = RGB(170, 200, 230);
    const DesignItem *it = &items[itemSel];
    int size = SpriteSize(sprSel);
    int cell = CellSize();

    Box(dc, 0, 0, GAME_W, GAME_H, RGB(16, 16, 28));

    // Title, with the switch's frames named "off" and "on"
    if (it->frames > 1 && curTab > 0)
        wsprintf(s, "DESIGNER   Tile set %d: %s (%s)%s", curTab, it->name, curFrame ? "on" : "off",
                 sprModified ? "  *" : "");
    else if (it->frames > 1)
        wsprintf(s, "DESIGNER   %s, frame %d%s", it->name, curFrame + 1, sprModified ? "  *" : "");
    else if (curTab > 0)
        wsprintf(s, "DESIGNER   Tile set %d: %s%s", curTab, it->name, sprModified ? "  *" : "");
    else
        wsprintf(s, "DESIGNER   %s%s", it->name, sprModified ? "  *" : "");
    TextAt(dc, hFontHud, GRID_X, 22, s, RGB(255, 230, 80));

    // The big grid. Transparent pixels show as a checkerboard.
    Box(dc, GRID_X - 1, GRID_Y - 1, size * cell + 1, size * cell + 1, RGB(0, 0, 0));
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            int c = sprites[sprSel].px[y][x];
            COLORREF col = (c >= 0) ? palette[c]
                         : ((x + y) & 1) ? RGB(62, 62, 74) : RGB(46, 46, 56);
            Box(dc, GRID_X + x * cell, GRID_Y + y * cell, cell - 1, cell - 1, col);
        }

    // The collision box for characters: the part that counts when touching
    // things. Walkers stand on its bottom edge; the boss floats, so it's centred.
    if (curTab == 0 && itemSel != CHAR_KEY)
    {
        int w, h, x0, y0;
        if (itemSel == CHAR_BOSS)
        {
            w = h = BOSS_HIT / SPR_SCALE;
            x0 = y0 = (size - w) / 2;
        }
        else
        {
            BOOL player = (itemSel == CHAR_PLAYER);
            w  = (player ? PLAYER_W : ENEMY_W) / SPR_SCALE;
            h  = (player ? PLAYER_H : ENEMY_H) / SPR_SCALE;
            x0 = (size - w) / 2;
            y0 = size - h;
        }
        Frame(dc, GRID_X + x0 * cell - 2, GRID_Y + y0 * cell - 2,
              w * cell + 3, h * cell + 3, 2, RGB(255, 210, 60));
    }

    int cx, cy;
    if (GridCell(mouseX, mouseY, &cx, &cy))
        Frame(dc, GRID_X + cx * cell - 2, GRID_Y + cy * cell - 2, cell + 3, cell + 3, 2, RGB(255, 255, 255));

    // Notes under the grid
    int noteY = GRID_Y + GRID_PIX + 14;
    if (curTab == 0)
    {
        if (itemSel != CHAR_KEY)
            TextAt(dc, hFontSmall, GRID_X, noteY, "Yellow box: the part that counts in collisions", label);
        TextAt(dc, hFontSmall, GRID_X, noteY + 18, "Draw facing right; the game flips it for going left", label);
        if (itemSel >= CHAR_MONSTER1 && itemSel < CHAR_MONSTER1 + NUM_MONSTERS)
        {
            CavernList(list, UsesMonster, itemSel - CHAR_MONSTER1);
            if (list[0])
                wsprintf(s, "Used in cavern%s %s", strchr(list, ',') ? "s" : "", list);
            else
                lstrcpy(s, "Not used yet: choose it with M in the cavern editor");
            TextAt(dc, hFontSmall, GRID_X, noteY + 36, s, RGB(255, 255, 255));
        }
        else if (itemSel == CHAR_BOSS)
            TextAt(dc, hFontSmall, GRID_X, noteY + 36,
                   "32 x 32. Turns up at random; B in the cavern editor", RGB(255, 255, 255));
    }
    else
    {
        static const char *hint[TILES_PER_SET] =
        {
            "Blocks from every side",
            "Stood on along its top edge; you can jump up through it",
            "Gaps appear in it as it crumbles away",
            "The picture slides along, left or right",
            "Deadly: only the bottom half counts",
            "Deadly to touch",
            "Off and on. Touching it opens all the doors",
            "Off and on. Touching it opens all the doors",
            "A wall until a switch is touched",
            "Climbed with Up and Down; draw it down the middle",
            "Repeated along the platform's length",
        };
        int t = it->first - TileSprite(curTab - 1, 0);
        TextAt(dc, hFontSmall, GRID_X, noteY, "Tiles fill a 32 x 32 square in the game", label);
        TextAt(dc, hFontSmall, GRID_X, noteY + 18, hint[t], label);
        CavernList(list, UsesTileset, curTab - 1);
        if (list[0])
            wsprintf(s, "Tile set %d is used in cavern%s %s", curTab, strchr(list, ',') ? "s" : "", list);
        else
            lstrcpy(s, "Not used yet: choose it with L in the cavern editor");
        TextAt(dc, hFontSmall, GRID_X, noteY + 36, s, RGB(255, 255, 255));
    }

    if (savedTimer > 0)
        TextAt(dc, hFontHud, GRID_X, noteY + 62, "Saved to " SPRITE_FILE, RGB(120, 255, 120));

    // The tabs
    for (int t = 0; t < NUM_TABS; t++)
    {
        BOOL on = (t == curTab);
        Box(dc, TabX(t), TAB_Y, TabW(t), TAB_H, on ? RGB(255, 230, 80) : RGB(60, 60, 80));
        if (t == 0)
            lstrcpy(s, "Characters");
        else
            wsprintf(s, "Tiles %d", t);
        TextAt(dc, hFontSmall, TabX(t) + 8, TAB_Y + 5, s, on ? RGB(0, 0, 0) : RGB(255, 255, 255));
    }

    // The thumbnails on this tab (first frame of each)
    int items_[NUM_ITEMS], count = TabItems(curTab, items_);
    for (int i = 0; i < count; i++)
    {
        const DesignItem *ti = &items[items_[i]];
        int tx, ty;
        ThumbPos(i, &tx, &ty);
        int sz    = SpriteSize(ti->first);
        int scale = THUMB / sz;                     // 3 for 16 x 16, 1 for the boss
        int off   = (THUMB - sz * scale) / 2;
        Box(dc, tx, ty, THUMB, THUMB, cav->sky);
        DrawSprite(dc, ti->first, tx + off, ty + off, scale, FALSE);
        if (SpriteBlank(ti->first))
            TextAt(dc, hFontSmall, tx + 20, ty + 16, "?", RGB(120, 120, 150));
        if (items_[i] == itemSel)
            Frame(dc, tx - 4, ty - 4, THUMB + 8, THUMB + 8, 2, RGB(255, 230, 80));
    }

    // Frame buttons
    for (int f = 0; f < it->frames; f++)
    {
        int bx = THUMB_X + f * (FRAME_W + 10);
        Box(dc, bx, FRAME_Y, FRAME_W, 28, f == curFrame ? RGB(255, 230, 80) : RGB(60, 60, 80));
        if (curTab > 0)
            lstrcpy(s, f ? "On" : "Off");
        else
            wsprintf(s, "Frame %d", f + 1);
        TextAt(dc, hFontSmall, bx + 24, FRAME_Y + 6, s, f == curFrame ? RGB(0, 0, 0) : RGB(255, 255, 255));
    }
    if (it->frames == 1)
        TextAt(dc, hFontSmall, THUMB_X, FRAME_Y + 6, "(one frame only)", label);

    // The colour swatches; the first one is "transparent"
    TextAt(dc, hFontSmall, SWATCH_X, SWATCH_Y - 22, "Colours", label);
    for (int i = 0; i <= 16; i++)
    {
        int sx, sy;
        SwatchPos(i, &sx, &sy);
        if (i == 0)
        {
            int half = SWATCH / 2;
            Box(dc, sx, sy, half, half, RGB(62, 62, 74));
            Box(dc, sx + half, sy, half, half, RGB(46, 46, 56));
            Box(dc, sx, sy + half, half, half, RGB(46, 46, 56));
            Box(dc, sx + half, sy + half, half, half, RGB(62, 62, 74));
        }
        else
            Box(dc, sx, sy, SWATCH, SWATCH, palette[i - 1]);
        if (i - 1 == sprColour)
            Frame(dc, sx - 4, sy - 4, SWATCH + 8, SWATCH + 8, 3, RGB(255, 255, 255));
    }
    wsprintf(s, "Colour: %s", sprColour < 0 ? "Transparent" : colourNames[sprColour]);
    TextAt(dc, hFontSmall, SWATCH_X + 9 * (SWATCH + SWATCH_GAP) - 150, SWATCH_Y - 22, s, RGB(255, 255, 255));

    // Preview
    TextAt(dc, hFontSmall, THUMB_X, PREVIEW_Y, "Preview, actual size", label);
    DrawPreview(dc, PREVIEW_Y + 22);

    // Help
    static const char *help[] =
    {
        "Left-click: paint    Right-click: transparent",
        "Shift+click: fill    Ctrl+click: pick colour",
        "Arrows: slide        H / V: flip",
        "Ctrl+Z: undo         Ctrl+C / V: copy / paste",
        "F: frame   Tab: tab  PgUp / PgDn: next item",
        "Del: clear   Ctrl+S: save   Esc: back to title",
    };
    for (int i = 0; i < 6; i++)
        TextAt(dc, hFontSmall, THUMB_X, 572 + i * 20, help[i], label);
}
