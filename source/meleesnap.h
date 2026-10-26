/* meleesnap.h - reading Super Smash Bros. Melee snapshots from the memory
 * card. Melee keeps each snapshot in its own save file, scrambled per 8 KB
 * sector with HAL's HSD_Encrypt, and the picture inside is a JPEG whose AC
 * coefficients are coded HAL's own way, so ordinary JPEG decoders can't read
 * it. These functions undo both. Format worked out from the doldecomp/melee
 * decompilation (lbsnap.c, card.c, crypt.c, hsd_3B34.c, hsd_3B5C.c). */
#ifndef MELEESNAP_H
#define MELEESNAP_H
#include <stdint.h>
#include <stdbool.h>

#define MSNAP_W 640
#define MSNAP_H 480

/* `file` is the save file's data as stored on the card (what CARD_Read gives
 * back, or a .gci without its 64-byte header). Writes the HAL JPEG stream
 * into *jpg (malloc'd; free it) and returns its length, or one of these: */
enum {
    MSNAP_X_SHORT = -1,       /* too small to be a snapshot */
    MSNAP_X_MEMORY = -2,      /* not enough memory */
    MSNAP_X_SCRAMBLED = -3,   /* the first block doesn't unscramble (not Melee's, or damaged) */
    MSNAP_X_MISSING = -4,     /* a block of it is missing or damaged */
    MSNAP_X_NOT_SNAP = -5,    /* unscrambled, but not a picture */
};
int msnap_extract(const uint8_t *file, int file_len, uint8_t **jpg);

/* What the decoder says. */
enum { MSNAP_OK = 0, MSNAP_NO_MEMORY, MSNAP_NOT_PICTURE, MSNAP_DAMAGED };

/* Decodes the HAL JPEG into 640x480 RGB (3 bytes per pixel). */
int msnap_decode(const uint8_t *jpg, int len, uint8_t *rgb);
/* The same, 16 rows at a time (row y..y+15, 640x16 RGB), using about 30 KB. */
typedef void (*MsnapRows)(void *ctx, int y, const uint8_t *band);
int msnap_decode_rows(const uint8_t *jpg, int len, MsnapRows rows, void *ctx);

/* true for a Melee snapshot's directory entry: game GAL? from company 01
 * (any region) and a name of digits only (Melee names snapshots by time) */
bool msnap_is_snapshot(const char *name, const uint8_t gamecode[4], const uint8_t company[2]);
#endif
