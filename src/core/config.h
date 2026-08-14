#pragma once

#define CFG_MAX_TIMERS 8

struct ctimer {
    wchar_t label[40];
    int min;
};

struct config {
    int greeted;
    int interval;
    int ntimers;
    ctimer timers[CFG_MAX_TIMERS];
};

extern config cfg;

void cfg_load(void);
void cfg_save(void);
