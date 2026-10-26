/* mcard.h - Super Smash Bros. Melee snapshots on a memory card, saved to
 * the SD card as PNG pictures (sd:/DCIM/MELEE). The card is only read,
 * never changed.
 *
 * A background thread does the slow parts: it reads the card once when a
 * slot is opened (keeping each snapshot's picture data, 50-80 KB, so saving
 * needs no card), makes small thumbnails, and writes the PNG files. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

#define MCARD_DIR "sd:/DCIM/MELEE"
#define MCARD_MAX 64

typedef enum {
    MC_IDLE,        /* nothing opened yet */
    MC_LOOKING,     /* reading the card */
    MC_READY,       /* the list is ready (it may be empty) */
    MC_NO_CARD,     /* no memory card in the slot */
    MC_SD_SLOT,     /* the SD card adapter is in this slot */
    MC_ERROR,       /* the card couldn't be read (message says why) */
} McState;

typedef struct {
    char file[64];         /* "Melee_2026-10-25_214606.png" */
    char when[32];         /* "25 Oct  21:46" */
    int64_t time;          /* seconds since 1970 */
    bool bad;              /* the picture couldn't be read */
    bool saved;            /* already on the SD card */
    bool thumb_ok;
    GfxImage thumb;        /* 160x120 */
} McSnap;

void mcard_open(int slot);          /* 0 = slot A, 1 = slot B; reads the card again */
void mcard_close(void);             /* forgets the list and frees the thumbnails */
McState mcard_state(void);
int mcard_slot(void);
const char *mcard_message(void);    /* why, for MC_ERROR */
int mcard_count(void);              /* snapshots found (thumbnails may still be coming) */
int mcard_reading(void);            /* how many are still being read, while MC_LOOKING */
McSnap *mcard_get(int i);
int mcard_unsaved(void);

/* Saves every snapshot not already on the SD card. */
void mcard_save_new(void);
/* true while saving; *done counts finished ones, of *total */
bool mcard_saving(int *done, int *total);
int mcard_just_saved(void);         /* how many the last save wrote */

void mcard_tick(void);              /* once a frame on the main thread */
