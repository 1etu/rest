#include "loc/strings.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const wchar_t *en[STR_COUNT] = {
    L"Next break in %d:%02d",
    L"Pause",
    L"Take a break now",
    L"Skip next break",
    L"Settings…",
    L"Launch at login",
    L"Quit",
    L"Runs in the tray, no window will open. Click the icon for the menu.",
    L"Resume",
    L"Paused",
    L"Break ends in %d s",
    L"Break every",
    L"%d min",
    L"New timer…",
    L"focus 3 hours",
    L"Look at something 20 feet away",
    L"Skip",
};

static const wchar_t *tr[STR_COUNT] = {
    L"Sonraki molaya %d:%02d",
    L"Duraklat",
    L"Şimdi mola ver",
    L"Sonraki molayı atla",
    L"Ayarlar…",
    L"Açılışta başlat",
    L"Çıkış Yap",
    L"Tepside çalışır, pencere açılmaz. Menü için simgeye tıkla.",
    L"Devam et",
    L"Duraklatıldı",
    L"Molanın bitmesine %d sn",
    L"Mola sıklığı",
    L"%d dk",
    L"Yeni sayaç…",
    L"odak 3 saat",
    L"6 metre uzaktaki bir şeye bak",
    L"Atla",
};

const wchar_t *str(int id)
{
    static const wchar_t **tbl;
    if (!tbl)
        tbl = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? tr : en;
    return tbl[id];
}
