/* menumusic.c - see menumusic.h.
 *
 * The music is streamed to one DSP voice in chunks, the way the music
 * player streams its notes: a thread renders chunks into a queue ahead of
 * time, and each time the sound library has finished copying the current
 * chunk it asks (from the audio interrupt) for the next one. Nothing has to
 * guess where the voice has got to, and if the thread ever falls behind the
 * voice is given a chunk of silence, never old or half-written sound.
 *
 * The voice also waits, before each of its copies, for any copy to sound
 * memory still in progress: the library copies every voice through one
 * shared buffer, so two copies overlapping mix their data (heard as
 * crackles and sudden loud bursts). The music player does the same. */
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
#include <ogc/aram.h>
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

#define CHUNK_FRAMES 576                   /* 2304 bytes: two of the library's copies */
#define CHUNK_BYTES (CHUNK_FRAMES * 4)
#define NCHUNKS 40                         /* the queue: about 1.3 seconds */

static int16_t chunks[NCHUNKS][CHUNK_FRAMES * 2] __attribute__((aligned(32)));
static int16_t quiet[CHUNK_FRAMES * 2] __attribute__((aligned(32)));
static volatile uint32_t handed;   /* chunks given to the voice (written by the interrupt) */
static volatile uint32_t filled;   /* chunks rendered (written by the thread) */
static volatile bool voice_on;
static AESNDPB *voice;
static thr_t th;
static bool th_ok;
static volatile bool quit;

/* Runs in the audio interrupt. */
static void voice_cb(AESNDPB *pb, u32 st) {
    if (st == VOICE_STATE_RUNNING) {
        for (int k = 0; k < 4000 && AR_GetDMAStatus(); k++) {}   /* let another voice's copy finish */
    } else if (st == VOICE_STATE_STREAM) {
        if (filled > handed) {
            AESND_SetVoiceBuffer(pb, chunks[handed % NCHUNKS], CHUNK_BYTES);
            handed++;
        } else {
            AESND_SetVoiceBuffer(pb, quiet, CHUNK_BYTES);   /* the thread fell behind */
        }
    }
}

static void *mm_thread(void *arg) {
    (void)arg;
    float level = 0;
    bool reset_done = false;
    int quiet_chunks = 0;
    while (!quit) {
        float want = target;
        if (!voice_on) {
            if (want <= 0) { thr_sleep_ms(30); continue; }
            if (!reset_done) { ms_reset(); reset_done = true; }
            handed = filled = 0;
            /* a few chunks ready before it starts */
            for (int i = 0; i < 6; i++) {
                level += (want - level) * 0.02f;
                ms_render(chunks[i], CHUNK_FRAMES, level);
                filled++;
            }
            AESND_SetVoiceVolume(voice, 255, 255);
            AESND_PlayVoice(voice, VOICE_STEREO16, chunks[0], CHUNK_BYTES, MS_RATE, 0, true);
            AESND_SetVoiceStream(voice, true);
            handed = 1;
            voice_on = true;
            quiet_chunks = 0;
        }
        /* keep the queue full (one chunk always left for the voice to be copying) */
        while (filled < handed + NCHUNKS - 2 && !quit) {
            want = target;
            /* fade towards the wanted loudness: in over about 2.5 s, out over 1 s */
            float step = CHUNK_FRAMES / (float)MS_RATE / (want > level ? 2.5f : 1.0f) * 0.5f;
            if (level < want) { level += step; if (level > want) level = want; }
            else if (level > want) { level -= step; if (level < want) level = want; }
            int16_t *c = chunks[filled % NCHUNKS];
            if (level > 0) { ms_render(c, CHUNK_FRAMES, level); quiet_chunks = 0; }
            else { memset(c, 0, CHUNK_BYTES); quiet_chunks++; }
            filled++;
        }
        /* silent for a good while: let the voice go */
        if (level <= 0 && want <= 0 && quiet_chunks > NCHUNKS * 2) {
            AESND_SetVoiceStop(voice, true);
            voice_on = false;
            continue;
        }
        thr_sleep_ms(8);
    }
    if (voice_on) AESND_SetVoiceStop(voice, true);
    return NULL;
}

void menumusic_init(void) {
    voice = AESND_AllocateVoice(voice_cb);
    if (!voice) return;
    memset(quiet, 0, sizeof(quiet));
    DCFlushRange(quiet, sizeof(quiet));
    quit = false;
    th_ok = thr_create(&th, mm_thread, NULL, 32 * 1024, 115);
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
