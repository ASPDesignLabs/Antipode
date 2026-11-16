#include "app.h"
#include "snd.h"
#include "gfx.h"

#define FADE_SECONDS 0.8
#define TRANS_SECONDS 0.3           /* each half of a switch: out to black, then in */
#define ZONE 64                     /* the left and right strips of the picture, in pixels: a tap there switches scenes */
#define SWIPE 36                    /* a drag this far sideways is a swipe */

static const Scene *const SCENES[SCENE_COUNT] = { &scene_afterimage, &scene_prism, &scene_tidepool, &scene_moth };
static const char *const XMAS_TITLE[SCENE_COUNT] = { "SPARKLER", "CAROLING RUN", "SLEIGH BELLS", "LANTERN" };

#define NFLAKE 72

static struct {
    AppConfig cfg;
    double t, fade_t;
    uint32_t seed;
    Frame frame;
    Input *in;
    int cur;
    int tr, tr_to; double tr_t;                     /* a switch: 0 none, 1 fading out, 2 fading in */
    int idle;                                       /* the scene is on a title or game-over screen: left/right switch */
    float hint;                                     /* seconds the switching hint stays up (it dissolves out) */
    struct { int down, swiped; float x0, y0; double t; } P;   /* a press that might be a tap or a swipe */
    float last_x, last_y;
    int pad_dir;
    struct { float x, y, vy, ph; } flake[NFLAKE];     /* snow, on the screens that wait for you */
} A;

static struct { uint8_t data[16]; int len, dirty; } SLOT;

void app_default_config(AppConfig *c) {
    memset(c, 0, sizeof *c);
    c->sound = 1;
    c->volume = 70;
    c->brightness = 100;
    c->start_fade = 1;
}

static void enter_scene(int idx) {
    const Scene *sc = SCENES[idx];
    A.cur = idx;
    A.frame.w = sc->w; A.frame.h = sc->h;
    A.frame.fade = 1.0f;
    memset(A.frame.pix, 0, sizeof A.frame.pix);
    memset(A.frame.pal, 0, sizeof A.frame.pal);
    A.seed = A.seed * 1664525u + 1013904223u;
    sc->enter(A.seed);
    A.hint = 2.5f;
    A.P.down = 0;
}

void app_set_theme(int on, int pair) { g_theme.on = on != 0; if (pair >= 0) g_theme.pair = pair != 0; }   /* pair < 0: keep what was saved */

void app_init(const AppConfig *cfg) {
    A.cfg = *cfg;
    for (int i = 0; i < NFLAKE; i++) {                           /* a fixed sprinkle, so the snow is the same every time */
        A.flake[i].x = (float)((i * 97 + 13) % 320); A.flake[i].y = (float)((i * 61 + 7) % 200);
        A.flake[i].vy = 9.0f + (float)((i * 29) % 17); A.flake[i].ph = (float)i * 0.7f;
    }
    A.t = 0;
    A.fade_t = cfg->start_fade ? 0.0 : FADE_SECONDS;
    A.seed = 0xA17190DEu;
    snd_init();
    snd_set_volume(cfg->sound ? cfg->volume / 100.0f : 0.0f);
    enter_scene(0);
}
void app_select(int scene) { if (scene >= 0 && scene < SCENE_COUNT) enter_scene(scene); }
int app_scene(void) { return A.cur; }
const char *app_scene_name(int scene) { return scene >= 0 && scene < SCENE_COUNT ? SCENES[scene]->name : ""; }
const char *app_scene_title(int scene) { return scene >= 0 && scene < SCENE_COUNT ? (g_theme.on ? XMAS_TITLE[scene] : SCENES[scene]->name) : ""; }
void app_set_input(Input *in) { A.in = in; }

static void clear_edges(Input *in) {
    memset(in->hit, 0, sizeof in->hit);
    memset(in->pad_hit, 0, sizeof in->pad_hit);
    in->ptr_hit = 0; in->ptr_up = 0; in->act = 0;
}

static void request_switch(int dir) {
    if (A.tr) return;
    A.tr = 1; A.tr_t = 0.0;
    A.tr_to = ((A.cur + dir) % SCENE_COUNT + SCENE_COUNT) % SCENE_COUNT;
}

/* What a person asked for this step, on a screen where left and right choose the scene: the arrow keys, Q and E, the
 * d-pad, the bumpers, the stick pushed over, a swipe, or a tap at either side. Anything else is "go". */
static int g_toggle;
static float fade_k(void);
static void read_switch(const Input *in, double dt, int *cycle, int *act) {
    *cycle = 0; *act = 0;
    for (int k = 0; k < KEY_COUNT; k++) if (in->hit[k] && k != KEY_LEFT && k != KEY_RIGHT && k != KEY_Q && k != KEY_E && k != KEY_T) *act = 1;
    for (int b = 0; b < PAD_COUNT; b++) if (in->pad_hit[b] && b != PAD_LEFT && b != PAD_RIGHT && b != PAD_LB && b != PAD_RB && b != PAD_Y) *act = 1;
    if (g_theme.on && (in->hit[KEY_T] || in->pad_hit[PAD_Y])) g_toggle = 1;
    if (in->hit[KEY_LEFT] || in->hit[KEY_Q] || in->pad_hit[PAD_LEFT] || in->pad_hit[PAD_LB]) *cycle = -1;
    if (in->hit[KEY_RIGHT] || in->hit[KEY_E] || in->pad_hit[PAD_RIGHT] || in->pad_hit[PAD_RB]) *cycle = 1;
    int dir = in->pad ? (in->lx > 0.6f ? 1 : in->lx < -0.6f ? -1 : 0) : 0;      /* the stick, on crossing */
    if (dir && dir != A.pad_dir) *cycle = dir;
    A.pad_dir = dir;
    if (in->ptr_hit) { A.P.down = 1; A.P.swiped = 0; A.P.x0 = in->ptr_x; A.P.y0 = in->ptr_y; A.P.t = 0.0; }
    if (A.P.down) {
        A.P.t += dt;
        float dx = in->ptr_x - A.P.x0, dy = in->ptr_y - A.P.y0;
        if (in->ptr_on && !A.P.swiped && (dx > SWIPE || dx < -SWIPE) && (dx > 0 ? dx : -dx) > 1.5f * (dy > 0 ? dy : -dy)) {
            A.P.swiped = 1;
            *cycle = dx < 0 ? 1 : -1;                                              /* swipe left: the next one */
        }
    }
    if (in->ptr_up) {
        if (A.P.down && !A.P.swiped && A.P.t < 0.5) {
            float x = in->ptr_x, dx = in->ptr_x - A.P.x0, dy = in->ptr_y - A.P.y0;
            if (dx * dx + dy * dy < 14.0f * 14.0f) {                               /* a tap: at the sides it switches, else it goes */
                if (g_theme.on && x > SCENES[A.cur]->w - 104 && in->ptr_y > SCENES[A.cur]->h - 16) g_toggle = 1;   /* the bottom corner: the other pair of colours */
                else if (x < ZONE) *cycle = -1; else if (x > SCENES[A.cur]->w - ZONE) *cycle = 1; else *act = 1;
            }
        }
        A.P.down = 0;
    }
}

void app_update(double dt) {
    static const Input none;
    if (dt > 0.25) dt = 0.25;
    A.t += dt;
    if (A.fade_t < FADE_SECONDS) A.fade_t += dt;
    if (A.hint > 0.0f) A.hint -= (float)dt;

    if (A.tr) {
        A.tr_t += dt;
        if (A.tr == 1 && A.tr_t >= TRANS_SECONDS) { enter_scene(A.tr_to); A.tr = 2; A.tr_t = 0.0; }
        else if (A.tr == 2 && A.tr_t >= TRANS_SECONDS) A.tr = 0;
    }

    const Scene *sc = SCENES[A.cur];
    Input *in = A.in;
    A.idle = A.tr == 0 && sc->idle && sc->idle();
    Input local = in ? *in : none;
    g_toggle = 0;
    if (in) {
        int cycle, act;
        if (A.idle) {
            read_switch(in, dt, &cycle, &act);
            if (in->ptr_on && ((in->ptr_x - A.last_x) > 0.5f || (A.last_x - in->ptr_x) > 0.5f || (in->ptr_y - A.last_y) > 0.5f || (A.last_y - in->ptr_y) > 0.5f)) A.hint = 1.6f;   /* the mouse moved: show the way */
            if (cycle) { request_switch(cycle); A.hint = 1.0f; }
        } else {
            act = 0;
            for (int k = 0; k < KEY_COUNT; k++) act |= in->hit[k];
            for (int b = 0; b < PAD_COUNT; b++) act |= in->pad_hit[b];
            A.P.down = 0;
        }
        A.last_x = in->ptr_x; A.last_y = in->ptr_y;
        local.act = act != 0 && !A.tr;
        if (g_toggle && A.idle) {                                  /* the other pair of colours: remembered, and the scene starts over in it */
            g_theme.pair ^= 1;
            SLOT.data[8] = (uint8_t)(g_theme.pair + 1); SLOT.len = 16; SLOT.dirty = 1;
            enter_scene(A.cur);
            local.act = 0; A.hint = 2.5f;
            if (in) clear_edges(in);
            return;
        }
    }
    if (g_theme.on) {                                              /* snow, and the carol that waits on a title or a game-over screen */
        for (int i = 0; i < NFLAKE; i++) {
            A.flake[i].y += A.flake[i].vy * (float)dt;
            A.flake[i].x += 6.0f * m_sin(A.flake[i].ph + (float)A.t * 0.9f) * (float)dt;
            if (A.flake[i].y > 202.0f) { A.flake[i].y -= 204.0f; }
            if (A.flake[i].x < -2.0f) A.flake[i].x += 324.0f;
            if (A.flake[i].x > 322.0f) A.flake[i].x -= 324.0f;
        }
        if (A.idle && snd_music_id() == MU_NONE && snd_idle()) snd_music_play(MU_SILENT);   /* once the last sound has died away */
    } else snd_music_stop();
    sc->update(dt, &local);
    snd_set_fade(fade_k());
    if (in) clear_edges(in);
}

int app_mode(int *w, int *h) { *w = A.frame.w; *h = A.frame.h; return A.cur; }
const Frame *app_frame(void) { return &A.frame; }

static float fade_k(void) {
    float k = smoothstepf(0.0f, (float)FADE_SECONDS, (float)A.fade_t);
    if (A.tr == 1) k *= 1.0f - smoothstepf(0.0f, TRANS_SECONDS, (float)A.tr_t);   /* the palette is the only thing there is to blend: out to black, in from it */
    else if (A.tr == 2) k *= smoothstepf(0.0f, TRANS_SECONDS, (float)A.tr_t);
    return k;
}

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

/* The switching hint: arrows at the sides and "< n/N >" along the foot, dissolving out by dither (one palette entry can't
 * blend). A pointer inside a side strip makes that arrow bigger: that is where a click goes. */
static void draw_hint(Frame *f) {
    if (!A.idle || A.tr || A.hint <= 0.0f || SCENE_COUNT < 2) return;
    float a = clampf(A.hint / 0.7f, 0.0f, 1.0f);
    int w = f->w, h = f->h;
    char line[16] = "<  1/2  >";
    line[3] = (char)('1' + A.cur); line[5] = (char)('0' + SCENE_COUNT);
    gfx_text_a(f, (w - gfx_text_width(line, 1)) / 2, h - 12, line, I_TEXT_ALL, 0, 1, a);
    int ptr = A.in && A.in->ptr_on;
    int left = ptr && A.in->ptr_x < ZONE, right = ptr && A.in->ptr_x > w - ZONE;
    gfx_text_a(f, left ? 6 : 10, h / 2 - (left ? 16 : 8), "<", I_TEXT_ALL, 1, left ? 2 : 1, a);
    gfx_text_a(f, right ? w - 22 : w - 18, h / 2 - (right ? 16 : 8), ">", I_TEXT_ALL, 1, right ? 2 : 1, a);
    if (g_theme.on) {                                            /* the other pair of colours: a key, a button, or a tap on this */
        const char *t = g_theme.pair ? "[T] RED/GREEN" : "[T] GOLD/BLUE";
        gfx_text_a(f, w - gfx_text_width(t, 1) - 4, h - 12, t, I_TEXT_ALL, 0, 1, a);
    }
}

static void draw_snow(Frame *f) {
    if (!g_theme.on || !(A.idle || A.tr)) return;               /* never in play: a flake must not hide what kills you */
    for (int i = 0; i < NFLAKE; i++) {
        int x = (int)A.flake[i].x, y = (int)A.flake[i].y;
        gfx_px(f, x, y, I_TEXT_ALL);
        if (i % 5 == 0) { gfx_px(f, x + 1, y, I_TEXT_ALL); gfx_px(f, x, y + 1, I_TEXT_ALL); gfx_px(f, x + 1, y + 1, I_TEXT_ALL); }
    }
}

void app_present(uint32_t *dst, int bgr) {
    uint32_t lut[256];
    SCENES[A.cur]->render(&A.frame);
    draw_hint(&A.frame);
    draw_snow(&A.frame);
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

/* best scores: a little-endian u16 a scene */
int best_get(void) { return A.cur < 8 ? SLOT.data[A.cur * 2] | (SLOT.data[A.cur * 2 + 1] << 8) : 0; }
void best_set(int v) {
    if (A.cur >= 8) return;
    SLOT.data[A.cur * 2] = (uint8_t)(v & 255); SLOT.data[A.cur * 2 + 1] = (uint8_t)((v >> 8) & 255);
    if (SLOT.len < 16) SLOT.len = 16;
    SLOT.dirty = 1;
}
void app_slot_load(const uint8_t *buf, int len) {
    if (!buf || len <= 0 || len > (int)sizeof SLOT.data) return;
    memcpy(SLOT.data, buf, (size_t)len); SLOT.len = 16; SLOT.dirty = 0;
    if (len > 8 && SLOT.data[8]) g_theme.pair = SLOT.data[8] == 2;     /* the pair of colours last chosen */
}
int app_slot_take(uint8_t *buf, int cap) {
    if (!SLOT.dirty || cap < 16) return 0;
    SLOT.dirty = 0;
    memcpy(buf, SLOT.data, 16);
    return 16;
}
