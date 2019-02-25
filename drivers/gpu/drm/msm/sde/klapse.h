#ifndef _LINUX_KLAPSE_H
#define _LINUX_KLAPSE_H

/* Required variables for external access. Change as per use */
extern void set_rgb_slider(u32 bl_lvl);

// This file uses generalised K_### defines
// The interpretation (right argument) should be the respective color's var that you
// include as extern via the CUSTOM_HEADER above
#define K_RED    kcal_red
#define K_GREEN  kcal_green
#define K_BLUE   kcal_blue

#define K_TYPE   unsigned short

extern K_TYPE K_RED, K_GREEN, K_BLUE;

#endif  /* _LINUX_KLAPSE_H */
