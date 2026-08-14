#include "app/app.h"
#include "core/timer.h"
#include "notify/overlay.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <math.h>

static HWND wnd;
static HHOOK khook;
static HDC basedc, stripbg, clockbg, pillbg;
static HBITMAP basebmp, stripbmp, clockbmp, pillbmp;
static HFONT title_f, sub_f, count_f, small_f, btn_f;
static int dpi, mx, my, mw, mh;
static int cellw, colonw, cellh;
static int shown_sec;
static DWORD anim_t, esc_t;
static wchar_t cur[8], old_[8];
static RECT strip_rc, clock_rc, pill_rc, skip_rc, lock_rc;
static int hot;
static SYSTEMTIME last_clock;

static int S(int v)
{
    return MulDiv(v, dpi, 96);
}

static void fill_round(HDC dc, int x, int y, int rw, int rh, int rad, COLORREF c, int a)
{
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = rw;
    bh.bV5Height = -rh;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ ob = SelectObject(m, b);
    DWORD *p = (DWORD *)bits;
    for (int yy = 0; yy < rh; yy++)
        for (int xx = 0; xx < rw; xx++, p++) {
            float fx = xx + 0.5f, fy = yy + 0.5f;
            float qx = fx < rad ? rad - fx : fx > rw - rad ? fx - (rw - rad) : 0;
            float qy = fy < rad ? rad - fy : fy > rh - rad ? fy - (rh - rad) : 0;
            float cov = rad - sqrtf(qx * qx + qy * qy);
            if (cov < 0) cov = 0;
            if (cov > 1) cov = 1;
            int aa = (int)(a * cov);
            *p = (DWORD)aa << 24 | (DWORD)(GetRValue(c) * aa / 255) << 16 |
                (DWORD)(GetGValue(c) * aa / 255) << 8 | (DWORD)(GetBValue(c) * aa / 255);
        }
    GdiFlush();
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, rw, rh, m, 0, 0, rw, rh, bf);
    SelectObject(m, ob);
    DeleteObject(b);
    DeleteDC(m);
}

static void blur_pass(DWORD *src, DWORD *dst, int bw, int bh, int horiz)
{
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int k = -2; k <= 2; k++) {
                int xx = horiz ? x + k : x, yy = horiz ? y : y + k;
                if (xx < 0 || xx >= bw || yy < 0 || yy >= bh)
                    continue;
                DWORD c = src[yy * bw + xx];
                r += (c >> 16) & 255;
                g += (c >> 8) & 255;
                b += c & 255;
                n++;
            }
            dst[y * bw + x] = (DWORD)(r / n) << 16 | (g / n) << 8 | (b / n);
        }
}

static void draw_clock(void)
{
    BitBlt(basedc, clock_rc.left, clock_rc.top, clock_rc.right - clock_rc.left,
        clock_rc.bottom - clock_rc.top, clockbg, 0, 0, SRCCOPY);
    GetLocalTime(&last_clock);
    wchar_t t[16];
    GetTimeFormatEx(0, TIME_NOSECONDS, &last_clock, 0, t, 16);
    SelectObject(basedc, small_f);
    SetTextColor(basedc, RGB(190, 196, 205));
    RECT r = clock_rc;
    DrawTextW(basedc, t, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (wnd)
        InvalidateRect(wnd, &clock_rc, FALSE);
}

static void draw_pills(void)
{
    BitBlt(basedc, pill_rc.left, pill_rc.top, pill_rc.right - pill_rc.left,
        pill_rc.bottom - pill_rc.top, pillbg, 0, 0, SRCCOPY);
    int rad = (skip_rc.bottom - skip_rc.top) / 2;
    fill_round(basedc, skip_rc.left, skip_rc.top, skip_rc.right - skip_rc.left,
        skip_rc.bottom - skip_rc.top, rad, RGB(255, 255, 255), hot == 1 ? 58 : 32);
    fill_round(basedc, lock_rc.left, lock_rc.top, lock_rc.right - lock_rc.left,
        lock_rc.bottom - lock_rc.top, rad, RGB(255, 255, 255), hot == 2 ? 58 : 32);
    SelectObject(basedc, btn_f);
    SetTextColor(basedc, RGB(238, 242, 248));
    RECT r1 = skip_rc, r2 = lock_rc;
    DrawTextW(basedc, str(STR_SKIP_BREAK), -1, &r1,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    DrawTextW(basedc, str(STR_LOCK), -1, &r2,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (wnd)
        InvalidateRect(wnd, &pill_rc, FALSE);
}

static void draw_strip(DWORD dt)
{
    BitBlt(basedc, strip_rc.left, strip_rc.top, strip_rc.right - strip_rc.left,
        strip_rc.bottom - strip_rc.top, stripbg, 0, 0, SRCCOPY);
    SelectObject(basedc, count_f);
    SetTextColor(basedc, RGB(228, 236, 248));
    float q = dt >= 220 ? 1.0f : dt / 220.0f;
    q = q * q * (3 - 2 * q);
    int x = strip_rc.left + S(8);
    for (int i = 0; cur[i]; i++) {
        int cw = cur[i] == L':' ? colonw : cellw;
        RECT cell = {x, strip_rc.top + S(6), x + cw, strip_rc.top + S(6) + cellh};
        wchar_t s1[2] = {old_[i], 0}, s2[2] = {cur[i], 0};
        SaveDC(basedc);
        IntersectClipRect(basedc, cell.left, cell.top, cell.right, cell.bottom);
        if (q < 1 && old_[i] && old_[i] != cur[i]) {
            int off = (int)(q * cellh);
            RECT a = cell, b = cell;
            OffsetRect(&a, 0, -off);
            OffsetRect(&b, 0, cellh - off);
            DrawTextW(basedc, s1, 1, &a, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            DrawTextW(basedc, s2, 1, &b, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        } else {
            DrawTextW(basedc, s2, 1, &cell, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        RestoreDC(basedc, -1);
        x += cw;
    }
    if (wnd)
        InvalidateRect(wnd, &strip_rc, FALSE);
}

static void build_base(void)
{
    HDC sdc = GetDC(0);
    BitBlt(basedc, 0, 0, mw, mh, sdc, mx, my, SRCCOPY);
    ReleaseDC(0, sdc);

    int bw = mw / 12, bh2 = mh / 12;
    BITMAPV5HEADER bhh = {sizeof bhh};
    bhh.bV5Width = bw;
    bhh.bV5Height = -bh2;
    bhh.bV5Planes = 1;
    bhh.bV5BitCount = 32;
    bhh.bV5Compression = BI_RGB;
    void *bits;
    HDC smalldc = CreateCompatibleDC(basedc);
    HBITMAP smallbmp = CreateDIBSection(0, (BITMAPINFO *)&bhh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ os = SelectObject(smalldc, smallbmp);
    SetStretchBltMode(smalldc, HALFTONE);
    SetBrushOrgEx(smalldc, 0, 0, 0);
    StretchBlt(smalldc, 0, 0, bw, bh2, basedc, 0, 0, mw, mh, SRCCOPY);
    GdiFlush();

    DWORD *tmp = (DWORD *)HeapAlloc(GetProcessHeap(), 0, bw * bh2 * 4);
    blur_pass((DWORD *)bits, tmp, bw, bh2, 1);
    blur_pass(tmp, (DWORD *)bits, bw, bh2, 0);
    blur_pass((DWORD *)bits, tmp, bw, bh2, 1);
    blur_pass(tmp, (DWORD *)bits, bw, bh2, 0);
    HeapFree(GetProcessHeap(), 0, tmp);
    DWORD *p = (DWORD *)bits;
    for (int i = 0; i < bw * bh2; i++, p++) {
        int r = ((*p >> 16) & 255) * 130 / 255;
        int g = ((*p >> 8) & 255) * 140 / 255;
        int b = (*p & 255) * 160 / 255 + 16;
        if (b > 255)
            b = 255;
        *p = (DWORD)r << 16 | (DWORD)g << 8 | b;
    }

    SetStretchBltMode(basedc, HALFTONE);
    SetBrushOrgEx(basedc, 0, 0, 0);
    StretchBlt(basedc, 0, 0, mw, mh, smalldc, 0, 0, bw, bh2, SRCCOPY);
    SelectObject(smalldc, os);
    DeleteObject(smallbmp);
    DeleteDC(smalldc);

    SetBkMode(basedc, TRANSPARENT);
    SelectObject(basedc, title_f);
    SetTextColor(basedc, RGB(245, 247, 250));
    RECT tr = {0, mh * 33 / 100 - S(34), mw, mh * 33 / 100 + S(34)};
    DrawTextW(basedc, str(STR_OVERLAY_TITLE), -1, &tr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SelectObject(basedc, sub_f);
    SetTextColor(basedc, RGB(210, 215, 222));
    RECT sr = {0, mh * 41 / 100 - S(14), mw, mh * 41 / 100 + S(14)};
    DrawTextW(basedc, str(STR_LOOK_AWAY), -1, &sr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    fill_round(basedc, mw / 2 - S(32), mh * 47 / 100, S(64), S(2), 1,
        RGB(255, 255, 255), 80);
    SelectObject(basedc, small_f);
    SetTextColor(basedc, RGB(150, 158, 170));
    RECT hr = {0, pill_rc.bottom + S(10), mw, pill_rc.bottom + S(34)};
    DrawTextW(basedc, str(STR_ESC_HINT), -1, &hr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    BitBlt(stripbg, 0, 0, strip_rc.right - strip_rc.left,
        strip_rc.bottom - strip_rc.top, basedc, strip_rc.left, strip_rc.top, SRCCOPY);
    BitBlt(clockbg, 0, 0, clock_rc.right - clock_rc.left,
        clock_rc.bottom - clock_rc.top, basedc, clock_rc.left, clock_rc.top, SRCCOPY);
    BitBlt(pillbg, 0, 0, pill_rc.right - pill_rc.left,
        pill_rc.bottom - pill_rc.top, basedc, pill_rc.left, pill_rc.top, SRCCOPY);

    draw_clock();
    draw_pills();
    draw_strip(1000);
}

static LRESULT CALLBACK escproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && wnd && wp == WM_KEYDOWN &&
        ((KBDLLHOOKSTRUCT *)lp)->vkCode == VK_ESCAPE) {
        DWORD now = GetTickCount();
        if (now - esc_t < 1200)
            timer_skip();
        else
            esc_t = now;
        return 1;
    }
    return CallNextHookEx(0, c, wp, lp);
}

static LRESULT CALLBACK overlayproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m, &ps);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
            basedc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(m, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        BitBlt((HDC)wp, 0, 0, mw, mh, basedc, 0, 0, SRCCOPY);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER: {
        int s = timer_seconds_left();
        if (s != shown_sec) {
            lstrcpyW(old_, cur);
            wsprintfW(cur, L"%02d:%02d", s / 60, s % 60);
            shown_sec = s;
            anim_t = GetTickCount();
        }
        DWORD dt = GetTickCount() - anim_t;
        if (dt < 300)
            draw_strip(dt);
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (st.wMinute != last_clock.wMinute)
            draw_clock();
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int over = PtInRect(&skip_rc, pt) ? 1 : PtInRect(&lock_rc, pt) ? 2 : 0;
        if (over != hot) {
            hot = over;
            draw_pills();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&skip_rc, pt))
            timer_skip();
        else if (PtInRect(&lock_rc, pt))
            LockWorkStation();
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        DeleteDC(basedc);
        DeleteDC(stripbg);
        DeleteDC(clockbg);
        DeleteDC(pillbg);
        DeleteObject(basebmp);
        DeleteObject(stripbmp);
        DeleteObject(clockbmp);
        DeleteObject(pillbmp);
        DeleteObject(title_f);
        DeleteObject(sub_f);
        DeleteObject(count_f);
        DeleteObject(small_f);
        DeleteObject(btn_f);
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void overlay_break(int on)
{
    if (!on) {
        if (!wnd)
            return;
        HWND t = wnd;
        wnd = 0;
        if (khook)
            UnhookWindowsHookEx(khook), khook = 0;
        KillTimer(t, 1);
        AnimateWindow(t, 150, AW_HIDE | AW_BLEND);
        DestroyWindow(t);
        return;
    }
    if (wnd)
        return;

    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = overlayproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_overlay";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    POINT cpt;
    GetCursorPos(&cpt);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(cpt, MONITOR_DEFAULTTONEAREST), &mi);
    mx = mi.rcMonitor.left;
    my = mi.rcMonitor.top;
    mw = mi.rcMonitor.right - mx;
    mh = mi.rcMonitor.bottom - my;

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_overlay", 0, WS_POPUP, mx, my, mw, mh, 0, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    title_f = CreateFontW(-MulDiv(30, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    sub_f = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    count_f = CreateFontW(-MulDiv(54, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    small_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    btn_f = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    HDC wdc = GetDC(wnd);
    basedc = CreateCompatibleDC(wdc);
    basebmp = CreateCompatibleBitmap(wdc, mw, mh);
    SelectObject(basedc, basebmp);

    HGDIOBJ of = SelectObject(wdc, count_f);
    SIZE dz, cz;
    GetTextExtentPoint32W(wdc, L"0", 1, &dz);
    GetTextExtentPoint32W(wdc, L":", 1, &cz);
    cellw = dz.cx + S(4);
    colonw = cz.cx + S(2);
    cellh = dz.cy;
    SIZE bs1, bs2;
    SelectObject(wdc, btn_f);
    GetTextExtentPoint32W(wdc, str(STR_SKIP_BREAK), lstrlenW(str(STR_SKIP_BREAK)), &bs1);
    GetTextExtentPoint32W(wdc, str(STR_LOCK), lstrlenW(str(STR_LOCK)), &bs2);
    SelectObject(wdc, of);

    int total = cellw * 4 + colonw;
    int scy = mh * 55 / 100;
    strip_rc.left = (mw - total) / 2 - S(8);
    strip_rc.right = strip_rc.left + total + S(16);
    strip_rc.top = scy - cellh / 2 - S(6);
    strip_rc.bottom = scy + cellh / 2 + S(6);
    clock_rc.left = mw / 2 - S(90);
    clock_rc.right = mw / 2 + S(90);
    clock_rc.top = S(36);
    clock_rc.bottom = S(62);
    int ph = S(34), py = mh * 78 / 100;
    int w1 = bs1.cx + S(40), w2 = bs2.cx + S(40), gap = S(12);
    skip_rc.left = mw / 2 - (w1 + w2 + gap) / 2;
    skip_rc.right = skip_rc.left + w1;
    skip_rc.top = py;
    skip_rc.bottom = py + ph;
    lock_rc.left = skip_rc.right + gap;
    lock_rc.right = lock_rc.left + w2;
    lock_rc.top = py;
    lock_rc.bottom = py + ph;
    pill_rc.left = skip_rc.left - S(8);
    pill_rc.right = lock_rc.right + S(8);
    pill_rc.top = py - S(4);
    pill_rc.bottom = py + ph + S(4);

    stripbg = CreateCompatibleDC(wdc);
    stripbmp = CreateCompatibleBitmap(wdc, strip_rc.right - strip_rc.left,
        strip_rc.bottom - strip_rc.top);
    SelectObject(stripbg, stripbmp);
    clockbg = CreateCompatibleDC(wdc);
    clockbmp = CreateCompatibleBitmap(wdc, clock_rc.right - clock_rc.left,
        clock_rc.bottom - clock_rc.top);
    SelectObject(clockbg, clockbmp);
    pillbg = CreateCompatibleDC(wdc);
    pillbmp = CreateCompatibleBitmap(wdc, pill_rc.right - pill_rc.left,
        pill_rc.bottom - pill_rc.top);
    SelectObject(pillbg, pillbmp);
    ReleaseDC(wnd, wdc);

    int s = timer_seconds_left();
    wsprintfW(cur, L"%02d:%02d", s / 60, s % 60);
    lstrcpyW(old_, cur);
    shown_sec = s;
    anim_t = GetTickCount() - 1000;
    hot = 0;
    esc_t = 0;

    build_base();

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    SetWindowPos(wnd, HWND_TOPMOST, mx, my, mw, mh, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    AnimateWindow(wnd, 220, AW_BLEND);
    SetTimer(wnd, 1, 33, 0);
    khook = SetWindowsHookExW(WH_KEYBOARD_LL, escproc, 0, 0);
}
