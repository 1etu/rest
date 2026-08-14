#include "app/app.h"
#include "core/config.h"
#include "shell/tray.h"
#include "notify/hello.h"
#include <math.h>

static NOTIFYICONDATAW nid;

static int first_run(void)
{
    if (cfg.greeted)
        return 0;
    cfg.greeted = 1;
    cfg_save();
    return 1;
}

static int light_taskbar(void)
{
    DWORD v = 0, n = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, 0, &v, &n);
    return v;
}

static HICON draw_icon(int brk)
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
    if (brk) {
        COLORREF ac = APP_ACCENT;
        rgb = (DWORD)GetRValue(ac) << 16 | GetGValue(ac) << 8 | GetBValue(ac);
    }
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
    nid.hIcon = draw_icon(0);
    lstrcpyW(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    if (first_run())
        hello_show(w);
}

void tray_set_break(int on)
{
    HICON old = nid.hIcon;
    nid.hIcon = draw_icon(on);
    nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    DestroyIcon(old);
}

void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

