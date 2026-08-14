#include "app/app.h"
#include "core/timer.h"
#include "notify/overlay.h"
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

static HWND wnd;
static HHOOK khook;
static swca_fn swca;
static int acrylic;

static HDC basedc, facedc, facebg, clockbg, pillbg;
static HBITMAP basebmp, facebmp, facebgbmp, clockbmp, pillbmp;
static void *facebits;
static HFONT title_f, sub_f, count_f, small_f, btn_f;
static int dpi, mx, my, mw, mh;
static int cellw, colonw, cellh, ring_r, ring_t;
static int shown_sec, total_sec, hot;
static int last_off = -9999;
static float last_deg = -1;
static double freq, show_t, anim_t, sec_t, esc_t;
static wchar_t cur[8], old_[8];
static RECT face_rc, clock_rc, pill_rc, skip_rc, lock_rc;
static SYSTEMTIME last_clock;

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

static BOOL set_acrylic(int alpha)
{
    accent_policy ap = {ACCENT_ACRYLIC, 0, (DWORD)alpha << 24 | TINT_RGB, 0};
    wca_data d = {WCA_ACCENT_POLICY, &ap, sizeof ap};
    return swca(wnd, &d);
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

static void blend_px(DWORD *p, COLORREF c, float a)
{
    if (a <= 0)
        return;
    if (a > 1) a = 1;
    int br = (*p >> 16) & 255, bg = (*p >> 8) & 255, bb = *p & 255;
    int r = (int)(br + (GetRValue(c) - br) * a);
    int g = (int)(bg + (GetGValue(c) - bg) * a);
    int b = (int)(bb + (GetBValue(c) - bb) * a);
    *p = (DWORD)r << 16 | (DWORD)g << 8 | b;
}

static void draw_ring(float frac)
{
    int fw = face_rc.right - face_rc.left, fh = face_rc.bottom - face_rc.top;
    float cx = fw / 2.0f, cy = fh / 2.0f;
    float mid = (float)ring_r, half = ring_t / 2.0f;
    float lo = mid - half - 1, hi = mid + half + 1;
    float span = frac * TAU;
    DWORD *bits = (DWORD *)facebits;

    for (int y = 0; y < fh; y++) {
        float dy = y + 0.5f - cy;
        if (dy < -hi || dy > hi)
            continue;
        DWORD *row = bits + y * fw;
        for (int x = 0; x < fw; x++) {
            float dx = x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 < lo * lo || d2 > hi * hi)
                continue;
            float d = sqrtf(d2);
            float cov = half + 0.5f - fabsf(d - mid);
            if (cov <= 0)
                continue;
            if (cov > 1) cov = 1;
            blend_px(row + x, RGB(255, 255, 255), cov * 0.10f);
            if (frac <= 0)
                continue;
            float a = atan2f(dx, -dy);
            if (a < 0)
                a += TAU;
            float lead = frac >= 1 ? 1 : (span - a) * d;
            float tail = frac >= 1 ? 1 : a * d;
            float e = lead < tail ? lead : tail;
            if (e <= 0)
                continue;
            if (e > 1) e = 1;
            blend_px(row + x, APP_ACCENT, cov * e);
        }
    }
}

static void draw_face(float frac, int off)
{
    int fw = face_rc.right - face_rc.left, fh = face_rc.bottom - face_rc.top;
    BitBlt(facedc, 0, 0, fw, fh, facebg, 0, 0, SRCCOPY);
    GdiFlush();
    draw_ring(frac);

    SelectObject(facedc, count_f);
    SetTextColor(facedc, RGB(236, 240, 246));
    int total = cellw * 4 + colonw;
    int x = (fw - total) / 2;
    int top = (fh - cellh) / 2;
    for (int i = 0; cur[i]; i++) {
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
            DrawTextW(facedc, s2, 1, &cell, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        RestoreDC(facedc, -1);
        x += cw;
    }

    BitBlt(basedc, face_rc.left, face_rc.top, fw, fh, facedc, 0, 0, SRCCOPY);
    if (wnd)
        InvalidateRect(wnd, &face_rc, FALSE);
}

static void draw_clock(void)
{
    BitBlt(basedc, clock_rc.left, clock_rc.top, clock_rc.right - clock_rc.left,
        clock_rc.bottom - clock_rc.top, clockbg, 0, 0, SRCCOPY);
    GetLocalTime(&last_clock);
    wchar_t t[16];
    GetTimeFormatEx(0, TIME_NOSECONDS, &last_clock, 0, t, 16);
    SelectObject(basedc, small_f);
    SetTextColor(basedc, RGB(150, 156, 166));
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
        skip_rc.bottom - skip_rc.top, rad, RGB(255, 255, 255), hot == 1 ? 60 : 30);
    fill_round(basedc, lock_rc.left, lock_rc.top, lock_rc.right - lock_rc.left,
        lock_rc.bottom - lock_rc.top, rad, RGB(255, 255, 255), hot == 2 ? 60 : 30);
    SelectObject(basedc, btn_f);
    SetTextColor(basedc, hot == 1 ? RGB(250, 251, 253) : RGB(226, 231, 238));
    RECT r1 = skip_rc;
    DrawTextW(basedc, str(STR_SKIP_BREAK), -1, &r1,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(basedc, hot == 2 ? RGB(250, 251, 253) : RGB(226, 231, 238));
    RECT r2 = lock_rc;
    DrawTextW(basedc, str(STR_LOCK), -1, &r2,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (wnd)
        InvalidateRect(wnd, &pill_rc, FALSE);
}

static void build_bg(void)
{
    if (acrylic) {
        RECT r = {0, 0, mw, mh};
        FillRect(basedc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }

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
        int r = ((*p >> 16) & 255) * 160 / 255;
        int g = ((*p >> 8) & 255) * 168 / 255;
        int b = (*p & 255) * 190 / 255 + 12;
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
}

static void build_base(void)
{
    build_bg();
    SetBkMode(basedc, TRANSPARENT);

    SelectObject(basedc, title_f);
    SetTextColor(basedc, RGB(244, 246, 250));
    RECT tr = {0, mh * 25 / 100 - S(36), mw, mh * 25 / 100 + S(36)};
    DrawTextW(basedc, str(STR_OVERLAY_TITLE), -1, &tr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(basedc, sub_f);
    SetTextColor(basedc, RGB(168, 176, 188));
    RECT sr = {0, mh * 32 / 100 - S(16), mw, mh * 32 / 100 + S(16)};
    DrawTextW(basedc, str(STR_LOOK_AWAY), -1, &sr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(basedc, small_f);
    SetTextColor(basedc, RGB(120, 126, 136));
    RECT hr = {0, pill_rc.bottom + S(12), mw, pill_rc.bottom + S(36)};
    DrawTextW(basedc, str(STR_ESC_HINT), -1, &hr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    BitBlt(facebg, 0, 0, face_rc.right - face_rc.left, face_rc.bottom - face_rc.top,
        basedc, face_rc.left, face_rc.top, SRCCOPY);
    BitBlt(clockbg, 0, 0, clock_rc.right - clock_rc.left,
        clock_rc.bottom - clock_rc.top, basedc, clock_rc.left, clock_rc.top, SRCCOPY);
    BitBlt(pillbg, 0, 0, pill_rc.right - pill_rc.left,
        pill_rc.bottom - pill_rc.top, basedc, pill_rc.left, pill_rc.top, SRCCOPY);

    draw_clock();
    draw_pills();
    draw_face(1, 0);
}

static LRESULT CALLBACK escproc(int c, WPARAM wp, LPARAM lp)
{
    if (c == HC_ACTION && wnd && wp == WM_KEYDOWN &&
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
        double t = now_ms();
        if (acrylic && t - show_t < 300) {
            float q = (float)((t - show_t) / 260.0);
            if (q > 1) q = 1;
            q = q * q * (3 - 2 * q);
            int a = (int)(TINT_ALPHA * q);
            set_acrylic(a < 8 ? 8 : a);
        }

        int s = timer_seconds_left();
        if (s != shown_sec) {
            lstrcpyW(old_, cur);
            wsprintfW(cur, L"%02d:%02d", s / 60, s % 60);
            shown_sec = s;
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
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(0, hot ? IDC_HAND : IDC_ARROW));
        return 1;
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
        DeleteDC(facedc);
        DeleteDC(facebg);
        DeleteDC(clockbg);
        DeleteDC(pillbg);
        DeleteObject(basebmp);
        DeleteObject(facebmp);
        DeleteObject(facebgbmp);
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
        timeEndPeriod(1);
        if (!acrylic)
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
        RegisterClassW(&wc);
        reg = 1;
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;

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
    title_f = CreateFontW(-MulDiv(32, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    sub_f = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    count_f = CreateFontW(-MulDiv(46, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
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
    cellw = dz.cx + S(3);
    colonw = cz.cx + S(2);
    cellh = dz.cy;
    SelectObject(wdc, btn_f);
    SIZE bs1, bs2;
    GetTextExtentPoint32W(wdc, str(STR_SKIP_BREAK), lstrlenW(str(STR_SKIP_BREAK)), &bs1);
    GetTextExtentPoint32W(wdc, str(STR_LOCK), lstrlenW(str(STR_LOCK)), &bs2);
    SelectObject(wdc, of);

    ring_r = mh * 15 / 100;
    if (ring_r > S(150))
        ring_r = S(150);
    if (ring_r < cellw * 3)
        ring_r = cellw * 3;
    ring_t = S(5);

    int cx = mw / 2, cy = mh * 53 / 100;
    int fr = ring_r + ring_t + S(4);
    face_rc.left = cx - fr;
    face_rc.right = cx + fr;
    face_rc.top = cy - fr;
    face_rc.bottom = cy + fr;

    clock_rc.left = cx - S(90);
    clock_rc.right = cx + S(90);
    clock_rc.top = S(38);
    clock_rc.bottom = S(64);

    int ph = S(38), py = mh * 82 / 100;
    int w1 = bs1.cx + S(44), w2 = bs2.cx + S(44), gap = S(14);
    skip_rc.left = cx - (w1 + w2 + gap) / 2;
    skip_rc.right = skip_rc.left + w1;
    skip_rc.top = py;
    skip_rc.bottom = py + ph;
    lock_rc.left = skip_rc.right + gap;
    lock_rc.right = lock_rc.left + w2;
    lock_rc.top = py;
    lock_rc.bottom = py + ph;
    pill_rc.left = skip_rc.left - S(8);
    pill_rc.right = lock_rc.right + S(8);
    pill_rc.top = py - S(6);
    pill_rc.bottom = py + ph + S(6);

    int fw = face_rc.right - face_rc.left, fh = face_rc.bottom - face_rc.top;
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = fw;
    bh.bV5Height = -fh;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    facedc = CreateCompatibleDC(wdc);
    facebmp = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &facebits, 0, 0);
    SelectObject(facedc, facebmp);
    SetBkMode(facedc, TRANSPARENT);
    facebg = CreateCompatibleDC(wdc);
    facebgbmp = CreateCompatibleBitmap(wdc, fw, fh);
    SelectObject(facebg, facebgbmp);
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
    total_sec = s > 0 ? s : 1;
    wsprintfW(cur, L"%02d:%02d", s / 60, s % 60);
    lstrcpyW(old_, cur);
    shown_sec = s;
    hot = 0;
    esc_t = 0;
    last_off = -9999;
    last_deg = -1;
    show_t = sec_t = anim_t = now_ms();

    if (!swca)
        swca = (swca_fn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
            "SetWindowCompositionAttribute");
    acrylic = swca && set_acrylic(8);

    build_base();

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    SetWindowPos(wnd, HWND_TOPMOST, mx, my, mw, mh, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    if (acrylic)
        SetWindowPos(wnd, HWND_TOPMOST, mx, my, mw, mh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    else
        AnimateWindow(wnd, 220, AW_BLEND);
    timeBeginPeriod(1);
    SetTimer(wnd, 1, 8, 0);
    khook = SetWindowsHookExW(WH_KEYBOARD_LL, escproc, 0, 0);
}
