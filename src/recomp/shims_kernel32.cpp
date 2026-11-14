// KERNEL32/WINMM/ADVAPI32/ole32/SHELL32/dbghelp imports (stdcall). Handles and synchronisation objects are host
// objects keyed by guest values.
#include "runtime.h"
#include "guest_lock.h"
#include <algorithm>
#include <ctype.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#if defined(_WIN32)
#include <filesystem>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <system_error>
#include <map>
#include <set>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

uint32_t shim_proc_address(const std::string& module, const std::string& name);   // shims_all.cpp

// ------------------------------------------------------------------------------------------------ biased locks
// (guest_lock.h) The handover of the main thread's biased locks to another thread that wants one: the asker lists
// the lock and waits until the main thread has served the list (between its lock operations, guest_lock_main_check)
// or, while the main thread is parked in a wait, serves it itself. Serving clears the biased bit of each listed lock;
// the main thread's plain stores before it are published by the release that ends the serving.
uint32_t g_lockMainId = 0;
std::atomic<uint32_t> g_lockRevokePending(0);
namespace {
std::mutex g_revokeLock;
std::vector<GuestLock*> g_revokeList;
uint64_t g_revokeAsked = 0;                             // under g_revokeLock
std::atomic<uint64_t> g_revokeDone(0);
std::atomic<int> g_mainState(0);                        // 0 running, 1 parked (blocked in a wait), 2 being served
std::atomic<uint32_t> g_revoked(0), g_revokeSlowMs(0);

void serve_listed() {                                   // with g_revokeLock
    for (size_t i = 0; i < g_revokeList.size(); ++i) {
        GuestLock* l = g_revokeList[i];
        l->word.store(l->word.load(std::memory_order_relaxed) & ~(uint32_t)kLockBiased, std::memory_order_relaxed);
    }
    g_revoked.fetch_add((uint32_t)g_revokeList.size(), std::memory_order_relaxed);
    g_revokeList.clear();
    g_lockRevokePending.store(0, std::memory_order_relaxed);
    g_revokeDone.store(g_revokeAsked, std::memory_order_release);
}
}  // namespace

void guest_lock_set_main(uint32_t id) { g_lockMainId = id; }

void guest_lock_serve() {
    std::lock_guard<std::mutex> g(g_revokeLock);
    serve_listed();
}

void guest_lock_revoke_slow(GuestLock& l, uint32_t me) {
    (void)me;
    uint64_t ticket;
    {
        std::lock_guard<std::mutex> g(g_revokeLock);
        if (!(l.word.load(std::memory_order_relaxed) & kLockBiased)) return;
        g_revokeList.push_back(&l);
        ticket = ++g_revokeAsked;
        g_lockRevokePending.store(1, std::memory_order_relaxed);
    }
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    for (uint32_t spins = 0;; ++spins) {
        if (g_revokeDone.load(std::memory_order_acquire) >= ticket) break;
        int parked = 1;
        if (g_mainState.compare_exchange_strong(parked, 2, std::memory_order_acquire)) {
            {
                std::lock_guard<std::mutex> g(g_revokeLock);
                serve_listed();
            }
            g_mainState.store(1, std::memory_order_release);
            break;
        }
        if (spins > 200) std::this_thread::yield();
    }
    // a main thread that neither locks nor waits for long (a spin on guest memory) would hold the asker here
    uint32_t ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (ms > g_revokeSlowMs.load(std::memory_order_relaxed)) {
        g_revokeSlowMs.store(ms, std::memory_order_relaxed);
        if (ms >= 50) recomp_log("locks: a thread waited %u ms for the main thread to hand over a lock", ms);
    }
}

void guest_lock_main_park() { g_mainState.store(1, std::memory_order_release); }

void guest_lock_main_unpark() {
    for (;;) {
        int parked = 1;
        if (g_mainState.compare_exchange_weak(parked, 0, std::memory_order_acquire)) return;
        std::this_thread::yield();                      // another thread is serving the list meanwhile
    }
}

uint32_t shims_locks_handed_over() { return g_revoked.load(std::memory_order_relaxed); }

namespace {
// ------------------------------------------------------------------------------------------------ sync
// A critical section is a GuestLock (guest_lock.h) found by the guest structure's address. The game enters and
// leaves them thousands of times a frame (its reference counts, sound, resource and log locks), so the lookup takes
// no lock: an open-addressed table whose slots are written once (the key last, after the lock) and never removed,
// as Windows' are never moved. A full table (it holds far more than the game makes) falls back to a map under a
// lock.
struct CsSlot {
    std::atomic<uint32_t> key;
    GuestLock* m;
    std::atomic<uint32_t> holder0, holder1;      // ISAAC360_PROFILE: the code that last entered it
};
const uint32_t kCsSlots = 8192;                    // a power of two
CsSlot g_csSlots[kCsSlots];
uint32_t g_csUsed = 0;
std::mutex g_csLock;
std::unordered_map<uint32_t, CsSlot*> g_csMore;

CsSlot* cs_slot(uint32_t a) {
    uint32_t i = (a >> 3) * 2654435761u & (kCsSlots - 1);
    for (uint32_t n = 0; n < 64; ++n, i = (i + 1) & (kCsSlots - 1)) {
        uint32_t k = g_csSlots[i].key.load(std::memory_order_acquire);
        if (k == a) return &g_csSlots[i];
        if (!k) break;
    }
    std::lock_guard<std::mutex> l(g_csLock);
    i = (a >> 3) * 2654435761u & (kCsSlots - 1);
    for (uint32_t n = 0; n < 64 && g_csUsed < kCsSlots * 3 / 4; ++n, i = (i + 1) & (kCsSlots - 1)) {
        uint32_t k = g_csSlots[i].key.load(std::memory_order_relaxed);
        if (k == a) return &g_csSlots[i];
        if (!k) {
            g_csSlots[i].m = new GuestLock;
            g_csSlots[i].key.store(a, std::memory_order_release);
            ++g_csUsed;
            return &g_csSlots[i];
        }
    }
    CsSlot*& more = g_csMore[a];
    if (!more) {
        more = new CsSlot;
        more->key.store(a);
        more->m = new GuestLock;
    }
    return more;
}
GuestLock* cs_of(uint32_t a) { return cs_slot(a)->m; }

// the main thread's own lookups first: a direct-mapped cache only it reads and writes (no atomic, no barrier; the
// table's acquire load is a sync on the console), right as table slots never move or go
const uint32_t kMainCache = 1024;
uint32_t g_mainCacheKey[kMainCache];
CsSlot* g_mainCacheSlot[kMainCache];
inline CsSlot* cs_find(uint32_t a, bool main) {
    if (!main) return cs_slot(a);
    uint32_t h = (a >> 2) * 2654435761u >> 22;           // 10 bits
    if (g_mainCacheKey[h] == a && a) return g_mainCacheSlot[h];
    CsSlot* s = cs_slot(a);
    g_mainCacheKey[h] = a;
    g_mainCacheSlot[h] = s;
    return s;
}
void k_InitializeCriticalSection(X86* c) { cs_of(ARG(c, 0)); RET_STD(c, 1, 0); }
void k_InitializeCriticalSectionAndSpinCount(X86* c) { cs_of(ARG(c, 0)); RET_STD(c, 2, 1); }
void k_DeleteCriticalSection(X86* c) { RET_STD(c, 1, 0); }
// ISAAC360_PROFILE: each critical section another thread held when it was entered, with the time waited, the code
// that waited and the code holding it (two return addresses each: the game enters through a wrapper, whose frame
// pointer gives its caller)
struct LockStats {
    uint32_t waits;
    double ms, maxMs;
    uint32_t waiter[2], holder[2];
};
std::mutex g_lockStatsLock;
std::unordered_map<uint32_t, LockStats> g_lockStats;
double g_lockWaitMs = 0;                            // all waits so far (the slow frame lines)
int g_lockProfile = -1;                             // read at the first Enter, once the console's arguments are set

// enters the section at a for c's thread; ISAAC360_PROFILE records a wait with where it came from (site: the return
// into the caller, and the caller's own from its ebp frame where that is on the stack)
// critical sections the main thread entered (the room lines, shims_gl.cpp): one thread writes it, no atomic
uint64_t g_mainEnters = 0;

void cs_enter(X86* c, uint32_t a, CsSlot* slot) {
    uint32_t me = guest_thread(c)->id;
    if (me == g_lockMainId) {
        ++g_mainEnters;
        // the main thread's own lock: nobody can be holding it, nothing to record
        if ((slot->m->word.load(std::memory_order_relaxed) & kLockBiased) && guest_lock_try(*slot->m, me)) return;
    }
    if (g_lockProfile < 0) g_lockProfile = getenv("ISAAC360_PROFILE") ? 1 : 0;
    if (!g_lockProfile) {
        guest_lock_enter(*slot->m, me);
        return;
    }
    uint32_t esp = c->r[4], ebp = c->r[5], site[2] = {RD32(esp), 0};
    if (ebp > esp && ebp - esp < 0x10000 && ebp + 8 <= g_mem_size) site[1] = RD32(ebp + 4);
    else recomp_guest_returns(c, site, 2);
    if (!guest_lock_try(*slot->m, me)) {
        uint32_t holder0 = slot->holder0.load(std::memory_order_relaxed), holder1 = slot->holder1.load(std::memory_order_relaxed);
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        guest_lock_wait(*slot->m, me);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::lock_guard<std::mutex> l(g_lockStatsLock);
        g_lockWaitMs += ms;
        LockStats& st = g_lockStats[a];
        ++st.waits;
        st.ms += ms;
        if (ms >= st.maxMs) {
            st.maxMs = ms;
            st.waiter[0] = site[0];
            st.waiter[1] = site[1];
            st.holder[0] = holder0;
            st.holder[1] = holder1;
        }
    }
    slot->holder0.store(site[0], std::memory_order_relaxed);
    slot->holder1.store(site[1], std::memory_order_relaxed);
}
void k_EnterCriticalSection(X86* c) {
    uint32_t a = ARG(c, 0);
    cs_enter(c, a, cs_find(a, guest_thread(c)->id == g_lockMainId));
    RET_STD(c, 1, 0);
}
void k_LeaveCriticalSection(X86* c) {
    uint32_t me = guest_thread(c)->id;
    guest_lock_leave(*cs_find(ARG(c, 0), me == g_lockMainId)->m, me);
    RET_STD(c, 1, 0);
}
void k_TryEnterCriticalSection(X86* c) {
    uint32_t me = guest_thread(c)->id;
    RET_STD(c, 1, guest_lock_try(*cs_find(ARG(c, 0), me == g_lockMainId)->m, me) ? 1 : 0);
}

struct Event {
    std::mutex m;
    std::condition_variable cv;
    bool manual, set;
    uint32_t code;                                         // a thread's: its exit code once set
};
std::mutex g_evLock;
std::unordered_map<uint32_t, Event*> g_events;
uint32_t g_nextHandle = 0x2000;
uint32_t new_handle() {
    std::lock_guard<std::mutex> l(g_evLock);
    return g_nextHandle += 4;
}
Event* event_of(uint32_t h) {
    std::lock_guard<std::mutex> l(g_evLock);
    std::unordered_map<uint32_t, Event*>::iterator it = g_events.find(h);
    return it == g_events.end() ? 0 : it->second;
}
void k_CreateEventW(X86* c) {
    uint32_t h = new_handle();
    Event* e = new Event;
    e->manual = ARG(c, 1) != 0;
    e->set = ARG(c, 2) != 0;
    std::lock_guard<std::mutex> l(g_evLock);
    g_events[h] = e;
    RET_STD(c, 4, h);
}
void k_SetEvent(X86* c) {
    if (Event* e = event_of(ARG(c, 0))) {
        std::lock_guard<std::mutex> l(e->m);
        e->set = true;
        e->cv.notify_all();
    }
    RET_STD(c, 1, 1);
}
void k_ResetEvent(X86* c) {
    if (Event* e = event_of(ARG(c, 0))) {
        std::lock_guard<std::mutex> l(e->m);
        e->set = false;
    }
    RET_STD(c, 1, 1);
}
uint32_t wait(uint32_t h, uint32_t ms) {
    Event* e = event_of(h);
    if (!e) return 0;                                      // others: signalled
    std::unique_lock<std::mutex> l(e->m);
    bool ok = ms == 0xffffffffu ? (e->cv.wait(l, [e] { return e->set; }), true)
                                 : e->cv.wait_for(l, std::chrono::milliseconds(ms), [e] { return e->set; });
    if (!ok) return 0x102;                                 // WAIT_TIMEOUT
    if (!e->manual) e->set = false;
    return 0;
}
// the main thread parks while it waits (guest_lock.h): the thread it waits for may want one of its locks
uint32_t wait_parked(X86* c, uint32_t h, uint32_t ms) {
    bool main = guest_thread(c)->id == g_lockMainId;
    if (main) guest_lock_main_park();
    uint32_t r = wait(h, ms);
    if (main) guest_lock_main_unpark();
    return r;
}
void k_WaitForSingleObject(X86* c) { RET_STD(c, 2, wait_parked(c, ARG(c, 0), ARG(c, 1))); }
void k_WaitForSingleObjectEx(X86* c) { RET_STD(c, 3, wait_parked(c, ARG(c, 0), ARG(c, 1))); }
void k_CloseHandle(X86* c) { RET_STD(c, 1, 1); }

// ------------------------------------------------------------------------------------------------ TLS
std::mutex g_tlsLock;
uint32_t g_tlsNext = 1;
thread_local std::unordered_map<uint32_t, uint32_t> t_tls;
void k_TlsAlloc(X86* c) {
    std::lock_guard<std::mutex> l(g_tlsLock);
    RET_STD(c, 0, g_tlsNext++);
}
void k_TlsFree(X86* c) { RET_STD(c, 1, 1); }
void k_TlsGetValue(X86* c) {
    uint32_t i = ARG(c, 0);
    RET_STD(c, 1, i < 128 ? guest_thread(c)->tls[i] : t_tls[i]);
}
void k_TlsSetValue(X86* c) {
    uint32_t i = ARG(c, 0);
    if (i < 128) guest_thread(c)->tls[i] = ARG(c, 1);
    else t_tls[i] = ARG(c, 1);
    RET_STD(c, 2, 1);
}

// ------------------------------------------------------------------------------------------------ time
std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
uint64_t now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_t0).count();
}
// the frame limiter polls it in a loop without locking (40,000 times a frame): the main thread serves handovers here
void k_QueryPerformanceCounter(X86* c) {
    if (guest_thread(c)->id == g_lockMainId) guest_lock_main_check();
    WR64(ARG(c, 0), now_us());
    RET_STD(c, 1, 1);
}
void k_QueryPerformanceFrequency(X86* c) { WR64(ARG(c, 0), 1000000); RET_STD(c, 1, 1); }
void k_GetSystemTimeAsFileTime(X86* c) {
    uint64_t t = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count() * 10 + 116444736000000000ull;
    WR64(ARG(c, 0), t);
    RET_STD(c, 1, 0);
}
void k_GetLocalTime(X86* c) {
    time_t t = time(0);
    struct tm* tm = localtime(&t);
    uint32_t a = ARG(c, 0);
    uint16_t v[8] = {(uint16_t)(tm->tm_year + 1900), (uint16_t)(tm->tm_mon + 1), (uint16_t)tm->tm_wday, (uint16_t)tm->tm_mday,
                     (uint16_t)tm->tm_hour, (uint16_t)tm->tm_min, (uint16_t)tm->tm_sec, 0};
    for (int i = 0; i < 8; ++i) WR16(a + 2 * i, v[i]);
    RET_STD(c, 1, 0);
}
void w_timeGetTime(X86* c) { RET_STD(c, 0, (uint32_t)(now_us() / 1000)); }
void w_timeBeginPeriod(X86* c) { RET_STD(c, 1, 0); }
void w_timeEndPeriod(X86* c) { RET_STD(c, 1, 0); }
void w_timeGetDevCaps(X86* c) { WR32(ARG(c, 0), 1); WR32(ARG(c, 0) + 4, 1000000); RET_STD(c, 2, 0); }
// the main thread's sleeps, kept apart from its work in the frame times (shims_gl.cpp): the game paces its own frames
static std::atomic<uint64_t> g_mainSleepUs(0);

void k_Sleep(X86* c) {
    uint32_t ms = ARG(c, 0);
    bool main = recomp_is_main_cpu(c);
    uint64_t start = main ? now_us() : 0;
    bool park = main && ms;                         // a sleep may outlast what another thread waits on it for
    if (park) guest_lock_main_park();
    else if (main) guest_lock_main_check();
    if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    else std::this_thread::yield();
    if (park) guest_lock_main_unpark();
    if (main) g_mainSleepUs += now_us() - start;
    RET_STD(c, 1, 0);
}

// ------------------------------------------------------------------------------------------------ process/thread
void k_GetCurrentThreadId(X86* c) { RET_STD(c, 0, RD32(c->fs_base + 0x24)); }
void k_GetCurrentProcessId(X86* c) { RET_STD(c, 0, 4); }
void k_GetCurrentProcess(X86* c) { RET_STD(c, 0, 0xffffffffu); }
void k_CreateThread(X86* c) {
    uint32_t tid = 0;
    uint32_t h = thread_create(ARG(c, 2), ARG(c, 3), &tid);
    if (ARG(c, 5)) WR32(ARG(c, 5), tid);
    RET_STD(c, 6, h);
}
void k_SetThreadPriority(X86* c) { RET_STD(c, 2, 1); }
void k_TerminateThread(X86* c) { RET_STD(c, 2, 1); }
void k_TerminateProcess(X86* c) {
    recomp_log("TerminateProcess(%u)", ARG(c, 1));
    recomp_log_drain();
    _exit((int)ARG(c, 1));
}
void k_SetThreadExecutionState(X86* c) { RET_STD(c, 1, 1); }
void k_IsDebuggerPresent(X86* c) { RET_STD(c, 0, 0); }
void k_IsProcessorFeaturePresent(X86* c) {
    uint32_t f = ARG(c, 0);
    RET_STD(c, 1, (f == 6 || f == 10 || f == 3) ? 1 : 0);    // XMMI, XMMI64 (SSE, SSE2), MMX
}
void k_SetUnhandledExceptionFilter(X86* c) { RET_STD(c, 1, 0); }
void k_UnhandledExceptionFilter(X86* c) { recomp_fatal(c, "UnhandledExceptionFilter"); }
void k_InitializeSListHead(X86* c) { WR64(ARG(c, 0), 0); RET_STD(c, 1, 0); }
void k_RaiseException(X86* c) {
    // 0x406D1388 names a thread for a debugger; without one the game's own __except swallows it
    if (ARG(c, 0) == 0x406D1388u && ARG(c, 3)) {
        recomp_log("thread name: %s", guest_str(RD32(ARG(c, 3) + 4)).c_str());
        RET_STD(c, 4, 0);
        return;
    }
    recomp_fatal(c, "RaiseException %08x", ARG(c, 0));
}
uint32_t g_lastError = 0;
void k_GetLastError(X86* c) { RET_STD(c, 0, g_lastError); }

// ------------------------------------------------------------------------------------------------ modules
std::map<uint32_t, std::string> g_modules;
uint32_t module_handle(const std::string& name) {
    std::string n = name;
    for (size_t i = 0; i < n.size(); ++i) n[i] = (char)tolower((unsigned char)n[i]);
    if (n.empty() || n == "isaac-ng.exe") return kGuestImageBase;
    for (std::map<uint32_t, std::string>::iterator it = g_modules.begin(); it != g_modules.end(); ++it)
        if (it->second == n) return it->first;
    uint32_t h = 0x7f000000 + 0x10000 * (uint32_t)g_modules.size();
    g_modules[h] = n;
    return h;
}
void k_GetModuleHandleA(X86* c) { RET_STD(c, 1, module_handle(ARG(c, 0) ? guest_str(ARG(c, 0)) : "")); }
void k_GetModuleHandleW(X86* c) {
    std::u16string w = guest_wstr(ARG(c, 0));
    RET_STD(c, 1, module_handle(ARG(c, 0) ? std::string(w.begin(), w.end()) : ""));
}
void k_LoadLibraryA(X86* c) {
    std::string n = guest_str(ARG(c, 0));
    uint32_t probe = shim_proc_address(n, "");
    recomp_log("LoadLibraryA %s -> %s", n.c_str(), probe ? "shimmed" : "missing");
    RET_STD(c, 1, probe ? module_handle(n) : 0);
}
void k_FreeLibrary(X86* c) { RET_STD(c, 1, 1); }
void k_GetProcAddress(X86* c) {
    uint32_t h = ARG(c, 0);
    std::string name = ARG(c, 1) < 0x10000 ? "#" + std::to_string(ARG(c, 1)) : guest_str(ARG(c, 1));
    std::string module = h == kGuestImageBase ? "isaac-ng.exe" : g_modules.count(h) ? g_modules[h] : "?";
    uint32_t a = shim_proc_address(module, name);
    if (!a) recomp_log("GetProcAddress %s!%s -> null", module.c_str(), name.c_str());
    RET_STD(c, 2, a);
}

// ------------------------------------------------------------------------------------------------ files and folders
std::string host_path(const std::string& p) { return recomp_host_path(p); }
void k_GetCurrentDirectoryA(X86* c) {
    std::string d = ".";                       // relative paths resolve against the game folder (host_path)
    guest_write_str(ARG(c, 1), d, ARG(c, 0));
    RET_STD(c, 2, (uint32_t)d.size());
}
void k_GetFullPathNameW(X86* c) {
    // relative paths stay relative (the shims resolve them); like Windows, a buffer too small for the path
    // returns the size it needs with the terminator, a filled one the length without it
    std::u16string w = guest_wstr(ARG(c, 0));
    uint32_t cap = ARG(c, 1), buf = ARG(c, 2);
    if (ARG(c, 3)) WR32(ARG(c, 3), 0);
    if (!buf || cap <= w.size()) {
        RET_STD(c, 4, (uint32_t)w.size() + 1);
        return;
    }
    for (size_t i = 0; i < w.size(); ++i) WR16(buf + 2 * (uint32_t)i, w[i]);
    WR16(buf + 2 * (uint32_t)w.size(), 0);
    RET_STD(c, 4, (uint32_t)w.size());
}
void k_GetFileAttributesA(X86* c) {
    std::string p = host_path(guest_str(ARG(c, 0)));
    if (getenv("ISAAC360_TRACE_FILES")) recomp_log("GetFileAttributesA %s", p.c_str());
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(p.c_str());
    RET_STD(c, 1, a);
#else
    bool dir = false;
    RET_STD(c, 1, recomp_host_exists(p, &dir) ? (dir ? 0x10u : 0x80u) : 0xffffffffu);   // DIRECTORY / NORMAL
#endif
}
// The game counts a failure as success only with ERROR_ALREADY_EXISTS (183) from GetLastError, the shim's own
// value: without it a save folder that already existed was "not writable" and options.ini was never written.
// The game asks again for the same folders as it loads (doors included): a folder made or found once is answered
// from memory, where each ask stat'ed it on the console's USB drive after the file writer's queue (boot 15's door
// profile: 4%). The game removes no folders.
std::mutex g_dirsLock;
std::set<std::string> g_knownDirs;
void k_CreateDirectoryA(X86* c) {
    std::string guest = guest_str(ARG(c, 0)), key = guest;
    for (size_t i = 0; i < key.size(); ++i) key[i] = key[i] == '\\' ? '/' : (char)tolower((unsigned char)key[i]);
    while (!key.empty() && key[key.size() - 1] == '/') key.erase(key.size() - 1);
    {
        std::lock_guard<std::mutex> l(g_dirsLock);
        if (g_knownDirs.count(key)) {
            g_lastError = 183;
            RET_STD(c, 2, 0);
            return;
        }
    }
    bool dir = false;
    bool existed = recomp_host_exists(recomp_host_path(guest), &dir) && dir;
    std::string p = recomp_host_path(guest + "/", true);     // makes the folder (and its parents) in the overlay
    bool made = existed || (recomp_host_exists(p, &dir) && dir);
    if (made) {
        std::lock_guard<std::mutex> l(g_dirsLock);
        g_knownDirs.insert(key);
    }
    if (existed) {
        g_lastError = 183;
        RET_STD(c, 2, 0);
        return;
    }
    g_lastError = made ? 0 : 3;                               // ERROR_PATH_NOT_FOUND
    RET_STD(c, 2, made ? 1 : 0);
}
void k_CreateFileA(X86* c) {
    recomp_log("CreateFileA %s", guest_str(ARG(c, 0)).c_str());
    RET_STD(c, 7, 0xffffffffu);
}
void k_LockFileEx(X86* c) { RET_STD(c, 6, 1); }
void k_UnlockFileEx(X86* c) { RET_STD(c, 5, 1); }
// directory listing: the matching entries are collected at FindFirstFileW and handed out one by one
struct FindEntry { std::string name; bool dir; uint64_t size; };
std::map<uint32_t, std::pair<std::vector<FindEntry>, size_t> > g_finds;
uint32_t g_nextFind = 0x3000;

bool wild_match(const char* p, const char* s) {
    if (!*p) return !*s;
    if (*p == '*') return wild_match(p + 1, s) || (*s && wild_match(p, s + 1));
    if (*s && (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s))) return wild_match(p + 1, s + 1);
    return false;
}

std::string lower_name(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

void fill_find_data(uint32_t fd, const FindEntry& e) {
    memset(g_mem + fd, 0, 592);
    WR32(fd, e.dir ? 0x10 : 0x80);                         // FILE_ATTRIBUTE_DIRECTORY / NORMAL
    WR32(fd + 28, (uint32_t)(e.size >> 32));
    WR32(fd + 32, (uint32_t)e.size);
    for (size_t i = 0; i < e.name.size() && i < 259; ++i) WR16(fd + 44 + 2 * (uint32_t)i, (uint8_t)e.name[i]);
}

void k_FindFirstFileW(X86* c) {
    std::u16string w = guest_wstr(ARG(c, 0));
    std::string pattern(w.begin(), w.end());
    if (getenv("ISAAC360_TRACE_FILES")) recomp_log("FindFirstFileW %s", pattern.c_str());
    for (size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] == '\\') pattern[i] = '/';
    size_t slash = pattern.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : pattern.substr(0, slash);
    std::string mask = slash == std::string::npos ? pattern : pattern.substr(slash + 1);
    std::vector<FindEntry> found;
    std::set<std::string> seen;                            // lower-case names: the first folder holding one wins
    std::vector<std::string> hostDirs = recomp_host_dirs(dir.empty() ? "." : dir);
    // a folder below a drive's root lists "." and ".." first on Windows, and code may skip its first two entries
    const char* dots[2] = {".", ".."};
    for (int k = 0; k < 2 && !hostDirs.empty(); ++k)
        if (wild_match(mask.c_str(), dots[k])) {
            FindEntry e;
            e.name = dots[k];
            e.dir = true;
            e.size = 0;
            found.push_back(e);
            seen.insert(dots[k]);
        }
    for (size_t k = 0; k < hostDirs.size(); ++k) {
        const std::string& hostDir = hostDirs[k];
#if defined(_WIN32)
        std::error_code ec;
        for (std::filesystem::directory_iterator it(std::filesystem::u8path(hostDir), ec), end; !ec && it != end;
             it.increment(ec)) {
            std::string name = it->path().filename().u8string();
            if (!wild_match(mask.c_str(), name.c_str()) || !seen.insert(lower_name(name)).second) continue;
            FindEntry e;
            e.name = name;
            e.dir = it->is_directory(ec);
            e.size = e.dir ? 0 : (uint64_t)it->file_size(ec);
            found.push_back(e);
        }
#else
        if (DIR* d = opendir(hostDir.c_str())) {
            while (dirent* de = readdir(d)) {
                std::string name = de->d_name;
                if (name == "." || name == ".." || !wild_match(mask.c_str(), name.c_str()) ||
                    !seen.insert(lower_name(name)).second)
                    continue;
                struct stat st;
                FindEntry e;
                e.name = name;
                e.dir = stat((hostDir + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
                e.size = e.dir ? 0 : (uint64_t)st.st_size;
                found.push_back(e);
            }
            closedir(d);
        }
#endif
    }
    if (found.empty()) {
        g_lastError = 2;
        RET_STD(c, 2, 0xffffffffu);
        return;
    }
    uint32_t h = g_nextFind++;
    fill_find_data(ARG(c, 1), found[0]);
    g_finds[h] = std::make_pair(found, (size_t)1);
    RET_STD(c, 2, h);
}
void k_FindNextFileW(X86* c) {
    std::map<uint32_t, std::pair<std::vector<FindEntry>, size_t> >::iterator it = g_finds.find(ARG(c, 0));
    if (it == g_finds.end() || it->second.second >= it->second.first.size()) {
        g_lastError = 18;                                   // ERROR_NO_MORE_FILES
        RET_STD(c, 2, 0);
        return;
    }
    fill_find_data(ARG(c, 1), it->second.first[it->second.second++]);
    RET_STD(c, 2, 1);
}
void k_FindClose(X86* c) { g_finds.erase(ARG(c, 0)); RET_STD(c, 1, 1); }
void k_GlobalAlloc(X86* c) { RET_STD(c, 2, guest_calloc(1, ARG(c, 1))); }
void k_GlobalLock(X86* c) { RET_STD(c, 1, ARG(c, 0)); }
void k_GlobalUnlock(X86* c) { RET_STD(c, 1, 1); }

void a_OpenProcessToken(X86* c) { RET_STD(c, 3, 0); }
void o_CoInitialize(X86* c) { RET_STD(c, 1, 0); }
void o_CoInitializeEx(X86* c) { RET_STD(c, 2, 0); }
void o_CoUninitialize(X86* c) { RET_STD(c, 0, 0); }
void sh_SHFileOperationA(X86* c) { RET_STD(c, 1, 1); }
void d_SymInitialize(X86* c) { RET_STD(c, 3, 0); }
void d_SymSetOptions(X86* c) { RET_STD(c, 1, 0); }
void d_SymFromAddr(X86* c) { RET_STD(c, 4, 0); }
void d_SymGetLineFromAddr(X86* c) { RET_STD(c, 4, 0); }

const ShimDef kKernel[] = {
    {"kernel32.dll!InitializeCriticalSection", k_InitializeCriticalSection},
    {"kernel32.dll!InitializeCriticalSectionAndSpinCount", k_InitializeCriticalSectionAndSpinCount},
    {"kernel32.dll!DeleteCriticalSection", k_DeleteCriticalSection},
    {"kernel32.dll!EnterCriticalSection", k_EnterCriticalSection},
    {"kernel32.dll!LeaveCriticalSection", k_LeaveCriticalSection},
    {"kernel32.dll!TryEnterCriticalSection", k_TryEnterCriticalSection},
    {"kernel32.dll!CreateEventW", k_CreateEventW},
    {"kernel32.dll!SetEvent", k_SetEvent},
    {"kernel32.dll!ResetEvent", k_ResetEvent},
    {"kernel32.dll!WaitForSingleObject", k_WaitForSingleObject},
    {"kernel32.dll!WaitForSingleObjectEx", k_WaitForSingleObjectEx},
    {"kernel32.dll!CloseHandle", k_CloseHandle},
    {"kernel32.dll!TlsAlloc", k_TlsAlloc},
    {"kernel32.dll!TlsFree", k_TlsFree},
    {"kernel32.dll!TlsGetValue", k_TlsGetValue},
    {"kernel32.dll!TlsSetValue", k_TlsSetValue},
    {"kernel32.dll!QueryPerformanceCounter", k_QueryPerformanceCounter},
    {"kernel32.dll!QueryPerformanceFrequency", k_QueryPerformanceFrequency},
    {"kernel32.dll!GetSystemTimeAsFileTime", k_GetSystemTimeAsFileTime},
    {"kernel32.dll!GetLocalTime", k_GetLocalTime},
    {"kernel32.dll!Sleep", k_Sleep},
    {"kernel32.dll!GetCurrentThreadId", k_GetCurrentThreadId},
    {"kernel32.dll!GetCurrentProcessId", k_GetCurrentProcessId},
    {"kernel32.dll!GetCurrentProcess", k_GetCurrentProcess},
    {"kernel32.dll!CreateThread", k_CreateThread},
    {"kernel32.dll!SetThreadPriority", k_SetThreadPriority},
    {"kernel32.dll!TerminateThread", k_TerminateThread},
    {"kernel32.dll!TerminateProcess", k_TerminateProcess},
    {"kernel32.dll!SetThreadExecutionState", k_SetThreadExecutionState},
    {"kernel32.dll!IsDebuggerPresent", k_IsDebuggerPresent},
    {"kernel32.dll!IsProcessorFeaturePresent", k_IsProcessorFeaturePresent},
    {"kernel32.dll!SetUnhandledExceptionFilter", k_SetUnhandledExceptionFilter},
    {"kernel32.dll!UnhandledExceptionFilter", k_UnhandledExceptionFilter},
    {"kernel32.dll!InitializeSListHead", k_InitializeSListHead},
    {"kernel32.dll!RaiseException", k_RaiseException},
    {"kernel32.dll!GetLastError", k_GetLastError},
    {"kernel32.dll!GetModuleHandleA", k_GetModuleHandleA},
    {"kernel32.dll!GetModuleHandleW", k_GetModuleHandleW},
    {"kernel32.dll!LoadLibraryA", k_LoadLibraryA},
    {"kernel32.dll!FreeLibrary", k_FreeLibrary},
    {"kernel32.dll!GetProcAddress", k_GetProcAddress},
    {"kernel32.dll!GetCurrentDirectoryA", k_GetCurrentDirectoryA},
    {"kernel32.dll!GetFullPathNameW", k_GetFullPathNameW},
    {"kernel32.dll!GetFileAttributesA", k_GetFileAttributesA},
    {"kernel32.dll!CreateDirectoryA", k_CreateDirectoryA},
    {"kernel32.dll!CreateFileA", k_CreateFileA},
    {"kernel32.dll!LockFileEx", k_LockFileEx},
    {"kernel32.dll!UnlockFileEx", k_UnlockFileEx},
    {"kernel32.dll!FindFirstFileW", k_FindFirstFileW},
    {"kernel32.dll!FindNextFileW", k_FindNextFileW},
    {"kernel32.dll!FindClose", k_FindClose},
    {"kernel32.dll!GlobalAlloc", k_GlobalAlloc},
    {"kernel32.dll!GlobalLock", k_GlobalLock},
    {"kernel32.dll!GlobalUnlock", k_GlobalUnlock},
    {"winmm.dll!timeGetTime", w_timeGetTime},
    {"winmm.dll!timeBeginPeriod", w_timeBeginPeriod},
    {"winmm.dll!timeEndPeriod", w_timeEndPeriod},
    {"winmm.dll!timeGetDevCaps", w_timeGetDevCaps},
    {"advapi32.dll!OpenProcessToken", a_OpenProcessToken},
    {"ole32.dll!CoInitialize", o_CoInitialize},
    {"ole32.dll!CoInitializeEx", o_CoInitializeEx},
    {"ole32.dll!CoUninitialize", o_CoUninitialize},
    {"shell32.dll!SHFileOperationA", sh_SHFileOperationA},
    {"dbghelp.dll!SymInitialize", d_SymInitialize},
    {"dbghelp.dll!SymSetOptions", d_SymSetOptions},
    {"dbghelp.dll!SymFromAddr", d_SymFromAddr},
    {"dbghelp.dll!SymGetLineFromAddr", d_SymGetLineFromAddr},
};
}

uint64_t shims_main_lock_enters() { return g_mainEnters; }

// A guest thread's handle is a manual-reset event set when its function returns, so WaitForSingleObject on it waits
// for the thread: it returned at once before, and the game's thread object, finding its thread still running after
// the wait, called terminate() as the game shut down (console boot 18).
void shims_thread_created(uint32_t handle) {
    Event* e = new Event;
    e->manual = true;
    e->set = false;
    e->code = 259;                                  // STILL_ACTIVE
    std::lock_guard<std::mutex> l(g_evLock);
    g_events[handle] = e;
}

void shims_thread_exited(uint32_t handle, uint32_t code) {
    Event* e = event_of(handle);
    if (!e) return;
    {
        std::lock_guard<std::mutex> l(e->m);
        e->code = code;
        e->set = true;
    }
    e->cv.notify_all();
}

uint32_t shims_thread_exit_code(uint32_t handle) {
    Event* e = event_of(handle);
    if (!e) return 0;
    std::lock_guard<std::mutex> l(e->m);
    return e->code;
}
double shims_main_sleep_ms() { return g_mainSleepUs.load() / 1000.0; }

double shims_lock_wait_ms() {
    std::lock_guard<std::mutex> l(g_lockStatsLock);
    return g_lockWaitMs;
}

void shims_lock_stats_dump(void (*line)(const char*)) {
    std::vector<std::pair<double, uint32_t> > top;
    std::unordered_map<uint32_t, LockStats> stats;
    {
        std::lock_guard<std::mutex> l(g_lockStatsLock);
        stats.swap(g_lockStats);
    }
    for (std::unordered_map<uint32_t, LockStats>::iterator it = stats.begin(); it != stats.end(); ++it)
        top.push_back(std::make_pair(it->second.ms, it->first));
    std::sort(top.rbegin(), top.rend());
    char buf[256];
    snprintf(buf, sizeof(buf), "locks: %u of the main thread's handed over to other threads so far (guest_lock.h)",
             shims_locks_handed_over());
    line(buf);
    for (size_t i = 0; i < top.size() && i < 8; ++i) {
        const LockStats& st = stats[top[i].second];
        snprintf(buf, sizeof(buf), "locks: %08x waited %u times, %.1f ms (longest %.1f ms: waiting from %08x %08x, held "
                 "from %08x %08x)", top[i].second, st.waits, st.ms, st.maxMs, st.waiter[0], st.waiter[1], st.holder[0],
                 st.holder[1]);
        line(buf);
    }
}

// ISAAC360_BENCH: a critical section entered and left on a host mutex, a GuestLock, through the game's imports (as
// the lock wrapper calls them, its frame pointer set), and the game's add-ref (0x407770: its lock object's lock and
// unlock, each a virtual call into a wrapper around the section), ~2,000 a frame on the menu. scratch: 128 bytes of
// guest stack the bench clears after.
void shims_bench_locks(X86* c, uint32_t scratch) {
    typedef std::chrono::steady_clock Clock;
    const uint32_t n = 200000;
    std::recursive_mutex m;
    Clock::time_point t0 = Clock::now();
    for (uint32_t i = 0; i < n; ++i) {
        m.lock();
        m.unlock();
    }
    double host = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    std::atomic<uint32_t> word(0);
    t0 = Clock::now();
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t expected = 0;
        word.compare_exchange_strong(expected, 1, std::memory_order_acquire);
        word.store(0, std::memory_order_release);
    }
    double atomic = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    GuestLock guest;                                // the main thread's, biased (guest_lock.h)
    const uint32_t me = guest_thread(c)->id;
    t0 = Clock::now();
    for (uint32_t i = 0; i < n; ++i) {
        guest_lock_enter(guest, me);
        guest_lock_leave(guest, me);
    }
    double biased = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    guest.word.store(0);                            // as one another thread has asked for
    t0 = Clock::now();
    for (uint32_t i = 0; i < n; ++i) {
        guest_lock_enter(guest, me);
        guest_lock_leave(guest, me);
    }
    double light = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    // the section, then the reference-counted object: its count at +4, its lock object at +8 (the vtable at
    // 0xb3d508 whose +0Ch locks and +10h unlocks, a ready flag, the section)
    const uint32_t cs = scratch, ref = scratch + 0x40, lockObj = ref + 8;
    WR16(ref + 4, 1);
    WR32(lockObj, 0x00b3d508u);
    WR8(lockObj + 4, 1);
    WR32(lockObj + 8, cs);
    const uint32_t enter = shim_proc_address("kernel32.dll", "EnterCriticalSection");
    const uint32_t leave = shim_proc_address("kernel32.dll", "LeaveCriticalSection");
    const uint32_t esp = c->r[4], ebp = c->r[5];
    c->r[5] = esp + 0x10;
    double shims[2];
    int profile = g_lockProfile < 0 ? (getenv("ISAAC360_PROFILE") ? 1 : 0) : g_lockProfile;
    for (int p = 0; p < 2; ++p) {
        g_lockProfile = p;
        t0 = Clock::now();
        for (uint32_t i = 0; i < n; ++i) {
            c->r[4] -= 8;
            WR32(c->r[4] + 4, cs);
            WR32(c->r[4], 0x401000u);
            x86_call(c, enter);
            c->r[4] -= 8;
            WR32(c->r[4] + 4, cs);
            WR32(c->r[4], 0x401000u);
            x86_call(c, leave);
        }
        shims[p] = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    }
    g_lockProfile = profile;
    t0 = Clock::now();
    for (uint32_t i = 0; i < n; ++i) {
        c->r[1] = ref;
        guest_call(c, 0x407770, 0, 0, true);
    }
    double addRef = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n;
    c->r[4] = esp;
    c->r[5] = ebp;
    recomp_log("bench: a critical section entered and left %.1f ns on a host mutex (an atomic swap and store %.1f ns, "
               "a GuestLock %.1f ns shared, %.1f biased to the main thread), %.1f ns through the game's imports (%.1f "
               "profiled); the game's add-ref %.1f ns", host, atomic, light, biased, shims[0], shims[1], addRef);
}

// The engine's lock object (vtable 0xb3d508: a ready flag at +4, its critical section at +8, whose byte at +0x18
// says it is held): its lock (0x94f870, thiscall, a timeout argument, ret 4) and unlock (0x94f930) as the game's
// code does them, for the reference counting that calls them ~2,300 times a frame (translate.py HOST): each was a
// virtual call into translated code calling the section's import. Anything else (not ready: the game logs it; a
// timeout; the section already held by this thread, where the game's code waits forever) runs the game's code.
extern "C" int hle_lockobj_lock(X86* c) {
    uint32_t self = c->r[1], esp = c->r[4];
    if (!RD8(self + 4) || RD32(esp + 4) != 0xffffffffu) return 0;
    uint32_t cs = RD32(self + 8);
    CsSlot* slot = cs_find(cs, guest_thread(c)->id == g_lockMainId);
    cs_enter(c, cs, slot);
    if (RD8(cs + 0x18)) {
        guest_lock_leave(*slot->m, guest_thread(c)->id);
        return 0;
    }
    WR8(cs + 0x18, 1);
    c->r[0] = 1;
    c->r[4] = esp + 8;
    return 1;
}

extern "C" int hle_lockobj_unlock(X86* c) {
    uint32_t self = c->r[1];
    if (!RD8(self + 4)) return 0;
    uint32_t cs = RD32(self + 8);
    WR8(cs + 0x18, 0);
    uint32_t me = guest_thread(c)->id;
    guest_lock_leave(*cs_find(cs, me == g_lockMainId)->m, me);
    c->r[0] = 0;
    c->r[4] += 4;
    return 1;
}

// The engine's reference counting itself (translate.py HOST): add-ref (0x407770) and release (0x407710), thiscall
// on an object whose 16-bit count is at +4 and whose lock object (vtable 0xb3d508) is at +8, each a lock, a count
// change and an unlock: 1,000 to 6,700 a frame (more in large rooms), each a translated body making two virtual
// calls. Done here on the same section as the game's lock object, with its held flag set while the count changes.
// A release that would reach 0 (the object's teardown follows) and anything the lock object's fast path would not
// take run the game's code.
static CsSlot* refcount_lock(X86* c, uint32_t lockObj, uint32_t* cs) {
    if (RD32(lockObj) != 0x00b3d508u || !RD8(lockObj + 4)) return 0;
    *cs = RD32(lockObj + 8);
    CsSlot* slot = cs_find(*cs, guest_thread(c)->id == g_lockMainId);
    cs_enter(c, *cs, slot);
    if (RD8(*cs + 0x18)) {
        guest_lock_leave(*slot->m, guest_thread(c)->id);
        return 0;
    }
    WR8(*cs + 0x18, 1);
    return slot;
}

static void refcount_unlock(X86* c, uint32_t cs, CsSlot* slot) {
    WR8(cs + 0x18, 0);
    guest_lock_leave(*slot->m, guest_thread(c)->id);
}

extern "C" int hle_refcount_add(X86* c) {
    uint32_t self = c->r[1], cs;
    CsSlot* slot = refcount_lock(c, self + 8, &cs);
    if (!slot) return 0;
    WR16(self + 4, (uint16_t)(RD16(self + 4) + 1));
    refcount_unlock(c, cs, slot);
    c->r[0] = 0;                                    // the unlock's result, as the tail call leaves it
    c->r[1] = self + 8;
    c->r[4] += 4;
    return 1;
}

extern "C" int hle_refcount_release(X86* c) {
    uint32_t self = c->r[1], cs;
    CsSlot* slot = refcount_lock(c, self + 8, &cs);
    if (!slot) return 0;
    uint16_t n = RD16(self + 4);
    if (n == 1) {
        refcount_unlock(c, cs, slot);
        return 0;
    }
    if (n) WR16(self + 4, (uint16_t)(n - 1));
    refcount_unlock(c, cs, slot);
    c->r[0] = 1;                                    // mov al, 1 over the unlock's 0
    c->r[1] = self + 8;
    c->r[4] += 4;
    return 1;
}

// 0x407790 (thiscall on the counted object): under the lock, a count of 0 answers 0; otherwise, after the unlock, a
// virtual call on the object (its vtable's +8) and 1. 0x959c30 (cdecl, a {pointer, counted object} pair): the
// count read under the lock; a count of 1 (the pair's release follows) runs the game's code. Together with add-ref
// and release they were a quarter of the console's frame in a room of many objects (boot 18, room 1.735).
extern "C" int hle_refcount_use(X86* c) {
    uint32_t self = c->r[1], cs;
    CsSlot* slot = refcount_lock(c, self + 8, &cs);
    if (!slot) return 0;
    uint16_t n = RD16(self + 4);
    refcount_unlock(c, cs, slot);
    if (!n) {
        c->r[0] = 0;
        c->r[1] = self + 8;
    } else {
        c->r[1] = self;
        uint32_t ret = guest_call(c, RD32(RD32(self) + 8), 0, 0, true);
        c->r[0] = (ret & ~0xffu) | 1;
    }
    c->r[4] += 4;
    return 1;
}

extern "C" int hle_refcount_peek(X86* c) {
    uint32_t pair = RD32(c->r[4] + 4), ctrl = RD32(pair + 4);
    if (!ctrl) {
        c->r[4] += 4;
        return 1;
    }
    uint32_t cs;
    CsSlot* slot = refcount_lock(c, ctrl + 8, &cs);
    if (!slot) return 0;
    uint16_t n = RD16(ctrl + 4);
    refcount_unlock(c, cs, slot);
    if (n == 1) return 0;
    c->r[0] = 0;
    c->r[1] = ctrl + 8;
    c->r[4] += 4;
    return 1;
}

void shims_kernel32_register() { shim_register(kKernel, (int)(sizeof(kKernel) / sizeof(kKernel[0]))); }
