/* SPDX-License-Identifier: MIT */
#include "pm_user.h"
#include "pm.h"
#include "pm_fx.h"
#include <string.h>

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void pm_user_reset(pm_user_bank_t *b)
{
    uint32_t i;
    memset(b, 0, sizeof *b);
    for (i = 0; i < PM_USER_N; i++)
        memcpy(b->slot[i].name, "EMPTY", 6);
}

static void pack_slot(uint8_t *p, const pm_user_slot_t *s)
{
    uint32_t i;
    p[0] = s->used;
    p[1] = s->model;
    for (i = 0; i < 6u; i++)
        p[2u + i] = s->p[i];
    p[8] = s->fx_kind;
    p[9] = s->fx_mix;
    p[10] = s->fx_time;
    p[11] = s->fx_fb;
    for (i = 0; i < 8u; i++)
        p[12u + i] = (uint8_t)s->name[i];
}

static void unpack_slot(pm_user_slot_t *s, const uint8_t *p)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    s->used = p[0];
    s->model = p[1];
    for (i = 0; i < 6u; i++)
        s->p[i] = p[2u + i];
    s->fx_kind = p[8];
    s->fx_mix = p[9];
    s->fx_time = p[10];
    s->fx_fb = p[11];
    for (i = 0; i < 7u; i++)
        s->name[i] = (char)p[12u + i];
    s->name[7] = 0;
}

int pm_user_encode(const pm_user_bank_t *b, uint8_t *dst)
{
    uint32_t i;
    memset(dst, 0, PM_USER_BYTES);
    put_u32(dst, 0x53554744u); /* "DGUS" */
    dst[4] = 1;
    dst[5] = (uint8_t)PM_USER_N;
    dst[6] = b->cur;
    put_u32(dst + 8, b->seq);
    for (i = 0; i < PM_USER_N; i++)
        pack_slot(dst + 12u + i * 20u, &b->slot[i]);
    put_u32(dst + 172, crc32(dst, 172));
    return 0;
}

int pm_user_decode(const uint8_t *src, uint32_t n, pm_user_bank_t *b)
{
    uint32_t i;
    pm_user_bank_t tmp;
    if (!src || n < PM_USER_BYTES || !b)
        return -1;
    if (get_u32(src) != 0x53554744u || src[4] != 1u || src[5] != PM_USER_N)
        return -1;
    if (get_u32(src + 172) != crc32(src, 172))
        return -1;
    memset(&tmp, 0, sizeof tmp);
    tmp.cur = src[6];
    if (tmp.cur >= PM_USER_N)
        tmp.cur = 0;
    tmp.seq = get_u32(src + 8);
    for (i = 0; i < PM_USER_N; i++)
        unpack_slot(&tmp.slot[i], src + 12u + i * 20u);
    *b = tmp;
    return 0;
}

int pm_user_choose(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb, pm_user_bank_t *out, int *src)
{
    pm_user_bank_t A, B;
    int oa, ob;
    if (!out || !src)
        return -1;
    oa = pm_user_decode(a, na, &A) == 0;
    ob = pm_user_decode(b, nb, &B) == 0;
    if (oa && ob) {
        if (B.seq > A.seq) {
            *out = B;
            *src = 1;
        } else {
            *out = A;
            *src = 0;
        }
        return 0;
    }
    if (oa) {
        *out = A;
        *src = 0;
        return 0;
    }
    if (ob) {
        *out = B;
        *src = 1;
        return 0;
    }
    pm_user_reset(out);
    *src = -1;
    return -1;
}

void pm_user_store_current(pm_user_bank_t *b, uint8_t slot, const char *name)
{
    pm_user_slot_t *s;
    uint32_t i;
    if (!b || slot >= PM_USER_N)
        return;
    s = &b->slot[slot];
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->model = pm_model();
    for (i = 0; i < PM_NPARAM && i < 6u; i++)
        s->p[i] = pm_get_param((uint8_t)i);
    pm_fx_get(&s->fx_kind, &s->fx_mix, &s->fx_time, &s->fx_fb);
    if (!name || !name[0])
        name = "USER";
    for (i = 0; i < 7u && name[i]; i++)
        s->name[i] = name[i];
    b->cur = slot;
}

int pm_user_recall(const pm_user_bank_t *b, uint8_t slot)
{
    const pm_user_slot_t *s;
    uint32_t i;
    uint8_t level;
    if (!b || slot >= PM_USER_N || !b->slot[slot].used)
        return -1;
    s = &b->slot[slot];
    if (s->model >= PM_NMODEL)
        return -1;
    level = pm_get_param(PM_LEVEL);
    pm_set_model(s->model);
    for (i = 0; i < PM_NPARAM && i < 6u; i++) {
        if (i == PM_LEVEL)
            continue;
        pm_set_param((uint8_t)i, s->p[i]);
    }
    pm_set_param(PM_LEVEL, level);
    pm_fx_set(s->fx_kind, s->fx_mix, s->fx_time, s->fx_fb);
    return 0;
}
