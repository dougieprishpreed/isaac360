// Imports of the recompiled msvcp140.dll (docs/RECOMP_PLAN.md): synchronisation, the "C" locale's tables and
// queries, character conversions and the vcruntime helpers its stream and locale code use. stdcall for KERNEL32.
#include "runtime.h"
#include "vfile.h"
#include <chrono>
#include <condition_variable>
#include <ctype.h>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unordered_map>

VFile* crt_host_file(uint32_t g);

namespace {
// ------------------------------------------------------------------------------------------------ SRW locks, CVs
std::mutex g_mapLock;
std::unordered_map<uint32_t, std::mutex*> g_srw;
std::unordered_map<uint32_t, std::condition_variable_any*> g_cv;
std::mutex* srw(uint32_t a) {
    std::lock_guard<std::mutex> l(g_mapLock);
    std::mutex*& m = g_srw[a];
    if (!m) m = new std::mutex;
    return m;
}
std::condition_variable_any* cv(uint32_t a) {
    std::lock_guard<std::mutex> l(g_mapLock);
    std::condition_variable_any*& v = g_cv[a];
    if (!v) v = new std::condition_variable_any;
    return v;
}
void k_AcquireSRWLockExclusive(X86* c) { srw(ARG(c, 0))->lock(); RET_STD(c, 1, 0); }
void k_ReleaseSRWLockExclusive(X86* c) { srw(ARG(c, 0))->unlock(); RET_STD(c, 1, 0); }
void k_TryAcquireSRWLockExclusive(X86* c) { RET_STD(c, 1, srw(ARG(c, 0))->try_lock() ? 1 : 0); }
// SleepConditionVariableSRW(cv, lock, ms, flags): the lock is held exclusively by the caller
void k_SleepConditionVariableSRW(X86* c) {
    std::mutex* m = srw(ARG(c, 1));
    std::unique_lock<std::mutex> l(*m, std::adopt_lock);
    uint32_t ms = ARG(c, 2);
    bool ok = true;
    if (ms == 0xffffffffu) cv(ARG(c, 0))->wait(l);
    else ok = cv(ARG(c, 0))->wait_for(l, std::chrono::milliseconds(ms)) == std::cv_status::no_timeout;
    l.release();
    RET_STD(c, 4, ok ? 1 : 0);
}
void k_WakeAllConditionVariable(X86* c) { cv(ARG(c, 0))->notify_all(); RET_STD(c, 1, 0); }
void k_WakeConditionVariable(X86* c) { cv(ARG(c, 0))->notify_one(); RET_STD(c, 1, 0); }
// InitOnceExecuteOnce(once, fn, param, context): fn(once, param, context) -> BOOL, once per INIT_ONCE
std::unordered_map<uint32_t, bool> g_once;
std::recursive_mutex g_onceLock;
void k_InitOnceExecuteOnce(X86* c) {
    std::lock_guard<std::recursive_mutex> l(g_onceLock);
    uint32_t once = ARG(c, 0);
    if (g_once[once]) { RET_STD(c, 4, 1); return; }
    uint32_t args[3] = {once, ARG(c, 2), ARG(c, 3)};
    uint32_t ok = guest_call(c, ARG(c, 1), 3, args, true);
    if (ok) g_once[once] = true;
    RET_STD(c, 4, ok ? 1 : 0);
}
void k_InitializeCriticalSectionEx(X86* c) { RET_STD(c, 3, 1); }
void k_EncodePointer(X86* c) { RET_STD(c, 1, ARG(c, 0)); }
void k_DecodePointer(X86* c) { RET_STD(c, 1, ARG(c, 0)); }
void k_GetTickCount64(X86* c) {
    static auto t0 = std::chrono::steady_clock::now();
    uint64_t ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    c->r[2] = (uint32_t)(ms >> 32);
    RET_STD(c, 0, (uint32_t)ms);
}
void k_GetSystemTimePreciseAsFileTime(X86* c) {
    uint64_t t = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count() * 10 + 116444736000000000ull;
    WR64(ARG(c, 0), t);
    RET_STD(c, 1, 0);
}
void k_GetCurrentProcessorNumber(X86* c) { RET_STD(c, 0, 0); }
void k_FlushProcessWriteBuffers(X86* c) { RET_STD(c, 0, 0); }
void k_SwitchToThread(X86* c) { std::this_thread::yield(); RET_STD(c, 0, 1); }
void k_LocalFree(X86* c) { RET_STD(c, 1, 0); }
void k_RtlCaptureStackBackTrace(X86* c) { RET_STD(c, 4, 0); }
void k_AreFileApisANSI(X86* c) { RET_STD(c, 0, 1); }
void k_GetModuleHandleExW(X86* c) { if (ARG(c, 2)) WR32(ARG(c, 2), kGuestImageBase); RET_STD(c, 3, 1); }
void k_GetExitCodeThread(X86* c) { WR32(ARG(c, 1), shims_thread_exit_code(ARG(c, 0))); RET_STD(c, 2, 1); }
// CP_ACP / UTF-8 as Latin-1 / ASCII: the game's text is ASCII or UTF-8 handled by the game itself
void k_MultiByteToWideChar(X86* c) {
    uint32_t src = ARG(c, 2), dst = ARG(c, 4), cap = ARG(c, 5);
    int32_t len = (int32_t)ARG(c, 3);
    uint32_t n = len < 0 ? (uint32_t)strlen((const char*)G(src)) + 1 : (uint32_t)len;
    if (!cap) { RET_STD(c, 6, n); return; }
    uint32_t i = 0;
    for (; i < n && i < cap; ++i) WR16(dst + 2 * i, RD8(src + i));
    RET_STD(c, 6, i);
}
void k_WideCharToMultiByte(X86* c) {
    uint32_t src = ARG(c, 2), dst = ARG(c, 4), cap = ARG(c, 5);
    int32_t len = (int32_t)ARG(c, 3);
    uint32_t n = len;
    if (len < 0) {
        n = 0;
        while (RD16(src + 2 * n)) ++n;
        ++n;
    }
    if (!cap) { RET_STD(c, 8, n); return; }
    uint32_t i = 0;
    for (; i < n && i < cap; ++i) {
        uint16_t w = RD16(src + 2 * i);
        WR8(dst + i, w < 256 ? (uint8_t)w : '?');
    }
    RET_STD(c, 8, i);
}
void k_GetCPInfo(X86* c) {
    uint32_t info = ARG(c, 1);
    memset(G(info), 0, 20);
    WR32(info, 1);                                   // MaxCharSize
    WR8(info + 4, '?');                              // DefaultChar
    RET_STD(c, 2, 1);
}
// GetStringTypeW(CT_CTYPE1, src, count, out WORD*)
void k_GetStringTypeW(X86* c) {
    uint32_t src = ARG(c, 1), out = ARG(c, 3);
    int32_t n = (int32_t)ARG(c, 2);
    if (n < 0) { n = 0; while (RD16(src + 2 * n)) ++n; }
    for (int32_t i = 0; i < n; ++i) {
        uint16_t w = RD16(src + 2 * i);
        int ch = w < 128 ? w : 0;
        uint16_t t = 0;
        if (isupper(ch)) t |= 0x1;
        if (islower(ch)) t |= 0x2;
        if (isdigit(ch)) t |= 0x4;
        if (isspace(ch)) t |= 0x8;
        if (ispunct(ch)) t |= 0x10;
        if (iscntrl(ch)) t |= 0x20;
        if (ch == ' ' || ch == '\t') t |= 0x40;
        if (isxdigit(ch)) t |= 0x80;
        if (isalpha(ch)) t |= 0x100;
        WR16(out + 2 * i, t);
    }
    RET_STD(c, 4, 1);
}
// LCMapStringEx(locale, flags, src, srcLen, dst, dstLen, ...): lower/upper case maps
void k_LCMapStringEx(X86* c) {
    uint32_t flags = ARG(c, 1), src = ARG(c, 2), dst = ARG(c, 4), cap = ARG(c, 5);
    int32_t n = (int32_t)ARG(c, 3);
    if (n < 0) { n = 0; while (RD16(src + 2 * n)) ++n; ++n; }
    if (!cap) { RET_STD(c, 9, (uint32_t)n); return; }
    for (int32_t i = 0; i < n && (uint32_t)i < cap; ++i) {
        uint16_t w = RD16(src + 2 * i);
        if (w < 128) w = (uint16_t)((flags & 0x100) ? tolower(w) : (flags & 0x200) ? toupper(w) : w);
        WR16(dst + 2 * i, w);
    }
    RET_STD(c, 9, (uint32_t)n);
}
void k_CompareStringEx(X86* c) {
    std::u16string a, b;
    uint32_t s1 = ARG(c, 2), s2 = ARG(c, 4);
    int32_t n1 = (int32_t)ARG(c, 3), n2 = (int32_t)ARG(c, 5);
    for (int32_t i = 0; n1 < 0 ? RD16(s1 + 2 * i) != 0 : i < n1; ++i) a += (char16_t)RD16(s1 + 2 * i);
    for (int32_t i = 0; n2 < 0 ? RD16(s2 + 2 * i) != 0 : i < n2; ++i) b += (char16_t)RD16(s2 + 2 * i);
    RET_STD(c, 9, a < b ? 1 : a == b ? 2 : 3);       // CSTR_LESS_THAN / EQUAL / GREATER_THAN
}
void k_GetLocaleInfoEx(X86* c) {
    uint32_t dst = ARG(c, 2), cap = ARG(c, 3);
    const char* v = "";
    switch (ARG(c, 1) & 0xffff) {
    case 0x0e: v = "."; break;                       // LOCALE_SDECIMAL
    case 0x0f: v = ","; break;                       // LOCALE_STHOUSAND
    case 0x10: v = "3;0"; break;                     // LOCALE_SGROUPING
    default: v = ""; break;
    }
    uint32_t n = (uint32_t)strlen(v) + 1;
    if (cap) for (uint32_t i = 0; i < n && i < cap; ++i) WR16(dst + 2 * i, (uint8_t)v[i]);
    RET_STD(c, 4, n);
}

// ------------------------------------------------------------------------------------------------ CRT locale
uint32_t g_pctype = 0, g_localeNames = 0;
void s_pctype_func(X86* c) {
    if (!g_pctype) {
        g_pctype = guest_calloc(257, 2) + 2;        // indexable by -1 (EOF) as the CRT's table is
        for (int ch = 0; ch < 256; ++ch) {
            uint16_t t = 0;
            if (ch < 128) {
                if (isupper(ch)) t |= 0x1;
                if (islower(ch)) t |= 0x2;
                if (isdigit(ch)) t |= 0x4;
                if (isspace(ch)) t |= 0x8;
                if (ispunct(ch)) t |= 0x10;
                if (iscntrl(ch)) t |= 0x20;
                if (ch == ' ') t |= 0x40;
                if (isxdigit(ch)) t |= 0x80;
                if (isalpha(ch)) t |= 0x100;
            }
            WR16(g_pctype + 2 * ch, t);
        }
    }
    RET_CDECL(c, g_pctype);
}
// _wsetlocale(category, locale): always the "C" locale
void s_wsetlocale(X86* c) {
    static uint32_t cname = 0;
    if (!cname) {
        cname = guest_calloc(2, 2);
        WR16(cname, 'C');
    }
    RET_CDECL(c, cname);
}
void s_mb_cur_max(X86* c) { RET_CDECL(c, 1); }
void s_lc_codepage(X86* c) { RET_CDECL(c, 0); }
void s_lc_collate_cp(X86* c) { RET_CDECL(c, 0); }
void s_lc_locale_name(X86* c) {
    if (!g_localeNames) g_localeNames = guest_calloc(6, 4);
    RET_CDECL(c, g_localeNames);
}
void s_ret0(X86* c) { RET_CDECL(c, 0); }
void s_strcspn(X86* c) { RET_CDECL(c, (uint32_t)strcspn((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)))); }
void s_wcsnlen(X86* c) {
    uint32_t n = 0, max = ARG(c, 1), s = ARG(c, 0);
    while (n < max && RD16(s + 2 * n)) ++n;
    RET_CDECL(c, n);
}
void s_strtof(X86* c) {
    const char* s = (const char*)G(ARG(c, 0));
    char* end = 0;
    double v = strtof(s, &end);
    if (ARG(c, 1)) WR32(ARG(c, 1), ARG(c, 0) + (uint32_t)(end - s));
    RET_ST0(c, v);
}
// _get_stream_buffer_pointers(FILE*, char*** base, char*** ptr, int** count): fields of our guest FILE block,
// left empty so msvcp140's filebuf always goes through fgetc/fputc/fread/fwrite
void s_get_stream_buffer_pointers(X86* c) {
    uint32_t f = ARG(c, 0);
    if (ARG(c, 1)) WR32(ARG(c, 1), f + 16);
    if (ARG(c, 2)) WR32(ARG(c, 2), f + 20);
    if (ARG(c, 3)) WR32(ARG(c, 3), f + 24);
    RET_CDECL(c, 0);
}
void s_fputc(X86* c) { VFile* f = crt_host_file(ARG(c, 1)); RET_CDECL(c, f ? (uint32_t)vf_putc((int)ARG(c, 0), f) : (uint32_t)EOF); }
void s_fputs(X86* c) { VFile* f = crt_host_file(ARG(c, 1)); RET_CDECL(c, f ? (uint32_t)vf_puts((const char*)G(ARG(c, 0)), f) : (uint32_t)EOF); }
void s_fgetc(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); RET_CDECL(c, f ? (uint32_t)vf_getc(f) : (uint32_t)EOF); }
void s_copysign(X86* c) { RET_ST0(c, copysign(ARGD(c, 0), ARGD(c, 2))); }
void s_dsign(X86* c) { RET_CDECL(c, signbit(ARGD(c, 0)) ? 0x8000 : 0); }
void s_fdsign(X86* c) { RET_CDECL(c, signbit(ARGF(c, 0)) ? 0x8000 : 0); }
void s_dclass(X86* c) {
    double d = ARGD(c, 0);
    RET_CDECL(c, (uint32_t)(isnan(d) ? 2 : isinf(d) ? 1 : d == 0 ? 0 : fpclassify(d) == FP_SUBNORMAL ? -2 : -1));
}

// ------------------------------------------------------------------------------------------------ vcruntime
uint32_t g_zero = 0;
void v_uncaught(X86* c) { RET_CDECL(c, 0); }
void v_current_exception(X86* c) {
    if (!g_zero) g_zero = guest_calloc(1, 16);
    RET_CDECL(c, g_zero);
}
void v_purecall(X86* c) { recomp_fatal(c, "pure virtual function call"); }
void v_std_terminate(X86* c) { recomp_fatal(c, "std::terminate"); }
// __std_exception_copy(const __std_exception_data* from, __std_exception_data* to): {const char* what; bool doFree}
void v_exception_copy(X86* c) {
    uint32_t from = ARG(c, 0), to = ARG(c, 1);
    uint32_t what = RD32(from);
    if (what && RD8(from + 4)) {
        std::string s = guest_str(what);
        uint32_t p = guest_malloc((uint32_t)s.size() + 1);
        memcpy(G(p), s.c_str(), s.size() + 1);
        WR32(to, p);
        WR8(to + 4, 1);
    } else {
        WR32(to, what);
        WR8(to + 4, 0);
    }
    RET_CDECL(c, 0);
}
void v_exception_destroy(X86* c) {
    uint32_t d = ARG(c, 0);
    if (RD8(d + 4)) guest_free(RD32(d));
    WR32(d, 0);
    WR8(d + 4, 0);
    RET_CDECL(c, 0);
}
// __RTDynamicCast(object, vfDelta, srcType, destType, isReference): MSVC RTTI walk (as Wine's msvcrt does)
uint32_t rtti_this(uint32_t bcd, uint32_t object) {
    int32_t mdisp = (int32_t)RD32(bcd + 8), pdisp = (int32_t)RD32(bcd + 12), vdisp = (int32_t)RD32(bcd + 16);
    if (pdisp >= 0) {
        object += (uint32_t)pdisp;
        object += RD32(RD32(object) + (uint32_t)vdisp);
    }
    return object + (uint32_t)mdisp;
}
void v_RTDynamicCast(X86* c) {
    uint32_t in = ARG(c, 0), dest = ARG(c, 3);
    if (!in) { RET_CDECL(c, 0); return; }
    uint32_t vft = RD32(in - ARG(c, 1));
    uint32_t col = RD32(vft - 4);                          // CompleteObjectLocator
    uint32_t offset = RD32(col + 4), cdOffset = RD32(col + 8);
    uint32_t base = in - offset;
    if (cdOffset) base -= RD32(in - cdOffset);
    uint32_t chd = RD32(col + 16);                         // ClassHierarchyDescriptor
    uint32_t n = RD32(chd + 8), bca = RD32(chd + 12);
    std::string want = guest_str(dest + 8);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t bcd = RD32(bca + 4 * i);
        uint32_t td = RD32(bcd);
        if (td == dest || guest_str(td + 8) == want) {
            RET_CDECL(c, rtti_this(bcd, base));
            return;
        }
    }
    if (ARG(c, 4)) recomp_fatal(c, "dynamic_cast to %s failed on a reference (std::bad_cast)", want.c_str());
    RET_CDECL(c, 0);
}

void v_CxxThrowException(X86* c) {
    // a C++ throw: logged with the type's name from its ThrowInfo; catching needs unwinding (docs/RECOMP_PLAN.md)
    uint32_t info = ARG(c, 1);
    std::string type = "?";
    if (info) {
        uint32_t cta = RD32(info + 12);              // CatchableTypeArray
        if (cta && RD32(cta) > 0) {
            uint32_t ct = RD32(cta + 4);
            uint32_t td = RD32(ct + 4);               // TypeDescriptor
            type = guest_str(td + 8);
        }
    }
    recomp_fatal(c, "C++ exception thrown: %s", type.c_str());
}

const ShimDef kCrt3[] = {
    {"kernel32.dll!AcquireSRWLockExclusive", k_AcquireSRWLockExclusive}, {"kernel32.dll!ReleaseSRWLockExclusive", k_ReleaseSRWLockExclusive},
    {"kernel32.dll!TryAcquireSRWLockExclusive", k_TryAcquireSRWLockExclusive},
    {"kernel32.dll!SleepConditionVariableSRW", k_SleepConditionVariableSRW},
    {"kernel32.dll!WakeAllConditionVariable", k_WakeAllConditionVariable}, {"kernel32.dll!WakeConditionVariable", k_WakeConditionVariable},
    {"kernel32.dll!InitOnceExecuteOnce", k_InitOnceExecuteOnce}, {"kernel32.dll!InitializeCriticalSectionEx", k_InitializeCriticalSectionEx},
    {"kernel32.dll!EncodePointer", k_EncodePointer}, {"kernel32.dll!DecodePointer", k_DecodePointer},
    {"kernel32.dll!GetTickCount64", k_GetTickCount64}, {"kernel32.dll!GetSystemTimePreciseAsFileTime", k_GetSystemTimePreciseAsFileTime},
    {"kernel32.dll!GetCurrentProcessorNumber", k_GetCurrentProcessorNumber}, {"kernel32.dll!FlushProcessWriteBuffers", k_FlushProcessWriteBuffers},
    {"kernel32.dll!SwitchToThread", k_SwitchToThread}, {"kernel32.dll!LocalFree", k_LocalFree},
    {"kernel32.dll!RtlCaptureStackBackTrace", k_RtlCaptureStackBackTrace}, {"kernel32.dll!AreFileApisANSI", k_AreFileApisANSI},
    {"kernel32.dll!GetModuleHandleExW", k_GetModuleHandleExW}, {"kernel32.dll!GetExitCodeThread", k_GetExitCodeThread},
    {"kernel32.dll!MultiByteToWideChar", k_MultiByteToWideChar}, {"kernel32.dll!WideCharToMultiByte", k_WideCharToMultiByte},
    {"kernel32.dll!GetCPInfo", k_GetCPInfo}, {"kernel32.dll!GetStringTypeW", k_GetStringTypeW},
    {"kernel32.dll!LCMapStringEx", k_LCMapStringEx}, {"kernel32.dll!CompareStringEx", k_CompareStringEx},
    {"kernel32.dll!GetLocaleInfoEx", k_GetLocaleInfoEx},
    {"api-ms-win-crt-locale-l1-1-0.dll!__pctype_func", s_pctype_func},
    {"api-ms-win-crt-locale-l1-1-0.dll!___mb_cur_max_func", s_mb_cur_max},
    {"api-ms-win-crt-locale-l1-1-0.dll!_wsetlocale", s_wsetlocale},
    {"api-ms-win-crt-locale-l1-1-0.dll!___lc_codepage_func", s_lc_codepage},
    {"api-ms-win-crt-locale-l1-1-0.dll!___lc_collate_cp_func", s_lc_collate_cp},
    {"api-ms-win-crt-locale-l1-1-0.dll!___lc_locale_name_func", s_lc_locale_name},
    {"api-ms-win-crt-locale-l1-1-0.dll!_lock_locales", s_ret0}, {"api-ms-win-crt-locale-l1-1-0.dll!_unlock_locales", s_ret0},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_set_new_handler", s_ret0},
    {"api-ms-win-crt-filesystem-l1-1-0.dll!_lock_file", s_ret0}, {"api-ms-win-crt-filesystem-l1-1-0.dll!_unlock_file", s_ret0},
    {"api-ms-win-crt-string-l1-1-0.dll!strcspn", s_strcspn}, {"api-ms-win-crt-string-l1-1-0.dll!wcsnlen", s_wcsnlen},
    {"api-ms-win-crt-convert-l1-1-0.dll!strtof", s_strtof},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fputc", s_fputc},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_get_stream_buffer_pointers", s_get_stream_buffer_pointers}, {"api-ms-win-crt-stdio-l1-1-0.dll!fputs", s_fputs},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fgetc", s_fgetc},
    {"api-ms-win-crt-math-l1-1-0.dll!copysign", s_copysign}, {"api-ms-win-crt-math-l1-1-0.dll!_dsign", s_dsign},
    {"api-ms-win-crt-math-l1-1-0.dll!_fdsign", s_fdsign}, {"api-ms-win-crt-math-l1-1-0.dll!_dclass", s_dclass},
    {"vcruntime140.dll!__uncaught_exceptions", v_uncaught}, {"vcruntime140.dll!__uncaught_exception", v_uncaught},
    {"vcruntime140.dll!__current_exception", v_current_exception}, {"vcruntime140.dll!__current_exception_context", v_current_exception},
    {"vcruntime140.dll!_purecall", v_purecall}, {"vcruntime140.dll!__std_terminate", v_std_terminate},
    {"vcruntime140.dll!__std_exception_copy", v_exception_copy}, {"vcruntime140.dll!__std_exception_destroy", v_exception_destroy},
    {"vcruntime140.dll!_CxxThrowException", v_CxxThrowException},
    {"vcruntime140.dll!__RTDynamicCast", v_RTDynamicCast},
};
}

void shims_crt3_register() { shim_register(kCrt3, (int)(sizeof(kCrt3) / sizeof(kCrt3[0]))); }
