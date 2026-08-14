#include "app/app.h"
#include "shell/menu.h"
#include <windowsx.h>
#include <dwmapi.h>

#define F_SEP 1
#define F_DIM 2

struct item {
    int cmd;
    const wchar_t *text;
    UINT flags;
};

static const item items[] = {
    {0, L"Next break in 20 min", F_DIM},
    {0, 0, F_SEP},
    {CMD_PAUSE, L"Pause", F_DIM},
    {CMD_BREAK, L"Take a break now", F_DIM},
    {CMD_SKIP, L"Skip next break", F_DIM},
    {0, 0, F_SEP},
    {CMD_SETTINGS, L"Settings…", F_DIM},
    {CMD_LOGIN, L"Launch at login", F_DIM},
    {0, 0, F_SEP},
    {CMD_QUIT, L"Quit", 0},
};
#define N ((int)(sizeof items / sizeof items[0]))

static HWND wnd, owner;
static HFONT font;
static HHOOK mhook, khook;
static HWINEVENTHOOK fhook;
static DWORD closed_t;
static int dpi, w, h, hot = -1;
static COLORREF col_bg, col_text, col_dim, col_hot, col_sep;

static int S(int v)
{
    return MulDiv(v, dpi, 96);
}

static int light_mode(void)
{
    DWORD v = 1, n = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &n);
    return v;
}

static int item_h(int i)
{
    return items[i].flags & F_SEP ? S(9) : S(30);
}

static int hit(int mx, int my)
{
    if (mx < 0 || mx >= w)
        return -1;
    int y = S(6);
    for (int i = 0; i < N; i++) {
        int ih = item_h(i);
        if (my >= y && my < y + ih && !(items[i].flags & (F_SEP | F_DIM)))
            return i;
        y += ih;
    }
    return -1;
}

static void paint(HDC dc)
{
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ obmp = SelectObject(mem, bmp);

    RECT rc = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(col_bg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);

    int y = S(6);
    for (int i = 0; i < N; i++) {
        int ih = item_h(i);
        if (items[i].flags & F_SEP) {
            RECT s = {S(12), y + ih / 2, w - S(12), y + ih / 2 + 1};
            HBRUSH sb = CreateSolidBrush(col_sep);
            FillRect(mem, &s, sb);
            DeleteObject(sb);
        } else {
            if (i == hot) {
                HBRUSH hb = CreateSolidBrush(col_hot);
                HGDIOBJ ob = SelectObject(mem, hb);
                HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
                RoundRect(mem, S(5), y + S(1), w - S(5) + 1, y + ih - S(1) + 1, S(8), S(8));
                SelectObject(mem, ob);
                SelectObject(mem, op);
                DeleteObject(hb);
            }
            SetTextColor(mem, items[i].flags & F_DIM ? col_dim : col_text);
            RECT t = {S(16), y, w - S(16), y + ih};
            DrawTextW(mem, items[i].text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        y += ih;
    }

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void close(void)
{
    if (!wnd)
        return;
    HWND t = wnd;
    wnd = 0;
    closed_t = GetTickCount();
    DestroyWindow(t);
}

static LRESULT CALLBACK mouseproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && wnd &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        POINT pt = ((MSLLHOOKSTRUCT *)lp)->pt;
        RECT rc;
        GetWindowRect(wnd, &rc);
        if (!PtInRect(&rc, pt))
            close();
    }
    return CallNextHookEx(0, c, wp, lp);
}

static LRESULT CALLBACK keyproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && wnd && wp == WM_KEYDOWN &&
        ((KBDLLHOOKSTRUCT *)lp)->vkCode == VK_ESCAPE) {
        close();
        return 1;
    }
    return CallNextHookEx(0, c, wp, lp);
}

static void CALLBACK fgproc(HWINEVENTHOOK, DWORD, HWND hw, LONG, LONG, DWORD, DWORD)
{
    if (wnd && hw != wnd)
        close();
}

static void hooks_off(void)
{
    if (mhook) UnhookWindowsHookEx(mhook), mhook = 0;
    if (khook) UnhookWindowsHookEx(khook), khook = 0;
    if (fhook) UnhookWinEvent(fhook), fhook = 0;
}

static LRESULT CALLBACK menuproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        paint(BeginPaint(m, &ps));
        EndPaint(m, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i != hot) {
            hot = i;
            InvalidateRect(m, 0, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(m, &rc);
        if (!PtInRect(&rc, pt))
            close();
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0) {
            PostMessageW(owner, WM_COMMAND, items[i].cmd, 0);
            close();
        }
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        hooks_off();
        DeleteObject(font);
        font = 0;
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void menu_show(HWND o)
{
    if (wnd) {
        close();
        return;
    }
    if (GetTickCount() - closed_t < 400)
        return;
    owner = o;

    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = menuproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_menu";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    if (light_mode()) {
        col_bg = RGB(249, 249, 249);
        col_text = RGB(26, 26, 26);
        col_dim = RGB(130, 130, 130);
        col_hot = RGB(232, 232, 232);
        col_sep = RGB(224, 224, 224);
    } else {
        col_bg = RGB(43, 43, 43);
        col_text = RGB(240, 240, 240);
        col_dim = RGB(140, 140, 140);
        col_hot = RGB(62, 62, 62);
        col_sep = RGB(66, 66, 66);
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_menu", 0, WS_POPUP, 0, 0, 0, 0, o, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, font);
    int tw = 0;
    h = S(6);
    for (int i = 0; i < N; i++) {
        if (!(items[i].flags & F_SEP)) {
            SIZE sz;
            GetTextExtentPoint32W(dc, items[i].text, lstrlenW(items[i].text), &sz);
            if (sz.cx > tw)
                tw = sz.cx;
        }
        h += item_h(i);
    }
    h += S(6);
    w = tw + S(16) * 2;
    if (w < S(190))
        w = S(190);
    SelectObject(dc, of);
    ReleaseDC(wnd, dc);

    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int x = pt.x - w / 2;
    if (x + w > mi.rcWork.right - S(8))
        x = mi.rcWork.right - S(8) - w;
    if (x < mi.rcWork.left + S(8))
        x = mi.rcWork.left + S(8);
    int y = pt.y - h - S(8);
    if (y < mi.rcWork.top + S(8))
        y = pt.y + S(8);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    hot = -1;
    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    ShowWindow(wnd, SW_SHOWNOACTIVATE);
    mhook = SetWindowsHookExW(WH_MOUSE_LL, mouseproc, 0, 0);
    khook = SetWindowsHookExW(WH_KEYBOARD_LL, keyproc, 0, 0);
    fhook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        0, fgproc, 0, 0, WINEVENT_OUTOFCONTEXT);
}
