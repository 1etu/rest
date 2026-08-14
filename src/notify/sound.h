#pragma once

void sound_play(const wchar_t *id);
void sound_stop(void);
const wchar_t *const *sound_builtins(int *count);
