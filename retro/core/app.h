/* Platform-independent game shell: the scenes, switching between them, a fade, palette -> RGB. The browser shell and the
 * test harness both sit on top of this. (Slimmed from HAUNTED.EXE's src/core/app.h.) */
#ifndef APP_H
#define APP_H

#include "core.h"

typedef struct AppConfig {
    int sound;                      /* 1 = play audio */
    int volume;                     /* 0..100 */
    int brightness;                 /* 30..100 % - global cap, easier on OLED */
    int start_fade;                 /* fade in from black at start */
} AppConfig;

void app_default_config(AppConfig *c);
void app_init(const AppConfig *cfg);
void app_select(int scene);                         /* straight to a scene (before the first frame) */
int  app_scene(void);                               /* the current one */
const char *app_scene_name(int scene);             /* the canonical name (also what ?mode= matches) */
const char *app_scene_title(int scene);            /* the name it goes by in season */
void app_set_theme(int on, int pair);              /* before app_init */
void app_set_input(Input *in);
void app_update(double dt);                         /* clears the input's edges after the step */
int  app_mode(int *w, int *h);                      /* current video mode */
void app_present(uint32_t *dst, int bgr);           /* w*h pixels; bgr=0: RGBA bytes, 1: 0x00RRGGBB */
void app_present_rgb(uint8_t *rgb, int *w, int *h); /* tightly packed, for tests */
const Frame *app_frame(void);

/* the saved bytes: the best scores */
void app_slot_load(const uint8_t *buf, int len);    /* from storage, before app_init */
int  app_slot_take(uint8_t *buf, int cap);          /* written since last asked: its length, else 0 */

#endif
