#pragma once

void dd_open(HWND owner, RECT anchor, const wchar_t **labels, int n, int sel, int token);
void dd_close(void);
int dd_is_open(void);
