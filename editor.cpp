// ============================================================================
//  editor.cpp - the cavern editor (press E on the title screen)
//
//  It edits caverns[] directly; caverns.txt is only written when you save.
// ============================================================================

#include "cavern.h"

BOOL editModified;                      // unsaved changes

// The palette: every tile you can paint, in the order shown
static const char paletteChars[] = "#=-<>^*kEFGsD|~PX ";
static const char *paletteNames[] =
{
    "Wall", "Platform", "Crumbling platform", "Conveyor left", "Conveyor right",
    "Spikes", "Poison plant", "Key", "Monster E", "Monster F", "Monster G",
    "Switch (opens doors)", "Door", "Rope", "Moving platform", "Player start", "Exit", "Eraser"
};
#define PALETTE_N   18
#define PAL_PER_ROW 9
#define PAL_X       16                  // palette position in the status strip
#define PAL_Y       (VIEW_H + 30)
#define PAL_STEP    34
#define HELP_X      340

static int curTile;                     // selected palette entry
static int mouseX = -1, mouseY = -1;    // last mouse position, for the hover box

// The monster letter (0-2 for E-G) that's selected in the palette, or -1
static int SelectedKind(void)
{
    char c = paletteChars[curTile];
    return (c >= 'E' && c <= 'G') ? c - 'E' : -1;
}

void EnterEditor(int n)
{
    checkDone = FALSE;                  // a check belongs to one cavern
    if (n < 0) n = 0;
    if (n >= numCaverns) n = numCaverns - 1;
    float keepCam = camX;               // stay at the same scroll position
    testMode = FALSE;
    LoadLevel(n);                       // sets cav, and resets the crumbling tiles
    camX = (keepCam > MaxCamera()) ? MaxCamera() : keepCam;
    SetState(ST_EDIT);
}

// Put a tile at a position in the play area
static void EditPaint(int mx, int my, char c)
{
    if (mx < 0 || mx >= VIEW_W || my < 0 || my >= VIEW_H)
        return;
    int tx = (mx + (int)camX) / TILE;
    int ty = my / TILE;
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H || cav->rows[ty][tx] == c)
        return;

    // Only one start and one exit: placing a new one removes the old
    if (c == 'P' || c == 'X')
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (cav->rows[y][x] == c)
                    cav->rows[y][x] = ' ';

    cav->rows[ty][tx] = c;
    editModified = TRUE;
    checkDone = FALSE;                  // the map changed: the check is out of date
}

static void EditNewCavern(void)
{
    if (numCaverns >= MAX_CAVERNS)
    {
        MessageBox(hwndMain, "That's the most caverns there can be.", "Editor", MB_ICONWARNING);
        return;
    }

    // Make room just after the current cavern
    int n = cavNum + 1;
    memmove(&caverns[n + 1], &caverns[n], (numCaverns - n) * sizeof(Cavern));
    numCaverns++;

    // A blank cavern with walls round the edge, a start and an exit
    Cavern *cv = &caverns[n];
    ClearCavern(cv, "New Cavern", n);
    for (int x = 0; x < MAP_W; x++)
        cv->rows[0][x] = cv->rows[MAP_H - 1][x] = '#';
    for (int y = 0; y < MAP_H; y++)
        cv->rows[y][0] = cv->rows[y][MAP_W - 1] = '#';
    cv->rows[MAP_H - 3][2]         = 'P';
    cv->rows[MAP_H - 3][MAP_W - 4] = 'X';

    editModified = TRUE;
    camX = 0;
    EnterEditor(n);
}

static void EditDeleteCavern(void)
{
    if (numCaverns <= 1)
    {
        MessageBox(hwndMain, "There must always be at least one cavern.", "Editor", MB_ICONWARNING);
        return;
    }
    char msg[120];
    wsprintf(msg, "Delete the cavern \"%s\"?", cav->name);
    if (MessageBox(hwndMain, msg, "Editor", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    int n = cavNum;
    memmove(&caverns[n], &caverns[n + 1], (numCaverns - n - 1) * sizeof(Cavern));
    numCaverns--;
    editModified = TRUE;
    EnterEditor(n < numCaverns ? n : numCaverns - 1);
}

static void EditRename(void)
{
    char buf[64];
    lstrcpy(buf, cav->name);
    if (InputBox(hwndMain, "Rename Cavern", "Cavern name:", buf, sizeof buf) && buf[0])
    {
        lstrcpy(cav->name, buf);
        editModified = TRUE;
    }
}

static void EditTest(void)
{
    const char *problem = CavernProblem(cavNum);
    if (problem)
    {
        char msg[150];
        wsprintf(msg, "This cavern can't be played yet: %s.", problem);
        MessageBox(hwndMain, msg, "Editor", MB_ICONWARNING);
        return;
    }
    score    = 0;
    lives    = START_LIVES;
    testMode = TRUE;                    // set first: LoadLevel uses it for the boss
    LoadLevel(cavNum);
    Play(SND_START);
}

// Offer to save. Returns FALSE if the user cancels.
static BOOL AskToSave(void)
{
    if (!editModified)
        return TRUE;
    int r = MessageBox(hwndMain, "Save your changes to " CAVERN_FILE "?", "Editor",
                       MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL)
        return FALSE;
    if (r == IDYES && !SaveCaverns())
        return FALSE;
    if (r == IDNO)
        LoadCaverns();                  // throw the changes away
    editModified = FALSE;
    return TRUE;
}

BOOL EditCanClose(void)
{
    return state != ST_EDIT || AskToSave();
}

void EditKey(WPARAM key)
{
    BOOL ctrl  = GetKeyState(VK_CONTROL) < 0;
    BOOL shift = GetKeyState(VK_SHIFT) < 0;
    int  kind  = SelectedKind() >= 0 ? SelectedKind() : 0;   // E if no monster is selected

    if (key >= '1' && key <= '9')
        curTile = (int)(key - '1');
    else if (key == '0')
        curTile = 9;
    else switch (key)
    {
    case VK_ESCAPE:
        if (AskToSave())
            ShowTitle();
        break;
    case 'T':       EditTest();             break;
    case 'V':       CheckCavern();          break;
    case 'N':       EditNewCavern();        break;
    case VK_DELETE: EditDeleteCavern();     break;
    case 'R':       EditRename();           break;
    case VK_HOME:   camX = 0;               break;
    case VK_END:    camX = MaxCamera();     break;
    case VK_PRIOR:
        if (cavNum > 0) EnterEditor(cavNum - 1);
        break;
    case VK_NEXT:
        if (cavNum + 1 < numCaverns) EnterEditor(cavNum + 1);
        break;

    case 'C':                           // background colours
        SetScheme(cav, (cav->scheme + 1) % NUM_SCHEMES);
        editModified = TRUE;
        break;
    case 'L':                           // tile set ("look")
        cav->tileset = (cav->tileset + (shift ? NUM_TILESETS - 1 : 1)) % NUM_TILESETS;
        editModified = TRUE;
        break;
    case 'M':                           // monster design (Shift goes backwards)
        cav->monster[kind] = (cav->monster[kind] + (shift ? NUM_MONSTERS - 1 : 1)) % NUM_MONSTERS;
        editModified = TRUE;
        break;
    case 'W':                           // how the monster moves
        cav->move[kind] = (cav->move[kind] + (shift ? NUM_MOVES - 1 : 1)) % NUM_MOVES;
        editModified = TRUE;
        break;
    case 'B':
        cav->boss = !cav->boss;
        editModified = TRUE;
        break;
    case 'U':                           // the tune
        cav->music = (cav->music + (shift ? NUM_TUNES - 1 : 1)) % NUM_TUNES;
        editModified = TRUE;
        break;
    case 'I':                           // bring in standard caverns you don't have
    {
        int n = ImportDefaultCaverns();
        char msg[120];
        if (n > 0)
        {
            wsprintf(msg, "Added %d standard cavern%s at the end of the list.\n"
                          "Press Ctrl+S to keep %s.", n, n == 1 ? "" : "s", n == 1 ? "it" : "them");
            editModified = TRUE;
        }
        else
            lstrcpy(msg, "You already have all the standard caverns.");
        MessageBox(hwndMain, msg, "Editor", MB_ICONINFORMATION);
        EnterEditor(cavNum);
        break;
    }

    case 'S':
        if (ctrl)
        {
            if (SaveCaverns())
            {
                editModified = FALSE;
                savedTimer   = 120;
            }
        }
        else
            soundOn = !soundOn;
        break;
    }
}

static BOOL PaletteHit(int x, int y, int *index)
{
    for (int i = 0; i < PALETTE_N; i++)
    {
        int ix = PAL_X + (i % PAL_PER_ROW) * PAL_STEP;
        int iy = PAL_Y + (i / PAL_PER_ROW) * 33;
        if (x >= ix && x < ix + TILE && y >= iy && y < iy + TILE)
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

void EditMouseDown(int x, int y, BOOL right)
{
    int i;
    if (!right && PaletteHit(x, y, &i))         // a click on the palette picks a tile
    {
        curTile = i;
        return;
    }
    if (y >= VIEW_H)
        return;
    SetCapture(hwndMain);                       // keep getting mouse moves while dragging
    EditPaint(x, y, right ? ' ' : paletteChars[curTile]);
}

void EditMouseMove(int x, int y, WPARAM buttons)
{
    mouseX = x;
    mouseY = y;
    if (buttons & MK_LBUTTON) EditPaint(x, y, paletteChars[curTile]);
    if (buttons & MK_RBUTTON) EditPaint(x, y, ' ');
}

void EditWheel(int delta)
{
    camX -= delta / 120 * 2 * TILE;
    if (camX < 0) camX = 0;
    if (camX > MaxCamera()) camX = MaxCamera();
}

// ============================================================================
//  Drawing the editor
// ============================================================================

// The map straight from the cavern, with the sprites standing in their tiles
static void DrawEditMap(HDC dc, int cam)
{
    int firstCol = cam / TILE;
    for (int ty = 0; ty < MAP_H; ty++)
    {
        for (int tx = firstCol; tx <= firstCol + VIEW_W / TILE && tx < MAP_W; tx++)
        {
            char c  = cav->rows[ty][tx];
            int  sx = tx * TILE - cam, sy = ty * TILE;
            switch (c)
            {
            case ' ': break;
            case 'E': case 'F': case 'G':
                DrawEnemy(dc, sx + (TILE - ENEMY_W) / 2, sy + TILE - ENEMY_H, 1, cav->monster[c - 'E']);
                break;
            case 'P': DrawPlayer(dc, sx + (TILE - PLAYER_W) / 2, sy + TILE - PLAYER_H); break;
            case 'X': DrawExit(dc, sx, sy, FALSE);                                       break;
            default:  DrawTile(dc, c, tx, ty, sx, sy);                                   break;
            }
        }
    }

    // A faint grid, and a box round the tile under the mouse
    for (int x = -(cam % TILE); x < VIEW_W; x += TILE)
        Box(dc, x, 0, 1, VIEW_H, cav->skyLines);
    for (int y = 0; y < VIEW_H; y += TILE)
        Box(dc, 0, y, VIEW_W, 1, cav->skyLines);

    if (mouseX >= 0 && mouseX < VIEW_W && mouseY >= 0 && mouseY < VIEW_H)
    {
        int tx = (mouseX + cam) / TILE;
        Frame(dc, tx * TILE - cam, (mouseY / TILE) * TILE, TILE, TILE, 2, RGB(255, 255, 255));
    }

    if (!checkDone)
        return;

    // The checker's results: a mark at the feet wherever you can stand -
    // green if you can get back to the start from there, orange if not -
    // and flashing red boxes round anything that can't be reached
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = firstCol; tx <= firstCol + VIEW_W / TILE && tx < MAP_W; tx++)
        {
            int sx = tx * TILE - cam, sy = ty * TILE;
            if (checkMark[ty][tx])
                Box(dc, sx + 10, sy + TILE - 6, 12, 4,
                    checkMark[ty][tx] == 1 ? RGB(80, 255, 80) : RGB(255, 160, 0));
            if (CheckMissed(tx, ty) && (frameCount / 15) % 2 == 0)
                Frame(dc, sx - 2, sy - 2, TILE + 4, (cav->rows[ty][tx] == 'X' ? 2 * TILE : TILE) + 4,
                      3, RGB(255, 40, 40));
        }

    Box(dc, 0, 0, VIEW_W, 30, RGB(0, 0, 0));
    CenterText(dc, hFontHud, checkSummary, 4, checkAllGood ? RGB(120, 255, 120) : RGB(255, 100, 100));
}

// One palette entry, drawn in a 32 x 32 square
static void DrawPaletteIcon(HDC dc, char c, int x, int y)
{
    Box(dc, x, y, TILE, TILE, cav->sky);
    switch (c)
    {
    case 'E': case 'F': case 'G':
        DrawEnemy(dc, x + TILE / 2 - ENEMY_W / 2, y + TILE - ENEMY_H, 1, cav->monster[c - 'E']);
        break;
    case 'P':
        DrawPlayer(dc, x + TILE / 2 - PLAYER_W / 2, y + TILE - PLAYER_H);
        break;
    case 'X':                                    // a small closed gate
        Box(dc, x + 6, y + 2, 20, 28, RGB(30, 30, 50));
        Frame(dc, x + 5, y + 1, 22, 30, 2, RGB(120, 120, 140));
        for (int i = 0; i < 3; i++)
            Box(dc, x + 10 + i * 6, y + 3, 2, 26, RGB(150, 150, 170));
        break;
    case ' ':                                    // eraser: a red cross
        SetDCPenColor(dc, RGB(255, 70, 70));
        MoveToEx(dc, x + 6, y + 6, NULL);  LineTo(dc, x + 26, y + 26);
        MoveToEx(dc, x + 26, y + 6, NULL); LineTo(dc, x + 6, y + 26);
        break;
    default:
        DrawTile(dc, c, 0, 0, x, y);
        break;
    }
}

static void DrawEditHud(HDC dc)
{
    int y = VIEW_H;
    char s[160];

    Box(dc, 0, y, GAME_W, HUD_H, RGB(0, 0, 0));
    Box(dc, 0, y, GAME_W, 2, RGB(90, 90, 120));

    wsprintf(s, "EDITOR   Cavern %d of %d: %s%s", cavNum + 1, numCaverns, cav->name,
             editModified ? "  *" : "");
    TextAt(dc, hFontHud, 16, y + 6, s, RGB(255, 230, 80));

    // Right-hand side: saved, a problem, or the selected tile
    const char *problem = CavernProblem(cavNum);
    COLORREF col = RGB(255, 255, 255);
    if (savedTimer > 0)
    {
        lstrcpy(s, "Saved to " CAVERN_FILE);
        col = RGB(120, 255, 120);
    }
    else if (problem)
    {
        wsprintf(s, "Not playable: %s", problem);
        col = RGB(255, 100, 100);
    }
    else
        wsprintf(s, "Tile: %s", paletteNames[curTile]);
    TextAt(dc, hFontSmall, 530, y + 10, s, col);

    // The palette, two rows, with a box round the selected tile
    for (int i = 0; i < PALETTE_N; i++)
    {
        int ix = PAL_X + (i % PAL_PER_ROW) * PAL_STEP;
        int iy = PAL_Y + (i / PAL_PER_ROW) * 33;
        DrawPaletteIcon(dc, paletteChars[i], ix, iy);
        if (i == curTile)
            Frame(dc, ix - 2, iy - 2, TILE + 4, TILE + 4, 2, RGB(255, 230, 80));
    }

    COLORREF help = RGB(170, 200, 230);
    TextAt(dc, hFontSmall, HELP_X, y + 30, "Click: paint  Right-click: erase  1-0: tile  Arrows: scroll", help);
    TextAt(dc, hFontSmall, HELP_X, y + 46, "PgUp/PgDn: cavern  T: test  V: check  I: import  Ctrl+S: save", help);
    wsprintf(s, "N: new  Del: delete  R: rename  C: sky  L: tiles %d  U: tune %d  B: boss %s",
             cav->tileset + 1, cav->music + 1, cav->boss ? "on" : "off");
    TextAt(dc, hFontSmall, HELP_X, y + 62, s, help);

    // The three monster kinds: their designs and movements. A blank design
    // that's on the map gets a warning.
    int n = wsprintf(s, "M/W: monster look/move ");
    col = RGB(255, 255, 255);
    for (int k = 0; k < MONSTER_KINDS; k++)
    {
        char m[16];
        lstrcpy(m, moveNames[cav->move[k]]);
        CharLower(m);
        n += wsprintf(s + n, " %c:%d %s", 'E' + k, cav->monster[k] + 1, m);
    }
    for (int k = 0; k < MONSTER_KINDS; k++)
        if (CavernHasTile(cav, 'E' + k) && SpriteBlank(SPR_MONSTER + cav->monster[k] * 2))
        {
            wsprintf(s + n, "  (%c is blank!)", 'E' + k);
            col = RGB(255, 100, 100);
            break;
        }
    TextAt(dc, hFontSmall, HELP_X, y + 78, s, col);
}

void DrawEditor(HDC dc)
{
    Box(dc, 0, 0, VIEW_W, VIEW_H, cav->sky);
    DrawEditMap(dc, (int)camX);
    DrawEditHud(dc);
}
