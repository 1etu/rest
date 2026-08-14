#pragma once

enum {
    HK_BREAK = 1,
    HK_SKIP,
    HK_PAUSE,
};

void hotkeys_apply(HWND w);
void hotkey_name(int code, wchar_t *out);
