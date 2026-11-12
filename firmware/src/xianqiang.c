/* SPDX-License-Identifier: GPL-3.0-only */
/* 道哥合成器 / DaoGe Synth — physical-modeling firmware for the M-VAVE FM-1.
 *
 * The panel, USB, MIDI, screen and update path are Dinghy (GPL-3.0-only),
 * itself the Felucca / SLOOP platform. Upstream Dinghy commit ea1b6bf.
 * The sound is dsp/pm.c (MIT): Karplus-Strong, modal bar / bell / membrane,
 * a sustained bowed string and a clarinet / flute waveguide.
 *
 * Integers only. app_audio() only calls pm_render(). The screen and the
 * encoders run in the main loop.
 *
 * Button dispatch follows JIANT (https://github.com/juanjiant-bit/JIANT,
 * GPL-3.0-only, a Felucca fork): the label table is PANEL_DEFAULT from
 * JIANT firmware/src/panel.c, and ui_input() samples fm1_in.buttons on
 * every pass the way this firmware already samples fm1_in.notes.
 * Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments.
 * Portions Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant). */
#include "daoge_title.h"
#define APP_NAME "XIANQ"
#define STORE_MAGIC 0x474E5158u             /* "XQNG" — not Dinghy's DNGY */
#define CV_MAX (240u * 120u)
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "pm.c"
#include "pm_user.c"
#include "xq_play.c"
#if BOARD_SMK37
#include "smk_wheel.h"
#endif

static int16_t scope_v[72];
static uint8_t scope_i;

static void app_audio(int32_t *out, uint32_t n)
{
    pm_render(out, n);
    if (n) {
        int32_t s = out[0];
        if (s > 24000)
            s = 24000;
        if (s < -24000)
            s = -24000;
        scope_v[scope_i++ % 72u] = (int16_t)s;
    }
}

/* Serum 2's panel, drawn here: dark slate, a lighter header, cyan arcs.
 * The font blends toward black, so the wells stay near-black. */
#define C_BG RGB(22, 28, 38)
#define C_WELL RGB(14, 18, 26)
#define C_HEAD RGB(46, 56, 70)
#define C_RULE RGB(64, 76, 92)
#define C_CYAN RGB(88, 206, 220)
#define C_GREEN RGB(146, 190, 78)
#define C_ORANGE RGB(232, 164, 64)
#define C_TEXT RGB(232, 236, 240)
#define C_MUTE RGB(132, 144, 158)
#define C_KNOB RGB(10, 14, 20)
#define C_ACC C_CYAN
#define C_INK C_MUTE
#define C_WARM C_TEXT
#define KEY0 53u

static int black_key(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void note_name(uint8_t note, char *s)
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *p;
    uint32_t i = 0;
    if (note > 127) {
        s[0] = '-';
        s[1] = 0;
        return;
    }
    p = NAME[note % 12u];
    while (*p)
        s[i++] = *p++;
    if (note >= 12u)
        s[i++] = (char)('0' + note / 12u - 1u);
    s[i] = 0;
}

static void num3(char *s, unsigned v)
{
    s[0] = (char)(v >= 100u ? '0' + v / 100u : ' ');
    s[1] = (char)(v >= 10u ? '0' + (v / 10u) % 10u : ' ');
    s[2] = (char)('0' + v % 10u);
    s[3] = 0;
}

static uint8_t ui_page, seq_edit, mod_depth, bend_range = 2;
static uint32_t catch_until;
static struct {
    int8_t oct;
    uint8_t ch, preset, latch;
    uint8_t p[PM_NPARAM];
} set;

/* Encoders are relative. After a page change, drop any steps already queued
 * and ignore a large burst for a moment so the knob you were holding does not
 * jump the new parameter. */
static int32_t take_enc(uint32_t id)
{
    int32_t e = fm1_enc_take(id);
    if (!e)
        return 0;
    if ((int32_t)(catch_until - fm1_ms) > 0 && (e > 2 || e < -2))
        return 0;
    return e;
}

static void add8(uint8_t *v, int32_t e, int32_t max)
{
    int32_t x = (int32_t)*v + ((e > 1 || e < -1) ? e * 2 : e);
    if (x < 0)
        x = 0;
    if (x > max)
        x = max;
    *v = (uint8_t)x;
}

static xq_play_t play;

static const int8_t ARC_X[16] = {-10, -12, -13, -13, -11, -9, -6, -2, 2, 6, 9, 11, 13, 13, 12, 10};
static const int8_t ARC_Y[16] = {8, 5, 1, -3, -6, -9, -12, -13, -13, -12, -9, -6, -3, 1, 5, 8};
/* JIANT firmware/src/panel.c PANEL_DEFAULT. Matrix ids are measured (PLAY 12,
 * REC 13, OCT- 0, OCT+ 1). The printed name is looked up from that table. */
enum { LB_FX, LB_SEL, LB_ENV, LB_LFO, LB_EDIT, LB_GLO, LB_HOME, LB_SAVE, LB_ARP, LB_SEQ, LB_PLAY, LB_REC,
       LB_OCTDN, LB_OCTUP, LB_N };
static const uint8_t PANEL_ID[LB_N] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
static const char *const PANEL_NAME[LB_N] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP",
                                              "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
#define PG_OSC 0u
#define PG_FX 1u
#define PG_ENV 2u
#define PG_LFO 3u
#define PG_ARP 4u
#define PG_SEQ 5u
#define PG_N 6u
static const char *const PAGE_NAME[PG_N] = {"OSC", "FX", "ENV", "LFO", "ARP", "SEQ"};

static int label_of(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < LB_N; i++)
        if (PANEL_ID[i] == id)
            return (int)i;
    return -1;
}

static char banner[20];
static uint32_t banner_until;
static uint8_t screen_force;
static uint8_t test_mode, env_down, shift_used;
static uint32_t env_t, both_oct_t;
static uint8_t last_id = 255;
static int8_t last_enc = -1, last_enc_n;

static void toast(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 18u) {
        banner[i] = s[i];
        i++;
    }
    banner[i] = 0;
    banner_until = fm1_ms + 1200u;
    screen_force = 1;
}

static int shift_held(void)
{
    return env_down && (int32_t)(fm1_ms - env_t) > 280;
}

static int lowbit(uint32_t bits)
{
    int i;
    for (i = 0; i < 14; i++)
        if (bits & (1u << i))
            return i;
    return -1;
}

static void go_page(uint8_t page)
{
    uint32_t ei;
    ui_page = (uint8_t)(page % PG_N);
    for (ei = 0; ei < 7u; ei++)
        (void)fm1_enc_take(ei);
    catch_until = fm1_ms + 250u;
    screen_force = 1;
}

static void toast_id(uint32_t id, const char *extra)
{
    char b[20];
    int lb = label_of(id);
    uint32_t i = 0;
    const char *p = lb >= 0 ? PANEL_NAME[lb] : "?";
    b[i++] = (char)('0' + (id / 10u) % 10u);
    b[i++] = (char)('0' + id % 10u);
    b[i++] = ' ';
    while (*p && i < 12u)
        b[i++] = *p++;
    if (extra && *extra) {
        b[i++] = ' ';
        while (*extra && i < 18u)
            b[i++] = *extra++;
    }
    b[i] = 0;
    toast(b);
}

static const char *fx_tag(void)
{
    uint8_t k = pm_fx_kind();
    return k == PM_FX_REV ? "REV" : k == PM_FX_DLY ? "DLY" : k == PM_FX_CHO ? "CHO" : "OFF";
}

static void knob_cell(int32_t x, int32_t y, const char *name, unsigned v, unsigned max, uint16_t acc)
{
    char num[4];
    int lit = max ? (int)(v * 12u / max) : 0;
    int i;
    if (lit > 12)
        lit = 12;
    cv_rect(x + 10, y + 2, 12, 12, C_KNOB);
    cv_rect(x + 13, y + 5, 6, 6, C_WELL);
    for (i = 0; i < 12; i++) {
        int32_t px = x + 16 + ARC_X[i + 2] * 3 / 4;
        int32_t py = y + 8 + ARC_Y[i + 2] * 3 / 4;
        cv_pset(px, py, i < lit ? acc : C_RULE);
    }
    cv_text(x, y + 16, &FONT_S, name, C_TEXT);
    num3(num, v);
    cv_text(x + 46, y + 16, &FONT_S, num, acc);
}

static void draw_graph(void)
{
    uint32_t i;
    cv_rect(0, 0, 240, 30, C_WELL);
    for (i = 0; i < 5u; i++)
        cv_rect(8, 4 + (int32_t)i * 5, 224, 1, C_RULE);
    if (ui_page == PG_ENV) {
        int32_t a = 8 + (int32_t)pm_get_param(PM_EXCITE) / 4;
        int32_t d = a + 16 + (int32_t)pm_get_param(PM_DECAY) / 2;
        if (d > 220)
            d = 220;
        cv_line(8, 24, a, 6, C_CYAN);
        cv_line(a, 6, d, set.latch ? 14 : 24, C_CYAN);
        if (set.latch)
            cv_line(d, 14, 228, 14, C_CYAN);
    } else if (ui_page == PG_LFO) {
        int32_t x = 8, y = 24, dir = -1, step = 4 + (int32_t)(12u - (bend_range > 12u ? 12u : bend_range));
        for (i = 0; i < 8u; i++) {
            int32_t y1 = dir < 0 ? 6 : 24;
            cv_line(x, y, x + 26, y1, C_CYAN);
            x += 26;
            y = y1;
            dir = -dir;
            (void)step;
        }
    } else if (ui_page == PG_OSC || ui_page == PG_FX) {
        uint16_t col = ui_page == PG_OSC ? C_GREEN : C_CYAN;
        int32_t prev = 15;
        for (i = 0; i < 56u; i++) {
            int32_t env = 28 - (int32_t)i / 3;
            int32_t s = (i & 2u) ? env : -env;
            int32_t y = 15 - s / 3;
            if (y < 4)
                y = 4;
            if (y > 26)
                y = 26;
            cv_line(8 + (int32_t)i * 4, prev, 12 + (int32_t)i * 4, y, col);
            prev = y;
        }
    } else {
        uint32_t n = ui_page == PG_ARP ? 8u : (uint32_t)play.seq_len;
        if (n > 16u)
            n = 16u;
        for (i = 0; i < n; i++) {
            int32_t h = ui_page == PG_ARP ? 6 + (int32_t)((i * 3u) % 12u) : (int32_t)play.seq_vel[i] / 10;
            if (h > 18)
                h = 18;
            cv_rect(10 + (int32_t)i * 14, 24 - h, 8, h, i == seq_edit && ui_page == PG_SEQ ? C_ORANGE : C_CYAN);
        }
    }
}

static void draw_params(void)
{
    static const char *const FXN[6] = {"TYPE", "MIX", "TIME", "FB", "----", "LEVEL"};
    static const char *const ENVN[6] = {"DECAY", "EXCITE", "TONE", "SUS", "COLOR", "LEVEL"};
    static const char *const LFON[6] = {"DEPTH", "BEND", "----", "----", "----", "LEVEL"};
    static const char *const ARPN[6] = {"MODE", "RATE", "GATE", "OCT", "LATCH", "LEVEL"};
    static const char *const SEQN[6] = {"STEP", "NOTE", "VEL", "LEN", "----", "LEVEL"};
    uint8_t knd, mx, tm, fbk;
    uint32_t i;
    pm_fx_get(&knd, &mx, &tm, &fbk);
    cv_begin(240, 96, C_BG);
    draw_graph();
    cv_rect(0, 31, 240, 1, C_RULE);
    for (i = 0; i < 6u; i++) {
        const char *name = "----";
        unsigned v = 0, max = 127;
        int32_t x = (int32_t)(i % 3u) * 78 + 4;
        int32_t y = 34 + (int32_t)(i / 3u) * 32;
        uint16_t acc = i == 5u ? C_ORANGE : C_CYAN;
        if (ui_page == PG_OSC) {
            name = pm_param_name(pm_model(), (uint8_t)i);
            v = pm_get_param((uint8_t)i);
        } else if (ui_page == PG_FX) {
            name = FXN[i];
            v = i == 0 ? knd : i == 1 ? mx : i == 2 ? tm : i == 3 ? fbk : i == 5 ? pm_get_param(PM_LEVEL) : 0;
            max = i == 0 ? 3 : 127;
        } else if (ui_page == PG_ENV) {
            name = ENVN[i];
            v = i == 0 ? pm_get_param(PM_DECAY) : i == 1 ? pm_get_param(PM_EXCITE) : i == 2 ? pm_get_param(PM_BRIGHT) : i == 3 ? set.latch : i == 4 ? pm_get_param(PM_COLOR) : pm_get_param(PM_LEVEL);
            max = i == 3 ? 1 : 127;
        } else if (ui_page == PG_LFO) {
            name = LFON[i];
            v = i == 0 ? mod_depth : i == 1 ? bend_range : i == 5 ? pm_get_param(PM_LEVEL) : 0;
            max = i == 1 ? 12 : 127;
        } else if (ui_page == PG_ARP) {
            name = ARPN[i];
            v = i == 0 ? play.mode : i == 1 ? play.rate : i == 2 ? play.gate : i == 3 ? play.octaves : i == 4 ? play.latch : pm_get_param(PM_LEVEL);
            max = i == 0 ? 4 : i == 1 ? 7 : i == 2 ? 8 : i == 3 ? 3 : i == 4 ? 1 : 127;
        } else {
            name = SEQN[i];
            v = i == 0 ? seq_edit : i == 1 ? play.seq_note[seq_edit] : i == 2 ? play.seq_vel[seq_edit] : i == 3 ? play.seq_len : i == 5 ? pm_get_param(PM_LEVEL) : 0;
            max = i == 0 ? 15 : i == 3 ? 16 : 127;
        }
        knob_cell(x, y, name, v, max, acc);
    }
    cv_blit(0, 52);
}

static void draw_keys(uint32_t lit)
{
    uint32_t k, w = 0;
    cv_begin(240, 28, C_BG);
    for (k = 0; k < 27u; k++)
        if (!black_key(KEY0 + k)) {
            cv_rect(6 + (int32_t)w * 14, 8, 12, 18, lit >> k & 1u ? C_ORANGE : C_TEXT);
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)
        if (black_key(KEY0 + k))
            cv_rect(6 + (int32_t)w * 14 - 4, 0, 8, 12, lit >> k & 1u ? C_CYAN : C_WELL);
        else
            w++;
    cv_blit(0, 186);
}

static void draw_header(const char *preset_line, const char *note)
{
    uint32_t i;
    cv_begin(240, 20, C_HEAD);
    cv_text(4, 2, &FONT_S, note, note[0] == '-' ? C_MUTE : C_GREEN);
    cv_text(40, 2, &FONT_S, preset_line, C_TEXT);
    cv_blit(0, 0);
    cv_begin(240, 16, C_HEAD);
    for (i = 0; i < PG_N; i++) {
        int32_t x = (int32_t)i * 40;
        if (i == ui_page) {
            cv_rect(x, 14, 36, 2, C_CYAN);
            cv_text(x + 4, 0, &FONT_S, PAGE_NAME[i], C_CYAN);
        } else
            cv_text(x + 4, 0, &FONT_S, PAGE_NAME[i], C_MUTE);
    }
    cv_blit(0, 20);
}

static void draw_scope(void)
{
    uint32_t i;
    cv_begin(240, 20, C_BG);
    cv_rect(4, 10, 232, 1, C_RULE);
    for (i = 0; i < 72u; i++) {
        int32_t s = scope_v[(scope_i + i) % 72u] / 1400;
        int32_t y0 = 10, y1 = 10 - s;
        if (y1 < 1)
            y1 = 1;
        if (y1 > 18)
            y1 = 18;
        cv_line(4 + (int32_t)i * 3, y0, 4 + (int32_t)i * 3, y1, C_GREEN);
    }
    cv_blit(0, 166);
}

static void draw_banner(void)
{
    cv_begin(240, 16, C_WELL);
    cv_rect(0, 15, 240, 1, C_RULE);
    cv_text(8, 0, &FONT_S, banner[0] ? banner : PAGE_NAME[ui_page], C_ORANGE);
    cv_blit(0, 36);
}

static void hex4(char *s, unsigned v)
{
    static const char H[] = "0123456789ABCDEF";
    s[0] = H[(v >> 12) & 15u];
    s[1] = H[(v >> 8) & 15u];
    s[2] = H[(v >> 4) & 15u];
    s[3] = H[v & 15u];
    s[4] = 0;
}

static void draw_test(void)
{
    char line[24], hx[8];
    uint32_t i;
    cv_begin(240, 80, C_WELL);
    cv_text(8, 4, &FONT_S, "BTN TEST", C_CYAN);
    if (last_id < 14u) {
        line[0] = (char)('0' + last_id / 10u);
        line[1] = (char)('0' + last_id % 10u);
        line[2] = ' ';
        i = 3;
        {
            int lb = label_of(last_id);
            const char *p = lb >= 0 ? PANEL_NAME[lb] : "?";
            while (*p && i < 16u)
                line[i++] = *p++;
        }
        line[i] = 0;
    } else if (last_enc >= 0) {
        line[0] = 'E';
        line[1] = (char)('0' + last_enc);
        line[2] = ' ';
        line[3] = last_enc_n < 0 ? '-' : '+';
        line[4] = (char)('0' + (last_enc_n < 0 ? -last_enc_n : last_enc_n) % 10);
        line[5] = 0;
    } else {
        line[0] = '-';
        line[1] = 0;
    }
    cv_text(8, 24, &FONT_S, line, C_ORANGE);
    hex4(hx, fm1_in.buttons);
    cv_text(8, 44, &FONT_S, hx, C_TEXT);
    cv_text(8, 62, &FONT_S, "HOME EXITS", C_MUTE);
    cv_blit(0, 36);
#ifndef DINGHY_HOST
    cv_begin(240, 36, C_BG);
    for (i = 0; i < 6u; i++) {
        hx[0] = (char)('0' + (fm1_in.raw[i] & 15u));
        hx[1] = 0;
        cv_text(8 + (int32_t)i * 28, 4, &FONT_S, hx, C_MUTE);
    }
    cv_blit(0, 124);
#else
    (void)i;
#endif
}

static void capture_set(void)
{
    uint32_t i;
    set.preset = pm_preset_index();
    for (i = 0; i < PM_NPARAM; i++)
        set.p[i] = pm_get_param((uint8_t)i);
}

static void apply_set(void)
{
    uint32_t i;
    pm_load_preset(set.preset);
    for (i = 0; i < PM_NPARAM; i++)
        pm_set_param((uint8_t)i, set.p[i]);
    pm_set_sustain(set.latch);
}

static uint32_t idle_ok, midi_src, clk_n, clk_t0, clk_bpm, clk_run, clk_beat_ms;
static uint8_t midi_sus;
static uint32_t ext_clk_ms, int_clk_ms, int_run;
static pm_user_bank_t ubank;
static int ubank_src = -1;
static uint8_t user_mode, user_slot, save_wait;

static void load_user_bank(void)
{
    uint8_t a[PM_USER_BYTES], b[PM_USER_BYTES];
    int na = blob_load(0, a, PM_USER_BYTES) == 0;
    int nb = blob_load(1, b, PM_USER_BYTES) == 0;
    if (pm_user_choose(na ? a : 0, na ? PM_USER_BYTES : 0, nb ? b : 0, nb ? PM_USER_BYTES : 0, &ubank, &ubank_src) != 0)
        pm_user_reset(&ubank);
}

/* Erases the spare blob sector (~50 ms, sound drops). Call only while quiet. */
static void save_user_bank(void)
{
    uint8_t buf[PM_USER_BYTES];
    int dest = ubank_src == 0 ? 1 : 0;
    uint32_t seq = ubank.seq;
    ubank.seq = seq + 1u;
    if (pm_user_encode(&ubank, buf) != 0 || blob_save((uint32_t)dest, buf, PM_USER_BYTES) != 0) {
        ubank.seq = seq;
        return;
    }
    ubank_src = dest;
}

static void publish_sustain(void)
{
    pm_set_sustain(set.latch || midi_sus);
}

static void nudge(uint8_t id, int32_t e)
{
    int32_t v, step;
    if (!e)
        return;
    step = (e > 1 || e < -1) ? e * 2 : e;
    v = (int32_t)pm_get_param(id) + step;
    if (v < 0)
        v = 0;
    if (v > 127)
        v = 127;
    pm_set_param(id, (uint8_t)v);
}

static void bend_from_midi(unsigned d1, unsigned d2)
{
    int32_t d = (int32_t)((d2 << 7) | d1) - 8192;
    int32_t range = bend_range ? bend_range : 2;
    int32_t coef = 3785 * range / 2;
    int32_t sq = (d * d) >> 12;
    int32_t b = 65536 + (d * coef >> 12) + ((sq * (109 * range / 2)) >> 12);
    if (b < 32768)
        b = 32768;
    if (b > 131072)
        b = 131072;
    pm_set_bend((uint32_t)b);
}

static void fx_cc(uint8_t which, uint8_t val)
{
    uint8_t k, m, t, f;
    pm_fx_get(&k, &m, &t, &f);
    if (which == 0)
        k = (uint8_t)(val > 3 ? val % 4u : val);
    else if (which == 1)
        m = val;
    else if (which == 2)
        t = val;
    else
        f = val;
    pm_fx_set(k, m, t, f);
}

static void midi_cc(unsigned cc, unsigned val)
{
    if (cc == 1)
        pm_set_mod((uint8_t)val);
    else if (cc == 7 || cc == 26)
        pm_set_param(PM_LEVEL, (uint8_t)val);
    else if (cc == 11)
        pm_set_expr((uint8_t)val);
    else if (cc == 64) {
        midi_sus = (uint8_t)(val >= 64u);
        publish_sustain();
    } else if (cc == 71 || cc == 24)
        pm_set_param(PM_COLOR, (uint8_t)val);
    else if (cc == 72 || cc == 21)
        pm_set_param(PM_DECAY, (uint8_t)val);
    else if (cc == 73 || cc == 25)
        pm_set_param(PM_EXCITE, (uint8_t)val);
    else if (cc == 74 || cc == 22)
        pm_set_param(PM_BRIGHT, (uint8_t)val);
    else if (cc == 23)
        pm_set_param(PM_POS, (uint8_t)val);
    else if (cc == 27)
        fx_cc(1, (uint8_t)val);
    else if (cc == 28)
        fx_cc(2, (uint8_t)val);
    else if (cc == 29)
        fx_cc(3, (uint8_t)val);
    else if (cc == 30)
        fx_cc(0, (uint8_t)val);
    else if (cc == 31)
        bend_range = (uint8_t)(1u + val * 11u / 127u);
    else if (cc == 102)
        play.mode = (uint8_t)(val * 4u / 127u);
    else if (cc == 103)
        play.rate = (uint8_t)(val * 7u / 127u);
    else if (cc == 104)
        play.gate = (uint8_t)(1u + val * 7u / 127u);
    else if (cc == 105)
        play.octaves = (uint8_t)(val * 3u / 127u);
    else if (cc == 106)
        play.latch = (uint8_t)(val >= 64u);
    else if (cc == 120)
        pm_panic();
    else if (cc == 123)
        pm_all_notes_off();
}

static void play_clocks(void)
{
    uint8_t note[2], vel[2];
    uint32_t ch = set.ch ? set.ch - 1u : 0u;
    int n = xq_play_clock(&play, note, vel), i;
    for (i = 0; i < n; i++) {
        if (vel[i]) {
            pm_note_on(note[i], vel[i]);
            midi_send(0x90u | ch, note[i], vel[i]);
        } else {
            pm_note_off(note[i]);
            midi_send(0x80u | ch, note[i], 0);
        }
    }
}

static void local_on(uint8_t note, uint8_t vel, uint32_t ch)
{
    if (play.arp_on)
        xq_play_note(&play, note, vel);
    else
        pm_note_on(note, vel);
    if (play.rec)
        xq_play_rec_note(&play, note, vel);
    midi_send(0x90u | ch, note, vel);
}

static void local_off(uint8_t note, uint32_t ch)
{
    if (!note)
        return;
    if (play.arp_on)
        xq_play_note(&play, note, 0);
    else
        pm_note_off(note);
    midi_send(0x80u | ch, note, 0);
}

static void midi_in(void)
{
    uint8_t st, d1, d2;
    int src;
    while ((src = midi_get(&st, &d1, &d2)) != 0) {
        uint32_t ty = st & 0xF0u;
        if (st == 0xF8u) {
            if (clk_n % 24u == 0u) {
                if (clk_n) {
                    uint32_t ms = fm1_ms - clk_t0;
                    clk_bpm = ms ? (60000u + ms / 2u) / ms : 0;
                }
                clk_t0 = fm1_ms;
                clk_beat_ms = fm1_ms;
            }
            clk_n++;
            ext_clk_ms = fm1_ms;
            if (play.arp_on || play.seq_on)
                play_clocks();
            continue;
        }
        if (st == 0xFAu) { clk_run = 1; clk_n = 0; continue; }
        if (st == 0xFBu) { clk_run = 1; continue; }
        if (st == 0xFCu) { clk_run = 0; continue; }
        if (set.ch && (st & 15u) != set.ch - 1u)
            continue;
        if (ty == 0x90u && d2) {
            if (play.arp_on)
                xq_play_note(&play, d1, d2);
            else
                pm_note_on(d1, d2);
            midi_src = (uint32_t)src;
        } else if (ty == 0x80u || ty == 0x90u) {
            if (play.arp_on)
                xq_play_note(&play, d1, 0);
            else
                pm_note_off(d1);
        }
        else if (ty == 0xE0u)
            bend_from_midi(d1, d2);
        else if (ty == 0xB0u)
            midi_cc(d1, d2);
        else if (ty == 0xD0u)
            pm_set_aftertouch(255, d1);
        else if (ty == 0xA0u)
            pm_set_aftertouch(d1, d2);
        else if (ty == 0xC0u) {
            uint8_t level = pm_get_param(PM_LEVEL);
            pm_load_preset((uint8_t)(d1 % PM_NPRESET));
            pm_set_param(PM_LEVEL, level);
            user_mode = 0;
        }
    }
}

static uint8_t ui_dirty;

static void enc_turn(uint32_t which, int32_t e)
{
    uint8_t k, m, t, f;
    if (!e)
        return;
    screen_force = 1;
    ui_dirty = 1;
    if (ui_page == PG_OSC) {
        if (which == 0) nudge(PM_DECAY, e);
        else if (which == 1) nudge(PM_BRIGHT, e);
        else if (which == 2) nudge(PM_POS, e);
        else if (which == 3) nudge(PM_COLOR, e);
        else if (which == 4) nudge(PM_EXCITE, e);
        else nudge(PM_LEVEL, e);
    } else if (ui_page == PG_FX) {
        if (which == 5) {
            nudge(PM_LEVEL, e);
            return;
        }
        pm_fx_get(&k, &m, &t, &f);
        if (which == 0) add8(&k, e, 3);
        else if (which == 1) add8(&m, e, 127);
        else if (which == 2) add8(&t, e, 127);
        else if (which == 3) add8(&f, e, 127);
        else return;
        pm_fx_set(k, m, t, f);
    } else if (ui_page == PG_ENV) {
        if (which == 0) nudge(PM_DECAY, e);
        else if (which == 1) nudge(PM_EXCITE, e);
        else if (which == 2) nudge(PM_BRIGHT, e);
        else if (which == 3) {
            set.latch = (uint8_t)!set.latch;
            publish_sustain();
        } else if (which == 4) nudge(PM_COLOR, e);
        else nudge(PM_LEVEL, e);
    } else if (ui_page == PG_LFO) {
        if (which == 0) {
            int32_t v = (int32_t)mod_depth + e;
            if (v < 0) v = 0;
            if (v > 127) v = 127;
            mod_depth = (uint8_t)v;
            pm_set_mod(mod_depth);
        } else if (which == 1) {
            int32_t v = (int32_t)bend_range + (e > 0 ? 1 : -1);
            if (v < 2) v = 2;
            if (v > 12) v = 12;
            bend_range = (uint8_t)v;
        } else if (which == 5)
            nudge(PM_LEVEL, e);
    } else if (ui_page == PG_ARP) {
        if (which == 0) add8(&play.mode, e, 4);
        else if (which == 1) add8(&play.rate, e, 7);
        else if (which == 2) add8(&play.gate, e, 8);
        else if (which == 3) add8(&play.octaves, e, 3);
        else if (which == 4) play.latch = (uint8_t)!play.latch;
        else nudge(PM_LEVEL, e);
    } else if (which == 0)
        add8(&seq_edit, e, 15);
    else if (which == 1)
        add8(&play.seq_note[seq_edit], e, 127);
    else if (which == 2)
        add8(&play.seq_vel[seq_edit], e, 127);
    else if (which == 3) {
        int32_t n = (int32_t)play.seq_len + (e > 0 ? 1 : -1);
        if (n < 1) n = 1;
        if (n > 16) n = 16;
        play.seq_len = (uint8_t)n;
    } else if (which == 5)
        nudge(PM_LEVEL, e);
}

static uint32_t btn_seen;

static void cycle_fx(void)
{
    uint8_t k, m, t, f;
    pm_fx_get(&k, &m, &t, &f);
    k = (uint8_t)((k + 1u) % PM_FX_N);
    if (!m)
        m = 64;
    if (!t)
        t = 72;
    if (!f)
        f = 48;
    pm_fx_set(k, m, t, f);
    ui_dirty = 1;
}

/* JIANT ui_input: the live button word is sampled every pass, the same way notes
 * are, and the one-shot edge word is OR'd in so a tap between passes is not lost.
 * Page buttons open their page immediately. */
static void act_button(uint32_t id)
{
    int lb = label_of(id);
    int shifted = shift_held();
    const pm_preset_t *presets = pm_presets();
    last_id = (uint8_t)id;
    screen_force = 1;
    if (test_mode) {
        if (lb == LB_HOME) {
            test_mode = 0;
            go_page(PG_OSC);
            toast_id(id, "EXIT");
        }
        return;
    }
    switch (lb) {
    case LB_OCTDN:
        if (shifted) {
            go_page((uint8_t)((ui_page + PG_N - 1u) % PG_N));
            shift_used = 1;
            toast_id(id, PAGE_NAME[ui_page]);
        } else if (set.oct > -2) {
            set.oct--;
            ui_dirty = 1;
            toast_id(id, 0);
        }
        break;
    case LB_OCTUP:
        if (shifted) {
            go_page((uint8_t)((ui_page + 1u) % PG_N));
            shift_used = 1;
            toast_id(id, PAGE_NAME[ui_page]);
        } else if (set.oct < 2) {
            set.oct++;
            ui_dirty = 1;
            toast_id(id, 0);
        }
        break;
    case LB_FX:
        cycle_fx();
        go_page(PG_FX);
        toast_id(id, fx_tag());
        break;
    case LB_SEL:
        if (shifted) {
            cycle_fx();
            go_page(PG_FX);
            shift_used = 1;
            toast_id(id, fx_tag());
        } else {
            set.ch = (uint8_t)(set.ch >= 16u ? 0u : set.ch + 1u);
            ui_dirty = 1;
            toast_id(id, set.ch ? "CH" : "OMNI");
        }
        break;
    case LB_ENV:
        go_page(PG_ENV);
        toast_id(id, 0);
        break;
    case LB_LFO:
        user_mode = (uint8_t)!user_mode;
        if (user_mode)
            pm_user_recall(&ubank, user_slot);
        go_page(PG_LFO);
        toast_id(id, user_mode ? "USER" : "PRESET");
        break;
    case LB_EDIT:
        go_page((uint8_t)((ui_page + 1u) % PG_N));
        toast_id(id, PAGE_NAME[ui_page]);
        break;
    case LB_GLO:
        bend_range = (uint8_t)(bend_range >= 12u ? 2u : bend_range + 5u);
        go_page(PG_LFO);
        toast_id(id, bend_range >= 12u ? "12" : bend_range >= 7u ? "7" : "2");
        break;
    case LB_HOME:
        if (shifted) {
            const char *nm = user_mode && ubank.slot[user_slot].used ? ubank.slot[user_slot].name : presets[pm_preset_index()].name;
            pm_user_store_current(&ubank, user_slot, nm);
            user_mode = 1;
            save_wait = 1;
            shift_used = 1;
            toast_id(id, "SAVE");
        } else {
            pm_panic();
            toast_id(id, "PANIC");
        }
        break;
    case LB_SAVE: {
        const char *nm = user_mode && ubank.slot[user_slot].used ? ubank.slot[user_slot].name : presets[pm_preset_index()].name;
        pm_user_store_current(&ubank, user_slot, nm);
        user_mode = 1;
        save_wait = 1;
        toast_id(id, "SAVE");
        break;
    }
    case LB_ARP:
        play.arp_on = (uint8_t)!play.arp_on;
        go_page(PG_ARP);
        toast_id(id, play.arp_on ? "ON" : "OFF");
        break;
    case LB_SEQ:
        play.seq_on = (uint8_t)!play.seq_on;
        go_page(PG_SEQ);
        toast_id(id, play.seq_on ? "ON" : "OFF");
        break;
    case LB_REC:
        play.rec = (uint8_t)!play.rec;
        toast_id(id, play.rec ? "ON" : "OFF");
        break;
    case LB_PLAY:
        int_run = int_run ? 0 : 1;
        int_clk_ms = fm1_ms;
        midi_send(int_run ? 0xFAu : 0xFCu, 0, 0);
        if (!int_run && play.sounding) {
            pm_note_off(play.cur);
            play.sounding = 0;
        }
        toast_id(id, int_run ? "PLAY" : "STOP");
        break;
    default:
        toast_id(id, 0);
        break;
    }
}

static void ui_input(void)
{
    uint32_t rel = 0, now, down, up, id;
    uint32_t both = (1u << PANEL_ID[LB_OCTDN]) | (1u << PANEL_ID[LB_OCTUP]);
    uint32_t edge = fm1_input_edges(&rel);
    now = fm1_in.buttons;
    down = (now & ~btn_seen) | (edge & 0x3FFFu);
    up = (btn_seen & ~now) | (rel & 0x3FFFu);
    btn_seen = now;
    if ((now & both) == both) {
        if (!both_oct_t)
            both_oct_t = fm1_ms ? fm1_ms : 1u;
        if (!test_mode && fm1_ms - both_oct_t > 450u) {
            test_mode = 1;
            toast("BTN TEST");
        }
    } else
        both_oct_t = 0;
    if (down & (1u << PANEL_ID[LB_ENV])) {
        env_down = 1;
        env_t = fm1_ms;
        shift_used = 0;
    }
    if (up & (1u << PANEL_ID[LB_ENV])) {
        if (!test_mode && !shift_used && (uint32_t)(fm1_ms - env_t) < 320u) {
            set.latch = (uint8_t)!set.latch;
            publish_sustain();
            go_page(PG_ENV);
            toast_id(PANEL_ID[LB_ENV], set.latch ? "SUS" : "OFF");
            ui_dirty = 1;
        }
        env_down = 0;
    }
    for (id = 0; id < 14u; id++)
        if (down & (1u << id))
            act_button(id);
}

static void app_idle(void)
{
    if (idle_ok) {
        midi_in();
        ui_input();
    }
}

/* The panel font is Latin, so the splash is a 1-bit 道哥合成器. */
static void draw_title(void)
{
    uint32_t y, x;
    cv_begin(DAOGE_TITLE_W, DAOGE_TITLE_H, C_BLACK);
    for (y = 0; y < DAOGE_TITLE_H; y++) {
        for (x = 0; x < DAOGE_TITLE_W; x++) {
            if (DAOGE_TITLE[y][x >> 3] & (uint8_t)(0x80u >> (x & 7u)))
                cv_pset((int32_t)x, (int32_t)y, C_ACC);
        }
    }
    cv_blit(0, 4);
    lcd_sync();
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0;
    uint32_t lit_drawn = ~0u, note_drawn = ~0u, parm_drawn = ~0u, status_drawn = ~0u;
    uint8_t held[27] = {0};
    const pm_preset_t *presets = pm_presets();
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    uint8_t pad_note[16] = {0}, ext_note[FM1_NKEY] = {0};
    smk_pw_t wheel = {0};
    smk_pad_events = 1;
#endif
    pm_init();
    xq_play_init(&play);
    load_user_bank();
    if (settings_load(&set, sizeof set) || set.oct < -2 || set.oct > 2 || set.ch > 16u || set.preset >= PM_NPRESET) {
        set.oct = 0;
        set.ch = 0;
        set.latch = 0;
        set.preset = 0;
        capture_set();
    } else
        apply_set();

    uint32_t preset_drawn = ~0u;
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_title();

    for (;;) {
        uint32_t keys, down, up, lit = 0, out_ch, st, parm;
        int32_t oct, e;
        uint32_t base;
        char line[24], note[8];
        const pm_preset_t *pr;
        platform_poll();
        oct = set.oct;
        base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        out_ch = set.ch ? set.ch - 1u : 0u;

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
#if BOARD_SMK37
            uint32_t vel = fm1_in.key_vel[SMK_KEY_F3 + k];
            if (!vel)
                vel = 100;
#else
            uint32_t vel = 108;
#endif
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                local_on(held[k], (uint8_t)vel, out_ch);
            }
            if (up >> k & 1u)
                local_off(held[k], out_ch);
        }
#if BOARD_SMK37
        while ((e = smk_extkey_take()) >= 0) {
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
            if (key >= FM1_NKEY)
                continue;
            if (vel) {
                ext_note[key] = (uint8_t)(base + key - SMK_KEY_F3);
                local_on(ext_note[key], (uint8_t)vel, out_ch);
            } else if (ext_note[key]) {
                local_off(ext_note[key], out_ch);
                ext_note[key] = 0;
            }
        }
        while ((e = smk_pad_take()) >= 0) {
            uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0x7Fu;
            uint32_t w = pad < 8u ? pad + 8u : pad - 8u;
            if (vel) {
                pad_note[pad] = (uint8_t)(base + WHITE[w]);
                local_on(pad_note[pad], (uint8_t)vel, out_ch);
            } else if (pad_note[pad]) {
                local_off(pad_note[pad], out_ch);
                pad_note[pad] = 0;
            }
        }
        {
            int32_t bd = smk_pw_bend(&wheel, fm1_in.wheel[0], fm1_ms);
            int32_t bend = 65536 + (bd * 3785 >> 12) + (((bd * bd) >> 12) * 109 >> 12);
            pm_set_bend((uint32_t)bend);
        }
#endif
        ui_input();
        if (ui_dirty) {
            dirty = 1;
            t_change = fm1_ms;
            ui_dirty = 0;
        }


        if (test_mode) {
            uint32_t ei;
            for (ei = 0; ei < 7u; ei++) {
                int32_t es = fm1_enc_take(ei);
                if (es) {
                    last_enc = (int8_t)ei;
                    last_enc_n = (int8_t)(es > 9 ? 9 : es < -9 ? -9 : es);
                }
            }
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            if (user_mode) {
                int32_t s = (int32_t)user_slot + (e > 0 ? 1 : -1);
                while (s < 0)
                    s += (int32_t)PM_USER_N;
                while (s >= (int32_t)PM_USER_N)
                    s -= (int32_t)PM_USER_N;
                user_slot = (uint8_t)s;
                pm_user_recall(&ubank, user_slot);
            } else {
                int32_t p = (int32_t)pm_preset_index() + e;
                uint8_t level = pm_get_param(PM_LEVEL);
                while (p < 0)
                    p += (int32_t)PM_NPRESET;
                while (p >= (int32_t)PM_NPRESET)
                    p -= (int32_t)PM_NPRESET;
                pm_load_preset((uint8_t)p);
                pm_set_param(PM_LEVEL, level);
            }
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_SELECT)) != 0)
            enc_turn(0, e);
        if ((e = take_enc(ENC_ALGORITHM)) != 0)
            enc_turn(1, e);
        if ((e = take_enc(ENC_KNOB1)) != 0)
            enc_turn(2, e);
        if ((e = take_enc(ENC_KNOB2)) != 0)
            enc_turn(3, e);
        if ((e = take_enc(ENC_KNOB3)) != 0)
            enc_turn(4, e);
        if ((e = take_enc(ENC_KNOB4)) != 0)
            enc_turn(5, e);
        if (ui_dirty) {
            dirty = 1;
            t_change = fm1_ms;
            ui_dirty = 0;
        }

        midi_in();
        if (clk_bpm && fm1_ms - clk_t0 > 2000u)
            clk_bpm = 0, clk_run = 0;
        pm_fx_set_bpm((uint16_t)clk_bpm);
        if (int_run && fm1_ms - ext_clk_ms > 80u) {
            uint32_t step = 60000u / 24u / 120u;
            if (!int_clk_ms)
                int_clk_ms = fm1_ms;
            while (fm1_ms - int_clk_ms >= step) {
                int_clk_ms += step;
                midi_send(0xF8u, 0, 0);
                if (play.arp_on || play.seq_on)
                    play_clocks();
            }
        }

        oct = set.oct;
        base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        for (k = 0; k < 27u; k++)
            if (held[k] && (keys >> k & 1u) && held[k] >= base && held[k] < base + 27u)
                lit |= 1u << (held[k] - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_ENV, set.latch || midi_sus);
        fm1_led_key(BTN_LFO, user_mode);
        fm1_led_key(BTN_SAVE, save_wait);
        fm1_led_key(BTN_ARP, play.arp_on);
        fm1_led_key(BTN_SEQ, play.seq_on);
        fm1_led_key(BTN_REC, play.rec);
        fm1_led_key(BTN_PLAY, int_run || clk_run || (clk_bpm && fm1_ms - clk_beat_ms < 80u));
#if BOARD_SMK37
        for (k = 0; k < 16u; k++) {
            uint32_t w = k < 8u ? k + 8u : k - 8u, on = lit >> WHITE[w] & 1u;
            padled_set(k, on ? 90 : 8, on ? 50 : 4, on ? 16 : 2);
        }
        padled_flush();
#endif
        pr = &presets[pm_preset_index()];
        note_name(pm_last_note(), note);
        parm = ui_page;
        for (k = 0; k < PM_NPARAM; k++)
            parm = parm * 131u + pm_get_param((uint8_t)k);
        parm ^= (uint32_t)pm_model() << 24;
        parm ^= (uint32_t)pm_fx_kind() << 16;
        parm ^= (uint32_t)play.mode << 8;
        parm ^= seq_edit;
        parm ^= (uint32_t)mod_depth << 3;
        parm ^= (uint32_t)bend_range << 11;
        parm ^= (uint32_t)set.latch << 16;
        parm ^= (uint32_t)play.rate << 17;
        parm ^= (uint32_t)play.gate << 20;
        parm ^= (uint32_t)play.seq_len << 24;
        parm ^= (uint32_t)play.latch << 28;

        idle_ok = 1;
        if (test_mode) {
            static uint32_t seen_btn, seen_id;
            if (screen_force || fm1_in.buttons != seen_btn || last_id != seen_id || last_enc >= 0) {
                seen_btn = fm1_in.buttons;
                seen_id = last_id;
                draw_test();
                last_enc = -1;
                screen_force = 0;
            }
        } else {
            uint8_t force = screen_force;
            uint32_t head = ui_page ^ ((uint32_t)pm_preset_index() << 8) ^ ((uint32_t)user_mode << 16) ^ ((uint32_t)user_slot << 20) ^ ((uint32_t)pm_last_note() << 24);
            if (user_mode) {
                uint32_t i = 0;
                const char *p = ubank.slot[user_slot].used ? ubank.slot[user_slot].name : "EMPTY";
                line[i++] = 'U';
                line[i++] = (char)('1' + user_slot);
                line[i++] = ' ';
                while (*p && i < 16u)
                    line[i++] = *p++;
                line[i] = 0;
            } else {
                uint32_t i = 3;
                const char *p;
                line[0] = (char)('0' + (pm_preset_index() + 1u) / 10u);
                line[1] = (char)('0' + (pm_preset_index() + 1u) % 10u);
                line[2] = ' ';
                p = pr->name;
                while (*p && i < 16u)
                    line[i++] = *p++;
                line[i] = 0;
            }
            screen_force = 0;
            if (force || head != preset_drawn || (uint32_t)pm_last_note() != note_drawn) {
                preset_drawn = head;
                note_drawn = pm_last_note();
                draw_header(line, note);
            }
            if (force || parm != parm_drawn) {
                parm_drawn = parm;
                draw_params();
            }
            if (force || banner_until) {
                draw_banner();
                banner_until = 0;
            }
            {
                static uint32_t scope_drawn;
                if (fm1_ms - scope_drawn > 80u) {
                    scope_drawn = fm1_ms;
                    draw_scope();
                }
            }
            if (lit != lit_drawn) {
                lit_drawn = lit;
                draw_keys(lit);
            }
        }
        st = (uint32_t)(oct + 2) | ((uint32_t)set.ch << 4) | (clk_bpm << 12) | (set.latch << 20) | (midi_sus << 21) | (midi_src << 22) | ((uint32_t)pm_fx_kind() << 26) | ((uint32_t)ui_page << 29);
        if (st != status_drawn) {
            char s[40];
            uint32_t i = 0;
            const char *p = "OCT ";
            status_drawn = st;
            p = PAGE_NAME[ui_page];
            while (*p)
                s[i++] = *p++;
            p = " OCT ";
            while (*p)
                s[i++] = *p++;
            s[i++] = oct < 0 ? '-' : '+';
            s[i++] = (char)('0' + (oct < 0 ? -oct : oct));
            p = set.ch ? "  CH " : "  OMNI";
            while (*p)
                s[i++] = *p++;
            if (set.ch) {
                if (set.ch >= 10u)
                    s[i++] = (char)('0' + set.ch / 10u);
                s[i++] = (char)('0' + set.ch % 10u);
            }
            if (set.latch || midi_sus) {
                p = "  SUS";
                while (*p)
                    s[i++] = *p++;
            }
            if (midi_src == 1u || midi_src == 2u) {
                p = midi_src == 2u ? " TRS" : " USB";
                while (*p)
                    s[i++] = *p++;
            }
            if (pm_fx_kind()) {
                const char *fxn = pm_fx_kind() == PM_FX_REV ? " REV" : pm_fx_kind() == PM_FX_DLY ? " DLY" : " CHO";
                while (*fxn && i < 30u)
                    s[i++] = *fxn++;
            }
            if (clk_bpm) {
                s[i++] = ' ';
                if (clk_bpm >= 100u)
                    s[i++] = (char)('0' + clk_bpm / 100u);
                s[i++] = (char)('0' + clk_bpm / 10u % 10u);
                s[i++] = (char)('0' + clk_bpm % 10u);
            }
            s[i] = 0;
            draw_text_box(0, 220, 240, &FONT_S, s, C_INK, 1);
        }
        idle_ok = 0;

        if (save_wait && pm_quiet()) {
            save_user_bank();
            save_wait = 0;
        }
        if (dirty && pm_quiet() && fm1_ms - t_change > 2000u) {
            capture_set();
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
