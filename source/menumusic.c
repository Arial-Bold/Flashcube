/* menumusic.c - see menumusic.h.
 *
 * The music is streamed to two mono DSP voices (left and right) in chunks,
 * the way the music player streams its notes: a thread renders chunks into
 * a queue ahead of time, and each time the sound library has finished
 * copying the current chunk it asks (from the audio interrupt) for the next
 * one. Nothing has to guess where the voices have got to, and if the thread
 * ever falls behind they're given a chunk of silence, never old or
 * half-written sound. The thread times itself, and if making the music
 * takes too much of the processor it switches to a lighter mix.
 *
 * Each voice also waits, before each of its copies, for any copy to sound
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
#include <ogc/machine/processor.h>
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

/* Two mono voices, one per speaker, streamed exactly like the music
 * player's notes (which play cleanly); the left voice decides which chunk
 * comes next and the right one follows it, so they never drift apart. */
#define CHUNK_FRAMES 1152                  /* 2304 bytes a channel: two of the library's copies */
#define CHUNK_BYTES (CHUNK_FRAMES * 2)
#define NCHUNKS 24                         /* the queue: about 1.5 seconds */

static int16_t chunk_l[NCHUNKS][CHUNK_FRAMES] __attribute__((aligned(32)));
static int16_t chunk_r[NCHUNKS][CHUNK_FRAMES] __attribute__((aligned(32)));
static int16_t quiet[CHUNK_FRAMES] __attribute__((aligned(32)));
static int16_t stereo[CHUNK_FRAMES * 2];
static volatile uint32_t handed_l, handed_r;   /* chunks given to each voice (in the interrupt) */
static volatile uint32_t filled;               /* chunks rendered (by the thread) */
static volatile uint32_t gaps;                 /* times the thread fell behind */
static volatile float load;                    /* time spent making music, of real time (0..1) */
static volatile bool voice_on;
static AESNDPB *vl, *vr;
static thr_t th;
static bool th_ok;
static volatile bool quit;

static void wait_copies(void) {
    for (int k = 0; k < 4000 && AR_GetDMAStatus(); k++) {}   /* let another voice's copy finish */
}

/* Run in the audio interrupt. */
static void left_cb(AESNDPB *pb, u32 st) {
    if (st == VOICE_STATE_RUNNING) wait_copies();
    else if (st == VOICE_STATE_STREAM) {
        if (filled > handed_l) { AESND_SetVoiceBuffer(pb, chunk_l[handed_l % NCHUNKS], CHUNK_BYTES); handed_l++; }
        else { AESND_SetVoiceBuffer(pb, quiet, CHUNK_BYTES); gaps++; }
    }
}
static void right_cb(AESNDPB *pb, u32 st) {
    if (st == VOICE_STATE_RUNNING) wait_copies();
    else if (st == VOICE_STATE_STREAM) {
        if (handed_r < handed_l) { AESND_SetVoiceBuffer(pb, chunk_r[handed_r % NCHUNKS], CHUNK_BYTES); handed_r++; }
        else AESND_SetVoiceBuffer(pb, quiet, CHUNK_BYTES);
    }
}

/* makes chunk number filled (both channels), and times it */
static void make_chunk(float gain) {
    u64 t0 = gettime();
    int k = (int)(filled % NCHUNKS);
    if (gain > 0) {
        ms_render(stereo, CHUNK_FRAMES, gain);
        for (int i = 0; i < CHUNK_FRAMES; i++) { chunk_l[k][i] = stereo[i * 2]; chunk_r[k][i] = stereo[i * 2 + 1]; }
    } else {
        memset(chunk_l[k], 0, CHUNK_BYTES);
        memset(chunk_r[k], 0, CHUNK_BYTES);
    }
    float used = (float)ticks_to_microsecs(gettime() - t0) / (CHUNK_FRAMES * 1000000.0f / MS_RATE);
    load = load * 0.9f + used * 0.1f;
    /* busy: switch to the lighter mix (and back when there's room again) */
    static int lite_on;
    if (!lite_on && load > 0.45f) { lite_on = 1; ms_set_lite(1); }
    else if (lite_on && load < 0.20f) { lite_on = 0; ms_set_lite(0); }
    filled++;
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
            handed_l = handed_r = filled = 0;
            for (int i = 0; i < 4; i++) { level += (want - level) * 0.05f; make_chunk(level); }
            AESND_SetVoiceVolume(vl, 255, 0);
            AESND_SetVoiceVolume(vr, 0, 255);
            /* both start in the same 2 ms block, so they stay in step */
            u32 isr;
            _CPU_ISR_Disable(isr);
            AESND_PlayVoice(vl, VOICE_MONO16, chunk_l[0], CHUNK_BYTES, MS_RATE, 0, true);
            AESND_SetVoiceStream(vl, true);
            AESND_PlayVoice(vr, VOICE_MONO16, chunk_r[0], CHUNK_BYTES, MS_RATE, 0, true);
            AESND_SetVoiceStream(vr, true);
            handed_l = handed_r = 1;
            _CPU_ISR_Restore(isr);
            voice_on = true;
            quiet_chunks = 0;
        }
        /* keep the queue full (the chunk being copied is never touched) */
        while (filled < handed_r + NCHUNKS - 2 && !quit) {
            want = target;
            /* fade towards the wanted loudness: in over about 2.5 s, out over 1 s */
            float step = CHUNK_FRAMES / (float)MS_RATE / (want > level ? 2.5f : 1.0f) * 0.5f;
            if (level < want) { level += step; if (level > want) level = want; }
            else if (level > want) { level -= step; if (level < want) level = want; }
            make_chunk(level);
            quiet_chunks = level > 0 ? 0 : quiet_chunks + 1;
        }
        /* silent for a good while: let the voices go */
        if (level <= 0 && want <= 0 && quiet_chunks > NCHUNKS * 2) {
            AESND_SetVoiceStop(vl, true);
            AESND_SetVoiceStop(vr, true);
            voice_on = false;
            continue;
        }
        thr_sleep_ms(10);
    }
    if (voice_on) { AESND_SetVoiceStop(vl, true); AESND_SetVoiceStop(vr, true); }
    return NULL;
}

void menumusic_init(void) {
    vl = AESND_AllocateVoice(left_cb);
    vr = AESND_AllocateVoice(right_cb);
    if (!vl || !vr) return;
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

void menumusic_stats(int *load_pct, int *gap_count) {
    *load_pct = (int)(load * 100 + 0.5f);
    *gap_count = (int)gaps;
}

#else   /* PC test build: no sound */

void menumusic_init(void) {}
void menumusic_shutdown(void) {}
void menumusic_stats(int *load_pct, int *gap_count) { *load_pct = 0; *gap_count = 0; }

#endif
