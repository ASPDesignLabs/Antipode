/* Antipode - Prism Run, on the 256-colour engine.
 *
 * You fly down a tunnel of rainbow cells and roll it left and right to stay on the wall you want. Your own hue drifts
 * round the wheel as you go, and the cells that are the *complement* of it kill: they glow hot, and they move round the
 * wheel with you. Stay on your own colour, or anything far from its opposite.
 *
 * The "shader" is a table: for each of the 64,000 pixels the angle round the tunnel and the depth down it were worked
 * out once (a perspective divide and an atan2, never again). A frame is then one pass of lookups:
 *
 *     cell  = pattern[ring(depth + z)][segment(angle + roll)]      the rings ahead are made as you reach them
 *     index = BASE + hue * FOG + fog(depth)                         24 hues x 10 steps of fog = 240 entries
 *
 * and everything colourful is the palette: the hue wheel, the fog, the lethal hues' glow, the flip when you die. The
 * lethal test asks what is drawn under the ship. */
#include "core.h"
#include "gfx.h"
#include "snd.h"

#define W 320
#define H 200

#define HUES 24
#define FOG 10
#define BASE 16                     /* BASE + HUES * FOG = 256 */
#define RINGLEN 256                 /* depth units in a ring */
#define NRING 64                    /* rings kept (a power of two); 30 are visible, 56 are made ahead */
#define SEGS 16
#define MAXD (30 * RINGLEN)
#define PX 160                      /* the ship, in pixels */
#define PY 172
#define DP 0.30f                    /* hue steps your colour drifts a ring: a full turn in 80 rings */
#define ROT_RATE 150.0f             /* roll, in angle units (256 a turn) a second */

enum { I_VOID = 0, I_GAP = 1, I_PLAYER = 3, I_PHALO = 4, I_YOU = 5, I_NOT = 6, I_RING = 7, I_PLATE = 8, I_TEXT = 15 };
enum { ST_MENU, ST_PLAY, ST_DEAD };

static struct {
    uint32_t rng;
    float p0;                       /* your hue (in steps of 1/24) at the start */
    int state, score, best, start, gen_next, lane;
    double zf, clock, dead_t;
    float rot, rotv;
    int pd;                         /* the depth under the ship */
} S;

static uint8_t pat[NRING][SEGS];    /* a ring's cells: hue steps */
static uint8_t tun_a[W * H];        /* angle round the tunnel, 0..255 (192 is straight down) */
static uint8_t tun_f[W * H];        /* fog step, dithered; 255 where the tunnel runs out (the void) */
static uint8_t tun_rg[W * H];       /* a ring's edge line is this thick, in depth units, to be about a pixel on screen */
static uint8_t tun_ag[W * H];       /* a segment's edge line: the same in angle units */
static uint16_t tun_d[W * H];       /* depth, in ring units / 256 */
static int tables_ready;

static float circ(float a, float b) {                       /* distance round the 24-step wheel */
    float d = a - b;
    d -= HUES * m_floor(d / HUES + 0.5f);
    return d < 0.0f ? -d : d;
}
static float p_at(double z) { float p = S.p0 + (float)(z / RINGLEN) * DP; return p - HUES * m_floor(p / HUES); }
static int lethal_hue(int h, float p) { return circ((float)h, p + 12.0f) < 1.2f; }

/* ---- the tunnel table, made once ---- */
static void build_tables(void) {
    static const uint8_t BAY[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    if (tables_ready) return;
    tables_ready = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            float dx = (float)x + 0.5f - 160.0f, dy = (float)y + 0.5f - 100.0f;
            float r = m_sqrt(dx * dx + dy * dy);
            if (r < 0.5f) r = 0.5f;
            tun_a[i] = (uint8_t)((int)((m_atan2(dy, dx) + PI_F) * (256.0f / TAU_F)) & 255);
            float dring = 45.0f / r;                          /* rings away: the perspective divide */
            float d = dring * RINGLEN;
            int voidpx = d >= (float)MAXD;
            tun_d[i] = (uint16_t)(voidpx ? MAXD : (int)d);
            float f = (FOG - 1) * (1.0f - clampf(dring / 22.0f, 0.0f, 1.0f));
            int lvl = clampi((int)(f + (float)BAY[(y & 3) * 4 + (x & 3)] * (1.0f / 16.0f)), 0, FOG - 1);
            tun_f[i] = (uint8_t)(voidpx ? 255 : lvl);
            tun_rg[i] = (uint8_t)clampi((int)(1.3f * (45.0f * RINGLEN) / (r * r) + 0.5f), 2, 64);
            tun_ag[i] = (uint8_t)clampi((int)(1.3f * 256.0f / (TAU_F * r) + 0.5f), 1, 8);
        }
    S.pd = tun_d[PY * W + PX];
}

/* ---- the rings ---- */
static void gen_ring(int n) {
    float pn = S.p0 + ((float)n + 0.5f) * DP;                /* where your colour will be as this ring goes under you */
    int ph = (int)(pn + 0.5f) % HUES, comp = (ph + 12) % HUES;
    float r = rng_f(&S.rng);
    int step = r < 0.4f ? 0 : r < 0.7f ? -1 : 1;
    S.lane = (S.lane + step + SEGS) % SEGS;                  /* a way through two cells wide, a cell sideways at most from the last:
                                                               * so two rings always share a cell that is safe in both */
    float lethal_p = n < 6 ? 0.0f : 0.30f + (float)(n < 60 ? n : 60) * 0.0065f;
    for (int s = 0; s < SEGS; s++) {
        int hue;
        if (s == S.lane || s == (S.lane + 1) % SEGS) hue = ph;                               /* your own colour: always safe */
        else if (rng_f(&S.rng) < lethal_p) hue = comp;       /* its opposite exactly: your colour drifts less than a step while you cross
                                                               * a ring, so a cell is never lethal one moment and safe the next */
        else {
            hue = (int)(rng_next(&S.rng) % (uint32_t)HUES);
            for (int t = 0; t < 8 && circ((float)hue, (float)comp) < 3.0f; t++) hue = (int)(rng_next(&S.rng) % (uint32_t)HUES);
        }
        pat[n & (NRING - 1)][s] = (uint8_t)hue;
    }
}
static void gen_ahead(void) { while (S.gen_next < (int)(S.zf / RINGLEN) + 56) gen_ring(S.gen_next++); }

static int cell_hue(int i, int roti, int z) {
    int a = (tun_a[i] + roti) & 255, wz = tun_d[i] + z;
    return pat[(wz >> 8) & (NRING - 1)][a >> 4];
}

/* ---- the game ---- */
static void reset_run(void) {
    S.p0 = rng_f(&S.rng) * HUES;
    S.zf = 0.0; S.rot = 0.0f; S.rotv = 0.0f;
    S.score = 0; S.start = 0; S.gen_next = 0; S.lane = SEGS / 2;
    gen_ahead();
}

static void begin(void) {
    reset_run();
    S.state = ST_PLAY;
    snd_sfx(SFX_START);
}

static void enter(uint32_t seed) {
    build_tables();
    S.rng = seed ? seed : 1u;
    S.state = ST_MENU; S.clock = 0.0; S.dead_t = 0.0;
    S.best = best_get();
    reset_run();
}

#ifdef PRISM_TRACE
#include <stdio.h>
static void trace_death(void) {
    int z = (int)S.zf, roti = (int)m_floor(S.rot) & 255, c0 = (int)((S.zf + S.pd) / RINGLEN);
    float p = p_at(S.zf + S.pd);
    printf("DEATH ring=%d frac=%.2f p=%.2f comp=%.2f rot=%.1f rotv=%.1f under=%d lane=%d\n", c0, (S.zf + S.pd) / RINGLEN - c0, p, p + 12, S.rot, S.rotv, (tun_a[PY * W + PX] + roti) & 255, 0);
    for (int dx = -4; dx <= 4; dx += 4) { int h = cell_hue(PY * W + PX + dx, roti, z); printf("  px%+d hue=%d lethal=%d seg=%d\n", dx, h, lethal_hue(h, p), ((tun_a[PY * W + PX + dx] + roti) & 255) >> 4); }
    for (int r = c0; r <= c0 + 1; r++) { printf("  ring %d:", r); for (int s2 = 0; s2 < SEGS; s2++) printf(" %c%02d", lethal_hue(pat[r & 63][s2], p) ? '!' : ' ', pat[r & 63][s2]); printf("\n"); }
}
#endif
static void die(void) {
#ifdef PRISM_TRACE
    trace_death();
#endif
    S.state = ST_DEAD; S.dead_t = 0.0;
    snd_sfx(SFX_DIE);
    if (S.score > S.best) { S.best = S.score; best_set(S.best); }
}

/* Roll the tunnel: the keys and a stick give an axis, a pointer steers by which side of the middle it is on. */
static float steer(const Input *in) {
    float a = 0.0f;
    if (in->down[KEY_LEFT] || in->down[KEY_A]) a -= 1.0f;
    if (in->down[KEY_RIGHT] || in->down[KEY_D]) a += 1.0f;
    if (in->pad) {
        float lx = in->lx;
        if (in->pad_down[PAD_LEFT]) lx -= 1.0f;
        if (in->pad_down[PAD_RIGHT]) lx += 1.0f;
        if (lx != 0.0f) a = lx;
    }
    if (a == 0.0f && in->ptr_on) a = clampf((in->ptr_x - PX) / 70.0f, -1.0f, 1.0f);
    return clampf(a, -1.0f, 1.0f);
}

static void update(double dt, const Input *in) {
    S.clock += dt;
    if (S.state == ST_MENU) {                                /* idle: a slow, swaying flight behind the title */
        S.zf += 1.4 * RINGLEN * dt;
        S.rot = 22.0f * m_sin((float)S.clock * 0.45f) + 10.0f;
        gen_ahead();
        if (in->act) begin();
        return;
    }
    if (S.state == ST_DEAD) {
        S.dead_t += dt;
        if (in->act && S.dead_t > 0.6) begin();
        return;
    }
    int rings = (int)(S.zf / RINGLEN);
    float speed = 3.0f + (float)(rings < 150 ? rings : 150) * 0.02f;
    S.zf += (double)speed * RINGLEN * dt;
    float target = -steer(in) * ROT_RATE;                    /* pressing left rolls the world right: you go left */
    S.rotv += (target - S.rotv) * clampf((float)dt * 14.0f, 0.0f, 1.0f);
    S.rot += S.rotv * (float)dt;
    S.rot -= 256.0f * m_floor(S.rot * (1.0f / 256.0f));
    gen_ahead();

    int z = (int)S.zf, roti = (int)m_floor(S.rot) & 255;
    float p = p_at(S.zf + S.pd);
    for (int dx = -4; dx <= 4; dx += 4)                      /* what is drawn under the ship, left, middle, right */
        if (lethal_hue(cell_hue(PY * W + PX + dx, roti, z), p)) { die(); return; }
    int sc = (int)(S.zf / RINGLEN) - S.start;
    if (sc > S.score) {
        if (sc % 10 == 0) snd_sfx_note(SFX_PICKUP, sc / 10);
        S.score = sc;
    }
}

/* ---- drawing ---- */
static void build_pal(Frame *f) {
    float t = (float)S.clock;
    float flip = S.state == ST_DEAD ? 0.5f * smoothstepf(0.0f, 0.5f, (float)S.dead_t) : 0.0f;
    float p = p_at(S.zf + S.pd), comp = p + 12.0f;
    for (int j = 0; j < HUES; j++) {
        float danger = 1.0f - smoothstepf(0.9f, 1.7f, circ((float)j, comp));   /* the complement of you glows hot, wherever it is */
        float hue = (float)j / HUES + flip;
        for (int l = 0; l < FOG; l++) {
            float x = (float)l / (FOG - 1), v = 0.05f + 0.95f * x * (0.45f + 0.55f * x);
            v *= 1.0f + danger * (0.30f + 0.30f * m_sin(t * 18.0f + (float)j * 0.8f));
            gfx_pal_hsv(f, BASE + j * FOG + l, hue, 0.85f * (1.0f - 0.5f * danger), v);
        }
    }
    float ph = p / HUES + flip;
    gfx_pal_hsv(f, I_VOID, ph, 0.5f, 0.0f);
    gfx_pal_hsv(f, I_GAP, ph, 0.5f, 0.04f);
    gfx_pal_hsv(f, I_PLAYER, ph, 0.25f, 1.0f);
    gfx_pal_hsv(f, I_PHALO, ph, 0.85f, 0.55f);
    gfx_pal_hsv(f, I_YOU, ph, 0.85f, 1.0f);
    gfx_pal_hsv(f, I_NOT, ph + 0.5f, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_RING, ph + 0.5f, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_PLATE, ph, 0.5f, 0.045f);
    gfx_pal_hsv(f, I_TEXT, ph, 0.18f, S.state == ST_PLAY ? 0.9f : 0.78f + 0.22f * m_sin(t * 2.5f));
}

static void plate(Frame *f, int x, int y, int w, int h) { gfx_rect(f, x, y, w, h, I_PLATE); }

static void render(Frame *f) {
    build_pal(f);
    int z = (int)S.zf, roti = (int)m_floor(S.rot) & 255;
    for (int i = 0; i < W * H; i++) {
        int fg = tun_f[i];
        if (fg == 255) { f->pix[i] = I_VOID; continue; }
        int a = (tun_a[i] + roti) & 255, wz = tun_d[i] + z, fr = wz & 255;
        if (fr < tun_rg[i] || ((a & 15) < tun_ag[i] && fg > 2)) { f->pix[i] = (uint8_t)(fg > 1 ? I_GAP : I_VOID); continue; }
        int lvl = fr < 40 ? fg + 1 : fg;                     /* a lit leading edge to each ring */
        if (lvl > FOG - 1) lvl = FOG - 1;
        f->pix[i] = (uint8_t)(BASE + pat[(wz >> 8) & (NRING - 1)][a >> 4] * FOG + lvl);
    }
    char num[16], line[32];
    if (S.state != ST_DEAD || S.dead_t < 0.35) { gfx_disc(f, PX, PY, 8, I_PHALO); gfx_disc(f, PX, PY, 4, I_PLAYER); }
    if (S.state == ST_PLAY || S.state == ST_DEAD) {
        plate(f, 3, 3, 46, 30);
        gfx_rect(f, 6, 6, 10, 10, I_YOU); gfx_text8(f, 20, 7, "YOU", I_TEXT, 1);
        gfx_rect(f, 6, 20, 10, 10, I_NOT); gfx_text8(f, 20, 21, "NOT", I_TEXT, 1);
    }
    if (S.state == ST_PLAY) {
        gfx_itoa(S.score, num);
        plate(f, W / 2 - 4 * (int)strlen(num) - 4, 3, 8 * (int)strlen(num) + 8, 20);
        gfx_text16_c(f, 5, num, I_TEXT, 1);
    } else if (S.state == ST_DEAD) {
        int r = (int)(S.dead_t * 130.0);
        gfx_ring(f, PX, PY, r, I_RING); gfx_ring(f, PX, PY, r + 1, I_RING);
        plate(f, 60, 54, 200, 108);
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 62, "SCORE", I_TEXT, 1);
        gfx_text16_c(f, 74, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 114, line, I_TEXT, 1);
        if (S.dead_t > 0.6) gfx_text8_c(f, 140, "PRESS TO RETRY", I_TEXT, 1);
    } else {
        plate(f, 24, 10, 272, 52); plate(f, 10, 112, 300, 72);
        gfx_text16_c(f, 14, "PRISM RUN", I_TEXT, 2);
        gfx_text8_c(f, 50, "A FLIGHT DOWN THE COLOUR WHEEL", I_TEXT, 1);
        gfx_text8_c(f, 118, "YOUR COLOUR DRIFTS AS YOU FLY", I_TEXT, 1);
        gfx_text8_c(f, 130, "NEVER TOUCH ITS OPPOSITE", I_TEXT, 1);
        gfx_text8_c(f, 148, "STEER: KEYS / STICK / MOUSE / TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 162, "PRESS ANY KEY", I_TEXT, 1);
    }
}

static int idle(void) { return S.state == ST_MENU || (S.state == ST_DEAD && S.dead_t > 0.6); }
const Scene scene_prism = { "PRISM RUN", W, H, enter, update, render, idle };

/* for the test harness only: a bot's steering, to find out whether the rings can always be flown */
float prism_debug(int *score, int *state) {
    *score = S.score; *state = S.state;
    int roti = (int)m_floor(S.rot) & 255, c0 = (int)((S.zf + S.pd) / RINGLEN), c1 = c0 + 1;
    float p = p_at(S.zf + S.pd), p1 = S.p0 + ((float)c1 + 0.5f) * DP;
    int under = (tun_a[PY * W + PX] + roti) & 255, cur = under >> 4;
    int safe0[SEGS], safe1[SEGS], best = -1, bd = 999;
    for (int s = 0; s < SEGS; s++) {
        safe0[s] = !lethal_hue(pat[c0 & (NRING - 1)][s], p);
        safe1[s] = !lethal_hue(pat[c1 & (NRING - 1)][s], p1);
    }
    for (int pass = 0; pass < 2 && best == -1; pass++)       /* first: follow your own colour (the way through); failing that, any safe cell */
        for (int s = 0; s < SEGS; s++) {
            if (!safe0[s] || !safe1[s]) continue;
            if (pass == 0 && (circ((float)pat[c0 & (NRING - 1)][s], p) > 0.8f || circ((float)pat[c1 & (NRING - 1)][s], p1) > 0.8f)) continue;
            int ds = (s - cur + 24) % SEGS - 8, dir = ds < 0 ? -1 : 1, ok = 1;     /* the shorter way round, every cell on it safe now */
            for (int k = cur, n = 0; n <= (ds < 0 ? -ds : ds); n++, k = (k + dir + SEGS) % SEGS) if (!safe0[k]) ok = 0;
            if (!ok) continue;
            int d = (s * 16 + 8 - under + 384) % 256 - 128;
            if ((d < 0 ? -d : d) < bd) { bd = d < 0 ? -d : d; best = d; }
        }
    if (best == -1) return 0.0f;
    return -clampf((float)best / 20.0f, -1.0f, 1.0f);         /* rolling the world by +d moves the ship d: the axis is minus that */
}
