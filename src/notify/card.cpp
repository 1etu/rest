#include "app/app.h"
#include "core/config.h"
#include "core/timer.h"
#include "notify/card.h"
#include "ui/paint.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <math.h>

#define M_NOTIFY 0
#define M_POPUP 1
#define M_CURSOR 2
#define SLIDE_MS 380.0

static HWND wnd;
static HDC basedc, facedc, facebg;
static HBITMAP basebmp, facebmp, facebgbmp;
static void *facebits;
static HFONT count_f, title_f, sub_f, btn_f;
static RECT face_rc, skip_rc;
static int mode, dpi, w, h, hot, ring_r, ring_t;
static int cellw, cellh, colonw;
static int shown_sec, total_sec;
static int last_off = -9999;
static float last_deg = -1;
static double freq, anim_t, sec_t;
static wchar_t cur[8], old_[8];
static COLORREF col_bg, col_title, col_sub, col_track, col_pill, col_pilltx;
static POINT follow;

static int S(int v)
{
    return MulDiv(v, dpi, 96);
}

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / freq;
}

static int light_mode(void)
{
    DWORD v = 1, n = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &n);
    return v;
}

static void fmt(wchar_t *out, int s)
{
    if (total_sec >= 60)
        wsprintfW(out, L"%d:%02d", s / 60, s % 60);
    else
        wsprintfW(out, L"%d", s);
}

static void draw_face(float frac, int off)
{
    int fw = face_rc.right - face_rc.left, fh = face_rc.bottom - face_rc.top;
    BitBlt(facedc, 0, 0, fw, fh, facebg, 0, 0, SRCCOPY);
    GdiFlush();
    paint_ring(facebits, fw, fh, fw / 2.0f, fh / 2.0f, (float)ring_r, (float)ring_t,
        frac, col_track, 1.0f, APP_ACCENT);

    SelectObject(facedc, count_f);
    SetTextColor(facedc, col_title);
    int n = lstrlenW(cur);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += cur[i] == L':' ? colonw : cellw;
    int x = (fw - total) / 2;
    int top = (fh - cellh) / 2;
    for (int i = 0; i < n; i++) {
        int cw = cur[i] == L':' ? colonw : cellw;
        RECT cell = {x, top, x + cw, top + cellh};
        wchar_t s1[2] = {old_[i], 0}, s2[2] = {cur[i], 0};
        SaveDC(facedc);
        IntersectClipRect(facedc, cell.left, cell.top, cell.right, cell.bottom);
        if (off && old_[i] && old_[i] != cur[i]) {
            RECT a = cell, b = cell;
            OffsetRect(&a, 0, -off);
            OffsetRect(&b, 0, cellh - off);
            DrawTextW(facedc, s1, 1, &a, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            DrawTextW(facedc, s2, 1, &b, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        } else {
            DrawTextW(facedc, s2, 1, &cell,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        RestoreDC(facedc, -1);
        x += cw;
    }

    BitBlt(basedc, face_rc.left, face_rc.top, fw, fh, facedc, 0, 0, SRCCOPY);
    if (wnd)
        InvalidateRect(wnd, &face_rc, FALSE);
}

static void draw_skip(void)
{
    RECT r = {skip_rc.left - S(4), skip_rc.top - S(4), skip_rc.right + S(4),
        skip_rc.bottom + S(4)};
    HBRUSH b = CreateSolidBrush(col_bg);
    FillRect(basedc, &r, b);
    DeleteObject(b);
    int rad = (skip_rc.bottom - skip_rc.top) / 2;
    paint_round(basedc, skip_rc.left, skip_rc.top, skip_rc.right - skip_rc.left,
        skip_rc.bottom - skip_rc.top, rad, col_pill, hot ? 46 : 24);
    SelectObject(basedc, btn_f);
    SetTextColor(basedc, col_pilltx);
    RECT t = skip_rc;
    DrawTextW(basedc, str(STR_SKIP_SHORT), -1, &t,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (wnd)
        InvalidateRect(wnd, &r, FALSE);
}

static void build_base(void)
{
    RECT all = {0, 0, w, h};
    HBRUSH b = CreateSolidBrush(col_bg);
    FillRect(basedc, &all, b);
    DeleteObject(b);
    SetBkMode(basedc, TRANSPARENT);

    int tx = face_rc.right + S(16);
    int tw = skip_rc.left - S(12) - tx;
    SelectObject(basedc, title_f);
    SetTextColor(basedc, col_title);
    RECT tr = {tx, h / 2 - S(21), tx + tw, h / 2 - S(1)};
    DrawTextW(basedc, str(STR_OVERLAY_TITLE), -1, &tr,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(basedc, sub_f);
    SetTextColor(basedc, col_sub);
    RECT sr = {tx, h / 2 + S(1), tx + tw, h / 2 + S(19)};
    DrawTextW(basedc, str(STR_LOOK_AWAY), -1, &sr,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    BitBlt(facebg, 0, 0, face_rc.right - face_rc.left, face_rc.bottom - face_rc.top,
        basedc, face_rc.left, face_rc.top, SRCCOPY);
    draw_skip();
    draw_face(1, 0);
}

static void place(void)
{
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT a = mi.rcWork;
    int m = S(16), x, y;

    if (mode == M_CURSOR) {
        x = pt.x + S(18);
        y = pt.y + S(18);
    } else if (mode == M_POPUP) {
        x = (a.left + a.right - w) / 2;
        y = (a.top + a.bottom - h) / 2;
    } else {
        const wchar_t *p = cfg.pos;
        x = a.left + m;
        y = a.top + m;
        if (wcsstr(p, L"center") && !wcsstr(p, L"-center"))
            x = (a.left + a.right - w) / 2, y = (a.top + a.bottom - h) / 2;
        else {
            if (wcsstr(p, L"right"))
                x = a.right - w - m;
            else if (wcsstr(p, L"center"))
                x = (a.left + a.right - w) / 2;
            if (wcsstr(p, L"bottom"))
                y = a.bottom - h - m;
        }
    }
    if (x + w > a.right - m)
        x = a.right - m - w;
    if (x < a.left + m)
        x = a.left + m;
    if (y + h > a.bottom - m)
        y = a.bottom - m - h;
    if (y < a.top + m)
        y = a.top + m;
    follow.x = x;
    follow.y = y;
    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

static void tick(double t)
{
    int sec = timer_seconds_left();
    if (sec != shown_sec) {
        lstrcpyW(old_, cur);
        fmt(cur, sec);
        if (lstrlenW(old_) != lstrlenW(cur))
            lstrcpyW(old_, cur);
        shown_sec = sec;
        sec_t = anim_t = t;
    }
    float sub = (float)((t - sec_t) / 1000.0);
    if (sub > 1) sub = 1;
    float frac = (shown_sec - sub) / total_sec;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    float e = (float)((t - anim_t) / SLIDE_MS);
    int off = 0;
    if (e < 1) {
        float k = 1 - e;
        off = (int)((1 - k * k * k) * cellh);
    }
    float deg = frac * 360;
    if (off != last_off || fabsf(deg - last_deg) >= 0.2f) {
        last_off = off;
        last_deg = deg;
        draw_face(frac, off);
    }

    if (mode == M_CURSOR) {
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
        int tx = pt.x + S(18), ty = pt.y + S(18);
        if (tx + w > mi.rcWork.right - S(8))
            tx = pt.x - S(18) - w;
        if (ty + h > mi.rcWork.bottom - S(8))
            ty = pt.y - S(18) - h;
        int nx = follow.x + (tx - follow.x) / 5;
        int ny = follow.y + (ty - follow.y) / 5;
        if (nx != follow.x || ny != follow.y) {
            follow.x = nx;
            follow.y = ny;
            SetWindowPos(wnd, HWND_TOPMOST, nx, ny, w, h,
                SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOREDRAW);
        }
    }
}

static LRESULT CALLBACK cardproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m, &ps);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top, basedc, ps.rcPaint.left, ps.rcPaint.top,
            SRCCOPY);
        EndPaint(m, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        BitBlt((HDC)wp, 0, 0, w, h, basedc, 0, 0, SRCCOPY);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        tick(now_ms());
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int over = PtInRect(&skip_rc, pt);
        if (over != hot) {
            hot = over;
            draw_skip();
        }
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(0, hot ? IDC_HAND : IDC_ARROW));
        return 1;
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&skip_rc, pt))
            timer_skip();
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        DeleteDC(basedc);
        DeleteDC(facedc);
        DeleteDC(facebg);
        DeleteObject(basebmp);
        DeleteObject(facebmp);
        DeleteObject(facebgbmp);
        DeleteObject(count_f);
        DeleteObject(title_f);
        DeleteObject(sub_f);
        DeleteObject(btn_f);
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

static void show(int m)
{
    mode = m;
    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = cardproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_card";
        RegisterClassW(&wc);
        reg = 1;
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;

    if (light_mode()) {
        col_bg = RGB(250, 250, 250);
        col_title = RGB(28, 28, 28);
        col_sub = RGB(126, 126, 126);
        col_track = RGB(224, 224, 224);
        col_pill = RGB(0, 0, 0);
        col_pilltx = RGB(176, 104, 44);
    } else {
        col_bg = RGB(38, 38, 38);
        col_title = RGB(240, 240, 240);
        col_sub = RGB(148, 148, 148);
        col_track = RGB(62, 62, 62);
        col_pill = RGB(255, 255, 255);
        col_pilltx = APP_ACCENT;
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_card", 0, WS_POPUP, 0, 0, 10, 10, 0, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);

    int big = mode == M_POPUP;
    int rf = big ? 34 : 26;
    count_f = CreateFontW(-MulDiv(big ? 19 : 15, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    title_f = CreateFontW(-MulDiv(big ? 15 : 13, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    sub_f = CreateFontW(-MulDiv(big ? 11 : 10, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    btn_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    HDC wdc = GetDC(wnd);
    HGDIOBJ of = SelectObject(wdc, count_f);
    SIZE dz, cz;
    GetTextExtentPoint32W(wdc, L"0", 1, &dz);
    GetTextExtentPoint32W(wdc, L":", 1, &cz);
    cellw = dz.cx + S(1);
    colonw = cz.cx;
    cellh = dz.cy;
    SelectObject(wdc, btn_f);
    SIZE bs;
    GetTextExtentPoint32W(wdc, str(STR_SKIP_SHORT), lstrlenW(str(STR_SKIP_SHORT)), &bs);
    SelectObject(wdc, of);
    ReleaseDC(wnd, wdc);

    ring_r = S(rf);
    ring_t = S(big ? 4 : 3);
    int pad = S(16);
    int ringbox = (ring_r + ring_t) * 2 + S(4);
    h = ringbox + pad * 2;
    int bw = bs.cx + S(26), bh = S(26);
    w = pad + ringbox + S(16) + S(big ? 230 : 186) + S(12) + bw + pad;

    face_rc.left = pad;
    face_rc.right = pad + ringbox;
    face_rc.top = (h - ringbox) / 2;
    face_rc.bottom = face_rc.top + ringbox;
    skip_rc.left = w - pad - bw;
    skip_rc.right = w - pad;
    skip_rc.top = (h - bh) / 2;
    skip_rc.bottom = skip_rc.top + bh;

    wdc = GetDC(wnd);
    basedc = CreateCompatibleDC(wdc);
    basebmp = CreateCompatibleBitmap(wdc, w, h);
    SelectObject(basedc, basebmp);
    int fw = face_rc.right - face_rc.left, fh = face_rc.bottom - face_rc.top;
    BITMAPV5HEADER bh5 = {sizeof bh5};
    bh5.bV5Width = fw;
    bh5.bV5Height = -fh;
    bh5.bV5Planes = 1;
    bh5.bV5BitCount = 32;
    bh5.bV5Compression = BI_RGB;
    facedc = CreateCompatibleDC(wdc);
    facebmp = CreateDIBSection(0, (BITMAPINFO *)&bh5, DIB_RGB_COLORS, &facebits, 0, 0);
    SelectObject(facedc, facebmp);
    SetBkMode(facedc, TRANSPARENT);
    facebg = CreateCompatibleDC(wdc);
    facebgbmp = CreateCompatibleBitmap(wdc, fw, fh);
    SelectObject(facebg, facebgbmp);
    ReleaseDC(wnd, wdc);

    int sec = timer_seconds_left();
    total_sec = sec > 0 ? sec : 1;
    fmt(cur, sec);
    lstrcpyW(old_, cur);
    shown_sec = sec;
    hot = 0;
    last_off = -9999;
    last_deg = -1;
    sec_t = anim_t = now_ms();

    build_base();
    place();

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
    AnimateWindow(wnd, 180, AW_BLEND);
    timeBeginPeriod(1);
    SetTimer(wnd, 1, 12, 0);
}

static void hide(void)
{
    if (!wnd)
        return;
    HWND t = wnd;
    wnd = 0;
    KillTimer(t, 1);
    timeEndPeriod(1);
    AnimateWindow(t, 150, AW_HIDE | AW_BLEND);
    DestroyWindow(t);
}

void card_notify(int on)
{
    on ? show(M_NOTIFY) : hide();
}

void card_popup(int on)
{
    on ? show(M_POPUP) : hide();
}

void card_cursor(int on)
{
    on ? show(M_CURSOR) : hide();
}
