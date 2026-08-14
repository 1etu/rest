#include "loc/strings.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const wchar_t *en[STR_COUNT] = {
    L"Next break in 20 min",
    L"Pause",
    L"Take a break now",
    L"Skip next break",
    L"Settings…",
    L"Launch at login",
    L"Quit",
    L"Runs in the tray — no window will open. Click the icon for the menu.",
};

static const wchar_t *tr[STR_COUNT] = {
    L"Sonraki mola 20 dk sonra",
    L"Duraklat",
    L"Şimdi mola ver",
    L"Sonraki molayı atla",
    L"Ayarlar…",
    L"Açılışta başlat",
    L"Çık",
    L"Tepside çalışır — pencere açılmaz. Menü için simgeye tıkla.",
};

const wchar_t *str(int id)
{
    static const wchar_t **tbl;
    if (!tbl)
        tbl = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? tr : en;
    return tbl[id];
}
