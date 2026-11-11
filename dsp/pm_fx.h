/* SPDX-License-Identifier: MIT */
/* One send effect, fixed point, shared by every voice. */
#ifndef XQ_PM_FX_H
#define XQ_PM_FX_H
#include <stdint.h>

enum { PM_FX_OFF = 0, PM_FX_REV, PM_FX_DLY, PM_FX_CHO, PM_FX_N };

void pm_fx_init(void);
void pm_fx_set(uint8_t kind, uint8_t mix, uint8_t time, uint8_t fb);
void pm_fx_set_bpm(uint16_t bpm);               /* 0: delay time is free, else tempo-synced */
void pm_fx_get(uint8_t *kind, uint8_t *mix, uint8_t *time, uint8_t *fb);
uint8_t pm_fx_kind(void);
void pm_fx_process(int32_t *l, int32_t *r);     /* dry in, dry+wet out */
uint32_t pm_fx_ram(void);
#endif
