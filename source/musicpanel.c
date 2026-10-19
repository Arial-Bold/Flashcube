/* musicpanel.c - the music player, as a panel over the slideshow (X) or the
 * slideshow settings: what's playing, previous / play-pause / next,
 * shuffle, repeat, volume, and which SoundFont (.sf2) plays the songs. */
#include "musicpanel.h"
#include "midi.h"
#include "settings.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { R_BUTTONS, R_SHUFFLE, R_REPEAT, R_VOLUME, R_SF2, NR };
static bool active;
static int row, button = 1;
static float t, flash;
#define MAX_SF2 24
static char sf2[MAX_SF2][256];   /* "" = automatic, "builtin", or a file */
static int nsf2;

bool music_panel_active(void) { return active; }
void music_panel_close(void) { if (active) { active = false; settings_save(); } }

static void scan_sf2(void) {
    nsf2 = 0;
    snprintf(sf2[nsf2++], 256, "%s", "");
    snprintf(sf2[nsf2++], 256, "%s", "builtin");
    DIR *d = opendir(MIDI_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && nsf2 < MAX_SF2) {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot || strcasecmp(dot, ".sf2")) continue;
        snprintf(sf2[nsf2++], 256, "%s/%s", MIDI_DIR, e->d_name);
    }
    closedir(d);
}

void music_panel_open(void) {
    active = true;
    t = 0;
    row = R_BUTTONS;
    button = 1;
    if (midi_count() == 0) midi_scan();
    scan_sf2();
}

static int sf2_index(void) {
    for (int i = 0; i < nsf2; i++) if (!strcmp(sf2[i], g_set.music_sf2)) return i;
    return 0;
}

static const char *sf2_label(int i, char *buf, size_t cap) {
    if (i == 0) return "Automatic";
    if (i == 1) return "Built-in";
    const char *n = strrchr(sf2[i], '/');
    snprintf(buf, cap, "%s", n ? n + 1 : sf2[i]);
    return buf;
}

static void press_button(int b) {
    if (midi_count() == 0) { sound_play(SFX_ERROR); return; }
    sound_play(SFX_SELECT);
    flash = 0.25f;
    if (b == 0) midi_prev();
    else if (b == 1) midi_toggle_pause();
    else midi_next();
}

void music_panel_update(void) {
    t += g_dt;
    if (flash > 0) flash -= g_dt;
    int d = input_dir();
    if (d == DIR_UP && row > 0) { row--; sound_play(SFX_HOVER); }
    else if (d == DIR_DOWN && row < NR - 1) { row++; sound_play(SFX_HOVER); }
    else if (d == DIR_LEFT || d == DIR_RIGHT) {
        int k = d == DIR_LEFT ? -1 : 1;
        switch (row) {
        case R_BUTTONS: button += k; if (button < 0) button = 0; if (button > 2) button = 2; break;
        case R_SHUFFLE: midi_set_shuffle(!midi_shuffle()); break;
        case R_REPEAT: midi_set_repeat(!midi_repeat()); break;
        case R_VOLUME:
            g_set.music_volume += k;
            if (g_set.music_volume < 0) g_set.music_volume = 0;
            if (g_set.music_volume > 10) g_set.music_volume = 10;
            break;
        case R_SF2: {
            int i = (sf2_index() + k + nsf2) % nsf2;
            snprintf(g_set.music_sf2, sizeof(g_set.music_sf2), "%s", sf2[i]);
            midi_instruments_changed();
            break;
        }
        }
        sound_play(SFX_HOVER);
    }
    if (input_pressed(BTN_A)) {
        if (row == R_BUTTONS) press_button(button);
        else if (row == R_SHUFFLE) { midi_set_shuffle(!midi_shuffle()); sound_play(SFX_SELECT); }
        else if (row == R_REPEAT) { midi_set_repeat(!midi_repeat()); sound_play(SFX_SELECT); }
    }
    if (input_pressed(BTN_B) || input_pressed(BTN_X)) {
        sound_play(SFX_CANCEL);
        settings_save();
        active = false;
    }
}

static void fmt_time(char *out, size_t cap, float s) {
    if (s < 0) s = 0;
    snprintf(out, cap, "%d:%02d", (int)s / 60, (int)s % 60);
}

/* round silver (or gold, when chosen) button with a drawn symbol */
static void round_button(float cx, float cy, float r, int kind, bool on, float a) {
    u8 A = (u8)(255 * a);
    if (on) gfx_circle(cx, cy, r + 5, rgba(240, 210, 80, (u8)(0.35f * A)));
    gfx_circle(cx, cy + 1, r + 2, rgba(0, 0, 0, (u8)(0.6f * A)));
    gfx_ring(cx, cy, r + 2, 2, 0, 6.2832f, on ? rgba(230, 200, 70, (u8)(0.8f * A)) : rgba(170, 178, 190, (u8)(0.5f * A)));
    Color top = on ? rgba(255, 244, 150, (u8)(0.85f * A)) : rgba(235, 240, 248, (u8)(0.42f * A));
    Color bot = on ? rgba(170, 140, 30, (u8)(0.7f * A)) : rgba(120, 130, 145, (u8)(0.30f * A));
    gfx_rrect_vgrad(cx - r, cy - r, 2 * r, 2 * r, r, top, bot);
    Color ic = on ? rgba(0x1a, 0x16, 0x08, A) : rgba(0xf2, 0xf4, 0xf8, A);
    float s = r * 0.42f;
    if (kind == 0 || kind == 2) {      /* previous / next: two triangles */
        float dir = kind == 2 ? 1 : -1;
        for (int k = 0; k < 2; k++) {
            float ox = cx + dir * (k ? s * 0.55f : -s * 0.55f);
            gfx_tri(ox - dir * s * 0.6f, cy - s, ox - dir * s * 0.6f, cy + s, ox + dir * s * 0.6f, cy, ic);
        }
    } else if (kind == 1) {            /* play */
        gfx_tri(cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, cx + s * 0.9f, cy, ic);
    } else {                           /* pause */
        gfx_rect(cx - s * 0.75f, cy - s, s * 0.5f, 2 * s, ic);
        gfx_rect(cx + s * 0.25f, cy - s, s * 0.5f, 2 * s, ic);
    }
}

void music_panel_draw(void) {
    if (!active) return;
    float a = ease_out(clamp01(t / 0.2f));
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    gfx_rect(fx, fy, fw, fh, rgba(0, 0, 0, (u8)(120 * a)));
    float pw = 480, ph = 330, px = UI_X + UI_W / 2 - pw / 2, py = 48 + 12 * (1 - a);
    ui_panel(px, py, pw, ph, a);
    MidiState st = midi_state();
    const char *state = st == MIDI_PLAYING ? "\x83 Playing" : st == MIDI_PAUSED ? "Paused" : st == MIDI_LOADING ? "Loading..." : "Stopped";
    ui_ptitle(px, py, pw, "Music", state, a);
    Color text = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), soft = rgba(0xc9, 0xd2, 0xee, (u8)(255 * a));

    /* now playing */
    const char *title = midi_count() ? (midi_current() >= 0 ? midi_title() : "Press play to start") : "No songs in the midi folder";
    ui_text_fit_scroll(FONT_MENU_BOLD, 16, 1.2f, px + 20, py + 44, pw - 40, 24, text, title);
    char tm[32], len[16];
    fmt_time(tm, sizeof(tm), midi_position());
    fmt_time(len, sizeof(len), midi_length());
    char both[48];
    snprintf(both, sizeof(both), "%s / %s", tm, len);
    float bar_y = py + 78;
    ui_text_right(FONT_MENU, 12, 1.2f, px + pw - 20, bar_y - 3, 14, soft, both);
    float bw = pw - 40 - 90;
    gfx_rrect(px + 20, bar_y + 2, bw, 7, 2, rgba(0, 0, 0, (u8)(115 * a)));
    gfx_rrect_outline(px + 20, bar_y + 2, bw, 7, 2, 1, rgba(200, 210, 230, (u8)(76 * a)));
    float frac = midi_length() > 0 ? midi_position() / midi_length() : 0;
    if (frac > 0) gfx_rect_vgrad(px + 20, bar_y + 2, bw * clamp01(frac), 7, rgba(0x7a, 0x6c, 0xff, (u8)(255 * a)),
                                 rgba(0x3b, 0x2c, 0xb6, (u8)(255 * a)));

    /* buttons */
    float by = py + 124, cxm = px + pw / 2;
    round_button(cxm - 70, by, 18, 0, row == R_BUTTONS && button == 0, a);
    float big = 22 + (flash > 0 && button == 1 ? 2 : 0);
    round_button(cxm, by, big, st == MIDI_PLAYING ? 3 : 1, row == R_BUTTONS && button == 1, a);
    round_button(cxm + 70, by, 18, 2, row == R_BUTTONS && button == 2, a);

    /* options */
    static const char *labels[4] = { "Shuffle", "Repeat", "Volume", "Instruments" };
    char v[4][80], tmp[256];
    snprintf(v[0], 80, "%s", midi_shuffle() ? "On" : "Off");
    snprintf(v[1], 80, "%s", midi_repeat() ? "On" : "Off");
    snprintf(v[2], 80, "%d", g_set.music_volume);
    snprintf(v[3], 80, "%s", sf2_label(sf2_index(), tmp, sizeof(tmp)));
    for (int i = 0; i < 4; i++) {
        float y = py + 160 + i * 30;
        ui_row(px + 8, y, pw - 16, 27, row == R_SHUFFLE + i, a);
        ui_text(FONT_MENU, 14, 1.4f, px + 20, y, 27, text, labels[i]);
        char vv[100];
        snprintf(vv, sizeof(vv), "\x81  %s  \x82", v[i]);
        float vw = gfx_text_width_sp(FONT_MENU, 14, 1.4f, vv, -1);
        if (vw > pw - 170) {   /* a long SoundFont name scrolls between the arrows */
            float right = px + pw - 20;
            ui_text_right(FONT_MENU, 14, 1.4f, right, y, 27, text, "\x82");
            ui_text(FONT_MENU, 14, 1.4f, px + 150, y, 27, text, "\x81");
            ui_text_fit_scroll(FONT_MENU, 14, 1.4f, px + 172, y, right - 26 - (px + 172), 27, text, v[i]);
        } else {
            ui_text_right(FONT_MENU, 14, 1.4f, px + pw - 20, y, 27, text, vv);
        }
    }

    /* what the instruments are doing */
    float dy = py + ph - 10 - 34;
    ui_desc(px + 10, dy, pw - 20, 34, a);
    char info[160];
    const char *msg = midi_message();
    if (msg && msg[0]) snprintf(info, sizeof(info), "%s", msg);
    else if (row == R_SF2) snprintf(info, sizeof(info), "SoundFonts (.sf2) in the midi folder give the songs real instruments.");
    else snprintf(info, sizeof(info), "Playing with: %s", midi_instruments());
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, dy, pw - 44, 34, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)), info);

    const char *h[] = { "S:Select", "B:Close", "A:Confirm" };
    ui_hints(h, 3, a, input_stick_dir());
}
