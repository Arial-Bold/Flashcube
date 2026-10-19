/* sound.c - plays the menu sounds (synthesised at start-up by sfxsynth.c) on
 * the DSP's hardware voices through libogc2's AESND library, which the music
 * player uses too. From CubeBrowser. */
#include "sound.h"
#include "settings.h"
#include "menumusic.h"
#include <gccore.h>
#include <aesndlib.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define RATE SFX_RATE

static s16 *buffers[SFX_COUNT];
static u32 sizes[SFX_COUNT];
static bool ready;

static void synth(int id) {
    int n = sfx_length(id);
    if (n <= 0) return;
    u32 bytes = ((u32)n * 2 + 31) & ~31u;
    s16 *buf = memalign(32, bytes);
    if (!buf) return;
    memset(buf, 0, bytes);
    if (!sfx_render(id, buf, n)) { free(buf); return; }
    DCFlushRange(buf, bytes);
    buffers[id] = buf;
    sizes[id] = bytes;
}

/* A few voices of their own; each plays one sound once and stops itself. */
#define SFX_VOICES 6
static AESNDPB *sfx_voice[SFX_VOICES];
static u64 sfx_busy_until[SFX_VOICES];
static int sfx_next;

void sound_init(void) {
    AESND_Init();
    AESND_Pause(false);
    for (int i = 0; i < SFX_VOICES; i++) sfx_voice[i] = AESND_AllocateVoice(NULL);
    for (int i = 0; i < SFX_COUNT; i++) synth(i);
    ready = true;
}

void sound_shutdown(void) {
    if (!ready) return;
    ready = false;
    menumusic_shutdown();
    AESND_Pause(true);
    AESND_Reset();
}

void sound_play(Sfx s) {
    /* option moves take turns between three sounds, like the system menu */
    static int move_turn;
    if (s == SFX_HOVER) { s = (Sfx)(SFX_HOVER + move_turn); move_turn = (move_turn + 1) % 3; }
    if (!ready || !g_set.sounds || g_set.volume == 0 || !buffers[s]) return;
    /* prefer a voice whose last sound has finished: the audio interrupt
     * isn't using it, so starting it can't collide */
    u64 now = gettime();
    int v = -1;
    for (int k = 0; k < SFX_VOICES && v < 0; k++) {
        int i = (sfx_next + k) % SFX_VOICES;
        if (sfx_voice[i] && now >= sfx_busy_until[i]) v = i;
    }
    if (v < 0) return;
    sfx_next = (v + 1) % SFX_VOICES;
    u16 vol = (u16)(g_set.volume * 255 / 10);
    AESND_SetVoiceVolume(sfx_voice[v], vol, vol);
    AESND_PlayVoice(sfx_voice[v], VOICE_MONO16, buffers[s], sizes[s], RATE, 0, false);
    sfx_busy_until[v] = now + millisecs_to_ticks((sizes[s] / 2) * 1000 / RATE + 30);
}
