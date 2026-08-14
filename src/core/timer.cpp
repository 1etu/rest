#include "app/app.h"
#include "core/timer.h"
#include "core/config.h"
#include "core/presence.h"
#include "shell/tray.h"
#include "notify/notify.h"
#include "notify/sound.h"

static int state = T_WORK;
static int secs = 20 * 60;
static int held, reason, paused;

static int work_hours_ok(void)
{
    if (!cfg.hours)
        return 1;
    SYSTEMTIME st;
    GetLocalTime(&st);
    int bit = st.wDayOfWeek ? st.wDayOfWeek - 1 : 6;
    if (!(cfg.days & 1 << bit))
        return 0;
    int m = st.wHour * 60 + st.wMinute;
    if (cfg.from_min <= cfg.to_min)
        return m >= cfg.from_min && m < cfg.to_min;
    return m >= cfg.from_min || m < cfg.to_min;
}

static void go_work(void)
{
    state = T_WORK;
    secs = cfg.interval * 60;
    held = 0;
    reason = 0;
}

static void start_break(void)
{
    state = T_BREAK;
    secs = cfg.brk;
    held = 0;
    reason = 0;
    tray_set_break(1);
    notify_break(1);
    sound_play(cfg.s_start);
}

static void end_break(void)
{
    tray_set_break(0);
    notify_break(0);
    sound_play(cfg.s_end);
    go_work();
}

static void count_down(void)
{
    secs--;
    if (cfg.warn_secs && secs == cfg.warn_secs)
        sound_play(cfg.s_warn);
}

void timer_init(HWND w)
{
    go_work();
    SetTimer(w, 1, 1000, 0);
}

void timer_tick(void)
{
    if (paused)
        return;

    if (state == T_BREAK) {
        if (--secs <= 0)
            end_break();
        return;
    }

    if (cfg.idle && presence_idle_secs() >= cfg.idle * 60) {
        go_work();
        return;
    }

    if (!work_hours_ok())
        return;

    if (state == T_WORK) {
        if (secs > 0) {
            count_down();
            return;
        }
        reason = cfg.smart ? presence_busy() : 0;
        if (reason) {
            state = T_HOLD;
            held = 0;
            return;
        }
        start_break();
        return;
    }

    if (++held >= cfg.max_hold * 60) {
        start_break();
        return;
    }

    if (state == T_HOLD) {
        reason = presence_busy();
        if (!reason) {
            state = T_GRACE;
            secs = cfg.grace * 60;
            if (secs <= 0)
                start_break();
        }
        return;
    }

    reason = cfg.smart ? presence_busy() : 0;
    if (reason) {
        state = T_HOLD;
        return;
    }
    count_down();
    if (secs <= 0)
        start_break();
}

void timer_pause_toggle(void)
{
    paused = !paused;
}

void timer_skip(void)
{
    if (state == T_BREAK) {
        tray_set_break(0);
        notify_break(0);
    }
    go_work();
}

void timer_break_now(void)
{
    paused = 0;
    if (state != T_BREAK)
        start_break();
}

void timer_set_interval(int min)
{
    cfg.interval = min;
    cfg_save();
    if (state == T_WORK)
        secs = cfg.interval * 60;
}

void timer_reload(void)
{
    if (state == T_WORK && secs > cfg.interval * 60)
        secs = cfg.interval * 60;
}

int timer_interval(void)
{
    return cfg.interval;
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
    return state == T_BREAK;
}

int timer_state(void)
{
    return state;
}

int timer_hold_reason(void)
{
    return reason;
}
