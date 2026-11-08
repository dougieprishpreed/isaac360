// Host runtime of the recompiled game (runtime.h, docs/RECOMP_PLAN.md).
#include "runtime.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

uint8_t* g_mem = 0;
extern "C" const X86FuncEntry x86_functions_lua[];
extern "C" const uint32_t x86_function_count_lua;
extern "C" const X86FuncEntry x86_functions_msvcp[];
extern "C" const uint32_t x86_function_count_msvcp;
uint32_t g_mem_size = 0;

static RecompConfig g_cfg;
const RecompConfig& recomp_config() { return g_cfg; }

// The game folder stays read only: writes to it go to an overlay folder (saveDir/overlay), and reads look there
// first, so files the game writes beside itself (savedatapath.txt, mods/, data/) never touch the user's copy.
// The game opens its archives thousands of times while loading and each probe of the console's USB drive costs
// milliseconds, so lookups are remembered until the next write (the only way overlay entries appear).
static std::mutex g_pathLock;
static std::unordered_map<std::string, std::string> g_pathCache;
static std::unordered_map<std::string, bool> g_madeDirs;

static void make_dirs(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/') {
            std::string dir = path.substr(0, i);
            if (g_madeDirs.count(dir)) continue;
#if defined(_WIN32)
            CreateDirectoryA(dir.c_str(), 0);
#else
            mkdir(dir.c_str(), 0777);
#endif
            g_madeDirs[dir] = true;
        }
}

// a file or a folder (the game checks its save folders with GetFileAttributes and _access)
bool recomp_host_exists(const std::string& path, bool* isDir) {
    std::string p = path;
    while (p.size() > 1 && p[p.size() - 1] == '/') p.erase(p.size() - 1);
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    if (isDir) *isDir = (st.st_mode & S_IFMT) == S_IFDIR;
    return true;
}

std::string recomp_host_path(const std::string& p, bool write) {
    std::string s = p;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\\') s[i] = '/';
    std::lock_guard<std::mutex> l(g_pathLock);
    if (write) g_pathCache.clear();
    else {
        std::unordered_map<std::string, std::string>::iterator it = g_pathCache.find(s);
        if (it != g_pathCache.end()) return it->second;
    }
    std::string key = s, out;
    size_t mg = s.find("documents/my games/");
    if (mg == std::string::npos) mg = s.find("Documents/My Games/");
    bool absolute = (s.size() > 1 && s[1] == ':') || (!s.empty() && s[0] == '/');
    if (mg != std::string::npos) {
        out = g_cfg.saveDir + "/" + s.substr(mg + 10);
        if (write) make_dirs(out);
    } else if (absolute) {
        out = s;
    } else {
        while (s.compare(0, 2, "./") == 0) s.erase(0, 2);
        std::string overlay = g_cfg.saveDir + "/overlay/" + s;
        if (write) {
            make_dirs(overlay);
            return overlay;
        }
        if (recomp_host_exists(overlay, 0)) out = overlay;
        // converted data (the console's archives, tools/recomp/console_sfx.py) in place of the game's own
        else if (!g_cfg.dataOverlay.empty() && recomp_host_exists(g_cfg.dataOverlay + "/" + s, 0))
            out = g_cfg.dataOverlay + "/" + s;
        else out = g_cfg.gameDir + "/" + s;
    }
    if (!write) g_pathCache[key] = out;
    return out;
}

std::string recomp_host_path(const std::string& p) { return recomp_host_path(p, false); }

std::string recomp_out_dir() {
    const char* d = getenv("BOI360_OUT_DIR");
    return d && *d ? d : "build/recomp";
}

// a folder as the game sees it is the overlay's, the converted data's and the game folder's together (listings
// merge them, the first one holding a name wins)
std::vector<std::string> recomp_host_dirs(const std::string& p) {
    std::string s = p;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\\') s[i] = '/';
    std::vector<std::string> all, out;
    bool absolute = (s.size() > 1 && s[1] == ':') || (!s.empty() && s[0] == '/');
    if (s.find("documents/my games/") != std::string::npos || s.find("Documents/My Games/") != std::string::npos ||
        absolute) {
        all.push_back(recomp_host_path(s));
    } else {
        while (s.compare(0, 2, "./") == 0) s.erase(0, 2);
        all.push_back(g_cfg.saveDir + "/overlay/" + s);
        if (!g_cfg.dataOverlay.empty()) all.push_back(g_cfg.dataOverlay + "/" + s);
        all.push_back(g_cfg.gameDir + "/" + s);
    }
    for (size_t i = 0; i < all.size(); ++i) {
        bool dir = false;
        if (recomp_host_exists(all[i], &dir) && dir) out.push_back(all[i]);
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ log
static FILE* g_log = 0;
static std::mutex g_logLock;

void recomp_log(const char* fmt, ...) {
    char line[2048];
    va_list a;
    va_start(a, fmt);
    vsnprintf(line, sizeof(line) - 2, fmt, a);
    va_end(a);
    size_t n = strlen(line);
    if (!n || line[n - 1] != '\n') strcat(line, "\n");
    std::lock_guard<std::mutex> g(g_logLock);
    fputs(line, stdout);
    fflush(stdout);
    if (g_log) {
        fputs(line, g_log);
        fflush(g_log);
    }
}

// guest call stack: return addresses on the guest stack that follow a call into translated code
void guest_backtrace(X86* c) {
    uint32_t sp = c->r[4];
    int shown = 0;
    for (uint32_t a = sp; a < sp + 0x4000 && a + 4 <= g_mem_size && shown < 24; a += 4) {
        uint32_t v = RD32(a);
        if (v >= kGuestImageBase + 0x1000 && v < kGuestImageBase + 0x5e8000) {
            recomp_log("  [esp+%04x] %08x", a - sp, v);
            ++shown;
        }
    }
}

void recomp_fatal(X86* c, const char* fmt, ...) {
    char line[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(line, sizeof(line), fmt, a);
    va_end(a);
    recomp_log("FATAL %s", line);
    if (c) {
        recomp_log("  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x", c->r[0], c->r[1], c->r[2],
                   c->r[3], c->r[4], c->r[5], c->r[6], c->r[7]);
        guest_backtrace(c);
    }
    if (g_log) fflush(g_log);
    fflush(stdout);
    _exit(3);
}

void x86_unimpl(X86* c, uint32_t eip, const char* what) { recomp_fatal(c, "unimplemented %s at %08x", what, eip); }
void x86_bad_jump(X86* c, uint32_t eip, uint32_t target) { recomp_fatal(c, "switch at %08x to unknown %08x", eip, target); }
void x86_unreachable(X86* c, uint32_t eip) { recomp_fatal(c, "reached unreachable code at %08x", eip); }

// ------------------------------------------------------------------------------------------------ strings
std::string guest_str(uint32_t a, size_t max) {
    std::string s;
    if (!a) return s;
    while (a < g_mem_size && s.size() < max) {
        char ch = (char)g_mem[a++];
        if (!ch) break;
        s += ch;
    }
    return s;
}

std::u16string guest_wstr(uint32_t a) {
    std::u16string s;
    if (!a) return s;
    for (;;) {
        uint16_t ch = RD16(a);
        a += 2;
        if (!ch) break;
        s += (char16_t)ch;
    }
    return s;
}

void guest_write_str(uint32_t a, const std::string& s, uint32_t cap) {
    if (!a || !cap) return;
    uint32_t n = (uint32_t)std::min<size_t>(s.size(), cap - 1);
    memcpy(g_mem + a, s.data(), n);
    g_mem[a + n] = 0;
}

// ------------------------------------------------------------------------------------------------ heap
// Blocks carry an 16-byte header in guest memory: size (bytes after the header) and a magic. Small sizes come
// from per-class free lists, large ones from an address-ordered free map with coalescing. The bookkeeping is
// host side, so a guest overflow cannot corrupt it (the magic catches a bad free).
namespace {
std::mutex g_heapLock;
uint32_t g_heapTop = 0, g_heapEnd = 0;
std::vector<std::pair<uint32_t, uint32_t> > g_reserved;      // guest ranges the heap must skip
std::vector<uint32_t> g_bins[129];                           // classes of 16 bytes up to 2048
uint32_t g_committed = 0;                                    // guest memory committed so far (reserved runs)
std::map<uint32_t, uint32_t> g_freeBig;                      // address -> size (header included)
uint32_t g_used = 0;
const uint32_t kMagic = 0x48454150;                          // 'HEAP'

uint32_t carve(uint32_t total) {
    for (;;) {
        uint32_t at = g_heapTop;
        bool moved = false;
        for (size_t i = 0; i < g_reserved.size(); ++i)
            if (at < g_reserved[i].second && at + total > g_reserved[i].first) {
                g_heapTop = g_reserved[i].second;
                moved = true;
            }
        if (moved) continue;
        if (at + total > g_heapEnd || at + total < at) return 0;
        if (g_cfg.commitMemory && at + total > g_committed) {
            uint32_t to = (at + total + 0xFFFFF) & ~0xFFFFFu;
            if (to > g_heapEnd) to = g_heapEnd;
            if (!g_cfg.commitMemory(g_mem, g_committed, to - g_committed)) {
                recomp_log("guest memory: committing %u MB failed", to >> 20);
                return 0;
            }
            g_committed = to;
        }
        g_heapTop = at + total;
        return at;
    }
}
}

uint32_t guest_malloc(uint32_t size) {
    if (size == 0) size = 1;
    uint32_t need = (size + 15) & ~15u;
    std::lock_guard<std::mutex> g(g_heapLock);
    uint32_t block = 0, total = need + 16;
    if (need <= 2048) {
        std::vector<uint32_t>& bin = g_bins[need >> 4];
        if (!bin.empty()) {
            block = bin.back();
            bin.pop_back();
        } else {
            block = carve(total);
        }
    } else {
        for (std::map<uint32_t, uint32_t>::iterator it = g_freeBig.begin(); it != g_freeBig.end(); ++it)
            if (it->second >= total) {
                block = it->first;
                uint32_t rest = it->second - total;
                g_freeBig.erase(it);
                if (rest >= 4096) g_freeBig[block + total] = rest;
                else total += rest;
                break;
            }
        if (!block) block = carve(total);
    }
    if (!block) {
        recomp_log("guest heap exhausted: %u bytes requested, %u in use", size, g_used);
        return 0;
    }
    WR32(block, total - 16);
    WR32(block + 4, kMagic);
    g_used += total;
    return block + 16;
}

uint32_t guest_block_size(uint32_t p) { return p ? RD32(p - 16) : 0; }
uint32_t guest_heap_used() { return g_used; }

void guest_free(uint32_t p) {
    if (!p) return;
    std::lock_guard<std::mutex> g(g_heapLock);
    uint32_t block = p - 16;
    if (RD32(block + 4) != kMagic) {
        recomp_log("guest free of a bad block %08x", p);
        return;
    }
    uint32_t need = RD32(block);
    WR32(block + 4, 0);
    g_used -= need + 16;
    if (need <= 2048) {
        g_bins[need >> 4].push_back(block);
        return;
    }
    uint32_t total = need + 16;
    std::map<uint32_t, uint32_t>::iterator next = g_freeBig.lower_bound(block);
    if (next != g_freeBig.end() && next->first == block + total) {
        total += next->second;
        g_freeBig.erase(next);
    }
    std::map<uint32_t, uint32_t>::iterator prev = g_freeBig.lower_bound(block);
    if (prev != g_freeBig.begin()) {
        --prev;
        if (prev->first + prev->second == block) {
            prev->second += total;
            return;
        }
    }
    g_freeBig[block] = total;
}

uint32_t guest_realloc(uint32_t p, uint32_t size) {
    if (!p) return guest_malloc(size);
    if (!size) {
        guest_free(p);
        return 0;
    }
    uint32_t old = guest_block_size(p);
    if (size <= old) return p;
    uint32_t q = guest_malloc(size);
    if (q) {
        memcpy(g_mem + q, g_mem + p, old);
        guest_free(p);
    }
    return q;
}

uint32_t guest_calloc(uint32_t n, uint32_t size) {
    uint64_t total = (uint64_t)n * size;
    if (total > 0x7fffffff) return 0;
    uint32_t p = guest_malloc((uint32_t)total);
    if (p) memset(g_mem + p, 0, (size_t)total);
    return p;
}

// ------------------------------------------------------------------------------------------------ images
namespace {
struct Image {
    std::string path;
    uint32_t base, size;
    uint32_t entry;
    uint32_t tlsStart, tlsEnd, tlsIndex, tlsZero;
    std::vector<std::pair<uint32_t, std::string> > imports;   // IAT slot -> "dll!name"
    std::map<std::string, uint32_t> exports;                   // name -> address
};
std::vector<Image> g_images;

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool load_image(const std::string& path, Image* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> file((size_t)n);
    bool ok = fread(&file[0], 1, file.size(), f) == file.size();
    fclose(f);
    if (!ok || n < 0x200) return false;
    const uint8_t* d = &file[0];
    uint32_t pe = le32(d + 0x3c);
    const uint8_t* coff = d + pe + 4;
    uint16_t nsec = le16(coff + 2), optSize = le16(coff + 16);
    const uint8_t* opt = coff + 20;
    Image im;
    im.path = path;
    im.entry = le32(opt + 16);
    im.base = le32(opt + 28);
    im.size = le32(opt + 56);
    uint32_t headers = le32(opt + 60);
    if (im.base + im.size > g_mem_size) return false;
    memcpy(g_mem + im.base, d, headers);
    const uint8_t* sec = opt + optSize;
    for (int i = 0; i < nsec; ++i, sec += 40) {
        uint32_t vsize = le32(sec + 8), va = le32(sec + 12), rsize = le32(sec + 16), raw = le32(sec + 20);
        memset(g_mem + im.base + va, 0, vsize);
        memcpy(g_mem + im.base + va, d + raw, std::min(vsize ? vsize : rsize, rsize));
    }
    im.entry += im.base;
    const uint8_t* dirs = opt + 96;
    // imports
    uint32_t impRva = le32(dirs + 8);
    if (impRva) {
        for (uint32_t desc = im.base + impRva; RD32(desc + 12); desc += 20) {
            std::string dll = guest_str(im.base + RD32(desc + 12));
            for (size_t k = 0; k < dll.size(); ++k) dll[k] = (char)tolower((unsigned char)dll[k]);
            uint32_t names = RD32(desc) ? RD32(desc) : RD32(desc + 16);
            uint32_t iat = im.base + RD32(desc + 16);
            for (uint32_t i = 0;; ++i) {
                uint32_t e = RD32(im.base + names + 4 * i);
                if (!e) break;
                std::string name;
                if (e & 0x80000000u) {
                    char ord[16];
                    sprintf(ord, "#%u", e & 0xffff);
                    name = ord;
                } else {
                    name = guest_str(im.base + e + 2);
                }
                im.imports.push_back(std::make_pair(iat + 4 * i, dll + "!" + name));
            }
        }
    }
    // exports
    uint32_t expRva = le32(dirs + 0);
    if (expRva) {
        uint32_t e = im.base + expRva;
        uint32_t nNames = RD32(e + 24), funcs = im.base + RD32(e + 28), names = im.base + RD32(e + 32), ords = im.base + RD32(e + 36);
        for (uint32_t i = 0; i < nNames; ++i) {
            std::string n = guest_str(im.base + RD32(names + 4 * i));
            uint16_t ord = RD16(ords + 2 * i);
            im.exports[n] = im.base + RD32(funcs + 4 * ord);
        }
    }
    // static TLS
    uint32_t tlsRva = le32(dirs + 9 * 8);
    im.tlsStart = im.tlsEnd = im.tlsIndex = im.tlsZero = 0;
    if (tlsRva) {
        uint32_t t = im.base + tlsRva;
        im.tlsStart = RD32(t);
        im.tlsEnd = RD32(t + 4);
        im.tlsIndex = RD32(t + 8);
        im.tlsZero = RD32(t + 16);
    }
    *out = im;
    return true;
}
}

// ------------------------------------------------------------------------------------------------ dispatch
namespace {
struct Thunk {
    ShimFn fn;
    std::string name;
};
std::vector<Thunk> g_thunks;
std::mutex g_thunkLock;
std::unordered_map<std::string, ShimFn> g_shims;
std::unordered_map<uint32_t, uint32_t> g_slotThunk;          // IAT slot -> thunk index

void missing_import(X86* c) { recomp_fatal(c, "call to an import without a shim (see the thunk name above)"); }

uint32_t add_thunk(ShimFn fn, const std::string& name) {
    std::lock_guard<std::mutex> g(g_thunkLock);
    g_thunks.push_back(Thunk());
    g_thunks.back().fn = fn;
    g_thunks.back().name = name;
    return kThunkBase + 16 * (uint32_t)(g_thunks.size() - 1);
}
}

bool shim_module_known(const std::string& module) {
    std::string prefix = module + "!";
    for (std::unordered_map<std::string, ShimFn>::iterator it = g_shims.begin(); it != g_shims.end(); ++it)
        if (it->first.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

ShimFn shim_lookup(const std::string& fullname) {
    std::unordered_map<std::string, ShimFn>::iterator it = g_shims.find(fullname);
    return it == g_shims.end() ? 0 : it->second;
}

void shim_register(const ShimDef* defs, int count) {
    for (int i = 0; i < count; ++i) g_shims[defs[i].name] = defs[i].fn;
}

uint32_t thunk_for(ShimFn fn, const char* name) { return add_thunk(fn, name); }

static thread_local const char* t_thunkName = "";
const char* current_thunk_name() { return t_thunkName; }

// what each guest thread is in, for the console's watchdog (recomp_sample_threads): its CPU and the shim it is in
static const int kSampledThreads = 8;
static X86* volatile g_sampleCpu[kSampledThreads];
static const char* volatile g_sampleThunk[kSampledThreads];
static const char* g_sampleName[kSampledThreads];
static thread_local int t_sampleSlot = -1;

static void call_thunk(X86* c, uint32_t target) {
    uint32_t i = (target - kThunkBase) / 16;
    if (i >= g_thunks.size()) recomp_fatal(c, "call to a bad thunk %08x", target);
    Thunk& t = g_thunks[i];
    if (t.fn == missing_import) recomp_fatal(c, "import %s has no shim", t.name.c_str());
    const char* outer = t_thunkName;
    t_thunkName = t.name.c_str();
    if (t_sampleSlot >= 0) g_sampleThunk[t_sampleSlot] = t_thunkName;
    t.fn(c);
    t_thunkName = outer;
    if (t_sampleSlot >= 0) g_sampleThunk[t_sampleSlot] = outer;
}

static void sample_register(X86* c, const char* name) {
    static std::atomic<int> next(0);
    int slot = next++;
    if (slot >= kSampledThreads) return;
    g_sampleName[slot] = name;
    g_sampleThunk[slot] = "";
    g_sampleCpu[slot] = c;
    t_sampleSlot = slot;
}

static bool guest_code(uint32_t v) {
    return (v >= kGuestImageBase + 0x1000 && v < kGuestImageBase + 0x5e8000) || (v >= 0x00C61000 && v < 0x00D60000);
}

// one line a guest thread: the shim it is in and the return addresses on its stack, innermost first. Read while
// the threads run: a sample, not a consistent snapshot (esp is the value at the thread's last call).
void recomp_sample_threads(void (*line)(const char*)) {
    for (int i = 0; i < kSampledThreads; ++i) {
        X86* c = g_sampleCpu[i];
        if (!c) continue;
        char buf[512];
        int n = snprintf(buf, sizeof(buf), "  %s: in %s; stack", g_sampleName[i], g_sampleThunk[i][0] ? g_sampleThunk[i] : "game code");
        uint32_t sp = c->r[4];
        int shown = 0;
        for (uint32_t a = sp; a < sp + 0x2000 && a + 4 <= g_mem_size && shown < 12 && n < (int)sizeof(buf) - 12; a += 4) {
            uint32_t v = RD32(a);
            if (guest_code(v)) {
                n += snprintf(buf + n, sizeof(buf) - n, " %08x", v);
                ++shown;
            }
        }
        line(buf);
    }
}

void x86_call(X86* c, uint32_t target) {
    if (target >= kThunkBase && target < kThunkLimit) {
        call_thunk(c, target);
        return;
    }
    for (int t = 0; t < 3; ++t) {
        const X86FuncEntry* table = t == 0 ? x86_functions : t == 1 ? x86_functions_lua : x86_functions_msvcp;
        uint32_t count = t == 0 ? x86_function_count : t == 1 ? x86_function_count_lua : x86_function_count_msvcp;
        uint32_t lo = 0, hi = count;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (table[mid].addr < target) lo = mid + 1;
            else hi = mid;
        }
        if (lo < count && table[lo].addr == target) {
            table[lo].fn(c);
            return;
        }
    }
    recomp_fatal(c, "call to %08x, which is not a known function", target);
}

void x86_import(X86* c, uint32_t slot) {
    // the slot holds the thunk address unless the game rewrote it
    x86_call(c, RD32(slot));
}

// guest atomics (translate.py wraps every lock-prefixed instruction): one spin lock, acquire/release ordered,
// so they are atomic with each other and order memory around them as x86's locked instructions do
static std::atomic_flag g_guestLock = ATOMIC_FLAG_INIT;
void x86_lock(void) {
    while (g_guestLock.test_and_set(std::memory_order_seq_cst)) {
    }
}
void x86_unlock(void) { g_guestLock.clear(std::memory_order_seq_cst); }

void x86_cpuid(X86* c) {
    // a Pentium 4-class CPU: SSE and SSE2 only, so library code never takes its SSSE3/SSE4/AVX paths
    uint32_t leaf = c->r[0];
    if (leaf == 0) {
        c->r[0] = 1;
        c->r[3] = 0x756e6547;    // "GenuineIntel"
        c->r[2] = 0x49656e69;
        c->r[1] = 0x6c65746e;
    } else if (leaf == 1) {
        c->r[0] = 0x00000f29;
        c->r[3] = 0;
        c->r[1] = 0;             // no SSE3/SSSE3/SSE4/AVX/OSXSAVE
        c->r[2] = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 15) | (1u << 23) | (1u << 25) | (1u << 26);
    } else {
        c->r[0] = c->r[1] = c->r[2] = c->r[3] = 0;
    }
}

uint32_t x86_div_check(X86* c, uint32_t eip) { (void)c; return eip; }

void x86_trace_enter(X86* c, uint32_t fn) {
    uint32_t sp = c->r[4];
    recomp_log("> %08x(%08x, %08x, %08x, %08x) ecx=%08x from %08x", fn, RD32(sp + 4), RD32(sp + 8), RD32(sp + 12), RD32(sp + 16),
               c->r[1], RD32(sp));
}
void x86_trace_leave(X86* c, uint32_t fn, uint32_t eax) { (void)c; recomp_log("< %08x = %08x", fn, eax); }

// ------------------------------------------------------------------------------------------------ threads
static thread_local X86* t_cpu = 0;
static std::mutex g_threadLock;
static uint32_t g_nextTid = 0x100;

X86* current_cpu() { return t_cpu; }

// a TIB, a stack and the static TLS blocks of every image, all in guest memory
static X86* make_cpu(uint32_t stackSize) {
    X86* c = (X86*)calloc(1, sizeof(X86));
    c->fcw = 0x27f;
    c->mxcsr = 0x1f80;
    uint32_t stack = guest_malloc(stackSize);
    uint32_t tib = guest_calloc(1, 0x1000);
    uint32_t tlsArray = guest_calloc(1, 64 * 4);
    if (!stack || !tib || !tlsArray) recomp_fatal(0, "no guest memory for a thread");
    WR32(tib + 0x00, 0xffffffffu);           // SEH chain end
    WR32(tib + 0x04, stack + stackSize);     // stack base
    WR32(tib + 0x08, stack);                 // stack limit
    WR32(tib + 0x18, tib);                   // self
    WR32(tib + 0x2c, tlsArray);
    {
        std::lock_guard<std::mutex> g(g_threadLock);
        WR32(tib + 0x24, g_nextTid++);
    }
    WR32(tib + 0x20, 0x4);                   // process id
    for (size_t i = 0; i < g_images.size(); ++i) {
        const Image& im = g_images[i];
        if (!im.tlsIndex) continue;
        uint32_t index = RD32(im.tlsIndex);
        uint32_t len = im.tlsEnd - im.tlsStart;
        uint32_t block = guest_calloc(1, len + im.tlsZero + 16);
        memcpy(g_mem + block, g_mem + im.tlsStart, len);
        WR32(tlsArray + 4 * index, block);
    }
    c->fs_base = tib;
    c->r[4] = (stack + stackSize - 64) & ~15u;
    return c;
}

X86* cpu_new_thread() { return make_cpu(1u << 20); }

uint32_t guest_call(X86* c, uint32_t fn, int nargs, const uint32_t* args, bool stdcall) {
    uint32_t saved[8];
    memcpy(saved, c->r, sizeof(saved));
    uint32_t sp = c->r[4];
    for (int i = nargs - 1; i >= 0; --i) {
        sp -= 4;
        WR32(sp, args[i]);
    }
    sp -= 4;
    WR32(sp, 0xF00DF00Du);                  // host return marker: never dispatched, the C call returns
    c->r[4] = sp;
    x86_call(c, fn);
    uint32_t ret = c->r[0];
    if (!stdcall) c->r[4] += 4 + 4 * nargs;
    if (c->r[4] != saved[4]) {
        // callee-saved registers and esp come back as they were (the callee popped its own arguments)
        c->r[4] = saved[4];
    }
    c->r[3] = saved[3];
    c->r[5] = saved[5];
    c->r[6] = saved[6];
    c->r[7] = saved[7];
    return ret;
}

// ------------------------------------------------------------------------------------------------ threads
#if defined(_WIN32)
void host_thread_start(void (*fn)(void*), void* arg, uint32_t stackBytes) {
    (void)stackBytes;                                        // the PC's default (1 MB) has been enough
    std::thread t(fn, arg);
    t.detach();
}
#else
struct HostThreadStart {
    void (*fn)(void*);
    void* arg;
};
static void* host_thread_main(void* p) {
    HostThreadStart s = *(HostThreadStart*)p;
    delete (HostThreadStart*)p;
    s.fn(s.arg);
    return 0;
}
void host_thread_start(void (*fn)(void*), void* arg, uint32_t stackBytes) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackBytes);
    pthread_t t;
    HostThreadStart* s = new HostThreadStart;
    s->fn = fn;
    s->arg = arg;
    if (pthread_create(&t, &attr, host_thread_main, s) == 0) pthread_detach(t);
    else recomp_log("thread: pthread_create failed");
    pthread_attr_destroy(&attr);
}
#endif

struct ThreadStart {
    uint32_t entry, arg;
    X86* cpu;
};

static void thread_main(void* arg) {
    ThreadStart* s = (ThreadStart*)arg;
    if (g_cfg.threadStarted) g_cfg.threadStarted("guest");
    t_cpu = s->cpu;
    char name[32];
    snprintf(name, sizeof(name), "guest thread %08x", s->entry);
    sample_register(s->cpu, strdup(name));
    uint32_t args[1] = {s->arg};
    guest_call(s->cpu, s->entry, 1, args, true);
    delete s;
}

uint32_t thread_create(uint32_t entry, uint32_t arg, uint32_t* tid_out) {
    ThreadStart* s = new ThreadStart;
    s->entry = entry;
    s->arg = arg;
    s->cpu = cpu_new_thread();
    if (tid_out) *tid_out = RD32(s->cpu->fs_base + 0x24);
    host_thread_start(thread_main, s, 2u << 20);
    return 0x1000 + RD32(s->cpu->fs_base + 0x24);
}

// ------------------------------------------------------------------------------------------------ run
static bool map_imports(Image& im) {
    bool complete = true;
    for (size_t i = 0; i < im.imports.size(); ++i) {
        const std::string& name = im.imports[i].second;
        size_t bang = name.find('!');
        bool bound = false;
        for (size_t k = 0; k < g_images.size() && !bound; ++k) {
            std::string file = g_images[k].path.substr(g_images[k].path.find_last_of("/\\") + 1);
            for (size_t j = 0; j < file.size(); ++j) file[j] = (char)tolower((unsigned char)file[j]);
            if (file != name.substr(0, bang)) continue;
            std::map<std::string, uint32_t>::iterator e = g_images[k].exports.find(name.substr(bang + 1));
            if (e != g_images[k].exports.end()) {
                WR32(im.imports[i].first, e->second);
                bound = true;
            }
        }
        if (bound) continue;
        std::unordered_map<std::string, ShimFn>::iterator it = g_shims.find(name);
        ShimFn fn = it != g_shims.end() ? it->second : missing_import;
        if (it == g_shims.end()) complete = false;
        WR32(im.imports[i].first, add_thunk(fn, name));
    }
    return complete;
}

int recomp_run(const RecompConfig& cfg) {
    g_cfg = cfg;
    if (cfg.logPath) g_log = fopen(cfg.logPath, "wt");
    g_mem_size = cfg.memSize;
    if (cfg.reserveMemory && cfg.commitMemory) {
        // the image area (isaac-ng.exe, the rebased DLLs) committed now, the heap as it grows
        g_mem = cfg.reserveMemory(g_mem_size);
        if (g_mem && !cfg.commitMemory(g_mem, 0, kGuestHeapLow)) g_mem = 0;
        g_committed = kGuestHeapLow;
        if (!g_mem) {
            recomp_log("guest memory: reserving %u MB failed; allocating it whole", g_mem_size >> 20);
            g_cfg.commitMemory = 0;
            g_mem = (uint8_t*)calloc(1, g_mem_size);
            g_committed = g_mem_size;
        }
    } else {
#if defined(_WIN32)
        g_mem = (uint8_t*)VirtualAlloc(0, g_mem_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        g_mem = (uint8_t*)calloc(1, g_mem_size);
#endif
        g_committed = g_mem_size;
    }
    if (!g_mem) {
        recomp_log("no guest memory (%u MB)", g_mem_size >> 20);
        return 2;
    }
    Image exe;
    if (!load_image(cfg.gameDir + "/isaac-ng.exe", &exe)) {
        recomp_log("cannot load %s/isaac-ng.exe", cfg.gameDir.c_str());
        return 2;
    }
    g_images.push_back(exe);
    g_heapTop = kGuestHeapLow;
    g_heapEnd = g_mem_size;
    shims_after_load();
    // the game's DLLs, rebased by tools/recomp/rebase.py into the free space after isaac-ng.exe's image
    // (Lua5.3.3r.dll at 0x00C60000, msvcp140.dll at 0x00CA0000), so guest memory is 16 MB plus the heap
    const std::string dlls[2] = {cfg.dllDir + "/msvcp140.dll", cfg.dllDir + "/lua5.3.3r.dll"};
    for (int i = 0; i < 2; ++i) {
        Image dll;
        if (!load_image(dlls[i], &dll)) {
            recomp_log("cannot load %s", dlls[i].c_str());
            return 2;
        }
        g_images.push_back(dll);
        map_imports(g_images.back());
        recomp_log("%s loaded at %08x, %u exports", dlls[i].c_str(), dll.base, (unsigned)dll.exports.size());
    }
    bool complete = map_imports(g_images[0]);
    int missing = 0;
    for (size_t i = 0; i < g_images[0].imports.size(); ++i)
        if (g_shims.find(g_images[0].imports[i].second) == g_shims.end()) ++missing;
    recomp_log("isaac-ng.exe loaded at %08x, %u bytes, entry %08x; %d of %d imports without a shim",
               exe.base, exe.size, exe.entry, missing, (int)g_images[0].imports.size());
    (void)complete;
    X86* c = make_cpu(4u << 20);
    t_cpu = c;
    sample_register(c, "main");
    for (size_t i = 1; i < g_images.size(); ++i) {           // DllMain(hinst, DLL_PROCESS_ATTACH, 0)
        uint32_t args[3] = {g_images[i].base, 1, 0};
        uint32_t ok = guest_call(c, g_images[i].entry, 3, args, true);
        recomp_log("%s DllMain -> %u", g_images[i].path.c_str(), ok);
    }
    c->r[4] -= 4;
    WR32(c->r[4], 0xF00DF00Du);
    x86_call(c, exe.entry);
    recomp_log("entry returned eax=%08x", c->r[0]);
    return (int)c->r[0];
}
