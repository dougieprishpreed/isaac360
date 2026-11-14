// A guest CRITICAL_SECTION on the host: its owner's thread id, a "biased" bit and a waiter bit in one word.
//
// Biased to the main thread: a new lock belongs to the game's main thread, which enters and leaves it with plain
// loads and stores, no atomic instruction and no barrier. The engine's reference counting alone locks 6,000-12,500
// times a frame on the main thread in large rooms, and on the console each atomic compare-and-swap costs ~130 ns
// (an uncontended GuestLock enter and leave 230 ns, the game's add-ref 430 ns: ISAAC360_BENCH, boot 21), a quarter
// of a large room's frame. Another thread that wants a biased lock asks for it (guest_lock_revoke): the main thread
// clears the bit between two of its lock operations, or the asking thread clears it itself while the main thread is
// blocked (parked: sleeps, waits, the present). From then on the lock is shared: owner id and waiter bit change by
// compare-and-swap, a contended enter sleeps on a condition variable and the leave that finds the waiter bit wakes
// the waiters. Each lock is handed over at most once; the game's other threads (sound, input, video) touch few.
#pragma once
#include <stdint.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

enum { kLockWaiting = 1u, kLockBiased = 2u };       // the word: owner id << 2 | biased | waiting (0: free, shared)

extern uint32_t g_lockMainId;                       // the main thread's GuestThread::id (0 until it runs)
extern std::atomic<uint32_t> g_lockRevokePending;   // another thread waits for biased locks (the main thread serves)
void guest_lock_serve();                            // the main thread: hand over the locks asked for
void guest_lock_revoke_slow(struct GuestLock& l, uint32_t me);   // another thread: returns once l is shared
void guest_lock_main_park();                        // the main thread is about to block: others may take locks
void guest_lock_main_unpark();
void guest_lock_set_main(uint32_t id);

struct GuestLock {
    std::atomic<uint32_t> word;
    uint32_t count;               // the owner's entries (it alone touches it)
    uint32_t waiting;             // threads in guest_lock_wait (under m)
    std::mutex m;
    std::condition_variable cv;
    GuestLock() : word(g_lockMainId ? kLockBiased : 0u), count(0), waiting(0) {}
};

// the main thread, between lock operations: hand over what others asked for (a plain load when nothing is asked)
inline void guest_lock_main_check() {
    if (g_lockRevokePending.load(std::memory_order_relaxed)) guest_lock_serve();
}

// owner ids come from GuestThread::id (runtime.h), from 1
inline bool guest_lock_try(GuestLock& l, uint32_t me) {
    uint32_t w = l.word.load(std::memory_order_relaxed);
    if (w & kLockBiased) {
        if (me == g_lockMainId) {
            if (g_lockRevokePending.load(std::memory_order_relaxed)) {
                guest_lock_serve();
                w = l.word.load(std::memory_order_relaxed);
            }
            if (w & kLockBiased) {                  // free, or the main thread's own
                if ((w >> 2) == me) ++l.count;
                else {
                    l.word.store(me << 2 | kLockBiased, std::memory_order_relaxed);
                    l.count = 1;
                }
                return true;
            }
        } else {
            guest_lock_revoke_slow(l, me);
        }
        w = l.word.load(std::memory_order_relaxed);
    }
    if ((w >> 2) == me) {
        ++l.count;
        return true;
    }
    uint32_t free = 0;
    if (l.word.compare_exchange_strong(free, me << 2, std::memory_order_acquire, std::memory_order_relaxed)) {
        l.count = 1;
        return true;
    }
    return false;
}

// the contended enter: marks the word, sleeps until a leave wakes it, takes the lock keeping the mark for the others
// (the lock is shared by now: a biased lock is free or the main thread's, and try took or handed over those)
inline void guest_lock_wait(GuestLock& l, uint32_t me) {
    bool main = me == g_lockMainId;
    if (main) guest_lock_main_park();
    {
        std::unique_lock<std::mutex> g(l.m);
        ++l.waiting;
        for (;;) {
            uint32_t w = l.word.load(std::memory_order_relaxed);
            if (w == 0) {
                --l.waiting;
                uint32_t free = 0;
                if (l.word.compare_exchange_strong(free, me << 2 | (l.waiting ? kLockWaiting : 0u), std::memory_order_acquire,
                                                   std::memory_order_relaxed))
                    break;
                ++l.waiting;
                continue;
            }
            if (!(w & kLockWaiting) && !l.word.compare_exchange_strong(w, w | kLockWaiting, std::memory_order_relaxed)) continue;
            l.cv.wait(g);
        }
    }
    l.count = 1;
    if (main) guest_lock_main_unpark();
}

inline void guest_lock_enter(GuestLock& l, uint32_t me) {
    if (!guest_lock_try(l, me)) guest_lock_wait(l, me);
}

// by the owner only (a leave by another thread, undefined on Windows, is ignored)
inline void guest_lock_leave(GuestLock& l, uint32_t me) {
    uint32_t w = l.word.load(std::memory_order_relaxed);
    if ((w >> 2) != me) return;
    if (w & kLockBiased) {                          // the main thread's, plainly
        if (!--l.count) l.word.store(kLockBiased, std::memory_order_relaxed);
        return;
    }
    if (--l.count) return;
    uint32_t mine = me << 2;
    if (l.word.compare_exchange_strong(mine, 0, std::memory_order_release, std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> g(l.m);
    l.word.store(0, std::memory_order_release);
    l.cv.notify_all();
}
