/* SPDX-License-Identifier: MIT */
/* Render a few stereo demos of the modeling engine. */
#include "pm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wav_write(const char *path, const int16_t *x, uint32_t frames)
{
    uint32_t data = frames * 4u;
    uint32_t riff = 36u + data;
    uint8_t h[44];
    FILE *f;
    memset(h, 0, sizeof h);
    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)riff; h[5] = (uint8_t)(riff >> 8); h[6] = (uint8_t)(riff >> 16); h[7] = (uint8_t)(riff >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16;
    h[20] = 1;
    h[22] = 2;
    h[24] = (uint8_t)PM_FS; h[25] = (uint8_t)(PM_FS >> 8);
    {
        uint32_t rate = PM_FS * 4u;
        h[28] = (uint8_t)rate; h[29] = (uint8_t)(rate >> 8); h[30] = (uint8_t)(rate >> 16); h[31] = (uint8_t)(rate >> 24);
    }
    h[32] = 4;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite(h, 1, 44, f);
    fwrite(x, 2, frames * 2u, f);
    fclose(f);
}

typedef struct {
    uint16_t at, dur;
    uint8_t preset, note, vel;
} ev_t;

static int16_t *render_events(const ev_t *ev, uint32_t n, uint32_t frames, int32_t *peak)
{
    int16_t *dst = calloc(frames * 2u, sizeof(int16_t));
    int32_t buf[256 * 2];
    uint8_t on[128] = {0};
    uint32_t done = 0, e;
    int32_t pk = 0;
    if (!dst)
        exit(1);
    pm_init();
    pm_set_param(PM_LEVEL, 100);
    while (done < frames) {
        uint32_t c = 256, i, t = done;
        if (c > frames - done)
            c = frames - done;
        for (e = 0; e < n; e++) {
            if (!on[ev[e].note] && ev[e].at >= t && ev[e].at < t + c) {
                pm_load_preset(ev[e].preset);
                pm_set_param(PM_LEVEL, 100);
                pm_note_on(ev[e].note, ev[e].vel);
                on[ev[e].note] = 1;
            }
            if (on[ev[e].note] && ev[e].at + ev[e].dur >= t && ev[e].at + ev[e].dur < t + c) {
                pm_note_off(ev[e].note);
                on[ev[e].note] = 0;
            }
        }
        memset(buf, 0, sizeof buf);
        pm_render(buf, c);
        for (i = 0; i < c; i++) {
            int32_t l = buf[2 * i], r = buf[2 * i + 1];
            int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (l > 32767) l = 32767;
            if (l < -32767) l = -32767;
            if (r > 32767) r = 32767;
            if (r < -32767) r = -32767;
            dst[(done + i) * 2u] = (int16_t)l;
            dst[(done + i) * 2u + 1u] = (int16_t)r;
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
        }
        done += c;
    }
    *peak = pk;
    return dst;
}

static void one(const char *path, const ev_t *ev, uint32_t n, uint32_t ms)
{
    uint32_t frames = (uint32_t)((uint64_t)ms * PM_FS / 1000u);
    int32_t peak = 0;
    int16_t *x = render_events(ev, n, frames, &peak);
    printf("%s  peak %d  %u ms\n", path, peak, ms);
    if (peak < 500) {
        fprintf(stderr, "demo too quiet: %s\n", path);
        exit(1);
    }
    wav_write(path, x, frames);
    free(x);
}

int main(void)
{
    const ev_t nylon[] = {
        {200, 700, 0, 60, 100}, {450, 700, 0, 64, 96}, {700, 900, 0, 67, 104},
        {1100, 1200, 1, 72, 110}, {1600, 900, 1, 76, 90},
    };
    const ev_t marimba[] = {
        {100, 380, 4, 67, 110}, {420, 380, 4, 72, 100}, {740, 380, 4, 76, 108},
        {1060, 500, 5, 79, 112}, {1500, 700, 4, 72, 96},
    };
    const ev_t bell[] = {
        {150, 1600, 6, 72, 100}, {900, 1400, 7, 79, 90}, {1500, 1200, 6, 76, 80},
    };
    const ev_t winds[] = {
        {200, 1100, 10, 62, 100}, {1500, 1100, 11, 50, 96},
        {2800, 1000, 12, 67, 100}, {4000, 1100, 13, 74, 96},
    };
    const ev_t koto[] = {
        {100, 900, 32, 62, 100}, {500, 900, 32, 67, 96}, {1000, 1200, 32, 74, 90},
    };
    const ev_t gong[] = {
        {200, 1800, 33, 48, 110}, {900, 1600, 33, 55, 90},
    };
    const ev_t zheng[] = {
        {80, 700, 36, 60, 108}, {400, 700, 36, 64, 100}, {760, 900, 36, 67, 104}, {1200, 1000, 36, 72, 96},
    };
    one("demos/nylon-steel.wav", nylon, 5, 2800);
    one("demos/marimba.wav", marimba, 5, 2400);
    one("demos/bell.wav", bell, 3, 2800);
    one("demos/bow-reed.wav", winds, 4, 5200);
    one("demos/koto.wav", koto, 3, 2400);
    one("demos/gong.wav", gong, 2, 2800);
    one("demos/zheng.wav", zheng, 4, 2400);
    return 0;
}
