#include "app/app.h"
#include "core/config.h"
#include "core/presence.h"
#include "core/timer.h"
#include "shell/menu.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>

#define F_SEP 1
#define F_DIM 2
#define F_STAY 4

struct item {
    int cmd;
    int sid;
    int arg;
    int tidx;
    UINT flags;
};

static item items[24];
static int N;

static void put(int cmd, int sid, int arg, int tidx, UINT flags)
{
    items[N].cmd = cmd;
    items[N].sid = sid;
    items[N].arg = arg;
    items[N].tidx = tidx;
    items[N].flags = flags;
    N++;
}

static void build_items(void)
{
    static const int presets[] = {15, 20, 30, 45};
    N = 0;
    put(0, STR_NEXT_BREAK, 0, -1, F_DIM);
    put(0, 0, 0, -1, F_SEP);
    put(CMD_PAUSE, STR_PAUSE, 0, -1, F_STAY);
    put(CMD_BREAK, STR_BREAK_NOW, 0, -1, 0);
    put(CMD_SKIP, STR_SKIP, 0, -1, F_STAY);
    put(0, 0, 0, -1, F_SEP);
    put(0, STR_EVERY, 0, -1, F_DIM);
    for (int i = 0; i < 4; i++)
        put(CMD_INTERVAL, STR_MIN_FMT, presets[i], -1, F_STAY);
    for (int i = 0; i < cfg.ntimers; i++)
        put(CMD_INTERVAL, 0, cfg.timers[i].min, i, F_STAY);
    put(CMD_NEW_TIMER, STR_NEW_TIMER, 0, -1, F_STAY);
    put(0, 0, 0, -1, F_SEP);
    put(CMD_SETTINGS, STR_SETTINGS, 0, -1, F_DIM);
    put(CMD_LOGIN, STR_LOGIN, 0, -1, F_DIM);
    put(0, 0, 0, -1, F_SEP);
    put(CMD_QUIT, STR_QUIT, 0, -1, 0);
}

static HWND wnd, owner, prev_fg;
static HFONT font;
static HHOOK mhook, khook;
static HWINEVENTHOOK fhook;
static DWORD closed_t;
static int dpi, w, h, hot = -1;
static int editing, elen;
static wchar_t ebuf[48];
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

static const wchar_t *item_text(int i, wchar_t *buf)
{
    if (items[i].cmd == CMD_NEW_TIMER) {
        if (!editing)
            return str(STR_NEW_TIMER);
        if (!elen)
            return str(STR_TIMER_HINT);
        lstrcpyW(buf, ebuf);
        if ((GetTickCount() >> 9) & 1)
            lstrcatW(buf, L"_");
        return buf;
    }
    if (items[i].cmd == CMD_INTERVAL && items[i].tidx >= 0) {
        wchar_t m[16];
        wsprintfW(m, str(STR_MIN_FMT), items[i].arg);
        wsprintfW(buf, L"%s — %s", cfg.timers[items[i].tidx].label, m);
        return buf;
    }
    if (items[i].cmd == CMD_INTERVAL) {
        wsprintfW(buf, str(STR_MIN_FMT), items[i].arg);
        return buf;
    }
    if (items[i].cmd == CMD_PAUSE)
        return str(timer_paused() ? STR_RESUME : STR_PAUSE);
    if (i == 0) {
        if (timer_paused())
            return str(STR_PAUSED);
        if (timer_state() == T_HOLD) {
            int r = timer_hold_reason();
            return str(r & BUSY_MEETING ? STR_HELD_MEETING
                : r & BUSY_FULLSCREEN   ? STR_HELD_FULLSCREEN
                : r & BUSY_FOCUS_APP    ? STR_HELD_FOCUS
                                        : STR_HELD);
        }
        int s = timer_seconds_left();
        if (timer_on_break())
            wsprintfW(buf, str(STR_ON_BREAK), s);
        else
            wsprintfW(buf, str(STR_NEXT_BREAK), s / 60, s % 60);
        return buf;
    }
    return str(items[i].sid);
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
            wchar_t buf[64];
            int tx = S(16);
            if (items[i].cmd == CMD_INTERVAL || items[i].cmd == CMD_NEW_TIMER)
                tx = S(32);
            if (items[i].cmd == CMD_INTERVAL && items[i].arg == timer_interval()) {
                SetTextColor(mem, APP_ACCENT);
                RECT c2 = {S(14), y, S(32), y + ih};
                DrawTextW(mem, L"\x2713", -1, &c2,
                    DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
            int dim = items[i].flags & F_DIM ||
                (items[i].cmd == CMD_NEW_TIMER && editing && !elen);
            SetTextColor(mem, dim ? col_dim : col_text);
            RECT t = {tx, y, w - S(16), y + ih};
            DrawTextW(mem, item_text(i, buf), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        y += ih;
    }

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void measure(void)
{
    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, font);
    int tw = 0;
    h = S(6);
    for (int i = 0; i < N; i++) {
        if (!(items[i].flags & F_SEP)) {
            wchar_t buf[64];
            const wchar_t *tx = item_text(i, buf);
            SIZE sz;
            GetTextExtentPoint32W(dc, tx, lstrlenW(tx), &sz);
            if (items[i].cmd == CMD_INTERVAL || items[i].cmd == CMD_NEW_TIMER)
                sz.cx += S(16);
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
}

static void relayout(void)
{
    RECT rc;
    GetWindowRect(wnd, &rc);
    int bottom = rc.bottom;
    measure();
    SetWindowPos(wnd, HWND_TOPMOST, rc.left, bottom - h, w, h, SWP_NOACTIVATE);
    InvalidateRect(wnd, 0, FALSE);
}

static int parse_timer(const wchar_t *in, wchar_t *label, int *out)
{
    int b = -1, e = -1;
    for (int i = 0; in[i]; i++)
        if (in[i] >= '0' && in[i] <= '9') {
            if (i == 0 || in[i - 1] < '0' || in[i - 1] > '9')
                b = i;
            e = i + 1;
        }
    if (b < 0)
        return 0;
    int num = 0;
    for (int i = b; i < e; i++)
        num = num * 10 + in[i] - '0';
    const wchar_t *u = in + e;
    while (*u == L' ')
        u++;
    wchar_t c0 = *u | 0x20, c1 = u[0] ? u[1] | 0x20 : 0;
    int hours = c0 == L'h' || (c0 == L's' && c1 == L'a');
    int min = hours ? num * 60 : num;
    if (min < 1 || min > 1440)
        return 0;
    int n = b;
    while (n > 0 && (in[n - 1] == L' ' || in[n - 1] == L'-' || in[n - 1] == L','))
        n--;
    lstrcpynW(label, in, n + 1 > 40 ? 40 : n + 1);
    if (!label[0])
        wsprintfW(label, str(STR_MIN_FMT), min);
    *out = min;
    return 1;
}

static void edit_begin(void)
{
    editing = 1;
    elen = 0;
    ebuf[0] = 0;
    prev_fg = GetForegroundWindow();
    SetWindowLongPtrW(wnd, GWL_EXSTYLE,
        GetWindowLongPtrW(wnd, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
    SetForegroundWindow(wnd);
    SetFocus(wnd);
    InvalidateRect(wnd, 0, FALSE);
}

static void edit_end(int commit, int restore)
{
    if (!editing)
        return;
    if (commit && elen) {
        wchar_t lab[40];
        int min;
        if (!parse_timer(ebuf, lab, &min) || cfg.ntimers >= CFG_MAX_TIMERS)
            return;
        lstrcpyW(cfg.timers[cfg.ntimers].label, lab);
        cfg.timers[cfg.ntimers].min = min;
        cfg.ntimers++;
        cfg_save();
    }
    editing = 0;
    elen = 0;
    ebuf[0] = 0;
    SetWindowLongPtrW(wnd, GWL_EXSTYLE,
        GetWindowLongPtrW(wnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    if (restore && prev_fg)
        SetForegroundWindow(prev_fg);
    build_items();
    relayout();
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
    if (c == HC_ACTION && wnd && !editing && wp == WM_KEYDOWN &&
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
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(m, &rc);
        if (!PtInRect(&rc, pt))
            close();
        return 0;
    }
    case WM_RBUTTONDOWN: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0 && items[i].tidx >= 0) {
            for (int j = items[i].tidx; j < cfg.ntimers - 1; j++)
                cfg.timers[j] = cfg.timers[j + 1];
            cfg.ntimers--;
            cfg_save();
            build_items();
            relayout();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i < 0)
            return 0;
        if (items[i].cmd == CMD_NEW_TIMER) {
            if (!editing)
                edit_begin();
            return 0;
        }
        if (editing)
            edit_end(0, 1);
        PostMessageW(owner, WM_COMMAND, MAKEWPARAM(items[i].cmd, items[i].arg), 0);
        if (items[i].flags & F_STAY)
            InvalidateRect(m, 0, FALSE);
        else
            close();
        return 0;
    }
    case WM_CHAR:
        if (editing) {
            if (wp == 8 && elen)
                ebuf[--elen] = 0;
            else if (wp >= 32 && elen < 46) {
                ebuf[elen++] = (wchar_t)wp;
                ebuf[elen] = 0;
            }
            InvalidateRect(m, 0, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (editing && wp == VK_RETURN)
            edit_end(1, 1);
        else if (editing && wp == VK_ESCAPE)
            edit_end(0, 1);
        return 0;
    case WM_KILLFOCUS:
        edit_end(0, 0);
        return 0;
    case WM_TIMER:
        InvalidateRect(m, 0, FALSE);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        editing = 0;
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

    build_items();
    measure();

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
    SetTimer(wnd, 1, 250, 0);
    mhook = SetWindowsHookExW(WH_MOUSE_LL, mouseproc, 0, 0);
    khook = SetWindowsHookExW(WH_KEYBOARD_LL, keyproc, 0, 0);
    fhook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        0, fgproc, 0, 0, WINEVENT_OUTOFCONTEXT);
}
