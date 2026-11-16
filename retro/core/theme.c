#include "core.h"

Theme g_theme = { 1, 0 };

/* The ring is laid out as the painter's wheel, six steps round: red, orange, yellow, green, blue, violet. Opposite steps are
 * opposite colours, so red faces green, orange faces blue, yellow faces violet. These are the hues (degrees) each is drawn as. */
static const float ANCH[7] = { 0.0f, 28.0f, 52.0f, 128.0f, 215.0f, 285.0f, 360.0f };

float theme_wheel(float h) {
    h -= m_floor(h);
    float k = h * 6.0f;
    int i = (int)k;
    if (i > 5) i = 5;
    float t = k - (float)i;
    return (ANCH[i] + (ANCH[i + 1] - ANCH[i]) * t) * (1.0f / 360.0f);
}

/* Always draws a number, so a scene's random stream is the same in either season. */
float theme_base_hue(uint32_t *rng) {
    float r = rng_f(rng);
    if (!g_theme.on) return r;
    if (g_theme.pair == 0) return r < 0.5f ? 0.0f : 0.5f;             /* red or green */
    return r < 0.5f ? GOLD_RING : GOLD_RING + 0.5f;                   /* gold or blue */
}

const char *theme_pair_name(int pair) { return pair ? "GOLD/BLUE" : "RED/GREEN"; }
