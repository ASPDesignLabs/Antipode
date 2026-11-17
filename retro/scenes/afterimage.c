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
 * 0..15 are reserved: an 8-step background ramp, the border, the spark, the head, the death ring and the text.
 *
 * In season Sparkler is a race. Grinches (rival sparklers) steer for the baubles too and leave trails of their own; each trail
 * pixel remembers who laid it (`owner`), because who laid it decides who it kills. Your ripe trail kills a Grinch (and you score);
 * a Grinch's trail kills you at once. Their speed, the length of their trail and their number follow your score, never past yours.
 * Fall too far behind the Grinches' haul and you lose. Six presents help you. The Grinches' trails are painted from a last block of
 * 16 palette entries (age by level), so the season has 56 slots in the ring where the plain game has 60. */
#include "core.h"
#include "gfx.h"
#include "snd.h"

#define W 320
#define H 200

#define SLOTS nsl                   /* 60 in the plain game, 56 in season: the Grinches' palette takes the last 16 */
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

static int nsl = 60;
#define RB 240                      /* the Grinches' trail colours (season): RB + age bucket * 4 + level */
#define MAXR 3
#define NPU 6
#define LEAD 7                      /* fall this far behind the Grinches' haul and you lose */
enum { PU_CANE, PU_COCOA, PU_GLOBE, PU_MISTLE, PU_BELL, PU_STAR };
typedef struct { int alive, limit; float x, y, ang, sx, sy, bulb; double wait, grace; } Rival;

static struct {
    uint32_t rng, rrng;
    Rival rv[MAXR];
    int gscore, elims, why, nick;
    float px[3], py[3];             /* the baubles (one in the plain game) */
    float pu_x, pu_y; int pu_kind; double pu_t, pu_next, eff[NPU], invuln, msg_t; int bell; const char *msg;
    float hue;
    int score, best, state;
    int tick;
    float tick_acc;                 /* 0..1 of the way to the next tick: the palette ages smoothly between ticks */
    float x, y, ang;
    float bulb_d;                   /* how far since the last bulb on the string */
    double clock, dead_t;
} S;

/* in season: the spark's ornament (g: the cap, o: the glass, h: a glint) */
static const char *const ORN[11] = {
    "...ggg...", "...g.g...", "..ooooo..", ".ooooooo.", "ooohhoooo", "oohooooo.",
    "ooooooooo", "ooooooooo", ".ooooooo.", "..ooooo..", "...ooo..." };
static uint8_t ORNMAP[256];

/* the six presents (season): w white, r the hot colour, p your colour, h a glint, o a glow */
static const char *const ICON[NPU][9] = {
    { "..rwrwr..", ".rw...wr.", ".wr...rw.", "......wr.", "......rw.", "......wr.", "......rw.", "......wr.", "......rw." },   /* candy cane */
    { "...o.o...", "....o.o..", ".wwwwwww.", ".wpppppww", ".wpppppw.", ".wpppppww", ".wwwwwww.", "..wwwww..", "........." },   /* hot cocoa */
    { "..wwwww..", ".w.h...w.", "w.h.....w", "w.......w", "w...h...w", ".w.....w.", "..wwwww..", ".rrrrrrr.", ".rwrwrwr." },    /* snow globe */
    { ".p...p...", "ppp.ppp..", ".ppp.ppp.", "..ppppp..", "...ppp...", "..r.w.r..", ".rrr.www.", "..r..w...", "........." },    /* mistletoe */
    { "....w....", "...www...", "..wwwww..", "..wwhww..", ".wwwwwww.", ".wwwwwww.", "wwwwwwwww", "....r....", "........." },    /* jingle bell */
    { "....w....", "....w....", "...www...", "wwwwwwwww", ".wwwwwww.", "..wwwww..", "..wwwww..", ".ww...ww.", ".w.....w." } };  /* star */
static uint8_t ICONMAP[256];

static uint8_t field[W * H];        /* the picture as the game keeps it: background, border and trail */
static uint8_t bgimg[W * H];        /* what a pixel goes back to when its trail expires */
static uint8_t slot_of[256];
static uint8_t level_of[256];
static uint8_t lethal[256];
static uint8_t lethal_r[256];       /* ripe, for a Grinch's purposes: your trail, a little sooner with a candy cane */
static uint8_t bk_of[256];          /* the age bucket of each slot, for the Grinches' trail colours */
static uint8_t owner[W * H];        /* who laid each trail pixel: 0 you, 1.. a Grinch */
static uint8_t cur_owner;

static int expire_ticks(void) {
    int e = EXPIRE0 + (int)(S.score * 1.08f), mx = g_theme.on ? 54 : EXPIRE_MAX;
    return e > mx ? mx : e;
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
    if (g_theme.on)                                         /* the Grinches' trails: another colour, dimming as they age */
        for (int bk = 0; bk < 4; bk++)
            for (int l = 0; l < LEVELS; l++) gfx_pal_hsv(f, RB + bk * 4 + l, S.hue + 0.25f + flip, 0.10f, (0.35f + 0.65f * LV[l]) * (1.0f - 0.14f * (float)bk));
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
            field[y * W + x] = (uint8_t)idx; owner[y * W + x] = cur_owner;
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

static int px_danger(int p) {                               /* a trail pixel kills you: yours once ripe, a Grinch's always */
    int v = field[p];
    if (v < BASE) return 0;
    return owner[p] ? level_of[v] >= 2 : lethal[v];
}
static int head_in_danger(float x, float y) {               /* the pixels under the head, asked of the table */
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy > 5) continue;
            int px = cx + dx, py = cy + dy;
            if ((unsigned)px < (unsigned)W && (unsigned)py < (unsigned)H && px_danger(py * W + px)) return 1;
        }
    return 0;
}

static void build_lethal(void) {
    int ex = expire_ticks(), thr = g_theme.on && S.eff[PU_CANE] > 0.0 ? 4 : LETHAL_FROM;
    memset(lethal, 0, sizeof lethal);
    memset(lethal_r, 0, sizeof lethal_r);
    for (int i = BASE; i < 256; i++) {
        int age = age_of(slot_of[i]);
        lethal[i] = (uint8_t)(level_of[i] >= 2 && age >= LETHAL_FROM && age < ex);
        lethal_r[i] = (uint8_t)(level_of[i] >= 2 && age >= thr && age < ex);
        bk_of[i] = (uint8_t)(age / 6 > 3 ? 3 : age / 6);
    }
}

static void sweep(void) {                                   /* the slot that just expired goes back to the background */
    int s = (((S.tick - expire_ticks()) % SLOTS) + SLOTS) % SLOTS, rs[MAXR + 1] = { -1, -1, -1, -1 };
    if (g_theme.on)
        for (int k = 0; k < MAXR; k++) if (S.rv[k].alive) rs[k + 1] = (((S.tick - S.rv[k].limit) % SLOTS) + SLOTS) % SLOTS;   /* a Grinch's trail is shorter than yours */
    for (int i = 0; i < W * H; i++) {
        int v = field[i];
        if (v >= BASE) {
            int sl = slot_of[v];
            if (sl == s) { field[i] = bgimg[i]; owner[i] = 0; }
            else if (owner[i] && sl == rs[owner[i]]) { field[i] = bgimg[i]; owner[i] = 0; }
        }
    }
}

static void advance_ticks(double dt) {
    S.tick_acc += (float)dt * TICK_HZ;
    while (S.tick_acc >= 1.0f) { S.tick_acc -= 1.0f; S.tick++; sweep(); }
}

/* ---- the game --------------------------------------------------------- */
static void spawn_pick(int i) {
    for (int k = 0; k < 24; k++) {
        float x = 18.0f + rng_f(&S.rng) * (W - 36), y = 18.0f + rng_f(&S.rng) * (H - 36);
        float dx = x - S.x, dy = y - S.y;
        if (dx * dx + dy * dy > 50.0f * 50.0f) { S.px[i] = x; S.py[i] = y; return; }
    }
    S.px[i] = W - S.x; S.py[i] = H - S.y;
}

static void die(void);
static void load_best(void) {
    S.best = best_get();
}

/* ---- the Grinches (season) ---------------------------------------------
 * Everything they are follows your score, and nothing about them goes past you: they run at 62% of your speed at the start and at
 * most 95% of it, their trail is 12 ticks long and grows to a little less than yours, they see a little further and turn a little
 * better, and there are more of them (one, then two, then three). They never run into a wall (they turn), and they do not mind each
 * other; only your ripe trail stops them. They are single-minded about a bauble that is close, and then they do not look. */
static float wrap_angle_f(float a) { return a - TAU_F * m_floor((a + PI_F) * (1.0f / TAU_F)); }
static float rr(void) { return rng_f(&S.rrng); }
static int rivals_wanted(void) { return S.score < 8 ? 1 : S.score < 24 ? 2 : 3; }
static float rival_speed_frac(void) { float v = 0.62f + 0.012f * (float)S.score; return v > 0.95f ? 0.95f : v; }
static int rival_limit(void) { int l = 12 + (int)(0.5f * (float)S.score), mx = expire_ticks() - 3; return l > mx ? mx : l; }
static float rival_sight(void) { float v = 12.0f + 0.5f * (float)S.score; return v > 30.0f ? 30.0f : v; }
static float rival_turn(void) { float v = 0.8f + 0.004f * (float)S.score; return TURN * (v > 0.95f ? 0.95f : v); }
static int picks_wanted(void) { return g_theme.on ? (rivals_wanted() == 1 ? 2 : 3) : 1; }
static float player_speed(void) { return SPEED0 + (S.score > 20 ? 20 : S.score) * 1.2f; }

static void rival_plan(Rival *r) {                          /* where the next one will come in: an edge, well away from you */
    for (int k = 0; k < 12; k++) {
        float t = rr(); int side = (int)(rr() * 4.0f);
        float x = side == 0 ? 20.0f : side == 1 ? W - 20.0f : 20.0f + t * (W - 40), y = side == 2 ? 20.0f : side == 3 ? H - 20.0f : 20.0f + t * (H - 40);
        float dx = x - S.x, dy = y - S.y;
        if (dx * dx + dy * dy > 110.0f * 110.0f || k == 11) { r->sx = x; r->sy = y; return; }
    }
}
static void rival_spawn(Rival *r) {
    r->alive = 1; r->x = r->sx; r->y = r->sy; r->ang = m_atan2(H * 0.5f - r->y, W * 0.5f - r->x); r->grace = 1.0; r->bulb = 9.0f; r->limit = rival_limit();
}
static void drop_powerup(float x, float y);

static void rival_erase(int id) {                           /* a Grinch that is gone takes its trail with it */
    for (int i = 0; i < W * H; i++) if (owner[i] == id) { field[i] = bgimg[i]; owner[i] = 0; }
}
static void rival_die(int k) {
    Rival *r = &S.rv[k];
    drop_powerup(r->x, r->y);
    rival_erase(k + 1);
    r->alive = 0; r->wait = 2.0 > 5.0 - 0.06 * S.score ? 2.0 : 5.0 - 0.06 * S.score; rival_plan(r);
    S.elims++; S.score += 5; S.gscore = S.gscore > 3 ? S.gscore - 3 : 0;      /* the presents it was carrying fall back to you */
    S.msg = "Grinch caught! +5"; S.msg_t = 1.8;
    snd_sfx(SFX_TIER);
}

static int rival_hazard(float x, float y, int border_only) {
    float e = x < W - 1 - x ? x : W - 1 - x, ey = y < H - 1 - y ? y : H - 1 - y;
    if (ey < e) e = ey;
    if (e < BORDER + 3.0f) return 1;
    if (border_only) return 0;
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int px = cx + dx, py = cy + dy;
            if ((unsigned)px < (unsigned)W && (unsigned)py < (unsigned)H) { int p = py * W + px, v = field[p]; if (v >= BASE && !owner[p] && lethal_r[v]) return 1; }
        }
    return 0;
}

static void rival_steer(Rival *r, double dt) {              /* head for the nearest bauble; turn away from what would kill it, if it sees it in time */
    float bd = 1e9f, tx = r->x, ty = r->y;
    for (int i = 0; i < picks_wanted(); i++) { float dx = S.px[i] - r->x, dy = S.py[i] - r->y, d = dx * dx + dy * dy; if (d < bd) { bd = d; tx = S.px[i]; ty = S.py[i]; } }
    float want = m_atan2(ty - r->y, tx - r->x), sight = rival_sight();
    int greedy = bd < 24.0f * 24.0f;
    float turn = rival_turn() * (float)dt, off[8] = { 0.0f, 0.45f, -0.45f, 0.9f, -0.9f, 1.5f, -1.5f, 0.0f };
    off[7] = clampf(wrap_angle_f(want - r->ang), -1.5f, 1.5f);
    float best = 1e9f, bo = 0.0f;
    for (int k = 0; k < 8; k++) {
        float a = r->ang + off[k]; int hz = 0;
        for (int j = 1; j <= 2; j++) { float d = sight * 0.5f * (float)j; hz += rival_hazard(r->x + m_cos(a) * d, r->y + m_sin(a) * d, greedy); }
        float c = m_abs(wrap_angle_f(a - want)) + 10.0f * (float)hz + (k == 7 ? 0.0f : 0.02f);
        if (c < best) { best = c; bo = off[k]; }
    }
    r->ang += clampf(bo, -turn, turn);
}

static void rivals_update(double dt) {
    float spd = player_speed() * rival_speed_frac() * (S.eff[PU_GLOBE] > 0.0 ? 0.4f : 1.0f);
    int want = rivals_wanted();
    for (int k = 0; k < MAXR; k++) {
        Rival *r = &S.rv[k];
        r->limit = rival_limit();
        if (!r->alive) {
            if (k >= want) continue;
            r->wait -= dt;
            if (r->wait <= 0.0) rival_spawn(r);
            continue;
        }
        if (r->grace > 0.0) r->grace -= dt;
        rival_steer(r, dt);
        float dist = spd * (float)dt; int steps = (int)(dist / 1.5f) + 1;
        float sx = m_cos(r->ang) * dist / (float)steps, sy = m_sin(r->ang) * dist / (float)steps;
        for (int i = 0; i < steps; i++) {
            r->x += sx; r->y += sy;
            float e = r->x < W - 1 - r->x ? r->x : W - 1 - r->x, ey = r->y < H - 1 - r->y ? r->y : H - 1 - r->y;
            if (ey < e) e = ey;
            if (e < BORDER + 2.0f) {                              /* a wall turns it round; it does not die on one */
                r->ang = m_atan2(H * 0.5f - r->y, W * 0.5f - r->x);
                r->x = clampf(r->x, BORDER + 3.0f, W - BORDER - 3.0f); r->y = clampf(r->y, BORDER + 3.0f, H - BORDER - 3.0f);
            }
            if (r->grace <= 0.0) {                                /* your ripe trail stops it */
                int hit = 0, cx = (int)(r->x + 0.5f), cy = (int)(r->y + 0.5f);
                for (int dy = -2; dy <= 2 && !hit; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        if (dx * dx + dy * dy > 5) continue;
                        int px = cx + dx, py = cy + dy;
                        if ((unsigned)px < (unsigned)W && (unsigned)py < (unsigned)H) { int p = py * W + px, v = field[p]; if (v >= BASE && !owner[p] && lethal_r[v]) { hit = 1; break; } }
                    }
                if (hit) { rival_die(k); break; }
            }
            for (int j = 0; j < picks_wanted(); j++) {              /* it takes baubles too */
                float px = r->x - S.px[j], py = r->y - S.py[j];
                if (px * px + py * py < (RADIUS + 4.0f) * (RADIUS + 4.0f)) {
                    S.gscore++; snd_sfx_note(SFX_PICKUP, 0); spawn_pick(j);
                    if (S.gscore >= S.score + LEAD && S.state == ST_PLAY) { S.why = 1; die(); return; }
                }
            }
            r->bulb += m_sqrt(sx * sx + sy * sy);
            int bulb = r->bulb >= 9.0f;
            if (bulb) r->bulb -= 9.0f;
            cur_owner = (uint8_t)(k + 1); stamp_head(r->x, r->y, bulb); cur_owner = 0;
        }
    }
}

/* ---- presents ---------------------------------------------------------- */
static const char *const PU_NAME[NPU] = { "Candy Cane: they trip sooner!", "Hot Cocoa: you're quicker!", "Snow Globe: they slow down!", "Mistletoe: baubles come to you!", "Jingle Bell: one free save!", "Star: pass through trails!" };
static const float PU_DUR[NPU] = { 8.0f, 6.0f, 6.0f, 8.0f, 0.0f, 5.0f };
static void drop_powerup(float x, float y) {
    if (S.pu_t > 0.0) return;
    S.pu_x = clampf(x, 16.0f, W - 16.0f); S.pu_y = clampf(y, 16.0f, H - 16.0f); S.pu_kind = (int)(rr() * (float)NPU) % NPU; S.pu_t = 12.0;
}
static void powerups_update(double dt) {
    for (int i = 0; i < NPU; i++) if (S.eff[i] > 0.0) S.eff[i] -= dt;
    if (S.invuln > 0.0) S.invuln -= dt;
    if (S.msg_t > 0.0) S.msg_t -= dt;
    if (S.pu_t > 0.0) {
        S.pu_t -= dt;
        float dx = S.x - S.pu_x, dy = S.y - S.pu_y;
        if (dx * dx + dy * dy < 11.0f * 11.0f) {
            int k = S.pu_kind; S.pu_t = 0.0;
            if (k == PU_BELL) S.bell = 1; else S.eff[k] = PU_DUR[k];
            S.msg = PU_NAME[k]; S.msg_t = 2.2; snd_sfx_note(SFX_PICKUP, 8 + k);
        }
    } else {
        S.pu_next -= dt;
        if (S.pu_next <= 0.0) {                                     /* one now and then, somewhere clear of you */
            for (int k = 0; k < 10; k++) { float x = 24.0f + rr() * (W - 48), y = 24.0f + rr() * (H - 48), dx = x - S.x, dy = y - S.y; if (dx * dx + dy * dy > 70.0f * 70.0f || k == 9) { drop_powerup(x, y); break; } }
            S.pu_next = 14.0 + rr() * 8.0;
        }
    }
}

static void begin(void) {
    S.hue = theme_base_hue(&S.rng);
    S.score = 0; S.state = ST_PLAY;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    memcpy(field, bgimg, sizeof field);
    memset(owner, 0, sizeof owner);
    S.gscore = 0; S.elims = 0; S.why = 0; S.bell = 0; S.invuln = 0.0; S.msg_t = 0.0; S.pu_t = 0.0; S.pu_next = 12.0;
    for (int i = 0; i < NPU; i++) S.eff[i] = 0.0;
    for (int i = 0; i < MAXR; i++) { S.rv[i].alive = 0; S.rv[i].wait = 3.0 + 1.5 * (double)i; rival_plan(&S.rv[i]); }
    spawn_pick(0);
    if (g_theme.on) for (int i = 1; i < 3; i++) spawn_pick(i);
    S.bulb_d = 9.0f;
    snd_sfx(SFX_START);
    snd_music_play(MU_JINGLE);
}

static void enter(uint32_t seed) {
    ORNMAP['g'] = I_CORE; ORNMAP['o'] = I_PICK; ORNMAP['h'] = I_PICKDOT;
    ICONMAP['w'] = I_CORE; ICONMAP['r'] = I_BORDER0; ICONMAP['p'] = I_PICK; ICONMAP['h'] = I_PICKDOT; ICONMAP['o'] = I_HALO;
    for (int i = BASE; i < 256; i++) { slot_of[i] = (uint8_t)((i - BASE) / LEVELS); level_of[i] = (uint8_t)((i - BASE) % LEVELS); }
    nsl = g_theme.on ? 56 : 60;
    S.rng = seed ? seed : 1u;
    S.rrng = (seed ? seed : 1u) * 2654435761u + 12345u;            /* the Grinches' own stream: the plain game's numbers are untouched */
    S.hue = theme_base_hue(&S.rng);
    S.state = ST_MENU; S.score = 0; S.tick = 0; S.tick_acc = 0.0f; S.clock = 0.0; S.dead_t = 0.0;
    S.x = W * 0.5f; S.y = H * 0.5f; S.ang = -PI_F * 0.5f;
    S.gscore = 0; S.elims = 0; S.why = 0; S.bell = 0; S.pu_t = 0.0; S.msg_t = 0.0;
    for (int i = 0; i < NPU; i++) S.eff[i] = 0.0;
    for (int i = 0; i < MAXR; i++) S.rv[i].alive = 0;
    memset(owner, 0, sizeof owner);
    load_best();
    build_bg();
    spawn_pick(0);
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
    float speed = player_speed();
    if (g_theme.on && S.eff[PU_COCOA] > 0.0) speed *= 1.35f;
    float dist = speed * (float)dt;
    int steps = (int)(dist / 1.5f) + 1;
    float sx = m_cos(S.ang) * dist / (float)steps, sy = m_sin(S.ang) * dist / (float)steps;
    build_lethal();
    if (g_theme.on) {
        powerups_update(dt);
        if (S.eff[PU_MISTLE] > 0.0)                                     /* mistletoe: nearby baubles come to you */
            for (int j = 0; j < picks_wanted(); j++) {
                float mx = S.x - S.px[j], my = S.y - S.py[j], d = m_sqrt(mx * mx + my * my) + 0.001f;
                if (d < 90.0f) { float v = 120.0f * (float)dt; S.px[j] += mx / d * v; S.py[j] += my / d * v; }
            }
    }
    for (int i = 0; i < steps; i++) {
        S.x += sx; S.y += sy;
        float e = S.x < W - 1 - S.x ? S.x : W - 1 - S.x, ey = S.y < H - 1 - S.y ? S.y : H - 1 - S.y;
        if (ey < e) e = ey;
        int wall = e < BORDER + RADIUS * 0.3f;
        int ghost = g_theme.on && (S.invuln > 0.0 || S.eff[PU_STAR] > 0.0);       /* a star or a bell's grace: trails cannot hurt you */
        if (wall || (!ghost && head_in_danger(S.x, S.y))) {              /* the border and ripe trail are the complement */
            if (g_theme.on && S.bell) {                                    /* the Jingle Bell rings: one free save */
                S.bell = 0; S.invuln = 1.2; S.msg = "Jingle! Saved!"; S.msg_t = 1.6; snd_sfx(SFX_TIER);
                if (wall) { S.ang = wrap_angle(S.ang + PI_F); S.x = clampf(S.x, BORDER + 6.0f, W - BORDER - 6.0f); S.y = clampf(S.y, BORDER + 6.0f, H - BORDER - 6.0f); break; }
            } else { S.why = 0; die(); return; }
        }
        for (int j = 0; j < picks_wanted(); j++) {
            float px = S.x - S.px[j], py = S.y - S.py[j], rr2 = RADIUS + 4.0f + (g_theme.on && S.eff[PU_MISTLE] > 0.0 ? 5.0f : 0.0f);
            if (px * px + py * py < rr2 * rr2) {
                S.score++;
                snd_sfx_note(SFX_PICKUP, S.score);
                spawn_pick(j);
            }
        }
        S.bulb_d += m_sqrt(sx * sx + sy * sy);
        int bulb = S.bulb_d >= 9.0f;
        if (bulb) S.bulb_d -= 9.0f;
        stamp_head(S.x, S.y, bulb);
    }
    if (g_theme.on && S.state == ST_PLAY) rivals_update(dt);
}

/* ---- drawing over the field ------------------------------------------ */
static void hud(Frame *f) {                                  /* you and the Grinches, the gap between you, what you hold */
    char a[24] = "YOU ", b[24] = "GRINCH ";
    gfx_itoa(S.score, a + 4); gfx_itoa(S.gscore, b + 7);
    gfx_rect(f, 6, 184, 8 * (int)strlen(a) + 4, 10, I_BG); gfx_text8(f, 8, 185, a, I_TEXT, 1);
    int bw = 8 * (int)strlen(b) + 4;
    gfx_rect(f, W - 6 - bw, 184, bw, 10, I_BG); gfx_text8(f, W - 4 - bw, 185, b, I_TEXT, 1);
    float gap = (float)(S.gscore - S.score);                 /* a gauge: you are well ahead on the left, caught on the right */
    int mx = 120 + (int)(clampf((gap + (float)LEAD) / (2.0f * LEAD), 0.0f, 1.0f) * 80.0f);
    int warn = gap >= (float)(LEAD - 3) && ((int)(S.clock * 6.0) & 1);
    gfx_rect(f, 119, 186, 82, 7, warn ? I_BORDER0 : I_HALO); gfx_rect(f, 120, 187, 80, 5, I_BG);
    gfx_rect(f, 160, 187, 1, 5, I_HALO); gfx_rect(f, mx - 1, 185, 3, 9, gap >= (float)(LEAD - 3) ? I_BORDER0 : I_CORE);
    int n = 0, kind[NPU + 1]; float frac[NPU + 1];
    for (int k = 0; k < NPU; k++) if (S.eff[k] > 0.0) { kind[n] = k; frac[n] = (float)(S.eff[k] / PU_DUR[k]); n++; }
    if (S.bell) { kind[n] = PU_BELL; frac[n] = 1.0f; n++; }
    for (int i = 0; i < n; i++) {
        int ix = W / 2 - n * 6 + i * 12, iy = 170;
        gfx_blit(f, ix, iy, ICON[kind[i]], 9, ICONMAP); gfx_rect(f, ix, iy + 10, (int)(9.0f * clampf(frac[i], 0.0f, 1.0f)), 1, I_CORE);
    }
    if (S.msg_t > 0.0) { int mw = 8 * (int)strlen(S.msg) + 8; gfx_rect(f, (W - mw) / 2, 14, mw, 12, I_BG); gfx_text8_c(f, 16, S.msg, I_TEXT, 1); }
}

static void render(Frame *f) {
    if (g_theme.on) gfx_set_style(I_BORDER0, I_BORDER1, I_BG, I_BG);
    if (g_theme.on) {                                       /* ripe trail is striped as well as hot: the danger is not only a colour */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int p = y * W + x, v = field[p];
                if (v >= BASE && owner[p]) { v = RB + bk_of[v] * 4 + level_of[v]; if (level_of[field[p]] >= 2 && (((x + y) >> 1) & 1)) v -= 2; }   /* a Grinch's trail: its own colour, striped as well, always dangerous */
                else if (lethal[v] && (((x + y) >> 1) & 1)) v -= 2;
                f->pix[p] = (uint8_t)v;
            }
    } else memcpy(f->pix, field, sizeof field);
    build_pal(f);
    char num[16], line[32];
    int px = (int)(S.px[0] + 0.5f), py = (int)(S.py[0] + 0.5f);
    if (g_theme.on) {
        for (int j = 0; j < picks_wanted(); j++) gfx_blit(f, (int)(S.px[j] + 0.5f) - 4, (int)(S.py[j] + 0.5f) - 5 + (int)(1.5f * m_sin((float)S.clock * 4.0f + (float)j)), ORN, 11, ORNMAP);   /* baubles, swaying */
        if (S.state == ST_PLAY && S.pu_t > 0.0 && (S.pu_t > 3.0 || ((int)(S.clock * 8.0) & 1))) {        /* a present to pick up: its picture, in a ring */
            int ix = (int)(S.pu_x + 0.5f), iy = (int)(S.pu_y + 0.5f) + (int)(1.5f * m_sin((float)S.clock * 5.0f));
            gfx_ring(f, ix, iy, 8, I_PICKDOT); gfx_blit(f, ix - 4, iy - 4, ICON[S.pu_kind], 9, ICONMAP);
        }
        if (S.state == ST_PLAY)
            for (int k = 0; k < MAXR; k++) {
                const Rival *r = &S.rv[k];
                if (!r->alive && k < rivals_wanted() && r->wait < 1.2 && ((int)(S.clock * 10.0) & 1)) gfx_ring(f, (int)r->sx, (int)r->sy, 6, I_BORDER0);   /* one is about to come in */
            }
    } else {
        gfx_ring(f, px, py, 4 + (int)(1.5f + 1.5f * m_sin((float)S.clock * 5.0f)), I_PICK);
        gfx_disc(f, px, py, 1, I_PICKDOT);
    }
    int hx = (int)(S.x + 0.5f), hy = (int)(S.y + 0.5f);
    if (S.state == ST_MENU) { gfx_disc(f, hx, hy, 6, I_HALO); gfx_disc(f, hx, hy, 3 + (m_sin((float)S.clock * 2.0f) > 0.0f), I_CORE); }
    else if (S.state == ST_PLAY) gfx_disc(f, hx, hy, 2, I_CORE);
    if (g_theme.on && S.state != ST_MENU)                    /* the Grinches: a head, a face and a hat */
        for (int k = 0; k < MAXR; k++) {
            const Rival *r = &S.rv[k];
            if (!r->alive) continue;
            int gx = (int)(r->x + 0.5f), gy = (int)(r->y + 0.5f);
            if (r->grace > 0.0 && ((int)(S.clock * 12.0) & 1)) continue;
            gfx_disc(f, gx, gy, 4, RB + 3); gfx_px(f, gx - 2, gy - 1, I_BG); gfx_px(f, gx + 2, gy - 1, I_BG); gfx_rect(f, gx - 1, gy + 1, 3, 1, I_BG);
            gfx_rect(f, gx - 2, gy - 5, 5, 2, I_BORDER0); gfx_px(f, gx, gy - 6, I_BORDER0); gfx_px(f, gx + 1, gy - 7, I_CORE);
        }
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
        if (g_theme.on) {
            gfx_text8_c(f, 88, "The Grinches race you for baubles.", I_TEXT, 1);
            gfx_text8_c(f, 99, "Lure them over your old trail,", I_TEXT, 1);
            gfx_text8_c(f, 110, "but never touch it yourself!", I_TEXT, 1);
            gfx_text8_c(f, 124, "Fall 7 baubles behind and you lose.", I_TEXT, 1);
            gfx_text8_c(f, 138, "Presents on the way help you.", I_TEXT, 1);
            gfx_text8_c(f, 160, "WASD / STICK / MOUSE / TOUCH", I_TEXT, 1);
            gfx_text8_c(f, 174, "PRESS ANY KEY", I_TEXT, 1);
        } else {
        gfx_text8_c(f, 128, "YOUR TRAIL RIPENS INTO", I_TEXT, 1);
        gfx_text8_c(f, 140, "THE COLOUR YOU CANNOT TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 160, "WASD / STICK / MOUSE / TOUCH", I_TEXT, 1);
        gfx_text8_c(f, 174, "PRESS ANY KEY", I_TEXT, 1);
        }
    } else if (S.state == ST_PLAY) {
        if (g_theme.on) hud(f);
        else { gfx_itoa(S.score, num); gfx_text16_c(f, 178, num, I_TEXT, 1); }
    } else {
        gfx_itoa(S.score, num);
        gfx_text8_c(f, 70, "SCORE", I_TEXT, 1);
        gfx_text16_c(f, 82, num, I_TEXT, 2);
        for (int i = 0; i < 5; i++) line[i] = "BEST "[i];
        gfx_itoa(S.best, line + 5);
        gfx_text8_c(f, 122, line, I_TEXT, 1);
        if (g_theme.on) {
            gfx_text8_c(f, 138, S.why ? "The Grinch got ahead!" : "Caught in the trail!", I_TEXT, 1);
            char gl[32] = "Grinches caught: "; gfx_itoa(S.elims, gl + 17);
            gfx_text8_c(f, 148, gl, I_TEXT, 1);
        }
        if (S.dead_t > 0.6) gfx_text8_c(f, g_theme.on ? 164 : 150, "PRESS TO RETRY", I_TEXT, 1);
    }
}

static int idle(void) { return S.state == ST_MENU || (S.state == ST_DEAD && S.dead_t > 0.6); }
const Scene scene_afterimage = { "AFTERIMAGE", W, H, enter, update, render, idle, 0, 0 };

/* for the test harness only: where things are, so a scripted bot can play */
void afterimage_debug(int *score, int *state, float *pick_x, float *pick_y, float *x, float *y) {
    *score = S.score; *state = S.state; *pick_x = S.px[0]; *pick_y = S.py[0]; *x = S.x; *y = S.y;
}
