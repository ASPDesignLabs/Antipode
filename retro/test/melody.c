/* Renders each carol's tune alone (no bass, shaker or echo) and prints what it should sound like: one line per note,
 *   N <start s> <length s> <MIDI note>
 * with the key changes and the minor mode already applied. test/melody.py listens to the audio and checks each note. */
#define SND_TEST_MELODY
#include "../core/snd.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: melody <track 1..5> <loops> <out.raw>\n"); return 1; }
    int id = atoi(argv[1]), loops = atoi(argv[2]);
    g_theme.on = 1; snd_init(); snd_set_volume(1.0f); snd_set_fade(1.0f);
    snd_music_play(id);
    const Track *k = TRACKS[id];
    float unit = 60.0f / (float)k->upm, t = 0.0f;
    for (int L = 0; L < loops; L++) {
        int trans = k->mods[L % k->nmods];
        for (int i = 0; i < k->nn; i++) { Nt n = k->n[i]; if (n.m >= 0) printf("N %.4f %.4f %d\n", t, n.l * unit, shape(k, n.m) + trans); t += n.l * unit; }
    }
    printf("T %.4f\n", t);
    int total = (int)(t * SND_RATE) + SND_RATE / 2;
    static int16_t buf[2 * 4096];
    FILE *f = fopen(argv[3], "wb");
    for (int done = 0; done < total; done += 4096) { int n = total - done < 4096 ? total - done : 4096; snd_render(buf, n); fwrite(buf, 4, (size_t)n, f); }
    fclose(f);
    return 0;
}
