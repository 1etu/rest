#include "app/app.h"
#include "notify/hello.h"
#include "loc/strings.h"
#include <dwmapi.h>
#include <math.h>

static HWND wnd;
static HFONT title_f, body_f;
static int dpi, w, h;
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

static void ring(DWORD *bits, int ox, int oy, int box)
{
    float c = box / 2.0f, r = box * 0.40f, t = box * 0.16f;
    for (int y = 0; y < box; y++)
        for (int x = 0; x < box; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a = r - d, b = d - (r - t);
            if (b < a) a = b;
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            if (a > 0) {
                COLORREF ac = APP_ACCENT;
                int br = GetRValue(col_bg), bg = GetGValue(col_bg), bb = GetBValue(col_bg);
                int ar = GetRValue(ac), ag = GetGValue(ac), ab = GetBValue(ac);
                bits[(oy + y) * w + ox + x] =
                    (DWORD)(br + (ar - br) * a) << 16 |
                    (DWORD)(bg + (ag - bg) * a) << 8 |
                    (DWORD)(bb + (ab - bb) * a);
            }
        }
}

static void paint(HDC dc)
{
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = w;
    bh.bV5Height = -h;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ ob = SelectObject(mem, bmp);

    DWORD bg = GetRValue(col_bg) << 16 | GetGValue(col_bg) << 8 | GetBValue(col_bg);
    DWORD *p = (DWORD *)bits;
    for (int i = 0; i < w * h; i++)
        p[i] = bg;

    int box = S(48);
    ring((DWORD *)bits, S(16), (h - box) / 2, box);

    SetBkMode(mem, TRANSPARENT);
    int tx = S(16) + box + S(14);
    SelectObject(mem, title_f);
    SetTextColor(mem, col_title);
    RECT tr = {tx, S(13), w - S(16), h};
    DrawTextW(mem, APP_NAME, -1, &tr, DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem, body_f);
    SetTextColor(mem, col_body);
    RECT br = {tx, S(35), w - S(16), h - S(13)};
    DrawTextW(mem, str(STR_HELLO), -1, &br, DT_WORDBREAK | DT_NOPREFIX);

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
    case WM_TIMER:
    case WM_LBUTTONUP:
        dismiss();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        wnd = 0;
        DeleteObject(title_f);
        DeleteObject(body_f);
        title_f = body_f = 0;
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void hello_show(HWND o)
{
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
        col_bg = RGB(249, 249, 249);
        col_title = RGB(26, 26, 26);
        col_body = RGB(110, 110, 110);
    } else {
        col_bg = RGB(43, 43, 43);
        col_title = RGB(240, 240, 240);
        col_body = RGB(165, 165, 165);
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_hello", 0, WS_POPUP, 0, 0, 0, 0, o, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    title_f = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    body_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");

    w = S(340);
    int tx = S(16) + S(48) + S(14);
    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, body_f);
    RECT mr = {0, 0, w - tx - S(16), 0};
    DrawTextW(dc, str(STR_HELLO), -1, &mr, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, of);
    ReleaseDC(wnd, dc);
    h = S(35) + mr.bottom + S(13);
    if (h < S(48) + S(28))
        h = S(48) + S(28);

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - w - S(16);
    int y = wa.bottom - h - S(16);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    AnimateWindow(wnd, 150, AW_BLEND);
    SetTimer(wnd, 1, 6000, 0);
}
