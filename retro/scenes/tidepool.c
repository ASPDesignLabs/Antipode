/* Antipode - Tidepool, on the 256-colour engine.
 *
 * You skim a dark pool leaving a wake; the pool answers. Two sets of waves live in it at once, yours and the pool's
 * (the sources that pulse at the edges), and they pass through each other as real waves do. Where a crest of one lands
 * on a crest of the other the water stands up, and that is what kills. Slow water is safe water: your wake grows with
 * your speed. Collect pearls; the pool pulses faster and gathers more sources as you do.
 *
 * Each set is a small integer wave simulation (the old "ripple" algorithm: new = half the sum of the four neighbours
 * minus the last one, a little damping) on a 160x100 grid. The picture is the interesting part. A cell is one palette
 * index made of both heights:
 *
 *     index = BASE + level(yours) * 15 + level(the pool's)          15 x 15 = 225 entries
 *
 * so blending two things is a lookup in a two-dimensional palette, nothing is mixed per pixel, and the palette gives
 * each combination its colour: your hue where your crests are, its opposite where the pool's are, the hues between them
 * where they overlap, white-hot where they stand up together. The lethal test reads the same heights. */
#include "core.h"
#include "gfx.h"
#include "snd.h"

#define W 320
#define H 200
#define GW 160
#define GH 100
#define LV 15                       /* levels each side: 7 is level water */
#define BASE 16
#define QA 100                      /* height units to a level of your wake (fine: a wake is low)... */
#define QB 300                      /* ...and of the pool's */
#define LETHAL 1800                 /* the two heights together this high kill */
#define STEP_HZ 60.0f
#define MAXEMIT 6
#define RADIUS 3.5f

enum { I_PLAYER = 1, I_PHALO = 2, I_PEARL = 3, I_PEARLDOT = 4, I_EMIT = 5, I_EMITHOT = 6, I_YOU = 7, I_NOT = 8, I_RING = 9, I_PLATE = 10, I_TEXT = 15 };
enum { ST_MENU, ST_PLAY, ST_DEAD };

typedef struct { float x, y, t; int on; } Emitter;

static struct {
    uint32_t rng;
    float hue;
    int state, score, best;
    float x, y, vx, vy;
    float pearl_x, pearl_y;
    Emitter em[MAXEMIT];
    int nem;
    double clock, dead_t, step_acc, menu_t;
    int pa;                         /* which of the two buffers of each field is the present one */
} S;

static int16_t fa[2][GW * GH];      /* yours: [present], [past] */
static int16_t fb[2][GW * GH];      /* the pool's */
static uint8_t shore[GW * GH];      /* 1 on the margin, where the waves are soaked up so they do not slosh back */
static uint16_t shore_list[8192];
static int nshore;

static float rf(void) { return rng_f(&S.rng); }
static int cell_of(float x, float y) {
    int gx = clampi((int)(x * 0.5f), 1, GW - 2), gy = clampi((int)(y * 0.5f), 1, GH - 2);
    return gy * GW + gx;
}

/* ---- the water ---- */
static void wave_step(int16_t *cur, int16_t *old) {          /* old becomes the new */
    for (int y = 1; y < GH - 1; y++) {
        const int16_t *c = cur + y * GW;
        int16_t *o = old + y * GW;
        for (int x = 1; x < GW - 1; x++) {
            int n = ((c[x - 1] + c[x + 1] + c[x - GW] + c[x + GW]) >> 1) - o[x];
            n -= (n + ((n >> 31) & 127)) >> 7;               /* a little damping, the same either side of zero */
            o[x] = (int16_t)(n > 30000 ? 30000 : n < -30000 ? -30000 : n);
        }
    }
    for (int k = 0; k < nshore; k++) { int i = shore_list[k]; old[i] = (int16_t)(old[i] - (old[i] >> 1)); }
}
static void water_step(void) {
    int c = S.pa, o = 1 - S.pa;
    wave_step(fa[c], fa[o]);
    wave_step(fb[c], fb[o]);
    S.pa = o;
}
static void drop(int16_t *f, float x, float y, int amp, int to_max) {    /* a blob of height: it spreads as a ring. to_max: raise to it, don't add (a wake is as high as your speed, however long you stay) */
    int gx = (int)(x * 0.5f), gy = (int)(y * 0.5f);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int xx = gx + dx, yy = gy + dy;
            if (xx < 1 || yy < 1 || xx >= GW - 1 || yy >= GH - 1) continue;
            int add = dx == 0 && dy == 0 ? amp : amp / 2, v = to_max ? (f[yy * GW + xx] > add ? f[yy * GW + xx] : add) : f[yy * GW + xx] + add;
            f[yy * GW + xx] = (int16_t)(v > 30000 ? 30000 : v);
        }
}

/* ---- the game ---- */
static float interval(void) { float v = 3.4f - 0.12f * (float)S.score; return v < 1.7f ? 1.7f : v; }
static int want_emitters(void) { int n = 2 + S.score / 4; return n > MAXEMIT ? MAXEMIT : n; }

static void place_emitter(Emitter *e) {
    for (int tries = 0; tries < 40; tries++) {
        float x = 22.0f + rf() * (W - 44), y = 22.0f + rf() * (H - 44);
        float dx = x - S.x, dy = y - S.y;
        int ok = dx * dx + dy * dy > 80.0f * 80.0f;
        for (int k = 0; k < S.nem && ok; k++) { dx = x - S.em[k].x; dy = y - S.em[k].y; if (dx * dx + dy * dy < 70.0f * 70.0f) ok = 0; }
        if (ok || tries == 39) { e->x = x; e->y = y; break; }
    }
    e->t = interval() * (0.6f + 0.8f * rf()); e->on = 1;
}
static void place_pearl(void) {
    for (int tries = 0; tries < 30; tries++) {
        float x = 18.0f + rf() * (W - 36), y = 18.0f + rf() * (H - 36);
        float dx = x - S.x, dy = y - S.y;
        if (dx * dx + dy * dy < 50.0f * 50.0f) continue;
        int ok = 1;
        for (int k = 0; k < S.nem; k++) { dx = x - S.em[k].x; dy = y - S.em[k].y; if (dx * dx + dy * dy < 28.0f * 28.0f) ok = 0; }
        if (ok) { S.pearl_x = x; S.pearl_y = y; return; }
    }
    S.pearl_x = W - S.x; S.pearl_y = H - S.y;
}

static void clear_water(void) { memset(fa, 0, sizeof fa); memset(fb, 0, sizeof fb); S.pa = 0; }

static void reset_run(void) {
    S.hue = theme_base_hue(&S.rng);
    S.score = 0; S.x = W * 0.5f; S.y = H * 0.5f; S.vx = S.vy = 0.0f;
    S.nem = 0; clear_water();
    while (S.nem < want_emitters()) { place_emitter(&S.em[S.nem]); S.nem++; }
    place_pearl();
}
static void begin(void) { reset_run(); S.state = ST_PLAY; snd_sfx(SFX_START); snd_music_play(MU_JOY); }

static void enter(uint32_t seed) {
    S.rng = seed ? seed : 1u;
    nshore = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            int e = x < GW - 1 - x ? x : GW - 1 - x, ey = y < GH - 1 - y ? y : GH - 1 - y;
            if (ey < e) e = ey;
            shore[y * GW + x] = (uint8_t)(e < 9);
            if (e < 9 && nshore < 8192) shore_list[nshore++] = (uint16_t)(y * GW + x);
        }
    S.state = ST_MENU; S.clock = 0.0; S.dead_t = 0.0; S.step_acc = 0.0; S.menu_t = 0.0;
    S.best = best_get();
    reset_run();
    S.nem = 0;                                              /* a still, empty pool behind the title */
}

static void die(void) {
    S.state = ST_DEAD; S.dead_t = 0.0;
    snd_sfx(g_theme.on ? SFX_CRASH : SFX_DIE);               /* in season, a comic trombone and a crash */
    snd_music_stop();
    if (S.score > S.best) { S.best = S.score; best_set(S.best); }
}

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
        if (d > 3.0f) { *dx = px / d; *dy = py / d; *mag = clampf(d / 18.0f, 0.0f, 1.0f); return 1; }
    }
    return 0;
}

static int lethal_here(void) {
    int c = S.pa, i = cell_of(S.x, S.y);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int j = i + dy * GW + dx;
            if (fa[c][j] + fb[c][j] >= LETHAL) return 1;
        }
    return 0;
}

static void fire(Emitter *e) { if (g_theme.on) snd_sfx_note(SFX_PICKUP, 1 + (int)(e - S.em)); drop(fb[S.pa], e->x, e->y, 13000, 0);   /* in season a pulse is a bell */
    e->t = interval() * (0.85f + 0.3f * rf()); }

static void update(double dt, const Input *in) {
    S.clock += dt;
    int playing = S.state == ST_PLAY;
    float speed = 0.0f;

    if (playing) {
        float dx, dy, mag, tvx = 0.0f, tvy = 0.0f;
        if (steer(in, &dx, &dy, &mag)) { tvx = dx * mag * 72.0f; tvy = dy * mag * 72.0f; }
        float k = clampf((float)dt * 7.0f, 0.0f, 1.0f);
        S.vx += (tvx - S.vx) * k; S.vy += (tvy - S.vy) * k;
        S.x = clampf(S.x + S.vx * (float)dt, 8.0f, W - 8.0f);
        S.y = clampf(S.y + S.vy * (float)dt, 8.0f, H - 8.0f);
        speed = m_sqrt(S.vx * S.vx + S.vy * S.vy);
        for (int i = 0; i < S.nem; i++) {                    /* the pool's sources keep time */
            S.em[i].t -= (float)dt;
            if (S.em[i].t <= 0.0f) fire(&S.em[i]);
        }
    } else if (S.state == ST_MENU) {                        /* behind the title: pulses in your colour, and now and then the pool's */
        S.menu_t += dt;
        if (S.menu_t > 0.9) {
            S.menu_t = 0.0;
            drop(fa[S.pa], 30.0f + rf() * (W - 60), 40.0f + rf() * (H - 80), 9000, 0);
            if (rf() < 0.5f) drop(fb[S.pa], 30.0f + rf() * (W - 60), 40.0f + rf() * (H - 80), 9000, 0);
        }
        if (in->act) begin();
    } else {
        S.dead_t += dt;
        if (in->act && S.dead_t > 0.6) begin();
    }

    if (playing) snd_music_tempo(0.9f + 0.03f * (float)(S.score > 20 ? 20 : S.score));
    S.step_acc += dt * STEP_HZ;
    int steps = (int)S.step_acc;
    if (steps > 3) steps = 3;
    S.step_acc -= (double)(int)S.step_acc;
    for (int k = 0; k < steps; k++) {
        if (playing && speed > 4.0f) drop(fa[S.pa], S.x, S.y, (int)(speed * 12.0f), 1);   /* your wake: more of it the faster you go */
        water_step();
    }
    if (!playing) return;

    if (lethal_here()) { die(); return; }
    float px = S.x - S.pearl_x, py = S.y - S.pearl_y;
    if (px * px + py * py < (RADIUS + 5.0f) * (RADIUS + 5.0f)) {
        S.score++;
        snd_sfx_note(SFX_PICKUP, S.score);
        if (S.nem < want_emitters()) { place_emitter(&S.em[S.nem]); S.nem++; }
        place_pearl();
    }
}

/* ---- drawing ---- */
static void build_pal(Frame *f) {
    float t = (float)S.clock;
    float flip = S.state == ST_DEAD ? 0.5f * smoothstepf(0.0f, 0.5f, (float)S.dead_t) : 0.0f;
    float h0 = S.hue + flip;
    for (int a = 0; a < LV; a++)
        for (int b = 0; b < LV; b++) {
            float ha = (float)((a - 7) * QA), hb = (float)((b - 7) * QB);                   /* true heights */
            float ca = ha > 0.0f ? clampf(ha / 500.0f, 0.0f, 1.0f) : 0.0f, cb = hb > 0.0f ? clampf(hb / 1050.0f, 0.0f, 1.0f) : 0.0f;   /* crests read at their own scale */
            float tr = clampf(((ha < 0.0f ? -ha : 0.0f) / 700.0f + (hb < 0.0f ? -hb : 0.0f) / 2100.0f) * 0.5f, 0.0f, 1.0f);   /* troughs: deeper, darker water */
            float e = ca + cb, w = e > 0.0f ? cb / e : 0.0f;
            float hue = h0 + 0.5f * w;                                                        /* the arc from your colour to its opposite */
            float v = (g_theme.on ? 0.26f : 0.07f) + 0.50f * (e > 1.3f ? 1.3f : e) - 0.10f * tr;
            float s = 0.86f;
            if (g_theme.on) {                                                                 /* in season the water is a pond of winter blue (deep green for gold and blue), and a wave fades into your colour */
                float skyh = g_theme.pair ? 0.5f : 0.62f, d = hue - skyh, k = clampf(e * 4.0f, 0.0f, 1.0f);
                d -= m_floor(d + 0.5f);
                hue = skyh + d * k; s = 0.45f + 0.41f * k;
            }
            float sum = ha + hb;
            if (sum >= 1200.0f) {                                                            /* standing water: hot, and from 1800 deadly */
                float k = clampf((sum - 1200.0f) / 600.0f, 0.0f, 1.0f);
                s *= 1.0f - 0.7f * k; v = clampf(v + 0.35f * k + 0.18f * k * m_sin(t * 22.0f + (float)(a * 3 + b)), 0.0f, 1.0f);
            }
            gfx_pal_hsv(f, BASE + a * LV + b, hue, s, v < 0.02f ? 0.02f : v);
        }
    gfx_pal_hsv(f, I_PLAYER, h0, 0.25f, 1.0f);
    gfx_pal_hsv(f, I_PHALO, h0, 0.85f, 0.55f);
    gfx_pal_hsv(f, I_PEARL, h0, 0.5f, 0.9f + 0.1f * m_sin(t * 5.0f));
    gfx_pal_hsv(f, I_PEARLDOT, h0, 0.2f, 1.0f);
    gfx_pal_hsv(f, I_EMIT, h0 + 0.5f, 0.9f, 0.85f);
    gfx_pal_hsv(f, I_EMITHOT, h0 + 0.5f, 0.3f, 1.0f);
    gfx_pal_hsv(f, I_YOU, h0, 0.85f, 1.0f);
    gfx_pal_hsv(f, I_NOT, h0 + 0.5f, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_RING, S.hue + 0.5f, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_PLATE, h0, 0.5f, 0.045f);
    gfx_pal_hsv(f, I_TEXT, h0, 0.18f, S.state == ST_PLAY ? 0.9f : 0.78f + 0.22f * m_sin(t * 2.5f));
}

/* in season: a bell for each of the pool's sources, a flake for a pearl (o: body, g: rim, c: clapper, x: the flake's heart) */
static const char *const BELL[10] = {
    "....o....", "...ooo...", "..ooooo..", "..ooooo..", ".ooooooo.", ".ooooooo.", "ooooooooo", "ggggggggg", "....c....", "...ccc..." };
static const char *const FLAKE[7] = { "o..o..o", ".o.o.o.", "..ooo..", "ooooxoo", "..ooo..", ".o.o.o.", "o..o..o" };

static int level(int h, int q) { return clampi(h / q + 7 + (h < 0 && h % q ? -1 : 0), 0, LV - 1); }
static void plate(Frame *f, int x, int y, int w, int h) { gfx_rect(f, x, y, w, h, I_PLATE); }

static void render(Frame *f) {
    build_pal(f);
    if (g_theme.on) gfx_set_style(I_EMIT, I_PEARLDOT, I_PLATE, I_PLATE);
    const int16_t *a = fa[S.pa], *b = fb[S.pa];
    for (int gy = 0; gy < GH; gy++) {
        uint8_t *r0 = f->pix + (gy * 2) * W, *r1 = r0 + W;
        for (int gx = 0; gx < GW; gx++) {
            int i = gy * GW + gx;
            int la = level(a[i], QA), lb = level(b[i], QB);
            if (g_theme.on && a[i] + b[i] >= 1500 && ((gx + gy) & 1)) lb = lb > 3 ? lb - 3 : 0;   /* water standing up is striped, not only hot */
            uint8_t px = (uint8_t)(BASE + la * LV + lb);
            r0[gx * 2] = r0[gx * 2 + 1] = r1[gx * 2] = r1[gx * 2 + 1] = px;
        }
    }
    char num[16], line[32];
    for (int i = 0; i < S.nem; i++) {                        /* the pool's sources: they blink before they pulse */
        int tel = S.state == ST_PLAY && S.em[i].t < 0.7f && ((int)(S.clock * 14.0) & 1);
        if (g_theme.on) {                                    /* a bell, swinging as it is about to ring */
            uint8_t bm[256]; memset(bm, 0, sizeof bm);
            bm['o'] = tel ? I_EMITHOT : I_EMIT; bm['g'] = I_EMITHOT; bm['c'] = I_EMITHOT;
            int sw = S.state == ST_PLAY && S.em[i].t < 0.7f ? (((int)(S.clock * 14.0) & 1) ? 1 : -1) : 0;
            gfx_blit(f, (int)S.em[i].x - 4 + sw, (int)S.em[i].y - 5, BELL, 10, bm);
        } else gfx_disc(f, (int)S.em[i].x, (int)S.em[i].y, 3, tel ? I_EMITHOT : I_EMIT);
        if (S.state == ST_PLAY && S.em[i].t < 0.7f) gfx_ring(f, (int)S.em[i].x, (int)S.em[i].y, 6 + (int)((0.7f - S.em[i].t) * 6.0f), I_EMIT);
    }
    int px = (int)(S.pearl_x + 0.5f), py = (int)(S.pearl_y + 0.5f);
    if (S.state != ST_MENU) {
        if (g_theme.on) {                                    /* a snowflake, bobbing */
            uint8_t fm[256]; memset(fm, 0, sizeof fm); fm['o'] = I_PEARL; fm['x'] = I_PEARLDOT;
            gfx_blit(f, px - 3, py - 3 + (int)(1.5f * m_sin((float)S.clock * 4.0f)), FLAKE, 7, fm);
        } else { gfx_ring(f, px, py, 4 + (int)(1.5f + 1.5f * m_sin((float)S.clock * 5.0f)), I_PEARL); gfx_disc(f, px, py, 1, I_PEARLDOT); }
    }
    int hx = (int)(S.x + 0.5f), hy = (int)(S.y + 0.5f);
    if (S.state != ST_DEAD || S.dead_t < 0.35) {
        gfx_disc(f, hx, hy, 6, I_PHALO);
        if (g_theme.on) {                                    /* a little sleigh, facing the way it goes */
            int dir = S.vx >= 0.0f ? 1 : -1;
            gfx_rect(f, hx - 4, hy - 1, 8, 3, I_PLAYER);
            gfx_rect(f, dir > 0 ? hx - 5 : hx + 4, hy - 3, 2, 3, I_PLAYER);
            gfx_line(f, hx - 6, hy + 3, hx + 6, hy + 3, I_PLAYER);
            gfx_px(f, hx + 6 * dir, hy + 2, I_PLAYER); gfx_px(f, hx + 7 * dir, hy + 1, I_PLAYER);
        } else gfx_disc(f, hx, hy, 3, I_PLAYER);
    }
    if (S.state == ST_PLAY || S.state == ST_DEAD) {
        plate(f, 3, 3, 46, 30);
        gfx_rect(f, 6, 6, 10, 10, I_YOU); gfx_text8(f, 20, 7, "YOU", I_TEXT, 1);
        gfx_rect(f, 6, 20, 10, 10, I_NOT); gfx_text8(f, 20, 21, g_theme.on ? "BELLS" : "POOL", I_TEXT, 1);
    }
    if (S.state == ST_PLAY) {
        gfx_itoa(S.score, num);
        plate(f, W / 2 - 4 * (int)strlen(num) - 4, 3, 8 * (int)strlen(num) + 8, 20);
        gfx_text16_c(f, 5, num, I_TEXT, 1);
    } else if (S.state == ST_DEAD) {
        int r = (int)(S.dead_t * 130.0);
        if (g_theme.on) gfx_gifts(f, S.dead_t, I_EMIT, I_PEARL, I_PEARLDOT, I_PLAYER);   /* presents and ornaments tumble down, for a laugh */
        else { gfx_ring(f, hx, hy, r, I_RING); gfx_ring(f, hx, hy, r + 1, I_RING); }
        plate(f, 60, 54, 200, 108);
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 62, g_theme.on ? "FLAKES" : "PEARLS", I_TEXT, 1);
        gfx_text16_c(f, 74, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 114, line, I_TEXT, 1);
        if (S.dead_t > 0.6) gfx_text8_c(f, 140, "PRESS TO RETRY", I_TEXT, 1);
    } else {
        if (g_theme.on) { gfx_garland(f, S.clock, I_EMIT, I_PEARL, I_TEXT, I_PHALO); gfx_sparkles(f, S.clock, I_TEXT); }
        plate(f, 24, 10, 272, 52); plate(f, 10, 112, 300, 72);
        gfx_text16_c(f, 14, g_theme.on ? "SLEIGH BELLS" : "TIDEPOOL", I_TEXT, 2);
        gfx_text8_c(f, 50, g_theme.on ? "THE BELLS ANSWER IN KIND" : "A POOL THAT ANSWERS IN KIND", I_TEXT, 1);
        gfx_text8_c(f, 118, g_theme.on ? "THE BELLS RING IN YOUR OPPOSITE" : "THE POOL RISES IN YOUR OPPOSITE", I_TEXT, 1);
        gfx_text8_c(f, 130, "WHERE THE CRESTS MEET, YOU DIE", I_TEXT, 1);
        gfx_text8_c(f, 148, "STEER: KEYS / STICK / MOUSE / TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 162, "PRESS ANY KEY", I_TEXT, 1);
    }
}

static int idle(void) { return S.state == ST_MENU || (S.state == ST_DEAD && S.dead_t > 0.6); }
const Scene scene_tidepool = { "TIDEPOOL", W, H, enter, update, render, idle };

/* for the test harness only: a bot's heading. It heads for the pearl, shies from a source that is about to pulse or whose
 * ring is still near, and from water that is already standing up ahead of it. */
void tidepool_debug(int *score, int *state, float *dirx, float *diry) {
    *score = S.score; *state = S.state; *dirx = *diry = 0.0f;
    if (S.state != ST_PLAY) return;
    float bx = S.pearl_x - S.x, by = S.pearl_y - S.y, bl = m_sqrt(bx * bx + by * by) + 0.001f;
    float best = -1e9f;
    for (int k = 0; k < 16; k++) {
        float ang = (float)k * (TAU_F / 16.0f), cx = m_cos(ang), cy = m_sin(ang);
        float sc = (cx * bx + cy * by) / bl;
        for (int d = 1; d <= 3; d++) {
            float x = clampf(S.x + cx * 10.0f * (float)d, 8.0f, W - 8.0f), y = clampf(S.y + cy * 10.0f * (float)d, 8.0f, H - 8.0f);
            int i = cell_of(x, y);
            float hgt = (float)(fa[S.pa][i] + fb[S.pa][i]) / (float)LETHAL;
            if (hgt > 0.35f) sc -= 4.0f * hgt;
            for (int e = 0; e < S.nem; e++) {
                float ex = x - S.em[e].x, ey = y - S.em[e].y, dd = m_sqrt(ex * ex + ey * ey);
                float since = interval() - S.em[e].t;                 /* seconds since it pulsed */
                if (dd < 78.0f && (S.em[e].t < 1.0f || since < 0.9f)) sc -= (78.0f - dd) * 0.06f;
                if (dd < 14.0f) sc -= 6.0f;
            }
        }
        if (sc > best) { best = sc; *dirx = cx; *diry = cy; }
    }
    int here = cell_of(S.x, S.y), worst = 0;                       /* water already standing round it: go slowly, the wake is what tips it over */
    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int v = fb[S.pa][here + dy * GW + dx]; if (v > worst) worst = v; }
    if (worst > LETHAL / 5) { *dirx *= 0.12f; *diry *= 0.12f; }
}
