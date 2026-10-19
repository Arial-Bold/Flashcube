/* logo.h - the opening animation (FLASHCUBE logo folding into the cube). */
#pragma once
float logo_duration(void);          /* seconds until the menu is fully there */
void  logo_draw(float t);           /* t: seconds since the start */
float logo_nav_alpha(float t);      /* how far the menu cube has faded in */
float logo_labels_alpha(float t);
float logo_hints_alpha(float t);
