#include "loc/strings.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const wchar_t *en[STR_COUNT] = {
    L"Next break in 20 min",
    L"Pause",
    L"Take a break now",
    L"Skip next break",
    L"Settings…",
    L"Launch at login",
    L"Quit",
};

const wchar_t *str(int id)
{
    return en[id];
}
