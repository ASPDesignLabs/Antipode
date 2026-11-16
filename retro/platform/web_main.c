/* The browser's shell, the C side. web/antipode.js drives it: it fills the input, steps the app each animation frame,
 * takes the frame as RGBA bytes, keeps the sound fed from snd_render, and keeps the saved number in localStorage.
 * Everything here is a thin door onto app.h, as platform/host.c is. Built freestanding (make web). */
#include "core.h"
#include "app.h"
#include "snd.h"

#define EXPORT(n) __attribute__((export_name(n)))

static Input g_in;
static uint32_t g_pix[FB_MAX_W * FB_MAX_H];
static int16_t g_pcm[4096 * 2];
static float g_pcmf[4096 * 2];
static uint8_t g_slot[16];
static AppConfig g_cfg;
static int g_started;

static const char KEYNAMES[] = "UP DOWN LEFT RIGHT W A S D SPACE ENTER ESC M ANY";
EXPORT("web_key_names") const char *web_key_names(void) { return KEYNAMES; }
EXPORT("web_pad_count") int web_pad_count(void) { return PAD_COUNT; }

/* before web_start */
EXPORT("web_config") void web_config(int sound, int volume, int brightness) {
    app_default_config(&g_cfg);
    g_cfg.sound = sound; g_cfg.volume = clampi(volume, 0, 100); g_cfg.brightness = clampi(brightness, 30, 100);
}
EXPORT("web_slot_buf") uint8_t *web_slot_buf(void) { return g_slot; }
EXPORT("web_slot_load") void web_slot_load(int len) { app_slot_load(g_slot, len); }
EXPORT("web_slot_take") int web_slot_take(void) { return app_slot_take(g_slot, sizeof g_slot); }

EXPORT("web_start") void web_start(void) {
    if (g_started) return;
    g_started = 1;
    if (!g_cfg.volume && !g_cfg.sound) app_default_config(&g_cfg);
    app_set_input(&g_in);
    app_init(&g_cfg);
}

EXPORT("web_key") void web_key(int k, int down) {
    if (k < 0 || k >= KEY_COUNT) return;
    if (down && !g_in.down[k]) g_in.hit[k] = 1;
    g_in.down[k] = (uint8_t)(down != 0);
}
/* the pointer in the picture's pixels, y down; on 0 when it has left or the finger lifted */
EXPORT("web_pointer") void web_pointer(float x, float y, int on) { g_in.ptr_on = on; if (on) { g_in.ptr_x = x; g_in.ptr_y = y; } }
EXPORT("web_pointer_down") void web_pointer_down(void) { g_in.ptr_hit = 1; }
/* the pad: buttons a bit each (PAD_*), the sticks past their dead zones, y up; present 0 when none */
EXPORT("web_pad") void web_pad(int present, int buttons, float lx, float ly, float rx, float ry) {
    g_in.pad = present;
    for (int b = 0; b < PAD_COUNT; b++) {
        int d = present && ((buttons >> b) & 1);
        if (d && !g_in.pad_down[b]) g_in.pad_hit[b] = 1;
        g_in.pad_down[b] = (uint8_t)d;
    }
    g_in.lx = lx; g_in.ly = ly; g_in.rx = rx; g_in.ry = ry;
}
EXPORT("web_release") void web_release(void) {
    memset(&g_in, 0, sizeof g_in);
}

EXPORT("web_step") void web_step(double dt) {
    if (!g_started) return;
    if (dt > 0.1) dt = 0.1;
    app_update(dt);
}
EXPORT("web_frame") uint32_t *web_frame(void) { app_present(g_pix, 0); return g_pix; }
EXPORT("web_width") int web_width(void) { int w, h; app_mode(&w, &h); return w; }
EXPORT("web_height") int web_height(void) { int w, h; app_mode(&w, &h); return h; }

/* sound: n frames of stereo at SND_RATE, as floats, interleaved */
EXPORT("web_audio") float *web_audio(int n) {
    if (n > 4096) n = 4096;
    snd_render(g_pcm, n);
    for (int i = 0; i < n * 2; i++) g_pcmf[i] = g_pcm[i] * (1.0f / 32768.0f);
    return g_pcmf;
}
EXPORT("web_rate") int web_rate(void) { return SND_RATE; }
