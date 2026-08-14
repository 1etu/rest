#pragma once

enum {
    T_WORK,
    T_HOLD,
    T_GRACE,
    T_BREAK,
};

void timer_init(HWND w);
void timer_tick(void);
void timer_pause_toggle(void);
void timer_skip(void);
void timer_break_now(void);
void timer_set_interval(int min);
void timer_reload(void);
int timer_interval(void);
int timer_seconds_left(void);
int timer_paused(void);
int timer_on_break(void);
int timer_state(void);
int timer_hold_reason(void);
