#include "gfx.h"

static inline int iabs(int v) { return v < 0 ? -v : v; }

void gfx_clear(Frame *f, uint8_t c) {
    memset(f->pix, c, (size_t)f->w * f->h);
}

void gfx_rect(Frame *f, int x, int y, int w, int h, uint8_t c) {
    int x0 = clampi(x, 0, f->w), x1 = clampi(x + w, 0, f->w);
    int y0 = clampi(y, 0, f->h), y1 = clampi(y + h, 0, f->h);
    for (int yy = y0; yy < y1; yy++)
        memset(&f->pix[yy * f->w + x0], c, (size_t)(x1 - x0));
}

void gfx_line(Frame *f, int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_px(f, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_disc(Frame *f, int cx, int cy, int r, uint8_t c) {
    for (int y = -r; y <= r; y++) {
        int w = (int)m_sqrt((float)(r * r - y * y) + 0.5f);
        gfx_rect(f, cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

void gfx_pal(Frame *f, int i, int r, int g, int b) {
    f->pal[i][0] = (uint8_t)clampi(r, 0, 63);
    f->pal[i][1] = (uint8_t)clampi(g, 0, 63);
    f->pal[i][2] = (uint8_t)clampi(b, 0, 63);
}

void gfx_pal_ramp(Frame *f, int first, int last,
                  int r0, int g0, int b0, int r1, int g1, int b1) {
    int n = last - first;
    for (int i = 0; i <= n; i++) {
        float t = n ? (float)i / n : 0.0f;
        gfx_pal(f, first + i, (int)(r0 + (r1 - r0) * t + 0.5f),
                (int)(g0 + (g1 - g0) * t + 0.5f), (int)(b0 + (b1 - b0) * t + 0.5f));
    }
}

void gfx_pal_black(Frame *f) { memset(f->pal, 0, sizeof f->pal); }

void gfx_glyph16(Frame *f, int x, int y, uint8_t ch, int fg, int bg, int scale) {
    const unsigned char *g = font_vga16[ch];
    for (int row = 0; row < 16; row++) {
        unsigned bits = g[row];
        for (int col = 0; col < 8; col++) {
            int on = (bits >> (7 - col)) & 1;
            int c = on ? fg : bg;
            if (c < 0) continue;
            if (scale == 1) gfx_px(f, x + col, y + row, (uint8_t)c);
            else gfx_rect(f, x + col * scale, y + row * scale, scale, scale, (uint8_t)c);
        }
    }
}

void gfx_glyph8(Frame *f, int x, int y, uint8_t ch, int fg, int bg, int scale) {
    const unsigned char *g = font_bios8[ch];
    for (int row = 0; row < 8; row++) {
        unsigned bits = g[row];
        for (int col = 0; col < 8; col++) {
            int on = (bits >> (7 - col)) & 1;
            int c = on ? fg : bg;
            if (c < 0) continue;
            if (scale == 1) gfx_px(f, x + col, y + row, (uint8_t)c);
            else gfx_rect(f, x + col * scale, y + row * scale, scale, scale, (uint8_t)c);
        }
    }
}

static void text16_plain(Frame *f, int x, int y, const char *s, int fg, int scale) {
    for (; *s; s++, x += 8 * scale) gfx_glyph16(f, x, y, (uint8_t)*s, fg, -1, scale);
}

static void text8_plain(Frame *f, int x, int y, const char *s, int fg, int scale) {
    for (; *s; s++, x += 8 * scale) gfx_glyph8(f, x, y, (uint8_t)*s, fg, -1, scale);
}

int gfx_text_width(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }

void gfx_hsv(float h, float s, float v, int *r, int *g, int *b) {
    h -= m_floor(h);
    if (g_theme.on) h = theme_wheel(h);                          /* the ring is the painter's wheel in season */
    float p = h * 6.0f;
    float fr = clampf(m_abs(p - 3.0f) - 1.0f, 0.0f, 1.0f);
    float fg = clampf(2.0f - m_abs(p - 2.0f), 0.0f, 1.0f);
    float fb = clampf(2.0f - m_abs(p - 4.0f), 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);
    s = clampf(s, 0.0f, 1.0f);
    *r = (int)((v * (1.0f + s * (fr - 1.0f))) * 63.0f + 0.5f);
    *g = (int)((v * (1.0f + s * (fg - 1.0f))) * 63.0f + 0.5f);
    *b = (int)((v * (1.0f + s * (fb - 1.0f))) * 63.0f + 0.5f);
}
void gfx_pal_hsv(Frame *f, int i, float h, float s, float v) {
    int r, g, b;
    gfx_hsv(h, s, v, &r, &g, &b);
    gfx_pal(f, i, r, g, b);
}

void gfx_ring(Frame *f, int cx, int cy, int r, uint8_t c) {   /* midpoint circle */
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_px(f, cx + x, cy + y, c); gfx_px(f, cx - x, cy + y, c); gfx_px(f, cx + x, cy - y, c); gfx_px(f, cx - x, cy - y, c);
        gfx_px(f, cx + y, cy + x, c); gfx_px(f, cx - y, cy + x, c); gfx_px(f, cx + y, cy - x, c); gfx_px(f, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

void gfx_itoa(int v, char *o) {
    char t[12]; int n = 0;
    if (v <= 0) { o[0] = '0'; o[1] = 0; return; }
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
}

void gfx_text8_c(Frame *f, int y, const char *s, int fg, int scale) { gfx_text8(f, (f->w - gfx_text_width(s, scale)) / 2, y, s, fg, scale); }
void gfx_text16_c(Frame *f, int y, const char *s, int fg, int scale) { gfx_text16(f, (f->w - gfx_text_width(s, scale)) / 2, y, s, fg, scale); }

void gfx_text_a(Frame *f, int x, int y, const char *s0, int fg, int tall, int scale, float alpha) {
    char fb[96]; const char *s = s0;
    if (g_theme.on) { extern void gfx_festive_case_(const char *, char *, int); gfx_festive_case_(s0, fb, sizeof fb); s = fb; }
    static const uint8_t BAY[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    int gh = tall ? 16 : 8, thr = (int)(clampf(alpha, 0.0f, 1.0f) * 16.0f + 0.5f);
    for (; *s; s++, x += 8 * scale)
        for (int row = 0; row < gh; row++) {
            unsigned bits = tall ? font_vga16[(uint8_t)*s][row] : font_bios8[(uint8_t)*s][row];
            for (int col = 0; col < 8; col++) {
                if (!((bits >> (7 - col)) & 1)) continue;
                for (int yy = 0; yy < scale; yy++)
                    for (int xx = 0; xx < scale; xx++) {
                        int px = x + col * scale + xx, py = y + row * scale + yy;
                        if (BAY[(py & 3) * 4 + (px & 3)] < thr) gfx_px(f, px, py, (uint8_t)fg);
                    }
            }
        }
}

void gfx_blit(Frame *f, int x, int y, const char *const *rows, int nrows, const uint8_t *map) {
    for (int r = 0; r < nrows; r++)
        for (int c = 0; rows[r][c]; c++) {
            uint8_t v = map[(uint8_t)rows[r][c]];
            if (v) gfx_px(f, x + c, y + r, v);
        }
}

/* ---- the season's cheer ---- */
static const char *const GIFT[7] = { "..rrr..", ".r.r.r.", "aaaraaa", "aaaraaa", "rrrrrrr", "aaaraaa", "aaaraaa" };
static const char *const ORNAMENT[9] = { "...r...", "..rrr..", ".aaaaa.", "aagaaaa", "agaaaaa", "aaaaaaa", "aaaaaaa", ".aaaaa.", "..aaa.." };

void gfx_gift(Frame *f, int x, int y, int box, int ribbon) {
    uint8_t map[256]; memset(map, 0, sizeof map);
    map['a'] = (uint8_t)box; map['r'] = (uint8_t)ribbon;
    gfx_blit(f, x, y, GIFT, 7, map);
}

void gfx_gifts(Frame *f, double t, int a, int b, int r, int g) {
    for (int i = 0; i < 18; i++) {
        double t0 = (double)(i % 9) * 0.16 + (double)(i / 9) * 0.05, k = t - t0;
        if (k < 0.0) continue;
        int orn = (i % 3) == 2, h = orn ? 9 : 7;
        int x = (i * 53 + 17) % (f->w - 10) + 1, land = f->h - 3 - h - (i / 9) * 6 - (i * 5) % 4;
        float y = -10.0f + (float)k * (110.0f + (float)(i % 4) * 28.0f) * (1.0f + (float)k * 0.9f);        /* gravity */
        int landed = y >= (float)land;
        if (landed) {                                                  /* and a little bounce */
            float kk = (float)k - (float)(land + 10) / 150.0f;
            float bounce = kk > 0.0f && kk < 0.5f ? 5.0f * m_abs(m_sin(kk * 12.0f)) * (1.0f - kk * 2.0f) : 0.0f;
            y = (float)land - bounce;
        }
        uint8_t map[256]; memset(map, 0, sizeof map);
        map['a'] = (uint8_t)(i & 1 ? b : a); map['r'] = (uint8_t)r; map['g'] = (uint8_t)g;
        gfx_blit(f, x, (int)y, orn ? ORNAMENT : GIFT, h, map);
    }
}

void gfx_garland(Frame *f, double t, int a, int b, int r, int g) {
    for (int x = 0; x < f->w; x++) {                                   /* the wire sags between pins every 64 pixels */
        float u = (float)((x % 64) - 32) / 32.0f;
        gfx_px(f, x, 2 + (int)(5.0f * (1.0f - u * u)), r);
    }
    for (int i = 0; i * 9 + 4 < f->w; i++) {                           /* a bulb every nine, each on its own beat */
        int bx = i * 9 + 4;
        float u = (float)((bx % 64) - 32) / 32.0f;
        int by = 3 + (int)(5.0f * (1.0f - u * u));
        int lit = ((int)(t * 2.5 + (double)i * 0.7) & 3) != 0;
        int c = lit ? ((i & 1) ? a : b) : g;
        gfx_rect(f, bx - 1, by + 1, 3, 4, c);
        gfx_px(f, bx, by, r);
    }
}

void gfx_sparkles(Frame *f, double t, int idx) {
    for (int i = 0; i < 16; i++) {
        float ph = m_sin((float)t * 2.3f + (float)i * 1.7f);
        if (ph < 0.35f) continue;
        int x = (i * 71 + 23) % (f->w - 8) + 4, y = 18 + (i * 37 + 11) % (f->h - 40);
        gfx_px(f, x, y, idx);
        if (ph > 0.75f) { gfx_px(f, x - 1, y, idx); gfx_px(f, x + 1, y, idx); gfx_px(f, x, y - 1, idx); gfx_px(f, x, y + 1, idx); }
    }
}

/* ---- the season's lettering ---- */
static int g_s1 = 1, g_s2 = 1, g_so = 0, g_ss = 0;
void gfx_set_style(int fill1, int fill2, int outline, int shadow) { g_s1 = fill1; g_s2 = fill2; g_so = outline; g_ss = shadow; }

/* All-capital text becomes friendly: sentence case (or Title Case); anything with a lower-case letter is the author's own. */
static void festive_case(const char *s, char *out, int n, int title) {
    int has_lower = 0;
    for (const char *q = s; *q; q++) if (*q >= 'a' && *q <= 'z') has_lower = 1;
    int cap = 1, i = 0;
    for (; s[i] && i < n - 1; i++) {
        char c = s[i];
        if (has_lower) { out[i] = c; continue; }
        if (c >= 'A' && c <= 'Z') { out[i] = cap ? c : (char)(c + 32); cap = 0; }
        else out[i] = c;
        if (title && (c == ' ' || c == '-')) cap = 1;
    }
    out[i] = 0;
    for (int k = 0; out[k + 3]; k++)                                     /* WASD is not a word */
        if (out[k] == 'W' && out[k + 1] == 'a' && out[k + 2] == 's' && out[k + 3] == 'd') { out[k + 1] = 'A'; out[k + 2] = 'S'; out[k + 3] = 'D'; }
}

/* Scale2x: an 8x8 glyph to 16x16 with its diagonals filled in, so it reads as lettering and not as a grid of squares */
static void smooth_glyph(uint8_t ch, uint8_t o[16][16]) {
    int p[10][10]; memset(p, 0, sizeof p);
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) p[r + 1][c + 1] = (font_bios8[ch][r] >> (7 - c)) & 1;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int P = p[r + 1][c + 1], A = p[r][c + 1], B = p[r + 1][c + 2], C = p[r + 1][c], D = p[r + 2][c + 1];
            int q1 = P, q2 = P, q3 = P, q4 = P;
            if (C == A && C != D && A != B) q1 = A;
            if (A == B && A != C && B != D) q2 = B;
            if (D == C && D != B && C != A) q3 = C;
            if (B == D && B != A && D != C) q4 = D;
            o[2 * r][2 * c] = (uint8_t)q1; o[2 * r][2 * c + 1] = (uint8_t)q2; o[2 * r + 1][2 * c] = (uint8_t)q3; o[2 * r + 1][2 * c + 1] = (uint8_t)q4;
        }
}

static void title_text(Frame *f, int x, int y, const char *s) {
    for (int pass = 0; pass < 3; pass++) {                                /* shadow, then outline, then the candy-striped fill */
        int cx = x;
        for (const char *q = s; *q; q++, cx += 16) {
            uint8_t g[16][16]; smooth_glyph((uint8_t)*q, g);
            for (int r = 0; r < 16; r++)
                for (int c = 0; c < 16; c++) {
                    if (!g[r][c]) continue;
                    int px = cx + c, py = y + r;
                    if (pass == 0) { gfx_px(f, px + 2, py + 2, (uint8_t)g_ss); }
                    else if (pass == 1) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) gfx_px(f, px + dx, py + dy, (uint8_t)g_so); }
                    else gfx_px(f, px, py, (uint8_t)((((px + py) >> 2) & 1) ? g_s1 : g_s2));
                }
        }
    }
}

static void small_text(Frame *f, int x, int y, const char *s, int fg, int tall) {
    int gh = tall ? 16 : 8;
    for (int pass = 0; pass < 2; pass++)                                  /* a soft shadow, then the letters, slanted */
        for (const char *q = s, *e = s; *q; q++) {
            (void)e;
            int cx = x + (int)(q - s) * 8;
            for (int r = 0; r < gh; r++) {
                unsigned bits = tall ? font_vga16[(uint8_t)*q][r] : font_bios8[(uint8_t)*q][r];
                int shear = r < gh / 2 ? 1 : 0;
                for (int c = 0; c < 8; c++)
                    if ((bits >> (7 - c)) & 1) { if (pass == 0) gfx_px(f, cx + c + shear + 1, y + r + 1, (uint8_t)g_ss); else gfx_px(f, cx + c + shear, y + r, (uint8_t)fg); }
            }
        }
}

void gfx_text8(Frame *f, int x, int y, const char *s, int fg, int scale) {
    if (g_theme.on && scale == 1) { char b[96]; festive_case(s, b, sizeof b, 0); small_text(f, x, y, b, fg, 0); return; }
    text8_plain(f, x, y, s, fg, scale);
}
void gfx_text16(Frame *f, int x, int y, const char *s, int fg, int scale) {
    if (g_theme.on && scale >= 2) { char b[96]; festive_case(s, b, sizeof b, 1); title_text(f, x, y, b); return; }
    if (g_theme.on) { char b[96]; festive_case(s, b, sizeof b, 0); small_text(f, x, y, b, fg, 1); return; }
    text16_plain(f, x, y, s, fg, scale);
}

void gfx_festive_case_(const char *s, char *out, int n) { festive_case(s, out, n, 0); }
