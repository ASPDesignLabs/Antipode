/* Caroling Run's rules: every ring has a way through of solid cells and neighbouring rings share one (so it can always be flown),
 * the verses alternate your colour, the other colour is always hollow, hollow is what kills, and the snow falls faster as you do. */
#include "../scenes/prism.c"
#include <stdio.h>
int best_get(void) { return 0; }
void best_set(int v) { (void)v; }
static int fails;
#define CHECK(c, msg) do { int ok_ = (c) ? 1 : 0; printf("%s %s\n", ok_ ? "PASS" : "FAIL", msg); fails += !ok_; } while (0)
int main(void) {
    g_theme.on = 1; g_theme.pair = 0; enter(0xC0FFEEu); S.lane = 8;
    int solid_lane = 1, shared = 1, verse_ok = 1, hostile_hollow = 1, early_safe = 1, hollow_seen = 0;
    static uint8_t ring[NRING][SEGS];
    for (int n = 0; n < 1500; n++) {
        gen_ring(n);
        memcpy(ring[n & (NRING - 1)], pat[n & (NRING - 1)], SEGS);
        int l = lane_dbg[n & (NRING - 1)], fk = verse_of(n) ? K_GREEN : K_RED;
        const uint8_t *r = pat[n & (NRING - 1)];
        if ((r[l] & K_HOLLOW) || (r[(l + 1) % SEGS] & K_HOLLOW)) solid_lane = 0;
        if ((r[l] & 3) != fk) verse_ok = 0;
        for (int s = 0; s < SEGS; s++) {
            if (r[s] & K_HOLLOW) { hollow_seen++; if ((r[s] & 3) == fk) hostile_hollow = 0; if (n < 6) early_safe = 0; }   /* a hollow cell is never your colour */
        }
        if (n > 0) {
            int ok = 0; const uint8_t *q = ring[(n - 1) & (NRING - 1)];
            for (int s = 0; s < SEGS; s++) if (!(r[s] & K_HOLLOW) && !(q[s] & K_HOLLOW)) ok = 1;
            if (!ok) shared = 0;
        }
    }
    CHECK(solid_lane, "every ring's way through (two cells) is solid");
    CHECK(shared, "every pair of neighbouring rings shares a solid cell");
    CHECK(verse_ok, "the way through is your colour, and the verses alternate it");
    CHECK(hostile_hollow, "a hollow cell is always the other colour");
    CHECK(early_safe && hollow_seen > 1000, "the first rings are safe, then hollow cells are common");
    CHECK(lethal_cell(K_RED | K_HOLLOW, 0) && !lethal_cell(K_WHITE, 0) && !lethal_cell(K_GREEN, 0), "what is hollow kills, and nothing else does");
    CHECK(verse_of(0) == 0 && verse_of(VERSE) == 1 && verse_of(2 * VERSE) == 0, "colours change places every verse");

    /* the snow: how far it falls in a second at the start, and at full speed */
    double fall[2];
    for (int run = 0; run < 2; run++) {
        enter(1u); begin();
        for (int n = 0; n < NRING; n++) for (int s = 0; s < SEGS; s++) pat[n][s] = K_WHITE;       /* nothing to hit */
        S.zf = run ? 160.0 * RINGLEN : 0.0; S.gen_next = 1 << 30;                                 /* and nothing new made */
        double s0 = S.snow; Input in; memset(&in, 0, sizeof in);
        for (int i = 0; i < 60; i++) update(1.0 / 60, &in);
        fall[run] = S.snow - s0;
    }
    CHECK(fall[1] > fall[0] * 2.8, "the snow falls faster as you fly faster");
    printf("   (snow falls %.0f px in a second at the start, %.0f at full speed)\n", fall[0], fall[1]);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
