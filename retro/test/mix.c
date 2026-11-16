/* Renders a carol as the game plays it (melody, bass and shaker, the delay and the limiter) and reports how many voices were
 * sounding at the busiest moment; test/mix.py checks the audio. This is the test that catches a runaway sequencer. */
#include "../core/snd.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 4) return 1;
    int id = atoi(argv[1]); float secs = (float)atof(argv[2]);
    g_theme.on = 1; snd_init(); snd_set_volume(0.7f); snd_set_fade(1.0f); snd_music_play(id);
    int total = (int)(secs * SND_RATE), maxv = 0;
    static int16_t buf[2 * 512];
    FILE *f = fopen(argv[3], "wb");
    for (int done = 0; done < total; done += 512) {
        int n = total - done < 512 ? total - done : 512; snd_render(buf, n); fwrite(buf, 4, (size_t)n, f);
        int live = 0; for (int i = 0; i < VOICES; i++) live += V[i].on; if (live > maxv) maxv = live;
    }
    fclose(f);
    printf("%d\n", maxv);
    return 0;
}
