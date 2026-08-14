#include "app.h"
#include "tray.h"

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
            tray_menu(w);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == CMD_QUIT)
            DestroyWindow(w);
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

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
