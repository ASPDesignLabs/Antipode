#include "snd.h"
#include "core.h"

#define VOICES 12
typedef struct { int on, wave; float ph, f0, f1, t, dur, vol, pan; } Voice;
static Voice V[VOICES];
static float g_vol = 0.7f;
static uint32_t g_rng = 12345u;

void snd_init(void) { memset(V, 0, sizeof V); }
void snd_set_volume(float v) { g_vol = clampf(v, 0.0f, 1.0f); }

enum { W_SIN, W_TRI, W_SAW, W_SQR };
static void voice(int wave, float f0, float f1, float dur, float vol, float delay_unused) {
    (void)delay_unused;
    int slot = 0;
    for (int i = 0; i < VOICES; i++) { if (!V[i].on) { slot = i; break; } if (V[i].t > V[slot].t) slot = i; }
    V[slot] = (Voice){ 1, wave, 0.0f, f0, f1, 0.0f, dur, vol, 0.5f };
}

void snd_sfx(int id) { snd_sfx_note(id, 0); }

void snd_sfx_note(int id, int note) {
    static const float PENT[5] = { 0, 2, 4, 7, 9 };
    switch (id) {
    case SFX_START:
        voice(W_TRI, 330.0f, 660.0f, 0.18f, 0.5f, 0);
        break;
    case SFX_PICKUP: {
        static const float R12[12] = { 1.0f, 1.0595f, 1.1225f, 1.1892f, 1.2599f, 1.3348f, 1.4142f, 1.4983f, 1.5874f, 1.6818f, 1.7818f, 1.8877f };
        int semi = (int)PENT[note % 5] + 12 * ((note / 5) % 2);
        float f = 523.25f * R12[semi % 12] * (semi >= 12 ? 2.0f : 1.0f);
        voice(W_SIN, f, f, 0.16f, 0.6f, 0);
        voice(W_TRI, f * 2.0f, f * 2.0f, 0.10f, 0.2f, 0);
        break; }
    case SFX_DIE:                                   /* a tritone - the antipode of the octave - falling away */
        voice(W_SAW, 440.0f, 60.0f, 0.6f, 0.4f, 0);
        voice(W_SQR, 622.25f, 80.0f, 0.6f, 0.22f, 0);
        break;
    }
}

static float osc(int wave, float p) {
    switch (wave) {
    case W_SIN: return m_sin(p * TAU_F);
    case W_TRI: return 4.0f * m_abs(p - 0.5f) - 1.0f;
    case W_SAW: return 2.0f * p - 1.0f;
    default:    return p < 0.5f ? 1.0f : -1.0f;
    }
}

void snd_render(int16_t *out, int frames) {
    for (int n = 0; n < frames; n++) {
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < VOICES; i++) {
            Voice *v = &V[i];
            if (!v->on) continue;
            float k = v->t / v->dur;
            if (k >= 1.0f) { v->on = 0; continue; }
            float f = v->f0 * (v->f1 > 0.0f ? 1.0f + (v->f1 / v->f0 - 1.0f) * k : 1.0f);   /* a glide, linear in k */
            if (f < 20.0f) f = 20.0f;
            float env = (v->t < 0.01f ? v->t * 100.0f : 1.0f) * (1.0f - k) * (1.0f - k);
            float s = osc(v->wave, v->ph) * env * v->vol;
            l += s * (1.0f - v->pan); r += s * v->pan;
            v->ph += f * (1.0f / SND_RATE); v->ph -= m_floor(v->ph);
            v->t += 1.0f / SND_RATE;
        }
        l *= g_vol * 0.5f; r *= g_vol * 0.5f;
        l = clampf(l, -1.0f, 1.0f); r = clampf(r, -1.0f, 1.0f);
        out[n * 2] = (int16_t)(l * 32000.0f); out[n * 2 + 1] = (int16_t)(r * 32000.0f);
    }
    (void)g_rng;
}
