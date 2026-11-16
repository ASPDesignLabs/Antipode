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
/* In season the text is not the terminal's: all-capital strings are set in friendly mixed case (a title in Title Case), small text
 * is slanted and shadowed, and large text (text16 at scale 2 or more) is lettered: smoothed and rounded, filled in candy stripes,
 * outlined and shadowed. A scene says which palette indices to use, once a frame. Strings that already have lower case are left alone. */
void gfx_set_style(int fill1, int fill2, int outline, int shadow);
/* The season's cheer, drawn with four palette indices a scene lends it (a: one box colour, b: another, r: ribbon and caps, g: a glint):
 * presents and ornaments that fall and land (a game-over), a string of twinkling bulbs along the top (a title), and sparkles. */
void gfx_gifts(Frame *f, double t, int a, int b, int r, int g);
void gfx_gift(Frame *f, int x, int y, int box, int ribbon);       /* one present, 7x7, top left at x,y */
void gfx_garland(Frame *f, double t, int a, int b, int r, int g);
void gfx_sparkles(Frame *f, double t, int idx);
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
