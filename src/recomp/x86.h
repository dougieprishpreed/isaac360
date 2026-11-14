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

/* Scalar SSE floats (translate.py's scalar float registers, Fn.sfo_flow): an XMM register a function uses only for
 * single floats keeps its lane 0 as an XS (its other lanes stay in the X86Xmm local). On the console an XS is a VMX
 * register holding the float in all four elements: guest memory's little-endian words load and store through
 * lvx/vperm and vperm/stvewx, which reverse bytes for nothing, where a float register reaches a byte-reversed word
 * only through a GPR and memory (~31 clocks a load, ~51 a store: xsdk docs/HARDWARE.md "Byte-reversed loads and
 * stores"; boot 21's bench: movss, addss, movss 53 ns). Elsewhere an XS is a float. Each operation gives x86's
 * result for every input but two: VMX takes denormal inputs and results as zero (its non-Java mode), and the NaN an
 * invalid operation makes is positive (x86's is negative). Division and square root go through a float register
 * (exact; they are rare). ISAAC360_NO_VMX builds the console's with floats too. */
#if defined(__ALTIVEC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ && !defined(ISAAC360_NO_VMX)
#include <altivec.h>
#undef vector
#undef pixel
#undef bool
#define XS_VMX 1
typedef __vector float XS;
typedef __vector unsigned int XSu;
typedef __vector unsigned char XSb;
#define XS_K(bits) ((XS)(XSu){(bits), (bits), (bits), (bits)})
static inline XS xs_ld(uint32_t a) {
    const unsigned char* p = g_mem + a;
    const XSb rev = {3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0};
    XSb lo = vec_ld(0, p), hi = vec_ld(3, p), sh = vec_lvsl(0, p);
    return (XS)vec_perm(lo, hi, vec_perm(sh, sh, rev));
}
static inline void xs_st(uint32_t a, XS v) {
    const XSb rev = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12};
    XSu w = (XSu)vec_perm((XSb)v, (XSb)v, rev);
    if (__builtin_expect(a & 3, 0)) {
        union { XSu v; uint32_t u[4]; } t;
        t.v = w;
        memcpy(g_mem + a, &t.u[0], 4);
        return;
    }
    vec_ste(w, 0, (unsigned int*)(g_mem + a));
}
/* lane 0 of an X86Xmm in host memory (the CPU state), as the u[X32(0)] element's address: a word on 4 bytes */
static inline XS xs_ldcpu(const uint32_t* p) {
    const XSb pick = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    XSb sh = vec_lvsl(0, (const unsigned char*)p);
    XSu w = vec_lde(0, (const unsigned int*)p);
    return (XS)vec_perm((XSb)w, (XSb)w, vec_perm(sh, sh, pick));
}
static inline void xs_stcpu(uint32_t* p, XS v) { vec_ste((XSu)v, 0, (unsigned int*)p); }
static inline uint32_t xs_bits(XS v) { union { XS v; uint32_t u[4]; } t; t.v = v; return t.u[0]; }
static inline XS xs_of_bits(uint32_t u) { union { uint32_t u[4]; XS v; } t; t.u[0] = t.u[1] = t.u[2] = t.u[3] = u; return t.v; }
static inline float xs_f(XS v) { union { XS v; float f[4]; } t; t.v = v; return t.f[0]; }
static inline XS xs_of_f(float f) { union { float f[4]; XS v; } t; t.f[0] = t.f[1] = t.f[2] = t.f[3] = f; return t.v; }
#define XS_ZERO() XS_K(0u)
#define XS_ADD(a, b) vec_add((a), (b))
#define XS_SUB(a, b) vec_sub((a), (b))
#define XS_MUL(a, b) vec_madd((a), (b), XS_K(0x80000000u))       /* a * b + -0: one rounding, x86's product */
#define XS_AND(a, b) ((XS)vec_and((XSu)(a), (XSu)(b)))
#define XS_OR(a, b) ((XS)vec_or((XSu)(a), (XSu)(b)))
#define XS_XOR(a, b) ((XS)vec_xor((XSu)(a), (XSu)(b)))
#define XS_ANDN(a, b) ((XS)vec_andc((XSu)(b), (XSu)(a)))         /* ~a & b, as andnps */
static inline XS xs_div(XS a, XS b) { return xs_of_f(xs_f(a) / xs_f(b)); }
static inline XS xs_sqrt(XS a) { return xs_of_f(sqrtf(xs_f(a))); }
static inline XS xs_min(XS a, XS b) { return vec_sel(b, a, vec_cmpgt(b, a)); }   /* a < b ? a : b (NaN: b) */
static inline XS xs_max(XS a, XS b) { return vec_sel(b, a, vec_cmpgt(a, b)); }   /* a > b ? a : b (NaN: b) */
static inline int xs_lt(XS a, XS b) { return vec_all_lt(a, b); }
static inline int xs_le(XS a, XS b) { return vec_all_le(a, b); }
static inline int xs_gt(XS a, XS b) { return vec_all_gt(a, b); }
static inline int xs_ge(XS a, XS b) { return vec_all_ge(a, b); }
static inline int xs_eq(XS a, XS b) { return vec_all_eq(a, b); }
static inline int xs_unord(XS a, XS b) { return !vec_all_eq(a, a) || !vec_all_eq(b, b); }
static inline XS xs_of_i32(int32_t i) {
    union { int32_t i[4]; __vector signed int v; } t;
    t.i[0] = t.i[1] = t.i[2] = t.i[3] = i;
    return vec_ctf(t.v, 0);
}
/* cvttss2si: toward zero; NaN and out of range give 0x80000000 (vctsxs saturates, so those are put back) */
static inline uint32_t xs_trunc_i32(XS v) {
    __vector signed int r = vec_cts(v, 0);
    __vector __bool int in = vec_and(vec_cmplt(v, XS_K(0x4f000000u)), vec_cmpge(v, XS_K(0xcf000000u)));
    r = vec_sel((__vector signed int)XS_K(0x80000000u), r, in);
    union { __vector signed int v; int32_t i[4]; } t;
    t.v = r;
    return (uint32_t)t.i[0];
}
/* cmpss's masks (imm & 7): eq, lt, le, unord, neq, nlt, nle, ord */
static inline XS xs_cmpmask(XS a, XS b, int imm) {
    XSu ord = vec_and((XSu)vec_cmpeq(a, a), (XSu)vec_cmpeq(b, b)), m;
    switch (imm & 3) {
    case 0: m = (XSu)vec_cmpeq(a, b); break;
    case 1: m = (XSu)vec_cmpgt(b, a); break;
    case 2: m = (XSu)vec_cmpge(b, a); break;
    default: m = vec_nor(ord, ord); break;
    }
    if (imm & 4) m = vec_nor(m, m);
    return (XS)m;
}
#else
typedef float XS;
#define XS_K(bits) U2F(bits)
static inline XS xs_ld(uint32_t a) { return RDF(a); }
static inline void xs_st(uint32_t a, XS v) { WRF(a, v); }
static inline XS xs_ldcpu(const uint32_t* p) { return U2F(*p); }
static inline void xs_stcpu(uint32_t* p, XS v) { *p = F2U(v); }
static inline uint32_t xs_bits(XS v) { return F2U(v); }
static inline XS xs_of_bits(uint32_t u) { return U2F(u); }
static inline float xs_f(XS v) { return v; }
static inline XS xs_of_f(float f) { return f; }
#define XS_ZERO() 0.0f
#define XS_ADD(a, b) ((a) + (b))
#define XS_SUB(a, b) ((a) - (b))
#define XS_MUL(a, b) ((a) * (b))
#define XS_AND(a, b) U2F(F2U(a) & F2U(b))
#define XS_OR(a, b) U2F(F2U(a) | F2U(b))
#define XS_XOR(a, b) U2F(F2U(a) ^ F2U(b))
#define XS_ANDN(a, b) U2F(~F2U(a) & F2U(b))
static inline XS xs_div(XS a, XS b) { return a / b; }
static inline XS xs_sqrt(XS a) { return sqrtf(a); }
static inline XS xs_min(XS a, XS b) { return a < b ? a : b; }
static inline XS xs_max(XS a, XS b) { return a > b ? a : b; }
static inline int xs_lt(XS a, XS b) { return a < b; }
static inline int xs_le(XS a, XS b) { return a <= b; }
static inline int xs_gt(XS a, XS b) { return a > b; }
static inline int xs_ge(XS a, XS b) { return a >= b; }
static inline int xs_eq(XS a, XS b) { return a == b; }
static inline int xs_unord(XS a, XS b) { return a != a || b != b; }
static inline XS xs_of_i32(int32_t i) { return (float)i; }
static inline uint32_t xs_trunc_i32(XS v) {             /* as x86_f2i_trunc */
    return (v != v || v >= 2147483648.0f || v < -2147483648.0f) ? 0x80000000u : (uint32_t)(int32_t)v;
}
static inline XS xs_cmpmask(XS a, XS b, int imm) {
    int r;
    switch (imm & 3) {
    case 0: r = a == b; break;
    case 1: r = a < b; break;
    case 2: r = a <= b; break;
    default: r = a != a || b != b; break;
    }
    if (imm & 4) r = !r;
    return U2F(r ? 0xffffffffu : 0u);
}
#endif

/* comiss/ucomiss fused with their consumer on X86Xmm floats (translate.py fuse_float_compare) */
static inline int x86_unord(float a, float b) { return a != a || b != b; }

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
/* the PC's ISAAC360_COUNT_CALLS=1: each translated function's entries on the game's main thread (runtime.cpp);
 * the console's translations count nothing */
#if defined(_WIN32)
extern int x86_counting;
void x86_count_enter(X86* c, uint32_t fn);
#define X86_ENTER(c, a) do { if (x86_counting) x86_count_enter(c, a); } while (0)
#endif
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
/* a float's bits widened to a double's in integer registers (translate.py's fld m32; fstp m64 pairs): exact, NaNs made
 * quiet as x87's load makes them; on the console a float register costs a trip through memory each way */
static inline uint64_t x86_f2d_bits(uint32_t f) {
    uint64_t sign = (uint64_t)(f & 0x80000000u) << 32;
    uint32_t e = (f >> 23) & 0xffu, m = f & 0x7fffffu;
    if (e == 0xffu) return sign | 0x7ff0000000000000ull | ((uint64_t)m << 29) | (m ? 0x0008000000000000ull : 0);
    if (e == 0) {
        if (!m) return sign;
        int p = 31 - __builtin_clz(m);                 /* the highest bit: the value is 1.x * 2^(p - 149) */
        return sign | ((uint64_t)(p - 149 + 1023) << 52) | ((uint64_t)((m << (23 - p)) & 0x7fffffu) << 29);
    }
    return sign | ((uint64_t)(e - 127 + 1023) << 52) | ((uint64_t)m << 29);
}
static inline double x86_x87_round(X86* c, double v) {
    switch ((c->fcw >> 10) & 3) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); }
}

typedef void (*X86Func)(X86* c);
typedef struct { uint32_t addr; X86Func fn; } X86FuncEntry;
extern const X86FuncEntry x86_functions[];
extern const uint32_t x86_function_count;
/* An indirect call site's own cache (translate.py): the function its last target was. A virtual call site mostly
 * calls one function, and the shared lookup (x86_call: a hash, then the table entry) took 57 ns on the console.
 * Threads may race on a site's pointer; each value it can hold is a whole, unchanging table entry. */
void x86_call_site(X86* c, uint32_t target, const X86FuncEntry** site);
#if defined(_WIN32)
#define X86_ICALL_COUNT(c) do { if (x86_counting) x86_count_icall(c); } while (0)
void x86_count_icall(X86* c);
#else
#define X86_ICALL_COUNT(c) ((void)0)
#endif
#define X86_ICALL(c, t) do { static const X86FuncEntry* ic_; const X86FuncEntry* e_ = ic_; \
    if (e_ && e_->addr == (t)) { X86_ICALL_COUNT(c); e_->fn(c); } else x86_call_site((c), (t), &ic_); } while (0)

#ifdef __cplusplus
}
#endif
#endif
