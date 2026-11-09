// KERNEL32/WINMM/ADVAPI32/ole32/SHELL32/dbghelp imports (stdcall). Handles and synchronisation objects are host
// objects keyed by guest values.
#include "runtime.h"
#include <algorithm>
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

namespace {
// ------------------------------------------------------------------------------------------------ sync
// A critical section is a host mutex found by the guest structure's address. The game enters and leaves them
// thousands of times a frame (its sound, resource and log locks), so the lookup takes no lock: an open-addressed
// table whose slots are written once (the key last, after the mutex) and never removed, as Windows' are never
// moved. A full table (it holds far more than the game makes) falls back to a map under a lock.
struct CsSlot {
    std::atomic<uint32_t> key;
    std::recursive_mutex* m;
    std::atomic<uint32_t> holder0, holder1;      // BOI360_PROFILE: the code that last entered it
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
            g_csSlots[i].m = new std::recursive_mutex;
            g_csSlots[i].key.store(a, std::memory_order_release);
            ++g_csUsed;
            return &g_csSlots[i];
        }
    }
    CsSlot*& more = g_csMore[a];
    if (!more) {
        more = new CsSlot;
        more->key.store(a);
        more->m = new std::recursive_mutex;
    }
    return more;
}
std::recursive_mutex* cs_of(uint32_t a) { return cs_slot(a)->m; }
void k_InitializeCriticalSection(X86* c) { cs_of(ARG(c, 0)); RET_STD(c, 1, 0); }
void k_InitializeCriticalSectionAndSpinCount(X86* c) { cs_of(ARG(c, 0)); RET_STD(c, 2, 1); }
void k_DeleteCriticalSection(X86* c) { RET_STD(c, 1, 0); }
// BOI360_PROFILE: each critical section another thread held when it was entered, with the time waited, the code
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

void k_EnterCriticalSection(X86* c) {
    uint32_t a = ARG(c, 0);
    CsSlot* slot = cs_slot(a);
    if (g_lockProfile < 0) g_lockProfile = getenv("BOI360_PROFILE") ? 1 : 0;
    if (!g_lockProfile) {
        slot->m->lock();
        RET_STD(c, 1, 0);
        return;
    }
    // the return into the game's wrapper, and the wrapper's own (its ebp frame) where that is on the stack
    uint32_t esp = c->r[4], ebp = c->r[5], site[2] = {RD32(esp), 0};
    if (ebp > esp && ebp - esp < 0x10000 && ebp + 8 <= g_mem_size) site[1] = RD32(ebp + 4);
    else recomp_guest_returns(c, site, 2);
    if (!slot->m->try_lock()) {
        uint32_t holder0 = slot->holder0.load(std::memory_order_relaxed), holder1 = slot->holder1.load(std::memory_order_relaxed);
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        slot->m->lock();
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
    RET_STD(c, 1, 0);
}
void k_LeaveCriticalSection(X86* c) { cs_of(ARG(c, 0))->unlock(); RET_STD(c, 1, 0); }
void k_TryEnterCriticalSection(X86* c) { RET_STD(c, 1, cs_of(ARG(c, 0))->try_lock() ? 1 : 0); }

struct Event {
    std::mutex m;
    std::condition_variable cv;
    bool manual, set;
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
    if (!e) return 0;                                      // threads and others: signalled
    std::unique_lock<std::mutex> l(e->m);
    bool ok = ms == 0xffffffffu ? (e->cv.wait(l, [e] { return e->set; }), true)
                                 : e->cv.wait_for(l, std::chrono::milliseconds(ms), [e] { return e->set; });
    if (!ok) return 0x102;                                 // WAIT_TIMEOUT
    if (!e->manual) e->set = false;
    return 0;
}
void k_WaitForSingleObject(X86* c) { RET_STD(c, 2, wait(ARG(c, 0), ARG(c, 1))); }
void k_WaitForSingleObjectEx(X86* c) { RET_STD(c, 3, wait(ARG(c, 0), ARG(c, 1))); }
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
void k_QueryPerformanceCounter(X86* c) { WR64(ARG(c, 0), now_us()); RET_STD(c, 1, 1); }
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
void k_Sleep(X86* c) {
    uint32_t ms = ARG(c, 0);
    if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    else std::this_thread::yield();
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
    if (getenv("BOI360_TRACE_FILES")) recomp_log("GetFileAttributesA %s", p.c_str());
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
void k_CreateDirectoryA(X86* c) {
    std::string guest = guest_str(ARG(c, 0));
    bool dir = false;
    bool existed = recomp_host_exists(recomp_host_path(guest), &dir) && dir;
    std::string p = recomp_host_path(guest + "/", true);     // makes the folder (and its parents) in the overlay
    if (existed) {
        g_lastError = 183;
        RET_STD(c, 2, 0);
        return;
    }
    bool made = recomp_host_exists(p, &dir) && dir;
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
    if (getenv("BOI360_TRACE_FILES")) recomp_log("FindFirstFileW %s", pattern.c_str());
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
    for (size_t i = 0; i < top.size() && i < 8; ++i) {
        const LockStats& st = stats[top[i].second];
        snprintf(buf, sizeof(buf), "locks: %08x waited %u times, %.1f ms (longest %.1f ms: waiting from %08x %08x, held "
                 "from %08x %08x)", top[i].second, st.waits, st.ms, st.maxMs, st.waiter[0], st.waiter[1], st.holder[0],
                 st.holder[1]);
        line(buf);
    }
}

// BOI360_BENCH: a critical section entered and left on a host mutex, through the game's imports (as the lock
// wrapper calls them, its frame pointer set), and the game's add-ref (0x407770: its lock object's lock and unlock,
// each a virtual call into a wrapper around the section), ~2,000 a frame on the menu. scratch: 128 bytes of guest
// stack the bench clears after.
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
    int profile = g_lockProfile < 0 ? (getenv("BOI360_PROFILE") ? 1 : 0) : g_lockProfile;
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
    recomp_log("bench: a critical section entered and left %.1f ns on a host mutex (an atomic swap and store %.1f ns), "
               "%.1f ns through the game's imports (%.1f profiled); the game's add-ref %.1f ns", host, atomic, shims[0],
               shims[1], addRef);
}

void shims_kernel32_register() { shim_register(kKernel, (int)(sizeof(kKernel) / sizeof(kKernel[0]))); }
