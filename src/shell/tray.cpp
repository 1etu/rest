#include "app/app.h"
#include "app/resource.h"
#include "core/config.h"
#include "shell/tray.h"
#include "notify/hello.h"

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

static HICON tint_icon(int brk)
{
    int s = GetSystemMetrics(SM_CXSMICON);
    HICON src = (HICON)LoadImageW(GetModuleHandleW(0), MAKEINTRESOURCEW(IDI_TRAY),
        IMAGE_ICON, s, s, LR_DEFAULTCOLOR);
    if (!src)
        return 0;

    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = s;
    bh.bV5Height = -s;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HDC dc = CreateCompatibleDC(0);
    HBITMAP bmp = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    DrawIconEx(dc, 0, 0, src, s, s, 0, 0, DI_NORMAL);
    GdiFlush();

    COLORREF c = brk ? APP_ACCENT : light_taskbar() ? RGB(32, 32, 32) : RGB(255, 255, 255);
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    DWORD *p = (DWORD *)bits;
    for (int i = 0; i < s * s; i++, p++) {
        int a = *p >> 24;
        *p = (DWORD)a << 24 | (DWORD)(r * a / 255) << 16 |
            (DWORD)(g * a / 255) << 8 | (DWORD)(b * a / 255);
    }

    ICONINFO ii = {TRUE};
    ii.hbmColor = bmp;
    ii.hbmMask = CreateBitmap(s, s, 1, 1, 0);
    HICON out = CreateIconIndirect(&ii);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteObject(ii.hbmMask);
    DeleteDC(dc);
    DestroyIcon(src);
    return out;
}

void tray_add(HWND w)
{
    nid.cbSize = sizeof nid;
    nid.hWnd = w;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = tint_icon(0);
    lstrcpyW(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    if (first_run())
        hello_show(w, HELLO_WELCOME);
}

void tray_set_break(int on)
{
    HICON old = nid.hIcon;
    nid.hIcon = tint_icon(on);
    nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (old)
        DestroyIcon(old);
}

void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &nid);
}
