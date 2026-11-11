/* SPDX-License-Identifier: MIT */
/* Reverb: four damped combs. Delay: one 8192-sample line (~186 ms).
 * Chorus: a short modulated delay. Only the selected effect runs. */
#include "pm_fx.h"
#include "pm.h"

static int16_t rev[887 + 911 + 941 + 977];
static const uint16_t COMB_N[4] = {887, 911, 941, 977};
static uint16_t comb_off[4], comb_i[4];
static int32_t comb_lp[4];
static int16_t dly[8192];
static uint16_t di;
static int16_t cho[2048];
static uint16_t ci;
static uint32_t lfo;
static uint8_t fx_kind, fx_mix, fx_time, fx_fb;
static uint16_t bpm;

static int32_t fx_clip(int32_t x)
{
    if (x > 32767)
        return 32767;
    if (x < -32767)
        return -32767;
    return x;
}

uint32_t pm_fx_ram(void)
{
    return (uint32_t)(sizeof rev + sizeof dly + sizeof cho);
}

void pm_fx_init(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 4u; i++) {
        comb_off[i] = (uint16_t)n;
        comb_i[i] = 0;
        comb_lp[i] = 0;
        n += COMB_N[i];
    }
    for (i = 0; i < sizeof rev / sizeof rev[0]; i++)
        rev[i] = 0;
    for (i = 0; i < 8192u; i++)
        dly[i] = 0;
    for (i = 0; i < 2048u; i++)
        cho[i] = 0;
    di = ci = 0;
    lfo = 0;
    fx_kind = fx_mix = fx_time = fx_fb = 0;
    bpm = 0;
}

void pm_fx_set(uint8_t k, uint8_t m, uint8_t t, uint8_t f)
{
    if (k >= PM_FX_N)
        k = PM_FX_OFF;
    if (m > 127)
        m = 127;
    if (t > 127)
        t = 127;
    if (f > 127)
        f = 127;
    if (k != fx_kind) {
        uint32_t i;
        for (i = 0; i < sizeof rev / sizeof rev[0]; i++)
            rev[i] = 0;
        for (i = 0; i < 8192u; i++)
            dly[i] = 0;
        for (i = 0; i < 2048u; i++)
            cho[i] = 0;
        for (i = 0; i < 4u; i++)
            comb_lp[i] = 0;
    }
    fx_kind = k;
    fx_mix = m;
    fx_time = t;
    fx_fb = f;
}

void pm_fx_set_bpm(uint16_t v) { bpm = v > 300 ? 300 : v; }
void pm_fx_get(uint8_t *k, uint8_t *m, uint8_t *t, uint8_t *f)
{
    if (k) *k = fx_kind;
    if (m) *m = fx_mix;
    if (t) *t = fx_time;
    if (f) *f = fx_fb;
}
uint8_t pm_fx_kind(void) { return fx_kind; }

static int32_t reverb(int32_t in)
{
    uint32_t i;
    int32_t sum = 0;
    int32_t fbv = 12000 + (int32_t)fx_fb * 140;
    int32_t damp = 6000 + (int32_t)(127 - fx_time) * 160;
    if (fbv > 30000)
        fbv = 30000;
    if (damp > 28000)
        damp = 28000;
    for (i = 0; i < 4u; i++) {
        uint16_t n = COMB_N[i];
        uint16_t idx = comb_i[i];
        int32_t y = rev[comb_off[i] + idx];
        int32_t d = comb_lp[i] + (int32_t)(((int64_t)(y - comb_lp[i]) * damp) >> 15);
        int32_t w = in + (int32_t)(((int64_t)d * fbv) >> 15);
        comb_lp[i] = d;
        rev[comb_off[i] + idx] = (int16_t)fx_clip(w);
        comb_i[i] = (uint16_t)(idx + 1u >= n ? 0 : idx + 1u);
        sum += y;
    }
    return sum >> 2;
}

static uint32_t delay_len(void)
{
    uint32_t len, div = 4u;
    if (bpm >= 40u) {
        if (fx_time >= 96u)
            div = 1u;
        else if (fx_time >= 64u)
            div = 3u;
        else if (fx_time >= 32u)
            div = 2u;
        len = (uint32_t)((uint64_t)PM_FS * 60u / bpm / div);
    } else
        len = 700u + (uint32_t)fx_time * 48u;
    if (len < 64u)
        len = 64u;
    if (len > 8000u)
        len = 8000u;
    return len;
}

static int32_t delay(int32_t in)
{
    uint32_t len = delay_len();
    uint32_t rd = (di - len) & 8191u;
    int32_t y = dly[rd];
    int32_t w = in + (int32_t)(((int64_t)y * fx_fb) >> 8);
    dly[di] = (int16_t)fx_clip(w);
    di = (uint16_t)((di + 1u) & 8191u);
    return y;
}

static int32_t chorus(int32_t in)
{
    uint32_t depth = 180u + (uint32_t)fx_time * 5u;
    uint32_t ph = lfo >> 16;
    uint32_t tri = (ph & 0x4000u) ? (0x7fffu - ((ph & 0x3fffu) << 1)) : ((ph & 0x3fffu) << 1);
    uint32_t off = 200u + (tri * depth >> 15);
    uint32_t rd;
    int32_t y;
    lfo += 90u + (uint32_t)fx_time * 4u;
    if (off >= 2048u)
        off = 2047u;
    rd = (ci - off) & 2047u;
    y = cho[rd];
    cho[ci] = (int16_t)fx_clip(in);
    ci = (uint16_t)((ci + 1u) & 2047u);
    return y;
}

void pm_fx_process(int32_t *l, int32_t *r)
{
    int32_t in, wet, m;
    if (!l || !r || fx_kind == PM_FX_OFF || !fx_mix)
        return;
    in = (*l + *r) >> 1;
    if (fx_kind == PM_FX_REV)
        wet = reverb(in);
    else if (fx_kind == PM_FX_DLY)
        wet = delay(in);
    else
        wet = chorus(in);
    m = (int32_t)fx_mix * 200;
    if (m > 24000)
        m = 24000;
    *l += (int32_t)(((int64_t)wet * m) >> 15);
    *r += (int32_t)(((int64_t)wet * m) >> 15);
}
