/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM and contributors */
/* MONO: a bass and lead voice in the manner of the Roland SH-101: a band-limited sawtooth and a pulse wave (its width
 * moved slowly, as by an LFO), a square one octave down (the SUB), and a four-pole low-pass filter whose cutoff the
 * track's envelope opens (the filter follows the amplitude envelope: SUSTAIN low and DECAY short make it pluck, as an
 * acid or EBM bass). Its own design, using dsp.c's oscillators and filter; it is not the SH-101's circuit and does
 * not use any of its code or samples. An engine of voices.c: mono_begin, mono_block, mono_text.
 *   WAVE   0 SAW .. 127 PULSE, the two crossfaded
 *   SUB    the square an octave down, 0 .. 100 %
 *   CUTOFF the filter's cutoff (key-tracked, and opened by the envelope up to MN_ENV steps more)
 *   RESO   its resonance (the peak sits at the cutoff; the second pole is not resonant)
 * The voice is polyphonic like every source (a voice a note); a track that plays one note at a time is the SH-101's
 * mono. */

#define MN_ENV 40                               /* the envelope's reach on the cutoff (of 127 steps) */
#define MN_PWM_INC 77914u                       /* the pulse width's LFO: 0.8 Hz (Hz * 2^32 / 44100) */
#define MN_PWM_DEPTH 16384                      /* its swing: +-12.5 % around 50 % (a full sine * this, as a phase) */

static void mono_begin(voice_t *v)              /* a note from silence: the oscillators start at the same phase, so a bass
                                                 * hits alike every time */
{
    uint32_t k;
    for (k = 0; k < VPH; k++)
        v->ph[k] = 0;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;  /* the two filter stages */
}

/* CTL frames of voice v, added to mono[] */
static void mono_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    int32_t wave = clamp(p[0], 0, 127), sub = clamp(p[1], 0, 127), p16 = VP16(v), i;
    int32_t wp = wave * 258, wa = 32767 - wp, sq = (sub * 258 * 12000) >> 15;       /* saw, pulse and sub gains */
    int32_t cut = (clamp(p[2], 0, 127) << 8) + ((64 * (p16 - 60 * 16)) >> 4)         /* KTR 64, as VA */
                  + (((amp0 + amp1) >> 1) * (MN_ENV << 8) >> 15);                    /* the envelope opens it */
    int32_t a1 = v->s[0], a2 = v->s[1], b1 = v->s[2], b2 = v->s[3], damp = amp1 - amp0;
    uint32_t inc = vinc(p16), ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    tsvf_t f1, f2;
    tsvf_coef(&f1, cut, clamp(p[3], 0, 127));
    tsvf_coef(&f2, cut, 0);
    for (i = 0; i < (int32_t)CTL; i++) {
        uint32_t pw = 0x80000000u + (uint32_t)(sine_i(ph2) * MN_PWM_DEPTH);
        int32_t s, y;
        s = mulq15(osc_saw(ph0, inc), wa) + mulq15(osc_pulse(ph0, inc, pw), wp) + (((ph1 >> 31) ? sq : -sq));
        ph0 += inc;
        ph1 += inc >> 1;                        /* an octave down */
        ph2 += MN_PWM_INC;
        y = tsvf_lp(&f1, s >> 1, &a1, &a2);
        y = tsvf_lp(&f2, y, &b1, &b2);
        y = soft_knee(y, 16000) << 1;
        mono[i] += mulq15(mulq15(y, amp_at(amp0, damp, i)), VOICE_LV) << 1;   /* (+6 dB: the four poles take a lot) */
    }
    v->s[0] = a1, v->s[1] = a2, v->s[2] = b1, v->s[3] = b2;
    v->ph[0] = ph0, v->ph[1] = ph1, v->ph[2] = ph2;
}

static void mono_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = clamp(p[k], 0, 127);
    switch (k) {
    case 0:
        if (v == 0) put_s(s, "SAW");
        else if (v == 127) put_s(s, "PULSE");
        else put_s(put_u(s, ((uint32_t)v * 100u + 63u) / 127u), "% PLS");
        return;
    case 1: put_s(put_u(s, ((uint32_t)v * 100u + 63u) / 127u), "%"); return;
    case 2: put_s(put_u(s, CUTOFF_HZ[v]), " Hz"); return;
    default: put_u(s, (uint32_t)v); return;
    }
}

static const engine_t MONO_ENGINE = {"MONO", {"WAVE", "SUB", "CUTOFF", "RESONANCE"}, {127, 127, 127, 127},
                                     mono_begin, mono_block, mono_text};
