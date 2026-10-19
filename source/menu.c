/* menu.c - the cube screen: the opening animation, the cube menu, turning
 * to a side, and opening a section (the cube shrinks away and the section's
 * parts swirl in, as the GameCube's memory card screen does). */
#include "app.h"
#include "cube.h"
#include "logo.h"
#include "ui.h"
#include "input.h"
#include "photos.h"
#include "settings.h"
#include "sound.h"
#include "net.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>

enum { M_INTRO, M_NAV, M_TURN_IN, M_CHOSEN, M_TURN_OUT, M_OPENING, M_RETURN };

static int state;
static float st;               /* seconds in this state */
static float intro_t;
static CubeView cv;
static int side, queued_side = -1;
static float icon_clock;
static float exit_armed_until;
static bool intro_skip, start_played;

#define TURN_IN_S 0.95f
#define TURN_OUT_S 0.8f
#define OPEN_SWIRL_AT 0.55f

static const ScreenId side_screen[SIDE_COUNT] = { SCR_ALBUM, SCR_SLIDESETUP, SCR_TRANSFER, SCR_PLAYTIME };

static int dir_to_side(int d) {
    switch (d) {
    case DIR_UP: return SIDE_ALBUM;
    case DIR_DOWN: return SIDE_SLIDESHOW;
    case DIR_LEFT: return SIDE_TRANSFER;
    case DIR_RIGHT: return SIDE_PLAYTIME;
    default: return -1;
    }
}

static int opposite(int s) {
    switch (s) {
    case SIDE_ALBUM: return SIDE_SLIDESHOW;
    case SIDE_SLIDESHOW: return SIDE_ALBUM;
    case SIDE_TRANSFER: return SIDE_PLAYTIME;
    default: return SIDE_TRANSFER;
    }
}

static void set_state(int s) { state = s; st = 0; }

static void fill_pages(void) {
    CubePage *p = &cv.pages[SIDE_ALBUM];
    p->title = "Album";
    p->nbands = 1;
    p->band_label[0] = "Photos";
    if (photos_scanning()) snprintf(p->band_value[0], sizeof(p->band_value[0]), "...");
    else snprintf(p->band_value[0], sizeof(p->band_value[0]), "%d", photos_count());

    p = &cv.pages[SIDE_SLIDESHOW];
    p->title = "Slideshow";
    p->nbands = 2;
    p->band_label[0] = "Each Photo";
    snprintf(p->band_value[0], sizeof(p->band_value[0]), "%d sec", g_set.ss_seconds);
    p->band_label[1] = "Music";
    snprintf(p->band_value[1], sizeof(p->band_value[1]), "%s", g_set.ss_music ? "On" : "Off");

    p = &cv.pages[SIDE_TRANSFER];
    p->title = "Transfer";
    p->nbands = 2;
    p->band_label[0] = "Address";
    snprintf(p->band_value[0], sizeof(p->band_value[0]), "%s", net_state() == NET_UP ? net_ip() : "-");
    p->band_label[1] = "Status";
    const char *s = "Off";
    if (net_state() == NET_STARTING) s = "Starting";
    else if (net_state() == NET_UP) s = "Ready";
    else if (net_state() == NET_FAILED) s = "No link";
    snprintf(p->band_value[1], sizeof(p->band_value[1]), "%s", s);

    p = &cv.pages[SIDE_PLAYTIME];
    p->title = "Playtime";
    p->nbands = 3;
    p->band_label[0] = "Photo Puzzle";
    p->band_label[1] = "Doodle";
    p->band_label[2] = "Color Looks";
    for (int i = 0; i < 3; i++) p->band_value[i][0] = 0;
}

static void menu_enter(int arg) {
    if (!cv.scale) cube_view_init(&cv);
    if (arg >= 100) {
        /* back from a section: the cube returns showing that side */
        side = arg - 100;
        cv.side = side;
        cv.turn = 1;
        cv.labels = 0;
        cv.icon = 1;
        cv.collapse = 0;
        set_state(M_RETURN);
        return;
    }
    cube_view_init(&cv);
    intro_t = 0;
    set_state(M_INTRO);
    intro_skip = !g_set.intro;
    start_played = false;   /* no logo: the cube just fades in */
}

static void start_turn(int s) {
    side = s;
    cv.side = s;
    icon_clock = 0;
    set_state(M_TURN_IN);
    sound_play(SFX_TURN_SIDE);
}

static void menu_update(void) {
    st += g_dt;
    cv.time += g_dt;
    icon_clock += g_dt;
    fill_pages();
    if (settings_panel_active()) { settings_panel_update(); return; }

    switch (state) {
    case M_INTRO:
        intro_t += g_dt;
        if (input_pressed(BTN_A | BTN_START | BTN_B)) intro_t = logo_duration();
        if (!start_played && intro_t >= (intro_skip ? 0.1f : 5.4f)) { start_played = true; sound_play(SFX_START); }
        if (intro_t >= (intro_skip ? 1.0f : logo_duration())) set_state(M_NAV);
        break;
    case M_NAV: {
        int s = dir_to_side(input_stick_dir());
        if (s < 0) s = dir_to_side(input_dir());
        if (s >= 0) start_turn(s);
        else if (input_pressed(BTN_START)) { sound_play(SFX_SELECT); settings_panel_open(); }
        else if (input_pressed(BTN_B)) {
            /* B twice: back to Swiss */
            if (g_time < exit_armed_until) { exit_armed_until = 0; app_exit_to_swiss(); }
            else { exit_armed_until = g_time + 3; sound_play(SFX_BACK); app_toast("Press B again to exit to Swiss"); }
        }
        break;
    }
    case M_TURN_IN:
        if (st >= TURN_IN_S) set_state(M_CHOSEN);
        break;
    case M_CHOSEN: {
        int s = dir_to_side(input_dir());
        if (input_pressed(BTN_A)) {
            sound_play(SFX_OPEN);
            const Screen *scr = app_screen_def(side_screen[side]);
            g_pick_for = PICK_NONE;
            scr->enter(0);
            set_state(M_OPENING);
        } else if (input_pressed(BTN_B) || (s >= 0 && s == opposite(side))) {
            sound_play(SFX_TURN_CENTER);
            queued_side = -1;
            set_state(M_TURN_OUT);
        } else if (s >= 0 && s != side) {
            queued_side = s;          /* turn back, then on to the other side */
            sound_play(SFX_TURN_CENTER);
            set_state(M_TURN_OUT);
        }
        break;
    }
    case M_TURN_OUT:
        if (st >= TURN_OUT_S) {
            if (queued_side >= 0) { int q = queued_side; queued_side = -1; start_turn(q); }
            else set_state(M_NAV);
        }
        break;
    case M_OPENING: {
        ScreenId target = side_screen[side];
        float open = st - OPEN_SWIRL_AT;
        if (open > 0.4f) {
            /* the section takes over (its parts may still be settling) */
            app_adopt(target, open);
            return;
        }
        break;
    }
    case M_RETURN:
        if (st >= 0.45f) set_state(M_CHOSEN);
        else if (input_pressed(BTN_B)) { sound_play(SFX_TURN_CENTER); set_state(M_TURN_OUT); queued_side = -1; }
        break;
    }
}

static void menu_draw(float open) {
    (void)open;
    ui_background_cube();
    float hints = 1;
    cv.lit = -1;
    cv.scale = 1;
    cv.alpha = 1;
    cv.collapse = 0;
    cv.icon_time = icon_clock;
    switch (state) {
    case M_INTRO:
        cv.turn = 0;
        cv.icon = 0;
        if (intro_skip) {
            cv.alpha = ease_out(clamp01(intro_t / 0.5f));
            cv.scale = 0.92f + 0.08f * cv.alpha;
            cv.labels = clamp01((intro_t - 0.3f) / 0.4f);
            cv.sway = 1;
            hints = clamp01((intro_t - 0.5f) / 0.4f);
            break;
        }
        logo_draw(intro_t);
        cv.alpha = logo_nav_alpha(intro_t);
        cv.labels = logo_labels_alpha(intro_t);
        cv.sway = clamp01((intro_t - 6.2f) / 2.0f);
        hints = logo_hints_alpha(intro_t);
        break;
    case M_NAV: {
        cv.turn = 0;
        cv.labels = 1;
        cv.icon = 0;
        cv.sway = 1;
        cv.lit = dir_to_side(input_stick_dir());
        break;
    }
    case M_TURN_IN: {
        float k = ease_bezier(clamp01(st / TURN_IN_S), 0.55f, 0, 0.3f, 1);
        cv.turn = k;
        cv.labels = 1 - clamp01(st / (TURN_IN_S * 0.4f));
        cv.lit = side;
        cv.icon = clamp01((st - TURN_IN_S * 0.55f) / (TURN_IN_S * 0.35f));
        hints = 1;
        break;
    }
    case M_CHOSEN:
        cv.turn = 1;
        cv.labels = 0;
        cv.icon = 1;
        break;
    case M_TURN_OUT: {
        float k = ease_bezier(clamp01(st / TURN_OUT_S), 0.55f, 0, 0.3f, 1);
        cv.turn = 1 - k;
        cv.icon = 1 - clamp01(st / (TURN_OUT_S * 0.3f));
        cv.labels = clamp01((st - TURN_OUT_S * 0.6f) / (TURN_OUT_S * 0.4f));
        break;
    }
    case M_OPENING: {
        cv.turn = 1;
        cv.labels = 0;
        cv.icon = 1;
        cv.collapse = clamp01(st / 0.6f);
        float k = ease_bezier(clamp01((st - 0.05f) / 0.75f), 0.5f, 0, 0.3f, 1);
        cv.scale = 1 - 0.2f * k;
        cv.alpha = 1 - k;
        hints = 1 - clamp01(st / 0.3f);
        break;
    }
    case M_RETURN: {
        float k = ease_bezier(clamp01(st / 0.45f), 0.2f, 0.6f, 0.3f, 1);
        cv.turn = 1;
        cv.labels = 0;
        cv.icon = k;
        cv.scale = 0.85f + 0.15f * k;
        cv.alpha = k;
        hints = k;
        break;
    }
    }
    cube_draw(&cv);

    /* hints along the bottom */
    if (settings_panel_active()) hints = 0;
    if (state == M_INTRO || state == M_NAV || (state == M_TURN_OUT && queued_side < 0 && st > TURN_OUT_S * 0.5f)) {
        int d = input_stick_dir();
        if (d < 0) {   /* demonstrate: the knob nods up, right, down, left */
            float c = fmodf(g_time, 4.0f);
            d = c < 0.2f ? -1 : c < 0.9f ? DIR_UP : c < 1.2f ? -1 : c < 1.9f ? DIR_RIGHT : c < 2.2f ? -1 : c < 2.9f ? DIR_DOWN
                : c < 3.2f ? -1 : c < 3.9f ? DIR_LEFT : -1;
        }
        const char *h[] = { "S:Menu Selection", "ST:Settings", "B:Exit to Swiss" };
        ui_hints_center(h, state == M_NAV ? 3 : 1, 480 - 34 - 13, 36, hints, d, true);
    } else {
        const char *h[] = { "B:Cancel", "A:Confirm" };
        ui_hints_center(h, 2, 480 - 34 - 13, 34, hints, -1, true);
    }

    /* the section swirling in */
    if (state == M_OPENING && st > OPEN_SWIRL_AT) {
        const Screen *scr = app_screen_def(side_screen[side]);
        scr->draw(st - OPEN_SWIRL_AT);
    }
    if (settings_panel_active()) settings_panel_draw();
}

const Screen screen_menu = { menu_enter, menu_update, menu_draw, NULL };
