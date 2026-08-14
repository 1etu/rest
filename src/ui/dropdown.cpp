#include "app/app.h"
#include "ui/dropdown.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "ui/paint.h"
#include <windowsx.h>
#include <dwmapi.h>

#define MAX_OPTS 32

static HWND wnd, owner;
static HFONT font;
static HHOOK mhook;
static const wchar_t *items[MAX_OPTS];
static int count, sel, hot = -1, tok, dpi, w, h, rowh;

static int S(int v)
{
    return MulDiv(v, dpi, 96);
}

static void paint(HDC dc)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(m, b);

    RECT all = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(th.pop);
    FillRect(m, &all, bg);
    DeleteObject(bg);
    SelectObject(m, font);
    SetBkMode(m, TRANSPARENT);

    int y = S(5);
    for (int i = 0; i < count; i++) {
        if (i == hot)
            paint_round(m, S(4), y, w - S(8), rowh, S(7), th.hover, 255);
        SetTextColor(m, i == sel ? th.accent : th.text);
        RECT t = {S(30), y, w - S(12), y + rowh};
        DrawTextW(m, items[i], -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (i == sel)
            paint_icon(m, ICON_CHECK, S(8), y + (rowh - S(16)) / 2, S(16), th.accent, 255);
        y += rowh;
    }

    BitBlt(dc, 0, 0, w, h, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(b);
    DeleteDC(m);
}

static int hit(int mx, int my)
{
    if (mx < 0 || mx >= w)
        return -1;
    int i = (my - S(5)) / rowh;
    return i >= 0 && i < count ? i : -1;
}

void dd_close(void)
{
    if (!wnd)
        return;
    HWND t = wnd;
    wnd = 0;
    if (mhook)
        UnhookWindowsHookEx(mhook), mhook = 0;
    DestroyWindow(t);
}

int dd_is_open(void)
{
    return wnd != 0;
}

static LRESULT CALLBACK mouseproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && wnd &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        POINT pt = ((MSLLHOOKSTRUCT *)lp)->pt;
        RECT rc;
        GetWindowRect(wnd, &rc);
        if (!PtInRect(&rc, pt))
            dd_close();
    }
    return CallNextHookEx(0, c, wp, lp);
}

static LRESULT CALLBACK ddproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        paint(BeginPaint(m, &ps));
        EndPaint(m, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i != hot) {
            hot = i;
            InvalidateRect(m, 0, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0)
            PostMessageW(owner, WM_DD, tok, i);
        dd_close();
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        DeleteObject(font);
        font = 0;
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void dd_open(HWND o, RECT anchor, const wchar_t **labels, int n, int s, int token)
{
    dd_close();
    if (n > MAX_OPTS)
        n = MAX_OPTS;
    owner = o;
    count = n;
    sel = s;
    tok = token;
    hot = -1;
    for (int i = 0; i < n; i++)
        items[i] = labels[i];

    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ddproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_dd";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_dd", 0, WS_POPUP, 0, 0, 0, 0, o, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, // try bold for accessibility maybne?
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    rowh = S(30);

    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, font);
    int tw = 0;
    for (int i = 0; i < n; i++) {
        SIZE sz;
        GetTextExtentPoint32W(dc, items[i], lstrlenW(items[i]), &sz);
        if (sz.cx > tw)
            tw = sz.cx;
    }
    SelectObject(dc, of);
    ReleaseDC(wnd, dc);

    w = tw + S(30) + S(16);
    int aw = anchor.right - anchor.left;
    if (w < aw)
        w = aw;
    h = rowh * n + S(10);

    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(o, MONITOR_DEFAULTTONEAREST), &mi);
    int x = anchor.right - w;
    int y = anchor.bottom + S(4);
    if (y + h > mi.rcWork.bottom - S(8))
        y = anchor.top - h - S(4);
    if (x < mi.rcWork.left + S(8))
        x = mi.rcWork.left + S(8);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    ShowWindow(wnd, SW_SHOWNOACTIVATE);
    mhook = SetWindowsHookExW(WH_MOUSE_LL, mouseproc, 0, 0);
}
