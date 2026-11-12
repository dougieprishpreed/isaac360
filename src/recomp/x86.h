/* Guest CPU model for the recompiled isaac-ng.exe (docs/RECOMP_PLAN.md).
 * Generated functions (build/recomp/out) include this. Guest memory is little-endian at g_mem + address; the
 * console (big-endian) byte-swaps on each access, the PC reads directly. */
#ifndef ISAAC360_X86_H
#define ISAAC360_X86_H
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    float f[4];
    double d[2];
    uint32_t u[4];
    int32_t i[4];
    uint64_t q[2];
    int64_t sq[2];
    uint16_t w[8];
    int16_t sw[8];
    uint8_t b[16];
    int8_t sb[16];
} X86Xmm;

typedef struct X86 {
    uint32_t r[8];                 /* eax ecx edx ebx esp ebp esi edi */
    uint8_t cf, pf, af, zf, sf, of, df, pad;
    double st[8];                  /* x87 stack, st[(top + i) & 7] is ST(i) */
    uint32_t top;
    uint16_t fcw, fsw;
    X86Xmm x[8];
    uint32_t mxcsr;
    uint32_t fs_base;              /* guest address of this thread's TIB */
    uint32_t last_eip;             /* the guest address being run when something stops */
    void* host_thread;
} X86;

extern uint8_t* g_mem;
extern uint32_t g_mem_size;

/* An X86Xmm holds its two 64-bit halves as native integers (RDX/WRX); a narrower lane i of the guest register is
 * element X32(i) / X16(i) / X8(i) of the union (translate.py writes every lane access this way). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define X86_BSWAP16(v) __builtin_bswap16(v)
#define X86_BSWAP32(v) __builtin_bswap32(v)
#define X86_BSWAP64(v) __builtin_bswap64(v)
#define X32(i) ((i) ^ 1)
#define X16(i) ((i) ^ 3)
#define X8(i) ((i) ^ 7)
#else
#define X86_BSWAP16(v) (v)
#define X86_BSWAP32(v) (v)
#define X86_BSWAP64(v) (v)
#define X32(i) (i)
#define X16(i) (i)
#define X8(i) (i)
#endif

static inline uint8_t RD8(uint32_t a) { return g_mem[a]; }
static inline uint16_t RD16(uint32_t a) { uint16_t v; memcpy(&v, g_mem + a, 2); return X86_BSWAP16(v); }
static inline uint32_t RD32(uint32_t a) { uint32_t v; memcpy(&v, g_mem + a, 4); return X86_BSWAP32(v); }
static inline uint64_t RD64(uint32_t a) { uint64_t v; memcpy(&v, g_mem + a, 8); return X86_BSWAP64(v); }
static inline void WR8(uint32_t a, uint8_t v) { g_mem[a] = v; }
static inline void WR16(uint32_t a, uint16_t v) { v = X86_BSWAP16(v); memcpy(g_mem + a, &v, 2); }
static inline void WR32(uint32_t a, uint32_t v) { v = X86_BSWAP32(v); memcpy(g_mem + a, &v, 4); }
static inline void WR64(uint32_t a, uint64_t v) { v = X86_BSWAP64(v); memcpy(g_mem + a, &v, 8); }
static inline float RDF(uint32_t a) { uint32_t u = RD32(a); float f; memcpy(&f, &u, 4); return f; }
static inline double RDD(uint32_t a) { uint64_t u = RD64(a); double d; memcpy(&d, &u, 8); return d; }
static inline void WRF(uint32_t a, float f) { uint32_t u; memcpy(&u, &f, 4); WR32(a, u); }
static inline void WRD(uint32_t a, double d) { uint64_t u; memcpy(&u, &d, 8); WR64(a, u); }
static inline void RDX(uint32_t a, X86Xmm* x) { x->q[0] = RD64(a); x->q[1] = RD64(a + 8); }
static inline void WRX(uint32_t a, const X86Xmm* x) { WR64(a, x->q[0]); WR64(a + 8, x->q[1]); }
static inline float U2F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t F2U(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline double Q2D(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t D2Q(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static inline uint8_t X86_PARITY(uint32_t v) { return (uint8_t)(!__builtin_parity(v & 0xff)); }

/* x87 80-bit extended <-> double (the stack holds doubles) */
static inline void WRF80(uint32_t a, double d) {
    uint64_t u = D2Q(d);
    uint32_t sign = (uint32_t)(u >> 63), exp = (uint32_t)((u >> 52) & 0x7ff);
    uint64_t frac = u & 0xfffffffffffffull, mant;
    uint32_t e80;
    if (exp == 0 && frac == 0) { mant = 0; e80 = 0; }
    else if (exp == 0x7ff) { mant = 0x8000000000000000ull | (frac << 11); e80 = 0x7fff; }
    else if (exp == 0) {                                   /* subnormal double: normalise */
        int shift = 0;
        while (!(frac & 0x10000000000000ull)) { frac <<= 1; ++shift; }
        mant = frac << 11;
        e80 = (uint32_t)(1 - 1023 + 16383 - shift);
    } else { mant = 0x8000000000000000ull | (frac << 11); e80 = exp - 1023 + 16383; }
    WR64(a, mant);
    WR16(a + 8, (uint16_t)((sign << 15) | e80));
}
static inline double RDF80(uint32_t a) {
    uint64_t mant = RD64(a);
    uint16_t se = RD16(a + 8);
    int sign = se >> 15, e = se & 0x7fff;
    if (e == 0 && mant == 0) return sign ? -0.0 : 0.0;
    if (e == 0x7fff) return (mant << 1) ? NAN : (sign ? -INFINITY : INFINITY);
    double v = ldexp((double)mant, e - 16383 - 63);
    return sign ? -v : v;
}

/* x87 */
#define ST(i) c->st[(c->top + (i)) & 7]
static inline void X87_PUSH(X86* c, double v) { c->top = (c->top - 1) & 7; c->st[c->top] = v; }
static inline double X87_POP(X86* c) { double v = c->st[c->top]; c->top = (c->top + 1) & 7; return v; }

/* runtime services (src/recomp/runtime.cpp) */
void x86_call(X86* c, uint32_t target);                  /* indirect call/jump to a guest function */
void x86_unimpl(X86* c, uint32_t eip, const char* what) __attribute__((noreturn));
void x86_bad_jump(X86* c, uint32_t eip, uint32_t target) __attribute__((noreturn));
void x86_unreachable(X86* c, uint32_t eip) __attribute__((noreturn));
void x86_import(X86* c, uint32_t slot);                  /* call through an IAT slot */
/* ISAAC360_REGCHECK translations: a function returned with a register it is taken to preserve changed (regflow.py) */
void x86_regcheck(X86* c, uint32_t fn, uint32_t eip, int reg, uint32_t entry, uint32_t now);
/* ISAAC360_REGCHECK=1 translations: after a call, esp outside the thread's stack (the call at eip came back wrong) */
void x86_espcheck(X86* c, uint32_t esp, uint32_t eip);
void x86_cpuid(X86* c);
/* around each lock-prefixed instruction: one lock for all guest atomics, with full barriers */
void x86_lock(void);
void x86_unlock(void);
void x86_trace_enter(X86* c, uint32_t fn);         /* ISAAC360_TRACE_FUNCS builds */
void x86_trace_leave(X86* c, uint32_t fn, uint32_t eax);
uint32_t x86_div_check(X86* c, uint32_t eip);
/* float -> int32 as x86 converts: out of range and NaN give 0x80000000 */
static inline uint32_t x86_f2i_trunc(double v) { return (v != v || v >= 2147483648.0 || v < -2147483648.0) ? 0x80000000u : (uint32_t)(int32_t)v; }
static inline uint32_t x86_f2i_round(double v) { return (v != v || v >= 2147483647.5 || v < -2147483648.5) ? 0x80000000u : (uint32_t)(int32_t)nearbyint(v); }
/* fist/fistp/fisttp of an already rounded value: out of range or NaN stores x86's "integer indefinite" (C's cast
 * is undefined there, and PowerPC saturates) */
static inline uint16_t x86_fist16(double v) { return (v != v || v >= 32768.0 || v < -32768.0) ? 0x8000u : (uint16_t)(int16_t)v; }
static inline uint32_t x86_fist32(double v) { return (v != v || v >= 2147483648.0 || v < -2147483648.0) ? 0x80000000u : (uint32_t)(int32_t)v; }
static inline uint64_t x86_fist64(double v) {
    return (v != v || v >= 9223372036854775808.0 || v < -9223372036854775808.0) ? 0x8000000000000000ull : (uint64_t)(int64_t)v;
}
static inline double x86_x87_round(X86* c, double v) {
    switch ((c->fcw >> 10) & 3) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); }
}

typedef void (*X86Func)(X86* c);
typedef struct { uint32_t addr; X86Func fn; } X86FuncEntry;
extern const X86FuncEntry x86_functions[];
extern const uint32_t x86_function_count;

#ifdef __cplusplus
}
#endif
#endif
