/* Sparkler's season rules, one at a time: the Grinches' skill follows your score and never passes yours, your ripe trail stops a Grinch
 * (and scores), theirs stops you, a Grinch's trail is shorter than yours, the six presents do what they say, falling behind loses, and
 * none of it exists in the plain game. */
#include "../scenes/afterimage.c"
#include <stdio.h>
int best_get(void) { return 0; }
void best_set(int v) { (void)v; }
static int fails;
#define CHECK(c, msg) do { int ok_ = (c) ? 1 : 0; printf("%s %s\n", ok_ ? "PASS" : "FAIL", msg); fails += !ok_; } while (0)
static Input none;
static void fresh(int season) { g_theme.on = season; g_theme.pair = 0; enter(4321u); begin(); S.tick = 200; S.tick_acc = 0.0f; for (int i = 0; i < MAXR; i++) S.rv[i].wait = 1e9; S.pu_next = 1e9; S.x = 160; S.y = 100; S.ang = 0.0f; }
static void lay(int owner_id, float x0, float y0, float x1, float y1, int age) {      /* a straight run of trail, laid as if `age` ticks ago */
    int t = S.tick; S.tick -= age; cur_owner = (uint8_t)owner_id;
    for (int i = 0; i <= 20; i++) stamp_head(x0 + (x1 - x0) * (float)i / 20.0f, y0 + (y1 - y0) * (float)i / 20.0f, 0);
    cur_owner = 0; S.tick = t;
}
static Rival *put_rival(int k, float x, float y, float ang) { Rival *r = &S.rv[k]; r->alive = 1; r->x = x; r->y = y; r->ang = ang; r->grace = 0.0; r->limit = rival_limit(); r->bulb = 0.0f; build_lethal(); return r; }
static void far_picks(void) { for (int i = 0; i < 3; i++) { S.px[i] = 20.0f + 10.0f * (float)i; S.py[i] = 20.0f; } }

int main(void) {
    /* the skill follows the score, and stays under yours */
    fresh(1); int ok = 1; float pf = 0.0f; int pl = 0, pw = 0;
    for (int sc = 0; sc <= 200; sc += 2) {
        S.score = sc; float sp = player_speed() * rival_speed_frac();
        if (sp >= player_speed() || rival_speed_frac() > 0.951f || rival_speed_frac() < pf - 1e-6f) ok = 0;
        if (rival_limit() > expire_ticks() - 3 || rival_limit() < 12 || rival_limit() < pl) ok = 0;
        if (rivals_wanted() < pw || rivals_wanted() > MAXR) ok = 0;
        pf = rival_speed_frac(); pl = rival_limit(); pw = rivals_wanted();
    }
    CHECK(ok, "a Grinch's speed and trail grow with your score, and never reach yours");
    S.score = 0; CHECK(rival_speed_frac() < 0.7f && rival_limit() == 12 && rivals_wanted() == 1, "at the start: slow, a short trail, one Grinch");
    S.score = 10; CHECK(rivals_wanted() == 2, "two from 8 baubles");
    S.score = 30; CHECK(rivals_wanted() == 3 && picks_wanted() == 3, "three from 24, and more baubles for them to race over");
    S.score = 500; CHECK(rival_speed_frac() <= 0.95f && rival_limit() <= expire_ticks() - 3, "capped at the top");

    /* your ripe trail stops a Grinch, and scores */
    fresh(1); far_picks(); S.score = 0;
    lay(0, 150, 60, 150, 140, 40);
    Rival *r = put_rival(0, 150, 100, 0.0f); (void)r;
    rivals_update(1.0 / 60);
    CHECK(!S.rv[0].alive && S.elims == 1 && S.score == 5, "your ripe trail eliminates a Grinch: +5");
    CHECK(S.pu_t > 0.0, "...and it drops a present");
    fresh(1); far_picks(); lay(0, 150, 60, 150, 140, 12);
    put_rival(0, 150, 100, 0.0f); rivals_update(1.0 / 60);
    CHECK(S.rv[0].alive, "fresh trail is harmless to a Grinch");
    fresh(1); far_picks(); S.eff[PU_CANE] = 5.0; build_lethal(); lay(0, 150, 60, 150, 140, 12);
    put_rival(0, 150, 100, 0.0f); rivals_update(1.0 / 60);
    CHECK(!S.rv[0].alive, "...but a candy cane makes it ripe sooner");
    fresh(1); far_picks(); S.gscore = 9; lay(0, 150, 60, 150, 140, 40); put_rival(0, 150, 100, 0.0f); rivals_update(1.0 / 60);
    CHECK(S.gscore == 6, "a caught Grinch gives back three of the Grinches' haul");
    fresh(1); far_picks(); lay(0, 150, 60, 150, 140, 40);
    Rival *g = put_rival(0, 150, 100, 0.0f); g->grace = 1.0; rivals_update(1.0 / 60);
    CHECK(S.rv[0].alive, "a newly arrived Grinch cannot be caught for a moment");

    /* the Grinches see it coming, but not far */
    fresh(1); far_picks(); S.score = 0; lay(0, 175, 60, 175, 140, 40);
    g = put_rival(0, 160, 100, 0.0f); build_lethal(); float a0 = g->ang; rivals_update(1.0 / 60);
    CHECK(m_abs(g->ang - a0) > 0.001f, "a Grinch turns from ripe trail that is close ahead");
    fresh(1); far_picks(); lay(0, 230, 60, 230, 140, 40); g = put_rival(0, 160, 100, 0.0f); build_lethal(); a0 = g->ang; rivals_update(1.0 / 60);
    CHECK(m_abs(g->ang - a0) < 0.05f, "...and does not see it far off");
    fresh(1); far_picks(); g = put_rival(0, 10, 100, PI_F); for (int i = 0; i < 40; i++) rivals_update(1.0 / 60);
    CHECK(S.rv[0].alive && g->x > 6.0f, "a Grinch turns at a wall; it never dies on one");

    /* their trail is shorter than yours */
    fresh(1); far_picks(); S.score = 0;
    lay(0, 40, 60, 40, 140, 10);                               /* yours, 10 ticks old */
    lay(1, 60, 60, 60, 140, 10);                               /* a Grinch's, as old */
    S.rv[0].alive = 1; S.rv[0].limit = rival_limit();
    for (int i = 0; i < 4; i++) { S.tick++; sweep(); }
    int mine = 0, theirs = 0;
    for (int y = 60; y < 140; y++) { if (field[y * W + 40] >= BASE) mine++; if (field[y * W + 60] >= BASE) theirs++; }
    CHECK(mine > 40 && theirs == 0, "a Grinch's trail is gone at its limit while yours is still there");

    /* a Grinch's trail kills you */
    fresh(1); far_picks(); lay(1, 160, 70, 160, 130, 3); S.ang = -PI_F / 2; S.y = 125; S.x = 160;
    update(1.0 / 60, &none);
    CHECK(S.state == ST_DEAD && S.why == 0, "a Grinch's trail kills you at once, however young");
    fresh(1); far_picks(); lay(1, 160, 70, 160, 130, 3); S.ang = -PI_F / 2; S.y = 125; S.x = 160; S.eff[PU_STAR] = 4.0;
    for (int i = 0; i < 20; i++) update(1.0 / 60, &none);
    CHECK(S.state == ST_PLAY, "a star lets you through trails");
    fresh(1); far_picks(); lay(1, 160, 70, 160, 130, 3); S.ang = -PI_F / 2; S.y = 125; S.x = 160; S.bell = 1;
    update(1.0 / 60, &none);
    CHECK(S.state == ST_PLAY && S.bell == 0 && S.invuln > 1.0, "a Jingle Bell saves you once...");
    S.invuln = 0.0; S.x = 160; S.y = 100; lay(1, 160, 90, 160, 110, 3); update(1.0 / 60, &none);
    CHECK(S.state == ST_DEAD, "...and only once");
    fresh(1); far_picks(); S.bell = 1; S.x = 5.5f; S.y = 100; S.ang = PI_F; update(1.0 / 60, &none);
    CHECK(S.state == ST_PLAY && S.bell == 0 && m_cos(S.ang) > 0.0f, "the bell turns you back from a wall");
    fresh(1); far_picks(); S.x = 5.5f; S.y = 100; S.ang = PI_F; update(1.0 / 60, &none);
    CHECK(S.state == ST_DEAD, "a wall without a bell is still a wall");

    /* falling behind loses */
    fresh(1); S.score = 3; S.gscore = 3 + LEAD - 1; put_rival(0, 100, 100, 0.0f); S.px[0] = 100; S.py[0] = 100;
    rivals_update(1.0 / 60);
    CHECK(S.state == ST_DEAD && S.why == 1, "when the Grinches get seven baubles ahead, you lose");
    fresh(1); S.score = 3; S.gscore = 3 + LEAD - 2; put_rival(0, 100, 100, 0.0f); S.px[0] = 100; S.py[0] = 100;
    rivals_update(1.0 / 60);
    CHECK(S.state == ST_PLAY, "...but not before");

    /* the six presents */
    fresh(1); far_picks(); float xa, xb;
    for (int run = 0; run < 2; run++) { fresh(1); far_picks(); S.x = 60; S.y = 100; S.ang = 0.0f; if (run) S.eff[PU_COCOA] = 5.0; for (int i = 0; i < 30; i++) update(1.0 / 60, &none); if (run) xb = S.x; else xa = S.x; }
    CHECK(xb - 60.0f > (xa - 60.0f) * 1.25f, "hot cocoa makes you quicker");
    for (int run = 0; run < 2; run++) {
        fresh(1); far_picks(); S.score = 20; Rival *q = put_rival(0, 60, 60, 0.0f); if (run) S.eff[PU_GLOBE] = 5.0;
        float x0 = q->x; rivals_update(1.0 / 20); if (run) xb = q->x - x0; else xa = q->x - x0;
    }
    CHECK(xb < xa * 0.5f, "a snow globe slows the Grinches");
    fresh(1); far_picks(); S.px[0] = S.x + 60; S.py[0] = S.y + 40; S.eff[PU_MISTLE] = 5.0; float d0 = 72.0f;
    for (int i = 0; i < 20; i++) update(1.0 / 60, &none);
    { float dx = S.px[0] - S.x, dy = S.py[0] - S.y; CHECK(m_sqrt(dx * dx + dy * dy) < d0 - 10.0f || S.score > 0, "mistletoe draws baubles toward you"); }
    for (int k = 0; k < NPU; k++) {
        fresh(1); far_picks(); S.pu_x = S.x; S.pu_y = S.y; S.pu_kind = k; S.pu_t = 5.0; update(1.0 / 60, &none);
        int got = k == PU_BELL ? S.bell == 1 : S.eff[k] > 0.0;
        char m[64] = "picking up present "; gfx_itoa(k, m + 19);
        CHECK(got && S.pu_t <= 0.0 && S.msg_t > 0.0, m);
    }
    fresh(1); far_picks(); S.pu_t = 0.1; update(0.2, &none);
    CHECK(S.pu_t <= 0.0, "an unclaimed present goes");

    /* the Grinches arrive and return */
    fresh(1); far_picks(); S.rv[0].wait = 0.05; rivals_update(0.1);
    CHECK(S.rv[0].alive && S.rv[0].grace > 0.5, "a Grinch arrives when its wait is up, and is safe for a moment");
    fresh(1); far_picks(); lay(0, 150, 60, 150, 140, 40); put_rival(0, 150, 100, 0.0f); rivals_update(1.0 / 60);
    CHECK(S.rv[0].wait >= 2.0 && S.rv[0].wait <= 5.0, "a caught one is back in two to five seconds");

    /* the plain game */
    fresh(0);
    CHECK(nsl == 60 && picks_wanted() == 1, "the plain game keeps 60 slots and one spark");
    for (int i = 0; i < 600; i++) { S.rv[0].wait = 0; update(1.0 / 60, &none); }
    int any = 0; for (int i = 0; i < MAXR; i++) any |= S.rv[i].alive; for (int i = 0; i < W * H; i++) any |= owner[i];
    CHECK(!any && S.pu_t <= 0.0, "in the plain game no Grinch ever appears, and no present");
    fresh(1); CHECK(nsl == 56, "in season the ring is 56 slots, to leave the Grinches' colours");
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
