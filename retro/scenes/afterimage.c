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
    float bulb_d;                   /* how far since the last bulb on the string */
    double clock, dead_t;
} S;

/* in season: the spark's ornament (g: the cap, o: the glass, h: a glint) */
static const char *const ORN[11] = {
    "...ggg...", "...g.g...", "..ooooo..", ".ooooooo.", "ooohhoooo", "oohooooo.",
    "ooooooooo", "ooooooooo", ".ooooooo.", "..ooooo..", "...ooo..." };
static uint8_t ORNMAP[256];

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
static void build_pal(Frame *f) {
    static const float LV[LEVELS] = { 0.30f, 0.50f, 0.78f, 1.0f };
    float flip = S.state == ST_DEAD ? 0.5f * smoothstepf(0.0f, 0.5f, (float)S.dead_t) : 0.0f;   /* death: the world turns to its antipode */
    float h0 = S.hue + flip, t = (float)S.clock;
    float bgh = g_theme.on ? (g_theme.pair ? 0.5f : 0.667f) + flip : h0;               /* in season the night is a winter blue (or a deep green), not black */
    for (int i = 0; i < 8; i++) gfx_pal_hsv(f, I_BG + i, bgh, g_theme.on ? 0.50f : 0.55f, g_theme.on ? 0.14f + 0.17f * (float)i / 7.0f : 0.012f + 0.085f * (float)i / 7.0f);
    float pulse = 0.82f + 0.18f * m_sin(t * 3.0f);
    gfx_pal_hsv(f, I_BORDER0, h0 + 0.5f, 0.9f, pulse);
    if (g_theme.on) gfx_pal_hsv(f, I_BORDER1, h0 + 0.5f, 0.10f, 0.80f * pulse);            /* the other stripe is white */
    else gfx_pal_hsv(f, I_BORDER1, h0 + 0.5f, 0.9f, pulse * 0.5f);
    gfx_pal_hsv(f, I_PICK, h0, 0.6f, 0.85f + 0.15f * m_sin(t * 5.0f));
    gfx_pal_hsv(f, I_PICKDOT, h0, 0.4f, 1.0f);
    gfx_pal_hsv(f, I_HALO, h0, 0.75f, 0.35f);
    gfx_pal_hsv(f, I_CORE, h0, 0.25f, 1.0f);
    gfx_pal_hsv(f, I_RING, S.hue + 0.5f, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_TEXT, h0, 0.3f, S.state == ST_PLAY ? 0.8f : 0.7f + 0.3f * m_sin(t * 2.5f));
    int ex = expire_ticks();
    for (int s = 0; s < SLOTS; s++) {
        float age = (float)age_of(s) + S.tick_acc;
        float c = clampf(age / NC, 0.0f, 1.0f);
        float hue = S.hue + 0.5f * c + flip;
        float fade = 1.0f - smoothstepf(LETHAL_FROM + 4.0f, (float)ex, age);
        float ripe = age >= LETHAL_FROM ? 1.0f + 0.22f * m_sin(t * 22.0f + (float)s * 0.9f) : 1.0f;   /* ripe trail shimmers */
        for (int l = 0; l < LEVELS; l++) {          /* as it expires a stamp dissolves into the background's tint, not into black */
            int r, g, b, br, bgc, bb;
            gfx_hsv(hue, 0.82f, LV[l] * (1.0f - 0.15f * c) * ripe, &r, &g, &b);
            gfx_hsv(bgh, g_theme.on ? 0.50f : 0.55f, g_theme.on ? 0.14f + 0.17f * 0.5f : 0.012f + 0.085f * 0.5f, &br, &bgc, &bb);
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
            bgimg[y * W + x] = (uint8_t)(e < BORDER ? (g_theme.on ? ((((x + y) >> 2) & 1) ? I_BORDER0 : I_BORDER1) : (e < 2 ? I_BORDER0 : I_BORDER1)) : I_BG + lvl);   /* in season: a candy-cane edge */
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
static void stamp_head(float x, float y, int bulb) {        /* a glow built from brightness steps: halo under, core over */
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    if (g_theme.on) {                                       /* in season the trail is a string of fairy lights: a thin wire, a bulb every so often */
        if (bulb) { stamp_disc(cx, cy, 5.5f, 0, 1); stamp_disc(cx, cy, 4.2f, 1, 1); stamp_disc(cx, cy, 3.2f, 2, 0); stamp_disc(cx, cy, 1.8f, 3, 0); }
        else stamp_disc(cx, cy, 1.2f, 2, 0);
        return;
    }
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
    S.best = best_get();
}

static void begin(void) {
    S.hue = theme_base_hue(&S.rng);
    S.score = 0; S.state = ST_PLAY;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    memcpy(field, bgimg, sizeof field);
    spawn_pick();
    S.bulb_d = 9.0f;
    snd_sfx(SFX_START);
    snd_music_play(MU_JINGLE);
}

static void enter(uint32_t seed) {
    ORNMAP['g'] = I_CORE; ORNMAP['o'] = I_PICK; ORNMAP['h'] = I_PICKDOT;
    for (int i = BASE; i < 256; i++) { slot_of[i] = (uint8_t)((i - BASE) / LEVELS); level_of[i] = (uint8_t)((i - BASE) % LEVELS); }
    S.rng = seed ? seed : 1u;
    S.hue = theme_base_hue(&S.rng);
    S.state = ST_MENU; S.score = 0; S.tick = 0; S.tick_acc = 0.0f; S.clock = 0.0; S.dead_t = 0.0;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    load_best();
    build_bg();
    spawn_pick();
}

static void die(void) {
    S.state = ST_DEAD; S.dead_t = 0.0;
    snd_sfx(g_theme.on ? SFX_CRASH : SFX_DIE);               /* in season, a comic trombone and a crash */
    snd_music_stop();
    if (S.score > S.best) {
        S.best = S.score;
        best_set(S.best);
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
    snd_music_tempo(1.0f + 0.025f * (float)(S.score > 20 ? 20 : S.score));          /* it quickens as you do */
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
        S.bulb_d += m_sqrt(sx * sx + sy * sy);
        int bulb = S.bulb_d >= 9.0f;
        if (bulb) S.bulb_d -= 9.0f;
        stamp_head(S.x, S.y, bulb);
    }
}

/* ---- drawing over the field ------------------------------------------ */
static void render(Frame *f) {
    if (g_theme.on) gfx_set_style(I_BORDER0, I_BORDER1, I_BG, I_BG);
    if (g_theme.on) {                                       /* ripe trail is striped as well as hot: the danger is not only a colour */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int v = field[y * W + x];
                if (lethal[v] && (((x + y) >> 1) & 1)) v -= 2;
                f->pix[y * W + x] = (uint8_t)v;
            }
    } else memcpy(f->pix, field, sizeof field);
    build_pal(f);
    char num[16], line[32];
    int px = (int)(S.pick_x + 0.5f), py = (int)(S.pick_y + 0.5f);
    if (g_theme.on) gfx_blit(f, px - 4, py - 5 + (int)(1.5f * m_sin((float)S.clock * 4.0f)), ORN, 11, ORNMAP);   /* a bauble, swaying */
    else {
        gfx_ring(f, px, py, 4 + (int)(1.5f + 1.5f * m_sin((float)S.clock * 5.0f)), I_PICK);
        gfx_disc(f, px, py, 1, I_PICKDOT);
    }
    int hx = (int)(S.x + 0.5f), hy = (int)(S.y + 0.5f);
    if (S.state == ST_MENU) { gfx_disc(f, hx, hy, 6, I_HALO); gfx_disc(f, hx, hy, 3 + (m_sin((float)S.clock * 2.0f) > 0.0f), I_CORE); }
    else if (S.state == ST_PLAY) gfx_disc(f, hx, hy, 2, I_CORE);
    if (g_theme.on && S.state != ST_DEAD) {                  /* the sparkler's spray: eight rays, never quite the same length */
        static const signed char RD[8][2] = { {1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1} };
        for (int k = 0; k < 8; k++) {
            int len = 3 + (((int)(S.clock * 31.0) + k * 5) % 4);
            for (int j = 2; j <= len; j++) gfx_px(f, hx + RD[k][0] * j, hy + RD[k][1] * j, j < 4 ? I_CORE : I_HALO);
        }
    }
    else {                                                   /* death: a ring runs out from where you were, the world flipping behind it */
        int r = (int)(S.dead_t * 130.0);
        gfx_ring(f, hx, hy, r, I_RING); gfx_ring(f, hx, hy, r + 1, I_RING);
        if (g_theme.on) gfx_gifts(f, S.dead_t, I_BORDER0, I_PICK, I_BORDER1, I_CORE);   /* presents and ornaments tumble down, for a laugh */
    }
    if (S.state == ST_MENU) {
        if (g_theme.on) { gfx_garland(f, S.clock, I_BORDER0, I_PICK, I_TEXT, I_HALO); gfx_sparkles(f, S.clock, I_TEXT); }
        gfx_text16_c(f, 14, g_theme.on ? "SPARKLER" : "ANTIPODE", I_TEXT, 2);
        gfx_text8_c(f, 52, g_theme.on ? "A SPARKLER ON A SNOWY NIGHT" : "A F T E R I M A G E", I_TEXT, 1);
        gfx_text8_c(f, 128, "YOUR TRAIL RIPENS INTO", I_TEXT, 1);
        gfx_text8_c(f, 140, "THE COLOUR YOU CANNOT TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 160, "WASD / STICK / MOUSE / TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 174, "PRESS ANY KEY", I_TEXT, 1);
    } else if (S.state == ST_PLAY) {
        gfx_itoa(S.score, num);
        gfx_text16_c(f, 178, num, I_TEXT, 1);
    } else {
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 70, "SCORE", I_TEXT, 1);
        gfx_text16_c(f, 82, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 122, line, I_TEXT, 1);
        if (S.dead_t > 0.6) gfx_text8_c(f, 150, "PRESS TO RETRY", I_TEXT, 1);
    }
}

static int idle(void) { return S.state == ST_MENU || (S.state == ST_DEAD && S.dead_t > 0.6); }
const Scene scene_afterimage = { "AFTERIMAGE", W, H, enter, update, render, idle };

/* for the test harness only: where things are, so a scripted bot can play */
void afterimage_debug(int *score, int *state, float *pick_x, float *pick_y, float *x, float *y) {
    *score = S.score; *state = S.state; *pick_x = S.pick_x; *pick_y = S.pick_y; *x = S.x; *y = S.y;
}
