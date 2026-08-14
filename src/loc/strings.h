#pragma once

enum {
    STR_NEXT_BREAK,
    STR_PAUSE,
    STR_BREAK_NOW,
    STR_SKIP,
    STR_SETTINGS,
    STR_LOGIN,
    STR_QUIT,
    STR_COUNT,
};

const wchar_t *str(int id);
