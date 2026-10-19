/* puzzle.c - Photo Puzzle: the photo cut into tiles with one missing;
 * slide them back into place. */
#include "app.h"
#include "photos.h"
#include "picture.h"
#include "storage.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 5
static PicRequest req;
static int photo, n = 4;
static int board[MAXN * MAXN];      /* tile at each place; n*n-1 = the gap */
static float tpos[MAXN * MAXN][2];  /* drawn place of each tile (animated) */
static int cursor, moves;
static float play_t, win_t = -1;
static bool started, peek;
static int best[3];                 /* best seconds for 3x3, 4x4, 5x5 */

#define BX 44.0f
#define BY 70.0f
#define BW 294.0f
#define BH 222.0f

static void load_best(void) {
    char path[300];
    storage_path(path, sizeof(path), "puzzle.txt");
    char *t = storage_read(path, NULL);
    best[0] = best[1] = best[2] = 0;
    if (t) { sscanf(t, "%d %d %d", &best[0], &best[1], &best[2]); free(t); }
}

static void save_best(void) {
    char path[300], buf[64];
    storage_path(path, sizeof(path), "puzzle.txt");
    int l = snprintf(buf, sizeof(buf), "%d %d %d\n", best[0], best[1], best[2]);
    storage_write_atomic(path, buf, (size_t)l);
}

static int gap(void) {
    for (int i = 0; i < n * n; i++) if (board[i] == n * n - 1) return i;
    return n * n - 1;
}

static bool solved(void) {
    for (int i = 0; i < n * n; i++) if (board[i] != i) return false;
    return true;
}

static void place_tiles(bool snap) {
    for (int i = 0; i < n * n; i++) {
        int t = board[i];
        float tx = (float)(i % n), ty = (float)(i / n);
        if (snap) { tpos[t][0] = tx; tpos[t][1] = ty; }
    }
}

static bool slide(int place) {
    int g = gap();
    int px = place % n, py = place / n, gx = g % n, gy = g / n;
    if (abs(px - gx) + abs(py - gy) != 1) return false;
    board[g] = board[place];
    board[place] = n * n - 1;
    return true;
}

/* shuffled by real moves from the solved state, so it can always be solved */
static void shuffle(void) {
    for (int i = 0; i < n * n; i++) board[i] = i;
    int prev = -1;   /* don't undo the last move straight away */
    for (int k = 0; k < n * n * 25; k++) {
        int g = gap(), opts[4], no = 0;
        int gx = g % n, gy = g / n;
        if (gx > 0 && g - 1 != prev) opts[no++] = g - 1;
        if (gx < n - 1 && g + 1 != prev) opts[no++] = g + 1;
        if (gy > 0 && g - n != prev) opts[no++] = g - n;
        if (gy < n - 1 && g + n != prev) opts[no++] = g + n;
        if (!no) { prev = -1; continue; }
        int pick = opts[rand() % no];
        prev = g;
        slide(pick);
    }
    if (solved()) slide(gap() - 1);
    place_tiles(true);
    moves = 0;
    play_t = 0;
    win_t = -1;
    started = false;
    cursor = gap() > 0 ? gap() - 1 : 1;
}

static void puzzle_enter(int arg) {
    photo = arg;
    photos_load_picture(&req, photo, 1024);
    load_best();
    shuffle();
}

static void puzzle_leave(void) { photos_release_picture(&req); }

static void puzzle_update(void) {
    peek = input_held(BTN_Z);
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_go(SCR_PLAYTIME, 0); return; }
    if (input_pressed(BTN_X)) {
        n = n == 3 ? 4 : n == 4 ? 5 : 3;
        sound_play(SFX_SELECT);
        shuffle();
        return;
    }
    if (input_pressed(BTN_Y)) { sound_play(SFX_SELECT); shuffle(); return; }
    if (win_t >= 0) { win_t += g_dt; if (input_pressed(BTN_A)) { sound_play(SFX_SELECT); shuffle(); } goto anim; }
    if (started) play_t += g_dt;
    int d = input_dir();
    int cx = cursor % n, cy = cursor / n;
    if (d == DIR_LEFT && cx > 0) cursor--;
    else if (d == DIR_RIGHT && cx < n - 1) cursor++;
    else if (d == DIR_UP && cy > 0) cursor -= n;
    else if (d == DIR_DOWN && cy < n - 1) cursor += n;
    if (d != DIR_NONE) sound_play(SFX_HOVER);
    if (input_pressed(BTN_A)) {
        if (slide(cursor)) {
            sound_play(SFX_KEY);
            moves++;
            started = true;
            if (solved()) {
                win_t = 0;
                sound_play(SFX_LOADED);
                int k = n - 3;
                int secs = (int)play_t;
                if (!best[k] || secs < best[k]) { best[k] = secs; save_best(); }
            }
        } else {
            sound_play(SFX_ERROR);
        }
    }
anim:
    for (int i = 0; i < n * n; i++) {
        int t = board[i];
        float tx = (float)(i % n), ty = (float)(i / n);
        tpos[t][0] += (tx - tpos[t][0]) * clamp01(g_dt * 18);
        tpos[t][1] += (ty - tpos[t][1]) * clamp01(g_dt * 18);
    }
}

static void fmt_time(char *out, size_t cap, int secs) { snprintf(out, cap, "%d:%02d", secs / 60, secs % 60); }

static void puzzle_draw(float open) {
    (void)open;
    float X = UI_X;
    ui_background_grid(1);
    ui_panel(X + 22, 18, 596, 400, 1);
    ui_ptitle(X + 22, 18, 596, "Photo Puzzle", NULL, 1);
    float bx = X + BX, by = BY;
    gfx_rect(bx - 8, by - 8, BW + 16, BH + 16, rgba(200, 208, 220, 153));
    gfx_rect(bx - 6, by - 6, BW + 12, BH + 12, rgba(10, 13, 16, 255));
    gfx_rect(bx - 5, by - 5, BW + 10, BH + 10, rgba(0x0b, 0x0f, 0x12, 255));
    bool ready = req.state == 2 && pic_ok(&req.pic);
    /* the part of the photo used: its middle, in the board's 4:3 shape */
    float u0 = 0, v0 = 0, uw = 1, vh = 1;
    if (ready) {
        float pw = req.pic.w, ph = req.pic.h;
        if (pw / ph > BW / BH) { uw = ph * BW / BH; vh = ph; u0 = (pw - uw) / 2; }
        else { uw = pw; vh = pw * BH / BW; v0 = (ph - vh) / 2; }
    }
    float tw = (BW - (n - 1) * 2) / n, th = (BH - (n - 1) * 2) / n;
    if (!ready) {
        if (req.state == 3) ui_text_center(FONT_MENU, 13, 1, bx + BW / 2, by + BH / 2 - 12, 24, rgba(0xdf, 0xe4, 0xff, 255), req.why);
        else ui_spinner(bx + BW / 2, by + BH / 2, 14, g_time, 1);
    } else if (peek || win_t >= 0) {
        float k = win_t >= 0 ? clamp01(win_t / 0.6f) : 1;
        /* the tiles close up into the whole photo */
        for (int t = 0; t < n * n; t++) {
            if (t == n * n - 1 && win_t < 0) continue;
            int sx = t % n, sy = t / n;
            float gapk = 2 * (1 - k);
            float tw2 = (BW - (n - 1) * gapk) / n, th2 = (BH - (n - 1) * gapk) / n;
            pic_draw(&req.pic, bx + sx * (tw2 + gapk), by + sy * (th2 + gapk), tw2, th2, u0 + uw * sx / n, v0 + vh * sy / n,
                     u0 + uw * (sx + 1) / n, v0 + vh * (sy + 1) / n, rgba(255, 255, 255, 255));
        }
        if (peek && win_t < 0) pic_draw(&req.pic, bx, by, BW, BH, u0, v0, u0 + uw, v0 + vh, rgba(255, 255, 255, 255));
    } else {
        for (int t = 0; t < n * n - 1; t++) {
            int sx = t % n, sy = t / n;
            float x = bx + tpos[t][0] * (tw + 2), y = by + tpos[t][1] * (th + 2);
            pic_draw(&req.pic, x, y, tw, th, u0 + uw * sx / n, v0 + vh * sy / n, u0 + uw * (sx + 1) / n, v0 + vh * (sy + 1) / n,
                     rgba(255, 255, 255, 255));
            gfx_rect(x, y, tw, 1, rgba(255, 255, 255, 50));
        }
        float cxp = bx + (cursor % n) * (tw + 2), cyp = by + (cursor / n) * (th + 2);
        ui_gold_ring(cxp + 2, cyp + 2, tw - 4, th - 4, 0.5f + 0.5f * sinf(g_time * 5), 1);
    }

    /* right column */
    float rx = X + 22 + 348, ry = 70, rw = 596 - 348 - 14;
    Photo *p = photos_get(photo);
    char v[3][48];
    snprintf(v[0], 48, "%s", p ? p->name : "-");
    if (strlen(v[0]) > 16) strcpy(v[0] + 13, "...");
    snprintf(v[1], 48, "%d", moves);
    fmt_time(v[2], 48, (int)play_t);
    static const char *lab[3] = { "Photo", "Moves", "Time" };
    for (int i = 0; i < 3; i++) {
        ui_text(FONT_MENU, 14, 1.4f, rx + 10, ry + i * 34, 30, COL_TEXT, lab[i]);
        ui_text_right(FONT_MENU, 14, 1.4f, rx + rw - 10, ry + i * 34, 30, COL_TEXT, v[i]);
    }
    float sy = ry + 3 * 34;
    ui_row(rx, sy, rw, 30, true, 1);
    ui_text(FONT_MENU, 14, 1.4f, rx + 10, sy, 30, COL_TEXT, "Size");
    static const char *sz[3] = { "3x3", "4x4", "5x5" };
    float ox = rx + rw - 10;
    for (int k = 2; k >= 0; k--) {
        float w = gfx_text_width_sp(FONT_MENU, 14, 1, sz[k], -1);
        ox -= w;
        ui_text(FONT_MENU, 14, 1, ox, sy, 30, COL_TEXT, sz[k]);
        ox -= 18;
        ui_radio(ox, sy + 15, n == k + 3, 1);
        ox -= 10;
    }
    char bl[48] = "Best: -";
    if (best[n - 3]) { char t[16]; fmt_time(t, sizeof(t), best[n - 3]); snprintf(bl, sizeof(bl), "Best for %s: %s", sz[n - 3], t); }
    ui_text(FONT_MENU, 13, 1.2f, rx + 10, sy + 40, 24, COL_SOFT, bl);
    ui_button(rx + 12, sy + 80, 96, 28, "Shuffle", false, 1);
    ui_button(rx + rw - 108, sy + 80, 96, 28, "Peek", peek, 1);
    ui_text_center(FONT_MENU, 11, 1, rx + 12 + 48, sy + 112, 18, COL_SOFT, "Y");
    ui_text_center(FONT_MENU, 11, 1, rx + rw - 60, sy + 112, 18, COL_SOFT, "hold Z");

    ui_desc(X + 32, 366, 576, 42, 1);
    char msg[120];
    if (win_t >= 0) {
        char t[16];
        fmt_time(t, sizeof(t), (int)play_t);
        snprintf(msg, sizeof(msg), "Solved in %d moves and %s! Press A to play again.", moves, t);
    } else {
        snprintf(msg, sizeof(msg), "Slide the tiles back into place to rebuild the photo. X changes the size.");
    }
    ui_text(FONT_MENU, 13, 1.2f, X + 44, 366, 42, rgba(0xec, 0xeb, 0xff, 255), msg);
    const char *h[] = { "DP:Select", "Z:Peek", "X:Size", "B:Back", "A:Slide" };
    ui_hints(h, 5, 1, -1);
}

const Screen screen_puzzle = { puzzle_enter, puzzle_update, puzzle_draw, puzzle_leave };
