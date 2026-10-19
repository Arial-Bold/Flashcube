/* album.c - the album: a grid of thumbnails with tabs (All, years, Folders,
 * Favorites). Also used to pick a photo for a Playtime game. */
#include "app.h"
#include "photos.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "settings.h"
#include "cube.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>

#define COLS 4
#define ROWS 3
#define CW 129.0f
#define CH 82.0f
#define GX 14.0f
#define GY 12.0f

enum { TAB_ALL, TAB_YEAR1, TAB_YEAR2, TAB_YEAR3, TAB_FOLDERS, TAB_FAVS };
static int tabs[8], ntabs, tab_years[3], tab = 0;
static int folder_open = -1;          /* in the Folders tab: the folder being looked at, or -1 */
static int folder_sel;
static float scroll, scroll_target;   /* in rows */
static int built_for_count = -1;
static float sel_flash;

static const char *tab_name(int t, char *buf, size_t cap) {
    switch (t) {
    case TAB_ALL: return "All";
    case TAB_FOLDERS: return "Folders";
    case TAB_FAVS: return "Favorites";
    default: snprintf(buf, cap, "%d", tab_years[t - TAB_YEAR1]); return buf;
    }
}

static void build_tabs(void) {
    int years[16];
    int ny = photos_years(years, 16);
    ntabs = 0;
    tabs[ntabs++] = TAB_ALL;
    for (int i = 0; i < ny && i < 3; i++) { tab_years[i] = years[i]; tabs[ntabs++] = TAB_YEAR1 + i; }
    if (photos_folder_count() > 1) tabs[ntabs++] = TAB_FOLDERS;
    tabs[ntabs++] = TAB_FAVS;
}

static int tab_index(int t) {
    for (int i = 0; i < ntabs; i++) if (tabs[i] == t) return i;
    return 0;
}

/* fills g_view for the current tab */
static void build_view(void) {
    int keep = (g_view_pos >= 0 && g_view_pos < g_view_count) ? g_view[g_view_pos] : -1;
    switch (tab) {
    case TAB_FOLDERS:
        if (folder_open >= 0) {
            g_view_count = photos_view(VIEW_FOLDER, folder_open, g_view, VIEW_MAX);
            const Folder *f = photos_folder(folder_open);
            snprintf(g_view_name, sizeof(g_view_name), "%s", f ? f->name : "Folder");
        } else {
            g_view_count = 0;
            snprintf(g_view_name, sizeof(g_view_name), "Folders");
        }
        break;
    case TAB_FAVS:
        g_view_count = photos_view(VIEW_FAVORITES, 0, g_view, VIEW_MAX);
        snprintf(g_view_name, sizeof(g_view_name), "Favorites");
        break;
    case TAB_ALL:
        g_view_count = photos_view(VIEW_ALL, 0, g_view, VIEW_MAX);
        snprintf(g_view_name, sizeof(g_view_name), "All");
        break;
    default:
        g_view_count = photos_view(VIEW_YEAR, tab_years[tab - TAB_YEAR1], g_view, VIEW_MAX);
        snprintf(g_view_name, sizeof(g_view_name), "%d", tab_years[tab - TAB_YEAR1]);
        break;
    }
    g_view_pos = 0;
    for (int i = 0; i < g_view_count; i++) if (g_view[i] == keep) { g_view_pos = i; break; }
    built_for_count = photos_count();
}

static int item_count(void) {
    if (tab == TAB_FOLDERS && folder_open < 0) return photos_folder_count();
    return g_view_count;
}

static int sel(void) { return (tab == TAB_FOLDERS && folder_open < 0) ? folder_sel : g_view_pos; }
static void set_sel(int s) {
    if (tab == TAB_FOLDERS && folder_open < 0) folder_sel = s;
    else g_view_pos = s;
}

static void keep_visible(void) {
    int row = sel() / COLS;
    if (row < scroll_target) scroll_target = (float)row;
    if (row > scroll_target + ROWS - 1) scroll_target = (float)(row - ROWS + 1);
}

static void album_enter(int arg) {
    (void)arg;
    build_tabs();
    if (tab >= TAB_YEAR1 && tab <= TAB_YEAR3 && tab_index(tab) == 0) tab = TAB_ALL;
    build_view();
    keep_visible();
    scroll = scroll_target;
    sel_flash = 0;
}

static void album_update(void) {
    if (photos_scanning()) return;
    if (built_for_count != photos_count()) { build_tabs(); build_view(); }
    sel_flash += g_dt;
    int n = item_count();
    int d = input_dir();
    int s = sel();
    if (n > 0 && d != DIR_NONE) {
        int ns = s;
        if (d == DIR_LEFT && s % COLS > 0) ns--;
        else if (d == DIR_RIGHT && s % COLS < COLS - 1 && s + 1 < n) ns++;
        else if (d == DIR_UP && s >= COLS) ns -= COLS;
        else if (d == DIR_DOWN) ns = s + COLS < n ? s + COLS : (s / COLS < (n - 1) / COLS ? n - 1 : s);
        if (ns != s) { set_sel(ns); sound_play(SFX_HOVER); sel_flash = 0; keep_visible(); }
    }
    if (input_repeat(BTN_L) || input_repeat(BTN_R)) {
        int ti = tab_index(tab) + (input_held(BTN_R) ? 1 : -1);
        if (ti >= 0 && ti < ntabs) {
            tab = tabs[ti];
            folder_open = -1;
            build_view();
            scroll = scroll_target = 0;
            keep_visible();
            scroll = scroll_target;
            sound_play(SFX_HOVER);
        }
    }
    if (input_pressed(BTN_A) && n > 0) {
        sound_play(SFX_SELECT);
        if (tab == TAB_FOLDERS && folder_open < 0) {
            folder_open = folder_sel;
            build_view();
            scroll = scroll_target = 0;
        } else if (g_pick_for) {
            app_go((ScreenId)g_pick_for, g_view[g_view_pos]);
        } else {
            app_go(SCR_VIEWER, 0);
        }
    } else if (input_pressed(BTN_Y) && g_view_count > 0 && !g_pick_for) {
        sound_play(SFX_SELECT);
        app_go(SCR_SLIDESETUP, 1);
    } else if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        if (tab == TAB_FOLDERS && folder_open >= 0) {
            folder_sel = folder_open;
            folder_open = -1;
            build_view();
            scroll = scroll_target = 0;
            keep_visible();
        } else if (g_pick_for) {
            app_go(SCR_PLAYTIME, 0);
        } else {
            app_back_to_cube(SIDE_ALBUM);
        }
    }
    scroll += (scroll_target - scroll) * clamp01(g_dt * 14);
}

static void draw_tabs(float x, float y, float al) {
    float tx = x;
    for (int i = 0; i < ntabs; i++) {
        char buf[16];
        const char *name = tab_name(tabs[i], buf, sizeof(buf));
        float w = gfx_text_width_sp(FONT_MENU, 13, 1.4f, name, -1) + 24;
        ui_row(tx, y, w, 25, tabs[i] == tab, al);
        ui_text(FONT_MENU, 13, 1.4f, tx + 12, y, 25, rgba(0xdf, 0xe4, 0xee, (u8)(255 * al)), name);
        tx += w + 6;
    }
}

static void album_draw(float open) {
    float X = UI_X;
    ui_background_grid(ramp(open, 0, 0.35f));
    float pa = ramp(open, 0.45f, 0.85f);
    ui_panel(X + 22, 18, 596, 400, pa);
    char right[64] = "";
    const char *title = g_pick_for ? "Pick a photo" : "Album";
    if (g_pick_for) {
        const char *g = g_pick_for == PICK_PUZZLE ? "Photo Puzzle" : g_pick_for == PICK_DOODLE ? "Doodle" : "Color Looks";
        snprintf(right, sizeof(right), "for %s", g);
    } else if (tab == TAB_FOLDERS && folder_open < 0) {
        snprintf(right, sizeof(right), "%d folders", photos_folder_count());
    } else {
        snprintf(right, sizeof(right), "%s \x7f %d photo%s", g_view_name, g_view_count, g_view_count == 1 ? "" : "s");
    }
    ui_ptitle(X + 22, 18, 596, title, right, pa);
    if (!photos_scanning()) draw_tabs(X + 36, 58, pa);

    float x0 = X + 40, y0 = 96;
    int n = item_count();
    float swirl_done = ui_swirl_duration(COLS * ROWS);
    if (photos_scanning()) {
        ui_spinner(X + 320, 220, 16, g_time, pa);
        ui_text_center(FONT_MENU, 14, 1.2f, X + 320, 250, 24, rgba(0xdf, 0xe4, 0xff, (u8)(255 * pa)), "Reading your photos...");
    } else if (n == 0) {
        const char *msg = tab == TAB_FAVS ? "No favorites yet. Mark a photo with Favorite in the photo menu."
                          : photos_status()[0] ? photos_status() : "No photos here yet.";
        ui_text_center(FONT_MENU, 14, 1.2f, X + 320, 200, 24, rgba(0xdf, 0xe4, 0xff, (u8)(255 * pa)), msg);
        if (photos_count() == 0)
            ui_text_center(FONT_MENU, 13, 1.2f, X + 320, 230, 24, rgba(0xc9, 0xd2, 0xee, (u8)(255 * pa)),
                           "Put photos in the DCIM folder, or send some with Transfer.");
    } else {
        gfx_set_clip(X + 26, 90, 588, 3 * (CH + GY) + 6);
        int first_row = (int)scroll;
        bool swirl_now = open < swirl_done;
        for (int r = first_row; r <= first_row + (swirl_now ? ROWS - 1 : ROWS); r++) {
            for (int c = 0; c < COLS; c++) {
                int i = r * COLS + c;
                if (i >= n) break;
                float x = x0 + c * (CW + GX), y = y0 + (r - scroll) * (CH + GY);
                int slot = (r - first_row) * COLS + c;
                float a = 1;
                bool swirling = open < swirl_done;
                if (swirling) {
                    gfx_clear_clip();
                    a = ui_swirl_begin(slot, COLS * ROWS, x, y, CW, CH, open, 150, 35);
                }
                ui_thumb_frame(x, y, CW, CH, a);
                if (tab == TAB_FOLDERS && folder_open < 0) {
                    int list[1];
                    if (photos_view(VIEW_FOLDER, i, list, 1) > 0) app_draw_thumb(list[0], x + 3, y + 3, CW - 6, CH - 6, a);
                    const Folder *f = photos_folder(i);
                    gfx_rect_vgrad(x + 3, y + CH - 30, CW - 6, 27, rgba(0, 0, 0, 0), rgba(0, 0, 0, (u8)(200 * a)));
                    char line[64];
                    snprintf(line, sizeof(line), "%s  %d", f ? f->name : "", f ? f->count : 0);
                    ui_text(FONT_MENU_BOLD, 12, 1, x + 8, y + CH - 22, 18, rgba(255, 255, 255, (u8)(255 * a)), line);
                } else {
                    app_draw_thumb(g_view[i], x + 3, y + 3, CW - 6, CH - 6, a);
                    Photo *p = photos_get(g_view[i]);
                    if (p && p->fav) ui_text(FONT_MENU, 13, 0, x + CW - 20, y + 5, 16, rgba(0xff, 0xd8, 0x4a, (u8)(255 * a)), "\x84");
                }
                if (swirling) {
                    ui_swirl_end();
                    gfx_set_clip(X + 26, 90, 588, 3 * (CH + GY) + 6);
                }
            }
        }
        gfx_clear_clip();
        /* the selection: a gold frame */
        int s = sel();
        float ra = ramp(open, swirl_done - 0.35f, swirl_done);
        if (s >= 0 && s < n) {
            float x = x0 + (s % COLS) * (CW + GX), y = y0 + (s / COLS - scroll) * (CH + GY);
            if (y > 60 && y < 370) {
                float pop = 1 + 0.06f * (1 - clamp01(sel_flash / 0.18f));
                float pw = CW * pop, ph = CH * pop;
                ui_gold_ring(x - (pw - CW) / 2, y - (ph - CH) / 2, pw, ph, 0.5f + 0.5f * sinf(g_time * 4), ra);
            }
        }
        /* prefetch the next page */
        if (!(tab == TAB_FOLDERS && folder_open < 0)) {
            int last = ((int)scroll + ROWS) * COLS;
            for (int i = last; i < last + COLS * 2 && i < n; i++) photos_prefetch(g_view[i]);
            int firsti = (int)scroll * COLS;
            for (int i = firsti - COLS; i < firsti; i++) if (i >= 0) photos_prefetch(g_view[i]);
        }
    }

    /* description */
    float da = ramp(open, swirl_done - 0.5f, swirl_done - 0.1f);
    float dy = 374 + 16 * (1 - ease_out(da));
    ui_desc(X + 32, dy, 576, 34, da);
    Color dc = rgba(0xec, 0xeb, 0xff, (u8)(255 * da)), dc2 = rgba(0xc9, 0xc6, 0xff, (u8)(255 * da));
    if (n > 0 && !(tab == TAB_FOLDERS && folder_open < 0)) {
        Photo *p = photos_get(g_view[g_view_pos]);
        if (p) {
            char info[96], date[24];
            photos_format_date(p, date, sizeof(date));
            if (p->w) snprintf(info, sizeof(info), "%s \x7f %d \x80 %d", date, p->w, p->h);
            else snprintf(info, sizeof(info), "%s", date);
            char name[48];
            snprintf(name, sizeof(name), "%s", p->name);
            ui_text(FONT_MENU, 13, 1.2f, X + 44, dy, 34, dc, name);
            ui_text_right(FONT_MENU, 13, 1.2f, X + 596, dy, 34, dc2, info);
        }
    } else if (n > 0) {
        const Folder *f = photos_folder(folder_sel);
        if (f) {
            ui_text(FONT_MENU, 13, 1.2f, X + 44, dy, 34, dc, f->name);
            char c[32];
            snprintf(c, sizeof(c), "%d photo%s", f->count, f->count == 1 ? "" : "s");
            ui_text_right(FONT_MENU, 13, 1.2f, X + 596, dy, 34, dc2, c);
        }
    }

    float ha = ramp(open, swirl_done - 0.4f, swirl_done);
    if (g_pick_for) {
        const char *h[] = { "S:Select", "LR:Tab", "B:Back", "A:Choose" };
        ui_hints(h, 4, ha, input_stick_dir());
    } else {
        const char *h[] = { "S:Select", "LR:Tab", "Y:Slideshow", "B:Back", "A:Confirm" };
        ui_hints(h, 5, ha, input_stick_dir());
    }
}

const Screen screen_album = { album_enter, album_update, album_draw, NULL };
