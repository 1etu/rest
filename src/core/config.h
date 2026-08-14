#pragma once

#define CFG_MAX_TIMERS 8
#define CFG_MAX_APPS 10

struct ctimer {
    wchar_t label[40];
    int min;
};

struct config {
    int greeted;
    int interval;
    int brk;
    wchar_t style[16];
    wchar_t pos[16];

    int smart;
    int on_fullscreen;
    int on_meeting;
    int on_focus;
    int grace;
    int max_hold;
    int idle;

    int sound;
    int volume;
    int warn_secs;
    wchar_t s_start[64];
    wchar_t s_end[64];
    wchar_t s_warn[64];

    int hours;
    int from_min;
    int to_min;
    int days;

    int login;

    int hk_break;
    int hk_skip;
    int hk_pause;

    int nfocus;
    wchar_t focus[CFG_MAX_APPS][40];
    int nexcl;
    wchar_t excl[CFG_MAX_APPS][40];
    int ntimers;
    ctimer timers[CFG_MAX_TIMERS];
};

extern config cfg;

void cfg_load(void);
void cfg_save(void);
