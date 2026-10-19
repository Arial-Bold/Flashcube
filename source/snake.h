/* snake.h - Photo Snake, the secret on the back of the cube (hold Z and push
 * up while the cube shows Album). The snake eats tiny thumbnails of your
 * photos and grows. Drawn in the back face's 180x180 plane. */
#pragma once
#include <stdbool.h>

void snake_reset(void);          /* back to the "press A" screen */
void snake_update(void);         /* once per frame while the back face is shown */
void snake_draw(float alpha);    /* in the face's own coordinates (0..180) */
bool snake_playing(void);
