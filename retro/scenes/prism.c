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
 * lethal test asks what is drawn under the ship.
 *
 * In season (Caroling Run) the rules are the same shape but the picture is not a rainbow. The cells are red, green and white
 * (candy stripes, holly and berries, snow) and what kills is not a colour but an EMPTY SPACE: a hollow panel, dark, with the night
 * showing through, rimmed in the colour you may not touch. You are red for a verse and green for the next, and the other colour is
 * always hollow; white and your own colour are always solid. A cell is one byte: its colour in the low bits, 0x80 for hollow. */
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
#define VERSE 24                    /* rings to a verse, in season */

enum { I_VOID = 0, I_GAP = 1, I_PLAYER = 3, I_PHALO = 4, I_YOU = 5, I_NOT = 6, I_RING = 7, I_PLATE = 8, I_GOLD = 9, I_TEXT = 15 };
enum { ST_MENU, ST_PLAY, ST_DEAD };
/* in season: the colours (the slots stand where the hues stood) and the kinds of cell */
enum { C_RED, C_DRED, C_GREEN, C_DGREEN, C_WHITE, C_SNOW, C_HOLLOW, C_RIMR, C_RIMG, C_STAR };
enum { K_RED = 0, K_GREEN = 1, K_WHITE = 2, K_HOLLOW = 0x80 };

static struct {
    uint32_t rng;
    float p0;                       /* your hue (in steps of 1/24) at the start */
    int state, score, best, start, gen_next, lane;
    double zf, clock, dead_t, banner_t;
    const char *banner;
    char bbuf[24];
    int verse;
    double snow;                    /* how far the snow has fallen: it falls faster as you fly faster */
    float rot, rotv;
    int pd;                         /* the depth under the ship */
} S;

static uint8_t pat[NRING][SEGS];    /* a ring's cells: hue steps (in season: a colour and 0x80 for hollow) */
static uint8_t lane_dbg[NRING];     /* where each ring's way through starts: for the test bot only */
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
static int lethal_cell(int c, float p) { return g_theme.on ? (c & K_HOLLOW) != 0 : lethal_hue(c, p); }   /* in season, what is hollow */

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
static int verse_of(int ring) { return (ring / VERSE) & 1; }   /* 0: you are red, 1: you are green */

/* In season: a way through two cells wide in your colour (so two rings always share a solid cell), the rest white, your colour,
 * or an empty space in the other colour. Nothing about the way depends on colour, only on which cells are hollow. */
static void gen_ring_xmas(int n) {
    int fk = verse_of(n) ? K_GREEN : K_RED, hk = fk == K_RED ? K_GREEN : K_RED;
    float r = rng_f(&S.rng);
    int step = r < 0.4f ? 0 : r < 0.7f ? -1 : 1;
    S.lane = (S.lane + step + SEGS) % SEGS;
    lane_dbg[n & (NRING - 1)] = (uint8_t)S.lane;
    float lethal_p = n < 6 ? 0.0f : 0.30f + (float)(n < 60 ? n : 60) * 0.0065f;
    for (int sg = 0; sg < SEGS; sg++) {
        int c;
        if (sg == S.lane || sg == (S.lane + 1) % SEGS) c = fk;
        else if (rng_f(&S.rng) < lethal_p) c = hk | K_HOLLOW;
        else c = rng_f(&S.rng) < 0.6f ? K_WHITE : fk;
        pat[n & (NRING - 1)][sg] = (uint8_t)c;
    }
}

static void gen_ring(int n) {
    if (g_theme.on) { gen_ring_xmas(n); return; }
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
    S.p0 = theme_base_hue(&S.rng) * HUES;
    S.zf = 0.0; S.rot = 0.0f; S.rotv = 0.0f;
    S.score = 0; S.start = 0; S.gen_next = 0; S.lane = SEGS / 2; S.verse = 0;
    gen_ahead();
}

static void begin(void) {
    reset_run();
    S.state = ST_PLAY;
    S.banner_t = 0.0;
    snd_sfx(SFX_START);
    snd_music_play(MU_DECK);
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
    snd_sfx(g_theme.on ? SFX_CRASH : SFX_DIE);                /* in season the carol breaks off into a trombone and a crash */
    snd_music_stop();
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
        S.snow += 40.0 * dt;
        S.zf += 1.4 * RINGLEN * dt;
        S.rot = 22.0f * m_sin((float)S.clock * 0.45f) + 10.0f;
        gen_ahead();
        if (in->act) begin();
        return;
    }
    if (S.state == ST_DEAD) {
        S.snow += 25.0 * dt;
        S.dead_t += dt;
        if (in->act && S.dead_t > 0.6) begin();
        return;
    }
    int rings = (int)(S.zf / RINGLEN);
    float speed = 1.5f + (float)(rings < 150 ? rings : 150) * 0.03f;   /* 1.5 rings a second to start, 6 by the 150th, as it ever was */
    S.zf += (double)speed * RINGLEN * dt;
    S.snow += (25.0 + 45.0 * (double)speed) * dt;            /* 90 pixels a second at the start, 300 at full speed */
    float target = -steer(in) * ROT_RATE;                    /* pressing left rolls the world right: you go left */
    S.rotv += (target - S.rotv) * clampf((float)dt * 14.0f, 0.0f, 1.0f);
    S.rot += S.rotv * (float)dt;
    S.rot -= 256.0f * m_floor(S.rot * (1.0f / 256.0f));
    gen_ahead();
    if (S.banner_t > 0.0) S.banner_t -= dt;
    snd_music_tempo(0.75f + speed * 0.17f);                  /* the carol keeps pace with the flight */

    if (g_theme.on) {                                        /* a new verse: the colours change places, and a banner says so */
        int v = (int)((S.zf + S.pd) / RINGLEN) / VERSE;
        if (v != S.verse) {
            S.verse = v;
            char nb[12]; gfx_itoa(v + 1, nb);
            const char *a = "VERSE "; int k = 0;
            for (; a[k]; k++) S.bbuf[k] = a[k];
            for (int q = 0; nb[q]; q++) S.bbuf[k++] = nb[q];
            S.bbuf[k] = 0; S.banner = S.bbuf; S.banner_t = 2.2;
            snd_sfx(SFX_START);
        }
    }
    int z = (int)S.zf, roti = (int)m_floor(S.rot) & 255;
    float p = p_at(S.zf + S.pd);
    for (int dx = -4; dx <= 4; dx += 4)                      /* what is drawn under the ship, left, middle, right */
        if (lethal_cell(cell_hue(PY * W + PX + dx, roti, z), p)) { die(); return; }
    int sc = (int)(S.zf / RINGLEN) - S.start;
    if (sc > S.score) {
        if (sc % (g_theme.on ? 6 : 10) == 0) snd_sfx_note(SFX_PICKUP, sc / (g_theme.on ? 6 : 10));
        S.score = sc;
    }
}

/* ---- drawing ---- */
/* In season: ten colours, each in ten steps of fog, in place of twenty-four hues. */
static void build_pal_xmas(Frame *f) {
    float t = (float)S.clock;
    int verse = verse_of((int)((S.zf + S.pd) / RINGLEN));
    float rh = g_theme.pair ? GOLD_RING : 0.0f, gh = rh + 0.5f;              /* the pair: red and green, or gold and blue */
    float fr_h = verse ? gh : rh, ho_h = verse ? rh : gh;                    /* your colour, and the one that is hollow */
    float skyh = g_theme.pair ? 0.5f : 0.667f;                               /* a winter-dusk blue, or a deep green for gold and blue */
    for (int l = 0; l < FOG; l++) {
        float x = (float)l / (FOG - 1), v = 0.05f + 0.95f * x * (0.45f + 0.55f * x);
        gfx_pal_hsv(f, BASE + C_RED * FOG + l, rh, 0.92f, v);
        gfx_pal_hsv(f, BASE + C_DRED * FOG + l, rh, 0.95f, v * 0.62f);
        gfx_pal_hsv(f, BASE + C_GREEN * FOG + l, gh, 0.85f, v * 0.92f);
        gfx_pal_hsv(f, BASE + C_DGREEN * FOG + l, gh, 0.95f, v * 0.52f);
        gfx_pal_hsv(f, BASE + C_WHITE * FOG + l, 0.0f, 0.03f, v);
        gfx_pal_hsv(f, BASE + C_SNOW * FOG + l, 0.60f, 0.16f, v * 0.86f);
        gfx_pal_hsv(f, BASE + C_HOLLOW * FOG + l, skyh, 0.42f, 0.86f - 0.24f * x);             /* the sky showing through: lighter in the distance, as mist */
        gfx_pal_hsv(f, BASE + C_RIMR * FOG + l, rh, 0.80f, 0.25f + 0.75f * x);
        gfx_pal_hsv(f, BASE + C_RIMG * FOG + l, gh, 0.70f, 0.25f + 0.75f * x);
        gfx_pal_hsv(f, BASE + C_STAR * FOG + l, 0.0f, 0.05f, 0.30f + 0.70f * x);
    }
    gfx_pal_hsv(f, I_VOID, skyh, 0.16f, 0.90f);                                             /* where the tunnel ends, a light of snow */
    gfx_pal_hsv(f, I_GAP, 0.5f, 0.8f, 0.34f);
    gfx_pal_hsv(f, I_GOLD, ho_h, 0.88f, 0.92f);                                               /* the garland on the ring edges: the hollow colour, so both are always about */
    gfx_pal_hsv(f, I_PLAYER, 0.0f, 0.04f, 1.0f);
    gfx_pal_hsv(f, I_PHALO, fr_h, 0.85f, 0.65f);
    gfx_pal_hsv(f, I_YOU, fr_h, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_NOT, ho_h, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_RING, ho_h, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_PLATE, 0.5f, 0.8f, 0.15f);
    gfx_pal_hsv(f, I_TEXT, 0.0f, 0.05f, S.state == ST_PLAY ? 0.95f : 0.85f + 0.15f * m_sin(t * 2.5f));
}

static void build_pal(Frame *f) {
    if (g_theme.on) { build_pal_xmas(f); return; }
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
    gfx_pal_hsv(f, I_GOLD, GOLD_RING, 0.85f, 0.95f);          /* ribbon */
    gfx_pal_hsv(f, I_PLAYER, ph, 0.25f, 1.0f);
    gfx_pal_hsv(f, I_PHALO, ph, 0.85f, 0.55f);
    gfx_pal_hsv(f, I_YOU, ph, 0.85f, 1.0f);
    gfx_pal_hsv(f, I_NOT, ph + 0.5f, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_RING, ph + 0.5f, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_PLATE, ph, 0.5f, 0.045f);
    gfx_pal_hsv(f, I_TEXT, ph, 0.18f, S.state == ST_PLAY ? 0.9f : 0.78f + 0.22f * m_sin(t * 2.5f));
}

static void plate(Frame *f, int x, int y, int w, int h) { gfx_rect(f, x, y, w, h, I_PLATE); }

/* In season: red cells are candy canes, green ones holly with berries, white ones snow, and a hollow one is an empty space
 * rimmed (and striped) in the colour you may not touch, with the odd star beyond it. Patterns are laid in the cell's own
 * coordinates (u across it, fr down it), so they run with the tunnel. */
/* snow that falls through the empty spaces and the far end of the tunnel (never over a solid cell: it must not hide anything).
 * Three speeds of column give it depth, and `off`, how far it has fallen, is what your speed drives. */
static int flake(int x, int y, int off) {
    if (((x * 37) & 7) >= 3) return 0;
    int sp = 2 + (x % 3), fy = y - ((off * sp) >> 1), m = (((fy + (x * 53) % 31) % 31) + 31) % 31;
    return m == 0 || (m == 1 && (x & 1));
}

static void render_xmas(Frame *f, int z, int roti) {
    int off = (int)S.snow, snowix = BASE + C_STAR * FOG + FOG - 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x, fg = tun_f[i];
            if (fg == 255) { f->pix[i] = flake(x, y, off) ? snowix : I_VOID; continue; }
            int a = (tun_a[i] + roti) & 255, wz = tun_d[i] + z, fr = wz & 255, u = a & 15;
            if (fr < tun_rg[i] || (u < tun_ag[i] && fg > 2)) { f->pix[i] = (uint8_t)(fg > 5 ? I_GOLD : fg > 1 ? I_GAP : I_VOID); continue; }
            int c = pat[(wz >> 8) & (NRING - 1)][a >> 4], lvl = fr < 40 ? fg + 1 : fg, j;
            if (c & K_HOLLOW) {
                int rim = u < 2 || u > 13 || fr < 20 || fr > 235;
                if (rim) j = (((u + (fr >> 3)) >> 1) & 1) ? ((c & 3) == K_RED ? C_RIMR : C_RIMG) : C_HOLLOW;
                else if (flake(x, y, off)) { f->pix[i] = (uint8_t)snowix; continue; }
                else j = C_HOLLOW;
            } else switch (c & 3) {
                case K_RED:   j = (((u + (fr >> 3)) >> 2) & 1) ? C_WHITE : C_RED; break;
                case K_GREEN: j = (u >= 6 && u <= 9 && fr >= 96 && fr <= 126) ? C_RED : (((u >> 1) + (fr >> 4)) & 1) ? C_GREEN : C_DGREEN; break;
                default:      j = ((a * 29 + (wz >> 2) * 53) & 15) == 0 ? C_SNOW : C_WHITE; break;   /* snow: white, with a few flecks */
            }
            if (lvl > FOG - 1) lvl = FOG - 1;
            f->pix[i] = (uint8_t)(BASE + j * FOG + lvl);
        }
}

static void render(Frame *f) {
    build_pal(f);
    if (g_theme.on) gfx_set_style(BASE + C_RED * FOG + 9, BASE + C_WHITE * FOG + 9, I_PLATE, I_PLATE);   /* candy-striped lettering, outlined in holly green */
    int z = (int)S.zf, roti = (int)m_floor(S.rot) & 255, xm = g_theme.on;
    if (xm) render_xmas(f, z, roti);
    else
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x, fg = tun_f[i];
            if (fg == 255) { f->pix[i] = I_VOID; continue; }
            int a = (tun_a[i] + roti) & 255, wz = tun_d[i] + z, fr = wz & 255;
            if (fr < tun_rg[i] || ((a & 15) < tun_ag[i] && fg > 2)) { f->pix[i] = (uint8_t)(fg > 1 ? I_GAP : I_VOID); continue; }
            int lvl = fr < 40 ? fg + 1 : fg;                 /* a lit leading edge to each ring */
            int hue = pat[(wz >> 8) & (NRING - 1)][a >> 4];
            if (lvl > FOG - 1) lvl = FOG - 1;
            f->pix[i] = (uint8_t)(BASE + hue * FOG + lvl);
        }
    char num[16], line[32];
    if (S.state != ST_DEAD || S.dead_t < 0.35) {
        gfx_disc(f, PX, PY, 8, I_PHALO); gfx_disc(f, PX, PY, 4, I_PLAYER);
        if (xm) for (int k = 5; k <= 9; k++) { gfx_px(f, PX - k, PY, I_PLAYER); gfx_px(f, PX + k, PY, I_PLAYER); gfx_px(f, PX, PY - k, I_PLAYER); gfx_px(f, PX, PY + k, I_PLAYER); }   /* a star */
    }
    if (!xm && (S.state == ST_PLAY || S.state == ST_DEAD)) {          /* the colour swatches: not in Caroling Run, where what kills is the empty space */
        plate(f, 3, 3, 46, 30);
        gfx_rect(f, 6, 6, 10, 10, I_YOU); gfx_text8(f, 20, 7, "YOU", I_TEXT, 1);
        gfx_rect(f, 6, 20, 10, 10, I_NOT); gfx_text8(f, 20, 21, xm ? "HOLLOW" : "NOT", I_TEXT, 1);
    }
    if (S.state == ST_PLAY) {
        gfx_itoa(S.score, num);
        { int pw = 8 * (int)strlen(num) + 8; if (xm && pw < 64) pw = 64; plate(f, W / 2 - pw / 2, 3, pw, xm ? 30 : 20); }   /* in season it also holds "VERSE n" */
        gfx_text16_c(f, 5, num, I_TEXT, 1);
        if (xm) { char vb[16] = "VERSE "; int k = 6; char vn[8]; gfx_itoa(S.verse + 1, vn); for (char *q = vn; *q; q++) vb[k++] = *q; vb[k] = 0; gfx_text8_c(f, 22, vb, I_TEXT, 1); }
        if (xm && S.banner_t > 0.0) {                         /* a banner as a verse begins */
            int bw = 8 * (int)strlen(S.banner) * 2 + 16;
            plate(f, (W - bw) / 2, 80, bw, 30);
            gfx_text16_c(f, 87, S.banner, I_TEXT, 2);
        }
    } else if (S.state == ST_DEAD) {
        int r = (int)(S.dead_t * 130.0);
        if (xm) gfx_gifts(f, S.dead_t, BASE + C_RED * FOG + 9, BASE + C_GREEN * FOG + 9, BASE + C_WHITE * FOG + 9, BASE + C_STAR * FOG + 9);   /* a heap of presents and ornaments, for a laugh */
        else { gfx_ring(f, PX, PY, r, I_RING); gfx_ring(f, PX, PY, r + 1, I_RING); }
        plate(f, 60, 54, 200, 108);
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 62, g_theme.on ? "NOTES SUNG" : "SCORE", I_TEXT, 1);
        gfx_text16_c(f, 74, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 114, line, I_TEXT, 1);
        if (S.dead_t > 0.6) gfx_text8_c(f, 140, "PRESS TO RETRY", I_TEXT, 1);
    } else {
        if (xm) { gfx_garland(f, S.clock, BASE + C_RED * FOG + 9, BASE + C_GREEN * FOG + 9, I_TEXT, BASE + C_DRED * FOG + 5); gfx_sparkles(f, S.clock, I_TEXT); }
        plate(f, 24, 10, 272, 52); plate(f, 10, 112, 300, 72);
        if (xm) {
            gfx_text16_c(f, 13, "CAROLING RUN", I_TEXT, 2);
            gfx_text8_c(f, 38, "Spread Christmas cheer while", I_TEXT, 1);
            gfx_text8_c(f, 49, "avoiding obstacles.", I_TEXT, 1);
            gfx_text8_c(f, 120, "Empty spaces are the obstacles", I_TEXT, 1);
            gfx_text8_c(f, 140, "Steer: keys, stick, mouse or touch", I_TEXT, 1);
            gfx_text8_c(f, 158, "Press any key", I_TEXT, 1);
        } else {
            gfx_text16_c(f, 14, "PRISM RUN", I_TEXT, 2);
            gfx_text8_c(f, 50, "A FLIGHT DOWN THE COLOUR WHEEL", I_TEXT, 1);
            gfx_text8_c(f, 118, "YOUR COLOUR DRIFTS AS YOU FLY", I_TEXT, 1);
            gfx_text8_c(f, 130, "NEVER TOUCH ITS OPPOSITE", I_TEXT, 1);
            gfx_text8_c(f, 148, "STEER: KEYS / STICK / MOUSE / TOUCH", I_TEXT, 1);
            gfx_text8_c(f, 162, "PRESS ANY KEY", I_TEXT, 1);
        }
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
        safe0[s] = !lethal_cell(pat[c0 & (NRING - 1)][s], p);
        safe1[s] = !lethal_cell(pat[c1 & (NRING - 1)][s], p1);
    }
    for (int pass = 0; pass < 2 && best == -1; pass++)       /* first: follow your own colour (the way through); failing that, any safe cell */
        for (int s = 0; s < SEGS; s++) {
            if (!safe0[s] || !safe1[s]) continue;
            if (pass == 0 && g_theme.on) {                                  /* in season: the way through (two cells) of both rings, whichever colour it is */
                int l0 = lane_dbg[c0 & (NRING - 1)], l1 = lane_dbg[c1 & (NRING - 1)];
                if (!((s == l0 || s == (l0 + 1) % SEGS) && (s == l1 || s == (l1 + 1) % SEGS))) continue;
            }
            if (pass == 0 && !g_theme.on && (circ((float)pat[c0 & (NRING - 1)][s], p) > 0.8f || circ((float)pat[c1 & (NRING - 1)][s], p1) > 0.8f)) continue;
            int ds = (s - cur + 24) % SEGS - 8, dir = ds < 0 ? -1 : 1, ok = 1;     /* the shorter way round, every cell on it safe now */
            for (int k = cur, n = 0; n <= (ds < 0 ? -ds : ds); n++, k = (k + dir + SEGS) % SEGS) if (!safe0[k]) ok = 0;
            if (!ok) continue;
            int d = (s * 16 + 8 - under + 384) % 256 - 128;
            if ((d < 0 ? -d : d) < bd) { bd = d < 0 ? -d : d; best = d; }
        }
    if (best == -1) return 0.0f;
    return -clampf((float)best / 20.0f, -1.0f, 1.0f);         /* rolling the world by +d moves the ship d: the axis is minus that */
}
