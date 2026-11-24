#ifndef CT_SOUND_H
#define CT_SOUND_H

/* Little sound effects, made in code (no sound files). Needs the DSP
 * firmware (sdmc:/3ds/dspfirm.cdc, made during the homebrew setup); without
 * it the app is simply silent. */

#include <stdbool.h>

typedef enum { SND_TAP, SND_POP, SND_WATER, SND_GOAL, SND_SCAN, SND_COUNT } Sound;

int sound_init(void);
void sound_exit(void);
void sound_enable(bool on);
void sound_play(Sound s);

#endif
