/* transfer.c - the Transfer screen: a QR code for the phone's camera, the
 * address to type, and the photos coming and going. */
#include "app.h"
#include "net.h"
#include "photos.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "cube.h"
#include "qrcodegen.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>

static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
static bool qr_ok;
static char qr_for[48];

static void make_qr(void) {
    const char *u = net_url();
    if (!u[0] || !strcmp(u, qr_for)) return;
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    qr_ok = qrcodegen_encodeText(u, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                 qrcodegen_Mask_AUTO, true);
    snprintf(qr_for, sizeof(qr_for), "%s", u);
}

static void transfer_enter(int arg) {
    (void)arg;
    net_start();
}

static void transfer_update(void) {
    net_tick();
    if (net_state() == NET_UP) make_qr();
    if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        app_back_to_cube(SIDE_TRANSFER);
    }
}

/* Draws part (piece px,py of a 4x4 split) of the QR code into the box. */
static void draw_qr_piece(float x, float y, float size, int px, int py, float a) {
    if (!qr_ok) return;
    int n = qrcodegen_getSize(qr);
    float m = size / (n + 8);     /* four modules of quiet space around */
    float ox = x + 4 * m, oy = y + 4 * m;
    int x0 = (n + 8) * px / 4 - 4, x1 = (n + 8) * (px + 1) / 4 - 4;
    int y0 = (n + 8) * py / 4 - 4, y1 = (n + 8) * (py + 1) / 4 - 4;
    Color dark = rgba(0x14, 0x16, 0x3a, (u8)(255 * a));
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            if (i >= 0 && j >= 0 && i < n && j < n && qrcodegen_getModule(qr, i, j))
                gfx_rect(ox + i * m, oy + j * m, m + 0.3f, m + 0.3f, dark);
}

static void transfer_draw(float open) {
    float X = UI_X;
    ui_background_grid(ramp(open, 0, 0.35f));
    float pa = ramp(open, 0.45f, 0.85f);
    float px = X + 22, py = 18, pw = UI_W - 44, ph = 400;
    ui_panel(px, py, pw, ph, pa);
    NetState st = net_state();
    char right[96];
    Color rc = rgba(0x8f, 0xe0, 0xa8, 255);
    if (st == NET_UP) snprintf(right, sizeof(right), "\x7f %s", net_message());
    else if (st == NET_FAILED) { snprintf(right, sizeof(right), "\x7f Not connected"); rc = rgba(0xff, 0x8c, 0xa0, 255); }
    else { snprintf(right, sizeof(right), "\x7f Connecting..."); rc = rgba(0xff, 0xd8, 0x6a, 255); }
    ui_ptitle(px, py, pw, "Transfer", NULL, pa);
    rc.a = (u8)(255 * pa);
    ui_text_right(FONT_MENU, 15, 1.6f, px + pw - 16, py + 9, 15, rc, right);

    int nitems = 16 + 4;
    float done = ui_swirl_duration(nitems);
    /* the code */
    float qx = px + 22, qy = py + 50, qs = 184;
    float wa = ramp(open, 0.3f, 0.6f);
    gfx_rect(qx - 3, qy - 3, qs + 6, qs + 6, rgba(200, 208, 220, (u8)(0.6f * 255 * wa)));
    gfx_rect(qx - 1, qy - 1, qs + 2, qs + 2, rgba(10, 13, 16, (u8)(255 * wa)));
    gfx_rect(qx, qy, qs, qs, rgba(255, 255, 255, (u8)(255 * wa)));
    if (st == NET_UP && qr_ok) {
        for (int k = 0; k < 16; k++) {
            int ix = k % 4, iy = k / 4;
            float sx = qx + qs * ix / 4, sy = qy + qs * iy / 4;
            float a = 1;
            bool sw = open < done;
            if (sw) {
                a = ui_swirl_begin(k, nitems, sx, sy, qs / 4, qs / 4, open, 135, 40);
                gfx_rect(sx, sy, qs / 4 + 0.5f, qs / 4 + 0.5f, rgba(255, 255, 255, (u8)(255 * a)));
            }
            draw_qr_piece(qx, qy, qs, ix, iy, a);
            if (sw) ui_swirl_end();
        }
    } else if (st == NET_FAILED) {
        ui_text_center(FONT_MENU_BOLD, 40, 0, qx + qs / 2, qy + qs / 2 - 30, 60, rgba(0xb0, 0xb4, 0xc8, (u8)(255 * wa)), "!");
    } else {
        ui_spinner(qx + qs / 2, qy + qs / 2, 18, g_time, wa);
    }
    Color soft = rgba(0xc9, 0xd2, 0xee, (u8)(255 * pa)), text = rgba(0xee, 0xf1, 0xf6, (u8)(255 * pa));
    if (st == NET_UP) {
        ui_text_center(FONT_MENU, 12, 1.2f, qx + qs / 2, qy + qs + 10, 16, soft, "or type this address");
        char addr[48];
        snprintf(addr, sizeof(addr), "%s", net_url() + 7);   /* without "http://" */
        size_t l = strlen(addr);
        if (l && addr[l - 1] == '/') addr[l - 1] = 0;
        ui_text_center(FONT_MENU_BOLD, 17, 1.6f, qx + qs / 2, qy + qs + 30, 22, text, addr);
    }

    /* arriving and leaving */
    float lx = px + 244, ly = py + 50;
    ui_text(FONT_MENU, 13, 1.5f, lx + 10, ly, 22, rgba(0x73, 0x92, 0xff, (u8)(255 * pa)), "Transfers");
    NetTransfer tr[4];
    int n = net_recent(tr, 4);
    for (int i = 0; i < 4; i++) {
        float x = lx + 8, y = ly + 28 + i * 40, w = pw - 244 - 18, h = 34;
        float a = 1;
        bool sw = open < done;
        if (sw) a = ui_swirl_begin(16 + i, nitems, x, y, w, h, open, 110, 10);
        Color tc = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a));
        /* small picture */
        gfx_rect(x + 10 - 1.5f, y + 3 - 1.5f, 38 + 3, 28 + 3, rgba(190, 200, 215, (u8)(0.45f * 255 * a)));
        gfx_rect(x + 10, y + 3, 38, 28, rgba(0x0b, 0x0f, 0x12, (u8)(255 * a)));
        if (i < n) {
            int photo = photos_find_key(tr[i].photo_key);
            if (photo >= 0) app_draw_thumb(photo, x + 12, y + 5, 34, 24, a);
            char status[48];
            Color sc = rgba(0xc9, 0xd2, 0xee, (u8)(255 * a));
            if (tr[i].state == 2) { snprintf(status, sizeof(status), tr[i].to_phone ? "Sent to phone" : "Done"); sc = rgba(0x8f, 0xe0, 0xa8, (u8)(255 * a)); }
            else if (tr[i].state == 3) { snprintf(status, sizeof(status), "Failed"); sc = rgba(0xff, 0x8c, 0xa0, (u8)(255 * a)); }
            else snprintf(status, sizeof(status), "%.1f / %.1f MB", tr[i].done / 1048576.0, tr[i].total / 1048576.0);
            float sw2 = ui_text_right(FONT_MENU, 13, 1.3f, x + w - 10, y + 2, 16, sc, status);
            ui_text_fit_scroll(FONT_MENU, 13, 1.3f, x + 58, y + 2, w - 68 - sw2 - 14, 16, tc, tr[i].name);
            float frac = tr[i].total ? (float)tr[i].done / tr[i].total : 0;
            if (tr[i].state == 2) frac = 1;
            gfx_rrect(x + 58, y + 24, w - 68, 7, 2, rgba(0, 0, 0, (u8)(115 * a)));
            gfx_rrect_outline(x + 58, y + 24, w - 68, 7, 2, 1, rgba(200, 210, 230, (u8)(76 * a)));
            if (frac > 0) gfx_rect_vgrad(x + 58, y + 24, (w - 68) * clamp01(frac), 7, rgba(0x7a, 0x6c, 0xff, (u8)(255 * a)),
                                         rgba(0x3b, 0x2c, 0xb6, (u8)(255 * a)));
        } else {
            ui_text(FONT_MENU, 13, 1.3f, x + 58, y + 2, 16, rgba(0x8a, 0x92, 0xb0, (u8)(255 * a)), i == 0 ? "Nothing yet" : "");
            gfx_rrect(x + 58, y + 24, w - 68, 7, 2, rgba(0, 0, 0, (u8)(115 * a)));
            gfx_rrect_outline(x + 58, y + 24, w - 68, 7, 2, 1, rgba(200, 210, 230, (u8)(76 * a)));
        }
        if (sw) ui_swirl_end();
    }
    if (net_received() > 0) {
        char c[64];
        snprintf(c, sizeof(c), "%d photo%s received \x7f in DCIM/100FLASH", net_received(), net_received() == 1 ? "" : "s");
        ui_text(FONT_MENU, 12, 1.2f, lx + 18, ly + 28 + 4 * 40 + 6, 20, soft, c);
    }

    float da = ramp(open, done - 0.5f, done - 0.1f);
    float dy = py + ph - 10 - 64 + 16 * (1 - ease_out(da));
    ui_desc(px + 10, dy, pw - 20, 64, da);
    Color dc = rgba(0xec, 0xeb, 0xff, (u8)(255 * da));
    if (st == NET_FAILED) {
        ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, dy + 6, pw - 44, 24, dc, net_message());
        ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 30, 24, dc, "Check the cable to your router, then open Transfer again.");
    } else {
        ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 6, 24, dc, "Scan the code with your phone's camera to send photos here or");
        ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 30, 24, dc, "save them to your phone. Your phone must be on the same Wi-Fi.");
    }
    const char *h[] = { "B:Back" };
    ui_hints(h, 1, ramp(open, done - 0.4f, done), -1);
}

const Screen screen_transfer = { transfer_enter, transfer_update, transfer_draw, NULL };
