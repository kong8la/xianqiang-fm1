/* SPDX-License-Identifier: GPL-3.0-only */
/* 道哥合成器 / DaoGe Synth — physical-modeling firmware for the M-VAVE FM-1.
 *
 * The panel, USB, MIDI, screen and update path are Dinghy (GPL-3.0-only),
 * itself the Felucca / SLOOP platform. Upstream Dinghy commit ea1b6bf.
 * The sound is dsp/pm.c (MIT): Karplus-Strong, modal bar / bell / membrane,
 * a sustained bowed string and a clarinet / flute waveguide.
 *
 * Integers only. app_audio() only calls pm_render(). The screen and the
 * encoders run in the main loop. */
#include "daoge_title.h"
#define APP_NAME "XIANQ"
#define STORE_MAGIC 0x474E5158u             /* "XQNG" — not Dinghy's DNGY */
#define CV_MAX (240u * 96u)
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

#define C_BG RGB(14, 15, 18)
#define C_RULE RGB(46, 52, 60)
#define C_CYAN RGB(42, 224, 206)
#define C_ORANGE RGB(255, 168, 42)
#define C_TEXT RGB(224, 228, 232)
#define C_MUTE RGB(118, 126, 136)
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

static uint8_t ui_page, seq_edit;
static uint32_t catch_until;

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
static const char *const PAGE_NAME[4] = {"TONE", "FX", "ARP", "SEQ"};
static const char *const BTN_LABEL[14] = {
    "OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};

static char banner[20];
static uint32_t banner_until;
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
    banner_until = fm1_ms + 900u;
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
    ui_page = page & 3u;
    for (ei = 0; ei < 7u; ei++)
        (void)fm1_enc_take(ei);
    catch_until = fm1_ms + 250u;
    toast(PAGE_NAME[ui_page]);
}

static const char *fx_tag(void)
{
    uint8_t k = pm_fx_kind();
    return k == PM_FX_REV ? "REV" : k == PM_FX_DLY ? "DLY" : k == PM_FX_CHO ? "CHO" : "OFF";
}

static void knob_cell(int32_t x, int32_t y, const char *name, unsigned v, unsigned max, uint16_t acc)
{
    char num[4];
    int lit = max ? (int)(v * 16u / max) : 0;
    int i;
    if (lit > 16)
        lit = 16;
    cv_text(x, y, &FONT_S, name, C_MUTE);
    num3(num, v);
    cv_text(x + 46, y, &FONT_S, num, acc);
    for (i = 0; i < 16; i++) {
        int32_t px = x + 16 + ARC_X[i];
        int32_t py = y + 28 + ARC_Y[i];
        uint16_t c = i < lit ? acc : C_RULE;
        cv_pset(px, py, c);
        cv_pset(px + 1, py, c);
    }
}

static void draw_params(void)
{
    static const char *const FXN[6] = {"TYPE", "MIX", "TIME", "FB", "----", "LEVEL"};
    static const char *const ARPN[6] = {"MODE", "RATE", "GATE", "OCT", "LATCH", "LEVEL"};
    static const char *const SEQN[6] = {"STEP", "NOTE", "VEL", "LEN", "----", "LEVEL"};
    uint8_t knd, mx, tm, fbk;
    uint32_t i;
    pm_fx_get(&knd, &mx, &tm, &fbk);
    cv_begin(240, 96, C_BG);
    cv_rect(4, 0, 232, 94, C_RULE);
    cv_rect(5, 1, 230, 92, C_BG);
    for (i = 0; i < 6u; i++) {
        const char *name = "----";
        unsigned v = 0, max = 127;
        int32_t x = (int32_t)(i % 3u) * 78 + 6;
        int32_t y = (int32_t)(i / 3u) * 46 + 2;
        uint16_t acc = i == 5 ? C_ORANGE : C_CYAN;
        if (ui_page == 0) {
            name = pm_param_name(pm_model(), (uint8_t)i);
            v = pm_get_param((uint8_t)i);
        } else if (ui_page == 1) {
            name = FXN[i];
            v = i == 0 ? knd : i == 1 ? mx : i == 2 ? tm : i == 3 ? fbk : i == 5 ? pm_get_param(PM_LEVEL) : 0;
            max = i == 0 ? 3 : 127;
        } else if (ui_page == 2) {
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
    cv_blit(0, 74);
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
            cv_rect(6 + (int32_t)w * 14 - 4, 0, 8, 12, lit >> k & 1u ? C_CYAN : C_BG);
        else
            w++;
    cv_blit(0, 192);
}

static void draw_header(const char *preset_line, const char *note)
{
    static const char *const TAB[4] = {"TONE", "FX", "ARP", "SEQ"};
    uint32_t i;
    cv_begin(240, 20, C_BG);
    cv_text(2, 2, &FONT_S, note, note[0] == '-' ? C_MUTE : C_ORANGE);
    cv_text(40, 2, &FONT_S, preset_line, C_TEXT);
    cv_blit(0, 34);
    cv_begin(240, 18, C_BG);
    for (i = 0; i < 4u; i++) {
        int32_t x = 4 + (int32_t)i * 58;
        if (i == (ui_page & 3u)) {
            cv_rect(x, 14, 48, 2, C_CYAN);
            cv_text(x, 0, &FONT_S, TAB[i], C_CYAN);
        } else
            cv_text(x, 0, &FONT_S, TAB[i], C_MUTE);
    }
    cv_blit(0, 54);
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
        cv_line(4 + (int32_t)i * 3, y0, 4 + (int32_t)i * 3, y1, C_CYAN);
    }
    cv_blit(0, 172);
}

static void draw_banner(void)
{
    cv_begin(240, 22, C_BG);
    cv_rect(8, 0, 224, 20, C_RULE);
    cv_text(16, 2, &FONT_S, banner, C_ORANGE);
    cv_blit(0, 100);
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
    cv_begin(240, 80, C_BG);
    cv_text(8, 4, &FONT_S, "BTN TEST", C_CYAN);
    if (last_id < 14u) {
        line[0] = (char)('0' + last_id / 10u);
        line[1] = (char)('0' + last_id % 10u);
        line[2] = ' ';
        i = 3;
        {
            const char *p = BTN_LABEL[last_id];
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
    cv_blit(0, 40);
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

static struct {
    int8_t oct;
    uint8_t ch, preset, latch;
    uint8_t p[PM_NPARAM];
} set;

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

static uint8_t bend_range = 2;

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

static void app_idle(void)
{
    if (idle_ok)
        midi_in();
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
        uint32_t keys, down, up, rel, b, lit = 0, out_ch, st, parm;
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
        b = fm1_input_edges(&rel);
        if ((fm1_in.buttons & ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP))) == ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP))) {
            if (!both_oct_t)
                both_oct_t = fm1_ms;
            if (!test_mode && fm1_ms - both_oct_t > 450u) {
                test_mode = 1;
                toast("BTN TEST");
            }
        } else
            both_oct_t = 0;
        if (b & (1u << BTN_ENV)) {
            env_down = 1;
            env_t = fm1_ms;
            shift_used = 0;
        }
        if (rel & (1u << BTN_ENV)) {
            if (!test_mode && !shift_used && fm1_ms - env_t < 320u) {
                set.latch = (uint8_t)!set.latch;
                publish_sustain();
                toast(set.latch ? "SUS ON" : "SUS OFF");
                dirty = 1;
                t_change = fm1_ms;
            }
            env_down = 0;
        }
        if (test_mode) {
            int id = lowbit(b);
            if (id == (int)BTN_HOME) {
                test_mode = 0;
                ui_page = 0;
                toast("TONE");
            } else if (id >= 0)
                last_id = (uint8_t)id;
        } else {
            int shifted = shift_held();
            if (b & (1u << BTN_OCT_DOWN)) {
                if (shifted) {
                    go_page((uint8_t)((ui_page + 3u) & 3u));
                    shift_used = 1;
                } else if (oct > -2) {
                    set.oct = (int8_t)(oct - 1);
                    toast("OCT-");
                    dirty = 1;
                    t_change = fm1_ms;
                }
            }
            if (b & (1u << BTN_OCT_UP)) {
                if (shifted) {
                    go_page((uint8_t)((ui_page + 1u) & 3u));
                    shift_used = 1;
                } else if (oct < 2) {
                    set.oct = (int8_t)(oct + 1);
                    toast("OCT+");
                    dirty = 1;
                    t_change = fm1_ms;
                }
            }
            if (b & (1u << BTN_SEL)) {
                if (shifted) {
                    uint8_t k, m, t, f;
                    pm_fx_get(&k, &m, &t, &f);
                    k = (uint8_t)((k + 1u) % PM_FX_N);
                    if (!m) m = 64;
                    if (!t) t = 72;
                    if (!f) f = 48;
                    pm_fx_set(k, m, t, f);
                    go_page(1);
                    toast(fx_tag());
                    shift_used = 1;
                } else {
                    set.ch = (uint8_t)(set.ch >= 16u ? 0 : set.ch + 1u);
                    toast(set.ch ? "CHANNEL" : "OMNI");
                    dirty = 1;
                    t_change = fm1_ms;
                }
            }
            if (b & (1u << BTN_HOME)) {
                if (shifted) {
                    const char *nm = user_mode && ubank.slot[user_slot].used ? ubank.slot[user_slot].name : presets[pm_preset_index()].name;
                    pm_user_store_current(&ubank, user_slot, nm);
                    user_mode = 1;
                    save_wait = 1;
                    toast("SAVE");
                    shift_used = 1;
                } else {
                    pm_panic();
                    toast("PANIC");
                }
            }
            if (b & (1u << BTN_EDIT))
                go_page((uint8_t)((ui_page + 1u) & 3u));
            if (b & (1u << BTN_GLO)) {
                bend_range = (uint8_t)(bend_range >= 12u ? 2u : bend_range + 5u);
                toast(bend_range >= 12u ? "BEND 12" : bend_range >= 7u ? "BEND 7" : "BEND 2");
            }
            if (b & (1u << BTN_ARP)) {
                play.arp_on = (uint8_t)!play.arp_on;
                go_page(2);
                toast(play.arp_on ? "ARP ON" : "ARP OFF");
            }
            if (b & (1u << BTN_SEQ)) {
                play.seq_on = (uint8_t)!play.seq_on;
                go_page(3);
                toast(play.seq_on ? "SEQ ON" : "SEQ OFF");
            }
            if (b & (1u << BTN_REC)) {
                play.rec = (uint8_t)!play.rec;
                toast(play.rec ? "REC ON" : "REC OFF");
            }
            if (b & (1u << BTN_PLAY)) {
                int_run = int_run ? 0 : 1;
                int_clk_ms = fm1_ms;
                midi_send(int_run ? 0xFAu : 0xFCu, 0, 0);
                if (!int_run && play.sounding) {
                    pm_note_off(play.cur);
                    play.sounding = 0;
                }
                toast(int_run ? "PLAY" : "STOP");
            }
            if (b & (1u << BTN_FX)) {
                uint8_t k, m, t, f;
                pm_fx_get(&k, &m, &t, &f);
                k = (uint8_t)((k + 1u) % PM_FX_N);
                if (!m) m = 64;
                if (!t) t = 72;
                if (!f) f = 48;
                pm_fx_set(k, m, t, f);
                go_page(1);
                toast(fx_tag());
                dirty = 1;
                t_change = fm1_ms;
            }
            if (b & (1u << BTN_LFO)) {
                user_mode = (uint8_t)!user_mode;
                if (user_mode)
                    pm_user_recall(&ubank, user_slot);
                toast(user_mode ? "USER" : "PRESET");
            }
            if (b & (1u << BTN_SAVE)) {
                const char *nm = user_mode && ubank.slot[user_slot].used ? ubank.slot[user_slot].name : presets[pm_preset_index()].name;
                pm_user_store_current(&ubank, user_slot, nm);
                user_mode = 1;
                save_wait = 1;
                toast("SAVE");
            }
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
        if ((e = take_enc(ENC_SELECT)) != 0) {
            if (ui_page == 0)
                nudge(PM_DECAY, e);
            else if (ui_page == 1) {
                uint8_t k, m, t, f;
                pm_fx_get(&k, &m, &t, &f);
                add8(&k, e, 3);
                pm_fx_set(k, m, t, f);
            } else if (ui_page == 2)
                add8(&play.mode, e, 4);
            else
                add8(&seq_edit, e, 15);
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_ALGORITHM)) != 0) {
            if (ui_page == 0)
                nudge(PM_BRIGHT, e);
            else if (ui_page == 1) {
                uint8_t k, m, t, f;
                pm_fx_get(&k, &m, &t, &f);
                add8(&m, e, 127);
                pm_fx_set(k, m, t, f);
            } else if (ui_page == 2)
                add8(&play.rate, e, 7);
            else
                add8(&play.seq_note[seq_edit], e, 127);
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_KNOB1)) != 0) {
            if (ui_page == 0)
                nudge(PM_POS, e);
            else if (ui_page == 1) {
                uint8_t k, m, t, f;
                pm_fx_get(&k, &m, &t, &f);
                add8(&t, e, 127);
                pm_fx_set(k, m, t, f);
            } else if (ui_page == 2)
                add8(&play.gate, e, 8);
            else
                add8(&play.seq_vel[seq_edit], e, 127);
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_KNOB2)) != 0) {
            if (ui_page == 0)
                nudge(PM_COLOR, e);
            else if (ui_page == 1) {
                uint8_t k, m, t, f;
                pm_fx_get(&k, &m, &t, &f);
                add8(&f, e, 127);
                pm_fx_set(k, m, t, f);
            } else if (ui_page == 2)
                add8(&play.octaves, e, 3);
            else {
                int32_t n = (int32_t)play.seq_len + (e > 0 ? 1 : -1);
                if (n < 1)
                    n = 1;
                if (n > 16)
                    n = 16;
                play.seq_len = (uint8_t)n;
            }
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_KNOB3)) != 0) {
            if (ui_page == 0)
                nudge(PM_EXCITE, e);
            else if (ui_page == 2)
                play.latch = (uint8_t)!play.latch;
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = take_enc(ENC_KNOB4)) != 0) {
            nudge(PM_LEVEL, e);
            dirty = 1;
            t_change = fm1_ms;
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

        idle_ok = 1;
        if (test_mode) {
            static uint32_t seen_btn, seen_id;
            if (fm1_in.buttons != seen_btn || last_id != seen_id || last_enc >= 0) {
                seen_btn = fm1_in.buttons;
                seen_id = last_id;
                draw_test();
                last_enc = -1;
            }
        } else {
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
            if (head != preset_drawn || (uint32_t)pm_last_note() != note_drawn) {
                preset_drawn = head;
                note_drawn = pm_last_note();
                draw_header(line, note);
            }
            if (parm != parm_drawn) {
                parm_drawn = parm;
                draw_params();
            }
            if (banner_until && (int32_t)(banner_until - fm1_ms) > 0)
                draw_banner();
            else if (banner_until) {
                banner_until = 0;
                parm_drawn = ~0u;
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
            p = ui_page == 1 ? "FX " : ui_page == 2 ? "ARP " : ui_page == 3 ? "SEQ " : "TONE ";
            while (*p)
                s[i++] = *p++;
            p = "OCT ";
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
