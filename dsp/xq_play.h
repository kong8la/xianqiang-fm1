/* SPDX-License-Identifier: MIT */
/* Arpeggiator and 16-step sequencer. The caller feeds MIDI clocks (24 ppq). */
#ifndef XQ_PLAY_H
#define XQ_PLAY_H
#include <stdint.h>

enum { XQ_ARP_UP = 0, XQ_ARP_DN, XQ_ARP_UD, XQ_ARP_RND, XQ_ARP_AS, XQ_ARP_N };

typedef struct {
    uint8_t arp_on, mode, latch, gate, octaves, rate;
    uint8_t seq_on, seq_len, seq_i, rec;
    uint8_t seq_note[16], seq_vel[16];
    uint8_t held[8], phys[8], as_ord[8], nheld;
    int8_t dir, idx;
    uint8_t oct_i, sounding, cur;
    uint16_t clk, gate_left;
    uint32_t rng;
} xq_play_t;

void xq_play_init(xq_play_t *p);
void xq_play_note(xq_play_t *p, uint8_t note, uint8_t vel);
void xq_play_rec_note(xq_play_t *p, uint8_t note, uint8_t vel);
/* One MIDI clock. note/vel length 2: a 0 velocity is note-off. Returns 0..2. */
int xq_play_clock(xq_play_t *p, uint8_t note[2], uint8_t vel[2]);
#endif
