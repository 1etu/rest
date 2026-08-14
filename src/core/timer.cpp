#include "app/app.h"
#include "core/timer.h"
#include "core/config.h"
#include "shell/tray.h"
#include "notify/notify.h"

#define BREAK_SECS 20

static int interval_min = 20;
static int secs = 20 * 60;
static int paused, on_break;

void timer_init(HWND w)
{
    interval_min = cfg.interval;
    secs = interval_min * 60;
    SetTimer(w, 1, 1000, 0);
}

void timer_tick(void)
{
    if (paused)
        return;
    if (--secs > 0)
        return;
    on_break = !on_break;
    secs = on_break ? BREAK_SECS : interval_min * 60;
    tray_set_break(on_break);
    notify_break(on_break);
}

void timer_pause_toggle(void)
{
    paused = !paused;
}

void timer_skip(void)
{
    on_break = 0;
    secs = interval_min * 60;
    tray_set_break(0);
    notify_break(0);
}

void timer_break_now(void)
{
    paused = 0;
    on_break = 1;
    secs = BREAK_SECS;
    tray_set_break(1);
    notify_break(1);
}

void timer_set_interval(int min)
{
    interval_min = min;
    cfg.interval = min;
    cfg_save();
    if (!on_break)
        secs = interval_min * 60;
}

int timer_interval(void)
{
    return interval_min;
}

int timer_seconds_left(void)
{
    return secs;
}

int timer_paused(void)
{
    return paused;
}

int timer_on_break(void)
{
    return on_break;
}
