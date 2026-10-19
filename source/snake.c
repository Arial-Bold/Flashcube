/* snake.c - see snake.h. */
#include "snake.h"
#include "app.h"
#include "photos.h"
#include "settings.h"
#include "input.h"
#include "sound.h"
#include "ui.h"
#include "m3d.h"
#include <stdio.h>
#include <stdlib.h>

#define GRID 11                 /* cells across and down */
#define CELL 13.0f              /* size of a cell on the face */
#define BX 18.5f                /* where the board starts on the face */
#define BY 30.0f
#define MAXLEN (GRID * GRID)

enum { S_READY, S_PLAY, S_OVER };
static int mode;
static int sx[MAXLEN], sy[MAXLEN], len;
static int dx, dy, ndx, ndy;    /* direction now, and the next one asked for */
static int fx, fy, food_photo;
static int score;
static float step_t, step_len;
static float flash;             /* the moment of eating */
static float over_t;
static bool new_best;
static unsigned rng = 12345;

static int rnd(int n) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 8) % (unsigned)n);
}

static bool on_snake(int x, int y, int from) {
    for (int i = from; i < len; i++) if (sx[i] == x && sy[i] == y) return true;
    return false;
}

static void place_food(void) {
    if (len >= MAXLEN) return;
    do { fx = rnd(GRID); fy = rnd(GRID); } while (on_snake(fx, fy, 0));
    int n = photos_count();
    food_photo = n > 0 ? rnd(n) : -1;
    if (food_photo >= 0) photos_prefetch(food_photo);
}

static void start_game(void) {
    len = 3;
    for (int i = 0; i < len; i++) { sx[i] = GRID / 2 - i; sy[i] = GRID / 2; }
    dx = ndx = 1;
    dy = ndy = 0;
    score = 0;
    step_len = 0.16f;
    step_t = 0;
    rng ^= (unsigned)(g_time * 1000);
    place_food();
    mode = S_PLAY;
}

void snake_reset(void) {
    mode = S_READY;
    flash = 0;
    len = 3;
    for (int i = 0; i < len; i++) { sx[i] = GRID / 2 - i; sy[i] = GRID / 2; }
    fx = GRID / 2 + 3;
    fy = GRID / 2;
    food_photo = photos_count() > 0 ? rnd(photos_count()) : -1;
}

bool snake_playing(void) { return mode == S_PLAY; }

static void step(void) {
    /* turn (never straight back into itself) */
    if (!(ndx == -dx && ndy == -dy)) { dx = ndx; dy = ndy; }
    int hx = sx[0] + dx, hy = sy[0] + dy;
    bool eat = hx == fx && hy == fy;
    /* the walls, or itself (the tail moves out of the way unless it grows) */
    if (hx < 0 || hy < 0 || hx >= GRID || hy >= GRID || (on_snake(hx, hy, 0) && !(!eat && hx == sx[len - 1] && hy == sy[len - 1]))) {
        mode = S_OVER;
        over_t = 0;
        sound_play(SFX_ERROR);
        new_best = score > g_set.snake_best;
        if (new_best) { g_set.snake_best = score; settings_save(); }
        return;
    }
    if (eat && len < MAXLEN) len++;
    for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = hx;
    sy[0] = hy;
    if (eat) {
        score++;
        flash = 1;
        sound_play(SFX_KEY);
        if (step_len > 0.075f) step_len -= 0.005f;   /* a little faster each time */
        place_food();
    }
}

void snake_update(void) {
    if (flash > 0) flash -= g_dt * 3;
    if (mode != S_PLAY) {
        over_t += g_dt;
        if (input_pressed(BTN_A) && (mode == S_READY || over_t > 0.6f)) { sound_play(SFX_CONFIRM); start_game(); }
        return;
    }
    /* steering: D-pad or stick */
    int d = -1;
    if (input_pressed(BTN_UP)) d = DIR_UP;
    else if (input_pressed(BTN_DOWN)) d = DIR_DOWN;
    else if (input_pressed(BTN_LEFT)) d = DIR_LEFT;
    else if (input_pressed(BTN_RIGHT)) d = DIR_RIGHT;
    else d = input_stick_dir();
    if (d == DIR_UP) { ndx = 0; ndy = -1; }
    else if (d == DIR_DOWN) { ndx = 0; ndy = 1; }
    else if (d == DIR_LEFT) { ndx = -1; ndy = 0; }
    else if (d == DIR_RIGHT) { ndx = 1; ndy = 0; }
    step_t += g_dt;
    while (step_t >= step_len && mode == S_PLAY) {
        step_t -= step_len;
        step();
    }
}

/* one glowing block, like the icons inside the cube's sides */
static void block(float x, float y, float s, float a, bool head) {
    Color hi = head ? rgba(0xf0, 0xff, 0xf2, 255) : rgba(0xe4, 0xff, 0xe9, 255);
    Color mid = head ? rgba(0x8a, 0xf5, 0xa8, 255) : rgba(0x5f, 0xe3, 0x8a, 255);
    Color lo = rgba(0x22, 0xa0, 0x4e, 255), sh = rgba(0x15, 0x7a, 0x38, 255);
    gfx_rrect(x - 2, y - 2, s + 4, s + 4, 3, rgba(90, 230, 140, (u8)(60 * a)));
    gfx_rect(x + 1.2f, y + 1.2f, s, s, color_alpha(sh, a));
    float q[8] = { x, y, x + s, y, x + s, y + s, x, y + s };
    Color c4[4] = { color_alpha(hi, a), color_alpha(mid, a), color_alpha(lo, a), color_alpha(mid, a) };
    gfx_quad_colors(q, c4);
}

void snake_draw(float a) {
    if (a <= 0) return;
    Color white = rgba(255, 255, 255, (u8)(255 * a)), soft = rgba(0xc9, 0xd2, 0xee, (u8)(220 * a));
    /* header */
    ui_text(FONT_CUBE, 15, -0.2f, BX, 9, 18, white, "Photo Snake");
    char sc[24];
    snprintf(sc, sizeof(sc), "%d", score);
    ui_text_right(FONT_ROUND, 15, -0.2f, BX + GRID * CELL, 9, 18, white, sc);
    /* the board */
    float bw = GRID * CELL;
    gfx_rrect(BX - 3, BY - 3, bw + 6, bw + 6, 4, rgba(0, 0, 0, (u8)(70 * a)));
    for (int i = 1; i < GRID; i++) {
        gfx_rect(BX + i * CELL, BY, 0.6f, bw, rgba(150, 190, 230, (u8)(22 * a)));
        gfx_rect(BX, BY + i * CELL, bw, 0.6f, rgba(150, 190, 230, (u8)(22 * a)));
    }
    gfx_rrect_outline(BX - 3, BY - 3, bw + 6, bw + 6, 4, 1, rgba(200, 220, 240, (u8)(70 * a)));
    /* the food: a tiny photo in a gold ring */
    float pulse = 0.5f + 0.5f * sinf(g_time * 5);
    float fxp = BX + fx * CELL, fyp = BY + fy * CELL;
    gfx_rrect(fxp - 1.5f, fyp - 1.5f, CELL + 3, CELL + 3, 3, rgba(240, 210, 80, (u8)((60 + 70 * pulse) * a)));
    if (food_photo >= 0 && food_photo < photos_count()) app_draw_thumb(food_photo, fxp + 1, fyp + 1, CELL - 2, CELL - 2, a);
    else gfx_rrect(fxp + 2, fyp + 2, CELL - 4, CELL - 4, 2, rgba(240, 210, 80, (u8)(255 * a)));
    gfx_rrect_outline(fxp, fyp, CELL, CELL, 2, 1, rgba(0xe8, 0xcf, 0x4a, (u8)(255 * a)));
    /* the snake, tail first so the head is on top */
    float da = mode == S_OVER ? (0.45f + 0.55f * (0.5f + 0.5f * cosf(over_t * 8)) * (over_t < 1 ? 1 : 0)) : 1;
    for (int i = len - 1; i >= 0; i--) {
        float s = CELL - 3;
        if (i == 0 && flash > 0) s += flash * 2;
        block(BX + sx[i] * CELL + (CELL - s) / 2, BY + sy[i] * CELL + (CELL - s) / 2, s, a * da, i == 0);
    }
    /* messages */
    if (mode != S_PLAY) {
        float my = BY + bw / 2 - 22;
        gfx_rrect(BX + 8, my, bw - 16, 44, 4, rgba(10, 12, 30, (u8)(200 * a)));
        gfx_rrect_outline(BX + 8, my, bw - 16, 44, 4, 1, rgba(175, 175, 255, (u8)(130 * a)));
        const char *l1 = mode == S_READY ? "Press A to play" : "Game over";
        char l2[40];
        if (mode == S_READY) snprintf(l2, sizeof(l2), "Best: %d", g_set.snake_best);
        else if (new_best) snprintf(l2, sizeof(l2), "\x84 New best: %d", score);
        else snprintf(l2, sizeof(l2), "%d \x7f Best %d \x7f A: again", score, g_set.snake_best);
        ui_text_center(FONT_ROUND, 14, -0.2f, BX + bw / 2, my + 4, 20, white, l1);
        ui_text_center(FONT_ROUND, 11, -0.2f, BX + bw / 2, my + 22, 18, soft, l2);
    }
}
