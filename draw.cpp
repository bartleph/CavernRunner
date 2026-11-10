// ============================================================================
//  draw.cpp - drawing the game
//
//  Everything is drawn into an off-screen picture GAME_W x GAME_H pixels in
//  size; main.cpp then copies that to the window, scaled to fit.
// ============================================================================

#include "cavern.h"

HFONT hFontSmall, hFontHud, hFontBig, hFontTitle;

static HFONT MakeFont(int height)
{
    return CreateFont(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                      FIXED_PITCH | FF_MODERN, "Consolas");
}

void MakeFonts(void)
{
    hFontSmall = MakeFont(15);
    hFontHud   = MakeFont(20);
    hFontBig   = MakeFont(48);
    hFontTitle = MakeFont(72);
}

void FreeFonts(void)
{
    DeleteObject(hFontSmall);
    DeleteObject(hFontHud);
    DeleteObject(hFontBig);
    DeleteObject(hFontTitle);
}

// ============================================================================
//  Basic shapes and text
// ============================================================================

// Filled rectangle in any colour, using the DC brush (nothing to create or free)
void Box(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

// Outline of a rectangle, 'thick' pixels wide
void Frame(HDC dc, int x, int y, int w, int h, int thick, COLORREF c)
{
    Box(dc, x, y, w, thick, c);
    Box(dc, x, y + h - thick, w, thick, c);
    Box(dc, x, y, thick, h, c);
    Box(dc, x + w - thick, y, thick, h, c);
}

void TextAt(HDC dc, HFONT font, int x, int y, const char *s, COLORREF col)
{
    HFONT old = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, col);
    TextOut(dc, x, y, s, lstrlen(s));
    SelectObject(dc, old);
}

// Centred across the picture, with a drop shadow
void CenterText(HDC dc, HFONT font, const char *s, int y, COLORREF col)
{
    HFONT old = (HFONT)SelectObject(dc, font);
    SIZE size;
    GetTextExtentPoint32(dc, s, lstrlen(s), &size);
    SetTextColor(dc, RGB(0, 0, 0));
    TextOut(dc, (GAME_W - size.cx) / 2 + 3, y + 3, s, lstrlen(s));
    SetTextColor(dc, col);
    TextOut(dc, (GAME_W - size.cx) / 2, y, s, lstrlen(s));
    SelectObject(dc, old);
}

// A dark box with a border, for messages over the play area
static void Panel(HDC dc, int y, int h)
{
    Box(dc, 120, y, GAME_W - 240, h, RGB(120, 120, 160));
    Box(dc, 124, y + 4, GAME_W - 248, h - 8, RGB(8, 8, 24));
}

// ============================================================================
//  Sprites and tiles
// ============================================================================

// Draw a sprite with its top-left corner at (x, y). Each sprite pixel becomes a
// scale x scale square; runs of the same colour are drawn as one rectangle.
void DrawSprite(HDC dc, int n, int x, int y, int scale, BOOL mirror)
{
    DrawSpriteEx(dc, n, x, y, scale, mirror, 0, 0);
}

// The same, with two extras for tiles:
//   shift    slides the picture sideways, wrapping round (moving conveyors)
//   dissolve 0-16: how many pixels in each 16 to leave out (crumbling floors)
void DrawSpriteEx(HDC dc, int n, int x, int y, int scale, BOOL mirror, int shift, int dissolve)
{
    int size = SpriteSize(n);
    shift = ((shift % size) + size) % size;

    for (int row = 0; row < size; row++)
    {
        const signed char *p = sprites[n].px[row];
        int col = 0;
        while (col < size)
        {
            int start = col, colour = -2;
            while (col < size)
            {
                int sx = mirror ? size - 1 - col : col;
                int c  = p[(sx - shift + size) % size];
                if (dissolve > 0 && (sx * 7 + row * 13) % 16 < dissolve)
                    c = -1;                         // a gap where the floor has crumbled
                if (colour == -2)
                    colour = c;
                else if (c != colour)
                    break;
                col++;
            }
            if (colour >= 0)
                Box(dc, x + start * scale, y + row * scale, (col - start) * scale, scale, palette[colour]);
        }
    }
}

int TileSprite(int set, int t)
{
    return SPR_TILES + set * TILES_PER_SET + t;
}

void DrawTile(HDC dc, char c, int tx, int ty, int sx, int sy)
{
    int set = cav->tileset;
    switch (c)
    {
    case '#': DrawSprite(dc, TileSprite(set, T_WALL),     sx, sy, SPR_SCALE, FALSE); break;
    case '=': DrawSprite(dc, TileSprite(set, T_PLATFORM), sx, sy, SPR_SCALE, FALSE); break;
    case '^': DrawSprite(dc, TileSprite(set, T_SPIKES),   sx, sy, SPR_SCALE, FALSE); break;
    case '*': DrawSprite(dc, TileSprite(set, T_PLANT),    sx, sy, SPR_SCALE, FALSE); break;
    case 'D': DrawSprite(dc, TileSprite(set, T_DOOR),     sx, sy, SPR_SCALE, FALSE); break;
    case '|': DrawSprite(dc, TileSprite(set, T_ROPE),     sx, sy, SPR_SCALE, FALSE); break;
    case '~': DrawSprite(dc, TileSprite(set, T_MOVER),    sx, sy, SPR_SCALE, FALSE); break;
    case 's':
        DrawSprite(dc, TileSprite(set, switchOn ? T_SWITCH_ON : T_SWITCH_OFF), sx, sy, SPR_SCALE, FALSE);
        break;

    case '-':                                   // crumbling: gaps appear as it wears
        DrawSpriteEx(dc, TileSprite(set, T_CRUMBLE), sx, sy, SPR_SCALE, FALSE, 0,
                     crumble[ty][tx] * 15 / CRUMBLE_TIME);
        break;

    case '<':                                   // conveyors: the picture slides along
    case '>':
        DrawSpriteEx(dc, TileSprite(set, T_CONVEYOR), sx, sy, SPR_SCALE, FALSE,
                     (frameCount / 3) * (c == '>' ? 1 : -1), 0);
        break;

    case 'k':                                   // keys bob gently
    {
        int bob = ((frameCount / 10 + tx) % 2) * 2;
        DrawSprite(dc, SPR_KEY, sx, sy + bob - 1, SPR_SCALE, FALSE);
        break;
    }
    }
}

void DrawExit(HDC dc, int sx, int sy, BOOL open)
{
    Box(dc, sx - 3, sy - 3, TILE + 6, 2 * TILE + 3, RGB(120, 120, 140));     // frame
    if (open)
    {
        // Open: a shimmering portal
        COLORREF c = ((frameCount / 6) % 2) ? RGB(255, 255, 255) : RGB(80, 200, 255);
        Box(dc, sx, sy, TILE, 2 * TILE, c);
        Box(dc, sx + 6, sy + 6, TILE - 12, 2 * TILE - 12, RGB(20, 40, 120));
    }
    else
    {
        // Closed: a barred gate
        Box(dc, sx, sy, TILE, 2 * TILE, RGB(30, 30, 50));
        for (int i = 0; i < 4; i++)
            Box(dc, sx + 3 + i * 8, sy, 3, 2 * TILE, RGB(150, 150, 170));
    }
}

// Monsters and the player are given by their collision box; the 32 x 32
// sprite is centred on it, with its bottom edge on the box's bottom edge.
void DrawEnemy(HDC dc, int sx, int sy, float vx, int design)
{
    int frame = (frameCount / 10) % 2;          // two frames, swapped 6 times a second
    DrawSprite(dc, SPR_MONSTER + design * 2 + frame, sx + ENEMY_W / 2 - SPR_SIZE / 2,
               sy + ENEMY_H - SPR_SIZE, SPR_SCALE, vx < 0);
}

void DrawPlayer(HDC dc, int sx, int sy)
{
    int frame = 0;                              // standing still: frame 1
    if (state == ST_PLAYING || state == ST_DYING)
    {
        if (climbing)
            frame = (walkAnim / 6) % 2;         // climbing: swap frames as you move
        else if (!onGround)
            frame = 1;                          // in the air: frame 2
        else if (airDir != 0)
            frame = (walkAnim / 6) % 2;         // walking: swap frames
    }
    DrawSprite(dc, SPR_PLAYER1 + frame, sx + PLAYER_W / 2 - SPR_SIZE / 2,
               sy + PLAYER_H - SPR_SIZE, SPR_SCALE, facing < 0);
}

// ============================================================================
//  The status strip and messages
// ============================================================================

static void DrawHud(HDC dc)
{
    int y = VIEW_H;
    char s[120];

    Box(dc, 0, y, GAME_W, HUD_H, RGB(0, 0, 0));
    Box(dc, 0, y, GAME_W, 2, RGB(90, 90, 120));

    // Top line: the cavern's name
    if (state == ST_TITLE)
        lstrcpy(s, "CAVERN RUNNER");
    else if (demoMode)
        wsprintf(s, "DEMO: %s   -   press any key to play", cav->name);
    else if (testMode)
        wsprintf(s, "Testing: %s   (Esc returns to the editor)", cav->name);
    else
        wsprintf(s, "Cavern %d of %d:  %s", cavNum + 1, numCaverns, cav->name);
    CenterText(dc, hFontHud, s, y + 14, RGB(255, 230, 80));

    HFONT old = (HFONT)SelectObject(dc, hFontHud);
    SetTextColor(dc, RGB(255, 255, 255));
    int ty = y + 54;

    // Air bar: turns red when it's getting low
    TextOut(dc, 16, ty, "AIR", 3);
    int barW = 240;
    Box(dc, 60, ty + 2, barW, 18, RGB(50, 50, 60));
    COLORREF airCol = (air < AIR_MAX / 5) ? RGB(255, 60, 60) : RGB(80, 220, 255);
    Box(dc, 60, ty + 2, barW * air / AIR_MAX, 18, airCol);

    wsprintf(s, "KEYS %d/%d", keysTotal - keysLeft, keysTotal);
    TextOut(dc, 320, ty, s, lstrlen(s));

    wsprintf(s, "SCORE %06d", score);
    TextOut(dc, 460, ty, s, lstrlen(s));

    wsprintf(s, "HI %06d", HiScore() > score ? HiScore() : score);
    TextOut(dc, 640, ty, s, lstrlen(s));

    TextOut(dc, 800, ty, "LIVES", 5);
    if (testMode)
        TextOut(dc, 866, ty, "inf", 3);
    else if (lives <= 5)
        for (int i = 0; i < lives; i++)
            DrawSprite(dc, SPR_PLAYER1, 866 + i * 18, ty + 2, 1, FALSE);   // little men
    else
    {
        DrawSprite(dc, SPR_PLAYER1, 866, ty + 2, 1, FALSE);
        wsprintf(s, "x%d", lives);
        TextOut(dc, 886, ty, s, lstrlen(s));
    }

    SelectObject(dc, old);
}

static void DrawTitle(HDC dc)
{
    char s[100];
    BOOL blink = (frameCount / 30) % 2 == 0;
    COLORREF info = RGB(150, 220, 255);

    Panel(dc, 70, 470);

    if (titlePage == 0)
    {
        CenterText(dc, hFontTitle, "CAVERN RUNNER", 100, RGB(255, 210, 60));
        CenterText(dc, hFontHud, "Collect every flashing key, then reach the exit", 190, RGB(255, 255, 255));
        CenterText(dc, hFontHud, "before your air runs out.", 215, RGB(255, 255, 255));
        CenterText(dc, hFontHud, "Arrows: walk   Space / Up: jump   Up / Down: climb", 262, info);
        CenterText(dc, hFontHud, "P: pause   S: sound   M: music   F11: full screen", 289, info);
        CenterText(dc, hFontHud, "E: caverns   C: characters & tiles   A: sounds", 316, info);
        CenterText(dc, hFontHud, "H: high scores   Esc: quit", 343, info);
        CenterText(dc, hFontSmall, "Gamepad: stick or d-pad to move, A to jump, Start to start, B to pause",
                   372, RGB(120, 170, 200));

        int bad = CountProblems();
        if (bad > 0)
        {
            wsprintf(s, "%d cavern%s can't be played yet - press E to fix",
                     bad, bad == 1 ? "" : "s");
            CenterText(dc, hFontHud, s, 405, RGB(255, 110, 110));
        }
    }
    else
    {
        CenterText(dc, hFontBig, "HIGH SCORES", 95, RGB(255, 210, 60));
        for (int i = 0; i < NUM_SCORES; i++)
        {
            // Every line the same length, so the columns line up when centred
            if (scores[i].score > 0)
                wsprintf(s, "%2d.  %06d  %-15s", i + 1, scores[i].score, scores[i].name);
            else
                wsprintf(s, "%2d.  ------  %-15s", i + 1, "");
            CenterText(dc, hFontHud, s, 165 + i * 26,
                       i == 0 ? RGB(255, 230, 80) : RGB(255, 255, 255));
        }
    }

    if (blink)
        CenterText(dc, hFontHud, "Press Enter to start", 470, RGB(255, 255, 255));
}

static void DrawMessages(HDC dc)
{
    char s[100];
    BOOL blink = (frameCount / 30) % 2 == 0;

    switch (state)
    {
    case ST_TITLE:
        DrawTitle(dc);
        break;

    case ST_DYING:
        CenterText(dc, hFontBig, "OUCH!", 240, RGB(255, 80, 80));
        break;

    case ST_GAMEOVER:
        Panel(dc, 180, 200);
        CenterText(dc, hFontBig, "GAME OVER", 210, RGB(255, 80, 80));
        wsprintf(s, "Final score %d", score);
        CenterText(dc, hFontHud, s, 285, RGB(255, 255, 255));
        if (blink)
            CenterText(dc, hFontHud, "Press Enter", 325, RGB(255, 255, 255));
        break;

    case ST_TALLY:
    case ST_CLEARED:
    {
        int next = NextPlayable(cavNum + 1);
        Panel(dc, 180, 200);
        CenterText(dc, hFontBig, "CAVERN COMPLETE!", 210, RGB(120, 255, 120));
        CenterText(dc, hFontHud, state == ST_TALLY ? "Counting your air bonus..." : "Well done!",
                   285, RGB(255, 255, 255));
        if (state == ST_CLEARED && !testMode && next >= 0)
        {
            wsprintf(s, "Next: %s", caverns[next].name);
            CenterText(dc, hFontHud, s, 325, RGB(255, 230, 80));
        }
        break;
    }

    case ST_VICTORY:
        Panel(dc, 150, 260);
        CenterText(dc, hFontBig, "YOU ESCAPED!", 180, RGB(255, 210, 60));
        CenterText(dc, hFontHud, "Every cavern cleared.", 255, RGB(255, 255, 255));
        wsprintf(s, "Final score %d", score);
        CenterText(dc, hFontHud, s, 290, RGB(255, 255, 255));
        if (blink)
            CenterText(dc, hFontHud, "Press Enter", 350, RGB(255, 255, 255));
        break;
    }

    if (demoMode && (frameCount / 40) % 2 == 0)
        CenterText(dc, hFontBig, "DEMO", 40, RGB(255, 230, 80));

    if (state == ST_PLAYING)
    {
        if (bossState == BOSS_WARNING && (frameCount / 8) % 2 == 0)
            CenterText(dc, hFontBig, "THE GUARDIAN COMES!", 180, RGB(255, 80, 80));
        else if (messageTimer > 0)
            CenterText(dc, hFontBig, message, 180, messageColour);
        if (paused)
            CenterText(dc, hFontBig, "PAUSED", 260, RGB(255, 255, 255));
    }

    if (!active)
        CenterText(dc, hFontHud, "Click here to continue", 560, RGB(255, 255, 255));

    char status[40] = "";
    if (!soundOn) lstrcat(status, "(sound off) ");
    if (!musicOn) lstrcat(status, "(music off)");
    if (status[0])
        CenterText(dc, hFontSmall, status, 8, RGB(160, 160, 160));
}

// ============================================================================
//  The whole picture
// ============================================================================

void Render(HDC dc)
{
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(DC_PEN));
    SetBkMode(dc, TRANSPARENT);

    if (state == ST_SPRITES)
    {
        DrawDesigner(dc);
        return;
    }
    if (state == ST_SOUNDS)
    {
        DrawAudio(dc);
        return;
    }
    if (state == ST_EDIT)
    {
        DrawEditor(dc);
        return;
    }

    int cam = (int)camX;

    // Background, with faint stripes that move at half speed (parallax)
    Box(dc, 0, 0, VIEW_W, VIEW_H, cav->sky);
    for (int x = -(cam / 2) % 64; x < VIEW_W; x += 64)
        Box(dc, x, 0, 1, VIEW_H, cav->skyLines);

    // Only the tile columns that are on screen
    int firstCol = cam / TILE;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = firstCol; tx <= firstCol + VIEW_W / TILE && tx < MAP_W; tx++)
            if (map[ty][tx] != ' ')
                DrawTile(dc, map[ty][tx], tx, ty, tx * TILE - cam, ty * TILE);

    // Moving platforms, one tile picture per 32 pixels of width
    for (int i = 0; i < numMovers; i++)
        for (int x = 0; x < movers[i].w; x += TILE)
            DrawSprite(dc, TileSprite(cav->tileset, T_MOVER), (int)movers[i].x + x - cam,
                       movers[i].y, SPR_SCALE, FALSE);

    DrawExit(dc, exitX - cam, exitY, keysLeft == 0);

    for (int i = 0; i < numEnemies; i++)
        DrawEnemy(dc, (int)enemies[i].x - cam, (int)enemies[i].y, enemies[i].vx,
                  cav->monster[enemies[i].kind]);

    // The boss: flickering while it warns you, then flapping between its frames
    if (bossState >= BOSS_WARNING && (bossState != BOSS_WARNING || (frameCount / 4) % 2 == 0))
    {
        int frame = (bossState == BOSS_HUNTING) ? (frameCount / 12) % 2 : 0;
        DrawSprite(dc, SPR_BOSS1 + frame, (int)bossX - cam, (int)bossY, SPR_SCALE, bossVX < 0);
    }

    // No player on the title screen; flickering while dying
    if (state != ST_TITLE && (state != ST_DYING || (stateTimer / 4) % 2 == 0))
        DrawPlayer(dc, px - cam, (int)py);

    DrawHud(dc);
    DrawMessages(dc);
}
