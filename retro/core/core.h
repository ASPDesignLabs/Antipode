/* Antipode (retro) - shared types.
 *
 * Vendored and slimmed from HAUNTED.EXE's core (ASPDesignLabs/HAUNTED.EXE, src/core): the same idea, an 8-bit indexed
 * Frame with a 6-bit VGA DAC palette, everything drawn on the CPU, no GPU, no assets. What was screensaver-only
 * (scene rotation, hold-H, touch stage, pad rumble, the ledger's slots) is gone; what a single interactive scene
 * needs (an absolute pointer, an "any button" edge, one saved number) is added.
 *
 * It builds freestanding (no libc, no libm) for WebAssembly as well as hosted, so only what is declared here is used. */
#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef AP_FREESTANDING
#define memset __builtin_memset                 /* wasm bulk-memory instructions: nothing to link */
#define memcpy __builtin_memcpy
size_t strlen(const char *s);               /* platform/libc_shim.c */
#else
#include <string.h>
#endif

#define FB_MAX_W 720
#define FB_MAX_H 400
#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717959f

typedef struct Frame {
    int w, h;                       /* active video mode (the game uses 320x200, VGA mode 13h) */
    float fade;                     /* scene-level fade, 0..1 (palette scale) */
    uint8_t pix[FB_MAX_W * FB_MAX_H];
    uint8_t pal[256][3];            /* VGA DAC: 6 bits per channel, 0..63 */
} Frame;

enum { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE, KEY_ENTER, KEY_ESC, KEY_M, KEY_Q, KEY_E, KEY_T, KEY_ANY, KEY_COUNT };
/* A game controller, by XInput's layout. */
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_VIEW, PAD_MENU, PAD_LS, PAD_RS,
       PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_LT, PAD_RT, PAD_COUNT };

typedef struct Input {
    uint8_t down[KEY_COUNT];        /* held now */
    uint8_t hit[KEY_COUNT];         /* went down since the last update */
    int pad;                        /* a controller is plugged in */
    uint8_t pad_down[PAD_COUNT], pad_hit[PAD_COUNT];
    float lx, ly, rx, ry;           /* the sticks past their dead zones, -1..1, y UP */
    /* A pointer in the picture's own pixels, y DOWN: the mouse while it is over the picture, a finger while it is down. */
    int ptr_on;
    float ptr_x, ptr_y;
    int ptr_hit;                    /* it went down since the last update */
    int ptr_up;                     /* it was released since the last update (ptr_x/y: where) */
    int act;                        /* filled by the app: any key, button or press landed this step */
} Input;

/* A scene owns one video mode. */
typedef struct Scene {
    const char *name;
    int w, h;                       /* native mode */
    void (*enter)(uint32_t seed);   /* (re)start from the top */
    void (*update)(double dt, const Input *in);
    void (*render)(Frame *f);       /* draw the current state */
    int  (*idle)(void);             /* on its title screen: safe to switch away (and the app draws < > for touch) */
    int  (*tap)(float x, float y);  /* a tap on an idle screen: return 1 if the scene took it (a button), else it is "go" or a scene switch */
    int  vert;                      /* in season the scene's title uses up and down to choose: they are not "go" */
} Scene;

/* The season. With `on`, every hue goes through a painter's wheel (so red's opposite is green, gold's is blue) and the
 * scenes dress themselves; `pair` picks which of the two opposed pairs you play: 0 red / green, 1 gold / blue. Nothing in a
 * scene's rules depends on either: it is all palette, sprites, words and sound. With `on` clear, the engine is as it was. */
typedef struct Theme { int on; int pair; } Theme;
extern Theme g_theme;
float theme_wheel(float h);                 /* a position on the ring (0..1) -> the hue it is drawn in */
float theme_base_hue(uint32_t *rng);        /* the colour you start a run as: one of the pair's two, or any, when off */
const char *theme_pair_name(int pair);      /* "RED/GREEN", "GOLD/BLUE" */
#define GOLD_RING 0.23f                    /* a ring position that is drawn gold */

/* A few more saved bytes a scene may keep for itself (a chosen difficulty): 0..6, each 0..255. */
int  pref_get(int i);
void pref_set(int i, int v);

/* Best scores, one per scene (the current one's), 0..65535. The app keeps the bytes, the platform stores them. */
int  best_get(void);
void best_set(int v);

/* Every scene keeps palette index 15 for its text: the app draws its own < > hints with it. */
#define I_TEXT_ALL 15
#define SCENE_COUNT 4
extern const Scene scene_afterimage, scene_prism, scene_tidepool, scene_moth;

extern const unsigned char font_vga16[256][16];
extern const unsigned char font_bios8[256][8];

/* ---- small helpers ---------------------------------------------------- */
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* xorshift32 - tiny, deterministic, per-scene state */
static inline uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static inline float rng_f(uint32_t *s) { return (rng_next(s) >> 8) * (1.0f / 16777216.0f); }

/* Math without libm: the same numbers on every build (host and WebAssembly), which keeps runs reproducible.
 * floorf/sqrtf are compiler builtins (one instruction on wasm). Accurate to about 1e-3, plenty for a 320x200 picture. */
#define m_floor(x) __builtin_floorf(x)
#define m_sqrt(x)  __builtin_sqrtf(x)
#define m_abs(x)   __builtin_fabsf(x)
static inline float m_sin(float x) {                       /* any x, radians */
    x -= TAU_F * m_floor(x * (1.0f / TAU_F) + 0.5f);          /* -pi..pi */
    float y = (4.0f / PI_F) * x - (4.0f / (PI_F * PI_F)) * x * m_abs(x);
    return 0.225f * (y * m_abs(y) - y) + y;
}
static inline float m_cos(float x) { return m_sin(x + PI_F * 0.5f); }
static inline float m_atan2(float y, float x) {
    float ax = m_abs(x), ay = m_abs(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0.0f) return 0.0f;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = PI_F * 0.5f - r;
    if (x < 0.0f) r = PI_F - r;
    return y < 0.0f ? -r : r;
}

#endif
