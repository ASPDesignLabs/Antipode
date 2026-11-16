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

void gfx_text16(Frame *f, int x, int y, const char *s, int fg, int scale) {
    for (; *s; s++, x += 8 * scale) gfx_glyph16(f, x, y, (uint8_t)*s, fg, -1, scale);
}

void gfx_text8(Frame *f, int x, int y, const char *s, int fg, int scale) {
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

void gfx_text_a(Frame *f, int x, int y, const char *s, int fg, int tall, int scale, float alpha) {
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
