#include "app/app.h"
#include "core/timer.h"
#include "ui/popup.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>

static HWND wnd;
static HFONT big_f, hint_f, btn_f;
static int dpi, w, h, skip_hot;
static RECT skip_rc;
static COLORREF col_bg, col_text, col_dim, col_hot;

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
    HGDIOBJ obmp = SelectObject(mem, bmp);

    RECT rc = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(col_bg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    wchar_t buf[8];
    wsprintfW(buf, L"%d", timer_seconds_left());
    SelectObject(mem, big_f);
    SetTextColor(mem, col_text);
    RECT br = {0, S(14), w, S(76)};
    DrawTextW(mem, buf, -1, &br, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(mem, hint_f);
    SetTextColor(mem, col_dim);
    RECT hr = {0, S(78), w, S(98)};
    DrawTextW(mem, str(STR_LOOK_AWAY), -1, &hr,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    if (skip_hot) {
        HBRUSH hb = CreateSolidBrush(col_hot);
        HGDIOBJ ob = SelectObject(mem, hb);
        HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
        RoundRect(mem, skip_rc.left, skip_rc.top, skip_rc.right + 1,
            skip_rc.bottom + 1, S(8), S(8));
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(hb);
    }
    SelectObject(mem, btn_f);
    SetTextColor(mem, APP_ACCENT);
    DrawTextW(mem, str(STR_SKIP_SHORT), -1, &skip_rc,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK popupproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
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
        InvalidateRect(m, 0, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int over = PtInRect(&skip_rc, pt);
        if (over != skip_hot) {
            skip_hot = over;
            InvalidateRect(m, 0, FALSE);
        }
        return 0;
    }
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
        DeleteObject(big_f);
        DeleteObject(hint_f);
        DeleteObject(btn_f);
        big_f = hint_f = btn_f = 0;
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void popup_break(int on)
{
    if (!on) {
        if (!wnd)
            return;
        HWND t = wnd;
        wnd = 0;
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
        wc.lpfnWndProc = popupproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_popup";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    if (light_mode()) {
        col_bg = RGB(249, 249, 249);
        col_text = RGB(26, 26, 26);
        col_dim = RGB(130, 130, 130);
        col_hot = RGB(232, 232, 232);
    } else {
        col_bg = RGB(43, 43, 43);
        col_text = RGB(240, 240, 240);
        col_dim = RGB(150, 150, 150);
        col_hot = RGB(62, 62, 62);
    }

    wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_NAME L"_popup", 0, WS_POPUP, 0, 0, 0, 0, 0, 0, inst, 0);
    dpi = GetDpiForWindow(wnd);
    big_f = CreateFontW(-MulDiv(40, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    hint_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    btn_f = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    HDC dc = GetDC(wnd);
    HGDIOBJ of = SelectObject(dc, hint_f);
    SIZE hs;
    GetTextExtentPoint32W(dc, str(STR_LOOK_AWAY), lstrlenW(str(STR_LOOK_AWAY)), &hs);
    SelectObject(dc, btn_f);
    SIZE bs;
    GetTextExtentPoint32W(dc, str(STR_SKIP_SHORT), lstrlenW(str(STR_SKIP_SHORT)), &bs);
    SelectObject(dc, of);
    ReleaseDC(wnd, dc);

    w = hs.cx + S(48);
    if (w < S(240))
        w = S(240);
    h = S(146);
    int bw = bs.cx + S(28);
    skip_rc.left = (w - bw) / 2;
    skip_rc.right = skip_rc.left + bw;
    skip_rc.top = S(106);
    skip_rc.bottom = S(132);
    skip_hot = 0;

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = (wa.left + wa.right - w) / 2;
    int y = wa.top + S(24);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);

    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    AnimateWindow(wnd, 150, AW_BLEND);
    SetTimer(wnd, 1, 250, 0);
}
