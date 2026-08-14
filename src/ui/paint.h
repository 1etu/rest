#pragma once

void paint_blend(DWORD *px, COLORREF c, float a);
void paint_round(HDC dc, int x, int y, int w, int h, int rad, COLORREF c, int alpha);
void paint_ring(void *bits, int w, int h, float cx, float cy, float r, float thick,
    float frac, COLORREF track, float track_a, COLORREF arc);
