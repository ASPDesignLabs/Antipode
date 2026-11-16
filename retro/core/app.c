#include "app.h"
#include "snd.h"

#define FADE_SECONDS 0.8

static struct {
    AppConfig cfg;
    double t, fade_t;
    uint32_t seed;
    Frame frame;
    Input *in;
} A;

static struct { uint8_t data[16]; int len, dirty; } SLOT;

void app_default_config(AppConfig *c) {
    memset(c, 0, sizeof *c);
    c->sound = 1;
    c->volume = 70;
    c->brightness = 100;
    c->start_fade = 1;
}

void app_init(const AppConfig *cfg) {
    A.cfg = *cfg;
    A.t = 0;
    A.fade_t = cfg->start_fade ? 0.0 : FADE_SECONDS;
    A.seed = 0xA17190DEu;
    snd_init();
    snd_set_volume(cfg->sound ? cfg->volume / 100.0f : 0.0f);
    A.frame.w = scene_afterimage.w; A.frame.h = scene_afterimage.h;
    A.frame.fade = 1.0f;
    memset(A.frame.pix, 0, sizeof A.frame.pix);
    memset(A.frame.pal, 0, sizeof A.frame.pal);
    scene_afterimage.enter(A.seed);
}

void app_set_input(Input *in) { A.in = in; }

static void clear_edges(Input *in) {
    memset(in->hit, 0, sizeof in->hit);
    memset(in->pad_hit, 0, sizeof in->pad_hit);
    in->ptr_hit = 0;
    in->act = 0;
}

void app_update(double dt) {
    static const Input none;
    if (dt > 0.25) dt = 0.25;
    A.t += dt;
    if (A.fade_t < FADE_SECONDS) A.fade_t += dt;
    Input *in = A.in;
    if (in) {                                       /* any key, button or press is an "act": start, retry */
        int act = in->ptr_hit;
        for (int k = 0; k < KEY_COUNT; k++) act |= in->hit[k];
        for (int b = 0; b < PAD_COUNT; b++) act |= in->pad_hit[b];
        in->act = act != 0;
    }
    scene_afterimage.update(dt, in ? in : &none);
    if (in) clear_edges(in);
}

int app_mode(int *w, int *h) { *w = A.frame.w; *h = A.frame.h; return 0; }
const Frame *app_frame(void) { return &A.frame; }

static float fade_k(void) { return smoothstepf(0.0f, (float)FADE_SECONDS, (float)A.fade_t); }

/* the palette, 6 bits a channel, to 256 packed pixels: palette animation costs these 256 entries, not the picture */
static void build_lut(uint32_t lut[256], int bgr) {
    float k = A.frame.fade * fade_k() * (A.cfg.brightness / 100.0f);
    for (int i = 0; i < 256; i++) {
        int c[3];
        for (int j = 0; j < 3; j++) {
            int v6 = A.frame.pal[i][j] & 63;
            int v8 = (v6 << 2) | (v6 >> 4);          /* VGA DAC 6 -> 8 bits */
            c[j] = clampi((int)(v8 * k + 0.5f), 0, 255);
        }
        lut[i] = bgr ? (uint32_t)((c[0] << 16) | (c[1] << 8) | c[2])
                     : (uint32_t)(c[0] | (c[1] << 8) | (c[2] << 16) | (0xFFu << 24));
    }
}

void app_present(uint32_t *dst, int bgr) {
    uint32_t lut[256];
    scene_afterimage.render(&A.frame);
    build_lut(lut, bgr);
    int n = A.frame.w * A.frame.h;
    const uint8_t *src = A.frame.pix;
    for (int i = 0; i < n; i++) dst[i] = lut[src[i]];
}

void app_present_rgb(uint8_t *rgb, int *w, int *h) {
    static uint32_t buf[FB_MAX_W * FB_MAX_H];
    app_present(buf, 0);
    *w = A.frame.w; *h = A.frame.h;
    for (int i = 0; i < A.frame.w * A.frame.h; i++) {
        rgb[i * 3 + 0] = (uint8_t)(buf[i] & 255);
        rgb[i * 3 + 1] = (uint8_t)((buf[i] >> 8) & 255);
        rgb[i * 3 + 2] = (uint8_t)((buf[i] >> 16) & 255);
    }
}

int slot_read(uint8_t *buf, int cap) {
    if (SLOT.len <= 0 || SLOT.len > cap) return 0;
    memcpy(buf, SLOT.data, (size_t)SLOT.len);
    return SLOT.len;
}
void slot_write(const uint8_t *buf, int len) {
    if (!buf || len <= 0 || len > (int)sizeof SLOT.data) return;
    memcpy(SLOT.data, buf, (size_t)len); SLOT.len = len; SLOT.dirty = 1;
}
void app_slot_load(const uint8_t *buf, int len) {
    if (!buf || len <= 0 || len > (int)sizeof SLOT.data) return;
    memcpy(SLOT.data, buf, (size_t)len); SLOT.len = len; SLOT.dirty = 0;
}
int app_slot_take(uint8_t *buf, int cap) {
    if (!SLOT.dirty) return 0;
    SLOT.dirty = 0;
    return slot_read(buf, cap);
}
