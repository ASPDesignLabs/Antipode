/* Drawing primitives on the indexed Frame (vendored from HAUNTED.EXE's src/core/gfx.h). */
#ifndef GFX_H
#define GFX_H

#include "core.h"

static inline void gfx_px(Frame *f, int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)f->w && (unsigned)y < (unsigned)f->h)
        f->pix[y * f->w + x] = c;
}

void gfx_clear(Frame *f, uint8_t c);
void gfx_rect(Frame *f, int x, int y, int w, int h, uint8_t c);
void gfx_line(Frame *f, int x0, int y0, int x1, int y1, uint8_t c);
void gfx_disc(Frame *f, int cx, int cy, int r, uint8_t c);

/* DAC values are 0..63 */
void gfx_pal(Frame *f, int i, int r, int g, int b);
void gfx_pal_ramp(Frame *f, int first, int last,
                  int r0, int g0, int b0, int r1, int g1, int b1);
void gfx_pal_black(Frame *f);
/* hue 0..1 (wraps), saturation and value 0..1 -> DAC values */
void gfx_hsv(float h, float s, float v, int *r, int *g, int *b);
void gfx_pal_hsv(Frame *f, int i, float h, float s, float v);

void gfx_ring(Frame *f, int cx, int cy, int r, uint8_t c);          /* a circle's outline */
void gfx_itoa(int v, char *out);
/* A sprite as rows of characters: each is looked up in `map` (a 256-entry table of palette indices; 0 is clear), the top left at x,y. */
void gfx_blit(Frame *f, int x, int y, const char *const *rows, int nrows, const uint8_t *map);
/* text that fades by ordered dither (a single palette entry cannot blend): alpha 0..1; tall: 8x16 glyphs, else 8x8 */
void gfx_text_a(Frame *f, int x, int y, const char *s, int fg, int tall, int scale, float alpha);
void gfx_text8_c(Frame *f, int y, const char *s, int fg, int scale);    /* centred on the picture */
void gfx_text16_c(Frame *f, int y, const char *s, int fg, int scale);

/* Glyph drawing; bg < 0 means transparent.  scale is an integer multiplier. */
void gfx_glyph16(Frame *f, int x, int y, uint8_t ch, int fg, int bg, int scale);
void gfx_glyph8(Frame *f, int x, int y, uint8_t ch, int fg, int bg, int scale);
void gfx_text16(Frame *f, int x, int y, const char *s, int fg, int scale);
void gfx_text8(Frame *f, int x, int y, const char *s, int fg, int scale);
int  gfx_text_width(const char *s, int scale);     /* 8 px per glyph */

#endif
