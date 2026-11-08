// Host side of the recompiled game (docs/RECOMP_PLAN.md): guest memory and images, heap, threads, dispatch,
// imports. Shims (src/recomp/shims_*.cpp) use these helpers to read their arguments from the guest stack.
#ifndef BOI360_RECOMP_RUNTIME_H
#define BOI360_RECOMP_RUNTIME_H
#include "x86.h"
#include <string>
#include <vector>

// guest address space
enum : uint32_t {
    kGuestImageBase = 0x00400000,
    kGuestHeapLow = 0x01000000,        // after isaac-ng.exe's image
    kThunkBase = 0xF0000000,           // import and host-callback thunks; never memory
    kThunkLimit = 0xF0100000,
};

typedef void (*ShimFn)(X86* c);
struct ShimDef {
    const char* name;                  // "kernel32.dll!GetTickCount" (dll lower case)
    ShimFn fn;
};
void shim_register(const ShimDef* defs, int count);
ShimFn shim_lookup(const std::string& fullname);
bool shim_module_known(const std::string& module);   // any import shim for "dll" (lower case, with .dll)   // a registered import shim by "dll!name"

// guest stack arguments of a stdcall/cdecl shim (the return address is at esp)
static inline uint32_t ARG(X86* c, int n) { return RD32(c->r[4] + 4 + 4 * n); }
static inline float ARGF(X86* c, int n) { return U2F(ARG(c, n)); }
static inline double ARGD(X86* c, int n) { uint64_t lo = ARG(c, n), hi = ARG(c, n + 1); return Q2D(lo | (hi << 32)); }
static inline void RET_STD(X86* c, int nargs, uint32_t v) { c->r[0] = v; c->r[4] += 4 + 4 * nargs; }
static inline void RET_CDECL(X86* c, uint32_t v) { c->r[0] = v; c->r[4] += 4; }
static inline void RET_CDECL64(X86* c, uint64_t v) { c->r[0] = (uint32_t)v; c->r[2] = (uint32_t)(v >> 32); c->r[4] += 4; }
static inline void RET_ST0(X86* c, double v) { X87_PUSH(c, v); c->r[4] += 4; }   // cdecl float/double result

// guest strings and buffers
std::string guest_str(uint32_t a, size_t max = 1 << 20);
std::u16string guest_wstr(uint32_t a);
void guest_write_str(uint32_t a, const std::string& s, uint32_t cap);
static inline void* G(uint32_t a) { return g_mem + a; }

// guest heap (16-byte aligned blocks)
uint32_t guest_malloc(uint32_t size);
void guest_free(uint32_t p);
uint32_t guest_realloc(uint32_t p, uint32_t size);
uint32_t guest_calloc(uint32_t n, uint32_t size);
uint32_t guest_block_size(uint32_t p);
uint32_t guest_heap_used();

// call a guest function from host code (window procedures, thread entries, callbacks): pushes the arguments
// right to left and a host return marker; stdcall callees pop their own arguments, cdecl ones are popped here.
uint32_t guest_call(X86* c, uint32_t fn, int nargs, const uint32_t* args, bool stdcall);

// a host function callable from guest code at a thunk address (callbacks the shims hand out)
uint32_t thunk_for(ShimFn fn, const char* name);
const char* current_thunk_name();       // the import or thunk being run (for shared trap shims)

// threads: the context of the calling host thread
X86* current_cpu();
uint32_t thread_create(uint32_t entry, uint32_t arg, uint32_t* tid_out);   // returns a handle
X86* cpu_new_thread();

void recomp_log(const char* fmt, ...);
[[noreturn]] void recomp_fatal(X86* c, const char* fmt, ...);
void guest_backtrace(X86* c);

// set by the platform before recomp_run
struct RecompConfig {
    std::string gameDir;               // folder holding isaac-ng.exe and resources/
    std::string saveDir;               // writable folder for the game's saves
    std::string dllDir;                // the rebased runtime DLLs the game needs (tools/recomp/rebase.py)
    std::string dataOverlay;           // converted game files read in place of the game folder's (may be empty)
    uint32_t memSize;                  // guest memory in bytes
    const char* logPath;
    // optional: guest memory reserved whole and committed as the heap grows (the console pays only for what the
    // game uses); without them it is allocated whole
    uint8_t* (*reserveMemory)(uint32_t size);
    bool (*commitMemory)(uint8_t* base, uint32_t offset, uint32_t size);
    // optional: told when a host thread starts running guest code ("main", "guest", "audio")
    void (*threadStarted)(const char* role);
    RecompConfig() : memSize(0), logPath(0), reserveMemory(0), commitMemory(0), threadStarted(0) {}
};
// a host thread with a stack of its own size (guest threads, the audio mixer)
void host_thread_start(void (*fn)(void*), void* arg, uint32_t stackBytes);
int recomp_run(const RecompConfig& cfg);
void shims_after_load();                 // shims_all.cpp: guest-side set-up once memory exists
const RecompConfig& recomp_config();
// A path the game uses -> the host path: relative paths are the game folder's, the Windows profile's
// "documents/my games/..." folder is the save folder.
std::string recomp_host_path(const std::string& guestPath);
std::string recomp_host_path(const std::string& guestPath, bool write);   // write: in the overlay or save folder
bool recomp_host_exists(const std::string& hostPath, bool* isDir);
std::vector<std::string> recomp_host_dirs(const std::string& guestDir);   // host folders behind one guest folder
void recomp_sample_threads(void (*line)(const char*));   // each guest thread's shim and stack (watchdog)
void recomp_profile_start();             // BOI360_PROFILE: sample the main guest thread (runtime.cpp)
void recomp_profile_dump(void (*line)(const char*));   // the samples since the last dump, most frequent first
int recomp_guest_returns(X86* c, uint32_t* out, int n);    // the innermost guest return addresses on c's stack
void shims_lock_stats_dump(void (*line)(const char*));    // BOI360_PROFILE: contended critical sections (kernel32)
// where diagnostics go (screenshots, texture lists): BOI360_OUT_DIR, the console sets it beside the XEX
std::string recomp_out_dir();

#endif
