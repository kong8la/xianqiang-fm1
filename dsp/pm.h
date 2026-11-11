/* SPDX-License-Identifier: MIT */
/* Physical-modeling voice for the M-VAVE FM-1 (and a desktop host).
 * Fixed point only: no floating-point types, no allocation, no 64-bit division.
 * The firmware that links this into Dinghy is GPL-3.0-only as a whole;
 * this file stays MIT. */
#ifndef XQ_PM_H
#define XQ_PM_H
#include <stdint.h>

#define PM_FS 44100u
#define PM_VOICES 6u
#define PM_NPARAM 6u
#define PM_NPRESET 32u

enum {
    PM_DECAY = 0,
    PM_BRIGHT,
    PM_POS,
    PM_COLOR,
    PM_EXCITE,
    PM_LEVEL
};

enum {
    PM_PLUCK = 0,
    PM_BAR,
    PM_BELL,
    PM_MEMB,
    PM_BOW,
    PM_REED,
    PM_NMODEL
};

typedef struct {
    const char *name;   /* short uppercase label for the panel */
    const char *kind;   /* PLUCK, BAR, BELL, MEMB, BOW, REED */
    uint8_t model;
    uint8_t decay, bright, pos, color, excite;
    uint8_t fx, fx_mix, fx_time, fx_fb;         /* 0 fills when an older line omits them */
} pm_preset_t;

const pm_preset_t *pm_presets(void);
const char *pm_param_name(uint8_t model, uint8_t param);

void pm_init(void);
void pm_load_preset(uint8_t index);                 /* sets model + five tone params; keeps level */
void pm_set_model(uint8_t model);
void pm_set_param(uint8_t param, uint8_t value);    /* 0..127 */
uint8_t pm_get_param(uint8_t param);
uint8_t pm_preset_index(void);
uint8_t pm_model(void);

void pm_set_bend(uint32_t bend_q16);                /* 65536 = unison, 32768..131072 */
void pm_set_aftertouch(uint8_t note, uint8_t value); /* note 255 = channel */
void pm_set_mod(uint8_t mod);                       /* 0..127, vibrato depth */
void pm_set_sustain(uint8_t on);
void pm_set_expr(uint8_t expr);                     /* 0..127 */

void pm_note_on(uint8_t note, uint8_t vel);
void pm_note_off(uint8_t note);
void pm_all_notes_off(void);                        /* sustain pedal still holds */
void pm_panic(void);                                /* silence now */

/* Add n stereo frames (L, R, ...) to out. out is not cleared. */
void pm_render(int32_t *out, uint32_t n);

int pm_quiet(void);                                 /* 1 = nothing left to hear */
uint8_t pm_last_note(void);                         /* 255 = none yet */
#endif
