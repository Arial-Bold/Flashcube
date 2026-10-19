/* apptest.c - runs the whole app on a PC with scripted controller input. */
#include "app.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void gxemu_save(const char *path, int out_w);
bool gfx_widescreen(void);
static int frame;
static void run(int frames) { for (int i = 0; i < frames; i++) { app_frame(); frame++; usleep(2000); } }
static void shot(const char *name) {
    char p[128];
    snprintf(p, sizeof(p), "out/%s.ppm", name);
    gxemu_save(p, gfx_widescreen() ? 854 : 640);
    printf("shot %s (frame %d)\n", name, frame);
}
static void press(uint32_t b) { input_inject(b); run(1); }
static void hold(uint32_t b, float sx, float sy, int frames) { input_test_state(b, sx, sy, 0, 0); run(frames); input_test_state(0, 0, 0, 0, 0); run(1); }
int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "tour";
    app_init();
    run(60 * 8);                  /* intro */
    shot("01_menu");
    if (!strcmp(script, "tour")) {
        hold(0, 0, -1, 70);       /* stick up: album side */
        shot("02_album_side");
        press(BTN_A); run(20); shot("03_opening_a");
        run(30); shot("04_opening_b");
        run(80); shot("05_album");
        press(BTN_RIGHT); run(10); press(BTN_DOWN); run(30); shot("06_album_sel");
        press(BTN_A); run(120); shot("07_viewer");
        press(BTN_DOWN); press(BTN_DOWN); press(BTN_A); run(30); shot("08_viewer_info");
        press(BTN_B); run(10);
        input_test_state(BTN_R, 0, 0, 0, 1); run(40); input_test_state(0, 0.6f, 0.3f, 0, 0); run(30); input_test_state(0, 0, 0, 0, 0); run(10);
        shot("09_viewer_zoom");
        press(BTN_Z); run(40); shot("10_viewer_full");
        press(BTN_B); run(20); press(BTN_B); run(30); shot("11_back_album");
        press(BTN_Y); run(60); shot("12_slide_setup");
        press(BTN_A); run(150); shot("13_slideshow");
        run(60 * 7); shot("14_slideshow_trans");
        press(BTN_B); run(40); press(BTN_B); run(30); press(BTN_B); run(80); shot("15_cube_return");
        press(BTN_B); run(80);
        hold(0, 1, 0, 70); shot("16_play_side");
        press(BTN_A); run(150); shot("17_playtime");
        press(BTN_A); run(60); shot("18_pick");
        press(BTN_A); run(120); shot("19_puzzle");
        press(BTN_A); run(20); press(BTN_B); run(40);
        press(BTN_RIGHT); run(5); press(BTN_A); run(60); press(BTN_A); run(120);
        hold(BTN_A, 0.8f, 0.2f, 40); hold(BTN_A, -0.3f, 0.7f, 30); shot("20_doodle");
        press(BTN_START); run(20); shot("21_doodle_saved");
        press(BTN_B); run(40); press(BTN_RIGHT); run(5); press(BTN_A); run(40); press(BTN_A); run(120);
        press(BTN_DOWN); run(60); shot("22_looks");
        press(BTN_A); run(30); shot("23_looks_saved");
        press(BTN_B); run(30); press(BTN_B); run(80); press(BTN_B); run(80);
        hold(0, -1, 0, 70); shot("24_transfer_side");
        press(BTN_A); run(150); shot("25_transfer");
        press(BTN_B); run(80); press(BTN_B); run(80);
        hold(0, 0, 1, 70); shot("26_slide_side");
        press(BTN_B); run(80);
        press(BTN_START); run(30); shot("27_settings");
    } else if (!strcmp(script, "wide")) {
        hold(0, 0, -1, 70); shot("w1_side");
        press(BTN_A); run(160); shot("w2_album");
        press(BTN_A); run(120); shot("w3_viewer");
        press(BTN_B); run(30); press(BTN_B); run(80); press(BTN_B); run(80); shot("w4_cube");
    } else if (!strcmp(script, "serve")) {
        hold(0, -1, 0, 70);
        press(BTN_A);
        for (int i = 0; i < 60 * 40; i++) { run(1); if (i == 400) shot("serve_a"); }
        shot("serve_b");
    }
    return 0;
}
