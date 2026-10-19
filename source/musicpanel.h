/* musicpanel.h - the music player panel (see musicpanel.c). */
#pragma once
#include <stdbool.h>
bool music_panel_active(void);
void music_panel_open(void);
void music_panel_close(void);
void music_panel_update(void);
void music_panel_draw(void);
