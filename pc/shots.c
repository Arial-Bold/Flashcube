/* shots.c - renders Flashcube scenes on a PC through gxemu, for checking. */
#include "gfx.h"
#include "ui.h"
#include "cube.h"
#include "logo.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void gxemu_save(const char *path, int out_w);

static void shot(const char *name) {
    char p[256];
    snprintf(p, sizeof(p), "out/%s.ppm", name);
    gxemu_save(p, gfx_widescreen() ? 854 : 640);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    storage_init();
    gfx_init(0, false, true);
    system("mkdir -p out");
    /* intro frames */
    float ts[] = { 0.5f, 2.0f, 2.8f, 3.4f, 4.2f, 4.8f, 5.3f, 5.6f, 6.0f };
    for (unsigned i = 0; i < sizeof(ts) / sizeof(ts[0]); i++) {
        gfx_begin_frame();
        ui_frame_begin();
        ui_background_cube();
        logo_draw(ts[i]);
        CubeView v;
        cube_view_init(&v);
        v.alpha = logo_nav_alpha(ts[i]);
        v.labels = logo_labels_alpha(ts[i]);
        v.sway = 0;
        cube_draw(&v);
        gfx_end_frame();
        char n[32];
        snprintf(n, sizeof(n), "intro_%.1f", ts[i]);
        shot(n);
    }
    /* front + each side */
    for (int s = -1; s < 4; s++) {
        gfx_begin_frame();
        ui_frame_begin();
        ui_background_cube();
        CubeView v;
        cube_view_init(&v);
        v.time = 1.0f;
        v.pages[SIDE_ALBUM].title = "Album"; v.pages[SIDE_ALBUM].nbands = 1; v.pages[SIDE_ALBUM].band_label[0] = "Photos";
        strcpy(v.pages[SIDE_ALBUM].band_value[0], "128");
        v.pages[SIDE_SLIDESHOW].title = "Slideshow"; v.pages[SIDE_SLIDESHOW].nbands = 2;
        v.pages[SIDE_SLIDESHOW].band_label[0] = "Each Photo"; strcpy(v.pages[SIDE_SLIDESHOW].band_value[0], "6 sec");
        v.pages[SIDE_SLIDESHOW].band_label[1] = "Music"; strcpy(v.pages[SIDE_SLIDESHOW].band_value[1], "On");
        v.pages[SIDE_TRANSFER].title = "Transfer"; v.pages[SIDE_TRANSFER].nbands = 2;
        v.pages[SIDE_TRANSFER].band_label[0] = "Address"; strcpy(v.pages[SIDE_TRANSFER].band_value[0], "192.168.1.50");
        v.pages[SIDE_TRANSFER].band_label[1] = "Status"; strcpy(v.pages[SIDE_TRANSFER].band_value[1], "Ready");
        v.pages[SIDE_PLAYTIME].title = "Playtime"; v.pages[SIDE_PLAYTIME].nbands = 3;
        v.pages[SIDE_PLAYTIME].band_label[0] = "Photo Puzzle"; v.pages[SIDE_PLAYTIME].band_label[1] = "Doodle";
        v.pages[SIDE_PLAYTIME].band_label[2] = "Color Looks";
        if (s >= 0) { v.side = s; v.turn = 1; v.labels = 0; v.icon = 1; v.icon_time = 0.5f; }
        else v.lit = SIDE_ALBUM;
        cube_draw(&v);
        const char *h1[] = { "S:Menu Selection" };
        const char *h2[] = { "B:Cancel", "A:Confirm" };
        if (s < 0) ui_hints_center(h1, 1, 480 - 34 - 13, 34, 1, 0, true);
        else ui_hints_center(h2, 2, 480 - 34 - 13, 34, 1, -1, true);
        gfx_end_frame();
        char n[32];
        snprintf(n, sizeof(n), "cube_%d", s);
        shot(n);
    }
    return 0;
}
