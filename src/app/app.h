#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define APP_NAME L"rest"
#define APP_ACCENT RGB(233, 138, 60)

#define WM_TRAY (WM_APP + 1)

enum {
    CMD_QUIT = 1,
    CMD_PAUSE,
    CMD_BREAK,
    CMD_SKIP,
    CMD_SETTINGS,
    CMD_LOGIN,
    CMD_INTERVAL,
    CMD_NEW_TIMER,
};
