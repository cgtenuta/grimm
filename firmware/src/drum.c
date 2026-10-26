/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM and contributors */
/* DRUM: a synthesized drum source for hard, dry, distorted kits (EBM, industrial, techno). Nothing is sampled: every
 * hit is built from sines, squares and noise, in the manner of analog drum machines (a pitch-swept sine for the kick,
 * square oscillators through a high-pass for the hats). An engine of voices.c: drum_begin, drum_block, drum_text.
 *
 * The note picks the drum, by its pitch class (any octave), so a track's 12 keys are a kit:
 *   C  KICK            C# KICK HARD (longer sweep, a built-in push into the DRIVE)   D  SNARE        D# CLAP
 *   E  RIM             F  TOM LOW    F# HAT CLOSED   G  TOM MID    G# HAT OPEN    A  TOM HIGH
 *   A# METAL (two squares ring-modulated)            B  NOISE (a burst of high-passed noise, a crash)
 * The four knobs, for whichever drum plays:
 *   TUNE   -64 .. +63, the drum's pitch (about 3/16 of a semitone a step, so +- 12 semitones)
 *   DECAY  how long it rings (each drum has its own range)
 *   DRIVE  hard clipping of the drum, after its envelope (0 clean .. 127 flat-topped)
 *   SNAP   the attack: the kick's click and sweep, the snare's noise against its body, the clap's, rim's and
 *          metal's brightness
 * The drum is one-shot: it ends by itself, and then hands its voice back (the track's own ENV still applies on top, so
 * give a drum sound SUSTAIN 100 % and a long RELEASE, which a key let go would otherwise cut it short with).
 * DRIVE clips the raw oscillator or noise and the decay comes after it, so a driven hit stays hard-edged to the end
 * (the snare's body and the rim also decay inside, a second, faster envelope). Envelopes run once a control block (CTL frames) in Q24 and are ramped across it; amplitudes are Q15. */

#define DR_LV 12000                             /* a drum's peak level (VOICE_LV is 8000: drums are meant to hit) */
#define DR_ONE 16777215                         /* Q24 one */
enum { DR_KICK, DR_KICKH, DR_SNARE, DR_CLAP, DR_RIM, DR_TOML, DR_HATC, DR_TOMM, DR_HATO, DR_TOMH, DR_METAL, DR_NOISE,
       DR_N };
/* the 808 hats' six square frequencies (Hz) as phase increments: Hz * 2^32 / 44100 */
static const uint32_t DR_HAT_INC[6] = {263u * 97392u, 400u * 97392u, 421u * 97392u, 474u * 97392u, 587u * 97392u,
                                       845u * 97392u};
/* a drum's decay time: tau = tmin + tspan * DECAY^2 / 16384 (ms), and 60 dB-ish per ~7 tau */
static const uint16_t DR_TAU[DR_N][2] = {
    {40, 360}, {60, 520}, {60, 300}, {70, 280}, {8, 32}, {80, 480}, {12, 45}, {80, 480}, {90, 420}, {80, 400},
    {70, 560}, {120, 1500}};
/* a pitch sweep: the base pitch (1/16 semitone, MIDI note x 16), the sweep's size at SNAP 0 and its extra per SNAP
 * step, and the sweep's decay time (ms; 0: no sweep) */
static const uint16_t DR_PITCH[DR_N][4] = {
    {528, 200, 4, 25}, {496, 260, 5, 40}, {880, 40, 1, 15}, {0, 0, 0, 0}, {1482, 0, 0, 0}, {688, 60, 1, 40},
    {0, 0, 0, 0}, {800, 60, 1, 40}, {0, 0, 0, 0}, {912, 60, 1, 40}, {1269, 0, 0, 0}, {0, 0, 0, 0}};

/* a decay factor per control block, Q16: exp(-CTL / (fs tau)) ~ 1 - 0.7256 / tau(ms) */
static inline uint32_t dr_k(uint32_t tau_ms)
{
    uint32_t x = 47554u / (tau_ms ? tau_ms : 1u);
    return x > 60000u ? 5536u : 65536u - x;
}
/* inc scaled by q / 4096 without 64-bit */
static inline uint32_t dr_scale(uint32_t inc, uint32_t q)
{
    return (inc >> 12) * q + (((inc & 0xFFFu) * q) >> 12);
}
static inline int32_t dr_noise(voice_t *v) { return (int32_t)(noise32(&v->rng) >> 16) - 32768; }
/* DRIVE: gain 1x .. ~17x into the tanh table, which flattens at about +-0.96 (a hard clip when pushed) */
static inline int32_t dr_drive(int32_t x, int32_t drv)
{
    int32_t g;
    if (drv <= 0)
        return x;
    g = 384 + ((drv * drv) >> 2);
    return softclip((x * g) >> 8);
}

static void drum_begin(voice_t *v)              /* a note from silence: the drum its pitch class names */
{
    uint32_t k;
    for (k = 0; k < VPH; k++)
        v->ph[k] = 0;
    v->ph[7] = (uint32_t)(v->note % DR_N);
    v->s[0] = DR_ONE;                           /* the amplitude envelope */
    v->s[1] = DR_ONE;                           /* the pitch sweep */
    v->s[2] = v->s[3] = 0;                      /* the noise filters */
}

/* CTL frames of drum v, added to mono[] */
static void drum_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    int32_t acc[CTL], ge[CTL], i;
    uint32_t id = v->ph[7] % DR_N, age = v->ph[6], bi = age >> CTL_LOG2;
    int32_t tune = clamp(p[0], 0, 127) - 64, dec = clamp(p[1], 0, 127), drv = clamp(p[2], 0, 127),
            snp = clamp(p[3], 0, 127);
    int32_t e0 = v->s[0], e1, pe0 = v->s[1], pe1 = pe0, g0, g1, off0 = 0, off1 = 0;
    uint32_t tau = DR_TAU[id][0] + (uint32_t)((DR_TAU[id][1] * dec * dec) >> 14), k = dr_k(tau);

    if (id == DR_CLAP) {                        /* four bursts 14 blocks (~10 ms) apart, then the tail */
        if (bi == 0u || bi == 14u || bi == 28u || bi == 42u)
            e0 = DR_ONE;
        if (bi < 42u)
            k = dr_k(6u);
    }
    e1 = mulq16(e0, k);
    g0 = e0 >> 9, g1 = e1 >> 9;
    for (i = 0; i < CTL; i++)
        ge[i] = amp_at(g0, g1 - g0, i);

    if (DR_PITCH[id][3]) {                      /* the pitch sweep: size, then its decay */
        int32_t size = (int32_t)DR_PITCH[id][1] + snp * (int32_t)DR_PITCH[id][2];
        pe1 = mulq16(pe0, dr_k(DR_PITCH[id][3]));
        off0 = ((pe0 >> 8) * size) >> 16;
        off1 = ((pe1 >> 8) * size) >> 16;
    }

    switch (id) {
    case DR_KICK: case DR_KICKH: case DR_TOML: case DR_TOMM: case DR_TOMH: {
        int32_t base = (int32_t)DR_PITCH[id][0] + tune * 3;
        uint32_t inc0 = pitch_inc((uint32_t)clamp(base + off0, 0, 2047)), inc1 = pitch_inc((uint32_t)clamp(base + off1, 0, 2047));
        int32_t dinc = ((int32_t)inc1 - (int32_t)inc0) / CTL;
        uint32_t ph = v->ph[0], inc = inc0;
        for (i = 0; i < CTL; i++) {
            int32_t s;
            ph += inc;
            inc = (uint32_t)((int32_t)inc + dinc);
            s = sine_i(ph);
            if (id == DR_KICKH)                 /* a second harmonic, a flatter, harder top */
                s = clamp(s + (sine_i(ph * 2u) >> 1) + (s >> 2), -32767, 32767);
            acc[i] = s;
        }
        v->ph[0] = ph;
        if (id <= DR_KICKH && age < 4u * CTL) {                 /* the click: ~3 ms of noise, SNAP's */
            for (i = 0; i < CTL; i++) {
                int32_t a = (int32_t)(4u * CTL) - (int32_t)age - i;
                acc[i] += (((dr_noise(v) * snp) >> 8) * a) >> 8;
            }
        }
        if (id == DR_KICKH && drv < 40)         /* the hard kick pushes itself */
            drv = 40;
        break;
    }
    case DR_SNARE: {
        int32_t base = (int32_t)DR_PITCH[id][0] + tune * 3, lp = v->s[2];
        uint32_t inc0 = pitch_inc((uint32_t)clamp(base + off0, 0, 2047)), inc1 = pitch_inc((uint32_t)clamp(base + 160 + off1, 0, 2047));
        uint32_t pa = v->ph[0], pb = v->ph[1];
        for (i = 0; i < CTL; i++) {
            int32_t z = dr_noise(v), hp, tone;
            hp = z - lp;
            lp += (z - lp) >> 3;
            pa += inc0, pb += inc1;
            tone = (sine_i(pa) + (sine_i(pb) >> 1)) >> 1;
            acc[i] = mulq15(tone, ge[i]) * (127 - snp) / 127 + hp * snp / 127;   /* (the body decays faster) */
        }
        v->s[2] = lp, v->ph[0] = pa, v->ph[1] = pb;
        break;
    }
    case DR_CLAP: {
        int32_t lp1 = v->s[2], lp2 = v->s[3], lp3 = v->ph[0];
        for (i = 0; i < CTL; i++) {             /* noise through a band of roughly 0.5 .. 2 kHz */
            int32_t z = dr_noise(v), hp = z - lp1;
            lp1 += (z - lp1) >> 4;
            lp2 += (hp - lp2) >> (snp > 64 ? 1 : 2);
            lp3 += (lp2 - lp3) >> (snp > 64 ? 1 : 2);   /* a second pole: a band, not a hiss */
            acc[i] = lp3 << 1;
        }
        v->s[2] = lp1, v->s[3] = lp2, v->ph[0] = (uint32_t)lp3;
        break;
    }
    case DR_RIM: {
        uint32_t inc = pitch_inc((uint32_t)clamp((int32_t)DR_PITCH[id][0] + tune * 3, 0, 2047)), ph = v->ph[0];
        for (i = 0; i < CTL; i++) {             /* a short bright ping with a click */
            int32_t t;
            ph += inc;
            t = (((ph >> 31) ? 20000 : -20000) * (127 + snp) >> 8) + (sine_i(ph * 3u) >> 2);
            acc[i] = mulq15(t, ge[i]);   /* (a ping: a squared decay, as well as the one after) */
        }
        v->ph[0] = ph;
        break;
    }
    case DR_HATC: case DR_HATO: {
        uint32_t q = (uint32_t)(4096 + tune * 32);
        int32_t lp1 = v->s[2], lp2 = v->s[3];
        uint32_t inc[6], ph[6];
        int32_t j;
        for (j = 0; j < 6; j++)
            inc[j] = dr_scale(DR_HAT_INC[j], q), ph[j] = v->ph[j];
        for (i = 0; i < CTL; i++) {
            int32_t s = 0, h1, h2;
            for (j = 0; j < 6; j++) {
                ph[j] += inc[j];
                s += (ph[j] >> 31) ? 5000 : -5000;
            }
            h1 = s - lp1;
            lp1 += (s - lp1) >> 1;
            h2 = h1 - lp2;
            lp2 += (h1 - lp2) >> 1;
            acc[i] = h2;
        }
        for (j = 0; j < 6; j++)
            v->ph[j] = ph[j];
        v->s[2] = lp1, v->s[3] = lp2;
        break;
    }
    case DR_METAL: {
        int32_t base = (int32_t)DR_PITCH[id][0] + tune * 3, lp = v->s[2];
        uint32_t inc0 = pitch_inc((uint32_t)clamp(base, 0, 2047)), inc1 = pitch_inc((uint32_t)clamp(base + 113, 0, 2047));
        uint32_t pa = v->ph[0], pb = v->ph[1];
        for (i = 0; i < CTL; i++) {             /* two squares ring-modulated (XOR), high-passed */
            int32_t r, hp;
            pa += inc0, pb += inc1;
            r = ((pa ^ pb) >> 31) ? 22000 : -22000;
            hp = r - lp;
            lp += (r - lp) >> (snp > 64 ? 2 : 4);
            acc[i] = hp;
        }
        v->s[2] = lp, v->ph[0] = pa, v->ph[1] = pb;
        break;
    }
    default: {                                  /* DR_NOISE: a high-passed burst */
        int32_t lp = v->s[2];
        for (i = 0; i < CTL; i++) {
            int32_t z = dr_noise(v), hp = z - lp;
            lp += (z - lp) >> 2;
            acc[i] = hp;
        }
        v->s[2] = lp;
        break;
    }
    }

    for (i = 0; i < CTL; i++) {
        int32_t y = mulq15(dr_drive(acc[i], drv), ge[i]);   /* DRIVE first, then the decay */
        mono[i] += mulq15(mulq15(y, amp_at(amp0, amp1 - amp0, i)), DR_LV) << 1;
    }
    v->s[0] = e1;
    v->s[1] = pe1;
    v->ph[6] = age + (uint32_t)CTL;
    if (age > 2048u && e1 < 24000 && !(id == DR_CLAP && bi < 42u))
        v->stage = ST_FADE;                     /* it has died away: give the voice back (a ~10 ms fade) */
}

static void drum_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = clamp(p[k], 0, 127);
    if (k == 0u) {
        s = put_s(s, v >= 64 ? "+" : "-");
        put_u(s, (uint32_t)(v >= 64 ? v - 64 : 64 - v));
        return;
    }
    put_s(put_u(s, ((uint32_t)v * 100u + 63u) / 127u), "%");
}

static const engine_t DRUM_ENGINE = {"DRUM", {"TUNE", "DECAY", "DRIVE", "SNAP"}, {127, 127, 127, 127},
                                     drum_begin, drum_block, drum_text};
