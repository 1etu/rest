#include "app/app.h"
#include "core/hotkeys.h"
#include "core/config.h"
#include "loc/strings.h"

static void bind(HWND w, int id, int code)
{
    UnregisterHotKey(w, id);
    if (!code)
        return;
    RegisterHotKey(w, id, (code >> 16) | MOD_NOREPEAT, code & 0xffff);
}

void hotkeys_apply(HWND w)
{
    bind(w, HK_BREAK, cfg.hk_break);
    bind(w, HK_SKIP, cfg.hk_skip);
    bind(w, HK_PAUSE, cfg.hk_pause);
}

void hotkey_name(int code, wchar_t *out)
{
    if (!code) {
        lstrcpyW(out, str(STR_HK_NONE));
        return;
    }
    int mods = code >> 16, vk = code & 0xffff;
    out[0] = 0;
    if (mods & MOD_CONTROL)
        lstrcatW(out, L"Ctrl + ");
    if (mods & MOD_ALT)
        lstrcatW(out, L"Alt + ");
    if (mods & MOD_SHIFT)
        lstrcatW(out, L"Shift + ");
    if (mods & MOD_WIN)
        lstrcatW(out, L"Win + ");

    wchar_t key[24] = {};
    if (vk >= 'A' && vk <= 'Z')
        key[0] = (wchar_t)vk;
    else if (vk >= '0' && vk <= '9')
        key[0] = (wchar_t)vk;
    else if (vk >= VK_F1 && vk <= VK_F24)
        wsprintfW(key, L"F%d", vk - VK_F1 + 1);
    else
        GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, key, 24);
    lstrcatW(out, key);
}
