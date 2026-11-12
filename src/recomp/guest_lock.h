// A guest CRITICAL_SECTION on the host: its owner's thread id and a waiter bit in one word, so an uncontended enter
// and leave are one compare-and-swap each. The console's host mutex took 326 ns an enter and leave and an atomic
// swap 98 (ISAAC360_BENCH, boot 13), and the engine's reference counting locks ~2,300 times a frame. A contended
// enter sleeps on a condition variable; the leave that finds the waiter bit wakes the waiters.
#pragma once
#include <stdint.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

struct GuestLock {
    std::atomic<uint32_t> word;   // owner id << 1 | 1 while threads wait (0: free)
    uint32_t count;               // the owner's entries (it alone touches it)
    uint32_t waiting;             // threads in guest_lock_wait (under m)
    std::mutex m;
    std::condition_variable cv;
    GuestLock() : word(0), count(0), waiting(0) {}
};

// owner ids come from GuestThread::id (runtime.h), from 1
inline bool guest_lock_try(GuestLock& l, uint32_t me) {
    if ((l.word.load(std::memory_order_relaxed) >> 1) == me) {
        ++l.count;
        return true;
    }
    uint32_t free = 0;
    if (l.word.compare_exchange_strong(free, me << 1, std::memory_order_acquire, std::memory_order_relaxed)) {
        l.count = 1;
        return true;
    }
    return false;
}

// the contended enter: marks the word, sleeps until a leave wakes it, takes the lock keeping the mark for the others
inline void guest_lock_wait(GuestLock& l, uint32_t me) {
    std::unique_lock<std::mutex> g(l.m);
    ++l.waiting;
    for (;;) {
        uint32_t w = l.word.load(std::memory_order_relaxed);
        if (w == 0) {
            --l.waiting;
            uint32_t free = 0;
            if (l.word.compare_exchange_strong(free, me << 1 | (l.waiting ? 1u : 0u), std::memory_order_acquire,
                                               std::memory_order_relaxed))
                break;
            ++l.waiting;
            continue;
        }
        if (!(w & 1) && !l.word.compare_exchange_strong(w, w | 1, std::memory_order_relaxed)) continue;
        l.cv.wait(g);
    }
    l.count = 1;
}

inline void guest_lock_enter(GuestLock& l, uint32_t me) {
    if (!guest_lock_try(l, me)) guest_lock_wait(l, me);
}

// by the owner only (a leave by another thread, undefined on Windows, is ignored)
inline void guest_lock_leave(GuestLock& l, uint32_t me) {
    if ((l.word.load(std::memory_order_relaxed) >> 1) != me || --l.count) return;
    uint32_t mine = me << 1;
    if (l.word.compare_exchange_strong(mine, 0, std::memory_order_release, std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> g(l.m);
    l.word.store(0, std::memory_order_release);
    l.cv.notify_all();
}
