/* menumusic.c - see menumusic.h.
 *
 * The music is played from a ring buffer on one looping DSP voice. The voice
 * runs at 3/8 of the DSP's rate, so it moves exactly 36 frames through the
 * ring for every 2 ms audio block; the music player counts those blocks
 * (midi_audio_blocks), which says exactly where the voice is. A thread keeps
 * the ring filled about a quarter of a second ahead of it. The ring's size
 * is a whole number of the sound library's 1152-byte copy chunks, so the
 * voice never plays the padding the library adds after a short last chunk. */
#include "menumusic.h"
#include "musicsynth.h"
#include "midi.h"
#include "settings.h"
#include "thr.h"
#include "ui.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#ifdef GEKKO
#include <gccore.h>
#include <aesndlib.h>
#include <ogc/lwp_watchdog.h>
#endif

static volatile float target;    /* wanted loudness (0..1), set by the main thread */
static bool allowed;
static float start_at = -1;

void menumusic_start(float delay) {
    if (allowed || start_at >= 0) return;
    start_at = g_time + delay;
}

void menumusic_update(void) {
    if (!allowed && start_at >= 0 && g_time >= start_at) allowed = true;
    MidiState st = midi_state();
    bool on = allowed && g_set.menu_music && st == MIDI_STOPPED;
    target = on ? g_set.music_volume / 10.0f * 0.5f : 0;
}

#ifdef GEKKO

#define CHUNK_FRAMES 288                   /* 1152 bytes of 16-bit stereo */
#define RING_FRAMES (CHUNK_FRAMES * 64)    /* about one second */
#define LEAD 4500                          /* frames written ahead of the voice */
#define FRAMES_PER_BLOCK 36

static int16_t ring[RING_FRAMES * 2] __attribute__((aligned(32)));
static AESNDPB *voice;
static thr_t th;
static bool th_ok;
static volatile bool quit;

/* where the voice is (frames since it started) */
static uint32_t blk0;
static u64 tick0;
static uint64_t voice_pos(void) {
    uint32_t b = midi_audio_blocks();
    if (b) return (uint64_t)(b - blk0) * FRAMES_PER_BLOCK;
    /* no block counter (the music player didn't start): go by the clock */
    return (uint64_t)ticks_to_microsecs(gettime() - tick0) * MS_RATE / 1000000u;
}

static void *mm_thread(void *arg) {
    (void)arg;
    bool playing = false;
    float level = 0;
    uint64_t written = 0, quiet_until = 0;
    bool reset_done = false;
    while (!quit) {
        float want = target;
        if (!playing) {
            if (want <= 0) { thr_sleep_ms(30); continue; }
            if (!reset_done) { ms_reset(); reset_done = true; }
            memset(ring, 0, sizeof(ring));
            DCFlushRange(ring, sizeof(ring));
            blk0 = midi_audio_blocks();
            tick0 = gettime();
            written = 600;
            AESND_SetVoiceVolume(voice, 255, 255);
            AESND_PlayVoice(voice, VOICE_STEREO16, ring, sizeof(ring), MS_RATE, 0, true);
            playing = true;
            quiet_until = 0;
        }
        uint64_t pos = voice_pos();
        if (written < pos + 600) written = pos + 600;   /* fell behind: skip ahead */
        uint64_t goal = pos + LEAD;
        while (written < goal) {
            int at = (int)(written % RING_FRAMES);
            int n = (int)(goal - written);
            if (n > 256) n = 256;
            if (n > RING_FRAMES - at) n = RING_FRAMES - at;
            /* fade towards the wanted loudness: in over 2.5 s, out over 1 s */
            float step = n / (float)MS_RATE / (want > level ? 2.5f : 1.0f);
            if (level < want) { level += step * 0.5f; if (level > want) level = want; }
            else if (level > want) { level -= step * 0.5f; if (level < want) level = want; }
            if (level > 0) ms_render(&ring[at * 2], n, level);
            else memset(&ring[at * 2], 0, (size_t)n * 4);
            DCFlushRange(&ring[at * 2], (u32)n * 4);
            written += (uint64_t)n;
        }
        /* silent for a while: let the voice go */
        if (level <= 0 && want <= 0) {
            if (!quiet_until) quiet_until = written + RING_FRAMES;
            else if (pos > quiet_until) {
                AESND_SetVoiceStop(voice, true);
                playing = false;
                continue;
            }
        } else {
            quiet_until = 0;
        }
        thr_sleep_ms(8);
    }
    if (playing) AESND_SetVoiceStop(voice, true);
    return NULL;
}

void menumusic_init(void) {
    voice = AESND_AllocateVoice(NULL);
    if (!voice) return;
    quit = false;
    th_ok = thr_create(&th, mm_thread, NULL, 32 * 1024, 105);
}

void menumusic_shutdown(void) {
    if (!th_ok) return;
    quit = true;
    thr_join(th);
    th_ok = false;
}

#else   /* PC test build: no sound */

void menumusic_init(void) {}
void menumusic_shutdown(void) {}

#endif
