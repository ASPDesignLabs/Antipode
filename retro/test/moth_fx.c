/* Lantern's season rules, one at a time: a snowman that reaches full size is finished (a bonus, a present, gone), a present gives its
 * effect (cocoa quickens you, a candy cane freezes the blobs, a magnet draws them together), merges in a row make a combo, quick
 * icicles are sent after a while, and none of it happens in the plain game. */
#include "../scenes/moth.c"
#include <stdio.h>
int best_get(void) { return 0; }
void best_set(int v) { (void)v; }
static int fails;
#define CHECK(c, msg) do { int ok_ = (c) ? 1 : 0; printf("%s %s\n", ok_ ? "PASS" : "FAIL", msg); fails += !ok_; } while (0)
static void fresh(int season) { g_theme.on = season; g_theme.pair = 0; enter(1234u); begin(); S.nb = 0; S.ng = 0; S.spawn_t = 1e9; S.gift_t = 1e9; S.x = 300.0f; S.y = 100.0f; S.vx = S.vy = 0.0f; }
static Input none;
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

    fresh(1); S.life = 20.0; int fast = 0;
    for (int i = 0; i < 300; i++) { S.nb = 0; spawn_blob(); if (S.b[0].sp > 1.4f) fast++; }
    CHECK(fast > 40 && fast < 150, "after a while about a third of what is sent is quick");
    fresh(1); S.life = 5.0; fast = 0;
    for (int i = 0; i < 300; i++) { S.nb = 0; spawn_blob(); if (S.b[0].sp > 1.4f) fast++; }
    CHECK(fast == 0, "...but not at first");

    fresh(0);
    S.b[0] = (Blob){ 100, 100, 0, 0, 17, 1 }; S.b[1] = (Blob){ 101, 100, 0, 0, 17, 1 }; S.nb = 2; sc = S.score;
    update(1.0 / 60, &none);
    CHECK(S.snowmen == 0 && S.nb == 1 && S.score == sc + 1, "in the plain game none of it applies: one point, the blob stays");
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
