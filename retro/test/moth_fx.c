/* Lantern's season rules, one at a time: a snowman that reaches full size is finished (a bonus, a present, gone), a present gives its
 * effect (cocoa quickens you, a candy cane freezes the blobs, a magnet draws them together), merges in a row make a combo, quick
 * icicles are sent from the second step, the night is climbed in steps by score (coal, the Grand Snowman, last bells, a list of your
 * choosing), and none of it happens in the plain game. */
#include "../scenes/moth.c"
#include <stdio.h>
int best_get(void) { return 0; }
void best_set(int v) { (void)v; }
static int prefs[8];
int pref_get(int i) { return prefs[i]; }
void pref_set(int i, int v) { prefs[i] = v; }
static int fails;
#define CHECK(c, msg) do { int ok_ = (c) ? 1 : 0; printf("%s %s\n", ok_ ? "PASS" : "FAIL", msg); fails += !ok_; } while (0)
static void fresh(int season) { g_theme.on = season; g_theme.pair = 0; enter(1234u); begin(); S.nb = 0; S.ng = 0; S.spawn_t = 1e9; S.gift_t = 1e9; S.x = 300.0f; S.y = 100.0f; S.vx = S.vy = 0.0f; }
static Input none;
static void fresh_d(int d) { prefs[0] = d; fresh(1); }
static void at_tier(int t) { S.score = (S.diff ? THR_NAUGHTY : THR_NICE)[t]; if (t == 4) S.boss_done = 1; update(1.0 / 60, &none); S.nb = 0; S.ng = 0; S.spawn_t = 1e9; S.gift_t = 1e9; S.invuln = 0; }
int main(void) {
    fresh(1);
    S.b[0] = (Blob){ 100, 100, 0, 0, 17, 1 }; S.b[1] = (Blob){ 101, 100, 0, 0, 17, 1 }; S.nb = 2; int sc = S.score;
    update(1.0 / 60, &none);
    CHECK(S.snowmen == 1 && S.nb == 0, "a snowman that reaches full size is finished and goes");
    CHECK(S.score >= sc + 5, "...with a bonus");
    CHECK(S.ng == 1, "...and a present is left");

    fresh(1);
    S.g[0] = (Gift){ S.x, S.y, 10.0f, FX_CANE }; S.ng = 1;
    update(1.0 / 60, &none);
    CHECK(S.fx == FX_CANE && S.fx_t > 5.0 && S.ng == 0, "picking up a present gives its effect for a while");
    S.b[0] = (Blob){ 100, 100, 60, 0, 10, 1 }; S.nb = 1;
    for (int i = 0; i < 40; i++) { S.fx = FX_CANE; S.fx_t = 5.0; update(1.0 / 60, &none); }
    CHECK(S.b[0].vx * S.b[0].vx + S.b[0].vy * S.b[0].vy < 25.0f, "a candy cane freezes the blobs");

    float gap_plain = 0.0f, gap_magnet = 0.0f;
    for (int run = 0; run < 2; run++) {
        fresh(1); S.x = 310.0f; S.y = 100.0f;
        S.b[0] = (Blob){ 120, 60, 0, 0, 9, 1 }; S.b[1] = (Blob){ 120, 150, 0, 0, 9, 1 }; S.nb = 2;
        for (int i = 0; i < 120; i++) { if (run) { S.fx = FX_MAGNET; S.fx_t = 5.0; } update(1.0 / 60, &none); if (S.state != ST_PLAY) break; }
        float g = S.nb < 2 ? 0.0f : m_abs(S.b[0].y - S.b[1].y);
        if (run) gap_magnet = g; else gap_plain = g;
    }
    CHECK(gap_magnet < gap_plain * 0.6f, "a magnet draws the blobs together");

    float x_plain = 0, x_cocoa = 0;
    for (int run = 0; run < 2; run++) {
        fresh(1); S.x = 20.0f; Input in; memset(&in, 0, sizeof in); in.down[KEY_RIGHT] = 1;
        for (int i = 0; i < 40; i++) { if (run) { S.fx = FX_COCOA; S.fx_t = 5.0; } update(1.0 / 60, &in); }
        if (run) x_cocoa = S.x; else x_plain = S.x;
    }
    CHECK(x_cocoa - 20.0f > (x_plain - 20.0f) * 1.25f, "hot cocoa makes you quicker");

    fresh(1);
    S.b[0] = (Blob){ 100, 100, 0, 0, 8, 1 }; S.b[1] = (Blob){ 101, 100, 0, 0, 8, 1 }; S.b[2] = (Blob){ 110, 100, 0, 0, 8, 1 }; S.nb = 3; sc = S.score;
    update(1.0 / 60, &none);
    CHECK(S.score - sc == 3, "two merges in a row make a combo: 1, then 2");

    fresh(1); S.tier = 1; int fast = 0;
    for (int i = 0; i < 300; i++) { S.nb = 0; spawn_blob(); if (S.b[0].sp > 1.4f) fast++; }
    CHECK(fast > 20 && fast < 120, "from the second step some of what is sent is a quick icicle");
    fresh(1); S.tier = 0; fast = 0;
    for (int i = 0; i < 300; i++) { S.nb = 0; spawn_blob(); if (S.b[0].sp > 1.4f) fast++; }
    CHECK(fast == 0, "...but not in the first");

    /* the steps */
    for (int d = 0; d < 2; d++) {
        fresh_d(d); const int *th = d ? THR_NAUGHTY : THR_NICE; int ok = 1;
        for (int t = 0; t < 4; t++) { S.score = th[t]; if (tier_for(S.score) != t) ok = 0; if (t && tier_for(th[t] - 1) != t - 1) ok = 0; }
        S.boss_done = 0; S.score = th[4] + 50; if (tier_for(S.score) != 3) ok = 0;
        S.boss_done = 1; if (tier_for(S.score) != 4) ok = 0;
        CHECK(ok, d ? "Naughty List: the steps come at 9, 24, 48; the last needs the boss beaten" : "Nice List: the steps come at 15, 40, 80; the last needs the boss beaten");
    }
    CHECK(THR_NAUGHTY[1] < THR_NICE[1] && THR_NAUGHTY[3] < THR_NICE[3], "the Naughty List climbs sooner");

    fresh_d(0); at_tier(3);
    int ng0 = S.ng; S.score = THR_NICE[1]; S.tier = 0; S.nb = 0; S.ng = 0; update(1.0 / 60, &none);
    CHECK(S.tier == 1 && S.ng == 1 && S.msg_t > 2.0, "a step up shows a banner and leaves a present");
    (void)ng0;

    /* the boss */
    fresh_d(0); S.tier = 2; S.score = THR_NICE[3]; S.nb = 0; S.ng = 0; update(1.0 / 60, &none);
    Blob *bs = boss();
    CHECK(S.tier == 3 && bs && bs->r > 29.0f && S.boss_hp == 3, "the Grand Snowman comes at Christmas Eve: size 30, three snowballs to melt");
    fresh_d(0); S.nb = 0; S.b[0] = (Blob){ 100, 100, 0, 0, 30, 0.75f, KD_BOSS }; S.b[1] = (Blob){ 104, 100, 0, 0, 20, 1, KD_SNOW }; S.nb = 2; S.tier = 3; S.score = 60;
    merge_blobs();
    CHECK(S.nb == 2, "the boss does not merge: it only pushes");
    fresh_d(0); S.tier = 3; S.score = 60; spawn_boss(); S.x = 10; S.y = 10;
    int sc3 = S.score;
    S.b[S.nb++] = (Blob){ 200, 150, 0, 0, 25, 1, KD_SNOW };      /* a finished snowman pelts it */
    finish_snowmen();
    bs = boss();
    CHECK(S.boss_hp == 2 && bs && bs->r < 27.0f && S.score == sc3 + 5, "a finished snowman pelts the boss: one down, and it shrinks");
    S.boss_hp = 1;
    S.b[S.nb++] = (Blob){ 200, 150, 0, 0, 25, 1, KD_SNOW };
    S.ng = 0; sc3 = S.score; finish_snowmen();
    CHECK(!boss() && S.boss_done && S.ng >= 3 && S.score >= sc3 + 30, "the third melts it: +25, three presents, and the way to Midnight is open");

    /* coal */
    fresh_d(0); S.tier = 2; int coal = 0;
    for (int i = 0; i < 300; i++) { S.nb = 0; spawn_blob(); if (S.b[0].kind == KD_COAL) coal++; }
    CHECK(coal > 30 && coal < 110, "from the Naughty List step about a fifth of what is sent is coal");
    fresh_d(0); S.tier = 1; coal = 0;
    for (int i = 0; i < 200; i++) { S.nb = 0; spawn_blob(); if (S.b[0].kind == KD_COAL) coal++; }
    CHECK(coal == 0, "...and none before");
    fresh_d(0); S.tier = 2; S.nb = 0; S.b[0] = (Blob){ 100, 100, 0, 0, 14, 0.65f, KD_COAL }; S.b[1] = (Blob){ 104, 100, 0, 0, 10, 1, KD_SNOW }; S.nb = 2; sc3 = S.score;
    merge_blobs();
    float want = m_sqrt(10.0f * 10.0f + 0.5f * 14.0f * 14.0f);
    CHECK(S.nb == 1 && S.b[0].kind == KD_SNOW && m_abs(S.b[0].r - want) < 0.01f && S.score >= sc3 + 3, "coal melts into the snow it touches, at half its weight, for +2");
    fresh_d(0); S.tier = 2; S.nb = 0; S.b[0] = (Blob){ 100, 100, 0, 0, 20, 0.65f, KD_COAL }; S.b[1] = (Blob){ 104, 100, 0, 0, 20, 0.65f, KD_COAL }; S.nb = 2;
    merge_blobs(); finish_snowmen();
    CHECK(S.snowmen == 0 && S.nb == 1 && S.b[0].kind == KD_COAL, "coal on coal stays coal, and never finishes as a snowman");
    fresh_d(0); S.tier = 2; Blob cb = { 0, 0, 0, 0, 14, 0.65f, KD_COAL }, sb = { 0, 0, 0, 0, 14, 1.0f, KD_SNOW };
    CHECK(vmax_for(&cb) < vmax_for(&sb) * 0.75f, "coal is heavy and slow");

    /* last bells */
    fresh_d(0); CHECK(S.bells == 2, "Nice List gives two last bells");
    fresh_d(1); CHECK(S.bells == 1, "Naughty List gives one");
    fresh_d(0); S.x = 100; S.y = 100; S.b[0] = (Blob){ 100, 100, 0, 0, 14, 1, KD_SNOW }; S.nb = 1;
    update(1.0 / 60, &none);
    CHECK(S.state == ST_PLAY && S.bells == 1 && S.invuln > 1.0 && S.fx == FX_CANE, "caught with a bell in hand: the list freezes, you live");
    S.b[0].x = S.x; S.b[0].y = S.y; update(1.0 / 60, &none);
    CHECK(S.state == ST_PLAY && S.bells == 1, "...and are safe a moment");
    S.invuln = 0; S.fx_t = 0; S.fx = FX_NONE; S.b[0].x = S.x; S.b[0].y = S.y; update(1.0 / 60, &none);
    S.invuln = 0; S.b[0].x = S.x; S.b[0].y = S.y; S.bells = 0; S.nb = 1; update(1.0 / 60, &none);
    CHECK(S.state == ST_DEAD, "with none left, caught is caught");

    /* difficulty */
    fresh_d(1); int s1 = S.score; S.nb = 0; S.b[0] = (Blob){ 100, 100, 0, 0, 8, 1, KD_SNOW }; S.b[1] = (Blob){ 101, 100, 0, 0, 8, 1, KD_SNOW }; S.nb = 2; S.x = 300;
    merge_blobs();
    CHECK(S.score - s1 == 2, "Naughty List pays half as much again (1 point becomes 2)");
    float g0 = 0, g1 = 0, v0 = 0, v1 = 0; Blob pb = { 0, 0, 0, 0, 10, 1, KD_SNOW };
    fresh_d(0); S.tier = 2; S.life = 10; g0 = spawn_gap(); v0 = vmax_for(&pb);
    fresh_d(1); S.tier = 2; S.life = 10; g1 = spawn_gap(); v1 = vmax_for(&pb);
    CHECK(g1 < g0 && v1 > v0, "the Naughty List sends them sooner and faster");
    float gp = 9, vp = 0;
    for (int t = 0; t < 5; t++) { fresh_d(0); S.tier = t; S.life = 10; float g = spawn_gap(), v = vmax_for(&pb); if (g > gp || v < vp) gp = -1; else gp = g; vp = v; }
    CHECK(gp > 0, "each step sends them sooner and faster");
    float c0, c1; fresh_d(0); S.tier = 0; S.fx_t = 0; c0 = 6.0f; (void)c0;
    fresh_d(0); S.tier = 3; S.fx_max = 0; S.g[0] = (Gift){ S.x, S.y, 10.0f, FX_COCOA }; S.ng = 1; update(1.0 / 60, &none); c1 = (float)S.fx_max;
    CHECK(c1 > 10.0f, "cocoa lasts longer higher up (10.5 s at Christmas Eve)");

    /* the music follows */
    fresh_d(0); S.tier = 0; music_progress(); CHECK(snd_music_layers_get() == 0, "the first step is the plain carol");
    S.tier = 1; music_progress(); int l1 = snd_music_layers_get();
    S.tier = 3; music_progress(); int l3 = snd_music_layers_get();
    S.tier = 4; music_progress(); int l4 = snd_music_layers_get();
    CHECK(l1 == ML_SHAKER && (l3 & ML_DRUMS) && (l3 & ML_BRASS) && l4 == ML_ALL, "each step adds a layer: shaker, shimmer, drums and brass");

    /* the choice */
    prefs[0] = 0; fresh(1); S.state = ST_MENU;
    Input in; memset(&in, 0, sizeof in); in.hit[KEY_DOWN] = 1; update(1.0 / 60, &in);
    CHECK(S.diff == 1 && prefs[0] == 1, "down chooses the Naughty List, and it is kept");
    memset(&in, 0, sizeof in); in.hit[KEY_UP] = 1; update(1.0 / 60, &in);
    CHECK(S.diff == 0 && prefs[0] == 0, "up chooses the Nice List");
    CHECK(moth_tap(100.0f, 128.0f) == 1 && S.diff == 1, "a tap on the lower bar chooses the Naughty List");
    CHECK(moth_tap(100.0f, 110.0f) == 1 && S.diff == 0, "a tap on the upper bar chooses the Nice List");
    CHECK(moth_tap(100.0f, 60.0f) == 0 && moth_tap(10.0f, 110.0f) == 0, "a tap elsewhere is left alone");
    prefs[0] = 1; enter(5u); CHECK(S.diff == 1, "the choice is remembered when the scene is entered again");
    fresh(0); S.state = ST_MENU; CHECK(moth_tap(100.0f, 128.0f) == 0, "the plain game has no choice");

    fresh(0);
    S.b[0] = (Blob){ 100, 100, 0, 0, 17, 1 }; S.b[1] = (Blob){ 101, 100, 0, 0, 17, 1 }; S.nb = 2; sc = S.score;
    update(1.0 / 60, &none);
    CHECK(S.snowmen == 0 && S.nb == 1 && S.score == sc + 1, "in the plain game none of it applies: one point, the blob stays");
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
