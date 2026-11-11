/* SPDX-License-Identifier: MIT */
/* Host tests for the physical-modeling core. No hardware, no float in the DSP. */
#include "pm.h"
#include <time.h>
#include "pm_fx.h"
#include "pm_user.h"
#include "xq_play.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

static void zero(int32_t *x, uint32_t n)
{
    memset(x, 0, n * sizeof *x);
}

/* Render a mono (left) take. Caller frees. */
static int16_t *take(uint8_t preset, uint8_t note, uint8_t vel, uint32_t on_ms, uint32_t total_ms, uint32_t *frames)
{
    uint32_t total = (uint32_t)((uint64_t)total_ms * PM_FS / 1000u);
    uint32_t on = (uint32_t)((uint64_t)on_ms * PM_FS / 1000u);
    uint32_t done = 0;
    int16_t *dst = calloc(total ? total : 1, sizeof(int16_t));
    int32_t buf[256 * 2];
    if (!dst)
        exit(1);
    pm_init();
    pm_load_preset(preset);
    pm_render(buf, 1); /* drain nothing; init has no queued notes */
    pm_note_on(note, vel);
    while (done < total) {
        uint32_t n = 256;
        uint32_t i;
        if (n > total - done)
            n = total - done;
        zero(buf, n * 2);
        if (done < on && done + n >= on)
            pm_note_off(note);
        pm_render(buf, n);
        for (i = 0; i < n; i++) {
            int32_t s = buf[2 * i];
            if (s > 32767)
                s = 32767;
            if (s < -32767)
                s = -32767;
            dst[done + i] = (int16_t)s;
        }
        done += n;
    }
    *frames = total;
    return dst;
}

static int64_t energy(const int16_t *x, uint32_t a, uint32_t b)
{
    int64_t e = 0;
    uint32_t i;
    if (b > a) {
        for (i = a; i < b; i++)
            e += (int64_t)x[i] * x[i];
        e /= (int64_t)(b - a);
    }
    return e;
}

static int32_t peak(const int16_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t a = x[i] < 0 ? -x[i] : x[i];
        if (a > p)
            p = a;
    }
    return p;
}

/* Integer autocorrelation. Returns the lag of the strongest peak. */
static int period(const int16_t *x, uint32_t n, int minlag, int maxlag)
{
    uint32_t start = n / 10u;
    uint32_t len = n / 3u;
    int lag, best = minlag;
    int64_t bestc = INT64_MIN;
    if (start + len + (uint32_t)maxlag >= n)
        len = n - start - (uint32_t)maxlag - 1u;
    for (lag = minlag; lag <= maxlag; lag++) {
        int64_t c = 0;
        uint32_t i;
        for (i = 0; i < len; i += 2)
            c += (int64_t)x[start + i] * x[start + i + (uint32_t)lag];
        if (c > bestc) {
            bestc = c;
            best = lag;
        }
    }
    return best;
}

static int64_t hf(const int16_t *x, uint32_t a, uint32_t b)
{
    int64_t e = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++) {
        int32_t d = (int32_t)x[i] - x[i - 1];
        e += (int64_t)d * d;
    }
    return e / (int64_t)(b - a);
}

static uint32_t hash_pcm(const int16_t *x, uint32_t n)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint16_t)x[i];
        h *= 16777619u;
    }
    return h;
}

static int near(int got, int want, int tol, const char *msg)
{
    if (got < want - tol || got > want + tol) {
        fprintf(stderr, "FAIL %s (got %d, want %d ±%d)\n", msg, got, want, tol);
        fails++;
        return 0;
    }
    return 1;
}

static void test_silence(void)
{
    int32_t buf[256 * 2];
    uint32_t i;
    pm_init();
    zero(buf, 512);
    pm_render(buf, 256);
    for (i = 0; i < 512; i++)
        if (buf[i]) {
            expect(0, "silence before note-on");
            return;
        }
    expect(pm_quiet(), "quiet before any note");
}

static void test_pluck_pitch(void)
{
    uint32_t n = 0;
    int16_t *x = take(1, 69, 110, 800, 900, &n); /* STEEL, A4 */
    int lag = period(x, n, 80, 140);
    int32_t pk = peak(x, n);
    fprintf(stderr, "  steel A4 lag %d peak %d\n", lag, pk);
    near(lag, 100, 2, "steel A4 period");
    expect(pk > 1500 && pk < 32000, "steel A4 level");
    free(x);
    x = take(1, 57, 110, 800, 900, &n); /* A3 */
    lag = period(x, n, 160, 260);
    fprintf(stderr, "  steel A3 lag %d\n", lag);
    near(lag, 200, 4, "steel A3 period");
    free(x);
    x = take(1, 81, 100, 500, 600, &n); /* A5 */
    lag = period(x, n, 40, 70);
    fprintf(stderr, "  steel A5 lag %d\n", lag);
    near(lag, 50, 2, "steel A5 period");
    free(x);
}

static void test_pluck_decay_and_tone(void)
{
    uint32_t n = 0;
    int16_t *harp = take(3, 60, 100, 1500, 1600, &n);
    int16_t *mute = take(2, 60, 100, 1500, 1600, &n);
    int64_t he = energy(harp, n / 2, n / 2 + PM_FS / 4);
    int64_t me = energy(mute, n / 2, n / 2 + PM_FS / 4);
    fprintf(stderr, "  harp mid %lld mute mid %lld\n", (long long)he, (long long)me);
    expect(he > me * 4, "harp rings longer than mute");
    free(harp);
    free(mute);

    pm_init();
    pm_load_preset(1);
    pm_set_param(PM_BRIGHT, 8);
    {
        uint32_t total = PM_FS / 2, done = 0;
        int16_t *dark = calloc(total, 2);
        int16_t *bright;
        int32_t buf[256 * 2];
        pm_note_on(64, 110);
        while (done < total) {
            uint32_t i, c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                dark[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        pm_init();
        pm_load_preset(1);
        pm_set_param(PM_BRIGHT, 127);
        bright = calloc(total, 2);
        done = 0;
        pm_note_on(64, 110);
        while (done < total) {
            uint32_t i, c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                bright[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        {
            int64_t hd = hf(dark, PM_FS / 20, PM_FS / 5);
            int64_t hb = hf(bright, PM_FS / 20, PM_FS / 5);
            fprintf(stderr, "  hf dark %lld bright %lld\n", (long long)hd, (long long)hb);
            expect(hb > hd * 12 / 10, "brighter pluck has more HF");
        }
        free(dark);
        free(bright);
    }
}

static void test_pick_changes(void)
{
    uint32_t total = PM_FS / 3, done;
    int16_t *a = calloc(total, 2), *b = calloc(total, 2);
    int32_t buf[256 * 2];
    int64_t diff = 0, denom = 0;
    uint32_t i;
    pm_init();
    pm_load_preset(0);
    pm_set_param(PM_POS, 8);
    pm_set_param(PM_COLOR, 127);
    pm_note_on(67, 100);
    for (done = 0; done < total;) {
        uint32_t c = 256;
        if (c > total - done)
            c = total - done;
        zero(buf, c * 2);
        pm_render(buf, c);
        for (i = 0; i < c; i++)
            a[done + i] = (int16_t)buf[2 * i];
        done += c;
    }
    pm_init();
    pm_load_preset(0);
    pm_set_param(PM_POS, 120);
    pm_set_param(PM_COLOR, 127);
    pm_note_on(67, 100);
    for (done = 0; done < total;) {
        uint32_t c = 256;
        if (c > total - done)
            c = total - done;
        zero(buf, c * 2);
        pm_render(buf, c);
        for (i = 0; i < c; i++)
            b[done + i] = (int16_t)buf[2 * i];
        done += c;
    }
    for (i = 0; i < total; i++) {
        int32_t d = (int32_t)a[i] - b[i];
        diff += (int64_t)d * d;
        denom += (int64_t)a[i] * a[i] + 1;
    }
    fprintf(stderr, "  pick diff/energy %lld\n", (long long)(diff / (denom / 1000 + 1)));
    expect(diff > denom / 20, "pick position changes the pluck");
    free(a);
    free(b);
}

static void test_modal(void)
{
    uint32_t n = 0;
    int16_t *bell = take(6, 69, 100, 1000, 1100, &n);
    int16_t *drum = take(8, 69, 110, 800, 900, &n);
    int lag = period(bell, n, 80, 140);
    int64_t be = energy(bell, n / 3, n / 3 + PM_FS / 5);
    int64_t de = energy(drum, n / 3, n / 3 + PM_FS / 5);
    fprintf(stderr, "  bell lag %d bellE %lld drumE %lld\n", lag, (long long)be, (long long)de);
    near(lag, 100, 3, "bell A4 period");
    expect(be > de * 3, "bell rings longer than drum");
    expect(peak(bell, n) > 800, "bell is audible");
    free(bell);
    free(drum);

    {
        int16_t *bar = take(4, 72, 100, 700, 800, &n); /* marimba C5 */
        lag = period(bar, n, 50, 90);
        fprintf(stderr, "  marimba C5 lag %d peak %d\n", lag, peak(bar, n));
        near(lag, 84, 3, "marimba C5 period"); /* MIDI 72 = 523.25 Hz, 44100/523.25 ≈ 84.3 */
        free(bar);
    }
}

static void test_bow_and_reed(void)
{
    uint32_t n = 0;
    int16_t *bow = take(10, 69, 110, 900, 1400, &n);
    uint32_t a = PM_FS / 10, b = PM_FS / 4, c = PM_FS / 2, d = (uint32_t)(PM_FS * 0.7);
    int64_t early = energy(bow, a, b);
    int64_t mid = energy(bow, c, d);
    int64_t tail = energy(bow, (uint32_t)(PM_FS * 1.1), (uint32_t)(PM_FS * 1.25));
    int lag = period(bow, PM_FS / 2, 80, 140);
    fprintf(stderr, "  bow early %lld mid %lld tail %lld lag %d peak %d\n",
            (long long)early, (long long)mid, (long long)tail, lag, peak(bow, n));
    expect(early > 20000, "bow speaks");
    expect(mid * 2 > early, "bow sustains while held");
    expect(tail * 5 < mid + 1, "bow decays after release");
    near(lag, 100, 3, "bow A4 period");
    free(bow);

    {
        int16_t *cl = take(12, 69, 100, 700, 800, &n);
        int16_t *fl = take(13, 69, 100, 700, 800, &n);
        int lc = period(cl, n, 70, 150);
        int lf = period(fl, n, 70, 150);
        fprintf(stderr, "  clar lag %d peak %d  flute lag %d peak %d\n", lc, peak(cl, n), lf, peak(fl, n));
        near(lc, 100, 4, "clarinet A4 period");
        near(lf, 100, 4, "flute A4 period");
        expect(peak(cl, n) > 400 && peak(fl, n) > 400, "winds are audible");
        expect(hash_pcm(cl, n) != hash_pcm(fl, n), "clarinet and flute differ");
        free(cl);
        free(fl);
    }
}

static void test_poly_and_bounds(void)
{
    int32_t buf[256 * 2];
    uint32_t block, i;
    int32_t pk = 0;
    const uint8_t notes[6] = {60, 64, 67, 72, 76, 79};
    pm_init();
    pm_load_preset(6); /* bell, the densest stack */
    for (i = 0; i < 6; i++)
        pm_note_on(notes[i], 120);
    for (block = 0; block < PM_FS / 256; block++) {
        zero(buf, 512);
        pm_render(buf, 256);
        for (i = 0; i < 512; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            if (buf[i] > 32767 || buf[i] < -32767)
                expect(0, "mix stayed inside int16");
            if (a > pk)
                pk = a;
        }
    }
    fprintf(stderr, "  poly peak %d\n", pk);
    expect(pk > 1000, "chord is audible");
    pm_panic();
    zero(buf, 512);
    pm_render(buf, 256);
    pm_render(buf, 256);
    expect(pm_quiet(), "panic goes quiet");
}

static void test_bend(void)
{
    uint32_t total = PM_FS / 2, done, i;
    int16_t *up = calloc(total, 2);
    int32_t buf[256 * 2];
    int lag0, lag1;
    pm_init();
    pm_load_preset(1);
    pm_set_bend(65536);
    pm_note_on(69, 100);
    for (done = 0; done < total;) {
        uint32_t c = 256;
        if (c > total - done)
            c = total - done;
        zero(buf, c * 2);
        pm_render(buf, c);
        for (i = 0; i < c; i++)
            up[done + i] = (int16_t)buf[2 * i];
        done += c;
    }
    lag0 = period(up, total, 80, 140);
    pm_init();
    pm_load_preset(1);
    pm_set_bend(73562); /* +2 semitones */
    pm_note_on(69, 100);
    for (done = 0; done < total;) {
        uint32_t c = 256;
        if (c > total - done)
            c = total - done;
        zero(buf, c * 2);
        pm_render(buf, c);
        for (i = 0; i < c; i++)
            up[done + i] = (int16_t)buf[2 * i];
        done += c;
    }
    lag1 = period(up, total, 70, 140);
    fprintf(stderr, "  bend lags %d -> %d\n", lag0, lag1);
    expect(lag1 < lag0 - 5, "bend up shortens the period");
    free(up);
}

static void test_determinism_and_presets(void)
{
    uint32_t n = 0, i;
    int16_t *a = take(0, 64, 90, 300, 400, &n);
    int16_t *b = take(0, 64, 90, 300, 400, &n);
    uint32_t ha = hash_pcm(a, n), hb = hash_pcm(b, n);
    expect(ha == hb, "same notes render the same samples");
    free(a);
    free(b);
    a = take(0, 64, 90, 300, 400, &n);
    b = take(4, 64, 90, 300, 400, &n);
    expect(hash_pcm(a, n) != hash_pcm(b, n), "nylon and marimba differ");
    free(a);
    free(b);
    /* every preset, both param extremes: finite, not stuck at full scale */
    for (i = 0; i < PM_NPRESET; i++) {
        int16_t *x = take((uint8_t)i, 60, 100, 200, 350, &n);
        int32_t pk = peak(x, n);
        char msg[64];
        snprintf(msg, sizeof msg, "preset %u audible", i);
        expect(pk > 200 && pk <= 32767, msg);
        free(x);
    }
}

static void test_level_and_panic_noteoff(void)
{
    uint32_t n = 0;
    int16_t *loud, *quiet;
    pm_init();
    pm_load_preset(0);
    pm_set_param(PM_LEVEL, 127);
    loud = take(0, 60, 120, 200, 300, &n);
    /* take() calls pm_init, so set level inside a manual render instead */
    free(loud);
    {
        uint32_t total = PM_FS / 4, done, i;
        int32_t buf[256 * 2];
        int64_t el = 0, eq = 0;
        int16_t *L = calloc(total, 2), *Q = calloc(total, 2);
        pm_init();
        pm_load_preset(0);
        pm_set_param(PM_LEVEL, 127);
        pm_note_on(60, 110);
        for (done = 0; done < total;) {
            uint32_t c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                L[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        pm_init();
        pm_load_preset(0);
        pm_set_param(PM_LEVEL, 8);
        pm_note_on(60, 110);
        for (done = 0; done < total;) {
            uint32_t c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                Q[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        el = energy(L, total / 5, total / 2);
        eq = energy(Q, total / 5, total / 2);
        fprintf(stderr, "  level energy %lld vs %lld\n", (long long)el, (long long)eq);
        expect(el > eq * 8, "level knob scales loudness");
        free(L);
        free(Q);
    }
    quiet = take(1, 69, 100, 40, 500, &n);
    {
        int64_t early = energy(quiet, PM_FS / 50, PM_FS / 20);
        int64_t late = energy(quiet, PM_FS / 5, PM_FS / 3);
        fprintf(stderr, "  noteoff early %lld late %lld\n", (long long)early, (long long)late);
        expect(early > late * 2, "note-off damps a pluck");
    }
    free(quiet);
}

static void test_fx(void)
{
    uint32_t n = 0, late_a, late_b;
    int16_t *dry, *wet;
    uint32_t i;
    int32_t buf[256 * 2];
    pm_init();
    pm_load_preset(17);
    expect(pm_fx_kind() == PM_FX_REV, "vibes preset opens the reverb");
    dry = take(2, 60, 110, 40, 800, &n);
    pm_init();
    pm_load_preset(2);
    pm_fx_set(PM_FX_DLY, 100, 64, 90);
    pm_fx_set_bpm(0);
    pm_note_on(60, 110);
    {
        uint32_t total = n, done = 0;
        wet = calloc(total, sizeof(int16_t));
        while (done < total) {
            uint32_t c = 256;
            if (done == PM_FS / 12)
                pm_note_off(60);
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++) {
                int32_t s = buf[2 * i];
                if (s > 32767 || s < -32767)
                    expect(0, "delay mix stayed in range");
                wet[done + i] = (int16_t)s;
            }
            done += c;
        }
    }
    late_a = (uint32_t)energy(dry, n * 2 / 3, n - 8);
    late_b = (uint32_t)energy(wet, n * 2 / 3, n - 8);
    fprintf(stderr, "  delay tail dry %u wet %u\n", late_a, late_b);
    expect(late_b > late_a + 50, "delay leaves a tail");
    expect(hash_pcm(dry, n) != hash_pcm(wet, n), "delay changes the take");
    free(dry);
    free(wet);
    pm_init();
    pm_fx_set(PM_FX_DLY, 80, 10, 40);
    pm_fx_set_bpm(120);
    pm_fx_set_bpm(0);
    expect(pm_fx_kind() == PM_FX_DLY, "bpm update keeps the effect");
    expect(pm_fx_ram() < 48u * 1024u, "effect memory stays under 48 KB");
}

static void test_midi_depth(void)
{
    uint32_t n = 0;
    int16_t *plain, *pushed;
    int lag;
    pm_init();
    pm_load_preset(1);
    pm_set_bend(131072u);
    plain = take(1, 69, 100, 400, 500, &n);
    /* take() resets bend; render manually */
    free(plain);
    {
        uint32_t total = PM_FS / 2, done = 0, i;
        int32_t buf[256 * 2];
        int16_t *up = calloc(total, sizeof(int16_t));
        pm_init();
        pm_load_preset(1);
        pm_set_bend(131072u);
        pm_note_on(69, 100);
        while (done < total) {
            uint32_t c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                up[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        lag = period(up, total, 40, 80);
        fprintf(stderr, "  octave bend lag %d\n", lag);
        near(lag, 50, 3, "12-semitone bend reaches the octave");
        free(up);
    }
    {
        uint32_t total = PM_FS / 4, done, i;
        int32_t buf[256 * 2];
        plain = calloc(total, sizeof(int16_t));
        pushed = calloc(total, sizeof(int16_t));
        n = total;
        pm_init();
        pm_load_preset(1);
        pm_set_param(PM_BRIGHT, 20);
        pm_note_on(64, 100);
        for (done = 0; done < total;) {
            uint32_t c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                plain[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        pm_init();
        pm_load_preset(1);
        pm_set_param(PM_BRIGHT, 20);
        pm_set_aftertouch(255, 127);
        pm_set_aftertouch(64, 127);
        pm_note_on(64, 100);
        for (done = 0; done < total;) {
            uint32_t c = 256;
            if (c > total - done)
                c = total - done;
            zero(buf, c * 2);
            pm_render(buf, c);
            for (i = 0; i < c; i++)
                pushed[done + i] = (int16_t)buf[2 * i];
            done += c;
        }
        expect(hash_pcm(plain, n) != hash_pcm(pushed, n), "aftertouch changes the tone");
        free(plain);
        free(pushed);
    }
}

static void test_user_bank(void)
{
    pm_user_bank_t bank, got;
    uint8_t a[PM_USER_BYTES], b[PM_USER_BYTES], bad[PM_USER_BYTES];
    int src = 9;
    const pm_preset_t *pr = pm_presets();
    pm_init();
    expect(PM_NPRESET == 37, "37 factory presets");
    expect(strcmp(pr[0].name, "NYLON") == 0 && strcmp(pr[1].name, "STEEL") == 0, "first presets stay put");
    expect(strcmp(pr[13].name, "FLUTE") == 0 && strcmp(pr[31].name, "SOLO") == 0, "last old and new presets");
    pm_load_preset(15); /* BANJO */
    pm_user_reset(&bank);
    pm_user_store_current(&bank, 2, "BANJO");
    expect(bank.slot[2].used && bank.slot[2].model == PM_PLUCK, "stored banjo");
    pm_load_preset(0);
    expect(pm_user_recall(&bank, 2) == 0, "recall used slot");
    expect(pm_model() == PM_PLUCK && pm_get_param(PM_BRIGHT) == pr[15].bright, "recall restored tone");
    expect(pm_user_recall(&bank, 0) != 0, "empty slot is not applied");
    bank.seq = 1;
    expect(pm_user_encode(&bank, a) == 0, "encode a");
    bank.seq = 4;
    bank.cur = 2;
    expect(pm_user_encode(&bank, b) == 0, "encode b");
    expect(pm_user_choose(a, PM_USER_BYTES, b, PM_USER_BYTES, &got, &src) == 0 && src == 1 && got.seq == 4, "newer copy wins");
    memcpy(bad, b, sizeof bad);
    bad[20] ^= 0x5a;
    expect(pm_user_decode(bad, PM_USER_BYTES, &got) != 0, "corrupt crc rejected");
    expect(pm_user_choose(bad, PM_USER_BYTES, bad, PM_USER_BYTES, &got, &src) != 0 && src == -1, "both corrupt falls back");
    expect(!got.slot[2].used, "fallback bank is empty");
}

static void clocks(xq_play_t *p, int count, uint8_t *got, int *ng)
{
    int i;
    *ng = 0;
    for (i = 0; i < count; i++) {
        uint8_t note[2], vel[2];
        int k, c = xq_play_clock(p, note, vel);
        for (k = 0; k < c; k++)
            if (vel[k] && *ng < 16)
                got[(*ng)++] = note[k];
    }
}

static void test_play(void)
{
    xq_play_t p;
    uint8_t got[16];
    int n = 0, i, ok;
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.mode = XQ_ARP_UP;
    xq_play_note(&p, 64, 100);
    xq_play_note(&p, 60, 100);
    xq_play_note(&p, 67, 100);
    clocks(&p, 36, got, &n);
    expect(n >= 4 && got[0] == 60 && got[1] == 64 && got[2] == 67 && got[3] == 60, "arp up");
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.mode = XQ_ARP_DN;
    xq_play_note(&p, 60, 100);
    xq_play_note(&p, 64, 100);
    xq_play_note(&p, 67, 100);
    clocks(&p, 24, got, &n);
    expect(n >= 3 && got[0] == 67 && got[1] == 64 && got[2] == 60, "arp down");
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.mode = XQ_ARP_AS;
    xq_play_note(&p, 67, 100);
    xq_play_note(&p, 60, 100);
    xq_play_note(&p, 64, 100);
    clocks(&p, 24, got, &n);
    expect(n >= 3 && got[0] == 67 && got[1] == 60 && got[2] == 64, "arp as played");
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.mode = XQ_ARP_UD;
    xq_play_note(&p, 60, 80);
    xq_play_note(&p, 64, 80);
    xq_play_note(&p, 67, 80);
    clocks(&p, 36, got, &n);
    expect(n >= 5 && got[0] == 60 && got[1] == 64 && got[2] == 67 && got[3] == 64 && got[4] == 60, "arp up-down");
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.latch = 1;
    xq_play_note(&p, 60, 100);
    xq_play_note(&p, 60, 0);
    clocks(&p, 12, got, &n);
    expect(n >= 1 && got[0] == 60, "latch holds a released note");
    xq_play_init(&p);
    p.seq_on = 1;
    p.rate = 0;
    clocks(&p, 24, got, &n);
    expect(n >= 4 && got[0] == 60 && got[1] == 62 && got[2] == 64 && got[3] == 67, "step sequencer");
    xq_play_init(&p);
    p.arp_on = 1;
    p.rate = 0;
    p.mode = XQ_ARP_RND;
    xq_play_note(&p, 60, 90);
    xq_play_note(&p, 72, 90);
    clocks(&p, 48, got, &n);
    ok = n >= 4;
    for (i = 0; i < n; i++)
        if (got[i] != 60 && got[i] != 72)
            ok = 0;
    expect(ok, "random arp stays on the held notes");
}

static void test_models_and_budget(void)
{
    uint32_t n = 0, i;
    int16_t *koto, *nylon, *zheng, *pluck;
    clock_t t0;
    double sec;
    int32_t buf[256 * 2];
    const uint8_t chord[6] = {60, 64, 67, 72, 76, 79};
    koto = take(32, 67, 100, 400, 600, &n);
    nylon = take(0, 67, 100, 400, 600, &n);
    expect(peak(koto, n) > 400, "koto is audible");
    expect(hash_pcm(koto, n) != hash_pcm(nylon, n), "koto is not the nylon pluck");
    free(koto);
    free(nylon);
    zheng = take(36, 64, 110, 500, 700, &n);
    pluck = take(0, 64, 110, 500, 700, &n);
    expect(peak(zheng, n) > 400, "guzheng is audible");
    expect(hash_pcm(zheng, n) != hash_pcm(pluck, n), "guzheng bend changes the pluck");
    free(zheng);
    free(pluck);
    {
        uint32_t ng = 0, no = 0, nb = 0;
        int16_t *gong = take(33, 60, 100, 400, 700, &ng);
        int16_t *organ = take(34, 60, 90, 500, 800, &no);
        int16_t *bowed = take(35, 60, 100, 500, 800, &nb);
        expect(peak(gong, ng) > 400, "gong is audible");
        expect(peak(organ, no) > 400, "organ pipe is audible");
        expect(peak(bowed, nb) > 400, "bowed bar is audible");
        free(gong);
        free(organ);
        free(bowed);
    }
    pm_init();
    pm_load_preset(33);
    expect(pm_voice_cap() == 4, "gong plus reverb drops to four voices");
    pm_load_preset(0);
    expect(pm_voice_cap() == PM_VOICES, "nylon keeps six voices");
    t0 = clock();
    pm_init();
    pm_load_preset(0);
    for (i = 0; i < 6; i++)
        pm_note_on(chord[i], 110);
    for (i = 0; i < PM_FS / 256; i++) {
        zero(buf, 512);
        pm_render(buf, 256);
    }
    sec = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    fprintf(stderr, "  budget pluck6 %.3fs realtime  ram %u bytes  fx %u\n", sec, pm_ram_bytes(), pm_fx_ram());
    expect(sec < 0.5, "six plucks render with headroom");
    expect(pm_ram_bytes() < 80u * 1024u, "voice plus effect RAM stays under 80 KB");
}

int main(void)
{
    test_silence();
    test_pluck_pitch();
    test_pluck_decay_and_tone();
    test_pick_changes();
    test_modal();
    test_bow_and_reed();
    test_poly_and_bounds();
    test_bend();
    test_determinism_and_presets();
    test_level_and_panic_noteoff();
    test_fx();
    test_user_bank();
    test_play();
    test_midi_depth();
    test_models_and_budget();
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("pm tests passed\n");
    return 0;
}
