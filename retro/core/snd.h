/* A very small synth: a few voices, no samples. Effects only for now; a music program can slot in beside them
 * (HAUNTED.EXE's snd.c is the full thing: procedural music, sound programs, fades). */
#ifndef SND_H
#define SND_H
#include <stdint.h>

#define SND_RATE 44100
enum { SFX_START, SFX_PICKUP, SFX_DIE };

void snd_init(void);
void snd_set_volume(float v);                  /* master, 0..1 */
void snd_sfx(int id);
void snd_sfx_note(int id, int note);           /* a pitched effect, note counted in pentatonic steps */
void snd_render(int16_t *out, int frames);     /* interleaved stereo */
#endif
