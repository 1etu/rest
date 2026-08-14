#include "app/app.h"
#include "app/resource.h"
#include "core/config.h"
#include "core/timer.h"
#include "notify/sound.h"
#include "ui/settings.h"
#include "ui/dropdown.h"
#include "ui/icons.h"
#include "ui/paint.h"
#include "ui/theme.h"
#include "loc/strings.h"
#include <windowsx.h>
#include <dwmapi.h>

#define R_TOGGLE 1
#define R_SEL_INT 2
#define R_SEL_STR 3
#define R_SLIDER 4
#define R_DAYS 5
#define R_TIME 6

#define WIN_W 760
#define WIN_H 604
#define BAR_H 44
#define SIDE_W 176
#define PAD 28

struct row {
    unsigned char type;
    int sid, dsid;
    void *val;
    const int *iv;
    const wchar_t *const *sv;
    const int *labels;
    int fmt;
    int n;
    int lo, hi;
    RECT rc, hit;
};

static const int iv_interval[] = {5, 10, 15, 20, 25, 30, 45, 60, 90};
static const int iv_break[] = {10, 20, 30, 45, 60, 120, 300};
static const int iv_grace[] = {0, 1, 2, 5};
static const int iv_hold[] = {5, 10, 15, 30, 60, 120};
static const int iv_idle[] = {0, 1, 2, 3, 5, 10, 15};
static const int iv_warn[] = {0, 5, 10, 15, 30, 60};

static const wchar_t *const sv_style[] = {L"overlay", L"notification", L"popup", L"cursor"};
static const int lb_style[] = {STR_O_OVERLAY, STR_O_NOTIFICATION, STR_O_POPUP, STR_O_CURSOR};

static const wchar_t *const sv_pos[] = {L"top-left", L"top-center", L"top-right",
    L"bottom-left", L"bottom-center", L"bottom-right", L"center"};
static const int lb_pos[] = {STR_P_TL, STR_P_TC, STR_P_TR, STR_P_BL, STR_P_BC,
    STR_P_BR, STR_P_CENTER};

static const wchar_t *const sv_snd[] = {L"none", L"chime", L"soft", L"tick", L"bell", L"drop"};
static const int lb_snd[] = {STR_S_NONE, STR_S_CHIME, STR_S_SOFT, STR_S_TICK,
    STR_S_BELL, STR_S_DROP};

#define SEL_INT(l, d, v, arr, f) \
    {R_SEL_INT, l, d, &v, arr, 0, 0, f, (int)(sizeof arr / sizeof arr[0]), 0, 0, {}, {}}
#define SEL_STR(l, d, v, arr, lb) \
    {R_SEL_STR, l, d, v, 0, arr, lb, 0, (int)(sizeof arr / sizeof arr[0]), 0, 0, {}, {}}
#define TOGGLE(l, d, v) {R_TOGGLE, l, d, &v, 0, 0, 0, 0, 0, 0, 0, {}, {}}

static row breaks_rows[] = {
    SEL_INT(STR_L_INTERVAL, 0, cfg.interval, iv_interval, STR_U_MIN),
    SEL_INT(STR_L_LENGTH, 0, cfg.brk, iv_break, STR_U_SEC),
    SEL_INT(STR_L_IDLE, STR_D_IDLE, cfg.idle, iv_idle, STR_U_MIN),
    TOGGLE(STR_L_SMART, STR_D_SMART, cfg.smart),
    TOGGLE(STR_L_FULLSCREEN, STR_D_FULLSCREEN, cfg.on_fullscreen),
    TOGGLE(STR_L_MEETING, STR_D_MEETING, cfg.on_meeting),
    SEL_INT(STR_L_GRACE, 0, cfg.grace, iv_grace, STR_U_MIN),
    SEL_INT(STR_L_MAXHOLD, STR_D_MAXHOLD, cfg.max_hold, iv_hold, STR_U_MIN),
};

static row notify_rows[] = {
    SEL_STR(STR_L_STYLE, STR_D_STYLE, cfg.style, sv_style, lb_style),
    SEL_STR(STR_L_POSITION, STR_D_POSITION, cfg.pos, sv_pos, lb_pos),
};

static row sound_rows[] = {
    TOGGLE(STR_L_SOUND, 0, cfg.sound),
    {R_SLIDER, STR_L_VOLUME, 0, &cfg.volume, 0, 0, 0, 0, 0, 0, 100, {}, {}},
    SEL_STR(STR_L_S_START, 0, cfg.s_start, sv_snd, lb_snd),
    SEL_STR(STR_L_S_END, 0, cfg.s_end, sv_snd, lb_snd),
    SEL_STR(STR_L_S_WARN, 0, cfg.s_warn, sv_snd, lb_snd),
    SEL_INT(STR_L_WARNSEC, STR_D_WARNSEC, cfg.warn_secs, iv_warn, STR_U_SEC),
};

static row schedule_rows[] = {
    TOGGLE(STR_L_HOURS, STR_D_HOURS, cfg.hours),
    {R_TIME, STR_L_FROM, 0, &cfg.from_min, 0, 0, 0, 0, 48, 0, 0, {}, {}},
    {R_TIME, STR_L_TO, 0, &cfg.to_min, 0, 0, 0, 0, 48, 0, 0, {}, {}},
    {R_DAYS, STR_L_DAYS, 0, &cfg.days, 0, 0, 0, 0, 7, 0, 0, {}, {}},
    TOGGLE(STR_L_LOGIN, 0, cfg.login),
};

struct pane {
    int icon;
    int sid;
    row *rows;
    int n;
};

static pane panes[] = {
    {ICON_BREAKS, STR_NAV_BREAKS, breaks_rows, (int)(sizeof breaks_rows / sizeof breaks_rows[0])},
    {ICON_NOTIFY, STR_NAV_NOTIFY, notify_rows, (int)(sizeof notify_rows / sizeof notify_rows[0])},
    {ICON_SOUND, STR_NAV_SOUND, sound_rows, (int)(sizeof sound_rows / sizeof sound_rows[0])},
    {ICON_SCHEDULE, STR_NAV_SCHEDULE, schedule_rows,
        (int)(sizeof schedule_rows / sizeof schedule_rows[0])},
};

#define NPANES ((int)(sizeof panes / sizeof panes[0]))
static const int nav_sid[NPANES] = {STR_NAV_BREAKS, STR_NAV_NOTIFY, STR_NAV_SOUND,
    STR_NAV_SCHEDULE};

static HWND wnd;
static HFONT f_nav, f_lab, f_desc, f_val, f_head, f_title;
static int dpi, cw, chh;
static int cur_pane, nav_hot = -1, tl_hot, row_hot = -1;
static int drag_slider;
static int edit_row = -1, elen, caret_on;
static wchar_t ebuf[12];
static float tl_a;
static RECT tl_rc[2], nav_rc[NPANES];

static int S(int v)
{
    return MulDiv(v, dpi, 96);
}

static void opt_label(row *r, int i, wchar_t *buf)
{
    if (i < 0) {
        wsprintfW(buf, str(r->fmt), *(int *)r->val);
        return;
    }
    if (r->labels) {
        lstrcpyW(buf, str(r->labels[i]));
        return;
    }
    if (r->iv && !r->iv[i] && r->fmt == STR_U_MIN) {
        lstrcpyW(buf, str(STR_U_OFF));
        return;
    }
    if (r->iv && !r->iv[i] && r->fmt == STR_U_SEC) {
        lstrcpyW(buf, str(STR_U_OFF));
        return;
    }
    if (r->iv && r->fmt == STR_U_SEC && r->iv[i] >= 60) {
        wsprintfW(buf, str(STR_U_MIN), r->iv[i] / 60);
        return;
    }
    if (r->iv && r->fmt == STR_U_MIN && r->iv[i] >= 60 && !(r->iv[i] % 60)) {
        wsprintfW(buf, str(STR_U_HOUR), r->iv[i] / 60);
        return;
    }
    wsprintfW(buf, str(r->fmt), r->iv ? r->iv[i] : 0);
}

static int cur_index(row *r)
{
    if (r->type == R_SEL_INT) {
        int v = *(int *)r->val;
        for (int i = 0; i < r->n; i++)
            if (r->iv[i] == v)
                return i;
        return -1;
    }
    for (int i = 0; i < r->n; i++)
        if (!lstrcmpiW((wchar_t *)r->val, r->sv[i]))
            return i;
    return 0;
}

static int row_h(row *r)
{
    return r->dsid ? S(60) : S(48);
}

static void edit_start(int i)
{
    pane *p = &panes[cur_pane];
    row *r = &p->rows[i];
    edit_row = i;
    caret_on = 1;
    if (r->type == R_TIME) {
        int v = *(int *)r->val;
        wsprintfW(ebuf, L"%02d:%02d", v / 60, v % 60);
    } else {
        wsprintfW(ebuf, L"%d", *(int *)r->val);
    }
    elen = lstrlenW(ebuf);
    SetTimer(wnd, 2, 530, 0);
    InvalidateRect(wnd, 0, FALSE);
}

static void edit_commit(int keep)
{
    if (edit_row < 0)
        return;
    pane *p = &panes[cur_pane];
    row *r = &p->rows[edit_row];
    if (keep && elen) {
        int a = 0, b = -1;
        for (int i = 0; i < elen; i++) {
            if (ebuf[i] >= '0' && ebuf[i] <= '9') {
                if (b >= 0)
                    b = b * 10 + ebuf[i] - '0';
                else
                    a = a * 10 + ebuf[i] - '0';
            } else if (b < 0) {
                b = 0;
            }
        }
        if (r->type == R_TIME) {
            int m = b < 0 ? a * 60 : a * 60 + b;
            if (a > 23 && b < 0)
                m = a;
            if (m >= 0 && m < 1440)
                *(int *)r->val = m / 30 * 30 == m ? m : m;
        } else {
            int lo = r->fmt == STR_U_SEC ? 5 : 1;
            int hi = r->fmt == STR_U_SEC ? 3600 : 1440;
            if (a >= lo && a <= hi)
                *(int *)r->val = a;
        }
        cfg_save();
        timer_reload();
    }
    edit_row = -1;
    elen = 0;
    KillTimer(wnd, 2);
    InvalidateRect(wnd, 0, FALSE);
}

static void draw_box(HDC dc, row *r, int editing)
{
    RECT *rc = &r->hit;
    int w2 = rc->right - rc->left, h2 = rc->bottom - rc->top;
    paint_round(dc, rc->left, rc->top, w2, h2, S(8), th.ctrl, 255);
    paint_round(dc, rc->left, rc->top, w2, h2, S(8),
        editing ? th.accent : th.border, editing ? 200 : 120);
    wchar_t buf[32];
    if (editing) {
        lstrcpyW(buf, ebuf);
        if (caret_on)
            lstrcatW(buf, L"|");
    } else if (r->type == R_TIME) {
        int v = *(int *)r->val;
        wsprintfW(buf, L"%02d:%02d", v / 60, v % 60);
    } else {
        opt_label(r, cur_index(r), buf);
    }
    SelectObject(dc, f_val);
    SetTextColor(dc, th.text);
    RECT t = {rc->left + S(12), rc->top, rc->right - S(12), rc->bottom};
    DrawTextW(dc, buf, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
}

static void layout(void)
{
    pane *p = &panes[cur_pane];
    int x = SIDE_W * dpi / 96 + S(PAD);
    int right = cw - S(PAD);
    int y = BAR_H * dpi / 96 + S(14) + S(46);
    for (int i = 0; i < p->n; i++) {
        row *r = &p->rows[i];
        int h = row_h(r);
        SetRect(&r->rc, x, y, right, y + h);
        int cy = y + h / 2;
        switch (r->type) {
        case R_TOGGLE:
            SetRect(&r->hit, right - S(42), cy - S(11), right, cy + S(11));
            break;
        case R_SLIDER:
            SetRect(&r->hit, right - S(170), cy - S(9), right, cy + S(9));
            break;
        case R_DAYS:
            SetRect(&r->hit, right - S(7 * 30 - 4), cy - S(13), right, cy + S(13));
            break;
        case R_TIME:
            SetRect(&r->hit, right - S(92), cy - S(16), right, cy + S(16));
            break;
        default:
            SetRect(&r->hit, right - S(146), cy - S(15), right, cy + S(15));
        }
        y += h;
    }
}

static void draw_toggle(HDC dc, RECT *rc, int on)
{
    int h = rc->bottom - rc->top, w = rc->right - rc->left;
    paint_round(dc, rc->left, rc->top, w, h, h / 2, on ? th.accent : th.border, 255);
    int k = h - S(4);
    int kx = on ? rc->right - S(2) - k : rc->left + S(2);
    paint_round(dc, kx, rc->top + S(2), k, k, k / 2, RGB(255, 255, 255), on ? 255 : 210);
}

static void draw_select(HDC dc, row *r, int hot)
{
    RECT *rc = &r->hit;
    if (hot)
        paint_round(dc, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
            S(8), th.hover, 255);
    wchar_t buf[48];
    opt_label(r, cur_index(r), buf);
    SelectObject(dc, f_val);
    SetTextColor(dc, th.text);
    RECT t = {rc->left + S(12), rc->top, rc->right - S(30), rc->bottom};
    DrawTextW(dc, buf, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    paint_icon(dc, ICON_CHEVRON, rc->right - S(26), (rc->top + rc->bottom) / 2 - S(9),
        S(18), th.dim, 255);
}

static void draw_slider(HDC dc, row *r)
{
    RECT *rc = &r->hit;
    int cy = (rc->top + rc->bottom) / 2;
    int x0 = rc->left, x1 = rc->right - S(14);
    int v = *(int *)r->val;
    int kx = x0 + (x1 - x0) * v / 100;
    paint_round(dc, x0, cy - S(2), x1 - x0 + S(14), S(4), S(2), th.border, 255);
    if (kx > x0)
        paint_round(dc, x0, cy - S(2), kx - x0 + S(7), S(4), S(2), th.accent, 255);
    paint_round(dc, kx, cy - S(7), S(14), S(14), S(7), RGB(255, 255, 255), 255);
}

static void draw_days(HDC dc, row *r)
{
    const wchar_t *letters = str(STR_DAYS_LETTERS);
    int bits = *(int *)r->val;
    int chip = S(26), gap = S(4);
    int x = r->hit.right - (chip + gap) * 7 + gap;
    SelectObject(dc, f_val);
    for (int i = 0; i < 7; i++) {
        int on = bits & 1 << i;
        paint_round(dc, x, r->hit.top, chip, chip, S(8), on ? th.accent : th.ctrl, 255);
        if (!on)
            paint_round(dc, x, r->hit.top, chip, chip, S(8), th.border, 90);
        SetTextColor(dc, on ? RGB(255, 255, 255) : th.dim);
        RECT t = {x, r->hit.top, x + chip, r->hit.bottom};
        wchar_t c[2] = {letters[i], 0};
        DrawTextW(dc, c, 1, &t, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        x += chip + gap;
    }
}

static void paint_rows(HDC dc)
{
    pane *p = &panes[cur_pane];
    for (int i = 0; i < p->n; i++) {
        row *r = &p->rows[i];
        if (i) {
            RECT s = {r->rc.left, r->rc.top, r->rc.right, r->rc.top + 1};
            HBRUSH b = CreateSolidBrush(th.sep);
            FillRect(dc, &s, b);
            DeleteObject(b);
        }
        int cy = (r->rc.top + r->rc.bottom) / 2;
        SelectObject(dc, f_lab);
        SetTextColor(dc, th.text);
        if (r->dsid) {
            RECT t = {r->rc.left, cy - S(19), r->hit.left - S(12), cy - S(1)};
            DrawTextW(dc, str(r->sid), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, f_desc);
            SetTextColor(dc, th.dim);
            RECT d = {r->rc.left, cy + S(1), r->hit.left - S(12), cy + S(19)};
            DrawTextW(dc, str(r->dsid), -1, &d,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else {
            RECT t = {r->rc.left, r->rc.top, r->hit.left - S(12), r->rc.bottom};
            DrawTextW(dc, str(r->sid), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        switch (r->type) {
        case R_TOGGLE:
            draw_toggle(dc, &r->hit, *(int *)r->val);
            break;
        case R_SLIDER:
            draw_slider(dc, r);
            break;
        case R_DAYS:
            draw_days(dc, r);
            break;
        case R_TIME:
            draw_box(dc, r, edit_row == i);
            break;
        default:
            if (edit_row == i)
                draw_box(dc, r, 1);
            else
                draw_select(dc, r, row_hot == i);
        }
    }
}

static void paint_chrome(HDC dc)
{
    RECT all = {0, 0, cw, chh};
    HBRUSH b = CreateSolidBrush(th.bg);
    FillRect(dc, &all, b);
    DeleteObject(b);

    RECT side = {0, 0, SIDE_W * dpi / 96, chh};
    b = CreateSolidBrush(th.side);
    FillRect(dc, &side, b);
    DeleteObject(b);
    RECT edge = {side.right, 0, side.right + 1, chh};
    b = CreateSolidBrush(th.sep);
    FillRect(dc, &edge, b);
    DeleteObject(b);

    static const COLORREF dots[2] = {RGB(255, 95, 87), RGB(254, 188, 46)};
    static const int glyphs[2] = {ICON_CLOSE, ICON_MINIMIZE};
    for (int i = 0; i < 2; i++) {
        int d = S(12);
        paint_round(dc, tl_rc[i].left, tl_rc[i].top, d, d, d / 2, dots[i], 255);
        if (tl_a > 0.01f)
            paint_icon(dc, glyphs[i], tl_rc[i].left, tl_rc[i].top, d,
                RGB(0, 0, 0), (int)(150 * tl_a));
    }

    wchar_t cap[64];
    wsprintfW(cap, L"%s - %s", APP_NAME, str(STR_SETTINGS_TITLE));
    SelectObject(dc, f_title);
    SetTextColor(dc, th.dim);
    RECT t = {0, 0, cw, BAR_H * dpi / 96};
    DrawTextW(dc, cap, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

    SelectObject(dc, f_nav);
    for (int i = 0; i < NPANES; i++) {
        int on = i == cur_pane;
        if (on || i == nav_hot)
            paint_round(dc, nav_rc[i].left, nav_rc[i].top,
                nav_rc[i].right - nav_rc[i].left, nav_rc[i].bottom - nav_rc[i].top,
                S(8), on ? th.sel : th.hover, on ? 255 : 160);
        COLORREF c = on ? th.text : th.dim;
        paint_icon(dc, panes[i].icon, nav_rc[i].left + S(10),
            (nav_rc[i].top + nav_rc[i].bottom) / 2 - S(9), S(18),
            on ? th.accent : th.dim, 255);
        SetTextColor(dc, c);
        RECT r = {nav_rc[i].left + S(38), nav_rc[i].top, nav_rc[i].right, nav_rc[i].bottom};
        DrawTextW(dc, str(nav_sid[i]), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(dc, f_head);
    SetTextColor(dc, th.text);
    RECT ht = {SIDE_W * dpi / 96 + S(PAD), BAR_H * dpi / 96 + S(10),
        cw - S(PAD), BAR_H * dpi / 96 + S(52)};
    DrawTextW(dc, str(panes[cur_pane].sid), -1, &ht,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

static void paint_all(HDC dc)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, cw, chh);
    HGDIOBJ ob = SelectObject(m, bm);
    SetBkMode(m, TRANSPARENT);
    paint_chrome(m);
    paint_rows(m);
    BitBlt(dc, 0, 0, cw, chh, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(bm);
    DeleteDC(m);
}

static void apply(row *r, int idx)
{
    if (r->type == R_SEL_INT)
        *(int *)r->val = r->iv[idx];
    else
        lstrcpyW((wchar_t *)r->val, r->sv[idx]);
    cfg_save();
    timer_reload();
    if (r->val == cfg.s_start || r->val == cfg.s_end || r->val == cfg.s_warn)
        sound_play((wchar_t *)r->val);
}

static void open_dd(row *r, int token)
{
    static wchar_t bufs[16][40];
    static const wchar_t *ptrs[16];
    int n = r->n;
    if (n > 15)
        n = 15;
    for (int i = 0; i < n; i++) {
        opt_label(r, i, bufs[i]);
        ptrs[i] = bufs[i];
    }
    if (r->type == R_SEL_INT) {
        lstrcpyW(bufs[n], str(STR_CUSTOM));
        ptrs[n] = bufs[n];
        n++;
    }
    RECT a = r->hit;
    POINT tl = {a.left, a.top}, br = {a.right, a.bottom};
    ClientToScreen(wnd, &tl);
    ClientToScreen(wnd, &br);
    SetRect(&a, tl.x, tl.y, br.x, br.y);
    dd_open(wnd, a, ptrs, n, cur_index(r), token);
}

static void set_volume(int x)
{
    row *r = &sound_rows[1];
    int x0 = r->hit.left, x1 = r->hit.right - S(14);
    int v = x1 > x0 ? (x - x0) * 100 / (x1 - x0) : 0;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v != cfg.volume) {
        cfg.volume = v;
        InvalidateRect(wnd, 0, FALSE);
    }
}

static void sync_login(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &k))
        return;
    if (cfg.login) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(0, exe, MAX_PATH);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (BYTE *)exe,
            (lstrlenW(exe) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}

static LRESULT CALLBACK setproc(HWND m, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        paint_all(BeginPaint(m, &ps));
        EndPaint(m, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER: {
        if (wp == 2) {
            caret_on = !caret_on;
            InvalidateRect(m, 0, FALSE);
            return 0;
        }
        float t = tl_hot ? tl_a + 0.12f : tl_a - 0.15f;
        if (t > 1) t = 1;
        if (t < 0) t = 0;
        if (t == tl_a) {
            KillTimer(m, 1);
            return 0;
        }
        tl_a = t;
        InvalidateRect(m, 0, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (drag_slider) {
            set_volume(pt.x);
            return 0;
        }
        RECT grp = {tl_rc[0].left - S(6), 0, tl_rc[1].right + S(6), BAR_H * dpi / 96};
        int t = PtInRect(&grp, pt) ? 1 : 0;
        if (t != tl_hot) {
            tl_hot = t;
            SetTimer(m, 1, 16, 0);
        }
        int nh = -1;
        for (int i = 0; i < NPANES; i++)
            if (PtInRect(&nav_rc[i], pt))
                nh = i;
        pane *p = &panes[cur_pane];
        int rh = -1;
        for (int i = 0; i < p->n; i++)
            if ((p->rows[i].type == R_SEL_INT || p->rows[i].type == R_SEL_STR ||
                    p->rows[i].type == R_TIME) &&
                PtInRect(&p->rows[i].hit, pt))
                rh = i;
        if (nh != nav_hot || rh != row_hot) {
            nav_hot = nh;
            row_hot = rh;
            InvalidateRect(m, 0, FALSE);
        }
        TRACKMOUSEEVENT tm = {sizeof tm, TME_LEAVE, m, 0};
        TrackMouseEvent(&tm);
        return 0;
    }
    case WM_MOUSELEAVE:
        nav_hot = row_hot = -1;
        if (tl_hot) {
            tl_hot = 0;
            SetTimer(m, 1, 16, 0);
        }
        InvalidateRect(m, 0, FALSE);
        return 0;
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m, &pt);
        int hand = nav_hot >= 0 || row_hot >= 0;
        RECT grp = {tl_rc[0].left - S(6), 0, tl_rc[1].right + S(6), BAR_H * dpi / 96};
        if (PtInRect(&grp, pt))
            hand = 1;
        SetCursor(LoadCursorW(0, hand ? IDC_HAND : IDC_ARROW));
        return 1;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (edit_row >= 0 && !PtInRect(&panes[cur_pane].rows[edit_row].hit, pt))
            edit_commit(1);
        for (int i = 0; i < 2; i++) {
            RECT hitrc = {tl_rc[i].left - S(4), tl_rc[i].top - S(4),
                tl_rc[i].right + S(4), tl_rc[i].bottom + S(4)};
            if (PtInRect(&hitrc, pt)) {
                if (i)
                    ShowWindow(m, SW_MINIMIZE);
                else
                    DestroyWindow(m);
                return 0;
            }
        }
        for (int i = 0; i < NPANES; i++)
            if (PtInRect(&nav_rc[i], pt)) {
                cur_pane = i;
                row_hot = -1;
                layout();
                InvalidateRect(m, 0, FALSE);
                return 0;
            }
        pane *p = &panes[cur_pane];
        for (int i = 0; i < p->n; i++) {
            row *r = &p->rows[i];
            if (!PtInRect(&r->hit, pt))
                continue;
            if (r->type == R_TOGGLE) {
                *(int *)r->val = !*(int *)r->val;
                if (r->val == &cfg.login)
                    sync_login();
                cfg_save();
                InvalidateRect(m, 0, FALSE);
            } else if (r->type == R_SLIDER) {
                drag_slider = 1;
                SetCapture(m);
                set_volume(pt.x);
            } else if (r->type == R_DAYS) {
                int chip = S(26), gap = S(4);
                int x0 = r->hit.right - (chip + gap) * 7 + gap;
                int k = (pt.x - x0) / (chip + gap);
                if (k >= 0 && k < 7) {
                    cfg.days ^= 1 << k;
                    cfg_save();
                    InvalidateRect(m, 0, FALSE);
                }
            } else if (r->type == R_TIME) {
                if (edit_row != i)
                    edit_start(i);
            } else if (edit_row != i) {
                open_dd(r, i);
            }
            return 0;
        }
        if (pt.y < BAR_H * dpi / 96) {
            ReleaseCapture();
            SendMessageW(m, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (drag_slider) {
            drag_slider = 0;
            ReleaseCapture();
            cfg_save();
            sound_play(cfg.s_warn);
        }
        return 0;
    case WM_DD: {
        pane *p = &panes[cur_pane];
        int i = (int)wp, idx = (int)lp;
        if (i >= 0 && i < p->n) {
            if (p->rows[i].type == R_SEL_INT && idx >= p->rows[i].n)
                edit_start(i);
            else
                apply(&p->rows[i], idx);
        }
        InvalidateRect(m, 0, FALSE);
        return 0;
    }
    case WM_CHAR:
        if (edit_row >= 0) {
            if (wp == 8 && elen)
                ebuf[--elen] = 0;
            else if (elen < 8 && ((wp >= '0' && wp <= '9') || wp == ':')) {
                ebuf[elen++] = (wchar_t)wp;
                ebuf[elen] = 0;
            }
            InvalidateRect(m, 0, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (edit_row >= 0 && (wp == VK_RETURN || wp == VK_ESCAPE))
            edit_commit(wp == VK_RETURN);
        return 0;
    case WM_SETTINGCHANGE:
        theme_load();
        InvalidateRect(m, 0, FALSE);
        return 0;
    case WM_CLOSE:
        DestroyWindow(m);
        return 0;
    case WM_DESTROY:
        dd_close();
        wnd = 0;
        DeleteObject(f_nav);
        DeleteObject(f_lab);
        DeleteObject(f_desc);
        DeleteObject(f_val);
        DeleteObject(f_head);
        DeleteObject(f_title);
        return 0;
    }
    return DefWindowProcW(m, msg, wp, lp);
}

void settings_show(void)
{
    if (wnd) {
        ShowWindow(wnd, SW_RESTORE);
        SetForegroundWindow(wnd);
        return;
    }
    theme_load();

    HINSTANCE inst = GetModuleHandleW(0);
    static int reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = setproc;
        wc.hInstance = inst;
        wc.lpszClassName = APP_NAME L"_settings";
        wc.hCursor = LoadCursorW(0, IDC_ARROW);
        RegisterClassW(&wc);
        reg = 1;
    }

    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int tmp = GetDpiForSystem();
    int w = MulDiv(WIN_W, tmp, 96), h = MulDiv(WIN_H, tmp, 96);
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2;
    int y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;

    wchar_t caption[64];
    wsprintfW(caption, L"%s - %s", APP_NAME, str(STR_SETTINGS_TITLE));
    wnd = CreateWindowExW(0, APP_NAME L"_settings", caption,
        WS_POPUP | WS_MINIMIZEBOX, x, y, w, h, 0, 0, inst, 0);
    HICON big = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0,
        LR_DEFAULTSIZE);
    HICON tiny = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    SendMessageW(wnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    SendMessageW(wnd, WM_SETICON, ICON_SMALL, (LPARAM)tiny);
    dpi = GetDpiForWindow(wnd);
    cw = MulDiv(WIN_W, dpi, 96);
    chh = MulDiv(WIN_H, dpi, 96);
    SetWindowPos(wnd, 0, 0, 0, cw, chh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    f_nav = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    f_lab = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    f_desc = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    f_val = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    f_head = CreateFontW(-MulDiv(18, dpi, 72), 0, 0, 0, FW_LIGHT, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Light");
    f_title = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    int d = S(12), gap = S(8), left = S(20), top = (BAR_H * dpi / 96 - d) / 2;
    for (int i = 0; i < 2; i++)
        SetRect(&tl_rc[i], left + i * (d + gap), top, left + i * (d + gap) + d, top + d);

    int ny = BAR_H * dpi / 96 + S(10);
    for (int i = 0; i < NPANES; i++) {
        SetRect(&nav_rc[i], S(10), ny, SIDE_W * dpi / 96 - S(10), ny + S(36));
        ny += S(38);
    }
    layout();

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(wnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
}
