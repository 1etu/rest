#include "app/app.h"
#include "core/presence.h"
#include "core/config.h"
#include <shlobj.h>

#define CONSENT L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager" \
    L"\\ConsentStore\\"
#define CACHE_MS 2000

static int excluded(const wchar_t *sub)
{
    const wchar_t *name = sub;
    for (const wchar_t *s = sub; *s; s++)
        if (*s == L'#' || *s == L'\\')
            name = s + 1;
    for (int i = 0; i < cfg.nexcl; i++)
        if (!lstrcmpiW(name, cfg.excl[i]))
            return 1;
    return 0;
}

static int in_use(HKEY parent, const wchar_t *sub)
{
    if (excluded(sub))
        return 0;
    HKEY k;
    if (RegOpenKeyExW(parent, sub, 0, KEY_READ, &k))
        return 0;
    ULONGLONG stop = 1;
    DWORD n = sizeof stop;
    int used = !RegGetValueW(k, 0, L"LastUsedTimeStop", RRF_RT_QWORD, 0, &stop, &n) && !stop;
    RegCloseKey(k);
    return used;
}

static int any_in_use(HKEY parent, int depth)
{
    wchar_t name[256];
    DWORD i = 0, n = 256;
    while (!RegEnumKeyExW(parent, i++, name, &n, 0, 0, 0, 0)) {
        n = 256;
        if (in_use(parent, name))
            return 1;
        if (depth) {
            HKEY sub;
            if (!RegOpenKeyExW(parent, name, 0, KEY_READ, &sub)) {
                int r = any_in_use(sub, 0);
                RegCloseKey(sub);
                if (r)
                    return 1;
            }
        }
    }
    return 0;
}

static int capture_active(const wchar_t *device)
{
    wchar_t path[160];
    lstrcpyW(path, CONSENT);
    lstrcatW(path, device);
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k))
        return 0;
    int r = any_in_use(k, 1);
    RegCloseKey(k);
    return r;
}

static int foreground_is_focus_app(void)
{
    if (!cfg.nfocus)
        return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (!pid)
        return 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p)
        return 0;
    wchar_t path[MAX_PATH];
    DWORD n = MAX_PATH;
    int ok = QueryFullProcessImageNameW(p, 0, path, &n);
    CloseHandle(p);
    if (!ok)
        return 0;
    const wchar_t *exe = path;
    for (const wchar_t *s = path; *s; s++)
        if (*s == L'\\')
            exe = s + 1;
    for (int i = 0; i < cfg.nfocus; i++)
        if (!lstrcmpiW(exe, cfg.focus[i]))
            return 1;
    return 0;
}

int presence_busy(void)
{
    static DWORD stamp;
    static int cached;
    DWORD now = GetTickCount();
    if (stamp && now - stamp < CACHE_MS)
        return cached;
    stamp = now;

    int b = 0;
    QUERY_USER_NOTIFICATION_STATE s;
    if (cfg.on_fullscreen && !SHQueryUserNotificationState(&s)) {
        if (s == QUNS_BUSY || s == QUNS_RUNNING_D3D_FULL_SCREEN ||
            s == QUNS_PRESENTATION_MODE || s == QUNS_APP)
            b |= BUSY_FULLSCREEN;
        if (s == QUNS_QUIET_TIME)
            b |= BUSY_QUIET;
    }
    if (cfg.on_meeting && (capture_active(L"microphone") || capture_active(L"webcam")))
        b |= BUSY_MEETING;
    if (cfg.on_focus && foreground_is_focus_app())
        b |= BUSY_FOCUS_APP;

    cached = b;
    return b;
}

int presence_idle_secs(void)
{
    LASTINPUTINFO li = {sizeof li};
    if (!GetLastInputInfo(&li))
        return 0;
    return (GetTickCount() - li.dwTime) / 1000;
}
