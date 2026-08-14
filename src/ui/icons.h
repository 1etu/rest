#pragma once

enum {
    ICON_BREAKS,
    ICON_NOTIFY,
    ICON_SOUND,
    ICON_SCHEDULE,
    ICON_KEYS,
    ICON_ABOUT,
    ICON_CHEVRON,
    ICON_CHECK,
    ICON_CLOSE,
    ICON_MINIMIZE,
    ICON_PLUS,
    ICON_COUNT,
};

void paint_icon(HDC dc, int id, int x, int y, int size, COLORREF c, int alpha);
