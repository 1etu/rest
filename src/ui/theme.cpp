#include "app/app.h"
#include "ui/theme.h"

ui_theme th;

int theme_is_light(void)
{
    DWORD v = 1, n = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &n);
    return v;
}

void theme_load(void)
{
    if (theme_is_light()) {
        th.bg = RGB(250, 250, 250);
        th.side = RGB(243, 243, 243);
        th.pop = RGB(252, 252, 252);
        th.text = RGB(28, 28, 28);
        th.dim = RGB(128, 128, 128);
        th.sep = RGB(230, 230, 230);
        th.ctrl = RGB(255, 255, 255);
        th.border = RGB(219, 219, 219);
        th.hover = RGB(236, 236, 236);
        th.sel = RGB(229, 229, 229);
    } else {
        th.bg = RGB(32, 32, 32);
        th.side = RGB(25, 25, 25);
        th.pop = RGB(43, 43, 43);
        th.text = RGB(237, 237, 237);
        th.dim = RGB(150, 150, 150);
        th.sep = RGB(48, 48, 48);
        th.ctrl = RGB(46, 46, 46);
        th.border = RGB(62, 62, 62);
        th.hover = RGB(56, 56, 56);
        th.sel = RGB(62, 62, 62);
    }
    th.accent = APP_ACCENT;
}
