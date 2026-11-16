/* Host-side test harness (Linux/anywhere): renders frames to PNG and times them, so the game can be checked headlessly.
 *
 *   host shot  <t1,t2,...> prefix [scale]   PNG frames at those times (seconds), default scale 3
 *   host bench <seconds>                    ms per frame, update + present, every frame drawn
 *   host bot   <seconds>                    a scripted player that steers at the spark; prints how it did
 *
 * HOST_INPUT plays a person: "1:+SPACE;2:+LEFT;2.5:-LEFT;3:p160,100;4:pu" (+/- keys, pX,Y the pointer on at a place,
 * pd the pointer pressed, pu the pointer gone, a stick as aLX0.8, a button +PAD_A).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include "core.h"
#include "app.h"

void afterimage_debug(int *score, int *state, float *pick_x, float *pick_y, float *x, float *y);

static void put32(FILE *f, uint32_t v) { fputc(v >> 24, f); fputc(v >> 16, f); fputc(v >> 8, f); fputc(v, f); }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    put32(f, len); fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    put32(f, crc);
}
static void write_png(const char *path, const uint8_t *rgb, int w, int h, int scale) {
    int W = w * scale, H = h * scale;
    size_t rowlen = 1 + (size_t)W * 3;
    uint8_t *raw = malloc(rowlen * H);
    for (int y = 0; y < H; y++) {
        uint8_t *row = raw + rowlen * y;
        row[0] = 0;
        for (int x = 0; x < W; x++) memcpy(row + 1 + x * 3, rgb + ((y / scale) * w + x / scale) * 3, 3);
    }
    uLongf clen = compressBound((uLong)(rowlen * H));
    uint8_t *comp = malloc(clen);
    compress2(comp, &clen, raw, (uLong)(rowlen * H), 6);
    FILE *f = fopen(path, "wb");
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = { W >> 24, W >> 16, W >> 8, W, H >> 24, H >> 16, H >> 8, H, 8, 2, 0, 0, 0 };
    chunk(f, "IHDR", ihdr, 13); chunk(f, "IDAT", comp, (uint32_t)clen); chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(comp);
}

/* ---- a scripted person ---- */
static Input g_in;
static struct { double t; int key, down, pad, axis, ptr; float v, x, y; } g_ev[4096];
static int g_nev, g_iev;
static const char *const KEYNAME[KEY_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "W", "A", "S", "D", "SPACE", "ENTER", "ESC", "M", "ANY" };
static const char *const PADNAME[PAD_COUNT] = { "PAD_A", "PAD_B", "PAD_X", "PAD_Y", "PAD_LB", "PAD_RB", "PAD_VIEW", "PAD_MENU",
    "PAD_LS", "PAD_RS", "PAD_UP", "PAD_DOWN", "PAD_LEFT", "PAD_RIGHT", "PAD_LT", "PAD_RT" };
static void input_parse(const char *s) {
    static char buf[65536];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, ";"); tok && g_nev < 4096; tok = strtok(NULL, ";")) {
        char *c = strchr(tok, ':');
        if (!c) continue;
        *c = 0;
        char *a = c + 1;
        g_ev[g_nev].t = atof(tok); g_ev[g_nev].key = -1; g_ev[g_nev].pad = -1; g_ev[g_nev].axis = -1; g_ev[g_nev].ptr = 0;
        if (*a == 'a') {
            static const char *const AX[4] = { "LX", "LY", "RX", "RY" };
            for (int k = 0; k < 4; k++) if (!strncmp(a + 1, AX[k], 2)) g_ev[g_nev].axis = k;
            if (g_ev[g_nev].axis < 0) continue;
            g_ev[g_nev].v = (float)atof(a + 3); g_in.pad = 1;
        } else if (*a == 'p') {
            if (a[1] == 'd') g_ev[g_nev].ptr = 2;
            else if (a[1] == 'u') g_ev[g_nev].ptr = 3;
            else { g_ev[g_nev].ptr = 1; sscanf(a + 1, "%f,%f", &g_ev[g_nev].x, &g_ev[g_nev].y); }
        } else if ((*a == '+' || *a == '-') && !strncmp(a + 1, "PAD_", 4)) {
            for (int k = 0; k < PAD_COUNT; k++) if (!strcmp(a + 1, PADNAME[k])) g_ev[g_nev].pad = k;
            if (g_ev[g_nev].pad < 0) continue;
            g_ev[g_nev].down = *a == '+'; g_in.pad = 1;
        } else if (*a == '+' || *a == '-') {
            for (int k = 0; k < KEY_COUNT; k++) if (!strcmp(a + 1, KEYNAME[k])) g_ev[g_nev].key = k;
            if (g_ev[g_nev].key < 0) continue;
            g_ev[g_nev].down = *a == '+';
        } else continue;
        g_nev++;
    }
}
static void input_step(double now) {
    while (g_iev < g_nev && g_ev[g_iev].t <= now) {
        int k = g_ev[g_iev].key, p = g_ev[g_iev].pad;
        if (k >= 0) { if (g_ev[g_iev].down && !g_in.down[k]) g_in.hit[k] = 1; g_in.down[k] = (uint8_t)g_ev[g_iev].down; }
        if (p >= 0) { if (g_ev[g_iev].down && !g_in.pad_down[p]) g_in.pad_hit[p] = 1; g_in.pad_down[p] = (uint8_t)g_ev[g_iev].down; }
        float *ax[4] = { &g_in.lx, &g_in.ly, &g_in.rx, &g_in.ry };
        if (g_ev[g_iev].axis >= 0) *ax[g_ev[g_iev].axis] = g_ev[g_iev].v;
        switch (g_ev[g_iev].ptr) {
        case 1: g_in.ptr_on = 1; g_in.ptr_x = g_ev[g_iev].x; g_in.ptr_y = g_ev[g_iev].y; break;
        case 2: g_in.ptr_on = 1; g_in.ptr_hit = 1; break;
        case 3: g_in.ptr_on = 0; break;
        }
        g_iev++;
    }
}

static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: host shot|bench|bot ...\n"); return 1; }
    AppConfig cfg;
    app_default_config(&cfg);
    cfg.start_fade = 0;
    app_set_input(&g_in);
    app_init(&cfg);
    const char *env = getenv("HOST_INPUT");
    if (env) input_parse(env);
    const double dt = 1.0 / 60.0;
    static uint8_t rgb[FB_MAX_W * FB_MAX_H * 3];
    int w, h;

    if (!strcmp(argv[1], "shot") && argc >= 4) {
        int scale = argc > 4 ? atoi(argv[4]) : 3;
        double times[256]; int nt = 0;
        for (char *tok = strtok(argv[2], ","); tok && nt < 256; tok = strtok(NULL, ",")) times[nt++] = atof(tok);
        double t = 0; int ti = 0;
        while (ti < nt) {
            input_step(t);
            app_update(dt);
            t += dt;
            while (ti < nt && t >= times[ti]) {
                char path[512];
                app_present_rgb(rgb, &w, &h);
                snprintf(path, sizeof path, "%s_%03d.png", argv[3], (int)(times[ti] * 10 + 0.5));
                write_png(path, rgb, w, h, scale);
                printf("%s\n", path);
                ti++;
            }
        }
    } else if (!strcmp(argv[1], "bench") && argc >= 3) {
        double secs = atof(argv[2]), worst = 0, total = 0;
        int n = (int)(secs * 60);
        static uint32_t dst[FB_MAX_W * FB_MAX_H];
        for (int i = 0; i < n; i++) {
            input_step(i * dt);
            double t0 = now_ms();
            app_update(dt);
            app_present(dst, 0);
            double d = now_ms() - t0;
            total += d; if (d > worst) worst = d;
        }
        printf("%d frames: %.3f ms/frame average, %.3f ms worst\n", n, total / n, worst);
    } else if (!strcmp(argv[1], "bot") && argc >= 3) {
        int n = (int)(atof(argv[2]) * 60), best = 0, deaths = 0, last_state = 0;
        for (int i = 0; i < n; i++) {
            int score, state; float pkx, pky, x, y;
            afterimage_debug(&score, &state, &pkx, &pky, &x, &y);
            if (state == 1) { g_in.ptr_on = 1; g_in.ptr_x = pkx; g_in.ptr_y = pky; }
            else { g_in.hit[KEY_SPACE] = 1; }                 /* start, retry */
            if (score > best) best = score;
            if (last_state == 1 && state == 2) { deaths++; printf("died at t=%.1fs with %d\n", i * dt, score); }
            last_state = state;
            app_update(dt);
        }
        printf("bot: best %d, %d deaths in %.0fs\n", best, deaths, atof(argv[2]));
    } else { fprintf(stderr, "usage: host shot|bench|bot ...\n"); return 1; }
    return 0;
}
