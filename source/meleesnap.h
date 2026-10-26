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
 * into *jpg (malloc'd; free it) and returns its length, or -1. */
int msnap_extract(const uint8_t *file, int file_len, uint8_t **jpg);

/* Decodes the HAL JPEG into 640x480 RGB (3 bytes per pixel). */
bool msnap_decode(const uint8_t *jpg, int len, uint8_t *rgb);

/* true for a Melee snapshot's directory entry: game GAL? from company 01
 * (any region) and a name of digits only (Melee names snapshots by time) */
bool msnap_is_snapshot(const char *name, const uint8_t gamecode[4], const uint8_t company[2]);
#endif
