#pragma once

enum {
    STR_NEXT_BREAK,
    STR_PAUSE,
    STR_BREAK_NOW,
    STR_SKIP,
    STR_SETTINGS,
    STR_LOGIN,
    STR_QUIT,
    STR_HELLO,
    STR_RESUME,
    STR_PAUSED,
    STR_ON_BREAK,
    STR_EVERY,
    STR_MIN_FMT,
    STR_NEW_TIMER,
    STR_TIMER_HINT,
    STR_COUNT,
};

const wchar_t *str(int id);
