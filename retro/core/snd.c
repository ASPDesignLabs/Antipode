#include "snd.h"
#include "core.h"

#define VOICES 28
enum { W_SIN, W_TRI, W_SAW, W_SQR, W_BELL, W_NOISE, W_BRASS, W_CYMBAL, W_KICK };
typedef struct { int on, wave; float ph, ph2, ph3, f0, f1, t, dur, vol, pan; } Voice;
static Voice V[VOICES];
static float g_vol = 0.7f, g_fade = 1.0f;
static uint32_t g_nz = 0x2545F491u;

void snd_init(void) { memset(V, 0, sizeof V); snd_music_stop(); }
void snd_set_volume(float v) { g_vol = clampf(v, 0.0f, 1.0f); }
void snd_set_fade(float k) { g_fade = clampf(k, 0.0f, 1.0f); }

static void voice(int wave, float f0, float f1, float dur, float vol, float delay, float pan) {
    int slot = 0;
    for (int i = 0; i < VOICES; i++) { if (!V[i].on) { slot = i; break; } if (V[i].t > V[slot].t) slot = i; }
    V[slot] = (Voice){ 1, wave, 0.0f, 0.0f, 0.0f, f0, f1, -delay, dur, vol, pan };
}

static const float R12C[12] = { 1.0f, 1.05946f, 1.12246f, 1.18921f, 1.25992f, 1.33484f, 1.41421f, 1.49831f, 1.58740f, 1.68179f, 1.78180f, 1.88775f };
static float mfreq(int m) { if (m < 0) m = 0; if (m > 127) m = 127; return 8.1758f * (float)(1 << (m / 12)) * R12C[m % 12]; }

void snd_sfx(int id) { snd_sfx_note(id, 0); }

void snd_sfx_note(int id, int note) {
    static const float PENT[5] = { 0, 2, 4, 7, 9 };
    int semi = (int)PENT[note % 5] + 12 * ((note / 5) % 2);
    if (g_theme.on) {                                        /* bells, a shake of sleigh bells, and a cracked bauble */
        switch (id) {
        case SFX_START:
            voice(W_BELL, mfreq(72), 0, 1.0f, 0.45f, 0.00f, 0.4f);
            voice(W_BELL, mfreq(76), 0, 1.0f, 0.45f, 0.08f, 0.5f);
            voice(W_BELL, mfreq(79), 0, 1.2f, 0.45f, 0.16f, 0.6f);
            break;
        case SFX_PICKUP: {
            float f = mfreq(72 + semi);
            voice(W_BELL, f, 0, 0.9f, 0.5f, 0.0f, 0.5f);
            voice(W_NOISE, 0, 0, 0.06f, 0.05f, 0.0f, 0.5f);
            break; }
        case SFX_CRASH: {                                                         /* the carol breaks off... */
            voice(W_KICK, 150.0f, 42.0f, 0.38f, 0.85f, 0.00f, 0.5f);
            voice(W_CYMBAL, 0, 0, 0.60f, 0.16f, 0.00f, 0.55f);                   /* ...a splash as it stops... */
            static const int TN[3] = { 58, 57, 56 };                              /* ...a sad trombone: wah, wah, wah... */
            for (int i = 0; i < 3; i++) voice(W_BRASS, mfreq(TN[i]), mfreq(TN[i]) * 0.985f, 0.42f, 0.50f, 0.10f + 0.45f * (float)i, 0.5f);
            voice(W_BRASS, mfreq(55), mfreq(50), 1.40f, 0.55f, 1.45f, 0.5f);      /* ...waaah, sliding down a fourth, and under it: */
            voice(W_KICK, 150.0f, 40.0f, 0.45f, 1.00f, 1.45f, 0.5f);              /* the kick, */
            voice(W_CYMBAL, 0, 0, 2.60f, 0.34f, 1.45f, 0.5f);                    /* a crash of cymbals, */
            voice(W_KICK, 140.0f, 38.0f, 0.50f, 0.85f, 1.72f, 0.5f);              /* and the kick again. */
            break; }
        case SFX_DIE:
            voice(W_NOISE, 0, 0, 0.12f, 0.22f, 0.0f, 0.5f);                    /* the crack */
            voice(W_BELL, mfreq(83), 0, 0.9f, 0.30f, 0.02f, 0.3f);               /* the bauble ringing... */
            voice(W_BELL, mfreq(89), 0, 0.9f, 0.26f, 0.02f, 0.7f);               /* ...a tritone off true */
            voice(W_SAW, 330.0f, 70.0f, 0.55f, 0.16f, 0.0f, 0.5f);
            break;
        }
        return;
    }
    if (id == SFX_CRASH) id = SFX_DIE;                                         /* the plain game has no trombone */
    static const float R12[12] = { 1.0f, 1.0595f, 1.1225f, 1.1892f, 1.2599f, 1.3348f, 1.4142f, 1.4983f, 1.5874f, 1.6818f, 1.7818f, 1.8877f };
    switch (id) {
    case SFX_START:
        voice(W_TRI, 330.0f, 660.0f, 0.18f, 0.5f, 0, 0.5f);
        break;
    case SFX_PICKUP: {
        float f = 523.25f * R12[semi % 12] * (semi >= 12 ? 2.0f : 1.0f);
        voice(W_SIN, f, f, 0.16f, 0.6f, 0, 0.5f);
        voice(W_TRI, f * 2.0f, f * 2.0f, 0.10f, 0.2f, 0, 0.5f);
        break; }
    case SFX_DIE:                                   /* a tritone - the antipode of the octave - falling away */
        voice(W_SAW, 440.0f, 60.0f, 0.6f, 0.4f, 0, 0.5f);
        voice(W_SQR, 622.25f, 80.0f, 0.6f, 0.22f, 0, 0.5f);
        break;
    }
}

/* ---- the carols ---------------------------------------------------------------------------------------------------
 * Notes are (MIDI note, length in units; -1 is a rest). A track says how long a unit is (units a minute), how many make a bar
 * (the bass plays the root of each bar's chord on its first beat and the fifth on its middle), and what it does each time
 * round: it climbs by the next of `mods` semitones, which is the modulation. `minor` lowers the third, sixth and seventh of the
 * tune's key, and so turns a bright tune dark (no track uses it now: they are all bright). These are public-domain tunes arranged by ear, and meant to be ours. */
typedef struct { short m, l; } Nt;
typedef struct { int upm, bar, tonic, minor, shaker, nn, nb, nmods; const Nt *n; const short *bass; const signed char *mods; } Track;

#define N(m, l) { m, l }
static const Nt T_SILENT[] = {                                  /* Silent Night, in 6/8 */
    N(67,3),N(69,1),N(67,2),N(64,6),  N(67,3),N(69,1),N(67,2),N(64,6),
    N(74,3),N(74,1),N(71,2),N(72,3),N(72,1),N(67,2),
    N(69,1),N(69,1),N(72,2),N(71,1),N(69,1),N(67,2),N(69,1),N(67,1),N(64,2),
    N(69,1),N(69,1),N(72,2),N(71,1),N(69,1),N(67,2),N(69,1),N(67,1),N(64,2),
    N(74,1),N(74,1),N(77,2),N(74,1),N(71,1),N(72,2),N(76,1),N(72,1),N(67,1),N(64,1),N(67,3),N(65,1),N(62,2),N(60,6) };
static const short B_SILENT[] = { 48,48,48,48,43,48,41,48,41,48,43,48,43,48 };
static const signed char M_SILENT[] = { 0, 3 };                 /* the second time through, a minor third up */

static const Nt T_JINGLE[] = {                                  /* the chorus of Jingle Bells, 4/4 */
    N(64,2),N(64,2),N(64,4),  N(64,2),N(64,2),N(64,4),  N(64,2),N(67,2),N(60,3),N(62,1),  N(64,8),
    N(65,2),N(65,2),N(65,3),N(65,1),  N(65,2),N(64,2),N(64,2),N(64,1),N(64,1),  N(64,2),N(62,2),N(62,2),N(64,2),  N(62,4),N(67,4),
    N(64,2),N(64,2),N(64,4),  N(64,2),N(64,2),N(64,4),  N(64,2),N(67,2),N(60,3),N(62,1),  N(64,8),
    N(65,2),N(65,2),N(65,3),N(65,1),  N(65,2),N(64,2),N(64,2),N(64,1),N(64,1),  N(67,2),N(67,2),N(65,2),N(62,2),  N(60,8) };
static const short B_JINGLE[] = { 48,48,48,48,41,41,48,43, 48,48,48,48,41,41,43,48 };
static const signed char M_JINGLE[] = { 0, 2, 4, 6 };

static const Nt T_DECK[] = {                                    /* Deck the Halls (the Welsh Nos Galan), 2/4 in sixteenths */
    N(74,3),N(72,1),N(71,2),N(69,2),  N(67,2),N(67,1),N(69,1),N(71,2),N(67,2),
    N(69,1),N(71,1),N(72,1),N(69,1),N(71,3),N(69,1),  N(67,2),N(66,2),N(67,4),
    N(74,2),N(74,1),N(72,1),N(71,2),N(69,2),  N(67,3),N(69,1),N(71,2),N(67,2),
    N(69,1),N(71,1),N(72,1),N(69,1),N(71,3),N(69,1),  N(67,2),N(66,2),N(67,4),
    N(69,2),N(69,1),N(71,1),N(72,2),N(69,2),  N(71,2),N(71,1),N(72,1),N(74,2),N(69,2),
    N(67,3),N(69,1),N(71,2),N(67,2),  N(67,2),N(66,2),N(67,4) };
static const short B_DECK[] = { 43,43,50,43, 43,43,50,43, 50,50,43,43 };
static const signed char M_DECK[] = { 0, 2, 3, 5 };

static const Nt T_JOY[] = {                                     /* Joy to the World's falling scale, and an answer of our own */
    N(72,3),N(71,1),N(69,3),N(67,1),  N(65,3),N(64,1),N(62,3),N(60,1),
    N(67,2),N(69,2),N(71,2),N(72,2),  N(72,8),
    N(67,3),N(69,1),N(71,3),N(72,1),  N(74,3),N(72,1),N(71,3),N(69,1),
    N(67,2),N(65,2),N(64,2),N(62,2),  N(60,8) };
static const short B_JOY[] = { 48,41,43,48, 48,43,41,48 };
static const signed char M_JOY[] = { 0, 2, 5, 7 };

static const Nt T_WISH[] = {                                    /* We Wish You a Merry Christmas, 3/4 */
    N(-1,4),N(62,2),
    N(67,2),N(67,1),N(69,1),N(67,1),N(66,1),  N(64,2),N(64,2),N(64,2),
    N(69,2),N(69,1),N(71,1),N(69,1),N(67,1),  N(66,2),N(62,2),N(62,2),
    N(71,2),N(71,1),N(72,1),N(71,1),N(69,1),  N(67,2),N(64,2),N(62,1),N(62,1),
    N(64,2),N(69,2),N(66,2),  N(67,6) };
static const short B_WISH[] = { 43,43,48,50,50,43,48,50,43 };
static const signed char M_WISH[] = { 0 };

#define TRK(name, upm, bar, tonic, minor, shaker, notes, bass, mods) \
    static const Track name = { upm, bar, tonic, minor, shaker, (int)(sizeof notes / sizeof notes[0]), (int)(sizeof bass / sizeof bass[0]), (int)(sizeof mods), notes, bass, mods }
TRK(K_SILENT, 150, 6, 60, 0, 0, T_SILENT, B_SILENT, M_SILENT);
TRK(K_JINGLE, 250, 8, 60, 0, 1, T_JINGLE, B_JINGLE, M_JINGLE);
TRK(K_DECK,   480, 8, 55, 0, 0, T_DECK,   B_DECK,   M_DECK);
TRK(K_JOY,    230, 8, 60, 0, 1, T_JOY,    B_JOY,    M_JOY);
TRK(K_WISH,   270, 6, 55, 0, 0, T_WISH,   B_WISH,   M_WISH);
static const Track *const TRACKS[] = { 0, &K_SILENT, &K_JINGLE, &K_DECK, &K_JOY, &K_WISH };

static struct { int id, idx, bar, loops, bump, ubase, half; float rem, upos, bar_t, tempo; } M;

void snd_music_stop(void) { M.id = MU_NONE; }
int snd_music_id(void) { return M.id; }
void snd_music_tempo(float mul) { M.tempo = clampf(mul, 0.4f, 2.5f); }
void snd_music_bump(int semis) { M.bump = semis; }
void snd_music_play(int id) {
    if (!g_theme.on || id <= MU_NONE || id > MU_WISH) { M.id = MU_NONE; return; }
    M.id = id; M.idx = -1; M.rem = 0.0f; M.upos = 0.0f; M.bar = -1; M.bar_t = (float)TRACKS[id]->bar; M.loops = 0; M.bump = 0; M.half = 1;   /* bar_t starts a bar full: the first bass note sounds at once, once */
    M.ubase = 0; M.tempo = 1.0f;
}

static int shape(const Track *k, int m) {                       /* minor: lower the third, sixth and seventh of the key */
    int pc = (((m - k->tonic) % 12) + 12) % 12;
    if (k->minor && (pc == 4 || pc == 9 || pc == 11)) m -= 1;
    return m;
}

static void music_step(const Track *k, float du, float unit_sec) {
    M.rem -= du; M.upos += du; M.bar_t += du;
    int trans = k->mods[M.loops % k->nmods] + M.bump;
    while (M.rem <= 0.0f) {                                     /* the next note */
        M.idx++;
        if (M.idx >= k->nn) { M.idx = 0; M.loops++; trans = k->mods[M.loops % k->nmods] + M.bump; M.upos = 0.0f; }
        Nt n = k->n[M.idx];
        if (n.m >= 0) {
            float len = (float)n.l * unit_sec;
            voice(W_BELL, mfreq(shape(k, n.m) + trans), 0, len * 1.7f > 1.6f ? 1.6f : len * 1.7f + 0.25f, 0.42f, 0.0f, 0.5f);
        }
        M.rem += (float)n.l;
    }
#ifndef SND_TEST_MELODY
    if (M.bar_t >= (float)k->bar) {                             /* the bass, at the top of each bar */
        M.bar_t -= (float)k->bar; M.bar++; M.half = 0;
        int root = k->bass[M.bar % k->nb];
        voice(W_TRI, mfreq(shape(k, root) + trans), 0, (float)k->bar * unit_sec * 0.95f, 0.30f, 0.0f, 0.4f);
    } else if (!M.half && M.bar_t >= (float)k->bar * 0.5f) {
        M.half = 1;
        int root = k->bass[M.bar % k->nb] + 7;
        voice(W_TRI, mfreq(shape(k, root) + trans), 0, (float)k->bar * unit_sec * 0.45f, 0.18f, 0.0f, 0.6f);
    }
#endif
#ifdef SND_TEST_MELODY
    return;                                                     /* the tests listen to the tune alone */
#endif
    if (k->shaker) {                                            /* sleigh bells on the off-beats */
        int u = (int)M.upos;
        if (u != M.ubase) { M.ubase = u; if (u & 1) voice(W_NOISE, 0, 0, 0.05f, 0.07f, 0.0f, (u & 2) ? 0.35f : 0.65f); }
    }
}

/* ---- the mixer ---- */
static float noise(void) { g_nz ^= g_nz << 13; g_nz ^= g_nz >> 17; g_nz ^= g_nz << 5; return (float)(g_nz >> 8) * (1.0f / 8388608.0f) - 1.0f; }

static float osc(Voice *v, float f) {
    switch (v->wave) {
    case W_SIN:   return m_sin(v->ph * TAU_F);
    case W_TRI:   return 4.0f * m_abs(v->ph - 0.5f) - 1.0f;
    case W_SAW:   return 2.0f * v->ph - 1.0f;
    case W_SQR:   return v->ph < 0.5f ? 1.0f : -1.0f;
    case W_BELL: {                                              /* a bell: a fundamental and inharmonic partials that die sooner */
        float k = v->t / v->dur;
        v->ph2 += f * 2.76f * (1.0f / SND_RATE); v->ph2 -= m_floor(v->ph2);
        v->ph3 += f * 5.40f * (1.0f / SND_RATE); v->ph3 -= m_floor(v->ph3);
        return m_sin(v->ph * TAU_F) + 0.50f * m_sin(v->ph2 * TAU_F) * (1.0f - k) + 0.22f * m_sin(v->ph3 * TAU_F) * (1.0f - k) * (1.0f - k);
    }
    case W_BRASS: {                                             /* a trombone: a sawtooth through a filter that opens on the attack and closes as it fades */
        float k = v->t / v->dur, x = 2.0f * v->ph - 1.0f, al = 0.08f + 0.30f * (1.0f - k);
        v->ph2 += (x - v->ph2) * al;
        return v->ph2 * 2.3f;
    }
    case W_CYMBAL: {                                            /* a cymbal: noise with the lows taken out */
        float n = noise();
        v->ph2 += (n - v->ph2) * 0.30f;
        return (n - v->ph2) * 1.5f;
    }
    case W_KICK: {                                              /* a kick: a sine that falls fast, and a click at the start */
        float k = v->t / v->dur;
        return m_sin(v->ph * TAU_F) + (k < 0.012f ? noise() * 0.5f : 0.0f);
    }
    default:      return noise();
    }
}

int snd_idle(void) {
    for (int i = 0; i < VOICES; i++) if (V[i].on) return 0;
    return 1;
}

#define DLEN 12800
static float dlL[DLEN], dlR[DLEN];
static int dpos;

void snd_render(int16_t *out, int frames) {
    const Track *k = M.id ? TRACKS[M.id] : 0;
    float unit_sec = k ? 60.0f / ((float)k->upm * M.tempo) : 0.0f, du = k ? 1.0f / (unit_sec * SND_RATE) : 0.0f;
    for (int n = 0; n < frames; n++) {
        if (k) music_step(k, du, unit_sec);
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < VOICES; i++) {
            Voice *v = &V[i];
            if (!v->on) continue;
            if (v->t < 0.0f) { v->t += 1.0f / SND_RATE; continue; }
            float kk = v->t / v->dur;
            if (kk >= 1.0f) { v->on = 0; continue; }
            float f = v->f0 * (v->f1 > 0.0f ? 1.0f + (v->f1 / v->f0 - 1.0f) * kk : 1.0f);   /* a glide, linear in k */
            if (v->wave == W_KICK) { float p2 = (1.0f - kk) * (1.0f - kk), p4 = p2 * p2; f = v->f1 + (v->f0 - v->f1) * p4 * p4 * p2; }   /* the pitch drops in the first moments */
            if (v->wave == W_BRASS) f *= 1.0f + 0.011f * m_sin(v->t * 36.0f);                                                          /* a trombonist's vibrato */
            if (f < 20.0f && v->wave < W_NOISE) f = 20.0f;
            float env = (v->t < 0.006f ? v->t * 166.0f : 1.0f) * (1.0f - kk) * (1.0f - kk);
            if (v->wave == W_BRASS) env = (v->t < 0.07f ? v->t * 14.0f : 1.0f) * (kk < 0.80f ? 1.0f : (1.0f - kk) * 5.0f);              /* held, not plucked */
            float s = osc(v, f) * env * v->vol;
            l += s * (1.0f - v->pan); r += s * v->pan;
            v->ph += f * (1.0f / SND_RATE); v->ph -= m_floor(v->ph);
            v->t += 1.0f / SND_RATE;
        }
        l *= g_vol * 0.5f * g_fade; r *= g_vol * 0.5f * g_fade;
        if (g_theme.on) {                                       /* a ping-pong delay: our own touch, and the room a carol wants */
            float wl = dlR[dpos], wr = dlL[dpos];
            dlL[dpos] = l + wl * 0.38f; dlR[dpos] = r + wr * 0.38f;
            if (++dpos >= DLEN) dpos = 0;
            l += wl * 0.30f; r += wr * 0.30f;
        }
        l = l / (1.0f + 0.55f * m_abs(l)); r = r / (1.0f + 0.55f * m_abs(r));   /* a soft limiter: a loud moment is rounded, not clipped */
        l = clampf(l * 1.3f, -1.0f, 1.0f); r = clampf(r * 1.3f, -1.0f, 1.0f);
        out[n * 2] = (int16_t)(l * 32000.0f); out[n * 2 + 1] = (int16_t)(r * 32000.0f);
    }
}
