#pragma once

struct ui_theme {
    COLORREF bg, side, pop, text, dim, sep, ctrl, border, hover, accent, sel;
};

extern ui_theme th;

void theme_load(void);
int theme_is_light(void);
