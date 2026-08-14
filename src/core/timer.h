#pragma once

void timer_init(HWND w);
void timer_tick(void);
void timer_pause_toggle(void);
void timer_skip(void);
void timer_break_now(void);
void timer_set_interval(int min);
int timer_interval(void);
int timer_minutes_left(void);
int timer_paused(void);
int timer_on_break(void);
