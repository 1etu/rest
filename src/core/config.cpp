#include "app/app.h"
#include "core/config.h"
#include <stdio.h>

config cfg = {0, 20, 0, {}};

static wchar_t path[MAX_PATH], tmp_path[MAX_PATH];

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
    if (!p || (*p < '0' || *p > '9'))
        return def;
    int v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + *p++ - '0';
    return v;
}

static void str_at(const char *p, wchar_t *out, int cap)
{
    out[0] = 0;
    if (!p || *p != '"')
        return;
    char raw[128];
    int n = 0;
    for (p++; *p && *p != '"' && n < 127; p++) {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\'))
            p++;
        raw[n++] = *p;
    }
    raw[n] = 0;
    MultiByteToWideChar(CP_UTF8, 0, raw, -1, out, cap);
}

static void migrate(void)
{
    DWORD v = 0, n = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\" APP_NAME, L"greeted",
            RRF_RT_REG_DWORD, 0, &v, &n))
        cfg.greeted = v;
    v = 0, n = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\" APP_NAME, L"interval",
            RRF_RT_REG_DWORD, 0, &v, &n) && v >= 5 && v <= 1440)
        cfg.interval = v;
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\" APP_NAME);
    cfg_save();
}

void cfg_load(void)
{
    cfg_path();
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0,
        OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) {
        migrate();
        return;
    }
    char buf[4096];
    DWORD n = 0;
    ReadFile(f, buf, sizeof buf - 1, &n, 0);
    CloseHandle(f);
    buf[n] = 0;

    cfg.greeted = num_at(find(buf, "\"greeted\""), 0) ? 1 : 0;
    int iv = num_at(find(buf, "\"interval\""), 20);
    if (iv >= 1 && iv <= 1440)
        cfg.interval = iv;

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
}

void cfg_save(void)
{
    cfg_path();
    char buf[4096];
    int n = sprintf(buf, "{\n  \"greeted\": %d,\n  \"interval\": %d,\n  \"timers\": [",
        cfg.greeted, cfg.interval);
    for (int i = 0; i < cfg.ntimers; i++) {
        char lab[176];
        int m = 0;
        char u8[160];
        WideCharToMultiByte(CP_UTF8, 0, cfg.timers[i].label, -1, u8, sizeof u8, 0, 0);
        for (int j = 0; u8[j] && m < 172; j++) {
            if (u8[j] == '"' || u8[j] == '\\')
                lab[m++] = '\\';
            lab[m++] = u8[j];
        }
        lab[m] = 0;
        n += sprintf(buf + n, "%s\n    {\"label\": \"%s\", \"minutes\": %d}",
            i ? "," : "", lab, cfg.timers[i].min);
    }
    n += sprintf(buf + n, "%s]\n}\n", cfg.ntimers ? "\n  " : "");

    HANDLE f = CreateFileW(tmp_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD wr;
    WriteFile(f, buf, n, &wr, 0);
    CloseHandle(f);
    MoveFileExW(tmp_path, path, MOVEFILE_REPLACE_EXISTING);
}
