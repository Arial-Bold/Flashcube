/* transfer.c - the Transfer screen, with two tabs (L/R):
 *   Phone        a QR code for the phone's camera, the address to type,
 *                and the photos coming and going;
 *   Memory Card  Super Smash Bros. Melee snapshots on a memory card, saved
 *                to the SD card as PNG pictures (see mcard.c). */
#include "app.h"
#include "net.h"
#include "photos.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "cube.h"
#include "qrcodegen.h"
#include "m3d.h"
#include "mcard.h"
#include "storage.h"
#include <math.h>
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

enum { TAB_PHONE, TAB_CARD };
static int tab = TAB_PHONE;
static float tab_t = 1;       /* time since the tab changed (fades the new one in) */
enum { ROW_SLOT_A, ROW_SLOT_B, ROW_SAVE };
static int row = ROW_SLOT_A;
static float grid_scroll, grid_target;
static float saved_flash;     /* shows "Saved" on the button for a moment */

static int first_slot(void) {
    /* skip the slot holding an SD Gecko */
    return !strcmp(storage_device_name(), "SD Gecko (slot A)") ? 1 : 0;
}

static void transfer_enter(int arg) {
    (void)arg;
    net_start();
    tab = TAB_PHONE;
    tab_t = 1;
}

static void card_update(void) {
    int d = input_dir();
    int nrow = row;
    if (d == DIR_UP && row > 0) nrow--;
    else if (d == DIR_DOWN && row < ROW_SAVE) nrow++;
    if (nrow != row) {
        row = nrow;
        sound_play(SFX_HOVER);
    }
    if (input_pressed(BTN_A)) {
        if (row == ROW_SAVE) {
            if (mcard_state() == MC_READY && mcard_unsaved() > 0 && !mcard_saving(NULL, NULL)) {
                sound_play(SFX_SELECT);
                mcard_save_new();
            } else {
                sound_play(SFX_ERROR);
            }
        } else if (!mcard_saving(NULL, NULL)) {
            sound_play(SFX_SELECT);   /* look again (after changing cards) */
            mcard_open(row);
            grid_target = grid_scroll = 0;
        }
    }
    /* the C-stick scrolls the pictures */
    float cy = input_cstick_y();
    if (fabsf(cy) > 0.3f) grid_target += cy * g_dt * 320;
    grid_scroll += (grid_target - grid_scroll) * clamp01(g_dt * 14);
}

static void transfer_update(void) {
    net_tick();
    mcard_tick();
    tab_t += g_dt;
    saved_flash -= g_dt;
    if (net_state() == NET_UP) make_qr();
    static bool was_saving;
    bool sv = mcard_saving(NULL, NULL);
    if (was_saving && !sv) {
        int n = mcard_just_saved();
        char m[64];
        snprintf(m, sizeof(m), "%d snapshot%s saved to DCIM/MELEE", n, n == 1 ? "" : "s");
        app_toast(m);
        sound_play(n ? SFX_CONFIRM : SFX_ERROR);
        saved_flash = 2.5f;
    }
    was_saving = sv;
    if ((input_pressed(BTN_L) && tab == TAB_CARD) || (input_pressed(BTN_R) && tab == TAB_PHONE)) {
        tab = tab == TAB_PHONE ? TAB_CARD : TAB_PHONE;
        tab_t = 0;
        sound_play(SFX_HOVER);
        if (tab == TAB_CARD && mcard_state() == MC_IDLE) {
            row = ROW_SAVE;
            mcard_open(first_slot());
            grid_target = grid_scroll = 0;
        }
        return;
    }
    if (tab == TAB_CARD) card_update();
    if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        mcard_close();
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

/* the Phone tab */
static void draw_phone(float open, float px, float py, float pw, float ph, float pa, float top) {
    NetState st = net_state();
    int nitems = 16 + 4;
    float done = ui_swirl_duration(nitems);
    /* the code */
    float qx = px + 22, qy = top, qs = 172;
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
    float lx = px + 232, ly = top;
    ui_text(FONT_MENU, 13, 1.5f, lx + 10, ly, 22, rgba(0x73, 0x92, 0xff, (u8)(255 * pa)), "Transfers");
    NetTransfer tr[4];
    int n = net_recent(tr, 4);
    for (int i = 0; i < 4; i++) {
        float x = lx + 8, y = ly + 28 + i * 40, w = pw - 232 - 18, h = 34;
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
    const char *h[] = { "LR:Tab", "B:Back" };
    ui_hints(h, 2, ramp(open, done - 0.4f, done), -1);
}

/* the Memory Card tab */
static void draw_status(float cx, float cy, float a, const char *big, const char *small) {
    ui_text_center(FONT_MENU, 15, 1.4f, cx, cy - 14, 22, rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), big);
    if (small) ui_text_center(FONT_MENU, 12, 1.2f, cx, cy + 12, 18, rgba(0xc9, 0xd2, 0xee, (u8)(255 * a)), small);
}

static void draw_card(float px, float py, float pw, float ph, float a, float top) {
    McState st = mcard_state();
    int slot = mcard_slot();
    Color text = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), soft = rgba(0xc9, 0xd2, 0xee, (u8)(255 * a));
    /* the slots and the button, on the left */
    float lx = px + 18, lw = 172, y = top;
    ui_text(FONT_MENU, 13, 1.5f, lx + 6, y, 22, rgba(0x73, 0x92, 0xff, (u8)(255 * a)), "Memory Card");
    y += 28;
    for (int s = 0; s < 2; s++) {
        bool on = row == s;
        ui_row(lx, y, lw, 46, on, a);
        /* a small memory card */
        float ix = lx + 12, iy = y + 10;
        bool here = s == slot;
        gfx_rect(ix, iy, 20, 26, rgba(0x9a, 0xa2, 0xb8, (u8)((here ? 230 : 110) * a)));
        gfx_rect(ix + 2, iy + 2, 16, 15, rgba(0x2a, 0x30, 0x40, (u8)((here ? 255 : 140) * a)));
        for (int k = 0; k < 4; k++) gfx_rect(ix + 3 + k * 4, iy + 21, 2, 4, rgba(0xe8, 0xcf, 0x4a, (u8)((here ? 230 : 110) * a)));
        ui_text(FONT_MENU, 14, 1.4f, lx + 42, y + 4, 20, text, s ? "Slot B" : "Slot A");
        char sub[48] = "";
        Color sc = soft;
        if (here) {
            if (st == MC_LOOKING) snprintf(sub, sizeof(sub), "Reading...");
            else if (st == MC_NO_CARD) snprintf(sub, sizeof(sub), "No memory card");
            else if (st == MC_SD_SLOT) snprintf(sub, sizeof(sub), "SD card adapter");
            else if (st == MC_ERROR) { snprintf(sub, sizeof(sub), "Can't be read"); sc = rgba(0xff, 0x8c, 0xa0, (u8)(255 * a)); }
            else if (st == MC_READY) {
                int n = mcard_count();
                snprintf(sub, sizeof(sub), "%d snapshot%s", n, n == 1 ? "" : "s");
            }
        } else {
            snprintf(sub, sizeof(sub), "A to read it");
            sc = rgba(0x8a, 0x92, 0xb0, (u8)(255 * a));
        }
        ui_text(FONT_MENU, 12, 1.2f, lx + 42, y + 23, 18, sc, sub);
        y += 52;
    }
    /* save */
    y += 10;
    int done = 0, total = 0;
    bool saving = mcard_saving(&done, &total);
    int unsaved = st == MC_READY ? mcard_unsaved() : 0;
    char label[48];
    if (saving) snprintf(label, sizeof(label), "Saving %d of %d", done + 1 > total ? total : done + 1, total);
    else if (saved_flash > 0 && unsaved == 0) snprintf(label, sizeof(label), "Saved");
    else if (unsaved > 0) snprintf(label, sizeof(label), "Save %d new", unsaved);
    else snprintf(label, sizeof(label), "Save to SD");
    float ba = (unsaved > 0 || saving || saved_flash > 0) ? a : a * 0.45f;
    ui_button(lx + 6, y, lw - 12, 30, label, row == ROW_SAVE, ba);
    if (saving) {
        float f = total ? (float)done / total : 0;
        gfx_rrect(lx + 6, y + 38, lw - 12, 7, 2, rgba(0, 0, 0, (u8)(115 * a)));
        gfx_rrect_outline(lx + 6, y + 38, lw - 12, 7, 2, 1, rgba(200, 210, 230, (u8)(76 * a)));
        if (f > 0) gfx_rect_vgrad(lx + 6, y + 38, (lw - 12) * clamp01(f), 7, rgba(0x7a, 0x6c, 0xff, (u8)(255 * a)),
                                  rgba(0x3b, 0x2c, 0xb6, (u8)(255 * a)));
    } else {
        ui_text_center(FONT_MENU, 11, 1.1f, lx + lw / 2, y + 36, 16, rgba(0x8a, 0x92, 0xb0, (u8)(255 * a)), "to DCIM/MELEE as PNG");
    }

    /* the snapshots, on the right */
    float gx = lx + lw + 22, gw = px + pw - 18 - gx, gy = top + 28, gh = py + ph - 10 - 64 - 10 - gy;
    ui_text(FONT_MENU, 13, 1.5f, gx + 4, top, 22, rgba(0x73, 0x92, 0xff, (u8)(255 * a)), "Super Smash Bros. Melee snapshots");
    gfx_rect(gx, gy - 2, gw, 1, rgba(200, 210, 230, (u8)(50 * a)));
    int n = mcard_count(), pending = st == MC_LOOKING ? mcard_reading() : 0;
    if (n + pending == 0) {
        float cx = gx + gw / 2, cy = gy + gh / 2;
        char big[80];
        if (st == MC_LOOKING) { ui_spinner(cx, cy - 34, 14, g_time, a); draw_status(cx, cy + 6, a, "Reading the memory card...", NULL); }
        else if (st == MC_NO_CARD) {
            snprintf(big, sizeof(big), "There's no memory card in Slot %c.", slot ? 'B' : 'A');
            draw_status(cx, cy, a, big, "Put one in, then press A.");
        } else if (st == MC_SD_SLOT) {
            snprintf(big, sizeof(big), "Slot %c holds your SD card adapter.", slot ? 'B' : 'A');
            draw_status(cx, cy, a, big, "Put the memory card in the other slot.");
        } else if (st == MC_ERROR) draw_status(cx, cy, a, mcard_message(), "Press A to try again.");
        else draw_status(cx, cy, a, "No Melee snapshots on this card.", "Snapshots taken in Melee's Camera Mode show up here.");
        return;
    }
    int cols = gw > 500 ? 4 : 3;
    float gap = 10, cw = (gw - gap * (cols - 1)) / cols, th = cw * 0.75f, chh = th + 24;
    int rows = (n + pending + cols - 1) / cols;
    float maxs = rows * (chh + gap) - gap - gh;
    if (maxs < 0) maxs = 0;
    if (grid_target < 0) grid_target = 0;
    if (grid_target > maxs) grid_target = maxs;
    gfx_set_clip(gx - 4, gy, gw + 8, gh);
    for (int i = 0; i < n + pending; i++) {
        float x = gx + (i % cols) * (cw + gap), y2 = gy + 6 + (i / cols) * (chh + gap) - grid_scroll;
        if (y2 > gy + gh || y2 + chh < gy) continue;
        McSnap *s = i < n ? mcard_get(i) : NULL;
        ui_thumb_frame(x, y2, cw, th, a);
        if (s && s->thumb_ok) gfx_image_draw(&s->thumb, x + 2, y2 + 2, cw - 4, th - 4, rgba(255, 255, 255, (u8)(255 * a)));
        else {
            float p = 0.5f + 0.5f * sinf(g_time * 3 + i);
            gfx_rect(x + 2, y2 + 2, cw - 4, th - 4, rgba(30, 38, 46, (u8)((140 + 40 * p) * a)));
            if (s && s->bad) {
                gfx_line(x + cw * 0.4f, y2 + th * 0.35f, x + cw * 0.6f, y2 + th * 0.65f, 2, rgba(255, 92, 124, (u8)(200 * a)));
                gfx_line(x + cw * 0.6f, y2 + th * 0.35f, x + cw * 0.4f, y2 + th * 0.65f, 2, rgba(255, 92, 124, (u8)(200 * a)));
            }
        }
        if (!s) continue;
        /* a badge: New or Saved */
        if (!s->bad) {
            const char *b = s->saved ? "Saved" : "New";
            float bw = gfx_text_width_sp(FONT_MENU_BOLD, 11, 1.1f, b, -1) + 12;
            Color bc = s->saved ? rgba(0x2e, 0x8a, 0x52, (u8)(230 * a)) : rgba(0x5a, 0x48, 0xe8, (u8)(230 * a));
            gfx_rrect(x + cw - bw - 6, y2 + 6, bw, 17, 3, bc);
            ui_text(FONT_MENU_BOLD, 11, 1.1f, x + cw - bw, y2 + 6, 17, rgba(255, 255, 255, (u8)(255 * a)), b);
        }
        if (s->bad) {
            /* why it couldn't be read (scrolls if it's long) */
            float tw = gfx_text_width_sp(FONT_MENU, 11, 1.0f, s->why, -1);
            float tx = tw < cw - 4 ? x + (cw - tw) / 2 : x + 2;
            ui_text_fit_scroll(FONT_MENU, 11, 1.0f, tx, y2 + th + 4, cw - 4, 18, rgba(0xff, 0x8c, 0xa0, (u8)(255 * a)), s->why);
        } else {
            ui_text_center(FONT_MENU, 12, 1.0f, x + cw / 2, y2 + th + 4, 18, soft, s->when);
        }
    }
    gfx_clear_clip();
    /* more above or below */
    if (maxs > 0) {
        float bar = gh * gh / (gh + maxs);
        float by = gy + (gh - bar) * (grid_scroll / maxs);
        gfx_rect(px + pw - 12, by, 3, bar, rgba(200, 210, 230, (u8)(90 * a)));
    }
}

static void transfer_draw(float open) {
    float X = UI_X;
    ui_background_grid(ramp(open, 0, 0.35f));
    float pa = ramp(open, 0.45f, 0.85f);
    float px = X + 22, py = 18, pw = UI_W - 44, ph = 400;
    ui_panel(px, py, pw, ph, pa);
    ui_ptitle(px, py, pw, "Transfer", NULL, pa);
    char right[96];
    Color rc = rgba(0x8f, 0xe0, 0xa8, 255);
    if (tab == TAB_PHONE) {
        NetState st = net_state();
        if (st == NET_UP) snprintf(right, sizeof(right), "\x7f %s", net_message());
        else if (st == NET_FAILED) { snprintf(right, sizeof(right), "\x7f Not connected"); rc = rgba(0xff, 0x8c, 0xa0, 255); }
        else { snprintf(right, sizeof(right), "\x7f Connecting..."); rc = rgba(0xff, 0xd8, 0x6a, 255); }
    } else {
        snprintf(right, sizeof(right), "Melee snapshots \x7f Slot %c", mcard_slot() ? 'B' : 'A');
        rc = rgba(0xc9, 0xd2, 0xee, 255);
    }
    rc.a = (u8)(255 * pa);
    ui_text_right(FONT_MENU, 15, 1.6f, px + pw - 16, py + 9, 15, rc, right);
    /* the tabs */
    const char *names[2] = { "Phone", "Memory Card" };
    float tx = px + 14, ty = py + 40;
    for (int i = 0; i < 2; i++) {
        float w = gfx_text_width_sp(FONT_MENU, 13, 1.4f, names[i], -1) + 24;
        ui_row(tx, ty, w, 25, tab == i, pa);
        ui_text(FONT_MENU, 13, 1.4f, tx + 12, ty, 25, rgba(0xdf, 0xe4, 0xee, (u8)(255 * pa)), names[i]);
        tx += w + 6;
    }
    float top = py + 78;
    float ta = pa * ease_out(clamp01(tab_t / 0.25f));
    if (tab == TAB_PHONE) {
        draw_phone(open, px, py, pw, ph, ta, top);
        return;
    }
    draw_card(px, py, pw, ph, ta, top);
    float dy = py + ph - 10 - 64;
    ui_desc(px + 10, dy, pw - 20, 64, pa);
    Color dc = rgba(0xec, 0xeb, 0xff, (u8)(255 * pa));
    ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 6, 24, dc, "Copies Super Smash Bros. Melee snapshots to the SD card as");
    ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 30, 24, dc, "PNG pictures. Nothing on the memory card is changed.");
    const char *h[] = { "S:Select", "LR:Tab", "B:Back", "A:Confirm" };
    ui_hints(h, 4, pa, input_stick_dir());
}

const Screen screen_transfer = { transfer_enter, transfer_update, transfer_draw, NULL };
