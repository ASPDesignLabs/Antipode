/* Antipode - Moth, on the 256-colour engine.
 *
 * You are a point of light in a dark that answers in your opposite. Blobs of anti-light drift in and chase you; touch
 * one and you are gone. They are metaballs: where two come near, their fields add and a bridge grows between them, and
 * when they touch they merge into one bigger, slower blob. Lure them together and slip round what is left. A merge is a
 * point; the dark sends more the longer you last.
 *
 * Nothing is drawn per blob. Each blob is *splatted* into a field through a precomputed radial kernel,
 *     field(p) = sum over blobs of  k(|p - blob| / R)        k(u) = (1 - u^2)^2, R = 2.2 radius, scaled so that the
 *                                                           surface (field = 1) sits at the blob's radius,
 * so two blobs' bridge is just the sum of their two skirts. Your own light is a second small field. A pixel is then
 * one palette index made of both, quantised:
 *
 *     index = BASE + level(your light) * 20 + level(the dark's field)         12 x 20 = 240 entries
 *
 * and the palette decides what that looks like: a soft aura in the opposite colour below the surface, a bright rim
 * at it, a solid body inside, your hue where your light falls, and the hues between where the two overlap. The
 * lethal test asks the field under the point of light. */
#include "core.h"
#include "gfx.h"
#include "snd.h"

#define W 320
#define H 200
#define LA 12                       /* levels of your light */
#define LB 20                       /* levels of the dark's field: 10 is the surface */
#define BASE 16                     /* BASE + LA * LB = 256 */
#define SURF 64                     /* the field's unit: the surface is at 64 */
#define LIGHT_R 50                  /* how far your light reaches, pixels */
#define MAXB 12
#define RMAX 32.0f                  /* a blob this big no longer merges: it is as big as the dark gets */

enum { I_CORE = 1, I_WING = 2, I_YOU = 3, I_NOT = 4, I_RING = 5, I_PLATE = 6, I_GOLD = 7, I_NOSE = 8, I_COAL = 9, I_TEXT = 15 };
enum { ST_MENU, ST_PLAY, ST_DEAD };

enum { KD_SNOW, KD_COAL, KD_BOSS };                                    /* in season: snow, a heavy lump of coal, the Grand Snowman */
typedef struct { float x, y, vx, vy, r, sp; int kind; } Blob;           /* sp: how fast it is, against a plain one */
typedef struct { float x, y, t; int kind; } Gift;                      /* in season: a present, for a while */
enum { FX_NONE = -1, FX_COCOA, FX_CANE, FX_MAGNET };
#define SNOWMAN_R 24.0f             /* in season, a blob this big is a finished snowman */

static struct {
    uint32_t rng;
    float hue;
    int state, score, best, nb;
    float x, y, vx, vy;
    Blob b[MAXB];
    double clock, dead_t, spawn_t, life;
    Gift g[3]; int ng;              /* presents lying about */
    int fx, combo, snowmen;         /* what a present has given you; merges in a row; snowmen finished */
    double fx_t, last_merge, msg_t, gift_t, fx_max;
    const char *msg;
    int diff, tier, boss_hp, boss_done, bells, stick;   /* the list you chose, the step you have reached, the boss, the last bells left */
    double invuln, boss_shed;
} S;

static uint16_t acc[W * H];         /* the dark's field */
static uint8_t lum[W * H];          /* your light, in levels */
static uint8_t ktab[257];           /* the kernel against d^2 / R^2, in 256ths */
static uint8_t lbq[1024];           /* field value -> level */
static uint8_t ltab[257];           /* your light's falloff, in levels */

static float rf(void) { return rng_f(&S.rng); }

static void build_tables(void) {
    float a = 1.0f - 1.0f / (2.2f * 2.2f);
    float norm = 1.0f / (a * a);                              /* so that the field is 1 at d = r */
    for (int i = 0; i <= 256; i++) {
        float u = (float)i / 256.0f, t = 1.0f - u;
        ktab[i] = (uint8_t)clampi((int)(norm * t * t * (float)SURF + 0.5f), 0, 255);
        ltab[i] = (uint8_t)clampi((int)((float)(LA - 1) * t * t + 0.5f), 0, LA - 1);
    }
    for (int i = 0; i < 1024; i++) lbq[i] = (uint8_t)clampi(i * 10 / SURF, 0, LB - 1);
}

/* add one blob's skirt into the field */
static void splat(const Blob *b) {
    float R = b->r * 2.2f;
    int cx = (int)(b->x + 0.5f), cy = (int)(b->y + 0.5f), Ri = (int)R + 1;
    uint32_t inv = (uint32_t)((256.0f * 65536.0f) / (R * R));
    int y0 = cy - Ri < 0 ? 0 : cy - Ri, y1 = cy + Ri >= H ? H - 1 : cy + Ri;
    int x0 = cx - Ri < 0 ? 0 : cx - Ri, x1 = cx + Ri >= W ? W - 1 : cx + Ri;
    int R2 = (int)(R * R);
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy, dy2 = dy * dy;
        uint16_t *row = acc + y * W;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, d2 = dx * dx + dy2;
            if (d2 >= R2) continue;
            row[x] = (uint16_t)(row[x] + ktab[(uint32_t)((uint32_t)d2 * inv) >> 16]);
        }
    }
}
static void splat_light(float fx, float fy, int R) {
    int cx = (int)(fx + 0.5f), cy = (int)(fy + 0.5f);
    uint32_t inv = (uint32_t)((256.0f * 65536.0f) / (float)(R * R));
    int y0 = cy - R < 0 ? 0 : cy - R, y1 = cy + R >= H ? H - 1 : cy + R;
    int x0 = cx - R < 0 ? 0 : cx - R, x1 = cx + R >= W ? W - 1 : cx + R;
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, d2 = dx * dx + dy * dy;
            if (d2 >= R * R) continue;
            lum[y * W + x] = ltab[(uint32_t)((uint32_t)d2 * inv) >> 16];
        }
    }
}
static void build_field(void) {
    memset(acc, 0, sizeof acc);
    memset(lum, 0, sizeof lum);
    for (int i = 0; i < S.nb; i++) splat(&S.b[i]);
    splat_light(S.x, S.y, LIGHT_R + (g_theme.on && S.fx == FX_COCOA ? 14 + 4 * S.tier : 0) + (g_theme.on ? (int)(2.0f * m_sin((float)S.clock * 11.0f) + 1.5f * m_sin((float)S.clock * 23.0f)) : 0));   /* a lantern's flame is never still */
}
static int field_at(float x, float y) { int ix = clampi((int)(x + 0.5f), 0, W - 1), iy = clampi((int)(y + 0.5f), 0, H - 1); return acc[iy * W + ix]; }

/* ---- the game ---- */
/* In season the night is climbed in steps. Each is reached by score, and each asks more: faster, more of them, and new things sent.
 * Nice List is the gentler way up (later steps, slower, more presents, two last bells); Naughty List asks sooner and harder and pays
 * half as much again. The last step needs the boss to be beaten first. */
static const int THR_NICE[5] = { 0, 15, 40, 80, 150 }, THR_NAUGHTY[5] = { 0, 9, 24, 48, 90 };
static const char *const TIER_NAME[5] = { "Little Nibbles", "Stocking Stuffers", "Naughty List", "Christmas Eve", "Midnight" };
static const char *const TIER_BANNER[5] = { "", "Stocking Stuffers!", "The Naughty List!", "Christmas Eve: the Grand Snowman!", "Midnight!" };
static const float GAP_T[5] = { 4.2f, 3.6f, 3.0f, 2.6f, 2.2f };
static const int MUSIC_LAYERS[5] = { 0, ML_SHAKER, ML_SHAKER | ML_SHIMMER, ML_SHAKER | ML_SHIMMER | ML_DRUMS | ML_BRASS, ML_ALL };
static const int MUSIC_KEY[5] = { 0, 2, 4, 5, 7 };

static float dm_gap(void) { return S.diff ? 0.85f : 1.0f; }
static float dm_spd(void) { return S.diff ? 1.12f : 1.0f; }
static void add_score(int pts) { S.score += g_theme.on && S.diff ? (pts * 3 + 1) / 2 : pts; }
static int tier_for(int score) {
    const int *th = S.diff ? THR_NAUGHTY : THR_NICE;
    int t = 0;
    for (int i = 1; i < 5; i++) if (score >= th[i]) t = i;
    return t == 4 && !S.boss_done ? 3 : t;
}
static Blob *boss(void) { for (int i = 0; i < S.nb; i++) if (S.b[i].kind == KD_BOSS) return &S.b[i]; return 0; }

static float spawn_gap(void) {
    if (!g_theme.on) { float v = 4.2f - 0.07f * (float)S.life; return v < 1.7f ? 1.7f : v; }
    float v = GAP_T[S.tier] * dm_gap() - 0.015f * (float)(S.life < 60.0 ? S.life : 60.0);
    return v < 1.3f ? 1.3f : v;
}
static float vmax_for(const Blob *b) {                          /* bigger is slower; all of them quicken as the night goes on */
    float base = g_theme.on ? (24.0f + 6.0f * (float)S.tier) * dm_spd() + 0.15f * (float)(S.life < 60.0 ? S.life : 60.0)
                            : 26.0f + 0.35f * (float)(S.life < 70.0 ? S.life : 70.0);
    return base * clampf(1.0f - 0.016f * (b->r - 9.0f), 0.35f, 1.0f) * b->sp;
}

static void spawn_blob(void) {
    if (S.nb >= MAXB) return;
    Blob *b = &S.b[S.nb];
    for (int tries = 0; tries < 20; tries++) {
        float t = rf();
        int side = (int)(rf() * 4.0f);
        float x = side == 0 ? -24.0f : side == 1 ? W + 24.0f : t * W, y = side == 2 ? -24.0f : side == 3 ? H + 24.0f : t * H;
        float dx = x - S.x, dy = y - S.y;
        if (dx * dx + dy * dy > 110.0f * 110.0f || tries == 19) { b->x = x; b->y = y; break; }
    }
    b->r = 8.0f + rf() * 4.0f; b->vx = b->vy = 0.0f; b->sp = 1.0f; b->kind = KD_SNOW;
    if (g_theme.on) {                                           /* the list sends different things as the steps go up */
        float pick = rf();
        if (S.tier >= 2 && pick < 0.22f) { b->kind = KD_COAL; b->r = 13.0f + rf() * 3.0f; b->sp = 0.65f; }                  /* heavy lumps of coal */
        else if (S.tier >= 1 && pick < (S.tier >= 2 ? 0.22f : 0.0f) + 0.12f + 0.04f * (float)S.tier) { b->r = 6.5f; b->sp = 1.5f; }   /* small quick icicles */
    }
    S.nb++;
}

static void reset_run(void) {
    S.hue = theme_base_hue(&S.rng);
    S.score = 0; S.nb = 0; S.life = 0.0; S.spawn_t = 2.0;
    S.x = W * 0.5f; S.y = H * 0.5f; S.vx = S.vy = 0.0f;
    S.ng = 0; S.fx = FX_NONE; S.fx_t = 0.0; S.combo = 0; S.last_merge = -9.0; S.snowmen = 0; S.msg_t = 0.0; S.gift_t = 14.0;
    S.tier = 0; S.boss_done = 0; S.boss_hp = 0; S.bells = S.diff ? 1 : 2; S.invuln = 0.0; S.boss_shed = 0.0; S.fx_max = 6.0;
    spawn_blob(); spawn_blob();
}
static void music_progress(void);
static void begin(void) { reset_run(); S.state = ST_PLAY; snd_sfx(SFX_START); snd_music_play(MU_WISH); if (g_theme.on) music_progress(); }

static void enter(uint32_t seed) {
    build_tables();
    S.rng = seed ? seed : 1u;
    S.state = ST_MENU; S.clock = 0.0; S.dead_t = 0.0;
    S.best = best_get();
    S.diff = g_theme.on ? (pref_get(0) ? 1 : 0) : 0;
    S.stick = 0;
    S.hue = theme_base_hue(&S.rng);
    S.nb = 0; S.x = W * 0.5f; S.y = H * 0.5f; S.vx = S.vy = 0.0f; S.life = 0.0;
    for (int i = 0; i < 4; i++) {                               /* behind the title: a few drifting, unhurried */
        Blob *b = &S.b[S.nb++];
        b->x = 30.0f + rf() * (W - 60); b->y = 30.0f + rf() * (H - 60); b->r = 9.0f + rf() * 6.0f; b->sp = 1.0f; b->kind = KD_SNOW;
        float a = rf() * TAU_F; b->vx = m_cos(a) * 9.0f; b->vy = m_sin(a) * 9.0f;
    }
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

/* in season: presents, and what the steps do */
static void spawn_gift_kind(float x, float y, int kind) {
    if (S.ng >= 3) { for (int i = 1; i < 3; i++) S.g[i - 1] = S.g[i]; S.ng = 2; }
    Gift *g = &S.g[S.ng++];
    g->x = clampf(x, 14.0f, W - 14.0f); g->y = clampf(y, 24.0f, H - 24.0f); g->t = 12.0f; g->kind = kind;
}
static void spawn_gift(float x, float y) { spawn_gift_kind(x, y, (int)(rng_next(&S.rng) % 3u)); }

static void music_progress(void) {                               /* the carol grows with the steps: more bells, a shimmer, drums and brass; a higher key */
    snd_music_layers(MUSIC_LAYERS[S.tier]);
    snd_music_bump(MUSIC_KEY[S.tier] + (S.score / 12 > 3 ? 3 : S.score / 12));
}

static void spawn_boss(void) {
    if (S.nb >= MAXB) S.nb = MAXB - 1;
    Blob *b = &S.b[S.nb++];
    b->kind = KD_BOSS; b->r = 30.0f; b->sp = 0.75f; b->vx = b->vy = 0.0f;
    b->x = S.x < W * 0.5f ? W + 36.0f : -36.0f; b->y = rf() * H;
    S.boss_hp = 3; S.boss_shed = 3.5;
}

static void tier_up(int t) {                                     /* a banner, a fanfare, a present near you for the new step, and a bigger carol */
    S.tier = t;
    S.msg = TIER_BANNER[t]; S.msg_t = 2.6;
    snd_sfx(SFX_TIER);
    float a = rf() * TAU_F;
    spawn_gift(S.x + m_cos(a) * 55.0f, S.y + m_sin(a) * 55.0f);
    if (t == 3 && !S.boss_done && !boss()) spawn_boss();
    if (t == 4) snd_music_play(MU_JINGLE);                       /* the finale: Jingle Bells, all of it */
    music_progress();
}
static void check_tier(void) { int t = tier_for(S.score); while (S.tier < t) tier_up(S.tier + 1); }

static void last_bell(void) {                                    /* about to be caught: a bell rings, the list freezes and is pushed back */
    S.bells--; S.invuln = 1.4; S.fx = FX_CANE; S.fx_max = S.fx_t = 4.0;
    for (int i = 0; i < S.nb; i++) {
        float dx = S.b[i].x - S.x, dy = S.b[i].y - S.y, d = m_sqrt(dx * dx + dy * dy) + 0.001f;
        if (d < 110.0f) { float push = 110.0f - d + 20.0f; S.b[i].x += dx / d * push; S.b[i].y += dy / d * push; S.b[i].vx = S.b[i].vy = 0.0f; }
    }
    S.msg = "Last bell! The list freezes!"; S.msg_t = 2.2;
    snd_sfx(SFX_TIER);
}

static void defeat_boss(void) {
    for (int i = 0; i < S.nb; i++) if (S.b[i].kind == KD_BOSS) {
        for (int k = 0; k < 3; k++) spawn_gift_kind(S.b[i].x + (float)(k - 1) * 22.0f, S.b[i].y, k);   /* one of each present */
        S.b[i] = S.b[--S.nb]; break;
    }
    S.boss_done = 1; S.boss_hp = 0;
    add_score(25);
    S.msg = "The Grand Snowman melts! +25"; S.msg_t = 2.8;
    snd_sfx(SFX_TIER);
}

/* A snowman that reaches full size is finished: a bonus, a fanfare, a present, and it waddles off. While the Grand Snowman is
 * about, each one pelts it with snowballs; the third melts it. */
static void finish_snowmen(void) {
    for (int i = 0; i < S.nb; i++) {
        if (S.b[i].kind != KD_SNOW || S.b[i].r < SNOWMAN_R) continue;
        spawn_gift(S.b[i].x, S.b[i].y);
        add_score(5); S.snowmen++;
        S.b[i] = S.b[--S.nb];
        Blob *bs = boss();
        if (bs) {
            S.boss_hp--; bs->r = 18.0f + 4.0f * (float)S.boss_hp;
            if (S.boss_hp <= 0) defeat_boss(); else { S.msg = "A snowman pelts the Grand Snowman!"; S.msg_t = 2.0; snd_sfx(SFX_START); }
        } else { S.msg = "A snowman! +5"; S.msg_t = 1.8; snd_sfx(SFX_START); }
        i = -1;                                                  /* look again: the list has moved up */
    }
}

static void merge_blobs(void) {                                 /* touching blobs become one, by area, keeping their momentum */
    for (int i = 0; i < S.nb; i++)
        for (int j = i + 1; j < S.nb; j++) {
            Blob *a = &S.b[i], *b = &S.b[j];
            float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy, rs = (a->r + b->r) * 0.78f;
            if (d2 >= rs * rs) continue;
            float nr2 = a->r * a->r + b->r * b->r;
            int melt = 0;
            if (g_theme.on) {
                if (a->kind == KD_BOSS || b->kind == KD_BOSS) nr2 = 1e9f;                          /* the Grand Snowman does not merge: it only pushes */
                else if ((a->kind == KD_COAL) != (b->kind == KD_COAL)) {                           /* coal melts into what it touches, half its size */
                    const Blob *c = a->kind == KD_COAL ? a : b, *o = c == a ? b : a;
                    nr2 = o->r * o->r + 0.5f * c->r * c->r; melt = 1;
                }
            }
            if (nr2 > RMAX * RMAX) {                            /* as big as it gets: they only push apart */
                float d = m_sqrt(d2) + 0.001f, push = (rs - d) * 0.5f;
                a->x -= dx / d * push; a->y -= dy / d * push; b->x += dx / d * push; b->y += dy / d * push;
                continue;
            }
            float wa = a->r * a->r, wb = b->r * b->r, w = wa + wb;
            float nsp = melt ? (a->kind == KD_COAL ? b->sp : a->sp) : (a->sp * wa + b->sp * wb) / w;
            int nkind = melt ? KD_SNOW : a->kind;
            a->x = (a->x * wa + b->x * wb) / w; a->y = (a->y * wa + b->y * wb) / w;
            a->vx = (a->vx * wa + b->vx * wb) / w; a->vy = (a->vy * wa + b->vy * wb) / w;
            a->sp = nsp; a->kind = nkind;
            a->r = m_sqrt(nr2);
            *b = S.b[--S.nb];
            if (S.state == ST_PLAY) {
                int pts = 1;
                if (g_theme.on) {                                       /* merges close together build a combo: 1, 2, 3 ... up to 5 a merge */
                    S.combo = S.clock - S.last_merge < 2.5 ? S.combo + 1 : 1;
                    S.last_merge = S.clock; pts = S.combo > 5 ? 5 : S.combo;
                }
                add_score(pts);
                if (melt) { add_score(2); S.msg = "Coal melted! +2"; S.msg_t = 1.4; }
                snd_sfx_note(SFX_PICKUP, S.score);
                if (g_theme.on) music_progress(); else snd_music_bump(S.score > 7 ? 7 : S.score);   /* each merge lifts the carol a little */
            }
            j = i;                                              /* look again from the start of this one */
        }
}

static void move_blobs(double dt, int chase) {
    float f = (float)dt;
    for (int i = 0; i < S.nb; i++) {
        Blob *b = &S.b[i];
        if (chase) {
            float dx = S.x - b->x, dy = S.y - b->y, d = m_sqrt(dx * dx + dy * dy) + 0.001f;
            float ch = g_theme.on && S.fx == FX_MAGNET ? 0.35f : 1.0f;       /* a magnet: they care more about each other than about you */
            if (g_theme.on && S.fx == FX_CANE) { b->vx -= b->vx * 6.0f * f; b->vy -= b->vy * 6.0f * f; b->x += b->vx * f; b->y += b->vy * f; continue; }   /* a candy cane: they stop where they are */
            b->vx += dx / d * 80.0f * ch * f; b->vy += dy / d * 80.0f * ch * f;
            if (g_theme.on && S.fx == FX_MAGNET)
                for (int j = 0; j < S.nb; j++) {
                    if (j == i) continue;
                    float ex = S.b[j].x - b->x, ey = S.b[j].y - b->y, ed = m_sqrt(ex * ex + ey * ey) + 0.001f;
                    if (ed < 170.0f) { b->vx += ex / ed * 190.0f * f; b->vy += ey / ed * 190.0f * f; }
                }
            b->vx -= b->vx * 0.7f * f; b->vy -= b->vy * 0.7f * f;
            float sp = m_sqrt(b->vx * b->vx + b->vy * b->vy), mx = vmax_for(b);
            if (sp > mx) { b->vx *= mx / sp; b->vy *= mx / sp; }
        } else {                                                /* idle: wander, bounce off the edges */
            if ((b->x < 20.0f && b->vx < 0.0f) || (b->x > W - 20.0f && b->vx > 0.0f)) b->vx = -b->vx;
            if ((b->y < 20.0f && b->vy < 0.0f) || (b->y > H - 20.0f && b->vy > 0.0f)) b->vy = -b->vy;
        }
        b->x += b->vx * f; b->y += b->vy * f;
    }
}

static void menu_choose(int d) { S.diff = d; pref_set(0, d); snd_sfx_note(SFX_PICKUP, 2 + 3 * d); }

/* a tap on one of the two bars on the title (x 40..280; Nice List at y 106..119, Naughty List at 122..135) */
static int moth_tap(float x, float y) {
    if (!g_theme.on || S.state != ST_MENU || x < 40.0f || x > 280.0f) return 0;
    if (y >= 106.0f && y <= 119.0f) { menu_choose(0); return 1; }
    if (y >= 122.0f && y <= 135.0f) { menu_choose(1); return 1; }
    return 0;
}

static void update(double dt, const Input *in) {
    S.clock += dt;
    if (S.state == ST_MENU) {
        move_blobs(dt, 0);
        merge_blobs();
        if (S.nb < 3) { S.b[S.nb] = (Blob){ 30.0f + rf() * (W - 60), 30.0f + rf() * (H - 60), 0.0f, 0.0f, 10.0f, 1.0f, KD_SNOW }; S.nb++; }
        if (g_theme.on) {                                       /* the title chooses the list: up and down, a pad's d-pad or stick, or a tap on a bar */
            int dir = in->pad ? (in->ly > 0.6f ? -1 : in->ly < -0.6f ? 1 : 0) : 0;
            int up = in->hit[KEY_UP] || in->hit[KEY_W] || in->pad_hit[PAD_UP] || (dir == -1 && S.stick != -1);
            int dn = in->hit[KEY_DOWN] || in->hit[KEY_S] || in->pad_hit[PAD_DOWN] || (dir == 1 && S.stick != 1);
            S.stick = dir;
            if (up && S.diff != 0) menu_choose(0);
            if (dn && S.diff != 1) menu_choose(1);
        }
        if (in->act) begin();
        return;
    }
    if (S.state == ST_DEAD) {
        S.dead_t += dt;
        move_blobs(dt, 0);
        merge_blobs();
        if (in->act && S.dead_t > 0.6) begin();
        return;
    }
    S.life += dt;
    if (g_theme.on) snd_music_tempo(0.95f + 0.07f * (float)S.tier + 0.004f * (float)(S.life < 50.0 ? S.life : 50.0));   /* it quickens with the steps */
    else snd_music_tempo(0.9f + 0.012f * (float)(S.life < 50.0 ? S.life : 50.0));
    float dx, dy, mag, tvx = 0.0f, tvy = 0.0f;
    float spd = g_theme.on && S.fx == FX_COCOA ? 72.0f * 1.45f : 72.0f;                 /* hot cocoa: quicker */
    if (steer(in, &dx, &dy, &mag)) { tvx = dx * mag * spd; tvy = dy * mag * spd; }
    float k = clampf((float)dt * 9.0f, 0.0f, 1.0f);
    S.vx += (tvx - S.vx) * k; S.vy += (tvy - S.vy) * k;
    S.x = clampf(S.x + S.vx * (float)dt, 6.0f, W - 6.0f);
    S.y = clampf(S.y + S.vy * (float)dt, 6.0f, H - 6.0f);

    S.spawn_t -= dt;
    if (S.spawn_t <= 0.0) { spawn_blob(); S.spawn_t = spawn_gap(); }
    if (g_theme.on) {                                           /* presents: left by snowmen, and now and then by the list itself */
        S.gift_t -= dt;
        if (S.gift_t <= 0.0) { spawn_gift(30.0f + rf() * (W - 60), 30.0f + rf() * (H - 60)); S.gift_t = (24.0 + rf() * 8.0) * (S.diff ? 1.3 : 1.0); }
        for (int i = 0; i < S.ng; i++) {
            S.g[i].t -= (float)dt;
            float gx = S.x - S.g[i].x, gy = S.y - S.g[i].y;
            if (S.g[i].t <= 0.0f) { S.g[i] = S.g[--S.ng]; i--; continue; }
            if (gx * gx + gy * gy < 10.0f * 10.0f) {
                S.fx = S.g[i].kind; S.fx_max = S.fx_t = 6.0 + (S.fx == FX_COCOA ? 1.5 * (double)S.tier : 0.0);   /* cocoa lasts longer higher up */
                S.msg = S.fx == FX_COCOA ? "Hot cocoa: you're quicker!" : S.fx == FX_CANE ? "Candy cane: they freeze!" : "Magnet: they stick together!"; S.msg_t = 2.0;
                snd_sfx_note(SFX_PICKUP, 8 + 2 * S.fx);
                S.g[i] = S.g[--S.ng]; i--;
            }
        }
        if (S.fx_t > 0.0) { S.fx_t -= dt; if (S.fx_t <= 0.0) S.fx = FX_NONE; }
        if (S.msg_t > 0.0) S.msg_t -= dt;
    }
    move_blobs(dt, 1);
    merge_blobs();
    if (g_theme.on) {
        finish_snowmen();
        check_tier();
        if (S.invuln > 0.0) S.invuln -= dt;
        Blob *bs = boss();
        if (bs) {                                                /* the Grand Snowman sheds snowballs as it comes */
            S.boss_shed -= dt;
            if (S.boss_shed <= 0.0 && S.nb < MAXB) {
                float ang = rf() * TAU_F;
                S.b[S.nb++] = (Blob){ bs->x, bs->y, m_cos(ang) * 45.0f, m_sin(ang) * 45.0f, 7.0f, 1.1f, KD_SNOW };
                S.boss_shed = S.diff ? 3.0 : 4.0;
            }
        }
    }
    build_field();                                              /* the field the picture and the lethal test both read */
    if (field_at(S.x, S.y) >= SURF + 8) {
        if (g_theme.on && S.invuln > 0.0) { }                    /* the last bell's grace */
        else if (g_theme.on && S.bells > 0) last_bell();
        else die();
    }
}

/* ---- drawing ---- */
static void build_pal(Frame *f) {
    float t = (float)S.clock;
    float flip = S.state == ST_DEAD ? 0.5f * smoothstepf(0.0f, 0.5f, (float)S.dead_t) : 0.0f;
    float h0 = S.hue + flip;
    for (int a = 0; a < LA; a++)
        for (int b = 0; b < LB; b++) {
            float y = (float)a / (LA - 1), yv = y * y * 0.85f;
            float an, s = g_theme.on ? 0.55f : 0.88f;                    /* in season the dark is frost: paler */
            if (b < 10) { float u = (float)b / 10.0f; an = u * u * 0.75f; }                  /* the aura, below the surface */
            else { an = 0.66f + (float)(b - 10) * 0.034f; }                                  /* the body */
            if (b == 10 || b == 11) { s = 0.35f; an = 1.0f; }                                  /* the rim, hot */
            float w = (an + yv) > 0.0f ? an / (an + yv) : 0.0f;
            float hue = h0 + 0.5f * w;                                                        /* the arc from your colour to its opposite */
            float v = (g_theme.on ? 0.20f : 0.012f) + yv * 0.78f + an * 0.80f;
            if (g_theme.on) {                                                                 /* in season the night is a winter blue (deep green for gold and blue), not black */
                float skyh = g_theme.pair ? 0.5f : 0.667f, d = hue - skyh, k = clampf((an + yv) * 5.0f, 0.0f, 1.0f);
                d -= m_floor(d + 0.5f);
                hue = skyh + d * k; if (an + yv < 0.2f) s = 0.50f;
            }
            if (b >= 10) v = 0.30f + 0.65f * an * (b >= 12 ? 0.85f : 1.0f) + yv * 0.2f;
            gfx_pal_hsv(f, BASE + a * LB + b, hue, s, v > 1.0f ? 1.0f : v);
        }
    gfx_pal_hsv(f, I_GOLD, GOLD_RING, 0.85f, 0.95f);
    gfx_pal_hsv(f, I_NOSE, 0.10f, 0.95f, 1.0f);
    gfx_pal_hsv(f, I_COAL, 0.0f, 0.0f, 0.07f);
    gfx_pal_hsv(f, I_CORE, h0, 0.18f, 1.0f);
    gfx_pal_hsv(f, I_WING, h0, 0.6f, 0.9f + 0.1f * m_sin(t * 40.0f));
    gfx_pal_hsv(f, I_YOU, h0, 0.85f, 1.0f);
    gfx_pal_hsv(f, I_NOT, h0 + 0.5f, 0.9f, 1.0f);
    gfx_pal_hsv(f, I_RING, S.hue + 0.5f, 0.7f, 1.0f);
    gfx_pal_hsv(f, I_PLATE, h0, 0.5f, 0.045f);
    gfx_pal_hsv(f, I_TEXT, h0, 0.18f, S.state == ST_PLAY ? 0.9f : 0.78f + 0.22f * m_sin(t * 2.5f));
}

/* in season: your light is a lantern (g: the frame, c: the glass, w: the flame) */
static const char *const LANTERN[10] = {
    "..ggg..", ".g...g.", "ggggggg", "gcccccg", "gccwccg", "gcwwwcg", "gccwccg", "gcccccg", "ggggggg", ".g...g." };

/* a blob that has grown becomes a snowman: eyes of coal, a carrot nose toward you, a smile, buttons, and a hat on the big ones */
static void draw_faces(Frame *f) {
    for (int i = 0; i < S.nb; i++) {
        const Blob *b = &S.b[i];
        if (b->r < 13.0f) continue;
        int bx = (int)(b->x + 0.5f), by = (int)(b->y + 0.5f), r = (int)b->r, big = r >= 18, ex = (int)(b->r * 0.32f), ey = (int)(b->r * 0.22f);
        if (b->kind == KD_COAL) {                                /* a lump of coal: glowing eyes and a scowl, no nose */
            gfx_rect(f, bx - ex - 1, by - ey, 2, 2, I_GOLD); gfx_rect(f, bx + ex - 1, by - ey, 2, 2, I_GOLD);
            for (int k = -3; k <= 3; k++) gfx_px(f, bx + k, by + (int)(b->r * 0.5f) + (k * k > 4 ? 1 : 0), I_GOLD);
            continue;
        }
        gfx_rect(f, bx - ex - (big ? 1 : 0), by - ey, big ? 2 : 1, big ? 2 : 1, I_COAL);
        gfx_rect(f, bx + ex, by - ey, big ? 2 : 1, big ? 2 : 1, I_COAL);
        float dx = S.x - b->x, dy = S.y - b->y, d = m_sqrt(dx * dx + dy * dy) + 0.001f;
        int nl = 2 + (int)(b->r * 0.22f);
        for (int k = 1; k <= nl; k++) gfx_px(f, bx + (int)(dx / d * (float)k), by + 1 + (int)(dy / d * (float)k), I_NOSE);
        for (int k = -2; k <= 2; k++) gfx_px(f, bx + k * (big ? 2 : 1), by + (int)(b->r * 0.46f) - ((k == 2 || k == -2) ? 1 : 0), I_COAL);
        if (big) { gfx_px(f, bx, by + (int)(b->r * 0.72f), I_COAL); gfx_px(f, bx, by + (int)(b->r * 0.88f), I_COAL); }
        if (r >= 19) { gfx_rect(f, bx - r / 2 - 1, by - r + 1, r + 2, 2, I_COAL); gfx_rect(f, bx - r / 3, by - r - 5, r * 2 / 3, 6, I_COAL); }
        if (b->kind == KD_BOSS) gfx_rect(f, bx - r / 3, by - r - 2, r * 2 / 3, 1, I_GOLD);   /* the Grand Snowman's hat-band */
    }
}

static void plate(Frame *f, int x, int y, int w, int h) { gfx_rect(f, x, y, w, h, I_PLATE); }

static void render(Frame *f) {
    build_pal(f);
    if (g_theme.on) gfx_set_style(I_NOT, I_CORE, I_PLATE, I_PLATE);
    if (S.state != ST_PLAY) build_field();                      /* in play the update already did */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x, lb = lbq[acc[i] > 1023 ? 1023 : acc[i]];
            if (g_theme.on && lb >= 12 && (((x + y) >> 2) & 1)) lb = 11;      /* the body is striped, not only bright: a candy-cane dark */
            f->pix[i] = (uint8_t)(BASE + lum[i] * LB + lb);
        }
    char num[16], line[32];
    int hx = (int)(S.x + 0.5f), hy = (int)(S.y + 0.5f);
    if (g_theme.on) {
        draw_faces(f);
        for (int i = 0; i < S.ng; i++) {                         /* presents, bobbing, blinking as they are about to go */
            if (S.g[i].t < 3.0f && ((int)(S.clock * 8.0) & 1)) continue;
            gfx_gift(f, (int)S.g[i].x - 3, (int)S.g[i].y - 3 + (int)(1.5f * m_sin((float)S.clock * 4.0f + (float)i)), I_YOU, I_CORE);
            gfx_px(f, (int)S.g[i].x + 5, (int)S.g[i].y - 6, I_CORE);
        }
        if (S.state != ST_DEAD || S.dead_t < 0.35) {
            uint8_t lm[256]; memset(lm, 0, sizeof lm); lm['g'] = I_GOLD; lm['c'] = I_WING; lm['w'] = I_CORE;
            gfx_blit(f, hx - 3, hy - 5, LANTERN, 10, lm);
        }
    }
    if (!g_theme.on && (S.state != ST_DEAD || S.dead_t < 0.35)) {   /* the moth: a point of light, wings a blur */
        int fl = ((int)(S.clock * 24.0) & 1) * 2;
        gfx_px(f, hx - 3 - fl / 2, hy - 1, I_WING); gfx_px(f, hx + 3 + fl / 2, hy - 1, I_WING);
        gfx_px(f, hx - 2, hy + fl - 1, I_WING); gfx_px(f, hx + 2, hy + fl - 1, I_WING);
        gfx_disc(f, hx, hy, 1, I_CORE);
    }
    if (S.state == ST_PLAY || S.state == ST_DEAD) {
        plate(f, 3, 3, 46, 30);
        gfx_rect(f, 6, 6, 10, 10, I_YOU); gfx_text8(f, 20, 7, "YOU", I_TEXT, 1);
        gfx_rect(f, 6, 20, 10, 10, I_NOT); gfx_text8(f, 20, 21, g_theme.on ? "NAUGHTY" : "DARK", I_TEXT, 1);
        if (g_theme.on && S.state == ST_PLAY) for (int i = 0; i < S.bells; i++) {          /* the last bells you have left */
            gfx_disc(f, 10 + i * 12, H - 10, 4, I_GOLD); gfx_rect(f, 9 + i * 12, H - 6, 2, 2, I_CORE);
        }
    }
    if (S.state == ST_PLAY) {
        gfx_itoa(S.score, num);
        plate(f, W / 2 - 4 * (int)strlen(num) - 4, 3, 8 * (int)strlen(num) + 8, 20);
        gfx_text16_c(f, 5, num, I_TEXT, 1);
        if (g_theme.on) {
            if (S.fx_t > 0.0) {                                  /* the present's effect, and how long it has left */
                const char *nm = S.fx == FX_COCOA ? "Hot cocoa" : S.fx == FX_CANE ? "Candy cane" : "Magnet";
                plate(f, W / 2 - 52, H - 24, 104, 18);
                gfx_text8_c(f, H - 22, nm, I_TEXT, 1);
                gfx_rect(f, W / 2 - 42, H - 11, (int)(S.fx_t / S.fx_max * 84.0), 3, I_GOLD);
            }
            {                                                    /* the step you have reached, and the boss's health while it lives */
                const char *tn = TIER_NAME[S.tier]; int tw = 8 * (int)strlen(tn) + 12;
                plate(f, (W - tw) / 2, 24, tw, 13); gfx_text8_c(f, 27, tn, I_TEXT, 1);
            }
            int by = 38;
            if (boss()) {
                plate(f, W / 2 - 60, by, 120, 13); gfx_text8_c(f, by + 3, "Grand Snowman", I_TEXT, 1);
                for (int i = 0; i < 3; i++) gfx_rect(f, W / 2 + 62 + i * 7, by + 3, 5, 6, i < S.boss_hp ? I_GOLD : I_PLATE);
                by += 14;
            }
            if (S.combo >= 2 && S.clock - S.last_merge < 2.5) {  /* a combo, while it lasts */
                char cb[16] = "Combo x"; gfx_itoa(S.combo > 5 ? 5 : S.combo, cb + 7);
                plate(f, W / 2 - 40, by, 80, 13); gfx_text8_c(f, by + 2, cb, I_TEXT, 1); by += 14;
            }
            if (S.msg_t > 0.0) { int mw = 8 * (int)strlen(S.msg) + 12; plate(f, (W - mw) / 2, by, mw, 14); gfx_text8_c(f, by + 3, S.msg, I_TEXT, 1); }
        }
    } else if (S.state == ST_DEAD) {
        int r = (int)(S.dead_t * 130.0);
        if (g_theme.on) gfx_gifts(f, S.dead_t, I_NOT, I_YOU, I_CORE, I_GOLD);            /* presents and ornaments tumble down, for a laugh */
        else { gfx_ring(f, hx, hy, r, I_RING); gfx_ring(f, hx, hy, r + 1, I_RING); }
        plate(f, 60, 54, 200, 108);
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 62, g_theme.on ? "Snowman score" : "MERGES", I_TEXT, 1);
        gfx_text16_c(f, 74, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 114, line, I_TEXT, 1);
        if (g_theme.on) gfx_text8_c(f, 126, TIER_NAME[S.tier], I_TEXT, 1);       /* how far up the list you got */
        if (S.dead_t > 0.6) gfx_text8_c(f, 140, "PRESS TO RETRY", I_TEXT, 1);
    } else {
        if (g_theme.on) { gfx_garland(f, S.clock, I_NOT, I_YOU, I_TEXT, I_GOLD); gfx_sparkles(f, S.clock, I_TEXT); }
        if (g_theme.on) {
            plate(f, 24, 10, 272, 40); plate(f, 10, 54, 300, 46); plate(f, 10, 138, 300, 40);
            gfx_text16_c(f, 13, "LANTERN", I_TEXT, 2);
            gfx_text8_c(f, 36, "A LANTERN ON A SNOWY NIGHT", I_TEXT, 1);
            gfx_text8_c(f, 58, "The Naughty List chases your lantern.", I_TEXT, 1);
            gfx_text8_c(f, 69, "Roll its snowballs into snowmen,", I_TEXT, 1);
            gfx_text8_c(f, 80, "then catch the presents they leave!", I_TEXT, 1);
            static const char *const DN[2] = { "Nice List - a gentler night", "Naughty List - harder, x1.5" };
            for (int d = 0; d < 2; d++) {                          /* the two ways up the list; the chosen one is framed and marked */
                int y = 106 + 16 * d;
                plate(f, 40, y, 240, 13);
                if (S.diff == d) {
                    gfx_rect(f, 40, y, 240, 1, I_GOLD); gfx_rect(f, 40, y + 12, 240, 1, I_GOLD);
                    gfx_rect(f, 40, y, 1, 13, I_GOLD); gfx_rect(f, 279, y, 1, 13, I_GOLD);
                    gfx_rect(f, 41, y + 1, 1, 11, I_GOLD); gfx_rect(f, 278, y + 1, 1, 11, I_GOLD);
                }
                gfx_text8_c(f, y + 3, DN[d], I_TEXT, 1);
            }
            gfx_text8_c(f, 142, "Up / down or tap to choose", I_TEXT, 1);
            gfx_text8_c(f, 154, "Steer: keys, stick, mouse or touch", I_TEXT, 1);
            gfx_text8_c(f, 166, "Press any key", I_TEXT, 1);
        } else {
            plate(f, 24, 10, 272, 52); plate(f, 10, 112, 300, 72);
            gfx_text16_c(f, 14, "MOTH", I_TEXT, 2);
            gfx_text8_c(f, 50, "A LIGHT IN A DARK THAT ANSWERS", I_TEXT, 1);
            gfx_text8_c(f, 118, "THE DARK CHASES YOU IN YOUR OPPOSITE", I_TEXT, 1);
            gfx_text8_c(f, 130, "LURE IT TOGETHER: TOUCHING, IT MERGES", I_TEXT, 1);
            gfx_text8_c(f, 148, "STEER: KEYS / STICK / MOUSE / TOUCH", I_TEXT, 1);
            gfx_text8_c(f, 162, "PRESS ANY KEY", I_TEXT, 1);
        }
    }
}

static int idle(void) { return S.state == ST_MENU || (S.state == ST_DEAD && S.dead_t > 0.6); }
const Scene scene_moth = { "MOTH", W, H, enter, update, render, idle, moth_tap, 1 };

/* for the test harness only: a bot's heading. It keeps clear of the dark, more of the big ones and of what is close, and of
 * the walls, and circles a little so it is not pinned in a corner. */
void moth_debug(int *score, int *state, float *dirx, float *diry, int *nblobs) {
    *score = S.score; *state = S.state; *nblobs = S.nb; *dirx = *diry = 0.0f;
    if (S.state != ST_PLAY) return;
    float fx = 0.0f, fy = 0.0f;
    for (int i = 0; i < S.nb; i++) {
        float dx = S.x - S.b[i].x, dy = S.y - S.b[i].y, d = m_sqrt(dx * dx + dy * dy) + 0.001f, reach = 60.0f + S.b[i].r * 2.6f;
        if (d < reach) { float wgt = (reach - d) / reach; wgt *= wgt * (1.0f + S.b[i].r / 14.0f) * 3.0f; fx += dx / d * wgt; fy += dy / d * wgt; }
    }
    float m = 36.0f;
    if (S.x < m) fx += (m - S.x) / m * 1.6f;
    if (S.x > W - m) fx -= (S.x - (W - m)) / m * 1.6f;
    if (S.y < m) fy += (m - S.y) / m * 1.6f;
    if (S.y > H - m) fy -= (S.y - (H - m)) / m * 1.6f;
    float len = m_sqrt(fx * fx + fy * fy);
    if (len > 0.02f) { *dirx = fx / len - fy / len * 0.45f; *diry = fy / len + fx / len * 0.45f; }
}
