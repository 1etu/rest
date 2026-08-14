#include "app/app.h"
#include "core/timer.h"
#include "shell/tray.h"
#include "shell/menu.h"

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
            menu_show(w);
        }
        return 0;
    case WM_TIMER:
        timer_tick();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case CMD_QUIT:
            DestroyWindow(w);
            break;
        case CMD_PAUSE:
            timer_pause_toggle();
            break;
        case CMD_BREAK:
            timer_break_now();
            break;
        case CMD_SKIP:
            timer_skip();
            break;
        case CMD_INTERVAL:
            timer_set_interval(HIWORD(wp));
            break;
        }
        return 0;
    case WM_DESTROY:
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = APP_NAME L"_main";
    RegisterClassW(&wc);
    HWND w = CreateWindowW(wc.lpszClassName, APP_NAME, 0, 0, 0, 0, 0, 0, 0, inst, 0);
    tray_add(w);
    timer_init(w);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
