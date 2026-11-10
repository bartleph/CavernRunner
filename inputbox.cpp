// ============================================================================
//  inputbox.cpp - a small window asking for one line of text
//
//  Win32 has no ready-made "type a value" box, so this builds one: a popup
//  window with a label, an edit box and OK / Cancel. It is made modal by
//  disabling its owner and running its own message loop until it closes.
// ============================================================================

#include "cavern.h"

static BOOL  ibDone, ibOK;
static HWND  ibEdit;
static char *ibBuf;
static int   ibSize;

static LRESULT CALLBACK InputBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            GetWindowText(ibEdit, ibBuf, ibSize);
            ibOK   = TRUE;
            ibDone = TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
            ibDone = TRUE;
        return 0;

    case WM_CLOSE:
        ibDone = TRUE;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// buf holds the starting text and receives the answer. Returns TRUE for OK.
BOOL InputBox(HWND owner, const char *title, const char *prompt, char *buf, int size)
{
    static BOOL registered = FALSE;
    HINSTANCE hInst = GetModuleHandle(NULL);

    if (!registered)
    {
        WNDCLASSEX wc = {0};
        wc.cbSize        = sizeof wc;
        wc.lpfnWndProc   = InputBoxProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "InputBoxClass";
        RegisterClassEx(&wc);
        registered = TRUE;
    }

    // Centre it over the owner window
    RECT r;
    GetWindowRect(owner, &r);
    int w = 360, h = 150;
    int x = r.left + (r.right - r.left - w) / 2;
    int y = r.top  + (r.bottom - r.top - h) / 3;

    HWND dlg = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, "InputBoxClass", title,
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              x, y, w, h, owner, NULL, hInst, NULL);

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND hPrompt = CreateWindow("STATIC", prompt, WS_CHILD | WS_VISIBLE,
                                12, 12, 320, 20, dlg, NULL, hInst, NULL);
    ibEdit = CreateWindow("EDIT", buf,
                          WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                          12, 36, 320, 24, dlg, (HMENU)100, hInst, NULL);
    HWND hOK = CreateWindow("BUTTON", "OK",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            152, 72, 85, 26, dlg, (HMENU)IDOK, hInst, NULL);
    HWND hCancel = CreateWindow("BUTTON", "Cancel",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                247, 72, 85, 26, dlg, (HMENU)IDCANCEL, hInst, NULL);

    SendMessage(hPrompt, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessage(ibEdit,  WM_SETFONT, (WPARAM)font, TRUE);
    SendMessage(hOK,     WM_SETFONT, (WPARAM)font, TRUE);
    SendMessage(hCancel, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessage(ibEdit, EM_LIMITTEXT, size - 1, 0);
    SendMessage(ibEdit, EM_SETSEL, 0, -1);      // select the starting text

    ibBuf  = buf;
    ibSize = size;
    ibDone = FALSE;
    ibOK   = FALSE;

    // Make it modal: disable the owner and run our own message loop
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(ibEdit);

    MSG m;
    while (!ibDone)
    {
        if (GetMessage(&m, NULL, 0, 0) <= 0)
        {
            PostQuitMessage((int)m.wParam);    // pass WM_QUIT on to the main loop
            break;
        }
        if (!IsDialogMessage(dlg, &m))         // gives Enter = OK, Esc = Cancel, Tab
        {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }

    EnableWindow(owner, TRUE);                 // re-enable BEFORE destroying,
    DestroyWindow(dlg);                        // so focus returns to the owner
    SetForegroundWindow(owner);
    return ibOK;
}
