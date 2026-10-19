/* settings.h - Flashcube's settings, kept in sd:/flashcube/settings.ini.
 * Field names shared with CubeBrowser's modules (music player, sounds) are
 * kept the same so that code is reused unchanged. */
#pragma once
#include <stdbool.h>

typedef struct {
    /* Network (static address only by editing settings.ini) */
    int  dhcp;
    char ip[16], netmask[16], gateway[16], dns[16];
    int  http_port;
    /* Display */
    int  video_mode, widescreen, full_width;
    int  margin;           /* screen margin for overscan, percent (0..12) */
    int  intro;            /* play the opening animation */
    /* Sound */
    int  sounds, volume;
    char music_sf2[256];   /* "" = first .sf2 in sd:/midi, "builtin", or a file */
    int  music_volume, music_shuffle, music_repeat;
    /* Slideshow */
    int  ss_transition;    /* SS_FADE, SS_PANZOOM, SS_CUBE */
    int  ss_seconds;       /* 3..20 */
    int  ss_shuffle;
    int  ss_music;         /* play the music folder during slideshows */
    /* Album */
    int  sort_newest;      /* newest photos first */
    /* Leaving */
    char swiss_dol[256];   /* Swiss, started by "Exit to Swiss" (found on the card if moved) */
} Settings;

enum { SS_FADE, SS_PANZOOM, SS_CUBE };

extern Settings g_set;

void settings_defaults(Settings *s);
bool settings_load(void);
bool settings_save(void);
