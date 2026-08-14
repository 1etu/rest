#pragma once

enum {
    BUSY_FULLSCREEN = 1,
    BUSY_MEETING = 2,
    BUSY_QUIET = 4,
    BUSY_FOCUS_APP = 8,
};

int presence_busy(void);
int presence_idle_secs(void);
