#include "app.h"
#include "tray.h"
#include <math.h>

static NOTIFYICONDATAW nid;

static int light_taskbar(void)
{
    DWORD v = 0, n = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, 0, &v, &n);
    return v;
}

static HICON draw_icon(void)
{
    int s = GetSystemMetrics(SM_CXSMICON);
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = s;
    bh.bV5Height = -s;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HBITMAP bmp = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);

    DWORD rgb = light_taskbar() ? 0x1f1f1f : 0xffffff;
    float c = s / 2.0f, r = s * 0.40f, t = s * 0.16f;
    DWORD *p = (DWORD *)bits;
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++, p++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a = r - d;
            float b = d - (r - t);
            if (b < a) a = b;
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            *p = (DWORD)(a * 255) << 24 | rgb;
        }

    ICONINFO ii = {TRUE};
    ii.hbmColor = bmp;
    ii.hbmMask = CreateBitmap(s, s, 1, 1, 0);
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    DeleteObject(ii.hbmMask);
    return ic;
}

void tray_add(HWND w)
{
    nid.cbSize = sizeof nid;
    nid.hWnd = w;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = draw_icon();
    lstrcpyW(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void tray_menu(HWND w)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(w);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, 0);
    PostMessageW(w, WM_NULL, 0, 0);
    DestroyMenu(m);
}
