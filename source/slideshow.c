/* slideshow.c - the slideshow's settings screen and the show itself:
 * photos one after another with a fade, a slow pan and zoom, or a turning
 * cube, with music from the sd:/midi folder (CubeBrowser's music player). */
#include "app.h"
#include "photos.h"
#include "picture.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "settings.h"
#include "midi.h"
#include "musicpanel.h"
#include "cube.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ the list */

static int list[VIEW_MAX];
static int nlist;
static char list_name[48];
static int source;           /* 0 the album view, 1 all, 2 favorites, 3.. years */
static int years[8], nyears;
static int song = -1;        /* -1 all songs, -2 no music, else a song */

static void make_list(void) {
    if (source == 0 && g_view_count > 0) {
        memcpy(list, g_view, sizeof(int) * g_view_count);
        nlist = g_view_count;
        snprintf(list_name, sizeof(list_name), "%s", g_view_name);
    } else if (source <= 1) {
        nlist = photos_view(VIEW_ALL, 0, list, VIEW_MAX);
        snprintf(list_name, sizeof(list_name), "All");
    } else if (source == 2) {
        nlist = photos_view(VIEW_FAVORITES, 0, list, VIEW_MAX);
        snprintf(list_name, sizeof(list_name), "Favorites");
    } else {
        nlist = photos_view(VIEW_YEAR, years[source - 3], list, VIEW_MAX);
        snprintf(list_name, sizeof(list_name), "%d", years[source - 3]);
    }
}

/* ------------------------------------------------------------ settings */

enum { R_TRANSITION, R_SECONDS, R_ORDER, R_MUSIC, R_PHOTOS, R_BUTTONS, NR };
static int srow, sbtn;
static bool from_album;
static float sflash;

static void setup_enter(int arg) {
    from_album = arg == 1;
    nyears = photos_years(years, 8);
    source = from_album ? 0 : 1;
    make_list();
    midi_scan();
    if (!g_set.ss_music) song = -2;
    else if (song >= midi_count()) song = -1;
    srow = R_BUTTONS;
    sbtn = 0;
}

static void change(int d) {
    switch (srow) {
    case R_TRANSITION: g_set.ss_transition = (g_set.ss_transition + d + 3) % 3; break;
    case R_SECONDS:
        g_set.ss_seconds += d;
        if (g_set.ss_seconds < 3) g_set.ss_seconds = 3;
        if (g_set.ss_seconds > 20) g_set.ss_seconds = 20;
        break;
    case R_ORDER: g_set.ss_shuffle = !g_set.ss_shuffle; break;
    case R_MUSIC: {
        int n = midi_count();
        /* off, all songs, then each song */
        int pos = song == -2 ? 0 : song == -1 ? 1 : song + 2;
        pos = (pos + d + n + 2) % (n + 2);
        song = pos == 0 ? -2 : pos == 1 ? -1 : pos - 2;
        g_set.ss_music = song != -2;
        break;
    }
    case R_PHOTOS: {
        int count = 3 + nyears;
        do { source = (source + d + count) % count; } while (source == 0 && !from_album);
        make_list();
        break;
    }
    case R_BUTTONS: sbtn = !sbtn; break;
    }
    sound_play(SFX_HOVER);
}

static void setup_update(void) {
    sflash += g_dt;
    if (music_panel_active()) {
        music_panel_update();
        if (!music_panel_active()) {
            /* the panel may have changed the music: keep the row in step */
            midi_stop();
        }
        return;
    }
    if (input_pressed(BTN_A) && srow == R_MUSIC) { sound_play(SFX_SELECT); music_panel_open(); return; }
    int d = input_dir();
    if (d == DIR_UP && srow > 0) { srow--; sound_play(SFX_HOVER); }
    else if (d == DIR_DOWN && srow < NR - 1) { srow++; sound_play(SFX_HOVER); }
    else if (d == DIR_LEFT) change(-1);
    else if (d == DIR_RIGHT) change(1);
    if (input_pressed(BTN_A)) {
        if (srow == R_BUTTONS && sbtn == 1) { sound_play(SFX_BACK); goto back; }
        if (nlist == 0) { sound_play(SFX_ERROR); app_toast("There are no photos to show."); return; }
        sound_play(SFX_SELECT);
        settings_save();
        app_go(SCR_SLIDESHOW, 0);
        return;
    }
    if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        goto back;
    }
    return;
back:
    settings_save();
    if (from_album) app_go(SCR_ALBUM, 0);
    else app_back_to_cube(SIDE_SLIDESHOW);
}

static const char *row_help(void) {
    switch (srow) {
    case R_TRANSITION:
        return g_set.ss_transition == SS_FADE ? "Each photo fades into the next."
             : g_set.ss_transition == SS_PANZOOM ? "Each photo slowly drifts and zooms while it is shown."
             : "The photos turn like the sides of a cube.";
    case R_SECONDS: return "How long each photo stays on screen.";
    case R_ORDER: return g_set.ss_shuffle ? "Photos are shown in a random order." : "Photos are shown in the album's order.";
    case R_MUSIC: return midi_count() ? "Songs from the midi folder on the SD card. Press A for the music player."
                                      : "Put .mid songs in the midi folder on the SD card to hear music.";
    case R_PHOTOS: return "Which photos to show. Use left and right to choose.";
    default: return sbtn ? "Go back without starting." : "Start the slideshow. A pauses, B stops.";
    }
}

static void setup_draw(float open) {
    float X = UI_X;
    ui_background_grid(ramp(open, 0, 0.35f));
    float pa = ramp(open, 0.45f, 0.85f);
    float px = X + 40, py = 30, pw = UI_W - 80, ph = 370;
    ui_panel(px, py, pw, ph, pa);
    ui_ptitle(px, py, pw, "Slideshow", NULL, pa);
    int nitems = 7;
    float done = ui_swirl_duration(nitems);
    char vals[5][96];
    char songname[80];
    if (song == -2) snprintf(songname, sizeof(songname), "Off");
    else if (song == -1) snprintf(songname, sizeof(songname), midi_count() ? "All songs" : "No songs found");
    else {
        snprintf(songname, sizeof(songname), "%s", midi_file(song));
        if (strlen(songname) > 26) strcpy(songname + 23, "...");
    }
    snprintf(vals[1], 96, "\x81  %d seconds  \x82", g_set.ss_seconds);
    snprintf(vals[3], 96, "\x81  %s  \x82", songname);
    snprintf(vals[4], 96, "\x81  %s \x7f %d  \x82", list_name, nlist);
    static const char *labels[5] = { "Transition", "Each Photo", "Order", "Music", "Photos" };
    for (int i = 0; i < 5; i++) {
        float x = px + 8, y = py + 42 + i * 34, w = pw - 16, h = 30;
        float a = 1;
        bool sw = open < done;
        if (sw) a = ui_swirl_begin(i, nitems, x, y, w, h, open, 120, 12);
        Color tc = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a));
        ui_row(x, y, w, h, srow == i, a);
        ui_text(FONT_MENU, 14, 1.5f, x + 12, y, h, tc, labels[i]);
        if (i == R_TRANSITION) {
            static const char *opt[3] = { "Fade", "Pan & Zoom", "Cube" };
            float rx = x + w - 12;
            for (int k = 2; k >= 0; k--) {
                float tw = gfx_text_width_sp(FONT_MENU, 14, 1.5f, opt[k], -1);
                rx -= tw;
                ui_text(FONT_MENU, 14, 1.5f, rx, y, h, tc, opt[k]);
                rx -= 19;
                ui_radio(rx, y + h / 2, g_set.ss_transition == k, a);
                rx -= 16;
            }
        } else if (i == R_ORDER) {
            static const char *opt[2] = { "In Order", "Shuffle" };
            float rx = x + w - 12;
            for (int k = 1; k >= 0; k--) {
                float tw = gfx_text_width_sp(FONT_MENU, 14, 1.5f, opt[k], -1);
                rx -= tw;
                ui_text(FONT_MENU, 14, 1.5f, rx, y, h, tc, opt[k]);
                rx -= 19;
                ui_radio(rx, y + h / 2, g_set.ss_shuffle == k, a);
                rx -= 16;
            }
        } else {
            ui_text_right(FONT_MENU, 14, 1.5f, x + w - 12, y, h, i == R_PHOTOS ? rgba(0xc9, 0xd2, 0xee, (u8)(255 * a)) : tc, vals[i]);
        }
        if (sw) ui_swirl_end();
    }
    /* buttons */
    for (int b = 0; b < 2; b++) {
        float bw = b ? 98 : 108, x = px + pw / 2 - 138 + (b ? 178 : 0), y = py + ph - 64 - 28;
        float a = 1;
        bool sw = open < done;
        if (sw) a = ui_swirl_begin(5 + b, nitems, x, y, bw, 28, open, 120, 12);
        ui_button(x, y, bw, 28, b ? "Back" : "Start", srow == R_BUTTONS && sbtn == b, a);
        if (sw) ui_swirl_end();
    }
    float da = ramp(open, done - 0.5f, done - 0.1f);
    float dy = py + ph - 10 - 42 + 16 * (1 - ease_out(da));
    ui_desc(px + 10, dy, pw - 20, 42, da);
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, dy, pw - 44, 42, rgba(0xec, 0xeb, 0xff, (u8)(255 * da)), row_help());
    const char *h[] = { "S:Select", "B:Back", "A:Confirm" };
    ui_hints(h, 3, music_panel_active() ? 0 : ramp(open, done - 0.4f, done), input_stick_dir());
    music_panel_draw();
}

const Screen screen_slidesetup = { setup_enter, setup_update, setup_draw, NULL };

/* ------------------------------------------------------------ the show */

static PicRequest bufs[2];
static int cur_buf;
static int order[VIEW_MAX];
static int pos;               /* position in order[] being shown */
static float shown_t;         /* seconds the current photo has been fully shown */
static float trans_t = -1;    /* >= 0: changing to the next photo */
static bool paused;
static float hint_t;
static int back_to;           /* -1 setup, else the viewer at this view position */
static bool music_started;
#define TRANS_S 1.1f
#define SHOW_SIDE 1024

static void shuffle(void) {
    for (int i = nlist - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
}

static void request_at(int b, int p) {
    if (nlist == 0) return;
    p = ((p % nlist) + nlist) % nlist;
    photos_load_picture(&bufs[b], list[order[p]], SHOW_SIDE);
}

static void show_enter(int arg) {
    if (arg >= 1000) {
        /* started from the photo viewer: this album, from that photo */
        back_to = arg - 1000;
        memcpy(list, g_view, sizeof(int) * g_view_count);
        nlist = g_view_count;
    } else {
        back_to = -1;
        if (nlist == 0) make_list();
    }
    for (int i = 0; i < nlist; i++) order[i] = i;
    if (g_set.ss_shuffle) shuffle();
    pos = 0;
    if (back_to >= 0) {
        for (int i = 0; i < nlist; i++) if (order[i] == back_to) { pos = i; break; }
    }
    cur_buf = 0;
    request_at(0, pos);
    request_at(1, pos + 1);
    shown_t = 0;
    trans_t = -1;
    paused = false;
    hint_t = 0;
    music_started = false;
    if (g_set.ss_music && song != -2) {
        midi_scan();
        if (midi_count() > 0) {
            midi_set_shuffle(song == -1);
            midi_set_repeat(true);
            midi_play(song >= 0 ? song : rand() % midi_count());
            music_started = true;
        }
    }
}

static void show_leave(void) {
    photos_release_picture(&bufs[0]);
    photos_release_picture(&bufs[1]);
    music_panel_close();
    if (music_started || midi_state() == MIDI_PLAYING || midi_state() == MIDI_PAUSED) midi_stop();
}

static void advance(int d) {
    pos = ((pos + d) % nlist + nlist) % nlist;
    if (d > 0) {
        cur_buf ^= 1;
        request_at(cur_buf ^ 1, pos + 1);
    } else {
        /* going back: load the previous photo into the spare buffer and swap */
        request_at(cur_buf ^ 1, pos);
        cur_buf ^= 1;
        request_at(cur_buf ^ 1, pos + 1);
    }
    shown_t = 0;
}

static void show_update(void) {
    hint_t += g_dt;
    if (input_any() || input_stick_dir() != DIR_NONE) hint_t = 0;   /* any button brings the hints back */
    if (music_panel_active()) {
        music_panel_update();
        goto timing;
    }
    if (input_pressed(BTN_X)) { sound_play(SFX_SELECT); music_panel_open(); return; }
    if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        if (back_to >= 0) {
            g_view_pos = order[pos];
            app_go(SCR_VIEWER, 0);
        } else {
            app_go(SCR_SLIDESETUP, 0);
        }
        return;
    }
    if (input_pressed(BTN_A)) { paused = !paused; hint_t = 0; sound_play(SFX_SELECT); if (music_started) midi_toggle_pause(); }
    if (input_pressed(BTN_RIGHT) && trans_t < 0) { trans_t = 0; hint_t = 0; }
    if (input_pressed(BTN_LEFT) && trans_t < 0 && nlist > 1) { advance(-1); trans_t = -1; hint_t = 0; }
timing:
    if (nlist == 0) return;
    if (trans_t >= 0) {
        trans_t += g_dt;
        if (trans_t >= TRANS_S) { trans_t = -1; advance(1); }
        return;
    }
    if (bufs[cur_buf].state == 2 || bufs[cur_buf].state == 3) {
        if (!paused) shown_t += g_dt;
        int nb = cur_buf ^ 1;
        bool next_ready = bufs[nb].state == 2 || bufs[nb].state == 3;
        if (shown_t >= g_set.ss_seconds && next_ready && nlist > 1) trans_t = 0;
    }
}

/* the photo filling the screen (cover = fill, else fit) with a slow move */
static void draw_slide(PicRequest *r, float a, float life, bool cover, int which) {
    if (r->state != 2 || !pic_ok(&r->pic) || a <= 0) return;
    float W = gfx_width(), H = 480, pw = r->pic.w, ph = r->pic.h;
    float s = cover ? fmaxf(W / pw, H / ph) : fminf(W / pw, H / ph);
    float cx = W / 2, cy = H / 2;
    if (g_set.ss_transition == SS_PANZOOM) {
        float k = clamp01(life / (g_set.ss_seconds + 2 * TRANS_S));
        float z = 1.04f + 0.10f * ((which & 1) ? k : 1 - k);
        s *= z;
        float dx = (which & 2) ? 1 : -1;
        cx += dx * (k - 0.5f) * W * 0.04f;
        cy += ((which & 1) ? -1 : 1) * (k - 0.5f) * H * 0.03f;
    }
    pic_draw(&r->pic, cx - pw * s / 2, cy - ph * s / 2, pw * s, ph * s, 0, 0, pw, ph, rgba(255, 255, 255, (u8)(255 * a)));
}

static void draw_cube_turn(PicRequest *from, PicRequest *to, float k) {
    /* two faces of a cube the size of the screen, turning left */
    float W = gfx_width(), H = 480, half = W / 2;
    float ang = -90 * ease_inout(k);
    float pull = sinf((float)M_PI * k) * 160;   /* step back a little mid-turn */
    for (int f = 0; f < 2; f++) {
        PicRequest *r = f ? to : from;
        if (r->state != 2 || !pic_ok(&r->pic)) continue;
        M34 m = m34_mul(m34_translate(0, 0, -half - pull), m34_mul(m34_ry(ang + f * 90), m34_translate(-half, -H / 2, half)));
        gfx_begin_3d(m.m, W / 2, H / 2, 900);
        float pw = r->pic.w, ph = r->pic.h, s = fminf(W / pw, H / ph);
        gfx_rect(0, 0, W, H, rgba(0, 0, 0, 255));
        pic_draw(&r->pic, (W - pw * s) / 2, (H - ph * s) / 2, pw * s, ph * s, 0, 0, pw, ph, rgba(255, 255, 255, 255));
    }
    gfx_end_3d();
}

static void show_draw(float open) {
    (void)open;
    gfx_fill_screen(rgba(0, 0, 0, 255));
    PicRequest *cur = &bufs[cur_buf], *nxt = &bufs[cur_buf ^ 1];
    bool cover = g_set.ss_transition == SS_PANZOOM;
    float life = shown_t + TRANS_S;
    if (trans_t >= 0) {
        float k = clamp01(trans_t / TRANS_S);
        if (g_set.ss_transition == SS_CUBE) {
            draw_cube_turn(cur, nxt, k);
        } else {
            draw_slide(cur, 1, life + trans_t, cover, pos);
            draw_slide(nxt, ease_inout(k), trans_t, cover, pos + 1);
        }
    } else {
        draw_slide(cur, 1, life, cover, pos);
    }
    if (cur->state == 3) {
        ui_text_center(FONT_MENU, 14, 1.2f, gfx_width() / 2, 230, 24, rgba(0xdf, 0xe4, 0xff, 255), cur->why);
    } else if (cur->state != 2) {
        ui_spinner(gfx_width() / 2, 240, 14, g_time, 1);
    }
    if (nlist == 0) ui_text_center(FONT_MENU, 15, 1.2f, gfx_width() / 2, 230, 24, rgba(0xdf, 0xe4, 0xff, 255), "No photos to show.");
    /* the hints appear for a moment after starting or a button */
    float ha = paused ? 1 : clamp01((3.0f - hint_t) / 0.5f);
    if (ha > 0) {
        {   /* shade under the hints, out to the very edges of the TV picture */
            float fx, fy, fw, fh;
            gfx_full_area(&fx, &fy, &fw, &fh);
            gfx_rect_vgrad(fx, 392, fw, fy + fh - 392, rgba(0, 0, 0, 0), rgba(0, 0, 0, (u8)(170 * ha)));
        }
        const char *h[] = { "X:\x83 Music", "DP:Next", "B:Stop", paused ? "A:Play" : "A:Pause" };
        ui_hints(h, 4, music_panel_active() ? 0 : ha, -1);
        if (paused) ui_text(FONT_MENU, 14, 1.2f, UI_X + 24, 24, 24, rgba(255, 255, 255, 220), "Paused");
    }
    music_panel_draw();
}

const Screen screen_slideshow = { show_enter, show_update, show_draw, show_leave };
