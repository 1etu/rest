#include "app/app.h"
#include "app/resource.h"
#include "notify/hello.h"
#include "ui/paint.h"
#include "loc/strings.h"
#include <dwmapi.h>

#define ALERT_RED RGB(232, 76, 68)

static HWND wnd;
static HFONT title_f, body_f, mark_f;
static int dpi, w, h, kind;
static COLORREF col_bg, col_title, col_body;

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

static void paint(HDC dc)
{
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(mem, bmp);

    RECT all = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(col_bg);
    FillRect(mem, &all, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    int box = S(46);
    int bx = S(18), by = (h - box) / 2;
    if (kind == HELLO_RUNNING) {
        BITMAPV5HEADER bh = {sizeof bh};
        bh.bV5Width = box;
        bh.bV5Height = -box;
        bh.bV5Planes = 1;
        bh.bV5BitCount = 32;
        bh.bV5Compression = BI_RGB;
        void *bits;
        HDC rd = CreateCompatibleDC(dc);
        HBITMAP rb = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
        HGDIOBJ orb = SelectObject(rd, rb);
        RECT rr = {0, 0, box, box};
        HBRUSH fb = CreateSolidBrush(col_bg);
        FillRect(rd, &rr, fb);
        DeleteObject(fb);
        GdiFlush();
        paint_ring(bits, box, box, box / 2.0f, box / 2.0f, box * 0.40f, S(3) + 0.0f, 1.0f,
            col_bg, 0.0f, ALERT_RED);
        BitBlt(mem, bx, by, box, box, rd, 0, 0, SRCCOPY);
        SelectObject(rd, orb);
        DeleteObject(rb);
        DeleteDC(rd);
        SelectObject(mem, mark_f);
        SetTextColor(mem, ALERT_RED);
        RECT mr = {bx, by, bx + box, by + box};
        DrawTextW(mem, L"!", -1, &mr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    } else {
        HICON ic = (HICON)LoadImageW(GetModuleHandleW(0), MAKEINTRESOURCEW(IDI_TRAY),
            IMAGE_ICON, box, box, LR_DEFAULTCOLOR);
        if (ic) {
            HDC id = CreateCompatibleDC(dc);
            BITMAPV5HEADER bh = {sizeof bh};
            bh.bV5Width = box;
            bh.bV5Height = -box;
            bh.bV5Planes = 1;
            bh.bV5BitCount = 32;
            bh.bV5Compression = BI_RGB;
            void *bits;
            HBITMAP ib = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
            HGDIOBJ oib = SelectObject(id, ib);
            DrawIconEx(id, 0, 0, ic, box, box, 0, 0, DI_NORMAL);
            GdiFlush();
            COLORREF c = APP_ACCENT;
            int rr2 = GetRValue(c), gg = GetGValue(c), bb2 = GetBValue(c);
            DWORD *p = (DWORD *)bits;
            for (int i = 0; i < box * box; i++, p++) {
                int a = *p >> 24;
                *p = (DWORD)a << 24 | (DWORD)(rr2 * a / 255) << 16 |
                    (DWORD)(gg * a / 255) << 8 | (DWORD)(bb2 * a / 255);
            }
            GdiFlush();
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(mem, bx, by, box, box, id, 0, 0, box, box, bf);
            SelectObject(id, oib);
            DeleteObject(ib);
            DeleteDC(id);
            DestroyIcon(ic);
        }
    }

    int tx = bx + box + S(14);
    SelectObject(mem, title_f);
    SetTextColor(mem, col_title);
    RECT tr = {tx, S(13), w - S(16), S(35)};
    DrawTextW(mem, APP_NAME, -1, &tr, DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem, body_f);
    SetTextColor(mem, col_body);
    RECT br = {tx, S(35), w - S(16), h - S(11)};
    DrawTextW(mem, str(kind == HELLO_RUNNING ? STR_ALREADY : STR_HELLO), -1, &br,
        DT_WORDBREAK | DT_NOPREFIX);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void dismiss(void)
{
    if (!wnd)
        return;
    HWND t = wnd;
    wnd = 0;
    KillTimer(t, 1);
    AnimateWindow(t, 150, AW_HIDE | AW_BLEND);
    DestroyWindow(t);
}

static LRESULT CALLBACK helloproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        paint(BeginPaint(m, &ps));
        EndPaint(m, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint((HDC)wp);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
    case WM_LBUTTONUP:
        dismiss();
        if (kind == HELLO_RUNNING)
            PostQuitMessage(0);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        DeleteObject(title_f);
        DeleteObject(body_f);
        DeleteObject(mark_f);
        title_f = body_f = mark_f = 0;
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void hello_show(HWND o, int k)
{
    kind = k;
    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = helloproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_hello";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    if (light_mode()) {
        col_bg = RGB(250, 250, 250);
        col_title = RGB(26, 26, 26);
        col_body = RGB(112, 112, 112);
    } else {
        col_bg = RGB(38, 38, 38);
        col_title = RGB(240, 240, 240);
        col_body = RGB(160, 160, 160);
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_hello", 0, WS_POPUP, 0, 0, 0, 0, o, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    title_f = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    body_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    mark_f = CreateFontW(-MulDiv(17, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    w = S(348);
    int tx = S(18) + S(46) + S(14);
    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, body_f);
    RECT mr = {0, 0, w - tx - S(16), 0};
    DrawTextW(dc, str(kind == HELLO_RUNNING ? STR_ALREADY : STR_HELLO), -1, &mr,
        DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, of);
    ReleaseDC(wnd, dc);
    h = S(35) + mr.bottom + S(13);
    if (h < S(78))
        h = S(78);

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - w - S(16);
    int y = wa.bottom - h - S(16);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    AnimateWindow(wnd, 150, AW_BLEND);
    SetTimer(wnd, 1, kind == HELLO_RUNNING ? 3600 : 6000, 0);
}
