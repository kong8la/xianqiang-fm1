/* SPDX-License-Identifier: MIT */
/* Six-voice physical models, fixed point, 44100 Hz.
 *
 *   PLUCK  extended Karplus-Strong: noise burst, pick-position comb,
 *          brightness blends the classic one-zero loop filter, decay is
 *          the loop gain. BODY is how deep the pick comb cuts.
 *   BAR    modal bar (marimba / xylophone ratios)
 *   BELL   modal bell
 *   MEMB   modal circular membrane (Bessel-ish ratios)
 *   BOW    one-delay bowed string: bridge reflection + a stick-slip bow
 *   REED   waveguide bore. COLOR below 64 is a clarinet (quarter-wave,
 *          odd reflection); 64 and above is a flute (open pipe, jet).
 *          The bore length is chosen when the note starts.
 *
 * Polyphony is 6. Each voice owns a 2048-sample power-of-two delay so the
 * audio loop never divides. Delay memory sits in the FM-1 pool on device
 * (~24 KB) and in ordinary BSS on the host.
 *
 * 64-bit multiplies are used; 64-bit division is not (the pi32v2 build has
 * no __udivdi3). Parameters are bytes the main loop publishes; the render
 * is the only writer of voice state, via a small command queue. */
#include "pm.h"
#include "pm_tables.h"

#if defined(DINGHY_HOST) || defined(PM_NO_POOL)
#define PM_MEM
#else
#define PM_MEM __attribute__((section(".pool")))
#endif

#define PM_DLEN 2048u
#define PM_MASK 2047u
#define PM_MAX_DEL 1900u
#define PM_MODES 5u

enum { OP_ON = 1, OP_OFF = 2, OP_ALLOFF = 3, OP_PANIC = 4, OP_PEDAL = 5 };

typedef struct {
    int16_t buf[PM_DLEN];
    uint32_t w;
    uint32_t base_del;          /* unbent period, Q16 */
    uint32_t note_inc;          /* unbent fundamental phase step */
    uint32_t ph[PM_MODES];
    uint32_t mode_inc[PM_MODES];
    int32_t amp[PM_MODES];
    int16_t ratio[PM_MODES];    /* Q10, 1024 = 1.0 */
    int32_t gain_m[PM_MODES];   /* Q15, set at note-on */
    int32_t lp, prev, dc, tail;
    int32_t breath;
    uint32_t bow_ph;
    uint32_t rng;
    uint32_t age;
    uint16_t trip;
    int32_t fade;               /* Q15 output fade-in */
    uint8_t model, note, vel, held, sus, alive, flute, click;
    uint8_t pos, color, excite; /* snapshotted at note-on */
} pm_voice;

static pm_voice voices[PM_VOICES] PM_MEM;
static uint32_t voice_age;

static volatile uint8_t g_param[PM_NPARAM] = {100, 90, 48, 40, 40, 96};
static volatile uint8_t g_model;
static volatile uint8_t g_preset;
static volatile uint8_t g_mod;
static volatile uint8_t g_sus;
static volatile uint8_t g_expr = 127;
static volatile uint32_t g_bend = 65536u;
static volatile uint8_t g_ready;
static volatile uint8_t g_sounding;
static volatile uint8_t g_last = 255;

static struct {
    volatile uint8_t op, note, vel;
} cmdq[16];
static volatile uint32_t cmd_w, cmd_r;

static const pm_preset_t PRESETS[PM_NPRESET] = {
    {"NYLON", "PLUCK", PM_PLUCK, 112, 72, 46, 90, 36},
    {"STEEL", "PLUCK", PM_PLUCK, 122, 118, 24, 110, 64},
    {"MUTE", "PLUCK", PM_PLUCK, 28, 48, 70, 40, 24},
    {"HARP", "PLUCK", PM_PLUCK, 126, 96, 16, 70, 18},
    {"MARIMBA", "BAR", PM_BAR, 108, 100, 78, 28, 48},
    {"XYLO", "BAR", PM_BAR, 86, 122, 60, 48, 70},
    {"BELL", "BELL", PM_BELL, 124, 112, 42, 18, 30},
    {"CHIME", "BELL", PM_BELL, 116, 78, 28, 8, 16},
    {"DRUM", "MEMB", PM_MEMB, 24, 96, 50, 20, 100},
    {"FRAME", "MEMB", PM_MEMB, 58, 64, 64, 12, 46},
    {"VIOLIN", "BOW", PM_BOW, 112, 100, 36, 100, 78},
    {"CELLO", "BOW", PM_BOW, 118, 62, 48, 80, 52},
    {"CLARINET", "REED", PM_REED, 100, 74, 40, 22, 48},
    {"FLUTE", "REED", PM_REED, 108, 96, 56, 100, 28},
    {"GUT", "PLUCK", PM_PLUCK, 100, 60, 55, 80, 20},
    {"BANJO", "PLUCK", PM_PLUCK, 70, 120, 90, 127, 80},
    {"DULC", "PLUCK", PM_PLUCK, 118, 100, 30, 100, 40},
    {"VIBES", "BAR", PM_BAR, 120, 90, 40, 20, 30},
    {"WOOD", "BAR", PM_BAR, 90, 110, 70, 60, 55},
    {"TUBES", "BELL", PM_BELL, 126, 80, 50, 30, 20},
    {"GLOCK", "BELL", PM_BELL, 80, 120, 60, 40, 70},
    {"TOM", "MEMB", PM_MEMB, 40, 70, 40, 30, 90},
    {"CONGA", "MEMB", PM_MEMB, 36, 88, 80, 16, 70},
    {"BASS", "BOW", PM_BOW, 120, 40, 60, 90, 40},
    {"VIOLA", "BOW", PM_BOW, 100, 90, 30, 110, 90},
    {"OBOE", "REED", PM_REED, 90, 80, 36, 18, 60},
    {"SHAKU", "REED", PM_REED, 110, 70, 50, 110, 20},
    {"PIZZ", "PLUCK", PM_PLUCK, 40, 100, 40, 70, 50},
    {"LYRE", "PLUCK", PM_PLUCK, 124, 84, 20, 60, 14},
    {"TEMPLE", "BELL", PM_BELL, 100, 60, 30, 10, 12},
    {"TABLA", "MEMB", PM_MEMB, 18, 110, 55, 40, 120},
    {"SOLO", "BOW", PM_BOW, 108, 115, 28, 120, 100},
};

static const char *const PNAME_PLUCK[6] = {"DECAY", "TONE", "PICK", "BODY", "NOISE", "LEVEL"};
static const char *const PNAME_MODAL[6] = {"DECAY", "BRIGHT", "STRIKE", "STIFF", "NOISE", "LEVEL"};
static const char *const PNAME_BOW[6] = {"DECAY", "TONE", "PLACE", "PRESS", "SPEED", "LEVEL"};
static const char *const PNAME_REED[6] = {"DECAY", "TONE", "PLACE", "REED", "BREATH", "LEVEL"};

/* Mode ratios, Q10. Bar partials sit near 1 : 4 : 9 (a stiff bar); the top
 * two are milder so high notes do not alias into a buzz. Bell partials are
 * the usual 1, 2, 2.4, 3, 4.5 set. Membrane partials follow the first zeros
 * of the circular-membrane Bessel function, normalised to the fundamental. */
static const uint16_t RATIO[3][PM_MODES] = {
    {1024, 4045, 9114, 10240, 11264},
    {1024, 2048, 2458, 3072, 4608},
    {1024, 1632, 2186, 2350, 2717},
};
static const int16_t MGAIN[3][PM_MODES] = {
    {14000, 7000, 2800, 1200, 500},
    {9000, 8000, 6400, 4200, 2600},
    {12000, 8000, 4200, 2600, 1400},
};

const pm_preset_t *pm_presets(void) { return PRESETS; }

const char *pm_param_name(uint8_t model, uint8_t param)
{
    const char *const *row = PNAME_PLUCK;
    if (param >= PM_NPARAM)
        return "";
    if (model == PM_BOW)
        row = PNAME_BOW;
    else if (model == PM_REED)
        row = PNAME_REED;
    else if (model == PM_BAR || model == PM_BELL || model == PM_MEMB)
        row = PNAME_MODAL;
    return row[param];
}

uint8_t pm_get_param(uint8_t param) { return param < PM_NPARAM ? g_param[param] : 0; }
uint8_t pm_preset_index(void) { return g_preset; }
uint8_t pm_model(void) { return g_model; }
int pm_quiet(void) { return g_ready && !g_sounding; }
uint8_t pm_last_note(void) { return g_last; }

void pm_set_param(uint8_t param, uint8_t value)
{
    if (param >= PM_NPARAM)
        return;
    if (value > 127)
        value = 127;
    g_param[param] = value;
}

void pm_set_mod(uint8_t mod) { g_mod = mod > 127 ? 127 : mod; }
void pm_set_expr(uint8_t expr) { g_expr = expr > 127 ? 127 : expr; }

void pm_set_bend(uint32_t bend_q16)
{
    if (bend_q16 < 56000u)
        bend_q16 = 56000u;
    if (bend_q16 > 76000u)
        bend_q16 = 76000u;
    g_bend = bend_q16;
}

void pm_set_model(uint8_t model)
{
    if (model < PM_NMODEL)
        g_model = model;
}

void pm_load_preset(uint8_t index)
{
    const pm_preset_t *p;
    if (index >= PM_NPRESET)
        index = 0;
    p = &PRESETS[index];
    g_preset = index;
    g_model = p->model;
    g_param[PM_DECAY] = p->decay;
    g_param[PM_BRIGHT] = p->bright;
    g_param[PM_POS] = p->pos;
    g_param[PM_COLOR] = p->color;
    g_param[PM_EXCITE] = p->excite;
}

void pm_init(void)
{
    uint32_t i, k;
    for (i = 0; i < PM_VOICES; i++) {
        pm_voice *v = &voices[i];
        for (k = 0; k < PM_DLEN; k++)
            v->buf[k] = 0;
        v->alive = 0;
        v->held = 0;
        v->sus = 0;
        v->tail = 0;
        v->rng = 0x12345678u ^ (i * 0x9E3779B9u);
    }
    g_expr = 127;
    g_bend = 65536u;
    g_mod = 0;
    g_sus = 0;
    g_sounding = 0;
    g_last = 255;
    if (g_param[PM_LEVEL] == 0)
        g_param[PM_LEVEL] = 96;
    pm_load_preset(0);
    g_param[PM_LEVEL] = 96;
    cmd_w = cmd_r = 0;
    g_ready = 1;
}

static void enqueue(uint8_t op, uint8_t note, uint8_t vel)
{
    uint32_t w = cmd_w;
    uint32_t i;
    if ((uint32_t)(w - cmd_r) >= 16u)
        return;
    i = w & 15u;
    cmdq[i].note = note;
    cmdq[i].vel = vel;
    cmdq[i].op = op;
    __asm__ volatile("" ::: "memory");
    cmd_w = w + 1u;
}

void pm_note_on(uint8_t note, uint8_t vel)
{
    if (!vel || note > 127)
        return;
    enqueue(OP_ON, note, vel);
}

void pm_note_off(uint8_t note) { enqueue(OP_OFF, note, 0); }
void pm_all_notes_off(void) { enqueue(OP_ALLOFF, 0, 0); }
void pm_panic(void) { enqueue(OP_PANIC, 0, 0); }

void pm_set_sustain(uint8_t on) { enqueue(OP_PEDAL, on ? 1 : 0, 0); }

static int32_t pm_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16;
    int32_t ax = x < 0 ? -x : x;
    int32_t y = (x * (32768 - ax)) >> 13;
    int32_t ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    if (y > 32767)
        y = 32767;
    if (y < -32767)
        y = -32767;
    return y;
}

static uint32_t pm_rand(pm_voice *v)
{
    uint32_t x = v->rng;
    if (!x)
        x = 0xA3C59AC3u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x;
    return x;
}

static int32_t pm_clip(int32_t x)
{
    if (x > 32767)
        return 32767;
    if (x < -32767)
        return -32767;
    return x;
}

static int32_t pm_soft(int32_t x)
{
    int32_t n = 0;
    if (x < 0) {
        n = 1;
        x = -x;
    }
    if (x > 24000) {
        int32_t over = x - 24000;
        if (over > 20000)
            over = 20000;
        x = 24000 + (over >> 1);
        if (x > 32767)
            x = 32767;
    }
    return n ? -x : x;
}

static int32_t pm_q15(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 15);
}

/* 1/bend around unison. x = bend-65536 (Q16); 1/(1+x) ≈ 1 - x + x^2 - x^3. */
static uint32_t pm_inv_q16(uint32_t bend)
{
    int32_t x = (int32_t)bend - 65536;
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 16);
    int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 16);
    int32_t inv = 65536 - x + x2 - x3;
    if (inv < 1)
        inv = 1;
    return (uint32_t)inv;
}

static uint32_t pm_bend_now(uint32_t *vib_ph, uint32_t *inv_out)
{
    uint32_t bend = g_bend;
    int32_t depth = (int32_t)g_mod * 8; /* 127 → about ±25 cents */
    int32_t delta;
    *vib_ph += 460000u; /* ~4.7 Hz */
    delta = pm_q15(pm_sine(*vib_ph), depth);
    if (delta < -4000)
        delta = -4000;
    if ((int32_t)bend + delta > 0)
        bend = (uint32_t)((int32_t)bend + delta);
    if (bend < 52000u)
        bend = 52000u;
    if (bend > 80000u)
        bend = 80000u;
    *inv_out = pm_inv_q16(bend);
    return bend;
}

static uint32_t pm_delay_of(uint32_t base_q16, uint32_t inv, uint32_t half)
{
    uint32_t d = (uint32_t)(((uint64_t)base_q16 * inv) >> 16);
    if (half)
        d >>= 1;
    if (d < (4u << 16))
        d = 4u << 16;
    if (d > (PM_MAX_DEL << 16))
        d = PM_MAX_DEL << 16;
    return d;
}

static int32_t pm_read(const pm_voice *v, uint32_t del_q16)
{
    uint32_t di = del_q16 >> 16;
    uint32_t frac = del_q16 & 0xFFFFu;
    uint32_t i0, i1;
    int32_t s0, s1;
    if (di < 1u)
        di = 1u;
    if (di > PM_MAX_DEL)
        di = PM_MAX_DEL;
    i0 = (v->w - di) & PM_MASK;
    i1 = (i0 - 1u) & PM_MASK;
    s0 = v->buf[i0];
    s1 = v->buf[i1];
    return s0 + (int32_t)(((int64_t)(s1 - s0) * (int32_t)frac) >> 16);
}

static void pm_write(pm_voice *v, int32_t y)
{
    v->buf[v->w & PM_MASK] = (int16_t)pm_clip(y);
    v->w++;
}

static int32_t loop_gain(uint8_t decay, int released, int extra)
{
    int32_t k = 127 - (int32_t)decay;
    int32_t loss = 1 + (k * k) / 4 + extra;
    if (released)
        loss += 10 + k / 4;
    if (loss > 6000)
        loss = 6000;
    return 32767 - loss;
}

static void clear_voice(pm_voice *v)
{
    uint32_t i;
    for (i = 0; i < PM_DLEN; i++)
        v->buf[i] = 0;
    v->lp = v->prev = v->dc = 0;
    v->breath = 0;
    v->alive = 0;
    v->held = 0;
    v->sus = 0;
    v->click = 0;
    for (i = 0; i < PM_MODES; i++) {
        v->amp[i] = 0;
        v->ph[i] = 0;
    }
}

static void start_pluck(pm_voice *v, uint32_t del_q16)
{
    uint32_t len = del_q16 >> 16;
    uint32_t pick, i, base;
    int32_t amp = ((int32_t)v->vel * 22000) >> 7;
    int32_t depth = (int32_t)v->color * 258;
    if (depth > 32767)
        depth = 32767;
    if (len < 4u)
        len = 4u;
    if (len > PM_MAX_DEL)
        len = PM_MAX_DEL;
    for (i = 0; i < PM_DLEN; i++)
        v->buf[i] = 0;
    base = (0u - len) & PM_MASK;
    for (i = 0; i < len; i++) {
        int32_t n = (int32_t)(pm_rand(v) >> 16) - 32768;
        /* a little extra impulse at the pluck point, scaled by NOISE */
        v->buf[(base + i) & PM_MASK] = (int16_t)pm_q15(n, amp);
    }
    pick = 1u + (uint32_t)v->pos * (len - 2u) / 127u;
    if (pick < 1u)
        pick = 1u;
    if (pick >= len)
        pick = len - 1u;
    for (i = pick; i < len; i++) {
        uint32_t ia = (base + i) & PM_MASK;
        uint32_t ib = (base + i - pick) & PM_MASK;
        int32_t a = v->buf[ia];
        int32_t b = v->buf[ib];
        int32_t c = a - pm_q15(b, depth);
        v->buf[ia] = (int16_t)(c >> 1);
    }
    /* noise amount: a single stronger sample so low NOISE is still a click */
    {
        int32_t kick = ((int32_t)v->vel * (20 + (int32_t)v->excite)) << 2;
        uint32_t ik = (base + pick) & PM_MASK;
        int32_t s = v->buf[ik] + (kick >> 7);
        v->buf[ik] = (int16_t)pm_clip(s);
    }
    v->w = 0;
    v->lp = 0;
    v->prev = 0;
    v->dc = 0;
}

static int32_t sin_pi(uint32_t f_q16)
{
    /* sin(pi * f) = sin(2pi * f/2); phase wraps, so a wide f is fine. */
    uint32_t phase = (uint32_t)((uint64_t)f_q16 << 15);
    int32_t s = pm_sine(phase);
    return s < 0 ? -s : s;
}

static void start_modal(pm_voice *v)
{
    uint32_t m;
    uint32_t which = 0;
    uint32_t p = 16u + (uint32_t)v->pos * 48u / 127u; /* 0.125 .. 0.5, keeps the fundamental */
    int32_t bright = (int32_t)g_param[PM_BRIGHT];
    if (v->model == PM_BELL)
        which = 1;
    else if (v->model == PM_MEMB)
        which = 2;
    for (m = 0; m < PM_MODES; m++) {
        uint32_t f = ((m + 1u) * p << 16) / 128u;
        int32_t strike = sin_pi(f);
        int32_t g = MGAIN[which][m];
        int32_t keep = 256;
        if (m) {
            keep = (bright * 320) / (int32_t)(40 + m * (140 - bright));
            if (keep > 384)
                keep = 384;
            if (keep < 16)
                keep = 16;
        }
        g = (g * keep) >> 8;
        g = pm_q15(g, strike);
        g = (g * (int32_t)v->vel) >> 7;
        v->ratio[m] = (int16_t)RATIO[which][m];
        v->gain_m[m] = g;
        v->amp[m] = g * 256; /* high-res so a 1-unit step is not the whole envelope */
        v->ph[m] = 0x20000000u * (m + 1u);
        v->mode_inc[m] = 0;
    }
    v->click = (uint8_t)(8u + (uint32_t)v->excite / 4u);
    v->dc = 0;
    v->lp = 0;
}

static int32_t render_pluck(pm_voice *v, uint32_t del_q16, int32_t lg, int32_t bright_q15)
{
    /* The one-zero filter adds up to half a sample when TONE is dark.
     * Take that out of the read so the pitch stays put. */
    uint32_t gd = (uint32_t)(32767 - bright_q15);
    int32_t s, avg, mixed, y;
    if (del_q16 > gd + (4u << 16))
        del_q16 -= gd;
    s = pm_read(v, del_q16);
    avg = (s + v->prev) >> 1;
    v->prev = s;
    mixed = avg + pm_q15(s - avg, bright_q15);
    y = pm_q15(mixed, lg);
    pm_write(v, y);
    return y;
}

static int32_t render_modal(pm_voice *v, uint32_t bend, int released, uint8_t decay, uint8_t color)
{
    uint32_t m;
    int32_t y = 0;
    int32_t k = 127 - (int32_t)decay;
    /* High-res amplitude (<< 8). Q15 unity truncates, so a multiply by 32767
     * loses one output unit per sample and a bell dies in a tenth of a second.
     * Subtract a small step instead; decay 127 loses one high-res unit per sample. */
    int32_t base = 1 + (k * k) / 10;
    if (released)
        base += 16 + k;
    for (m = 0; m < PM_MODES; m++) {
        uint32_t stretch = 1024u + ((uint32_t)color * m * m) / 6u;
        uint32_t inc;
        int32_t step, s, a;
        if (stretch > 2400u)
            stretch = 2400u;
        inc = (uint32_t)(((uint64_t)v->note_inc * (uint32_t)v->ratio[m]) >> 10);
        inc = (uint32_t)(((uint64_t)inc * stretch) >> 10);
        inc = (uint32_t)(((uint64_t)inc * bend) >> 16);
        if (inc > 0x70000000u || v->amp[m] <= 0) {
            v->amp[m] = 0;
            continue;
        }
        v->mode_inc[m] = inc;
        v->ph[m] += inc;
        s = pm_sine(v->ph[m]);
        a = v->amp[m] >> 8;
        y += pm_q15(s, a);
        step = base + (base * (int32_t)m) / 2;
        v->amp[m] -= step;
        if (v->amp[m] < 0)
            v->amp[m] = 0;
    }
    if (v->click) {
        int32_t n = (int32_t)(pm_rand(v) >> 16) - 32768;
        int32_t a = ((int32_t)v->vel * (int32_t)v->excite) >> 4;
        y += pm_q15(n, a);
        v->click--;
    }
    return y;
}

/* Bow and flute: a tuned delay (same loop filter as the pluck, so the pitch
 * stays on the note) fed with breath. Once per round trip a pulse marks the
 * bow hair or the jet; a little noise keeps it from freezing. The pulse is
 * what locks the tone to the delay — a memoryless friction curve was settling
 * on a DC point that the output high-pass then erased. */
static int32_t render_sustained(pm_voice *v, uint32_t del_q16, int32_t lg, int32_t bright_q15, int32_t breath, int flute)
{
    uint32_t gd = (uint32_t)(32767 - bright_q15);
    int32_t s, avg, mixed, y, drive, press;
    if (del_q16 > gd + (4u << 16))
        del_q16 -= gd;
    s = pm_read(v, del_q16);
    press = 24 + (int32_t)g_param[PM_COLOR] / 2;
    drive = 0;
    if (breath > 0) {
        drive = pm_q15((int32_t)(pm_rand(v) >> 16) - 32768, breath >> (flute ? 5 : 4));
        if (v->trip == 0) {
            uint32_t per = del_q16 >> 16;
            if (per < 4u)
                per = 4u;
            v->trip = (uint16_t)per;
            drive += (breath * press) >> (flute ? 8 : 7);
        }
    }
    if (v->trip)
        v->trip--;
    s += drive >> 2;
    s = pm_clip(s);
    avg = (s + v->prev) >> 1;
    v->prev = s;
    mixed = avg + pm_q15(s - avg, bright_q15);
    if (flute) {
        int32_t d2 = pm_q15(mixed, mixed);
        mixed = pm_q15(mixed, 32767 - (d2 >> 1));
    } else {
        mixed = pm_soft(mixed);
    }
    y = (int32_t)(((int64_t)mixed * lg) >> 15);
    y = pm_clip(y);
    pm_write(v, y);
    return y;
}

static int32_t render_wind(pm_voice *v, uint32_t del_q16, int32_t lg, int32_t bright_q15, int32_t breath)
{
    int32_t bore = pm_read(v, del_q16);
    int32_t open = 40 + (int32_t)(v->flute ? (v->color - 64) : v->color);
    int32_t delta, d, d2, jet, into, y;
    if (open < 8)
        open = 8;
    delta = pm_q15(breath, open * 200) - (v->flute ? pm_q15(bore, 22000) : bore);
    d = pm_clip(delta);
    d2 = pm_q15(d, d);
    if (v->flute) {
        jet = pm_q15(d, 32767 - d2); /* x (1 - x^2) */
        into = jet;
    } else {
        jet = d - pm_q15(d2, d); /* odd reed, saturates */
        into = -jet;
    }
    if (bright_q15 < 6000)
        bright_q15 = 6000;
    v->lp += pm_q15(into - v->lp, bright_q15);
    y = (int32_t)(((int64_t)v->lp * lg) >> 15);
    if (breath > 0)
        y += pm_q15((int32_t)(pm_rand(v) >> 16) - 32768, breath >> 6);
    y = pm_clip(y);
    pm_write(v, y);
    return bore * 4; /* a signed left shift is undefined; the bore sample can be negative */
}

static int32_t dc_block(pm_voice *v, int32_t x)
{
    v->dc += (x - v->dc) >> 9;
    return x - v->dc;
}

static void note_on(uint8_t note, uint8_t vel)
{
    uint32_t i, best = 0, score_b = 0xFFFFFFFFu;
    pm_voice *v;
    for (i = 0; i < PM_VOICES; i++) {
        uint32_t score;
        if (!voices[i].alive)
            score = 10u + i;
        else if (voices[i].note == note && voices[i].held)
            score = 0;
        else
            score = 0x80000000u + voices[i].age;
        if (score < score_b) {
            score_b = score;
            best = i;
        }
    }
    v = &voices[best];
    v->model = g_model;
    v->note = note;
    v->vel = vel;
    v->held = 1;
    v->sus = 0;
    v->alive = 1;
    v->pos = g_param[PM_POS];
    v->color = g_param[PM_COLOR];
    v->excite = g_param[PM_EXCITE];
    v->flute = (uint8_t)(v->model == PM_REED && v->color >= 64);
    v->base_del = PM_DEL_Q16[note];
    v->note_inc = PM_INC[note];
    v->age = ++voice_age;
    v->fade = 0;
    v->breath = 0;
    v->tail = 0;
    v->trip = 0;
    v->bow_ph = 0;
    v->lp = 0;
    v->prev = 0;
    if (v->model == PM_PLUCK)
        start_pluck(v, v->base_del);
    else if (v->model == PM_BAR || v->model == PM_BELL || v->model == PM_MEMB)
        start_modal(v);
    else {
        uint32_t k;
        for (k = 0; k < PM_DLEN; k++)
            v->buf[k] = 0;
        v->dc = 0;
    }
    g_last = note;
}

static void note_off(uint8_t note)
{
    uint32_t i;
    for (i = 0; i < PM_VOICES; i++) {
        if (voices[i].alive && voices[i].note == note && voices[i].held) {
            voices[i].held = 0;
            voices[i].sus = g_sus ? 1 : 0;
        }
    }
}

static void pedal(uint8_t on)
{
    uint32_t i;
    g_sus = on ? 1 : 0;
    if (!on) {
        for (i = 0; i < PM_VOICES; i++)
            voices[i].sus = 0;
    }
}

static void panic(void)
{
    uint32_t i;
    for (i = 0; i < PM_VOICES; i++)
        clear_voice(&voices[i]);
    g_sounding = 0;
}

static void apply_cmds(void)
{
    while (cmd_r != cmd_w) {
        uint32_t i = cmd_r & 15u;
        uint8_t op = cmdq[i].op, note = cmdq[i].note, vel = cmdq[i].vel;
        cmd_r++;
        if (op == OP_ON)
            note_on(note, vel);
        else if (op == OP_OFF)
            note_off(note);
        else if (op == OP_ALLOFF) {
            uint32_t v;
            for (v = 0; v < PM_VOICES; v++) {
                if (voices[v].held) {
                    voices[v].held = 0;
                    voices[v].sus = g_sus ? 1 : 0;
                }
            }
        } else if (op == OP_PANIC)
            panic();
        else if (op == OP_PEDAL)
            pedal(note);
    }
}

void pm_render(int32_t *out, uint32_t n)
{
    uint32_t i, v;
    uint8_t decay, bright, level;
    int32_t expr;
    uint32_t vib = 0;
    if (!g_ready || !n || !out)
        return;
    apply_cmds();
    decay = g_param[PM_DECAY];
    bright = g_param[PM_BRIGHT];
    level = g_param[PM_LEVEL];
    expr = (int32_t)g_expr;
    for (i = 0; i < n; i++) {
        uint32_t inv = 65536u, bend;
        int32_t acc_l = 0, acc_r = 0;
        int32_t bright_q15 = (int32_t)bright * 258;
        if (bright_q15 > 32767)
            bright_q15 = 32767;
        bend = pm_bend_now(&vib, &inv);
        for (v = 0; v < PM_VOICES; v++) {
            pm_voice *voice = &voices[v];
            int released, sounding;
            int32_t y, lg, pan, breath_to, step;
            uint32_t del;
            if (!voice->alive)
                continue;
            released = !(voice->held || voice->sus);
            sounding = voice->held || voice->sus;
            del = pm_delay_of(voice->base_del, inv, voice->model == PM_REED && !voice->flute);
            if (voice->model == PM_PLUCK) {
                lg = loop_gain(decay, released, 0);
                y = render_pluck(voice, del, lg, bright_q15);
            } else if (voice->model == PM_BOW || voice->model == PM_REED) {
                int32_t k = 127 - (int32_t)decay;
                int32_t loss = 3 + k / 4;
                if (released)
                    loss += 40 + k;
                if (loss > 800)
                    loss = 800;
                /* 32768 is unity. 32767 truncates and drains the loop. */
                lg = 32768 - loss;
                breath_to = sounding ? ((int32_t)voice->vel * 258) : 0;
                if (breath_to > 32767)
                    breath_to = 32767;
                step = voice->model == PM_BOW ? 48 : 180;
                if (!sounding)
                    step *= 2;
                if (voice->breath < breath_to) {
                    voice->breath += step;
                    if (voice->breath > breath_to)
                        voice->breath = breath_to;
                } else if (voice->breath > breath_to) {
                    voice->breath -= step;
                    if (voice->breath < breath_to)
                        voice->breath = breath_to;
                }
                if (voice->model == PM_BOW || voice->flute)
                    y = render_sustained(voice, del, lg, bright_q15, voice->breath, voice->flute);
                else
                    y = render_wind(voice, del, lg, bright_q15, voice->breath);
            } else {
                y = render_modal(voice, bend, released, decay, g_param[PM_COLOR]);
            }
            y = dc_block(voice, y);
            if (voice->fade < 32767) {
                int32_t df = (voice->model == PM_BOW || voice->model == PM_REED) ? 512 : 4096;
                voice->fade += df;
                if (voice->fade > 32767)
                    voice->fade = 32767;
            }
            y = pm_q15(y, voice->fade);
            y = (y * (int32_t)level) >> 7;
            y = (y * expr) >> 7;
            pan = 28 + (int32_t)voice->note; /* 28..155, clamped */
            if (pan < 24)
                pan = 24;
            if (pan > 104)
                pan = 104;
            acc_l += (y * (128 - pan)) >> 7;
            acc_r += (y * pan) >> 7;
            {
                int32_t e = y < 0 ? -y : y;
                uint32_t m;
                int modal = voice->model == PM_BAR || voice->model == PM_BELL || voice->model == PM_MEMB;
                voice->tail += (e - voice->tail) >> 8;
                if (voice->tail < 0)
                    voice->tail = 0;
                if (modal && voice->click == 0) {
                    int dead = 1;
                    for (m = 0; m < PM_MODES; m++)
                        if (voice->amp[m] > 256)
                            dead = 0;
                    if (dead)
                        voice->alive = 0;
                } else if (released && voice->breath == 0 && voice->fade == 32767 && voice->tail < 16)
                    voice->alive = 0;
            }
        }
        out[2u * i] += pm_soft(acc_l);
        out[2u * i + 1u] += pm_soft(acc_r);
    }
    {
        uint8_t any = 0;
        for (v = 0; v < PM_VOICES; v++)
            if (voices[v].alive)
                any = 1;
        g_sounding = any;
    }
}
