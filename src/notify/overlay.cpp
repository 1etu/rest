#include "app/app.h"
#include "core/timer.h"
#include "notify/overlay.h"
#include "ui/paint.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <math.h>

struct accent_policy {
    int state;
    int flags;
    DWORD gradient;
    int anim;
};

struct wca_data {
    int attrib;
    void *data;
    SIZE_T size;
};

typedef BOOL(WINAPI *swca_fn)(HWND, wca_data *);

#define ACCENT_ACRYLIC 4
#define WCA_ACCENT_POLICY 19
#define TINT_RGB 0x1C1816
#define TINT_ALPHA 0xB4
#define SLIDE_MS 380.0
#define TAU 6.2831853f
#define MAX_SCREENS 8

struct screen {
    HWND wnd;
    HDC basedc, facedc, facebg, clockbg, pillbg;
    HBITMAP basebmp, facebmp, facebgbmp, clockbmp, pillbmp;
    void *facebits;
    HFONT title_f, sub_f, count_f, small_f, btn_f;
    RECT face_rc, clock_rc, pill_rc, skip_rc, lock_rc;
    SYSTEMTIME last_clock;
    int mx, my, mw, mh, dpi;
    int cellw, colonw, cellh, ring_r, ring_t;
    int hot;
};

static screen scr[MAX_SCREENS];
static int nscr;
static HHOOK khook;
static swca_fn swca;
static int acrylic;
static int shown_sec, total_sec;
static int last_off = -9999;
static float last_deg = -1;
static double freq, show_t, anim_t, sec_t, esc_t;
static wchar_t cur[8], old_[8];

static int S(screen *s, int v)
{
    return MulDiv(v, s->dpi, 96);
}

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / freq;
}

static BOOL set_acrylic(HWND w, int alpha)
{
    accent_policy ap = {ACCENT_ACRYLIC, 0, (DWORD)alpha << 24 | TINT_RGB, 0};
    wca_data d = {WCA_ACCENT_POLICY, &ap, sizeof ap};
    return swca(w, &d);
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

static void draw_face(screen *s, float frac, int off)
{
    int fw = s->face_rc.right - s->face_rc.left, fh = s->face_rc.bottom - s->face_rc.top;
    BitBlt(s->facedc, 0, 0, fw, fh, s->facebg, 0, 0, SRCCOPY);
    GdiFlush();
    paint_ring(s->facebits, fw, fh, fw / 2.0f, fh / 2.0f, (float)s->ring_r,
        (float)s->ring_t, frac, RGB(255, 255, 255), 0.10f, APP_ACCENT);

    SelectObject(s->facedc, s->count_f);
    SetTextColor(s->facedc, RGB(236, 240, 246));
    int total = s->cellw * 4 + s->colonw;
    int x = (fw - total) / 2;
    int top = (fh - s->cellh) / 2;
    for (int i = 0; cur[i]; i++) {
        int cw = cur[i] == L':' ? s->colonw : s->cellw;
        RECT cell = {x, top, x + cw, top + s->cellh};
        wchar_t s1[2] = {old_[i], 0}, s2[2] = {cur[i], 0};
        SaveDC(s->facedc);
        IntersectClipRect(s->facedc, cell.left, cell.top, cell.right, cell.bottom);
        if (off && old_[i] && old_[i] != cur[i]) {
            RECT a = cell, b = cell;
            OffsetRect(&a, 0, -off);
            OffsetRect(&b, 0, s->cellh - off);
            DrawTextW(s->facedc, s1, 1, &a,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            DrawTextW(s->facedc, s2, 1, &b,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        } else {
            DrawTextW(s->facedc, s2, 1, &cell,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        RestoreDC(s->facedc, -1);
        x += cw;
    }

    BitBlt(s->basedc, s->face_rc.left, s->face_rc.top, fw, fh, s->facedc, 0, 0, SRCCOPY);
    if (s->wnd)
        InvalidateRect(s->wnd, &s->face_rc, FALSE);
}

static void draw_clock(screen *s)
{
    BitBlt(s->basedc, s->clock_rc.left, s->clock_rc.top,
        s->clock_rc.right - s->clock_rc.left, s->clock_rc.bottom - s->clock_rc.top,
        s->clockbg, 0, 0, SRCCOPY);
    GetLocalTime(&s->last_clock);
    wchar_t t[16];
    GetTimeFormatEx(0, TIME_NOSECONDS, &s->last_clock, 0, t, 16);
    SelectObject(s->basedc, s->small_f);
    SetTextColor(s->basedc, RGB(150, 156, 166));
    RECT r = s->clock_rc;
    DrawTextW(s->basedc, t, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (s->wnd)
        InvalidateRect(s->wnd, &s->clock_rc, FALSE);
}

static void draw_pills(screen *s)
{
    BitBlt(s->basedc, s->pill_rc.left, s->pill_rc.top,
        s->pill_rc.right - s->pill_rc.left, s->pill_rc.bottom - s->pill_rc.top,
        s->pillbg, 0, 0, SRCCOPY);
    int rad = (s->skip_rc.bottom - s->skip_rc.top) / 2;
    paint_round(s->basedc, s->skip_rc.left, s->skip_rc.top,
        s->skip_rc.right - s->skip_rc.left, s->skip_rc.bottom - s->skip_rc.top, rad,
        RGB(255, 255, 255), s->hot == 1 ? 60 : 30);
    paint_round(s->basedc, s->lock_rc.left, s->lock_rc.top,
        s->lock_rc.right - s->lock_rc.left, s->lock_rc.bottom - s->lock_rc.top, rad,
        RGB(255, 255, 255), s->hot == 2 ? 60 : 30);
    SelectObject(s->basedc, s->btn_f);
    SetTextColor(s->basedc, s->hot == 1 ? RGB(250, 251, 253) : RGB(220, 226, 234));
    RECT r1 = s->skip_rc;
    DrawTextW(s->basedc, str(STR_SKIP_BREAK), -1, &r1,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(s->basedc, s->hot == 2 ? RGB(250, 251, 253) : RGB(220, 226, 234));
    RECT r2 = s->lock_rc;
    DrawTextW(s->basedc, str(STR_LOCK), -1, &r2,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (s->wnd)
        InvalidateRect(s->wnd, &s->pill_rc, FALSE);
}

static void build_bg(screen *s)
{
    if (acrylic) {
        RECT r = {0, 0, s->mw, s->mh};
        FillRect(s->basedc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }

    HDC sdc = GetDC(0);
    BitBlt(s->basedc, 0, 0, s->mw, s->mh, sdc, s->mx, s->my, SRCCOPY);
    ReleaseDC(0, sdc);

    int bw = s->mw / 12, bh2 = s->mh / 12;
    BITMAPV5HEADER bhh = {sizeof bhh};
    bhh.bV5Width = bw;
    bhh.bV5Height = -bh2;
    bhh.bV5Planes = 1;
    bhh.bV5BitCount = 32;
    bhh.bV5Compression = BI_RGB;
    void *bits;
    HDC smalldc = CreateCompatibleDC(s->basedc);
    HBITMAP smallbmp = CreateDIBSection(0, (BITMAPINFO *)&bhh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ os = SelectObject(smalldc, smallbmp);
    SetStretchBltMode(smalldc, HALFTONE);
    SetBrushOrgEx(smalldc, 0, 0, 0);
    StretchBlt(smalldc, 0, 0, bw, bh2, s->basedc, 0, 0, s->mw, s->mh, SRCCOPY);
    GdiFlush();

    DWORD *tmp = (DWORD *)HeapAlloc(GetProcessHeap(), 0, bw * bh2 * 4);
    blur_pass((DWORD *)bits, tmp, bw, bh2, 1);
    blur_pass(tmp, (DWORD *)bits, bw, bh2, 0);
    blur_pass((DWORD *)bits, tmp, bw, bh2, 1);
    blur_pass(tmp, (DWORD *)bits, bw, bh2, 0);
    HeapFree(GetProcessHeap(), 0, tmp);
    DWORD *p = (DWORD *)bits;
    for (int i = 0; i < bw * bh2; i++, p++) {
        int r = ((*p >> 16) & 255) * 160 / 255;
        int g = ((*p >> 8) & 255) * 168 / 255;
        int b = (*p & 255) * 190 / 255 + 12;
        if (b > 255)
            b = 255;
        *p = (DWORD)r << 16 | (DWORD)g << 8 | b;
    }

    SetStretchBltMode(s->basedc, HALFTONE);
    SetBrushOrgEx(s->basedc, 0, 0, 0);
    StretchBlt(s->basedc, 0, 0, s->mw, s->mh, smalldc, 0, 0, bw, bh2, SRCCOPY);
    SelectObject(smalldc, os);
    DeleteObject(smallbmp);
    DeleteDC(smalldc);
}

static void build_base(screen *s)
{
    build_bg(s);
    SetBkMode(s->basedc, TRANSPARENT);

    int ty = s->face_rc.top - S(s, 94), sy = s->face_rc.top - S(s, 42);
    SelectObject(s->basedc, s->title_f);
    SetTextColor(s->basedc, RGB(242, 245, 250));
    RECT tr = {0, ty - S(s, 34), s->mw, ty + S(s, 34)};
    DrawTextW(s->basedc, str(STR_OVERLAY_TITLE), -1, &tr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(s->basedc, s->sub_f);
    SetTextColor(s->basedc, RGB(182, 190, 202));
    RECT sr = {0, sy - S(s, 16), s->mw, sy + S(s, 16)};
    DrawTextW(s->basedc, str(STR_LOOK_AWAY), -1, &sr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(s->basedc, s->small_f);
    SetTextColor(s->basedc, RGB(124, 130, 141));
    RECT hr = {0, s->pill_rc.bottom + S(s, 10), s->mw, s->pill_rc.bottom + S(s, 32)};
    DrawTextW(s->basedc, str(STR_ESC_HINT), -1, &hr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    BitBlt(s->facebg, 0, 0, s->face_rc.right - s->face_rc.left,
        s->face_rc.bottom - s->face_rc.top, s->basedc, s->face_rc.left, s->face_rc.top,
        SRCCOPY);
    BitBlt(s->clockbg, 0, 0, s->clock_rc.right - s->clock_rc.left,
        s->clock_rc.bottom - s->clock_rc.top, s->basedc, s->clock_rc.left,
        s->clock_rc.top, SRCCOPY);
    BitBlt(s->pillbg, 0, 0, s->pill_rc.right - s->pill_rc.left,
        s->pill_rc.bottom - s->pill_rc.top, s->basedc, s->pill_rc.left, s->pill_rc.top,
        SRCCOPY);

    draw_clock(s);
    draw_pills(s);
    draw_face(s, 1, 0);
}

static LRESULT CALLBACK escproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && nscr && wp == WM_KEYDOWN &&
        ((KBDLLHOOKSTRUCT *)lp)->vkCode == VK_ESCAPE) {
        double now = now_ms();
        if (now - esc_t < 1200)
            timer_skip();
        else
            esc_t = now;
        return 1;
    }
    return CallNextHookEx(0, c, wp, lp);
}

static void free_screen(screen *s)
{
    DeleteDC(s->basedc);
    DeleteDC(s->facedc);
    DeleteDC(s->facebg);
    DeleteDC(s->clockbg);
    DeleteDC(s->pillbg);
    DeleteObject(s->basebmp);
    DeleteObject(s->facebmp);
    DeleteObject(s->facebgbmp);
    DeleteObject(s->clockbmp);
    DeleteObject(s->pillbmp);
    DeleteObject(s->title_f);
    DeleteObject(s->sub_f);
    DeleteObject(s->count_f);
    DeleteObject(s->small_f);
    DeleteObject(s->btn_f);
}

static void tick(double t)
{
    if (acrylic && t - show_t < 300) {
        float q = (float)((t - show_t) / 260.0);
        if (q > 1) q = 1;
        q = q * q * (3 - 2 * q);
        int a = (int)(TINT_ALPHA * q);
        if (a < 8) a = 8;
        for (int i = 0; i < nscr; i++)
            set_acrylic(scr[i].wnd, a);
    }

    int sec = timer_seconds_left();
    if (sec != shown_sec) {
        lstrcpyW(old_, cur);
        wsprintfW(cur, L"%02d:%02d", sec / 60, sec % 60);
        shown_sec = sec;
        sec_t = anim_t = t;
    }

    float sub = (float)((t - sec_t) / 1000.0);
    if (sub > 1) sub = 1;
    float frac = (shown_sec - sub) / total_sec;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    float e = (float)((t - anim_t) / SLIDE_MS);
    float deg = frac * 360;
    if (fabsf(deg - last_deg) < 0.2f && e >= 1 && last_off == 0)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    for (int i = 0; i < nscr; i++) {
        screen *s = &scr[i];
        int off = 0;
        if (e < 1) {
            float k = 1 - e;
            off = (int)((1 - k * k * k) * s->cellh);
        }
        if (i == 0)
            last_off = off;
        draw_face(s, frac, off);
        if (st.wMinute != s->last_clock.wMinute)
            draw_clock(s);
    }
    last_deg = deg;
}

static LRESULT CALLBACK overlayproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    screen *s = (screen *)GetWindowLongPtrW(m, GWLP_USERDATA);
    if (!s)
        return DefWindowProcW(m, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m, &ps);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
            s->basedc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(m, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        BitBlt((HDC)wp, 0, 0, s->mw, s->mh, s->basedc, 0, 0, SRCCOPY);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        tick(now_ms());
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int over = PtInRect(&s->skip_rc, pt) ? 1 : PtInRect(&s->lock_rc, pt) ? 2 : 0;
        if (over != s->hot) {
            s->hot = over;
            draw_pills(s);
        }
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(0, s->hot ? IDC_HAND : IDC_ARROW));
        return 1;
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&s->skip_rc, pt))
            timer_skip();
        else if (PtInRect(&s->lock_rc, pt))
            LockWorkStation();
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

static BOOL CALLBACK add_monitor(HMONITOR mon, HDC, LPRECT, LPARAM)
{
    if (nscr >= MAX_SCREENS)
        return FALSE;
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(mon, &mi);
    screen *s = &scr[nscr++];
    s->mx = mi.rcMonitor.left;
    s->my = mi.rcMonitor.top;
    s->mw = mi.rcMonitor.right - s->mx;
    s->mh = mi.rcMonitor.bottom - s->my;
    return TRUE;
}

static void layout(screen *s)
{
    HDC wdc = GetDC(s->wnd);
    s->basedc = CreateCompatibleDC(wdc);
    s->basebmp = CreateCompatibleBitmap(wdc, s->mw, s->mh);
    SelectObject(s->basedc, s->basebmp);

    HGDIOBJ of = SelectObject(wdc, s->count_f);
    SIZE dz, cz;
    GetTextExtentPoint32W(wdc, L"0", 1, &dz);
    GetTextExtentPoint32W(wdc, L":", 1, &cz);
    s->cellw = dz.cx + S(s, 3);
    s->colonw = cz.cx + S(s, 2);
    s->cellh = dz.cy;
    SelectObject(wdc, s->btn_f);
    SIZE bs1, bs2;
    GetTextExtentPoint32W(wdc, str(STR_SKIP_BREAK), lstrlenW(str(STR_SKIP_BREAK)), &bs1);
    GetTextExtentPoint32W(wdc, str(STR_LOCK), lstrlenW(str(STR_LOCK)), &bs2);
    SelectObject(wdc, of);

    s->ring_r = s->mh * 15 / 100;
    if (s->ring_r > S(s, 150))
        s->ring_r = S(s, 150);
    if (s->ring_r < s->cellw * 3)
        s->ring_r = s->cellw * 3;
    s->ring_t = S(s, 5);

    int cx = s->mw / 2, cy = s->mh * 50 / 100;
    int fr = s->ring_r + s->ring_t + S(s, 4);
    s->face_rc.left = cx - fr;
    s->face_rc.right = cx + fr;
    s->face_rc.top = cy - fr;
    s->face_rc.bottom = cy + fr;

    s->clock_rc.left = cx - S(s, 90);
    s->clock_rc.right = cx + S(s, 90);
    s->clock_rc.top = S(s, 38);
    s->clock_rc.bottom = S(s, 64);

    int ph = S(s, 38), py = s->face_rc.bottom + S(s, 52);
    int w1 = bs1.cx + S(s, 44), w2 = bs2.cx + S(s, 44), gap = S(s, 14);
    s->skip_rc.left = cx - (w1 + w2 + gap) / 2;
    s->skip_rc.right = s->skip_rc.left + w1;
    s->skip_rc.top = py;
    s->skip_rc.bottom = py + ph;
    s->lock_rc.left = s->skip_rc.right + gap;
    s->lock_rc.right = s->lock_rc.left + w2;
    s->lock_rc.top = py;
    s->lock_rc.bottom = py + ph;
    s->pill_rc.left = s->skip_rc.left - S(s, 8);
    s->pill_rc.right = s->lock_rc.right + S(s, 8);
    s->pill_rc.top = py - S(s, 6);
    s->pill_rc.bottom = py + ph + S(s, 6);

    int fw = s->face_rc.right - s->face_rc.left, fh = s->face_rc.bottom - s->face_rc.top;
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = fw;
    bh.bV5Height = -fh;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    s->facedc = CreateCompatibleDC(wdc);
    s->facebmp = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &s->facebits, 0, 0);
    SelectObject(s->facedc, s->facebmp);
    SetBkMode(s->facedc, TRANSPARENT);
    s->facebg = CreateCompatibleDC(wdc);
    s->facebgbmp = CreateCompatibleBitmap(wdc, fw, fh);
    SelectObject(s->facebg, s->facebgbmp);
    s->clockbg = CreateCompatibleDC(wdc);
    s->clockbmp = CreateCompatibleBitmap(wdc, s->clock_rc.right - s->clock_rc.left,
        s->clock_rc.bottom - s->clock_rc.top);
    SelectObject(s->clockbg, s->clockbmp);
    s->pillbg = CreateCompatibleDC(wdc);
    s->pillbmp = CreateCompatibleBitmap(wdc, s->pill_rc.right - s->pill_rc.left,
        s->pill_rc.bottom - s->pill_rc.top);
    SelectObject(s->pillbg, s->pillbmp);
    ReleaseDC(s->wnd, wdc);
}

void overlay_break(int on)
{
    if (!on) {
        if (!nscr)
            return;
        if (khook)
            UnhookWindowsHookEx(khook), khook = 0;
        KillTimer(scr[0].wnd, 1);
        timeEndPeriod(1);
        for (int i = 0; i < nscr; i++) {
            if (!acrylic)
                AnimateWindow(scr[i].wnd, 150, AW_HIDE | AW_BLEND);
            SetWindowLongPtrW(scr[i].wnd, GWLP_USERDATA, 0);
            DestroyWindow(scr[i].wnd);
            free_screen(&scr[i]);
        }
        nscr = 0;
        return;
    }
    if (nscr)
        return;

    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = overlayproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_overlay";
        RegisterClassW(&wc);
        reg = 1;
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;

    EnumDisplayMonitors(0, 0, add_monitor, 0);
    if (!nscr)
        return;

    for (int i = 0; i < nscr; i++) {
        screen *s = &scr[i];
        s->wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            APP_NAME L"_overlay", 0, WS_POPUP, s->mx, s->my, s->mw, s->mh, 0, 0, inst, 0);
        SetWindowLongPtrW(s->wnd, GWLP_USERDATA, (LONG_PTR)s);
        s->dpi = GetDpiForWindow(s->wnd);
        s->hot = 0;
        s->title_f = CreateFontW(-MulDiv(34, s->dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
        s->sub_f = CreateFontW(-MulDiv(14, s->dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
        s->count_f = CreateFontW(-MulDiv(48, s->dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
        s->small_f = CreateFontW(-MulDiv(10, s->dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        s->btn_f = CreateFontW(-MulDiv(11, s->dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        layout(s);
    }

    int sec = timer_seconds_left();
    total_sec = sec > 0 ? sec : 1;
    wsprintfW(cur, L"%02d:%02d", sec / 60, sec % 60);
    lstrcpyW(old_, cur);
    shown_sec = sec;
    esc_t = 0;
    last_off = -9999;
    last_deg = -1;
    show_t = sec_t = anim_t = now_ms();

    if (!swca)
        swca = (swca_fn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
            "SetWindowCompositionAttribute");
    acrylic = swca && set_acrylic(scr[0].wnd, 8);

    for (int i = 0; i < nscr; i++) {
        screen *s = &scr[i];
        if (acrylic && i)
            set_acrylic(s->wnd, 8);
        build_base(s);
        DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(s->wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
        SetWindowPos(s->wnd, HWND_TOPMOST, s->mx, s->my, s->mw, s->mh,
            SWP_NOACTIVATE | SWP_HIDEWINDOW);
        if (acrylic)
            SetWindowPos(s->wnd, HWND_TOPMOST, s->mx, s->my, s->mw, s->mh,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else
            AnimateWindow(s->wnd, 220, AW_BLEND);
    }

    timeBeginPeriod(1);
    SetTimer(scr[0].wnd, 1, 8, 0);
    khook = SetWindowsHookExW(WH_KEYBOARD_LL, escproc, 0, 0);
}
