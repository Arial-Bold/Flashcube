/* looks.h - colour looks for photos (used by Color Looks and the cards). */
#pragma once
#include <stdint.h>

enum { LOOK_ORIGINAL, LOOK_SEPIA, LOOK_MONO, LOOK_POP, LOOK_COOL, LOOK_WARM, LOOK_VINTAGE, LOOK_NEGATIVE, LOOK_COUNT };
extern const char *const look_names[LOOK_COUNT];
extern const char *const look_help[LOOK_COUNT];
/* Changes one pixel in place. */
void look_apply(int look, uint8_t *r, uint8_t *g, uint8_t *b);
