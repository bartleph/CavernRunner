// ============================================================================
//  CAVERN RUNNER - a Manic Miner style scrolling platform game
//  main.cpp - the window, the game loop, the keyboard and the mouse
//
//  Build: open Cavern.dev in Dev-C++ and press F11.
//  (The project links with -static and -lwinmm.)
//
//  A gamepad works too: d-pad or stick to move, A to jump, Start, B = pause.
//
//  Game keys:   Left/Right = walk    Space or Up = jump    Up/Down = climb ropes
//               P = pause    S = sound    M = music    F11 = full screen
//               H = high scores (on the title)    Esc = back to title / quit
//               E = cavern editor    C = character & tile designer    A = sound designer
// ============================================================================

#include "cavern.h"

HWND hwndMain;

// The game is drawn into this off-screen picture, then scaled to the window
static HDC     backDC;
static HBITMAP backBmp, oldBackBmp;

// Where the picture sits in the window (it keeps its shape, with black bars)
static int viewX, viewY, viewW = GAME_W, viewH = GAME_H;

static BOOL            fullScreen;
static WINDOWPLACEMENT savedPlacement = { sizeof(WINDOWPLACEMENT) };

// ============================================================================
//  Showing the picture, scaled to fit
// ============================================================================

static void Blit(HWND hwnd, HDC dc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0)
        return;                         // minimised

    // The biggest size that fits, keeping the picture's shape
    if (cw * GAME_H > ch * GAME_W)
    {
        viewH = ch;
        viewW = ch * GAME_W / GAME_H;
    }
    else
    {
        viewW = cw;
        viewH = cw * GAME_H / GAME_W;
    }
    viewX = (cw - viewW) / 2;
    viewY = (ch - viewH) / 2;

    // Black bars either side (or above and below)
    PatBlt(dc, 0, 0, viewX, ch, BLACKNESS);
    PatBlt(dc, viewX + viewW, 0, cw - viewX - viewW, ch, BLACKNESS);
    PatBlt(dc, 0, 0, cw, viewY, BLACKNESS);
    PatBlt(dc, 0, viewY + viewH, cw, ch - viewY - viewH, BLACKNESS);

    if (viewW == GAME_W && viewH == GAME_H)
        BitBlt(dc, viewX, viewY, GAME_W, GAME_H, backDC, 0, 0, SRCCOPY);
    else
    {
        // Growing: keep the pixels crisp. Shrinking: blend, so text stays readable.
        SetStretchBltMode(dc, viewW > GAME_W ? COLORONCOLOR : HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, viewX, viewY, viewW, viewH, backDC, 0, 0, GAME_W, GAME_H, SRCCOPY);
    }
}

static void Present(HWND hwnd)
{
    Render(backDC);                     // draw the whole frame off-screen...
    HDC dc = GetDC(hwnd);
    Blit(hwnd, dc);                     // ...then show it at once
    ReleaseDC(hwnd, dc);
}

// Window coordinates (from a mouse message) to picture coordinates
static void ToGame(LPARAM lParam, int *x, int *y)
{
    int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
    *x = (viewW > 0) ? (mx - viewX) * GAME_W / viewW : mx;
    *y = (viewH > 0) ? (my - viewY) * GAME_H / viewH : my;
}

// F11: swap between a window and the whole screen
static void ToggleFullScreen(HWND hwnd)
{
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!fullScreen)
    {
        MONITORINFO mi = { sizeof mi };
        if (GetWindowPlacement(hwnd, &savedPlacement) &&
            GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi))
        {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            fullScreen = TRUE;
        }
    }
    else
    {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &savedPlacement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullScreen = FALSE;
    }
}

// ============================================================================
//  Keys for the game and the title screen
// ============================================================================

static void GameKey(HWND hwnd, WPARAM key)
{
    switch (key)
    {
    case VK_ESCAPE:
        StopSounds();
        if (testMode)
            EnterEditor(cavNum);        // back to the editor after a test
        else if (state == ST_TITLE)
            SendMessage(hwnd, WM_CLOSE, 0, 0);
        else
            EndGame();                  // abandon the game (high score still counts)
        break;

    case 'E':
        if (state == ST_TITLE)
        {
            camX = 0;
            editModified = FALSE;
            EnterEditor(0);
        }
        break;

    case 'C':
        if (state == ST_TITLE)
            EnterDesigner();
        break;

    case 'A':
        if (state == ST_TITLE)
            EnterAudio();
        break;

    case 'H':
        if (state == ST_TITLE)
        {
            titlePage  = !titlePage;
            stateTimer = 1;             // restart the time until it swaps by itself
        }
        break;

    case 'P':
        if (state == ST_PLAYING) paused = !paused;
        break;

    case 'S':
        soundOn = !soundOn;
        if (!soundOn) StopSounds();
        break;

    case 'M':
        musicOn = !musicOn;
        break;

    case VK_RETURN:
        if (state == ST_TITLE)
            NewGame();
        else if (state == ST_GAMEOVER || state == ST_VICTORY)
            EndGame();
        else if (state == ST_CLEARED)
            stateTimer = 1000;          // skip the wait
        break;
    }
}

// ============================================================================
//  The window
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int x, y;

    switch (msg)
    {
    case WM_CREATE:
    {
        hwndMain = hwnd;

        HDC dc = GetDC(hwnd);
        backDC     = CreateCompatibleDC(dc);
        backBmp    = CreateCompatibleBitmap(dc, GAME_W, GAME_H);
        oldBackBmp = (HBITMAP)SelectObject(backDC, backBmp);
        ReleaseDC(hwnd, dc);

        MakeFonts();
        LoadScores();
        LoadCaverns();
        LoadSprites();
        LoadSounds();
        ShowTitle();
        return 0;
    }

    case WM_ACTIVATE:                   // pause automatically when you switch away
        active = (LOWORD(wParam) != WA_INACTIVE);
        return 0;

    case WM_KEYDOWN:
        if (demoMode && wParam != VK_F11)   // any key stops the demo
        {
            StopDemo();
            return 0;
        }
        // Ignore auto-repeat while a key is held - except in the sound
        // designer, where holding + or - to change a number is handy
        if ((lParam & 0x40000000) && state != ST_SOUNDS)
            return 0;
        if (wParam == VK_F11)
            ToggleFullScreen(hwnd);
        else if (state == ST_EDIT)
            EditKey(wParam);
        else if (state == ST_SPRITES)
            DesignKey(wParam);
        else if (state == ST_SOUNDS)
            AudioKey(wParam);
        else
            GameKey(hwnd, wParam);
        return 0;

    // ---- The mouse, used by the editor and the designer
    case WM_MOUSEMOVE:
        ToGame(lParam, &x, &y);
        if (state == ST_EDIT)    EditMouseMove(x, y, wParam);
        if (state == ST_SPRITES) DesignMouseMove(x, y, wParam);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (demoMode)
        {
            StopDemo();
            return 0;
        }
        ToGame(lParam, &x, &y);
        if (state == ST_EDIT)    EditMouseDown(x, y, msg == WM_RBUTTONDOWN);
        if (state == ST_SPRITES) DesignMouseDown(x, y, msg == WM_RBUTTONDOWN);
        if (state == ST_SOUNDS && msg == WM_LBUTTONDOWN) AudioMouseDown(x, y);
        return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        ReleaseCapture();
        return 0;

    case WM_MOUSEWHEEL:
        if (state == ST_EDIT)
            EditWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_GETMINMAXINFO:              // don't let the window get too small to use
    {
        MINMAXINFO *mm = (MINMAXINFO *)lParam;
        mm->ptMinTrackSize.x = 500;
        mm->ptMinTrackSize.y = 420;
        return 0;
    }

    case WM_CLOSE:                      // the window's X button, or Esc on the title
        if (EditCanClose() && DesignCanClose() && AudioCanClose())
            DestroyWindow(hwnd);
        return 0;

    case WM_PAINT:                      // e.g. after being uncovered: show the last frame
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Blit(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:                 // we paint every pixel ourselves: no flicker
        return 1;

    case WM_DESTROY:
        SelectObject(backDC, oldBackBmp);
        DeleteObject(backBmp);
        DeleteDC(backDC);
        FreeFonts();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    srand(GetTickCount());              // so the boss turns up at different times
    InitSpriteTables();
    SoundInit();
    timeBeginPeriod(1);                 // 1 ms timer accuracy, so Sleep(1) is short

    // The icon comes from the resource file (cavern.rc)
    HICON icon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    if (icon == NULL)
        icon = LoadIcon(NULL, IDI_APPLICATION);

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = icon;
    wc.hIconSm       = icon;
    wc.hbrBackground = NULL;
    wc.lpszClassName = "CavernRunnerClass";
    if (!RegisterClassEx(&wc))
        return 0;

    // Start with the inside of the window the size of the picture, or smaller
    // if that won't fit on the screen. It can be resized, or made full screen.
    RECT work;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    DWORD style = WS_OVERLAPPEDWINDOW;
    int cw = GAME_W, ch = GAME_H;
    for (;;)
    {
        RECT r = { 0, 0, cw, ch };
        AdjustWindowRect(&r, style, FALSE);
        if ((r.bottom - r.top <= work.bottom - work.top && r.right - r.left <= work.right - work.left)
            || cw < 400)
        {
            cw = r.right - r.left;
            ch = r.bottom - r.top;
            break;
        }
        cw = cw * 9 / 10;
        ch = ch * 9 / 10;
    }

    HWND hwnd = CreateWindowEx(0, "CavernRunnerClass", "Cavern Runner", style,
                               CW_USEDEFAULT, CW_USEDEFAULT, cw, ch,
                               NULL, NULL, hInstance, NULL);
    if (hwnd == NULL)
        return 0;
    ShowWindow(hwnd, nCmdShow);

    // The game loop: handle any waiting messages, keep the sound card fed,
    // then run as many 1/60-second updates as real time says are due, and draw.
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    double behind = 0;
    const double STEP = 1.0 / 60.0;

    MSG msg = {0};
    BOOL running = TRUE;
    while (running)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                running = FALSE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running)
            break;

        SoundPump();

        QueryPerformanceCounter(&now);
        behind += (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        last = now;
        if (behind > 0.25) behind = 0.25;       // don't try to catch up after a long stall

        if (behind >= STEP)
        {
            while (behind >= STEP)
            {
                // The gamepad: held buttons are read by KeyDown; Start, Back
                // and B act like Enter, Esc and P (not in the editors)
                PadPoll();
                if (demoMode && PadAnyPressed())
                    StopDemo();
                int key = PadPressed();
                if (key && active && state != ST_EDIT && state != ST_SPRITES && state != ST_SOUNDS)
                    GameKey(hwnd, key);

                Update();
                behind -= STEP;
            }
            Present(hwnd);
        }
        else
            Sleep(1);                           // nothing due yet: give the CPU a rest
    }

    SoundShutdown();
    timeEndPeriod(1);
    return (int)msg.wParam;
}
