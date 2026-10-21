/* aramcache.c - see aramcache.h. The copy routine is CubeBrowser's: small
 * pieces with interrupts off, because the sound library starts its own
 * copies from the audio interrupt and one starting mid-copy would cut ours
 * short (in the browser that was heard as a loud buzz). */
#include "aramcache.h"
#include <string.h>
#ifdef GEKKO
#include <gccore.h>
#include <ogc/aram.h>
#include <ogc/machine/processor.h>
#include "thr.h"

#define MAX_SLOTS 256
typedef struct { uint32_t key; uint32_t used; bool full; } Slot;
static Slot slots[MAX_SLOTS];
static int nslots;
static uint32_t slot_size, clock_;
static thr_mutex lock;
static bool ready;

#define PIECE 4096u
static bool wait_idle(void) {
    for (int k = 0; k < 400000; k++) if (!AR_GetDMAStatus()) return true;
    return false;
}

/* Our copies raise the copy engine's "finished" interrupt too, which the
 * sound library's copy queue would take as the end of its own copy (and then
 * start its next one on top of a copy still running: garbled sound). So
 * unless the flag was already up (someone else's finish), it's cleared after
 * ours. */
#define DSP_CSR (*(volatile u16 *)0xCC00500A)

static bool dma(u32 dir, void *mram, uint32_t aram, uint32_t len) {
    for (uint32_t off = 0; off < len; off += PIECE) {
        uint32_t n = len - off < PIECE ? len - off : PIECE;
        u32 level;
        _CPU_ISR_Disable(level);
        bool ok = wait_idle();
        if (ok) {
            bool flagged = (DSP_CSR & 0x0020) != 0;
            AR_StartDMA(dir, (u32)MEM_VIRTUAL_TO_PHYSICAL((char *)mram + off), aram + off, n);
            ok = wait_idle();
            if (ok && !flagged) DSP_CSR = (u16)((DSP_CSR & (u16)~(0x0080 | 0x0008)) | 0x0020);
        }
        _CPU_ISR_Restore(level);
        if (!ok) return false;
    }
    return true;
}

void aramcache_init(uint32_t slot_bytes) {
    if (ready || !AR_CheckInit() || (slot_bytes & 31) || !slot_bytes) return;
    uint32_t size = AR_GetSize();
    if (size <= ARAM_MUSIC_TOP) return;
    slot_size = slot_bytes;
    nslots = (int)((size - ARAM_MUSIC_TOP) / slot_bytes);
    if (nslots > MAX_SLOTS) nslots = MAX_SLOTS;
    ready = nslots > 0 && thr_mutex_init(&lock);
}

int aramcache_slots(void) { return ready ? nslots : 0; }

static int find(uint32_t key) {
    for (int i = 0; i < nslots; i++) if (slots[i].full && slots[i].key == key) return i;
    return -1;
}

bool aramcache_put(uint32_t key, const void *data) {
    if (!ready || ((uintptr_t)data & 31)) return false;
    static unsigned char check[PIECE] __attribute__((aligned(32)));
    thr_lock(&lock);
    int s = find(key);
    if (s < 0) {
        s = 0;
        for (int i = 0; i < nslots; i++) {
            if (!slots[i].full) { s = i; break; }
            if (slots[i].used < slots[s].used) s = i;
        }
    }
    slots[s].full = false;
    uint32_t at = ARAM_MUSIC_TOP + (uint32_t)s * slot_size;
    DCFlushRange((void *)data, slot_size);
    bool ok = dma(AR_MRAMTOARAM, (void *)data, at, slot_size);
    /* read back a piece to be sure the copy wasn't disturbed */
    if (ok) {
        uint32_t n = slot_size < PIECE ? slot_size : PIECE;
        DCInvalidateRange(check, n);
        ok = dma(AR_ARAMTOMRAM, check, at, n) && memcmp(check, data, n) == 0;
    }
    if (ok) {
        slots[s].key = key;
        slots[s].used = ++clock_;
        slots[s].full = true;
    }
    thr_unlock(&lock);
    return ok;
}

bool aramcache_get(uint32_t key, void *dst) {
    if (!ready || ((uintptr_t)dst & 31)) return false;
    thr_lock(&lock);
    int s = find(key);
    bool ok = false;
    if (s >= 0) {
        DCInvalidateRange(dst, slot_size);
        ok = dma(AR_ARAMTOMRAM, dst, ARAM_MUSIC_TOP + (uint32_t)s * slot_size, slot_size);
        if (ok) slots[s].used = ++clock_;
        else slots[s].full = false;
    }
    thr_unlock(&lock);
    return ok;
}

void aramcache_clear(void) {
    if (!ready) return;
    thr_lock(&lock);
    for (int i = 0; i < nslots; i++) slots[i].full = false;
    thr_unlock(&lock);
}
#else
/* On a PC (test programs): no audio memory. */
void aramcache_init(uint32_t slot_bytes) { (void)slot_bytes; }
int  aramcache_slots(void) { return 0; }
bool aramcache_put(uint32_t key, const void *data) { (void)key; (void)data; return false; }
bool aramcache_get(uint32_t key, void *dst) { (void)key; (void)dst; return false; }
void aramcache_clear(void) {}
#endif
