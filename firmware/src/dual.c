/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM and contributors */
/* DUAL: a two-oscillator analog-modelling voice for aggressive leads, stabs and basses, in the manner of the Korg
 * Electribe A: two oscillators of the same waveform, the second set an interval away, joined four ways. Its own design
 * on dsp.c's oscillators; it has no filter of its own (use TRACK's CUTOFF and RESONANCE, which sit after every
 * source) and none of Korg's code. An engine of voices.c: dual_begin, dual_block, dual_text.
 *   WAVE    SAW, SQUARE or TRIANGLE (both oscillators)
 *   OSC 2   the second oscillator's interval, -24 .. +24 semitones (centre: in unison, a few cents apart in MIX)
 *   MODE    MIX: the two added            SYNC: oscillator 2 restarts with oscillator 1 (sweep OSC 2 for the sync sound)
 *           RING: oscillator 1 times oscillator 2    FM: oscillator 2 bends the phase of a sine (WAVE is not used)
 *   AMOUNT  MIX: oscillator 2's level; SYNC and RING: the share of the result against oscillator 1 alone;
 *           FM: the index, up to 3 cycles
 * SYNC restarts the second oscillator without band-limiting the break, so it is bright and a little aliased: that is
 * its character. */

enum { DUAL_SAW, DUAL_SQR, DUAL_TRI, DUAL_NWAVE };
enum { DUAL_MIX, DUAL_SYNC, DUAL_RING, DUAL_FM, DUAL_NMODE };
static const char *const DUAL_WAVE_NAME[DUAL_NWAVE] = {"SAW", "SQUARE", "TRIANGLE"};
static const char *const DUAL_MODE_NAME[DUAL_NMODE] = {"MIX", "SYNC", "RING", "FM"};

static inline int32_t dual_semis(int32_t v)     /* OSC 2: 0..127 -> -24 .. +24 semitones, rounded */
{
    int32_t d = (clamp(v, 0, 127) - 64) * 24;
    return (d + (d < 0 ? -32 : 32)) / 64;
}
static inline int32_t dual_osc(int32_t wave, uint32_t ph, uint32_t inc)
{
    int32_t t;
    if (wave == DUAL_SQR)
        return osc_pulse(ph, inc, 0x80000000u);
    if (wave == DUAL_TRI) {
        t = (int32_t)(ph >> 16);
        return (t < 32768 ? t * 2 : (65535 - t) * 2) - 32768;
    }
    return osc_saw(ph, inc);
}

static void dual_begin(voice_t *v)              /* a note from silence: both oscillators at phase 0, so a hit is alike each time */
{
    uint32_t k;
    for (k = 0; k < VPH; k++)
        v->ph[k] = 0;
}

/* CTL frames of voice v, added to mono[] */
static void dual_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    int32_t wave = clamp(p[0], 0, DUAL_NWAVE - 1), semis = dual_semis(p[1]), mode = clamp(p[2], 0, DUAL_NMODE - 1),
            amt = clamp(p[3], 0, 127), p16 = VP16(v), damp = amp1 - amp0, i;
    int32_t a = amt * 258;                      /* Q15 */
    uint32_t inc = vinc(p16), inc2, ph0 = v->ph[0], ph1 = v->ph[1], fmi = (uint32_t)(amt * 3096);
    inc2 = mode == DUAL_MIX ? cents_inc(p16 + semis * 16, 7, tune_f) : vinc(p16 + semis * 16);
    for (i = 0; i < (int32_t)CTL; i++) {
        int32_t o1, o2, s;
        switch (mode) {
        case DUAL_SYNC:
            o1 = dual_osc(wave, ph0, inc);
            o2 = dual_osc(wave, ph1, inc2);
            s = o1 + mulq15(o2 - o1, a);
            break;
        case DUAL_RING:
            o1 = dual_osc(wave, ph0, inc);
            o2 = dual_osc(wave, ph1, inc2);
            s = o1 + mulq15(mulq15(o1, o2) - o1, a);
            break;
        case DUAL_FM:
            s = sine_i(ph0 + (uint32_t)sine_i(ph1) * fmi);
            break;
        default:
            s = dual_osc(wave, ph0, inc) + mulq15(dual_osc(wave, ph1, inc2), a);
            s >>= 1;
            break;
        }
        ph0 += inc;
        ph1 += inc2;
        if (mode == DUAL_SYNC && ph0 < inc)     /* oscillator 1 wrapped: oscillator 2 starts over */
            ph1 = 0;
        mono[i] += mulq15(mulq15(soft_knee(s, 24000), amp_at(amp0, damp, i)), VOICE_LV) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
}

static void dual_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = clamp(p[k], 0, 127), d;
    switch (k) {
    case 0: put_s(s, DUAL_WAVE_NAME[clamp(v, 0, DUAL_NWAVE - 1)]); return;
    case 1:
        d = dual_semis(v);
        s = put_s(s, d < 0 ? "-" : "+");
        put_s(put_u(s, (uint32_t)(d < 0 ? -d : d)), " st");
        return;
    case 2: put_s(s, DUAL_MODE_NAME[clamp(v, 0, DUAL_NMODE - 1)]); return;
    default: put_s(put_u(s, ((uint32_t)v * 100u + 63u) / 127u), "%"); return;
    }
}

static const engine_t DUAL_ENGINE = {"DUAL", {"WAVE", "OSC 2", "MODE", "AMOUNT"}, {DUAL_NWAVE - 1, 127, DUAL_NMODE - 1, 127},
                                     dual_begin, dual_block, dual_text};
