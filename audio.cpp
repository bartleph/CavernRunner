// ============================================================================
//  audio.cpp - the sound designer (press A on the title screen)
//
//  On the left, a list: the eleven sound effects, then the three tunes.
//  An effect is a list of notes, each sliding from a start pitch to an end
//  pitch over a length of time; you edit them in a table, with a graph of
//  the pitch underneath. The music is shown as a piano roll: each note a
//  bar, higher notes higher up, longer notes longer. Each cavern chooses
//  which tune it plays (U in the cavern editor).
//
//  It edits effectDefs[] and T->notes[] directly; sounds.txt is only written
//  when you save.
// ============================================================================

#include "cavern.h"
#include <stdio.h>

#define LIST_X      40
#define LIST_Y      70
#define LIST_STEP   30
#define PANEL_X     300
#define PANEL_W     620
#define TABLE_Y     70
#define ROW_H       24
#define GRAPH_Y     500
#define GRAPH_H     140
#define ROLL_Y      70
#define ROLL_LOW    45                  // lowest note shown in the piano roll (A2)
#define ROLL_HIGH   84                  // highest (C6)
#define ROLL_ROW    9                   // pixels per semitone
#define ROLL_EIGHTH 10                  // pixels per eighth

#define LIST_N      (NUM_SOUNDS + NUM_TUNES)    // the effects, then the tunes
#define IS_TUNE(i)  ((i) >= NUM_SOUNDS)

BOOL audioModified;

static int  item;                       // 0..NUM_SOUNDS-1 for an effect, then the tunes
static int  row, column;                // in the effect table (column 0 = start, 1 = end, 2 = ms)
static int  tuneSel;                    // selected note in the tune
static int  rollStart;                  // first eighth shown in the piano roll

static const char *noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void NoteName(int n, char *s)
{
    wsprintf(s, "%s%d", noteNames[n % 12], n / 12 - 1);
}

void EnterAudio(void)
{
    audioModified = FALSE;
    SetState(ST_SOUNDS);
}

// Offer to save. Returns FALSE if the user cancels.
static BOOL AskToSave(void)
{
    StopPreview();
    if (!audioModified)
        return TRUE;
    int r = MessageBox(hwndMain, "Save your sounds to " SOUND_FILE "?", "Sound Designer",
                       MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL)
        return FALSE;
    if (r == IDYES && !SaveSounds())
        return FALSE;
    if (r == IDNO)
        LoadSounds();                   // throw the changes away
    audioModified = FALSE;
    return TRUE;
}

BOOL AudioCanClose(void)
{
    return state != ST_SOUNDS || AskToSave();
}

// Play an effect even when sound effects are switched off in the game
static void Audition(int n)
{
    BOOL was = soundOn;
    soundOn = TRUE;
    Play(n);
    soundOn = was;
}

static void Changed(void)
{
    audioModified = TRUE;
    if (!IS_TUNE(item))
    {
        RebuildEffect(item);
        Audition(item);                 // hear each change straight away
    }
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Bring the selected note of the tune into view in the piano roll
static void ScrollToSelected(void)
{
    Tune *T = &tunes[item - NUM_SOUNDS];
    int at = 0;
    for (int i = 0; i < tuneSel; i++)
        at += T->notes[i].len;
    int visible = PANEL_W / ROLL_EIGHTH;
    if (at < rollStart)
        rollStart = at;
    if (at + T->notes[tuneSel].len > rollStart + visible)
        rollStart = at + T->notes[tuneSel].len - visible;
    if (rollStart < 0)
        rollStart = 0;
}

static void EffectKey(WPARAM key, BOOL shift)
{
    EffectDef *d = &effectDefs[item];
    SoundNote *n = &d->notes[row];
    int up = 0;

    switch (key)
    {
    case VK_UP:    if (row > 0) row--;              break;
    case VK_DOWN:  if (row + 1 < d->count) row++;   break;
    case VK_LEFT:  if (column > 0) column--;        break;
    case VK_RIGHT: if (column < 2) column++;        break;
    case VK_SPACE: Audition(item);                  break;

    case VK_PRIOR: case VK_ADD: case VK_OEM_PLUS:       up = 1;  break;
    case VK_NEXT:  case VK_SUBTRACT: case VK_OEM_MINUS: up = -1; break;

    case VK_INSERT:                     // a copy of this note, just after it
        if (d->count < MAX_FX_NOTES)
        {
            memmove(&d->notes[row + 1], &d->notes[row], (d->count - row) * sizeof(SoundNote));
            d->count++;
            row++;
            Changed();
        }
        break;

    case VK_DELETE:
        if (d->count > 1)
        {
            memmove(&d->notes[row], &d->notes[row + 1], (d->count - row - 1) * sizeof(SoundNote));
            d->count--;
            if (row >= d->count) row = d->count - 1;
            Changed();
        }
        break;
    }

    if (up != 0)
    {
        if (column == 0) n->startHz = Clamp(n->startHz + up * (shift ? 100 : 10), 0, 5000);
        if (column == 1) n->endHz   = Clamp(n->endHz   + up * (shift ? 100 : 10), 0, 5000);
        if (column == 2) n->ms      = Clamp(n->ms      + up * (shift ? 50 : 10), 10, 2000);
        Changed();
    }
}

static void TuneKey(WPARAM key, BOOL shift)
{
    Tune *T = &tunes[item - NUM_SOUNDS];
    TuneNote *t = &T->notes[tuneSel];

    switch (key)
    {
    case VK_LEFT:  if (tuneSel > 0) tuneSel--;              break;
    case VK_RIGHT: if (tuneSel + 1 < T->len) tuneSel++;    break;
    case VK_HOME:  tuneSel = 0;                             break;
    case VK_END:   tuneSel = T->len - 1;                   break;

    case VK_UP:    t->note = (unsigned char)Clamp(t->note + 1,  ROLL_LOW, ROLL_HIGH); Changed(); break;
    case VK_DOWN:  t->note = (unsigned char)Clamp(t->note - 1,  ROLL_LOW, ROLL_HIGH); Changed(); break;
    case VK_PRIOR: t->note = (unsigned char)Clamp(t->note + 12, ROLL_LOW, ROLL_HIGH); Changed(); break;
    case VK_NEXT:  t->note = (unsigned char)Clamp(t->note - 12, ROLL_LOW, ROLL_HIGH); Changed(); break;

    case VK_ADD: case VK_OEM_PLUS:
        t->len = (unsigned char)Clamp(t->len + 1, 1, 16);
        Changed();
        break;
    case VK_SUBTRACT: case VK_OEM_MINUS:
        t->len = (unsigned char)Clamp(t->len - 1, 1, 16);
        Changed();
        break;

    case VK_OEM_4:                      // [ : slower
        T->tempo = Clamp(T->tempo + (shift ? 50 : 10), 40, 1000);
        Changed();
        break;
    case VK_OEM_6:                      // ] : faster
        T->tempo = Clamp(T->tempo - (shift ? 50 : 10), 40, 1000);
        Changed();
        break;

    case VK_SPACE:
        if (previewing)
            StopPreview();
        else
            PreviewTune(item - NUM_SOUNDS, tuneSel);
        break;

    case VK_INSERT:
        if (T->len < MAX_TUNE)
        {
            memmove(&T->notes[tuneSel + 1], &T->notes[tuneSel], (T->len - tuneSel) * sizeof(TuneNote));
            T->len++;
            tuneSel++;
            Changed();
        }
        break;

    case VK_DELETE:
        if (T->len > 1)
        {
            memmove(&T->notes[tuneSel], &T->notes[tuneSel + 1], (T->len - tuneSel - 1) * sizeof(TuneNote));
            T->len--;
            if (tuneSel >= T->len) tuneSel = T->len - 1;
            Changed();
        }
        break;
    }
    ScrollToSelected();
}

static void SelectItem(int i)
{
    StopPreview();
    item = (i + LIST_N) % LIST_N;
    row = column = 0;
    if (IS_TUNE(item))
    {
        tuneSel = 0;
        rollStart = 0;
    }
}

void AudioKey(WPARAM key)
{
    BOOL ctrl  = GetKeyState(VK_CONTROL) < 0;
    BOOL shift = GetKeyState(VK_SHIFT) < 0;

    if (ctrl)
    {
        if (key == 'S' && SaveSounds())
        {
            audioModified = FALSE;
            savedTimer = 120;
        }
        if (key == 'D')                 // back to the standard sound for this one
        {
            StandardSound(item);
            row = 0;
            tuneSel = 0;
            rollStart = 0;
            Changed();
        }
        return;
    }

    if (key == VK_ESCAPE)
    {
        if (AskToSave())
            ShowTitle();
        return;
    }
    if (key == VK_TAB)
    {
        SelectItem(item + (shift ? -1 : 1));
        return;
    }

    if (IS_TUNE(item))
        TuneKey(key, shift);
    else
        EffectKey(key, shift);
}

void AudioMouseDown(int x, int y)
{
    // The list
    for (int i = 0; i < LIST_N; i++)
        if (x >= LIST_X && x < LIST_X + 220 &&
            y >= LIST_Y + i * LIST_STEP + (IS_TUNE(i) ? 12 : 0) &&
            y <  LIST_Y + (i + 1) * LIST_STEP - 4 + (IS_TUNE(i) ? 12 : 0))
        {
            SelectItem(i);
            if (!IS_TUNE(i))
                Audition(i);
            return;
        }

    if (!IS_TUNE(item))
    {
        // A cell in the table
        int r = (y - TABLE_Y - ROW_H) / ROW_H;
        if (y >= TABLE_Y + ROW_H && r < effectDefs[item].count && x >= PANEL_X + 60)
        {
            row = r;
            column = Clamp((x - PANEL_X - 60) / 140, 0, 2);
        }
        return;
    }

    // A note in the piano roll
    Tune *T = &tunes[item - NUM_SOUNDS];
    int at = 0;
    for (int i = 0; i < T->len; i++)
    {
        int nx = PANEL_X + (at - rollStart) * ROLL_EIGHTH;
        if (x >= nx && x < nx + T->notes[i].len * ROLL_EIGHTH && y >= ROLL_Y && y < ROLL_Y + 380)
        {
            tuneSel = i;
            return;
        }
        at += T->notes[i].len;
    }
}

// ============================================================================
//  Drawing
// ============================================================================

// Pitch to a height in the graph: a log scale from 50 Hz to 4000 Hz
static int GraphY(int hz)
{
    if (hz <= 0)
        return GRAPH_Y + GRAPH_H - 2;
    double f = log((double)hz / 50.0) / log(4000.0 / 50.0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return GRAPH_Y + GRAPH_H - 4 - (int)(f * (GRAPH_H - 8));
}

static void DrawEffect(HDC dc)
{
    char s[100];
    const EffectDef *d = &effectDefs[item];
    COLORREF label = RGB(170, 200, 230);
    static const int colX[3] = { PANEL_X + 60, PANEL_X + 200, PANEL_X + 340 };

    TextAt(dc, hFontHud, PANEL_X, TABLE_Y, "Note", label);
    TextAt(dc, hFontHud, colX[0], TABLE_Y, "Start Hz", label);
    TextAt(dc, hFontHud, colX[1], TABLE_Y, "End Hz", label);
    TextAt(dc, hFontHud, colX[2], TABLE_Y, "Length ms", label);

    int total = 0;
    for (int i = 0; i < d->count; i++)
    {
        int y = TABLE_Y + (i + 1) * ROW_H;
        const SoundNote *n = &d->notes[i];
        int v[3] = { n->startHz, n->endHz, n->ms };

        if (i == row)
        {
            Box(dc, PANEL_X - 6, y - 2, 500, ROW_H - 2, RGB(40, 40, 70));
            Box(dc, colX[column] - 6, y - 2, 120, ROW_H - 2, RGB(255, 230, 80));
        }
        wsprintf(s, "%2d", i + 1);
        TextAt(dc, hFontHud, PANEL_X, y, s, RGB(160, 160, 160));
        for (int c = 0; c < 3; c++)
        {
            if (c < 2 && v[c] == 0)
                lstrcpy(s, "silent");
            else
                wsprintf(s, "%d", v[c]);
            TextAt(dc, hFontHud, colX[c], y, s, (i == row && c == column) ? RGB(0, 0, 0) : RGB(255, 255, 255));
        }
        total += n->ms;
    }

    wsprintf(s, "%d note%s, %d.%02d seconds in all", d->count, d->count == 1 ? "" : "s",
             total / 1000, total % 1000 / 10);
    TextAt(dc, hFontSmall, PANEL_X, TABLE_Y + (MAX_FX_NOTES + 1) * ROW_H + 4, s, label);

    // The graph: pitch (up) against time (across), each note a sloping line
    Box(dc, PANEL_X, GRAPH_Y, PANEL_W, GRAPH_H, RGB(8, 8, 24));
    Frame(dc, PANEL_X, GRAPH_Y, PANEL_W, GRAPH_H, 1, RGB(90, 90, 120));
    TextAt(dc, hFontSmall, PANEL_X + 6, GRAPH_Y + 4, "pitch", RGB(100, 100, 130));
    if (total == 0)
        return;

    int t = 0;
    for (int i = 0; i < d->count; i++)
    {
        const SoundNote *n = &d->notes[i];
        int x0 = PANEL_X + 4 + (PANEL_W - 8) * t / total;
        int x1 = PANEL_X + 4 + (PANEL_W - 8) * (t + n->ms) / total;
        if (i == row)
            Box(dc, x0, GRAPH_Y + 1, x1 - x0, GRAPH_H - 2, RGB(30, 30, 60));
        if (n->startHz > 0 || n->endHz > 0)
        {
            HPEN pen = CreatePen(PS_SOLID, 3, i == row ? RGB(255, 230, 80) : RGB(80, 220, 255));
            HPEN old = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, x0, GraphY(n->startHz), NULL);
            LineTo(dc, x1, GraphY(n->endHz));
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        Box(dc, x1, GRAPH_Y + 1, 1, GRAPH_H - 2, RGB(50, 50, 80));
        t += n->ms;
    }
}

static void DrawTune(HDC dc)
{
    Tune *T = &tunes[item - NUM_SOUNDS];
    char s[120], name[8];
    COLORREF label = RGB(170, 200, 230);
    int rollH = (ROLL_HIGH - ROLL_LOW + 1) * ROLL_ROW;
    int bottom = ROLL_Y + rollH;

    // The roll: a faint stripe for each C, and a line every bar of 8 eighths
    Box(dc, PANEL_X, ROLL_Y, PANEL_W, rollH, RGB(8, 8, 24));
    for (int n = ROLL_LOW; n <= ROLL_HIGH; n++)
        if (n % 12 == 0)
        {
            int y = bottom - (n - ROLL_LOW + 1) * ROLL_ROW;
            Box(dc, PANEL_X, y, PANEL_W, ROLL_ROW, RGB(24, 24, 48));
            NoteName(n, name);
            TextAt(dc, hFontSmall, PANEL_X - 34, y - 4, name, label);
        }
    for (int e = 0; e < PANEL_W / ROLL_EIGHTH; e++)
        if ((rollStart + e) % 8 == 0)
            Box(dc, PANEL_X + e * ROLL_EIGHTH, ROLL_Y, 1, rollH, RGB(50, 50, 80));

    // The notes
    int at = 0;
    for (int i = 0; i < T->len; i++)
    {
        int x = PANEL_X + (at - rollStart) * ROLL_EIGHTH;
        int w = T->notes[i].len * ROLL_EIGHTH - 1;
        int y = bottom - (T->notes[i].note - ROLL_LOW + 1) * ROLL_ROW;
        at += T->notes[i].len;
        if (x + w <= PANEL_X || x >= PANEL_X + PANEL_W)
            continue;                   // off the edge of the roll
        COLORREF c = (previewing && previewTune == item - NUM_SOUNDS && i == previewNote) ? RGB(255, 255, 255)
                   : (i == tuneSel) ? RGB(255, 230, 80) : RGB(80, 220, 255);
        int x0 = x < PANEL_X ? PANEL_X : x;
        int x1 = (x + w > PANEL_X + PANEL_W) ? PANEL_X + PANEL_W : x + w;
        Box(dc, x0, y, x1 - x0, ROLL_ROW - 1, c);
    }
    Frame(dc, PANEL_X - 1, ROLL_Y - 1, PANEL_W + 2, rollH + 2, 1, RGB(90, 90, 120));

    // About the selected note
    NoteName(T->notes[tuneSel].note, name);
    wsprintf(s, "Note %d of %d:  %s  for %d step%s", tuneSel + 1, T->len, name,
             T->notes[tuneSel].len, T->notes[tuneSel].len == 1 ? "" : "s");
    TextAt(dc, hFontHud, PANEL_X, bottom + 14, s, RGB(255, 255, 255));
    wsprintf(s, "Speed: a step lasts %d ms   (%s)", T->tempo,
             previewing ? "playing - Space to stop" : "Space to play from here");
    TextAt(dc, hFontSmall, PANEL_X, bottom + 42, s, label);
    TextAt(dc, hFontSmall, PANEL_X, bottom + 62, "In the game the tune speeds up as your air runs out.", label);

    // Which caverns play it (U in the cavern editor chooses)
    char list[100] = "";
    int n = 0, count = 0;
    for (int i = 0; i < numCaverns && n < 80; i++)
        if (caverns[i].music == item - NUM_SOUNDS)
            n += wsprintf(list + n, "%s%d", count++ ? ", " : "", i + 1);
    if (count)
        wsprintf(s, "Played in cavern%s %s", count == 1 ? "" : "s", list);
    else
        lstrcpy(s, "Not played in any cavern yet: choose it with U in the cavern editor");
    TextAt(dc, hFontSmall, PANEL_X, bottom + 82, s, RGB(255, 255, 255));
}

void DrawAudio(HDC dc)
{
    char s[100];
    COLORREF label = RGB(170, 200, 230);

    Box(dc, 0, 0, GAME_W, GAME_H, RGB(16, 16, 28));

    wsprintf(s, "SOUND DESIGNER   %s%s", IS_TUNE(item) ? tuneNames[item - NUM_SOUNDS] : soundNames[item],
             audioModified ? "  *" : "");
    TextAt(dc, hFontHud, LIST_X, 22, s, RGB(255, 230, 80));

    // The list
    for (int i = 0; i < LIST_N; i++)
    {
        int y = LIST_Y + i * LIST_STEP + (IS_TUNE(i) ? 12 : 0);     // a gap before the tunes
        BOOL on = (i == item);
        Box(dc, LIST_X, y, 220, LIST_STEP - 4, on ? RGB(255, 230, 80) : IS_TUNE(i) ? RGB(30, 50, 70) : RGB(40, 40, 64));
        TextAt(dc, hFontHud, LIST_X + 10, y + 2, IS_TUNE(i) ? tuneNames[i - NUM_SOUNDS] : soundNames[i],
               on ? RGB(0, 0, 0) : RGB(255, 255, 255));
    }
    if (savedTimer > 0)
        TextAt(dc, hFontHud, LIST_X, LIST_Y + LIST_N * LIST_STEP + 22, "Saved to " SOUND_FILE,
               RGB(120, 255, 120));

    if (IS_TUNE(item))
        DrawTune(dc);
    else
        DrawEffect(dc);

    // Help
    static const char *effectHelp[] =
    {
        "Up/Down: note   Left/Right: column   + / - or PgUp/PgDn: change it (Shift: bigger steps)",
        "Space: play   Ins: add a note   Del: remove a note   Ctrl+D: back to the standard sound",
        "Tab: next sound   Ctrl+S: save   Esc: back to the title",
    };
    static const char *tuneHelp[] =
    {
        "Left/Right: note   Up/Down: pitch   PgUp/PgDn: an octave   + / -: length   Home/End",
        "Space: play / stop   Ins: add a note   Del: remove   [ / ]: slower / faster",
        "Ctrl+D: back to the standard tune   Tab: next   Ctrl+S: save   Esc: back to the title",
    };
    for (int i = 0; i < 3; i++)
        TextAt(dc, hFontSmall, LIST_X, 662 + i * 20, IS_TUNE(item) ? tuneHelp[i] : effectHelp[i], label);
}
