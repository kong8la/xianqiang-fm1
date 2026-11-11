/* SPDX-License-Identifier: MIT */
/* User presets for 道哥合成器. The bytes are what blob 0 and blob 1 store
 * (FM-1 0xF4000 and 0xF5000, the app's allow-list). Never a boot or SDK sector.
 * Two copies: a save writes the older one, so one erase does not destroy the
 * last good bank. A bad magic or CRC leaves the caller on defaults. */
#ifndef XQ_PM_USER_H
#define XQ_PM_USER_H
#include <stdint.h>

#define PM_USER_N 8u
#define PM_USER_BYTES 176u

typedef struct {
    uint8_t used;
    uint8_t model;
    uint8_t p[6];
    uint8_t fx_kind, fx_mix, fx_time, fx_fb;
    char name[8];
} pm_user_slot_t;

typedef struct {
    uint32_t seq;
    uint8_t cur;
    pm_user_slot_t slot[PM_USER_N];
} pm_user_bank_t;

void pm_user_reset(pm_user_bank_t *b);
/* 0 on a valid image. *b is unchanged on failure. */
int pm_user_decode(const uint8_t *src, uint32_t n, pm_user_bank_t *b);
/* Writes PM_USER_BYTES. Returns 0 on success. */
int pm_user_encode(const pm_user_bank_t *b, uint8_t *dst);
/* Prefer the copy with the greater seq. *src is 0, 1, or -1 when neither is valid. */
int pm_user_choose(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb, pm_user_bank_t *out, int *src);

void pm_user_store_current(pm_user_bank_t *b, uint8_t slot, const char *name);
/* 0 if the slot was used and applied. Keeps PM_LEVEL. */
int pm_user_recall(const pm_user_bank_t *b, uint8_t slot);
#endif
