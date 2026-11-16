/* Antipode - Afterimage, on the 256-colour engine.
 *
 * You glide through the dark leaving a trail in your hue. The trail drifts round the colour wheel and ripens into
 * your complement; ripe trail, and the border, kill. Collect sparks.
 *
 * Every pixel of the picture is a palette index, and the palette does the thinking:
 *
 *   index = BASE + slot * LEVELS + level
 *
 * `slot` is the tick a pixel was stamped on, mod SLOTS (a ring of birth times), `level` its brightness step
 * (halo .. core, so a glow is index arithmetic). Each frame the palette is rewritten so that slot's colour is a
 * function of its age: hue = yours + half a turn * age/NC. Nothing touches the pixels as they ripen; the whole trail
 * recolours itself for 240 palette entries of work. Danger is a 256-entry table asked of the pixel under the head
 * (CPU collision, no read-back), and a once-a-tick sweep gives expired slots back to the background.
 *
 * 0..15 are reserved: an 8-step background ramp, the border, the spark, the head, the death ring and the text. */
#include "core.h"
#include "gfx.h"
#include "snd.h"

#define W 320
#define H 200

#define SLOTS 60
#define LEVELS 4
#define BASE 16                     /* BASE + SLOTS * LEVELS = 256 */
#define NC 36                       /* a stamp reaches the complement at this age (ticks): 36 / 11.25 = 3.2 s */
#define LETHAL_FROM 32              /* ...and is lethal from here, until it expires */
#define EXPIRE0 45
#define EXPIRE_MAX 58               /* must stay under SLOTS: a slot is cleared before the ring comes round to it */
#define TICK_HZ 11.25f
#define RADIUS 3.5f                 /* the head, in pixels */
#define BORDER 4                    /* the lethal border, in pixels */
#define TURN 3.4f                   /* rad/s */
#define SPEED0 55.0f                /* px/s, a little quicker with every spark */

enum { I_BG = 0, I_BORDER0 = 8, I_BORDER1 = 9, I_PICK = 10, I_PICKDOT = 11, I_HALO = 12, I_CORE = 13, I_RING = 14, I_TEXT = 15 };
enum { ST_MENU, ST_PLAY, ST_DEAD };

static struct {
    uint32_t rng;
    float hue;
    int score, best, state;
    int tick;
    float tick_acc;                 /* 0..1 of the way to the next tick: the palette ages smoothly between ticks */
    float x, y, ang;
    float pick_x, pick_y;
    double clock, dead_t;
} S;

static uint8_t field[W * H];        /* the picture as the game keeps it: background, border and trail */
static uint8_t bgimg[W * H];        /* what a pixel goes back to when its trail expires */
static uint8_t slot_of[256];
static uint8_t level_of[256];
static uint8_t lethal[256];

static int expire_ticks(void) {
    int e = EXPIRE0 + (int)(S.score * 1.08f);
    return e > EXPIRE_MAX ? EXPIRE_MAX : e;
}
static int age_of(int slot) { return ((S.tick - slot) % SLOTS + SLOTS) % SLOTS; }

/* ---- colour ----------------------------------------------------------- */
static void hsv(float h, float s, float v, int *r, int *g, int *b) {
    h -= m_floor(h);
    float p = h * 6.0f;
    float fr = clampf(m_abs(p - 3.0f) - 1.0f, 0.0f, 1.0f);
    float fg = clampf(2.0f - m_abs(p - 2.0f), 0.0f, 1.0f);
    float fb = clampf(2.0f - m_abs(p - 4.0f), 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);
    *r = (int)((v * (1.0f + s * (fr - 1.0f))) * 63.0f + 0.5f);
    *g = (int)((v * (1.0f + s * (fg - 1.0f))) * 63.0f + 0.5f);
    *b = (int)((v * (1.0f + s * (fb - 1.0f))) * 63.0f + 0.5f);
}
static void setpal(Frame *f, int i, float h, float s, float v) {
    int r, g, b;
    hsv(h, s, v, &r, &g, &b);
    gfx_pal(f, i, r, g, b);
}

static void build_pal(Frame *f) {
    static const float LV[LEVELS] = { 0.30f, 0.50f, 0.78f, 1.0f };
    float flip = S.state == ST_DEAD ? 0.5f * smoothstepf(0.0f, 0.5f, (float)S.dead_t) : 0.0f;   /* death: the world turns to its antipode */
    float h0 = S.hue + flip, t = (float)S.clock;
    for (int i = 0; i < 8; i++) setpal(f, I_BG + i, h0, 0.55f, 0.012f + 0.085f * (float)i / 7.0f);
    float pulse = 0.82f + 0.18f * m_sin(t * 3.0f);
    setpal(f, I_BORDER0, h0 + 0.5f, 0.9f, pulse);
    setpal(f, I_BORDER1, h0 + 0.5f, 0.9f, pulse * 0.5f);
    setpal(f, I_PICK, h0, 0.6f, 0.85f + 0.15f * m_sin(t * 5.0f));
    setpal(f, I_PICKDOT, h0, 0.4f, 1.0f);
    setpal(f, I_HALO, h0, 0.75f, 0.35f);
    setpal(f, I_CORE, h0, 0.25f, 1.0f);
    setpal(f, I_RING, S.hue + 0.5f, 0.7f, 1.0f);
    setpal(f, I_TEXT, h0, 0.3f, S.state == ST_PLAY ? 0.8f : 0.7f + 0.3f * m_sin(t * 2.5f));
    int ex = expire_ticks();
    for (int s = 0; s < SLOTS; s++) {
        float age = (float)age_of(s) + S.tick_acc;
        float c = clampf(age / NC, 0.0f, 1.0f);
        float hue = S.hue + 0.5f * c + flip;
        float fade = 1.0f - smoothstepf(LETHAL_FROM + 4.0f, (float)ex, age);
        float ripe = age >= LETHAL_FROM ? 1.0f + 0.22f * m_sin(t * 22.0f + (float)s * 0.9f) : 1.0f;   /* ripe trail shimmers */
        for (int l = 0; l < LEVELS; l++) {          /* as it expires a stamp dissolves into the background's tint, not into black */
            int r, g, b, br, bgc, bb;
            hsv(hue, 0.82f, LV[l] * (1.0f - 0.15f * c) * ripe, &r, &g, &b);
            hsv(h0, 0.55f, 0.012f + 0.085f * 0.5f, &br, &bgc, &bb);
            gfx_pal(f, BASE + s * LEVELS + l, (int)lerpf((float)br, (float)r, fade + 0.0f), (int)lerpf((float)bgc, (float)g, fade), (int)lerpf((float)bb, (float)b, fade));
        }
    }
}

/* ---- the field -------------------------------------------------------- */
static void build_bg(void) {
    static const uint8_t BAY[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = ((float)x - 159.5f) / 160.0f, dy = ((float)y - 99.5f) / 100.0f;
            float t = 1.0f - clampf((dx * dx + dy * dy) * 0.55f, 0.0f, 1.0f);
            int lvl = clampi((int)(t * 7.0f + (float)BAY[(y & 3) * 4 + (x & 3)] * (1.0f / 16.0f)), 0, 7);
            int e = x < W - 1 - x ? x : W - 1 - x;
            int ey = y < H - 1 - y ? y : H - 1 - y;
            if (ey < e) e = ey;
            bgimg[y * W + x] = (uint8_t)(e < BORDER ? (e < 2 ? I_BORDER0 : I_BORDER1) : I_BG + lvl);
        }
    memcpy(field, bgimg, sizeof field);
}

static void stamp_disc(int cx, int cy, float r, int level, int empty_only) {
    int idx = BASE + (S.tick % SLOTS) * LEVELS + level, ri = (int)r + 1;
    float r2 = r * r;
    for (int dy = -ri; dy <= ri; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)H) continue;
        for (int dx = -ri; dx <= ri; dx++) {
            int x = cx + dx;
            if ((unsigned)x >= (unsigned)W || (float)(dx * dx + dy * dy) > r2) continue;
            if (empty_only && field[y * W + x] >= BASE) continue;
            field[y * W + x] = (uint8_t)idx;
        }
    }
}
static void stamp_head(float x, float y) {                  /* a glow built from brightness steps: halo under, core over */
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    stamp_disc(cx, cy, 5.5f, 0, 1);
    stamp_disc(cx, cy, 4.5f, 1, 1);
    stamp_disc(cx, cy, RADIUS, 2, 0);
    stamp_disc(cx, cy, 2.2f, 3, 0);
}

static int head_in_danger(float x, float y) {               /* the pixels under the head, asked of the table */
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy > 5) continue;
            int px = cx + dx, py = cy + dy;
            if ((unsigned)px < (unsigned)W && (unsigned)py < (unsigned)H && lethal[field[py * W + px]]) return 1;
        }
    return 0;
}

static void build_lethal(void) {
    int ex = expire_ticks();
    memset(lethal, 0, sizeof lethal);
    for (int i = BASE; i < 256; i++) {
        int age = age_of(slot_of[i]);
        lethal[i] = (uint8_t)(level_of[i] >= 2 && age >= LETHAL_FROM && age < ex);
    }
}

static void sweep(void) {                                   /* the slot that just expired goes back to the background */
    int s = (((S.tick - expire_ticks()) % SLOTS) + SLOTS) % SLOTS;
    for (int i = 0; i < W * H; i++) {
        int v = field[i];
        if (v >= BASE && slot_of[v] == s) field[i] = bgimg[i];
    }
}

static void advance_ticks(double dt) {
    S.tick_acc += (float)dt * TICK_HZ;
    while (S.tick_acc >= 1.0f) { S.tick_acc -= 1.0f; S.tick++; sweep(); }
}

/* ---- the game --------------------------------------------------------- */
static void spawn_pick(void) {
    for (int i = 0; i < 24; i++) {
        float x = 18.0f + rng_f(&S.rng) * (W - 36), y = 18.0f + rng_f(&S.rng) * (H - 36);
        float dx = x - S.x, dy = y - S.y;
        if (dx * dx + dy * dy > 50.0f * 50.0f) { S.pick_x = x; S.pick_y = y; return; }
    }
    S.pick_x = W - S.x; S.pick_y = H - S.y;
}

static void load_best(void) {
    uint8_t b[2];
    S.best = slot_read(b, 2) == 2 ? b[0] | (b[1] << 8) : 0;
}

static void begin(void) {
    S.hue = rng_f(&S.rng);
    S.score = 0; S.state = ST_PLAY;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    memcpy(field, bgimg, sizeof field);
    spawn_pick();
    snd_sfx(SFX_START);
}

static void enter(uint32_t seed) {
    for (int i = BASE; i < 256; i++) { slot_of[i] = (uint8_t)((i - BASE) / LEVELS); level_of[i] = (uint8_t)((i - BASE) % LEVELS); }
    S.rng = seed ? seed : 1u;
    S.hue = rng_f(&S.rng);
    S.state = ST_MENU; S.score = 0; S.tick = 0; S.tick_acc = 0.0f; S.clock = 0.0; S.dead_t = 0.0;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    load_best();
    build_bg();
    spawn_pick();
}

static void die(void) {
    S.state = ST_DEAD; S.dead_t = 0.0;
    snd_sfx(SFX_DIE);
    if (S.score > S.best) {
        uint8_t b[2] = { (uint8_t)(S.score & 255), (uint8_t)((S.score >> 8) & 255) };
        S.best = S.score;
        slot_write(b, 2);
    }
}

/* One steering vector from every device: keys and sticks give a direction, a pointer a place to head for. */
static int steer(const Input *in, float *dx, float *dy, float *mag) {
    float sx = 0.0f, sy = 0.0f;
    if (in->down[KEY_LEFT] || in->down[KEY_A]) sx -= 1.0f;
    if (in->down[KEY_RIGHT] || in->down[KEY_D]) sx += 1.0f;
    if (in->down[KEY_UP] || in->down[KEY_W]) sy -= 1.0f;
    if (in->down[KEY_DOWN] || in->down[KEY_S]) sy += 1.0f;
    if (in->pad) {
        float lx = in->lx, ly = -in->ly;
        if (in->pad_down[PAD_LEFT]) lx -= 1.0f;
        if (in->pad_down[PAD_RIGHT]) lx += 1.0f;
        if (in->pad_down[PAD_UP]) ly -= 1.0f;
        if (in->pad_down[PAD_DOWN]) ly += 1.0f;
        if (lx != 0.0f || ly != 0.0f) { sx = lx; sy = ly; }
    }
    float m = m_sqrt(sx * sx + sy * sy);
    if (m > 0.01f) { *dx = sx / m; *dy = sy / m; *mag = m > 1.0f ? 1.0f : m; return 1; }
    if (in->ptr_on) {
        float px = in->ptr_x - S.x, py = in->ptr_y - S.y, d = m_sqrt(px * px + py * py);
        if (d > 3.0f) { *dx = px / d; *dy = py / d; *mag = clampf(d / 15.0f, 0.0f, 1.0f); return 1; }
    }
    return 0;
}

static float wrap_angle(float a) { return a - TAU_F * m_floor((a + PI_F) * (1.0f / TAU_F)); }

static void update(double dt, const Input *in) {
    S.clock += dt;
    advance_ticks(dt);
    if (S.state == ST_MENU) { if (in->act) begin(); return; }
    if (S.state == ST_DEAD) {
        S.dead_t += dt;
        if (in->act && S.dead_t > 0.6) begin();
        return;
    }
    float dx, dy, mag;
    if (steer(in, &dx, &dy, &mag)) {
        float d = wrap_angle(m_atan2(dy, dx) - S.ang);
        float m = TURN * (float)dt * (0.35f + 0.65f * mag);
        S.ang += clampf(d, -m, m);
    }
    float speed = SPEED0 + (S.score > 20 ? 20 : S.score) * 1.2f;
    float dist = speed * (float)dt;
    int steps = (int)(dist / 1.5f) + 1;
    float sx = m_cos(S.ang) * dist / (float)steps, sy = m_sin(S.ang) * dist / (float)steps;
    build_lethal();
    for (int i = 0; i < steps; i++) {
        S.x += sx; S.y += sy;
        float e = S.x < W - 1 - S.x ? S.x : W - 1 - S.x, ey = S.y < H - 1 - S.y ? S.y : H - 1 - S.y;
        if (ey < e) e = ey;
        if (e < BORDER + RADIUS * 0.3f || head_in_danger(S.x, S.y)) { die(); return; }   /* the border and ripe trail are the complement */
        float px = S.x - S.pick_x, py = S.y - S.pick_y;
        if (px * px + py * py < (RADIUS + 4.0f) * (RADIUS + 4.0f)) {
            S.score++;
            snd_sfx_note(SFX_PICKUP, S.score);
            spawn_pick();
        }
        stamp_head(S.x, S.y);
    }
}

/* ---- drawing over the field ------------------------------------------ */
static void ring(Frame *f, int cx, int cy, int r, uint8_t c) {   /* midpoint circle */
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_px(f, cx + x, cy + y, c); gfx_px(f, cx - x, cy + y, c); gfx_px(f, cx + x, cy - y, c); gfx_px(f, cx - x, cy - y, c);
        gfx_px(f, cx + y, cy + x, c); gfx_px(f, cx - y, cy + x, c); gfx_px(f, cx + y, cy - x, c); gfx_px(f, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

static void itoa_(int v, char *o) {
    char t[12]; int n = 0;
    if (v <= 0) { o[0] = '0'; o[1] = 0; return; }
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
}
static void centred8(Frame *f, int y, const char *s) { gfx_text8(f, (W - gfx_text_width(s, 1)) / 2, y, s, I_TEXT, 1); }
static void centred16(Frame *f, int y, const char *s, int sc) { gfx_text16(f, (W - gfx_text_width(s, sc)) / 2, y, s, I_TEXT, sc); }

static void render(Frame *f) {
    memcpy(f->pix, field, sizeof field);
    build_pal(f);
    char num[16], line[32];
    int px = (int)(S.pick_x + 0.5f), py = (int)(S.pick_y + 0.5f);
    ring(f, px, py, 4 + (int)(1.5f + 1.5f * m_sin((float)S.clock * 5.0f)), I_PICK);
    gfx_disc(f, px, py, 1, I_PICKDOT);
    int hx = (int)(S.x + 0.5f), hy = (int)(S.y + 0.5f);
    if (S.state == ST_MENU) { gfx_disc(f, hx, hy, 6, I_HALO); gfx_disc(f, hx, hy, 3 + (m_sin((float)S.clock * 2.0f) > 0.0f), I_CORE); }
    else if (S.state == ST_PLAY) gfx_disc(f, hx, hy, 2, I_CORE);
    else {                                                   /* death: a ring runs out from where you were, the world flipping behind it */
        int r = (int)(S.dead_t * 130.0);
        ring(f, hx, hy, r, I_RING); ring(f, hx, hy, r + 1, I_RING);
    }
    if (S.state == ST_MENU) {
        centred16(f, 14, "ANTIPODE", 2);
        centred8(f, 52, "A F T E R I M A G E");
        centred8(f, 128, "YOUR TRAIL RIPENS INTO");
        centred8(f, 140, "THE COLOUR YOU CANNOT TOUCH");
        centred8(f, 160, "WASD / STICK / MOUSE / TOUCH");
        centred8(f, 174, "PRESS ANY KEY");
    } else if (S.state == ST_PLAY) {
        itoa_(S.score, num);
        centred16(f, 178, num, 1);
    } else {
        itoa_(S.score, num);
        centred8(f, 70, "SCORE");
        centred16(f, 82, num, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        itoa_(S.best, line + 5);
        centred8(f, 122, line);
        if (S.dead_t > 0.6) centred8(f, 150, "PRESS TO RETRY");
    }
}

const Scene scene_afterimage = { "AFTERIMAGE", W, H, enter, update, render };

/* for the test harness only: where things are, so a scripted bot can play */
void afterimage_debug(int *score, int *state, float *pick_x, float *pick_y, float *x, float *y) {
    *score = S.score; *state = S.state; *pick_x = S.pick_x; *pick_y = S.pick_y; *x = S.x; *y = S.y;
}
