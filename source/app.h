/* app.h - moving between screens, and what they share (the album being
 * looked at, the photo picked for a game). */
#pragma once
#include <stdbool.h>

typedef enum {
    SCR_MENU, SCR_ALBUM, SCR_VIEWER, SCR_SLIDESETUP, SCR_SLIDESHOW, SCR_TRANSFER,
    SCR_PLAYTIME, SCR_PUZZLE, SCR_DOODLE, SCR_LOOKS, SCR_COUNT
} ScreenId;

/* Each screen: enter (with an argument), update (input, once a frame),
 * draw. `open` is seconds since the screen appeared (screens opened from the
 * cube swirl their parts into place while it's small). */
typedef struct {
    void (*enter)(int arg);
    void (*update)(void);
    void (*draw)(float open);
    void (*leave)(void);
} Screen;

/* Switches screen with a short cross-fade. */
void app_go(ScreenId s, int arg);
/* Back to the cube, showing the side the screen belongs to. */
void app_back_to_cube(int side);
ScreenId app_screen(void);
const Screen *app_screen_def(ScreenId s);
void app_adopt(ScreenId s, float already_open);
void app_init(void);
/* Starts the launcher, or whatever Exit is set to (false, with a message shown, if it couldn't). */
const char *app_exit_label(void);  /* "Exit to Launcher", "Restart"... */
bool app_exit(void);                 /* Settings > Exit: start the chosen .dol, or restart */
void app_frame(void);

/* The current album view: photo indexes in display order. */
#define VIEW_MAX 4000
extern int g_view[VIEW_MAX];
extern int g_view_count;
extern int g_view_pos;            /* selected position in g_view */
extern char g_view_name[48];      /* "2026", "Favorites", "100CANON"... */

/* Album picker mode (choosing a photo for a game). */
enum { PICK_NONE = 0, PICK_PUZZLE = SCR_PUZZLE, PICK_DOODLE = SCR_DOODLE, PICK_LOOKS = SCR_LOOKS };
extern int g_pick_for;

/* Screens (defined in their own files). */
extern const Screen screen_menu, screen_album, screen_viewer, screen_slidesetup, screen_slideshow,
    screen_transfer, screen_playtime, screen_puzzle, screen_doodle, screen_looks;

/* Draws one photo's thumbnail filling a w x h cell (cropped to fit). */
void app_draw_thumb(int photo, float x, float y, float w, float h, float alpha);
/* Asks for the thumbnails around position pos of the view. */
void app_prefetch_view(int pos, int ahead);

/* Settings overlay (START on the cube). */
bool settings_panel_active(void);
void settings_panel_open(void);
void settings_panel_update(void);
void settings_panel_draw(void);

/* The Software Update window (Settings, or Z on the cube). */
bool update_panel_active(void);
void update_panel_open(bool check_now);
void update_panel_update(void);
void update_panel_draw(void);

/* A message shown for a few seconds at the bottom of the screen. */
void app_toast(const char *msg);
void app_draw_toast(void);
