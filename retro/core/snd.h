/* A very small synth: a few voices, no samples. Effects, and in season a little carol sequencer (our own arrangements of
 * public-domain tunes: bells over a triangle bass, a shaker, a delay, a key change each time round). */
#ifndef SND_H
#define SND_H
#include <stdint.h>

#define SND_RATE 44100
enum { SFX_START, SFX_PICKUP, SFX_DIE };
enum { MU_NONE, MU_SILENT, MU_JINGLE, MU_DECK, MU_JOY, MU_WISH };

void snd_init(void);
void snd_set_volume(float v);                  /* master, 0..1 */
void snd_set_fade(float k);                    /* 0..1 on top of it: the picture's fades, heard */
void snd_sfx(int id);
void snd_sfx_note(int id, int note);           /* a pitched effect, note counted in pentatonic steps */
void snd_render(int16_t *out, int frames);     /* interleaved stereo */

/* carols (only in season: g_theme.on) */
void snd_music_play(int id);                   /* from the top; MU_NONE stops */
void snd_music_stop(void);
int  snd_music_id(void);
void snd_music_tempo(float mul);               /* 1 as written */
void snd_music_bump(int semitones);            /* up this far until the next play: a game can raise the key as things go */
#endif
