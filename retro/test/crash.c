/* Renders the crash (SFX_CRASH) on its own: a splash and a kick as the carol breaks off, a sad trombone (wah, wah, wah, waaah),
 * and under the last note a kick and a crash of cymbals, then the kick again. test/crash.py checks the result. */
#include "../core/snd.c"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    g_theme.on = 1; snd_init(); snd_set_volume(0.7f); snd_set_fade(1.0f);
    snd_sfx(SFX_CRASH);
    int total = (int)(5.2f * SND_RATE);
    static int16_t buf[2 * 4096];
    FILE *f = fopen(argv[1], "wb");
    for (int done = 0; done < total; done += 4096) { int n = total - done < 4096 ? total - done : 4096; snd_render(buf, n); fwrite(buf, 4, (size_t)n, f); }
    fclose(f);
    return 0;
}
