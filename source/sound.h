/* sound.h - menu sounds (made at start-up, see sfxsynth.c) played on the
 * DSP's hardware voices. */
#pragma once
#include "sfxsynth.h"

void sound_init(void);
void sound_play(Sfx s);
void sound_shutdown(void);   /* before launching another program */
