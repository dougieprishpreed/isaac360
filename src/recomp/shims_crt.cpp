// UCRT and VCRUNTIME imports of isaac-ng.exe (docs/RECOMP_PLAN.md). cdecl unless noted. Strings and buffers are
// guest memory; files go through the host C library with the game folder as the current directory.
#include "runtime.h"
#include "vfile.h"
#include <algorithm>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <map>
#include <string>
#include <vector>

void hle_vorbis_freed(uint32_t p);                  // hle_vorbis.cpp

namespace {
// ------------------------------------------------------------------------------------------------ files
std::mutex g_fileLock;
std::map<uint32_t, VFile*> g_files;         // guest FILE* (a small guest block) -> host stream
uint32_t g_stdio[3];
uint32_t g_errno;

VFile* host_file(uint32_t g) {
    std::lock_guard<std::mutex> l(g_fileLock);
    std::map<uint32_t, VFile*>::iterator it = g_files.find(g);
    return it == g_files.end() ? 0 : it->second;
}

std::string host_path(const std::string& p) { return recomp_host_path(p); }

void s_fopen(X86* c) {
    std::string path = guest_str(ARG(c, 0)), mode = guest_str(ARG(c, 1));
    bool write = mode.find_first_of("wa+") != std::string::npos;
    // the game's data (resources/, chiefly the archives it reopens per entry) is read through shared readers
    std::string lower = path;
    for (size_t i = 0; i < lower.size(); ++i) lower[i] = lower[i] == '\\' ? '/' : (char)tolower((unsigned char)lower[i]);
    bool shared = !write && (lower.compare(0, 10, "resources/") == 0 || lower.compare(0, 12, "./resources/") == 0);
    VFile* f = vf_open(recomp_host_path(path, write), mode, shared);
    if (!f) {
        recomp_log("fopen %s (%s): not found", path.c_str(), mode.c_str());
        RET_CDECL(c, 0);
        return;
    }
    uint32_t g = guest_calloc(1, 32);
    static int trace = getenv("BOI360_TRACE_FILES") ? 1 : 0;
    if (trace) recomp_log("fopen %s (%s) -> %08x", path.c_str(), mode.c_str(), g);
    std::lock_guard<std::mutex> l(g_fileLock);
    g_files[g] = f;
    RET_CDECL(c, g);
}
void s_fclose(X86* c) {
    uint32_t g = ARG(c, 0);
    VFile* f = host_file(g);
    if (f && g > 3) {
        vf_close(f);
        std::lock_guard<std::mutex> l(g_fileLock);
        g_files.erase(g);
        guest_free(g);
    }
    RET_CDECL(c, f ? 0 : (uint32_t)-1);
}
void s_fread(X86* c) {
    VFile* f = host_file(ARG(c, 3));
    uint32_t n = f ? (uint32_t)vf_read(f, G(ARG(c, 0)), ARG(c, 1), ARG(c, 2)) : 0;
    RET_CDECL(c, n);
}
void s_fwrite(X86* c) {
    VFile* f = host_file(ARG(c, 3));
    uint32_t n = f ? (uint32_t)vf_write(f, G(ARG(c, 0)), ARG(c, 1), ARG(c, 2)) : 0;
    RET_CDECL(c, n);
}
void s_fseek(X86* c) {
    VFile* f = host_file(ARG(c, 0));
    RET_CDECL(c, f ? (uint32_t)vf_seek(f, (int32_t)ARG(c, 1), (int)ARG(c, 2)) : (uint32_t)-1);
}
void s_ftell(X86* c) {
    VFile* f = host_file(ARG(c, 0));
    RET_CDECL(c, f ? (uint32_t)vf_tell(f) : (uint32_t)-1);
}
void s_fflush(X86* c) {
    VFile* f = host_file(ARG(c, 0));
    if (f) vf_flush(f);
    RET_CDECL(c, 0);
}
void s_fileno(X86* c) { RET_CDECL(c, 3); }
void s_get_osfhandle(X86* c) { RET_CDECL(c, 0x1234); }
void s_acrt_iob_func(X86* c) {
    uint32_t i = ARG(c, 0);
    RET_CDECL(c, i < 3 ? g_stdio[i] : 0);
}
void s_set_fmode(X86* c) { RET_CDECL(c, 0); }
uint32_t g_commode;
void s_p_commode(X86* c) {
    if (!g_commode) g_commode = guest_calloc(1, 4);
    RET_CDECL(c, g_commode);
}

// printf family: the guest format and va_list are formatted host side, argument by argument
std::string format_guest(const std::string& fmt, uint32_t va) {
    std::string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out += fmt[i];
            continue;
        }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && strchr("-+ #0", fmt[j])) spec += fmt[j++];
        if (j < fmt.size() && fmt[j] == '*') {
            spec += std::to_string((int32_t)RD32(va));
            va += 4;
            ++j;
        }
        while (j < fmt.size() && isdigit((unsigned char)fmt[j])) spec += fmt[j++];
        if (j < fmt.size() && fmt[j] == '.') {
            spec += fmt[j++];
            if (j < fmt.size() && fmt[j] == '*') {
                spec += std::to_string((int32_t)RD32(va));
                va += 4;
                ++j;
            }
            while (j < fmt.size() && isdigit((unsigned char)fmt[j])) spec += fmt[j++];
        }
        int longs = 0;
        bool i64 = false;
        while (j < fmt.size() && strchr("hlLzjtI", fmt[j])) {
            if (fmt[j] == 'l') ++longs;
            if (fmt[j] == 'I' && j + 2 < fmt.size() && fmt[j + 1] == '6' && fmt[j + 2] == '4') {
                i64 = true;
                j += 2;
            }
            ++j;
        }
        if (longs >= 2) i64 = true;
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        char buf[512];
        buf[0] = 0;
        switch (conv) {
        case '%': strcpy(buf, "%"); break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
            if (i64) {
                uint64_t v = (uint64_t)RD32(va) | ((uint64_t)RD32(va + 4) << 32);
                va += 8;
                snprintf(buf, sizeof(buf), (spec + "ll" + conv).c_str(), (unsigned long long)v);
            } else {
                uint32_t v = RD32(va);
                va += 4;
                snprintf(buf, sizeof(buf), (spec + conv).c_str(), v);
            }
            break;
        case 'p':
            snprintf(buf, sizeof(buf), "%08X", RD32(va));
            va += 4;
            break;
        case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': case 'a': case 'A': {
            double d = RDD(va);
            va += 8;
            snprintf(buf, sizeof(buf), (spec + conv).c_str(), d);
            break;
        }
        case 's': {
            std::string s = guest_str(RD32(va));
            va += 4;
            if (!RD32(va - 4)) s = "(null)";
            snprintf(buf, sizeof(buf), (spec + "s").c_str(), s.c_str());
            break;
        }
        default:
            snprintf(buf, sizeof(buf), "<%%%c?>", conv);
        }
        out += buf;
        i = j;
    }
    return out;
}

// __stdio_common_vsprintf(options(8), buffer, count, format, locale, va_list) -> int
void s_vsprintf(X86* c) {
    uint32_t buf = ARG(c, 2), count = ARG(c, 3);
    std::string s = format_guest(guest_str(ARG(c, 4)), ARG(c, 6));
    // BOI360_TRACE_PRINTF=1 logs the first 3000 strings, =N the first N; BOI360_TRACE_PRINTF_ONLY="a,b" only those
    // holding one of the words (a whole run's game log lines without its file lookups)
    static int trace = getenv("BOI360_TRACE_PRINTF") ? (atoi(getenv("BOI360_TRACE_PRINTF")) > 1 ? atoi(getenv("BOI360_TRACE_PRINTF")) : 3000) : 0;
    static int traced = 0;
    static std::vector<std::string> only;
    static bool onlyParsed = false;
    if (!onlyParsed) {
        onlyParsed = true;
        std::string list = getenv("BOI360_TRACE_PRINTF_ONLY") ? getenv("BOI360_TRACE_PRINTF_ONLY") : "";
        for (size_t at = 0; at < list.size();) {
            size_t comma = list.find(',', at);
            if (comma == std::string::npos) comma = list.size();
            if (comma > at) only.push_back(list.substr(at, comma - at));
            at = comma + 1;
        }
    }
    bool wanted = only.empty();
    for (size_t i = 0; i < only.size() && !wanted; ++i) wanted = s.find(only[i]) != std::string::npos;
    if (traced < trace && wanted) {
        ++traced;
        recomp_log("printf> %s", s.substr(0, 200).c_str());
    }
    if (buf && count) guest_write_str(buf, s, count);
    RET_CDECL(c, (uint32_t)s.size());
}
void s_vsprintf_s(X86* c) { s_vsprintf(c); }
// __stdio_common_vfprintf(options(8), stream, format, locale, va_list)
void s_vfprintf(X86* c) {
    std::string s = format_guest(guest_str(ARG(c, 3)), ARG(c, 5));
    VFile* f = host_file(ARG(c, 2));
    if (f) vf_puts(s.c_str(), f);
    RET_CDECL(c, (uint32_t)s.size());
}
// __stdio_common_vsscanf(options(8), buffer, count, format, locale, va_list): the formats the game uses. sscanf_s
// (options bit 0, _CRT_INTERNAL_SCANF_SECURECRT) passes each %s buffer's size after it; a word that doesn't fit
// empties the buffer and ends the scan, as the CRT does.
void s_vsscanf(X86* c) {
    std::string in = guest_str(ARG(c, 2)), fmt = guest_str(ARG(c, 4));
    bool secure = (ARG(c, 0) & 1) != 0;
    uint32_t va = ARG(c, 6);
    const char* p = in.c_str();
    int assigned = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (isspace((unsigned char)fmt[i])) {
            while (isspace((unsigned char)*p)) ++p;
            continue;
        }
        if (fmt[i] != '%') {
            if (*p != fmt[i]) break;
            ++p;
            continue;
        }
        ++i;
        bool l = false;
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'h')) l = fmt[i++] == 'l' || l;
        char conv = fmt[i];
        char* end = 0;
        if (conv == 'd' || conv == 'i') {
            long v = strtol(p, &end, conv == 'i' ? 0 : 10);
            if (end == p) break;
            WR32(RD32(va), (uint32_t)v);
        } else if (conv == 'u' || conv == 'x' || conv == 'X') {
            unsigned long v = strtoul(p, &end, conv == 'u' ? 10 : 16);
            if (end == p) break;
            WR32(RD32(va), (uint32_t)v);
        } else if (conv == 'f' || conv == 'g' || conv == 'e') {
            double v = strtod(p, &end);
            if (end == p) break;
            if (l) WRD(RD32(va), v);
            else WRF(RD32(va), (float)v);
        } else if (conv == 's') {
            while (isspace((unsigned char)*p)) ++p;
            const char* s = p;
            while (*p && !isspace((unsigned char)*p)) ++p;
            if (p == s) break;
            std::string w(s, p);
            if (secure) {
                uint32_t room = RD32(va + 4);
                va += 4;
                if (w.size() + 1 > room) {
                    if (room) WR8(RD32(va - 4), 0);
                    break;
                }
            }
            memcpy(g_mem + RD32(secure ? va - 4 : va), w.c_str(), w.size() + 1);
            end = (char*)p;
        } else {
            recomp_log("sscanf: %%%c not handled (%s)", conv, fmt.c_str());
            break;
        }
        va += 4;
        p = end;
        ++assigned;
    }
    RET_CDECL(c, (uint32_t)assigned);
}

// ------------------------------------------------------------------------------------------------ heap
void big_alloc_log(X86* c, uint32_t size) {
    if (size >= (4u << 20)) recomp_log("alloc %u KB from %08x (heap %u MB)", size >> 10, RD32(c->r[4]), guest_heap_used() >> 20);
}
// BOI360_HEAP_PROFILE=frame: live heap bytes by allocation site (the first three return addresses in
// isaac-ng.exe found on the stack), written to build/recomp/heap_profile.txt at that presented frame
bool g_heapProfile = getenv("BOI360_HEAP_PROFILE") != 0;
std::map<uint32_t, std::pair<std::string, uint32_t> > g_live;
std::mutex g_liveLock;
// the first three return addresses in isaac-ng.exe on c's stack
std::string stack_site(X86* c) {
    std::string site;
    char b[16];
    int found = 0;
    for (int i = 0; i < 64 && found < 3; ++i) {
        uint32_t v = RD32(c->r[4] + 4 * i);
        if (v >= 0x00401000 && v < 0x00E00000) {
            sprintf(b, found ? " %08x" : "%08x", v);
            site += b;
            ++found;
        }
    }
    return site;
}
void profile_alloc(X86* c, uint32_t p, uint32_t size) {
    if (!p) return;
    std::string site = stack_site(c);
    std::lock_guard<std::mutex> lock(g_liveLock);
    g_live[p] = std::make_pair(site, size);
}
void profile_free(uint32_t p) {
    if (!p) return;
    std::lock_guard<std::mutex> lock(g_liveLock);
    g_live.erase(p);
}
uint32_t traced_alloc(X86* c, uint32_t p, uint32_t size) {
    if (g_heapProfile) profile_alloc(c, p, size);
    if (guest_heap_checking()) guest_heap_note(p, "made from", stack_site(c));
    return p;
}
void s_malloc(X86* c) { big_alloc_log(c, ARG(c, 0)); RET_CDECL(c, traced_alloc(c, guest_malloc(ARG(c, 0)), ARG(c, 0))); }
void s_free(X86* c) {
    if (g_heapProfile) profile_free(ARG(c, 0));
    if (guest_heap_checking()) guest_heap_note(ARG(c, 0), "freed from", stack_site(c));
    hle_vorbis_freed(ARG(c, 0));                    // a music decoder the host serves goes with its object
    guest_free(ARG(c, 0));
    RET_CDECL(c, 0);
}
void s_realloc(X86* c) {
    big_alloc_log(c, ARG(c, 1));
    if (g_heapProfile) profile_free(ARG(c, 0));
    if (guest_heap_checking() && ARG(c, 0)) guest_heap_note(ARG(c, 0), "moved from", stack_site(c));
    RET_CDECL(c, traced_alloc(c, guest_realloc(ARG(c, 0), ARG(c, 1)), ARG(c, 1)));
}
void s_calloc(X86* c) { big_alloc_log(c, ARG(c, 0) * ARG(c, 1)); RET_CDECL(c, traced_alloc(c, guest_calloc(ARG(c, 0), ARG(c, 1)), ARG(c, 0) * ARG(c, 1))); }
void s_callnewh(X86* c) { RET_CDECL(c, 0); }
void s_set_new_mode(X86* c) { RET_CDECL(c, 0); }

// ------------------------------------------------------------------------------------------------ strings
void s_memcpy(X86* c) { memmove(G(ARG(c, 0)), G(ARG(c, 1)), ARG(c, 2)); RET_CDECL(c, ARG(c, 0)); }
void s_memmove(X86* c) { memmove(G(ARG(c, 0)), G(ARG(c, 1)), ARG(c, 2)); RET_CDECL(c, ARG(c, 0)); }
void s_memset(X86* c) { memset(G(ARG(c, 0)), (int)ARG(c, 1), ARG(c, 2)); RET_CDECL(c, ARG(c, 0)); }
void s_memchr(X86* c) {
    const void* p = memchr(G(ARG(c, 0)), (int)ARG(c, 1), ARG(c, 2));
    RET_CDECL(c, p ? (uint32_t)((const uint8_t*)p - g_mem) : 0);
}
void s_strchr(X86* c) {
    const char* p = strchr((const char*)G(ARG(c, 0)), (int)(uint8_t)ARG(c, 1));
    RET_CDECL(c, p ? (uint32_t)((const uint8_t*)p - g_mem) : 0);
}
void s_strstr(X86* c) {
    const char* p = strstr((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)));
    RET_CDECL(c, p ? (uint32_t)((const uint8_t*)p - g_mem) : 0);
}
void s_strpbrk(X86* c) {
    const char* p = strpbrk((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)));
    RET_CDECL(c, p ? (uint32_t)((const uint8_t*)p - g_mem) : 0);
}
void s_strncmp(X86* c) { RET_CDECL(c, (uint32_t)strncmp((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)), ARG(c, 2))); }
void s_strnicmp(X86* c) {
    const char* a = (const char*)G(ARG(c, 0));
    const char* b = (const char*)G(ARG(c, 1));
    uint32_t n = ARG(c, 2);
    int r = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) {
            r = x - y;
            break;
        }
    }
    RET_CDECL(c, (uint32_t)r);
}
void s_strncpy(X86* c) { strncpy((char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)), ARG(c, 2)); RET_CDECL(c, ARG(c, 0)); }
void s_strcpy_s(X86* c) {
    std::string s = guest_str(ARG(c, 2));
    if (s.size() + 1 > ARG(c, 1)) { RET_CDECL(c, 34); return; }
    guest_write_str(ARG(c, 0), s, ARG(c, 1));
    RET_CDECL(c, 0);
}
void s_strncpy_s(X86* c) {
    std::string s = guest_str(ARG(c, 2));
    uint32_t n = ARG(c, 3);
    if (n != 0xffffffffu && s.size() > n) s.resize(n);
    guest_write_str(ARG(c, 0), s, ARG(c, 1));
    RET_CDECL(c, 0);
}
void s_strcat_s(X86* c) {
    std::string s = guest_str(ARG(c, 0)) + guest_str(ARG(c, 2));
    guest_write_str(ARG(c, 0), s, ARG(c, 1));
    RET_CDECL(c, 0);
}
void s_strdup(X86* c) {
    std::string s = guest_str(ARG(c, 0));
    uint32_t p = guest_malloc((uint32_t)s.size() + 1);
    memcpy(G(p), s.c_str(), s.size() + 1);
    RET_CDECL(c, p);
}
void s_tolower(X86* c) { RET_CDECL(c, (uint32_t)tolower((int)ARG(c, 0))); }
void s_toupper(X86* c) { RET_CDECL(c, (uint32_t)toupper((int)ARG(c, 0))); }
void s_isdigit(X86* c) { RET_CDECL(c, (uint32_t)isdigit((int)(ARG(c, 0) & 0xff))); }
void s_isspace(X86* c) { RET_CDECL(c, (uint32_t)isspace((int)(ARG(c, 0) & 0xff))); }
void s_ispunct(X86* c) { RET_CDECL(c, (uint32_t)ispunct((int)(ARG(c, 0) & 0xff))); }
void s_atoi(X86* c) { RET_CDECL(c, (uint32_t)atoi((const char*)G(ARG(c, 0)))); }
void s_atof(X86* c) { RET_ST0(c, atof((const char*)G(ARG(c, 0)))); }
void s_strtoull(X86* c) {
    char* end = 0;
    const char* s = (const char*)G(ARG(c, 0));
    unsigned long long v = strtoull(s, &end, (int)ARG(c, 2));
    if (ARG(c, 1)) WR32(ARG(c, 1), ARG(c, 0) + (uint32_t)(end - s));
    RET_CDECL64(c, v);
}
// mbstowcs_s(size_t* converted, wchar_t* dst, size_t dstWords, const char* src, size_t count): a null dst asks
// for the length the conversion needs (terminator included)
void s_mbstowcs_s(X86* c) {
    std::string s = guest_str(ARG(c, 3));
    uint32_t dst = ARG(c, 1), cap = ARG(c, 2), count = ARG(c, 4);
    if (count != 0xffffffffu && count < s.size()) s.resize(count);
    if (!dst) {
        if (ARG(c, 0)) WR32(ARG(c, 0), (uint32_t)s.size() + 1);
        RET_CDECL(c, 0);
        return;
    }
    uint32_t n = 0;
    for (; n < s.size() && n + 1 < cap; ++n) WR16(dst + 2 * n, (uint8_t)s[n]);
    if (cap) WR16(dst + 2 * n, 0);
    if (ARG(c, 0)) WR32(ARG(c, 0), n + 1);
    RET_CDECL(c, 0);
}
// wcstombs_s(size_t* converted, char* dst, size_t dstBytes, const wchar_t* src, size_t count)
void s_wcstombs_s(X86* c) {
    std::u16string w = guest_wstr(ARG(c, 3));
    uint32_t dst = ARG(c, 1), cap = ARG(c, 2), count = ARG(c, 4);
    if (count != 0xffffffffu && count < w.size()) w.resize(count);
    if (!dst) {
        if (ARG(c, 0)) WR32(ARG(c, 0), (uint32_t)w.size() + 1);
        RET_CDECL(c, 0);
        return;
    }
    uint32_t n = 0;
    for (; n < w.size() && n + 1 < cap; ++n) WR8(dst + n, w[n] < 128 ? (uint8_t)w[n] : '?');
    if (cap) WR8(dst + n, 0);
    if (ARG(c, 0)) WR32(ARG(c, 0), n + 1);
    RET_CDECL(c, 0);
}

// ------------------------------------------------------------------------------------------------ math
// _libm_sse2_*_precise: argument and result in xmm0 (double)
#define LIBM1(name, expr) void name(X86* c) { double x = c->x[0].d[0]; c->x[0].d[0] = (expr); RET_CDECL(c, c->r[0]); }
LIBM1(s_sin, sin(x))
LIBM1(s_cos, cos(x))
LIBM1(s_acos, acos(x))
LIBM1(s_asin, asin(x))
LIBM1(s_atan, atan(x))
LIBM1(s_exp, exp(x))
LIBM1(s_log, log(x))
LIBM1(s_log10, log10(x))
LIBM1(s_sqrt, sqrt(x))
void s_pow(X86* c) { c->x[0].d[0] = pow(c->x[0].d[0], c->x[1].d[0]); RET_CDECL(c, c->r[0]); }
// _CIatan2/_CIfmod: arguments on the x87 stack (ST1 op ST0), result in ST0
void s_CIatan2(X86* c) { double x = X87_POP(c); ST(0) = atan2(ST(0), x); RET_CDECL(c, c->r[0]); }
void s_CIfmod(X86* c) { double y = X87_POP(c); ST(0) = fmod(ST(0), y); RET_CDECL(c, c->r[0]); }
void s_ceil(X86* c) { RET_ST0(c, ceil(ARGD(c, 0))); }
void s_floor(X86* c) { RET_ST0(c, floor(ARGD(c, 0))); }
void s_ldexp(X86* c) { RET_ST0(c, ldexp(ARGD(c, 0), (int)ARG(c, 2))); }
void s_modf(X86* c) {
    double ip;
    double f = modf(ARGD(c, 0), &ip);
    WRD(ARG(c, 2), ip);
    RET_ST0(c, f);
}
void s_roundf(X86* c) { RET_ST0(c, (double)roundf(ARGF(c, 0))); }
void s_copysignf(X86* c) { RET_ST0(c, (double)copysignf(ARGF(c, 0), ARGF(c, 1))); }
void s_fdclass(X86* c) {
    float f = ARGF(c, 0);
    int k = isnan(f) ? 2 : isinf(f) ? 1 : f == 0 ? 0 : fpclassify(f) == FP_SUBNORMAL ? -2 : -1;
    RET_CDECL(c, (uint32_t)k);
}
void s_setusermatherr(X86* c) { RET_CDECL(c, 0); }
void s_controlfp_s(X86* c) {
    if (ARG(c, 0)) WR32(ARG(c, 0), 0x0009001f);
    RET_CDECL(c, 0);
}

// ------------------------------------------------------------------------------------------------ runtime
uint32_t g_argc, g_argv, g_envp;
void s_p_argc(X86* c) { RET_CDECL(c, g_argc); }
void s_p_argv(X86* c) { RET_CDECL(c, g_argv); }
void s_ret0(X86* c) { RET_CDECL(c, 0); }
void s_ret1(X86* c) { RET_CDECL(c, 1); }
void s_get_env(X86* c) { RET_CDECL(c, g_envp); }
// _initterm(first, last): every non-null function pointer in [first, last)
void s_initterm(X86* c) {
    uint32_t a = ARG(c, 0), b = ARG(c, 1);
    for (; a < b; a += 4) {
        uint32_t fn = RD32(a);
        if (fn) guest_call(c, fn, 0, 0, false);
    }
    RET_CDECL(c, 0);
}
void s_initterm_e(X86* c) {
    uint32_t a = ARG(c, 0), b = ARG(c, 1), r = 0;
    for (; a < b && !r; a += 4) {
        uint32_t fn = RD32(a);
        if (fn) r = guest_call(c, fn, 0, 0, false);
    }
    RET_CDECL(c, r);
}
std::vector<uint32_t> g_atexit;
void s_crt_atexit(X86* c) { g_atexit.push_back(ARG(c, 0)); RET_CDECL(c, 0); }
void s_register_onexit(X86* c) { g_atexit.push_back(ARG(c, 1)); RET_CDECL(c, 0); }
void s_errno(X86* c) {
    static uint32_t e = 0;
    if (!e) e = guest_calloc(1, 4);
    RET_CDECL(c, e);
}
void s_set_errno(X86* c) { RET_CDECL(c, 0); }
void s_exit(X86* c) {
    recomp_log("game exit(%d)", (int)ARG(c, 0));
    vf_drain();                         // the files the game wrote reach the drive
    recomp_log_drain();
    _exit((int)ARG(c, 0));
}
void s_abort(X86* c) { recomp_fatal(c, "abort()"); }
void s_terminate(X86* c) { recomp_fatal(c, "terminate()"); }
void s_invalid_param(X86* c) { recomp_fatal(c, "_invalid_parameter_noinfo_noreturn"); }
void s_wassert(X86* c) {
    std::u16string m = guest_wstr(ARG(c, 0)), f = guest_wstr(ARG(c, 1));
    std::string ms(m.begin(), m.end()), fs(f.begin(), f.end());
    recomp_fatal(c, "assert %s (%s:%u)", ms.c_str(), fs.c_str(), ARG(c, 2));
}
void s_perror(X86* c) { recomp_log("perror %s", guest_str(ARG(c, 0)).c_str()); RET_CDECL(c, 0); }
void s_beginthreadex(X86* c) {
    uint32_t tid = 0;
    uint32_t h = thread_create(ARG(c, 2), ARG(c, 3), &tid);
    if (ARG(c, 5)) WR32(ARG(c, 5), tid);
    RET_CDECL(c, h);
}
void s_getenv(X86* c) {
    std::string n = guest_str(ARG(c, 0));
    static std::map<std::string, uint32_t> cache;
    if (n == "USERPROFILE") {
        if (!cache.count(n)) {
            std::string v = recomp_config().saveDir;
            uint32_t p = guest_malloc((uint32_t)v.size() + 1);
            memcpy(G(p), v.c_str(), v.size() + 1);
            cache[n] = p;
        }
        RET_CDECL(c, cache[n]);
        return;
    }
    recomp_log("getenv %s -> null", n.c_str());
    RET_CDECL(c, 0);
}
void s_access(X86* c) {
    std::string p = host_path(guest_str(ARG(c, 0)));
    if (getenv("BOI360_TRACE_FILES")) recomp_log("_access %s", p.c_str());
    RET_CDECL(c, recomp_host_exists(p, 0) ? 0 : (uint32_t)-1);
}
void s_remove(X86* c) {
    vf_drain();
    RET_CDECL(c, (uint32_t)remove(recomp_host_path(guest_str(ARG(c, 0)), true).c_str()));
}
void s_qsort(X86* c) {
    uint32_t base = ARG(c, 0), n = ARG(c, 1), size = ARG(c, 2), cmp = ARG(c, 3);
    // insertion sort with the guest comparator (cdecl, (a, b)): stable enough and simple
    std::vector<uint8_t> tmp(size);
    for (uint32_t i = 1; i < n; ++i) {
        memcpy(&tmp[0], g_mem + base + i * size, size);
        uint32_t j = i;
        uint32_t key = guest_malloc(size);
        memcpy(g_mem + key, &tmp[0], size);
        while (j > 0) {
            uint32_t args[2] = {base + (j - 1) * size, key};
            if ((int32_t)guest_call(c, cmp, 2, args, false) <= 0) break;
            memmove(g_mem + base + j * size, g_mem + base + (j - 1) * size, size);
            --j;
        }
        memcpy(g_mem + base + j * size, g_mem + key, size);
        guest_free(key);
    }
    RET_CDECL(c, 0);
}

// ------------------------------------------------------------------------------------------------ time
void s_time64(X86* c) {
    time_t t = time(0);
    if (ARG(c, 0)) WR64(ARG(c, 0), (uint64_t)t);
    RET_CDECL64(c, (uint64_t)t);
}
uint32_t g_tm;
void fill_tm(const struct tm* t) {
    if (!g_tm) g_tm = guest_calloc(1, 64);
    int v[9] = {t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday, t->tm_mon, t->tm_year, t->tm_wday, t->tm_yday, t->tm_isdst};
    for (int i = 0; i < 9; ++i) WR32(g_tm + 4 * i, (uint32_t)v[i]);
}
void read_tm(uint32_t a, struct tm* t) {
    memset(t, 0, sizeof(*t));
    t->tm_sec = (int)RD32(a); t->tm_min = (int)RD32(a + 4); t->tm_hour = (int)RD32(a + 8); t->tm_mday = (int)RD32(a + 12);
    t->tm_mon = (int)RD32(a + 16); t->tm_year = (int)RD32(a + 20); t->tm_wday = (int)RD32(a + 24); t->tm_yday = (int)RD32(a + 28);
    t->tm_isdst = (int)RD32(a + 32);
}
void s_localtime64(X86* c) {
    time_t t = (time_t)RD64(ARG(c, 0));
    struct tm* tm = localtime(&t);
    if (!tm) { RET_CDECL(c, 0); return; }
    fill_tm(tm);
    RET_CDECL(c, g_tm);
}
void s_gmtime64(X86* c) {
    time_t t = (time_t)RD64(ARG(c, 0));
    struct tm* tm = gmtime(&t);
    if (!tm) { RET_CDECL(c, 0); return; }
    fill_tm(tm);
    RET_CDECL(c, g_tm);
}
void s_mkgmtime64(X86* c) {
    struct tm t;
    read_tm(ARG(c, 0), &t);
#if defined(_WIN32)
    RET_CDECL64(c, (uint64_t)_mkgmtime(&t));
#else
    RET_CDECL64(c, (uint64_t)mktime(&t));
#endif
}
void s_strftime(X86* c) {
    struct tm t;
    read_tm(ARG(c, 3), &t);
    char buf[512];
    size_t n = strftime(buf, sizeof(buf), guest_str(ARG(c, 2)).c_str(), &t);
    guest_write_str(ARG(c, 0), std::string(buf, n), ARG(c, 1));
    RET_CDECL(c, (uint32_t)n);
}

// setjmp returns 0 on the direct path; a longjmp back into a translated frame needs translator support
// (docs/RECOMP_PLAN.md): it stops with its caller until then
void s_setjmp3(X86* c) { RET_CDECL(c, 0); }
void s_longjmp(X86* c) { recomp_fatal(c, "longjmp (Lua error or libpng error path) is not supported yet"); }

const ShimDef kCrt[] = {
    {"vcruntime140.dll!_setjmp3", s_setjmp3},
    {"vcruntime140.dll!longjmp", s_longjmp},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fopen", s_fopen},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fclose", s_fclose},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fread", s_fread},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fwrite", s_fwrite},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fseek", s_fseek},
    {"api-ms-win-crt-stdio-l1-1-0.dll!ftell", s_ftell},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fflush", s_fflush},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_fileno", s_fileno},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle", s_get_osfhandle},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__acrt_iob_func", s_acrt_iob_func},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_set_fmode", s_set_fmode},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__p__commode", s_p_commode},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsprintf", s_vsprintf},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsprintf_s", s_vsprintf_s},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vfprintf", s_vfprintf},
    {"api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vsscanf", s_vsscanf},
    {"api-ms-win-crt-heap-l1-1-0.dll!malloc", s_malloc},
    {"api-ms-win-crt-heap-l1-1-0.dll!free", s_free},
    {"api-ms-win-crt-heap-l1-1-0.dll!realloc", s_realloc},
    {"api-ms-win-crt-heap-l1-1-0.dll!calloc", s_calloc},
    {"api-ms-win-crt-heap-l1-1-0.dll!_callnewh", s_callnewh},
    {"api-ms-win-crt-heap-l1-1-0.dll!_set_new_mode", s_set_new_mode},
    {"vcruntime140.dll!memcpy", s_memcpy},
    {"vcruntime140.dll!memmove", s_memmove},
    {"vcruntime140.dll!memset", s_memset},
    {"vcruntime140.dll!memchr", s_memchr},
    {"vcruntime140.dll!strchr", s_strchr},
    {"vcruntime140.dll!strstr", s_strstr},
    {"api-ms-win-crt-string-l1-1-0.dll!strpbrk", s_strpbrk},
    {"api-ms-win-crt-string-l1-1-0.dll!strncmp", s_strncmp},
    {"api-ms-win-crt-string-l1-1-0.dll!_strnicmp", s_strnicmp},
    {"api-ms-win-crt-string-l1-1-0.dll!strncpy", s_strncpy},
    {"api-ms-win-crt-string-l1-1-0.dll!strcpy_s", s_strcpy_s},
    {"api-ms-win-crt-string-l1-1-0.dll!strncpy_s", s_strncpy_s},
    {"api-ms-win-crt-string-l1-1-0.dll!strcat_s", s_strcat_s},
    {"api-ms-win-crt-string-l1-1-0.dll!_strdup", s_strdup},
    {"api-ms-win-crt-string-l1-1-0.dll!tolower", s_tolower},
    {"api-ms-win-crt-string-l1-1-0.dll!toupper", s_toupper},
    {"api-ms-win-crt-string-l1-1-0.dll!isdigit", s_isdigit},
    {"api-ms-win-crt-string-l1-1-0.dll!isspace", s_isspace},
    {"api-ms-win-crt-string-l1-1-0.dll!ispunct", s_ispunct},
    {"api-ms-win-crt-convert-l1-1-0.dll!atoi", s_atoi},
    {"api-ms-win-crt-convert-l1-1-0.dll!atof", s_atof},
    {"api-ms-win-crt-convert-l1-1-0.dll!strtoull", s_strtoull},
    {"api-ms-win-crt-convert-l1-1-0.dll!mbstowcs_s", s_mbstowcs_s},
    {"api-ms-win-crt-convert-l1-1-0.dll!wcstombs_s", s_wcstombs_s},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_sin_precise", s_sin},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_cos_precise", s_cos},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_acos_precise", s_acos},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_asin_precise", s_asin},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_atan_precise", s_atan},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_exp_precise", s_exp},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_log_precise", s_log},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_log10_precise", s_log10},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_sqrt_precise", s_sqrt},
    {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_pow_precise", s_pow},
    {"api-ms-win-crt-math-l1-1-0.dll!_CIatan2", s_CIatan2},
    {"api-ms-win-crt-math-l1-1-0.dll!_CIfmod", s_CIfmod},
    {"api-ms-win-crt-math-l1-1-0.dll!ceil", s_ceil},
    {"api-ms-win-crt-math-l1-1-0.dll!floor", s_floor},
    {"api-ms-win-crt-math-l1-1-0.dll!ldexp", s_ldexp},
    {"api-ms-win-crt-math-l1-1-0.dll!modf", s_modf},
    {"api-ms-win-crt-math-l1-1-0.dll!roundf", s_roundf},
    {"api-ms-win-crt-math-l1-1-0.dll!copysignf", s_copysignf},
    {"api-ms-win-crt-math-l1-1-0.dll!_fdclass", s_fdclass},
    {"api-ms-win-crt-math-l1-1-0.dll!__setusermatherr", s_setusermatherr},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_controlfp_s", s_controlfp_s},
    {"api-ms-win-crt-runtime-l1-1-0.dll!__p___argc", s_p_argc},
    {"api-ms-win-crt-runtime-l1-1-0.dll!__p___argv", s_p_argv},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_configure_narrow_argv", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_initialize_narrow_environment", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_get_initial_narrow_environment", s_get_env},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_initialize_onexit_table", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_register_onexit_function", s_register_onexit},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_crt_atexit", s_crt_atexit},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_initterm", s_initterm},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_initterm_e", s_initterm_e},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_set_app_type", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_seh_filter_exe", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_register_thread_local_exe_atexit_callback", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_c_exit", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_cexit", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_errno", s_errno},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_set_errno", s_set_errno},
    {"api-ms-win-crt-runtime-l1-1-0.dll!exit", s_exit},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_exit", s_exit},
    {"api-ms-win-crt-runtime-l1-1-0.dll!abort", s_abort},
    {"api-ms-win-crt-runtime-l1-1-0.dll!terminate", s_terminate},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_invalid_parameter_noinfo_noreturn", s_invalid_param},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_wassert", s_wassert},
    {"api-ms-win-crt-runtime-l1-1-0.dll!perror", s_perror},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_beginthreadex", s_beginthreadex},
    {"api-ms-win-crt-environment-l1-1-0.dll!getenv", s_getenv},
    {"api-ms-win-crt-filesystem-l1-1-0.dll!_access", s_access},
    {"api-ms-win-crt-filesystem-l1-1-0.dll!remove", s_remove},
    {"api-ms-win-crt-locale-l1-1-0.dll!_configthreadlocale", s_ret0},
    {"api-ms-win-crt-utility-l1-1-0.dll!qsort", s_qsort},
    {"api-ms-win-crt-time-l1-1-0.dll!_time64", s_time64},
    {"api-ms-win-crt-time-l1-1-0.dll!_localtime64", s_localtime64},
    {"api-ms-win-crt-time-l1-1-0.dll!_gmtime64", s_gmtime64},
    {"api-ms-win-crt-time-l1-1-0.dll!_mkgmtime64", s_mkgmtime64},
    {"api-ms-win-crt-time-l1-1-0.dll!strftime", s_strftime},
};
}

void shims_crt_init() {
    // argv: "isaac-ng.exe"; stdin/stdout/stderr get guest handles of their own
    uint32_t name = guest_malloc(16);
    memcpy(G(name), "isaac-ng.exe", 13);
    g_argv = guest_calloc(2, 4);
    WR32(g_argv, name);
    uint32_t argc = guest_calloc(1, 4);
    WR32(argc, 1);
    g_argc = argc;
    uint32_t pargv = guest_calloc(1, 4);
    WR32(pargv, g_argv);
    g_argv = pargv;
    g_envp = guest_calloc(2, 4);
    for (int i = 0; i < 3; ++i) {
        g_stdio[i] = guest_calloc(1, 32);
        g_files[g_stdio[i]] = vf_wrap(i == 0 ? stdin : i == 1 ? stdout : stderr);
    }
}

VFile* crt_host_file(uint32_t g) { return host_file(g); }

// the host stream behind a guest FILE* (hle_vorbis.cpp: the game's decoder reads music through one)
VFile* shims_guest_file(uint32_t guestFile) { return host_file(guestFile); }

void shims_crt_register() { shim_register(kCrt, (int)(sizeof(kCrt) / sizeof(kCrt[0]))); }

void heap_profile_dump() {
    if (!g_heapProfile) return;
    std::map<std::string, std::pair<uint64_t, uint32_t> > sites;
    uint64_t total = 0;
    {
        std::lock_guard<std::mutex> lock(g_liveLock);
        for (std::map<uint32_t, std::pair<std::string, uint32_t> >::iterator it = g_live.begin(); it != g_live.end(); ++it) {
            sites[it->second.first].first += it->second.second;
            ++sites[it->second.first].second;
            total += it->second.second;
        }
    }
    std::vector<std::pair<uint64_t, std::string> > rows;
    for (std::map<std::string, std::pair<uint64_t, uint32_t> >::iterator it = sites.begin(); it != sites.end(); ++it)
        rows.push_back(std::make_pair(it->second.first, it->first + " x" + std::to_string(it->second.second)));
    std::sort(rows.rbegin(), rows.rend());
    FILE* f = fopen("build/recomp/heap_profile.txt", "w");
    if (!f) return;
    fprintf(f, "live %llu KB in %u blocks, heap %u MB\n", (unsigned long long)(total >> 10), (unsigned)g_live.size(),
            guest_heap_used() >> 20);
    for (size_t i = 0; i < rows.size() && i < 80; ++i)
        fprintf(f, "%9llu KB  %s\n", (unsigned long long)(rows[i].first >> 10), rows[i].second.c_str());
    fclose(f);
    recomp_log("heap profile written");
}
