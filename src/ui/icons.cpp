#include "app/app.h"
#include "ui/icons.h"
#include <math.h>

#define SH_LINE 1
#define SH_CIRC 2
#define SH_RECT 3
#define SH_ARC 4

#define W 1.5f
#define TAU 6.2831853f

struct shape {
    unsigned char k;
    float v[6];
};

static const shape breaks_s[] = {
    {SH_ARC, {12, 18.34f, 11.84f, W, 302.4f, 57.6f}},
    {SH_ARC, {12, 5.66f, 11.84f, W, 122.4f, 237.6f}},
    {SH_CIRC, {12, 12, 2.5f, 0}},
};

static const shape notify_s[] = {
    {SH_RECT, {6.4f, 4.2f, 11.2f, 11.6f, 5.6f, W}},
    {SH_LINE, {4.4f, 16.4f, 19.6f, 16.4f, W}},
    {SH_CIRC, {12, 19.4f, 1.35f, 0}},
};

static const shape sound_s[] = {
    {SH_LINE, {6.6f, 9.6f, 6.6f, 14.4f, W}},
    {SH_LINE, {6.6f, 9.6f, 11.2f, 6.2f, W}},
    {SH_LINE, {11.2f, 6.2f, 11.2f, 17.8f, W}},
    {SH_LINE, {11.2f, 17.8f, 6.6f, 14.4f, W}},
    {SH_ARC, {11.2f, 12, 4.0f, W, 40, 140}},
    {SH_ARC, {11.2f, 12, 6.8f, W, 45, 135}},
};

static const shape schedule_s[] = {
    {SH_CIRC, {12, 12, 8.4f, W}},
    {SH_LINE, {12, 7.2f, 12, 12, W}},
    {SH_LINE, {12, 12, 15.4f, 13.4f, W}},
};

static const shape keys_s[] = {
    {SH_RECT, {3.2f, 6.4f, 17.6f, 11.2f, 2.8f, W}},
    {SH_CIRC, {7.4f, 10.4f, 0.85f, 0}},
    {SH_CIRC, {12, 10.4f, 0.85f, 0}},
    {SH_CIRC, {16.6f, 10.4f, 0.85f, 0}},
    {SH_LINE, {8.4f, 14.2f, 15.6f, 14.2f, W}},
};

static const shape about_s[] = {
    {SH_CIRC, {12, 12, 8.4f, W}},
    {SH_CIRC, {12, 8.4f, 0.95f, 0}},
    {SH_LINE, {12, 11.4f, 12, 16.2f, W}},
};

static const shape chevron_s[] = {
    {SH_LINE, {8.2f, 10.4f, 12, 14.2f, 1.6f}},
    {SH_LINE, {12, 14.2f, 15.8f, 10.4f, 1.6f}},
};

static const shape check_s[] = {
    {SH_LINE, {6.6f, 12.4f, 10.2f, 16, 1.7f}},
    {SH_LINE, {10.2f, 16, 17.4f, 8.4f, 1.7f}},
};

static const shape close_s[] = {
    {SH_LINE, {8.2f, 8.2f, 15.8f, 15.8f, 2.0f}},
    {SH_LINE, {15.8f, 8.2f, 8.2f, 15.8f, 2.0f}},
};

static const shape minimize_s[] = {
    {SH_LINE, {6.8f, 12, 17.2f, 12, 2.0f}},
};

static const shape plus_s[] = {
    {SH_LINE, {12, 7, 12, 17, 1.7f}},
    {SH_LINE, {7, 12, 17, 12, 1.7f}},
};

struct icon {
    const shape *s;
    int n;
};

#define ENTRY(a) {a, (int)(sizeof a / sizeof a[0])}

static const icon icons[ICON_COUNT] = {
    ENTRY(breaks_s),
    ENTRY(notify_s),
    ENTRY(sound_s),
    ENTRY(schedule_s),
    ENTRY(keys_s),
    ENTRY(about_s),
    ENTRY(chevron_s),
    ENTRY(check_s),
    ENTRY(close_s),
    ENTRY(minimize_s),
    ENTRY(plus_s),
};

static float seg_dist(float px, float py, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = dx * dx + dy * dy;
    float t = len > 0 ? ((px - x0) * dx + (py - y0) * dy) / len : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float ax = px - (x0 + t * dx), ay = py - (y0 + t * dy);
    return sqrtf(ax * ax + ay * ay);
}

static float rect_dist(float px, float py, const float *v)
{
    float cx = v[0] + v[2] / 2, cy = v[1] + v[3] / 2;
    float ex = v[2] / 2 - v[4], ey = v[3] / 2 - v[4];
    float dx = fabsf(px - cx) - ex, dy = fabsf(py - cy) - ey;
    float mx = dx > 0 ? dx : 0, my = dy > 0 ? dy : 0;
    float outside = sqrtf(mx * mx + my * my);
    float inside = dx > dy ? dx : dy;
    if (inside > 0) inside = 0;
    return outside + inside - v[4];
}

static float cover(const shape *s, float px, float py)
{
    float d, t;
    switch (s->k) {
    case SH_LINE:
        d = seg_dist(px, py, s->v[0], s->v[1], s->v[2], s->v[3]);
        return s->v[4] / 2 + 0.5f - d;
    case SH_CIRC: {
        float ddx = px - s->v[0], ddy = py - s->v[1];
        d = sqrtf(ddx * ddx + ddy * ddy);
        if (s->v[3] <= 0)
            return s->v[2] + 0.5f - d;
        return s->v[3] / 2 + 0.5f - fabsf(d - s->v[2]);
    }
    case SH_RECT:
        d = rect_dist(px, py, s->v);
        if (s->v[5] <= 0)
            return 0.5f - d;
        return s->v[5] / 2 + 0.5f - fabsf(d);
    case SH_ARC: {
        float ddx = px - s->v[0], ddy = py - s->v[1];
        d = sqrtf(ddx * ddx + ddy * ddy);
        float radial = s->v[3] / 2 + 0.5f - fabsf(d - s->v[2]);
        if (radial <= 0)
            return 0;
        float a = atan2f(ddx, -ddy) * 180 / 3.14159265f;
        if (a < 0) a += 360;
        float lo = s->v[4], hi = s->v[5];
        if (lo > hi) {
            if (a < lo && a > hi)
                return 0;
            if (a < lo)
                a += 360;
            hi += 360;
        } else if (a < lo || a > hi) {
            return 0;
        }
        t = (a - lo) * d * 0.0175f;
        float u = (hi - a) * d * 0.0175f;
        if (u < t) t = u;
        return radial < t ? radial : t;
    }
    }
    return 0;
}

void paint_icon(HDC dc, int id, int x, int y, int size, COLORREF c, int alpha)
{
    if (id < 0 || id >= ICON_COUNT || size <= 0)
        return;
    const icon *ic = &icons[id];
    float scale = size / 24.0f;

    BITMAPV5HEADER bh = {sizeof bh};
    bh.bV5Width = size;
    bh.bV5Height = -size;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    void *bits;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateDIBSection(0, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ ob = SelectObject(m, b);

    int r = GetRValue(c), g = GetGValue(c), bl = GetBValue(c);
    DWORD *p = (DWORD *)bits;
    for (int yy = 0; yy < size; yy++)
        for (int xx = 0; xx < size; xx++, p++) {
            float px = (xx + 0.5f) / scale, py = (yy + 0.5f) / scale;
            float best = 0;
            for (int i = 0; i < ic->n; i++) {
                float v = cover(&ic->s[i], px, py) * scale;
                if (v > best)
                    best = v;
            }
            if (best <= 0) {
                *p = 0;
                continue;
            }
            if (best > 1) best = 1;
            int a = (int)(best * alpha);
            *p = (DWORD)a << 24 | (DWORD)(r * a / 255) << 16 |
                (DWORD)(g * a / 255) << 8 | (DWORD)(bl * a / 255);
        }

    GdiFlush();
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, size, size, m, 0, 0, size, size, bf);
    SelectObject(m, ob);
    DeleteObject(b);
    DeleteDC(m);
}
