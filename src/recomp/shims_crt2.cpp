// More UCRT/VCRUNTIME/KERNEL32 imports, mostly the recompiled Lua5.3.3r.dll's (cdecl unless stdcall noted).
#include "runtime.h"
#include "vfile.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <string>

VFile* crt_host_file(uint32_t g);                    // shims_crt.cpp

namespace {
#define CTYPE(name, fn) void name(X86* c) { RET_CDECL(c, (uint32_t)(fn((int)(ARG(c, 0) & 0xff)) ? 1 : 0)); }
CTYPE(s_isxdigit, isxdigit)
CTYPE(s_islower, islower)
CTYPE(s_iscntrl, iscntrl)
CTYPE(s_isalpha, isalpha)
CTYPE(s_isalnum, isalnum)
CTYPE(s_isupper, isupper)
CTYPE(s_isgraph, isgraph)

void s_strrchr(X86* c) {
    const char* p = strrchr((const char*)G(ARG(c, 0)), (int)(uint8_t)ARG(c, 1));
    RET_CDECL(c, p ? (uint32_t)((const uint8_t*)p - g_mem) : 0);
}
void s_strspn(X86* c) { RET_CDECL(c, (uint32_t)strspn((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)))); }
void s_strcoll(X86* c) { RET_CDECL(c, (uint32_t)strcmp((const char*)G(ARG(c, 0)), (const char*)G(ARG(c, 1)))); }
void s_strtod(X86* c) {
    const char* s = (const char*)G(ARG(c, 0));
    char* end = 0;
    double v = strtod(s, &end);
    if (ARG(c, 1)) WR32(ARG(c, 1), ARG(c, 0) + (uint32_t)(end - s));
    RET_ST0(c, v);
}
void s_strerror(X86* c) {
    static uint32_t buf = 0;
    if (!buf) buf = guest_malloc(128);
    guest_write_str(buf, strerror((int)ARG(c, 0)), 128);
    RET_CDECL(c, buf);
}
void s_frexp(X86* c) {
    int e = 0;
    double m = frexp(ARGD(c, 0), &e);
    WR32(ARG(c, 2), (uint32_t)e);
    RET_ST0(c, m);
}
void s_tan(X86* c) { c->x[0].d[0] = tan(c->x[0].d[0]); RET_CDECL(c, c->r[0]); }
void s_except1(X86* c) { recomp_fatal(c, "_except1 (floating point exception)"); }
uint32_t g_rand = 1;
void s_srand(X86* c) { g_rand = ARG(c, 0); RET_CDECL(c, 0); }
void s_rand(X86* c) {                                   // the MSVC CRT's generator
    g_rand = g_rand * 214013u + 2531011u;
    RET_CDECL(c, (g_rand >> 16) & 0x7fff);
}
void s_clock(X86* c) {
    static auto t0 = std::chrono::steady_clock::now();
    RET_CDECL(c, (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
}
void s_mktime64(X86* c) {
    uint32_t a = ARG(c, 0);
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_sec = (int)RD32(a); t.tm_min = (int)RD32(a + 4); t.tm_hour = (int)RD32(a + 8); t.tm_mday = (int)RD32(a + 12);
    t.tm_mon = (int)RD32(a + 16); t.tm_year = (int)RD32(a + 20); t.tm_isdst = (int)RD32(a + 32);
    RET_CDECL64(c, (uint64_t)mktime(&t));
}
void s_difftime64(X86* c) {
    int64_t a = (int64_t)((uint64_t)ARG(c, 0) | ((uint64_t)ARG(c, 1) << 32));
    int64_t b = (int64_t)((uint64_t)ARG(c, 2) | ((uint64_t)ARG(c, 3) << 32));
    RET_ST0(c, (double)(a - b));
}
uint32_t g_lconv = 0;
void s_localeconv(X86* c) {
    if (!g_lconv) {
        g_lconv = guest_calloc(1, 96);
        uint32_t dot = guest_malloc(2);
        memcpy(G(dot), ".", 2);
        WR32(g_lconv, dot);                             // decimal_point
        uint32_t empty = guest_calloc(1, 4);
        for (int i = 1; i < 10; ++i) WR32(g_lconv + 4 * i, empty);
    }
    RET_CDECL(c, g_lconv);
}
void s_setlocale(X86* c) {
    static uint32_t cname = 0;
    if (!cname) { cname = guest_malloc(2); memcpy(G(cname), "C", 2); }
    RET_CDECL(c, cname);
}
// stdio pieces Lua's io library uses
void s_fgets(X86* c) {
    VFile* f = crt_host_file(ARG(c, 2));
    if (!f) { RET_CDECL(c, 0); return; }
    char* r = vf_gets((char*)G(ARG(c, 0)), (int)ARG(c, 1), f);
    RET_CDECL(c, r ? ARG(c, 0) : 0);
}
void s_getc(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); RET_CDECL(c, f ? (uint32_t)vf_getc(f) : (uint32_t)EOF); }
void s_ungetc(X86* c) { VFile* f = crt_host_file(ARG(c, 1)); RET_CDECL(c, f ? (uint32_t)vf_ungetc((int)ARG(c, 0), f) : (uint32_t)EOF); }
void s_feof(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); RET_CDECL(c, f ? (uint32_t)vf_eof(f) : 1); }
void s_ferror(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); RET_CDECL(c, f ? (uint32_t)vf_error(f) : 1); }
void s_clearerr(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); if (f) vf_clearerr(f); RET_CDECL(c, 0); }
void s_setvbuf(X86* c) { RET_CDECL(c, 0); }
void s_fseeki64(X86* c) {
    VFile* f = crt_host_file(ARG(c, 0));
    int64_t off = (int64_t)((uint64_t)ARG(c, 1) | ((uint64_t)ARG(c, 2) << 32));
    RET_CDECL(c, f ? (uint32_t)vf_seek(f, off, (int)ARG(c, 3)) : (uint32_t)-1);
}
void s_ftelli64(X86* c) { VFile* f = crt_host_file(ARG(c, 0)); RET_CDECL64(c, f ? (uint64_t)vf_tell(f) : (uint64_t)-1); }
void s_refuse(X86* c) { recomp_log("CRT %s refused", current_thunk_name()); RET_CDECL(c, 0); }
void s_rename(X86* c) {
    vf_drain();
    RET_CDECL(c, (uint32_t)rename(recomp_host_path(guest_str(ARG(c, 0)), true).c_str(), recomp_host_path(guest_str(ARG(c, 1)), true).c_str()));
}
void s_execute_onexit(X86* c) { RET_CDECL(c, 0); }
void s_ret0c(X86* c) { RET_CDECL(c, 0); }
void s_except_handler4(X86* c) { recomp_fatal(c, "_except_handler4_common: an SEH exception reached a handler"); }

// KERNEL32 (stdcall)
void k_DisableThreadLibraryCalls(X86* c) { RET_STD(c, 1, 1); }
void k_GetModuleFileNameA(X86* c) {
    std::string p = recomp_config().gameDir + "/isaac-ng.exe";
    guest_write_str(ARG(c, 1), p, ARG(c, 2));
    RET_STD(c, 3, (uint32_t)p.size());
}
void k_LoadLibraryExA(X86* c) { recomp_log("LoadLibraryExA %s refused", guest_str(ARG(c, 0)).c_str()); RET_STD(c, 3, 0); }
void k_FormatMessageA(X86* c) { RET_STD(c, 7, 0); }

const ShimDef kCrt2[] = {
    {"api-ms-win-crt-string-l1-1-0.dll!isxdigit", s_isxdigit}, {"api-ms-win-crt-string-l1-1-0.dll!islower", s_islower},
    {"api-ms-win-crt-string-l1-1-0.dll!iscntrl", s_iscntrl}, {"api-ms-win-crt-string-l1-1-0.dll!isalpha", s_isalpha},
    {"api-ms-win-crt-string-l1-1-0.dll!isalnum", s_isalnum}, {"api-ms-win-crt-string-l1-1-0.dll!isupper", s_isupper},
    {"api-ms-win-crt-string-l1-1-0.dll!isgraph", s_isgraph}, {"vcruntime140.dll!strrchr", s_strrchr},
    {"api-ms-win-crt-string-l1-1-0.dll!strspn", s_strspn}, {"api-ms-win-crt-string-l1-1-0.dll!strcoll", s_strcoll},
    {"api-ms-win-crt-convert-l1-1-0.dll!strtod", s_strtod}, {"api-ms-win-crt-runtime-l1-1-0.dll!strerror", s_strerror},
    {"api-ms-win-crt-math-l1-1-0.dll!frexp", s_frexp}, {"api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_tan_precise", s_tan},
    {"api-ms-win-crt-math-l1-1-0.dll!_except1", s_except1}, {"api-ms-win-crt-utility-l1-1-0.dll!srand", s_srand},
    {"api-ms-win-crt-utility-l1-1-0.dll!rand", s_rand}, {"api-ms-win-crt-time-l1-1-0.dll!clock", s_clock},
    {"api-ms-win-crt-time-l1-1-0.dll!_mktime64", s_mktime64}, {"api-ms-win-crt-time-l1-1-0.dll!_difftime64", s_difftime64},
    {"api-ms-win-crt-locale-l1-1-0.dll!localeconv", s_localeconv}, {"api-ms-win-crt-locale-l1-1-0.dll!setlocale", s_setlocale},
    {"api-ms-win-crt-stdio-l1-1-0.dll!fgets", s_fgets}, {"api-ms-win-crt-stdio-l1-1-0.dll!getc", s_getc},
    {"api-ms-win-crt-stdio-l1-1-0.dll!ungetc", s_ungetc}, {"api-ms-win-crt-stdio-l1-1-0.dll!feof", s_feof},
    {"api-ms-win-crt-stdio-l1-1-0.dll!ferror", s_ferror}, {"api-ms-win-crt-stdio-l1-1-0.dll!clearerr", s_clearerr},
    {"api-ms-win-crt-stdio-l1-1-0.dll!setvbuf", s_setvbuf}, {"api-ms-win-crt-stdio-l1-1-0.dll!_fseeki64", s_fseeki64},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_ftelli64", s_ftelli64}, {"api-ms-win-crt-stdio-l1-1-0.dll!tmpfile", s_refuse},
    {"api-ms-win-crt-stdio-l1-1-0.dll!tmpnam", s_refuse}, {"api-ms-win-crt-stdio-l1-1-0.dll!_popen", s_refuse},
    {"api-ms-win-crt-stdio-l1-1-0.dll!_pclose", s_refuse}, {"api-ms-win-crt-stdio-l1-1-0.dll!freopen", s_refuse},
    {"api-ms-win-crt-runtime-l1-1-0.dll!system", s_refuse}, {"api-ms-win-crt-filesystem-l1-1-0.dll!rename", s_rename},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_execute_onexit_table", s_execute_onexit},
    {"api-ms-win-crt-runtime-l1-1-0.dll!_seh_filter_dll", s_ret0c},
    {"vcruntime140.dll!__std_type_info_destroy_list", s_ret0c},
    {"vcruntime140.dll!_except_handler4_common", s_except_handler4},
    {"kernel32.dll!DisableThreadLibraryCalls", k_DisableThreadLibraryCalls},
    {"kernel32.dll!GetModuleFileNameA", k_GetModuleFileNameA},
    {"kernel32.dll!LoadLibraryExA", k_LoadLibraryExA}, {"kernel32.dll!FormatMessageA", k_FormatMessageA},
};
}

void shims_crt2_register() { shim_register(kCrt2, (int)(sizeof(kCrt2) / sizeof(kCrt2[0]))); }
