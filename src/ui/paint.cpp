#include "app/app.h"
#include "ui/paint.h"
#include <math.h>

#define TAU 6.2831853f

void paint_blend(DWORD *px, COLORREF c, float a)
{
    if (a <= 0)
        return;
    if (a > 1) a = 1;
    int br = (*px >> 16) & 255, bg = (*px >> 8) & 255, bb = *px & 255;
    int r = (int)(br + (GetRValue(c) - br) * a);
    int g = (int)(bg + (GetGValue(c) - bg) * a);
    int b = (int)(bb + (GetBValue(c) - bb) * a);
    *px = (DWORD)r << 16 | (DWORD)g << 8 | b;
}

void paint_round(HDC dc, int x, int y, int w, int h, int rad, COLORREF c, int alpha)
{
    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = w;
    bh.bV5Height = -h;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ ob = SelectObject(m, b);
    DWORD *p = (DWORD *)bits;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++, p++) {
            float fx = xx + 0.5f, fy = yy + 0.5f;
            float qx = fx < rad ? rad - fx : fx > w - rad ? fx - (w - rad) : 0;
            float qy = fy < rad ? rad - fy : fy > h - rad ? fy - (h - rad) : 0;
            float cov = rad - sqrtf(qx * qx + qy * qy);
            if (cov < 0) cov = 0;
            if (cov > 1) cov = 1;
            int a = (int)(alpha * cov);
            *p = (DWORD)a << 24 | (DWORD)(GetRValue(c) * a / 255) << 16 |
                (DWORD)(GetGValue(c) * a / 255) << 8 | (DWORD)(GetBValue(c) * a / 255);
        }
    GdiFlush();
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, w, h, m, 0, 0, w, h, bf);
    SelectObject(m, ob);
    DeleteObject(b);
    DeleteDC(m);
}

void paint_ring(void *bits, int w, int h, float cx, float cy, float r, float thick,
    float frac, COLORREF track, float track_a, COLORREF arc)
{
    float half = thick / 2;
    float lo = r - half - 1, hi = r + half + 1;
    float span = frac * TAU;
    DWORD *px = (DWORD *)bits;

    for (int y = 0; y < h; y++) {
        float dy = y + 0.5f - cy;
        if (dy < -hi || dy > hi)
            continue;
        DWORD *row = px + y * w;
        for (int x = 0; x < w; x++) {
            float dx = x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 < lo * lo || d2 > hi * hi)
                continue;
            float d = sqrtf(d2);
            float cov = half + 0.5f - fabsf(d - r);
            if (cov <= 0)
                continue;
            if (cov > 1) cov = 1;
            paint_blend(row + x, track, cov * track_a);
            if (frac <= 0)
                continue;
            float a = atan2f(dx, -dy);
            if (a < 0)
                a += TAU;
            float lead = frac >= 1 ? 1 : (span - a) * d;
            float tail = frac >= 1 ? 1 : a * d;
            float e = lead < tail ? lead : tail;
            if (e <= 0)
                continue;
            if (e > 1) e = 1;
            paint_blend(row + x, arc, cov * e);
        }
    }
}
