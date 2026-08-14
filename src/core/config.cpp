#include "app/app.h"
#include "core/config.h"
#include <string.h>

config cfg;

static wchar_t path[MAX_PATH], tmp_path[MAX_PATH];

static void defaults(void)
{
    config d = {};
    d.interval = 20;
    d.brk = 20;
    lstrcpyW(d.style, L"overlay");
    lstrcpyW(d.pos, L"bottom-right");
    d.smart = 1;
    d.on_fullscreen = 1;
    d.on_meeting = 1;
    d.on_focus = 1;
    d.grace = 1;
    d.max_hold = 30;
    d.idle = 3;
    d.sound = 1;
    d.volume = 60;
    d.warn_secs = 10;
    lstrcpyW(d.s_start, L"chime");
    lstrcpyW(d.s_end, L"soft");
    lstrcpyW(d.s_warn, L"tick");
    d.from_min = 9 * 60;
    d.to_min = 18 * 60;
    d.days = 0x1f;
    cfg = d;
}

static void cfg_path(void)
{
    if (path[0])
        return;
    GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    lstrcatW(path, L"\\" APP_NAME);
    CreateDirectoryW(path, 0);
    lstrcatW(path, L"\\settings.json");
    lstrcpyW(tmp_path, path);
    lstrcatW(tmp_path, L".tmp");
}

static const char *find(const char *s, const char *key)
{
    const char *p = strstr(s, key);
    if (!p)
        return 0;
    p = strchr(p + strlen(key), ':');
    if (!p)
        return 0;
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        p++;
    return p;
}

static int num_at(const char *p, int def)
{
    if (!p)
        return def;
    if (*p == 't')
        return 1;
    if (*p == 'f')
        return 0;
    if (*p < '0' || *p > '9')
        return def;
    int v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + *p++ - '0';
    return v;
}

static const char *str_at(const char *p, wchar_t *out, int cap)
{
    out[0] = 0;
    if (!p || *p != '"')
        return p;
    char raw[256];
    int n = 0;
    for (p++; *p && *p != '"' && n < 255; p++) {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\'))
            p++;
        raw[n++] = *p;
    }
    raw[n] = 0;
    MultiByteToWideChar(CP_UTF8, 0, raw, -1, out, cap);
    return *p ? p + 1 : p;
}

static int list_at(const char *p, wchar_t out[][40], int max)
{
    int n = 0;
    if (!p || *p != '[')
        return 0;
    const char *end = strchr(p, ']');
    for (p++; p && end && p < end && n < max;) {
        while (p < end && *p != '"')
            p++;
        if (p >= end)
            break;
        p = str_at(p, out[n], 40);
        if (out[n][0])
            n++;
    }
    return n;
}

static void clamp(void)
{
    if (cfg.interval < 1 || cfg.interval > 1440)
        cfg.interval = 20;
    if (cfg.brk < 5 || cfg.brk > 3600)
        cfg.brk = 20;
    if (cfg.grace < 0 || cfg.grace > 60)
        cfg.grace = 1;
    if (cfg.max_hold < 1 || cfg.max_hold > 240)
        cfg.max_hold = 30;
    if (cfg.idle < 0 || cfg.idle > 120)
        cfg.idle = 3;
    if (cfg.volume < 0 || cfg.volume > 100)
        cfg.volume = 60;
    if (cfg.warn_secs < 0 || cfg.warn_secs > 120)
        cfg.warn_secs = 10;
    if (cfg.from_min < 0 || cfg.from_min > 1439)
        cfg.from_min = 9 * 60;
    if (cfg.to_min < 0 || cfg.to_min > 1439)
        cfg.to_min = 18 * 60;
    if (!cfg.style[0])
        lstrcpyW(cfg.style, L"overlay");
    if (!cfg.pos[0])
        lstrcpyW(cfg.pos, L"bottom-right");
}

static void migrate_old(const char *buf)
{
    int iv = num_at(find(buf, "\"interval\""), 0);
    if (iv)
        cfg.interval = iv;
}

void cfg_load(void)
{
    defaults();
    cfg_path();
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD v = 0, n = sizeof v;
        if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\" APP_NAME, L"greeted",
                RRF_RT_REG_DWORD, 0, &v, &n))
            cfg.greeted = v;
        v = 0, n = sizeof v;
        if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\" APP_NAME, L"interval",
                RRF_RT_REG_DWORD, 0, &v, &n) && v)
            cfg.interval = v;
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\" APP_NAME);
        clamp();
        cfg_save();
        return;
    }
    char buf[8192];
    DWORD n = 0;
    ReadFile(f, buf, sizeof buf - 1, &n, 0);
    CloseHandle(f);
    buf[n] = 0;

    cfg.greeted = num_at(find(buf, "\"greeted\""), 0);
    migrate_old(buf);
    cfg.interval = num_at(find(buf, "\"interval\""), cfg.interval);
    cfg.brk = num_at(find(buf, "\"break\""), cfg.brk);
    str_at(find(buf, "\"style\""), cfg.style, 16);
    str_at(find(buf, "\"position\""), cfg.pos, 16);

    cfg.smart = num_at(find(buf, "\"smart\""), cfg.smart);
    cfg.on_fullscreen = num_at(find(buf, "\"onFullscreen\""), cfg.on_fullscreen);
    cfg.on_meeting = num_at(find(buf, "\"onMeeting\""), cfg.on_meeting);
    cfg.on_focus = num_at(find(buf, "\"onFocusApp\""), cfg.on_focus);
    cfg.grace = num_at(find(buf, "\"grace\""), cfg.grace);
    cfg.max_hold = num_at(find(buf, "\"maxHold\""), cfg.max_hold);
    cfg.idle = num_at(find(buf, "\"idle\""), cfg.idle);

    cfg.sound = num_at(find(buf, "\"sound\""), cfg.sound);
    cfg.volume = num_at(find(buf, "\"volume\""), cfg.volume);
    cfg.warn_secs = num_at(find(buf, "\"warnSeconds\""), cfg.warn_secs);
    str_at(find(buf, "\"soundStart\""), cfg.s_start, 64);
    str_at(find(buf, "\"soundEnd\""), cfg.s_end, 64);
    str_at(find(buf, "\"soundWarn\""), cfg.s_warn, 64);

    cfg.hours = num_at(find(buf, "\"workHours\""), cfg.hours);
    cfg.from_min = num_at(find(buf, "\"from\""), cfg.from_min);
    cfg.to_min = num_at(find(buf, "\"to\""), cfg.to_min);
    cfg.days = num_at(find(buf, "\"days\""), cfg.days);
    cfg.login = num_at(find(buf, "\"launchAtLogin\""), cfg.login);

    cfg.nfocus = list_at(find(buf, "\"focusApps\""), cfg.focus, CFG_MAX_APPS);
    cfg.nexcl = list_at(find(buf, "\"ignoreApps\""), cfg.excl, CFG_MAX_APPS);

    const char *p = find(buf, "\"timers\"");
    while (p && cfg.ntimers < CFG_MAX_TIMERS) {
        const char *o = strchr(p, '{');
        const char *e = o ? strchr(o, '}') : 0;
        if (!o || !e)
            break;
        ctimer *t = &cfg.timers[cfg.ntimers];
        str_at(find(o, "\"label\""), t->label, 40);
        t->min = num_at(find(o, "\"minutes\""), 0);
        if (t->label[0] && t->min >= 1 && t->min <= 1440)
            cfg.ntimers++;
        p = e + 1;
    }
    clamp();
}

static int put_str(char *b, const wchar_t *w)
{
    char u8[160];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, sizeof u8, 0, 0);
    int m = 0;
    for (int j = 0; u8[j] && m < 156; j++) {
        if (u8[j] == '"' || u8[j] == '\\')
            b[m++] = '\\';
        b[m++] = u8[j];
    }
    b[m] = 0;
    return m;
}

static int put_list(char *b, const char *key, wchar_t list[][40], int n)
{
    int k = wsprintfA(b, "  \"%s\": [", key);
    for (int i = 0; i < n; i++) {
        char e[176];
        put_str(e, list[i]);
        k += wsprintfA(b + k, "%s\"%s\"", i ? ", " : "", e);
    }
    k += wsprintfA(b + k, "],\n");
    return k;
}

void cfg_save(void)
{
    cfg_path();
    char buf[8192];
    char st[64], ps[64], s1[176], s2[176], s3[176];
    put_str(st, cfg.style);
    put_str(ps, cfg.pos);
    put_str(s1, cfg.s_start);
    put_str(s2, cfg.s_end);
    put_str(s3, cfg.s_warn);

    int n = wsprintfA(buf, "{\n  \"greeted\": %d,\n", cfg.greeted);
    n += wsprintfA(buf + n, "  \"interval\": %d,\n  \"break\": %d,\n", cfg.interval, cfg.brk);
    n += wsprintfA(buf + n, "  \"style\": \"%s\",\n  \"position\": \"%s\",\n", st, ps);
    n += wsprintfA(buf + n, "  \"smart\": %d,\n  \"onFullscreen\": %d,\n",
        cfg.smart, cfg.on_fullscreen);
    n += wsprintfA(buf + n, "  \"onMeeting\": %d,\n  \"onFocusApp\": %d,\n",
        cfg.on_meeting, cfg.on_focus);
    n += wsprintfA(buf + n, "  \"grace\": %d,\n  \"maxHold\": %d,\n  \"idle\": %d,\n",
        cfg.grace, cfg.max_hold, cfg.idle);
    n += wsprintfA(buf + n, "  \"sound\": %d,\n  \"volume\": %d,\n  \"warnSeconds\": %d,\n",
        cfg.sound, cfg.volume, cfg.warn_secs);
    n += wsprintfA(buf + n, "  \"soundStart\": \"%s\",\n  \"soundEnd\": \"%s\",\n", s1, s2);
    n += wsprintfA(buf + n, "  \"soundWarn\": \"%s\",\n", s3);
    n += wsprintfA(buf + n, "  \"workHours\": %d,\n  \"from\": %d,\n  \"to\": %d,\n  \"days\": %d,\n",
        cfg.hours, cfg.from_min, cfg.to_min, cfg.days);
    n += wsprintfA(buf + n, "  \"launchAtLogin\": %d,\n", cfg.login);
    n += put_list(buf + n, "focusApps", cfg.focus, cfg.nfocus);
    n += put_list(buf + n, "ignoreApps", cfg.excl, cfg.nexcl);

    n += wsprintfA(buf + n, "  \"timers\": [");
    for (int i = 0; i < cfg.ntimers; i++) {
        char lab[176];
        put_str(lab, cfg.timers[i].label);
        n += wsprintfA(buf + n, "%s\n    {\"label\": \"%s\", \"minutes\": %d}",
            i ? "," : "", lab, cfg.timers[i].min);
    }
    n += wsprintfA(buf + n, "%s]\n}\n", cfg.ntimers ? "\n  " : "");

    HANDLE f = CreateFileW(tmp_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD wr;
    WriteFile(f, buf, n, &wr, 0);
    CloseHandle(f);
    MoveFileExW(tmp_path, path, MOVEFILE_REPLACE_EXISTING);
}
