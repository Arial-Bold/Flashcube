/* cube.h - the glass menu cube, drawn in 3D like the GameCube's own: the
 * front face names the four sides; pushing the stick turns the cube to show
 * that side, with its page and a block icon floating inside the glass. */
#pragma once
#include <stdbool.h>
#include "gfx.h"
#include "m3d.h"

enum { SIDE_ALBUM = 0, SIDE_SLIDESHOW = 1, SIDE_TRANSFER = 2, SIDE_PLAYTIME = 3, SIDE_COUNT = 4 };
/* Stick direction for each side: up, down, left, right. */

typedef struct {
    const char *title;
    int nbands;
    const char *band_label[3];
    char band_value[3][32];
} CubePage;

typedef struct {
    float time;           /* seconds, drives the sway and the icon */
    int   side;           /* the side turned to (or being turned to) */
    float turn;           /* 0 = front, 1 = side fully shown (already eased) */
    float labels;         /* opacity of the front labels */
    int   lit;            /* label lit by the stick, or -1 */
    float icon;           /* opacity of the block icon */
    float icon_time;      /* drives the icon's change of shape */
    float collapse;       /* 0..1: the blocks fall into the middle (opening) */
    float scale;          /* 1 = normal */
    float alpha;          /* whole cube */
    float sway;           /* 0..1 how much the idle sway applies */
    float back;           /* 0..1: rolled on past Album to the back face (a secret) */
    void (*back_page)(float alpha);   /* draws on the back face, in its 0..180 plane */
    CubePage pages[SIDE_COUNT];
} CubeView;

void cube_view_init(CubeView *v);
void cube_draw(const CubeView *v);
/* The cube's base orientation at the front (used by the intro so the logo
 * cube lines up exactly). */
void cube_base_angles(float *x, float *y, float *z);

/* Glass for small planes (the intro logo's tiles): kind 0 = the front
 * face's tinted glass, 1 = the sides' clear glass. Drawn in the current 3D
 * plane in a size x size square. */
void cube_glass(int kind, float size, float radius, float alpha, bool rim);
