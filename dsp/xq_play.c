/* SPDX-License-Identifier: MIT */
#include "xq_play.h"
#include <string.h>

static const uint8_t DIV[8] = {6, 8, 12, 16, 24, 32, 48, 96};
static const uint8_t PENTA[16] = {60, 62, 64, 67, 69, 67, 64, 62, 60, 64, 67, 72, 67, 64, 60, 62};

void xq_play_init(xq_play_t *p)
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->dir = 1;
    p->gate = 4;
    p->rate = 2;
    p->seq_len = 16;
    p->rng = 0xA5A5u;
    for (i = 0; i < 16u; i++) {
        p->seq_note[i] = PENTA[i];
        p->seq_vel[i] = 100;
    }
}

static int find_held(const xq_play_t *p, uint8_t note)
{
    uint32_t i;
    for (i = 0; i < p->nheld; i++)
        if (p->held[i] == note)
            return (int)i;
    return -1;
}

static void drop_held(xq_play_t *p, uint32_t i)
{
    uint32_t j;
    for (j = i + 1u; j < p->nheld; j++) {
        p->held[j - 1u] = p->held[j];
        p->phys[j - 1u] = p->phys[j];
        p->as_ord[j - 1u] = p->as_ord[j];
    }
    if (p->nheld)
        p->nheld--;
}

void xq_play_note(xq_play_t *p, uint8_t note, uint8_t vel)
{
    int i;
    if (!p || note > 127)
        return;
    i = find_held(p, note);
    if (vel) {
        if (i < 0 && p->nheld < 8u) {
            p->held[p->nheld] = note;
            p->phys[p->nheld] = 1;
            p->as_ord[p->nheld] = note;
            p->nheld++;
        } else if (i >= 0)
            p->phys[i] = 1;
    } else if (i >= 0) {
        if (p->latch)
            p->phys[i] = 0;
        else
            drop_held(p, (uint32_t)i);
    }
}

void xq_play_rec_note(xq_play_t *p, uint8_t note, uint8_t vel)
{
    if (!p || !p->rec || !vel)
        return;
    p->seq_note[p->seq_i] = note;
    p->seq_vel[p->seq_i] = vel;
    p->seq_i = (uint8_t)((p->seq_i + 1u) % 16u);
}

static int pick(xq_play_t *p, uint8_t *note)
{
    uint8_t ord[8], n, i, j;
    if (!p->nheld)
        return 0;
    n = p->nheld;
    if (p->mode == XQ_ARP_AS) {
        for (i = 0; i < n; i++)
            ord[i] = p->as_ord[i];
    } else if (p->mode == XQ_ARP_RND) {
        p->rng = p->rng * 1664525u + 1013904223u;
        *note = (uint8_t)(p->held[p->rng % n] + 12u * p->oct_i);
        if (*note > 127)
            *note = 127;
        p->oct_i = (uint8_t)(p->oct_i < p->octaves ? p->oct_i + 1u : 0);
        return 1;
    } else {
        for (i = 0; i < n; i++)
            ord[i] = p->held[i];
        for (i = 0; i < n; i++)
            for (j = (uint8_t)(i + 1u); j < n; j++)
                if (ord[j] < ord[i]) {
                    uint8_t t = ord[i];
                    ord[i] = ord[j];
                    ord[j] = t;
                }
        if (p->mode == XQ_ARP_DN)
            for (i = 0; i < n / 2u; i++) {
                uint8_t t = ord[i];
                ord[i] = ord[n - 1u - i];
                ord[n - 1u - i] = t;
            }
    }
    if (p->idx < 0)
        p->idx = 0;
    if ((uint8_t)p->idx >= n)
        p->idx = 0;
    *note = (uint8_t)(ord[p->idx] + 12u * p->oct_i);
    if (*note > 127)
        *note = 127;
    if (p->oct_i < p->octaves)
        p->oct_i++;
    else {
        p->oct_i = 0;
        if (p->mode == XQ_ARP_UD) {
            p->idx = (int8_t)(p->idx + p->dir);
            if (p->idx >= (int8_t)n) {
                p->dir = -1;
                p->idx = (int8_t)(n > 1u ? n - 2u : 0);
            } else if (p->idx < 0) {
                p->dir = 1;
                p->idx = n > 1u ? 1 : 0;
            }
        } else
            p->idx = (int8_t)((p->idx + 1) % (int)n);
    }
    return 1;
}

int xq_play_clock(xq_play_t *p, uint8_t note[2], uint8_t vel[2])
{
    uint16_t step, hold;
    int n = 0;
    uint8_t nxt = 0, use_vel = 100;
    int have = 0;
    if (!p)
        return 0;
    step = DIV[p->rate & 7u];
    hold = (uint16_t)(step * (p->gate ? p->gate : 4u) / 8u);
    if (!hold)
        hold = 1;
    if (hold > step)
        hold = step;
    p->clk++;
    if (p->sounding && p->gate_left) {
        p->gate_left--;
        if (!p->gate_left && hold < step) {
            note[n] = p->cur;
            vel[n] = 0;
            n++;
            p->sounding = 0;
        }
    }
    if (p->clk % step)
        return n;
    if (p->arp_on && p->nheld)
        have = pick(p, &nxt);
    else if (p->seq_on) {
        uint8_t si = p->seq_i;
        uint8_t len = p->seq_len ? p->seq_len : 16u;
        if (len > 16u)
            len = 16u;
        nxt = p->seq_note[si];
        use_vel = p->seq_vel[si];
        have = use_vel != 0 && nxt != 0;
        p->seq_i = (uint8_t)((si + 1u) % len);
    }
    if (p->sounding && hold >= step) {
        note[n] = p->cur;
        vel[n] = 0;
        n++;
        p->sounding = 0;
    }
    if (have && nxt) {
        note[n] = nxt;
        vel[n] = use_vel ? use_vel : 100;
        n++;
        p->cur = nxt;
        p->sounding = 1;
        p->gate_left = hold;
    }
    return n;
}
