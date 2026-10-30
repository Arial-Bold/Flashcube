/* diag.h - freeze recorder. Parts of Flashcube leave a short note of what
 * they're doing ("breadcrumbs"). A watchdog thread notices when the screen
 * stops updating for 10 seconds and writes the notes to freeze.log in
 * Flashcube's folder, so a freeze can be traced afterwards. */
#pragma once

enum { DIAG_MAIN, DIAG_MUSIC, DIAG_SLOTS };

void diag_init(void);
void diag_note(int slot, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void diag_frame(void);   /* call once per frame */
const char *diag_get(int slot);
