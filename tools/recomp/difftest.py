"""Instruction-level differential test of the translator against Unicorn (docs/RECOMP_PLAN.md).

Samples every distinct instruction form (iced Code + operand kinds) from the translated images, emits each one
with the translator's own emitter (all flags live, no compare fusion) as a C test function, compiles them into a
test DLL (clang-cl, x64) and runs each on random register/flag/xmm/memory states, comparing registers, the flags
x86 defines for that instruction, xmm registers and the memory around its operands with Unicorn executing the
same instruction bytes. Only single instructions run, never the game.

    tools/.venv/Scripts/python tools/recomp/difftest.py [--per-form 3] [--states 6] [--only MNEMONIC,...]
Writes build/recomp/difftest/report.txt; prints the failing forms.
"""
import argparse
import ctypes
import json
import os
import random
import struct
import subprocess
import sys
from collections import defaultdict

import iced_x86 as ix
import unicorn as uc
from unicorn import x86_const as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translate as T  # noqa: E402

ROOT = T.ROOT
OUTD = os.path.join(ROOT, 'build', 'recomp', 'difftest')
WINDOW = 16 << 20
REGS = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
UREGS = [X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX, X.UC_X86_REG_ESP, X.UC_X86_REG_EBP,
         X.UC_X86_REG_ESI, X.UC_X86_REG_EDI]
UXMM = [getattr(X, 'UC_X86_REG_XMM%d' % i) for i in range(8)]
FLAGBITS = [(T.CF, 0), (T.PF, 2), (T.AF, 4), (T.ZF, 6), (T.SF, 7), (T.OF, 11)]

SKIP_FLOW = {ix.FlowControl.CALL, ix.FlowControl.INDIRECT_CALL, ix.FlowControl.RETURN, ix.FlowControl.UNCONDITIONAL_BRANCH,
             ix.FlowControl.INDIRECT_BRANCH, ix.FlowControl.CONDITIONAL_BRANCH, ix.FlowControl.INTERRUPT,
             ix.FlowControl.EXCEPTION}


def collect(tr, per_form, only):
    forms = defaultdict(list)
    seen = set()
    for start in sorted(tr.funcs):
        fn = T.Fn(tr, start)
        fn.discover()
        for va, ins in fn.insns.items():
            if ins is None or va in seen:
                continue
            seen.add(va)
            if ins.flow_control in SKIP_FLOW:
                continue
            name = T.MNAME[ins.mnemonic]
            if only and name not in only:
                continue
            if name.startswith('V') or name in ('CPUID', 'RDTSC', 'XGETBV', 'INT', 'IN', 'OUT') or ins.memory_segment in (ix.Register.FS, ix.Register.GS):
                continue
            if ins.has_rep_prefix or ins.has_repne_prefix:
                continue
            if any(ins.op_kind(k) == ix.OpKind.REGISTER and T.RNAME[ins.op_register(k)].startswith(('MM', 'ST', 'ES', 'CS', 'SS', 'DS', 'FS', 'GS', 'CR', 'DR'))
                   for k in range(ins.op_count)):
                continue
            if name.startswith('F'):                     # x87: tested separately
                continue
            key = (ins.code, tuple(ins.op_kind(k) for k in range(ins.op_count)))
            if len(forms[key]) < per_form:
                forms[key].append((va, ins))
    return forms


def collect_pairs(tr, per_form):
    """compare/test + Jcc/SETcc/CMOVcc pairs the translator fuses, a few per (compare form, consumer)."""
    forms = defaultdict(list)
    for start in sorted(tr.funcs):
        fn = T.Fn(tr, start)
        fn.discover()
        fn.liveness()
        fn.find_fusions()
        for va in fn.fusable:
            a = fn.insns[va]
            b = fn.insns[va + a.len]
            if a.memory_segment in (ix.Register.FS, ix.Register.GS):
                continue
            key = (a.code, tuple(a.op_kind(k) for k in range(a.op_count)), b.mnemonic)
            if len(forms[key]) < per_form:
                forms[key].append((va, a, b))
    return forms


def xmm_wrap(fn, body, xs_init):
    """The XMM locals around translated code (translate.py keeps them per function: xmm_flow, sse_scalar), loaded
    from and stored back to the CPU state; xs_init: lane 0 starts in both x_N and xs_N (else in x_N only)."""
    pre = ''.join('X86Xmm x_%d=c->x[%d];XS xs_%d=%s;' % (n, n, n, 'xs_ldcpu(&c->x[%d].u[0])' % n if xs_init else 'XS_ZERO()')
                  for n in range(8))
    post = fn.x_flush(0xff) + ''.join('c->x[%d]=x_%d;' % (n, n) for n in range(8))
    return T.lanes('{%s\n%s\n%s}' % (pre, body, post))


def scalar_form(ins):
    """True if translate.py's sse_scalar may take this instruction (worth a second run with lane 0 in xs_N)."""
    return ins.mnemonic in T.Fn.SFO_FORMS

class Pair(T.Fn):
    """Two instructions, the first fused into the second; a taken branch leaves through x86_unreachable."""

    def __init__(self, tr, va, a, b, xs_init=False):
        super().__init__(tr, va)
        vb = va + a.len
        self.insns = {va: a, vb: b}
        self.order = [va, vb]
        self.succ = {va: [vb], vb: [vb + b.len]}
        self.live_out = {va: T.ALLF, vb: 0}
        self.fusable = set()
        self.fused = {}
        self.labels = set()
        self.xs_init = xs_init
        self.xst = [self.XB if xs_init else self.XC] * 8
        self.no_fold = True

    def code(self):
        self.liveness_fixed = True
        self.find_fusions()
        out = []
        for v in self.order:
            self.out = []
            self.translate_insn(v, self.insns[v], v + self.insns[v].len)
            out.append(''.join(self.out))
        return xmm_wrap(self, '\n'.join(out), self.xs_init), bool(self.fusable)

    def arith_follows(self, va, n):
        return self.xs_init                            # a movss load goes to xs_N in the second run


class Unit(T.Fn):
    """A one-instruction function: all flags live after it, no fusion."""

    def __init__(self, tr, va, ins, xs_init=False):
        super().__init__(tr, va)
        self.insns = {va: ins}
        self.order = [va]
        self.succ = {va: [va + ins.len]}
        self.live_out = {va: T.ALLF}
        self.fusable = set()
        self.fused = {}
        self.xs_init = xs_init
        self.xst = [self.XB if xs_init else self.XC] * 8
        self.no_fold = True

    def code(self):
        self.out = []
        self.translate_insn(self.start, self.insns[self.start], self.start + self.insns[self.start].len)
        return xmm_wrap(self, ''.join(self.out), self.xs_init)

    def arith_follows(self, va, n):
        return self.xs_init                            # a movss load goes to xs_N in the second run


HARNESS = r'''
#include "x86.h"
#include <setjmp.h>
#include <stdio.h>
uint8_t* g_mem; uint32_t g_mem_size;
static jmp_buf g_jb; static char g_why[128];
void x86_call(X86* c, uint32_t t) { (void)c; snprintf(g_why, 128, "call %08x", t); longjmp(g_jb, 1); }
void x86_import(X86* c, uint32_t s) { (void)c; snprintf(g_why, 128, "import %08x", s); longjmp(g_jb, 1); }
void x86_unimpl(X86* c, uint32_t e, const char* w) { (void)c; (void)e; snprintf(g_why, 128, "unimpl %s", w); longjmp(g_jb, 1); }
void x86_bad_jump(X86* c, uint32_t e, uint32_t t) { (void)c; (void)e; (void)t; snprintf(g_why, 128, "badjump"); longjmp(g_jb, 1); }
void x86_unreachable(X86* c, uint32_t e) { (void)c; (void)e; snprintf(g_why, 128, "unreachable"); longjmp(g_jb, 1); }
void x86_cpuid(X86* c) { (void)c; }
uint32_t x86_div_check(X86* c, uint32_t e) { (void)c; return e; }
const X86FuncEntry x86_functions[1]; const uint32_t x86_function_count = 0;
'''


def build(units, tmpdir):
    os.makedirs(tmpdir, exist_ok=True)
    src = [T.PRELUDE, HARNESS]
    names = []
    for i, (va, ins, code) in enumerate(units):
        n = 'T_%d' % i
        names.append(n)
        src.append('static void %s(X86* c){REGS();%s\nSPILL();}\n' % (n, code))
    src.append('typedef void (*TF)(X86*);\nstatic const TF g_tests[] = {%s};\n' % ','.join(names))
    src.append(r'''
__declspec(dllexport) int run_test(int i, X86* c, uint8_t* mem, char* why) {
    g_mem = mem; g_mem_size = 16u << 20; g_why[0] = 0;
    if (setjmp(g_jb)) { for (int k = 0; k < 128; ++k) why[k] = g_why[k]; return 1; }
    __try { g_tests[i](c); } __except (1) { return 2; }
    return 0;
}
__declspec(dllexport) int ctx_size(void) { return (int)sizeof(X86); }
''')
    path = os.path.join(tmpdir, 'tests.c')
    open(path, 'w', newline='\n').write('\n'.join(src))
    dll = os.path.join(tmpdir, 'tests.dll')
    cl = r'C:\Program Files\LLVM\bin\clang-cl.exe'
    vc = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
    bat = os.path.join(tmpdir, 'build.cmd')
    with open(bat, 'w') as f:
        f.write('@echo off\ncall "%s" x64 >nul 2>nul\n"%s" /nologo /LD /O1 /w -fno-strict-aliasing /I"%s" "%s" /Fe"%s" /Fo"%s"\n' % (
            vc, cl, os.path.join(ROOT, 'src', 'recomp'), path, dll, os.path.join(tmpdir, 'tests.obj')))
    r = subprocess.run(['cmd', '/d', '/c', bat], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-4000:], r.stderr[-2000:])
        raise SystemExit('test DLL build failed')
    return dll


class Ctx(ctypes.Structure):
    _fields_ = [('r', ctypes.c_uint32 * 8), ('cf', ctypes.c_uint8), ('pf', ctypes.c_uint8), ('af', ctypes.c_uint8),
                ('zf', ctypes.c_uint8), ('sf', ctypes.c_uint8), ('of', ctypes.c_uint8), ('df', ctypes.c_uint8),
                ('pad', ctypes.c_uint8), ('st', ctypes.c_double * 8), ('top', ctypes.c_uint32), ('fcw', ctypes.c_uint16),
                ('fsw', ctypes.c_uint16), ('x', (ctypes.c_uint8 * 16) * 8), ('mxcsr', ctypes.c_uint32),
                ('fs_base', ctypes.c_uint32), ('last_eip', ctypes.c_uint32), ('host_thread', ctypes.c_void_p)]


def ea_regs(ins, rng):
    """Register values that put the memory operand (and esp, esi, edi) inside the test window."""
    vals = [rng.getrandbits(32) for _ in range(8)]
    vals[4] = 0x00800000 + (rng.randrange(0, 0x10000) & ~3)        # esp
    vals[6] = 0x00900000 + rng.randrange(0, 0x1000)                 # esi
    vals[7] = 0x00a00000 + rng.randrange(0, 0x1000)                 # edi
    mem = ins.op_count and any(ins.op_kind(k) == ix.OpKind.MEMORY for k in range(ins.op_count))
    if mem:
        target = 0x00b00000 + rng.randrange(0, 0x10000)
        b, x, s, d = ins.memory_base, ins.memory_index, ins.memory_index_scale, ins.memory_displacement & 0xffffffff
        bi = REGS.index(T.GPR32[b]) if b in T.GPR32 else None
        xi = REGS.index(T.GPR32[x]) if x in T.GPR32 else None
        if bi is None and xi is None:
            target = d
        elif bi is not None and xi is not None and bi == xi:
            vals[bi] = ((target - d) // (1 + s)) & 0xffffffff
        else:
            if xi is not None:
                if xi == 4:
                    pass
                else:
                    vals[xi] = rng.randrange(0, 64)
            if bi is not None:
                idx = vals[xi] * s if xi is not None else 0
                if bi == 4:                                 # esp-based: keep esp, move nothing else
                    pass
                else:
                    vals[bi] = (target - d - idx) & 0xffffffff
    return vals


def regions(vals, ins):
    out = [(vals[4] - 64, 192), (vals[6] - 64, 192), (vals[7] - 64, 192)]
    if ins.op_count and any(ins.op_kind(k) == ix.OpKind.MEMORY for k in range(ins.op_count)):
        b, x, s, d = ins.memory_base, ins.memory_index, ins.memory_index_scale, ins.memory_displacement & 0xffffffff
        ea = d
        if b in T.GPR32: ea += vals[REGS.index(T.GPR32[b])]
        if x in T.GPR32: ea += vals[REGS.index(T.GPR32[x])] * s
        ea &= 0xffffffff
        out.append((ea - 64, 192))
    return [(a & 0xffffffff, n) for a, n in out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--per-form', type=int, default=2)
    ap.add_argument('--states', type=int, default=6)
    ap.add_argument('--only', default='')
    ap.add_argument('--pairs', action='store_true', help='fused compare + consumer pairs')
    args = ap.parse_args()
    only = set(args.only.split(',')) if args.only else None
    tr = T.Translator()
    units = []
    sfo_units = 0
    if args.pairs:
        forms = collect_pairs(tr, args.per_form)
        for key, samples in forms.items():
            for va, a, b in samples:
                # each pair as translated with X86Xmm registers and, for float compares, with scalar float ones
                for xs in (False, True) if a.mnemonic in (T.M.COMISS, T.M.UCOMISS) else (False,):
                    try:
                        code, fused = Pair(tr, va, a, b, xs).code()
                    except T.Unhandled:
                        continue
                    if fused and 'x86_unimpl' not in code:
                        units.append((va, (a, b), code))
                        sfo_units += xs
    else:
        forms = collect(tr, args.per_form, only)
        for key, samples in forms.items():
            for va, ins in samples:
                # each instruction as translated with X86Xmm registers and, where its form allows, scalar float ones
                for xs in (False, True) if scalar_form(ins) else (False,):
                    try:
                        code = Unit(tr, va, ins, xs).code()
                    except T.Unhandled:
                        continue
                    if 'x86_unimpl' in code:
                        continue
                    units.append((va, ins, code))
                    sfo_units += xs
    print('%d with lane 0 starting in xs_N' % sfo_units)
    print('%d forms, %d samples' % (len(forms), len(units)))
    dll = ctypes.CDLL(build(units, OUTD))
    dll.run_test.argtypes = [ctypes.c_int, ctypes.POINTER(Ctx), ctypes.c_void_p, ctypes.c_char_p]
    assert dll.ctx_size() == ctypes.sizeof(Ctx), (dll.ctx_size(), ctypes.sizeof(Ctx))
    mem = ctypes.create_string_buffer(WINDOW)
    rng = random.Random(1234)
    emu = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
    emu.mem_map(0, WINDOW)
    img = tr.img.data
    failures = defaultdict(list)
    for i, (va, ins, code) in enumerate(units):
        pair = isinstance(ins, tuple)
        first = ins[0] if pair else ins
        length = (ins[0].len + ins[1].len) if pair else ins.len
        ibytes = img[va - tr.img.base:va - tr.img.base + length]
        defined = 0 if pair else T.rf_to_mask(ins.rflags_modified) & ~T.rf_to_mask(ins.rflags_undefined)
        ins = first
        for st in range(args.states):
            vals = ea_regs(ins, rng)
            flags = rng.getrandbits(6)
            xmm = [rng.getrandbits(128) for _ in range(8)]
            # identical memory in the regions the instruction can touch
            regs_ = regions(vals, ins)
            blobs = [(a, bytes(rng.getrandbits(8) for _ in range(n))) for a, n in regs_]
            code_at = 0x00c00000
            for a, b in blobs:
                if a + len(b) <= WINDOW:
                    ctypes.memmove(ctypes.addressof(mem) + a, b, len(b))
                    emu.mem_write(a, b)
            emu.mem_write(code_at, bytes(ibytes))
            for k in range(8):
                emu.reg_write(UREGS[k], vals[k])
                emu.reg_write(UXMM[k], xmm[k])
            ef = 0x2
            for bit, (m, sh) in enumerate(FLAGBITS):
                if flags >> bit & 1:
                    ef |= 1 << sh
            emu.reg_write(X.UC_X86_REG_EFLAGS, ef)
            # relative operands (none for these forms) would differ: the bytes run at code_at
            try:
                emu.emu_start(code_at, code_at + length, count=2 if pair else 1)
                uerr = None
            except uc.UcError as e:
                uerr = str(e)
            ctx = Ctx()
            for k in range(8):
                ctx.r[k] = vals[k]
            for bit, (m, sh) in enumerate(FLAGBITS):
                setattr(ctx, T.FLAGNAME[m], (flags >> bit) & 1)
            for k in range(8):
                ctypes.memmove(ctx.x[k], xmm[k].to_bytes(16, 'little'), 16)
            ctx.fcw = 0x27f
            ctx.mxcsr = 0x1f80
            why = ctypes.create_string_buffer(128)
            stopped = dll.run_test(i, ctypes.byref(ctx), ctypes.addressof(mem), why)
            if stopped == 2:
                continue                                     # our side touched memory outside the window
            if uerr:
                if not stopped:
                    pass                                     # the CPU faulted (e.g. divide error): not comparable
                continue
            diffs = []
            for k in range(8):
                u = emu.reg_read(UREGS[k])
                if u != ctx.r[k]:
                    diffs.append('%s %08x!=%08x' % (REGS[k], ctx.r[k], u))
            uef = emu.reg_read(X.UC_X86_REG_EFLAGS)
            for m, sh in FLAGBITS:
                if defined & m and ((uef >> sh) & 1) != getattr(ctx, T.FLAGNAME[m]):
                    diffs.append('%s %d!=%d' % (T.FLAGNAME[m], getattr(ctx, T.FLAGNAME[m]), (uef >> sh) & 1))
            for k in range(8):
                u = emu.reg_read(UXMM[k]).to_bytes(16, 'little')
                if bytes(ctx.x[k]) != u:
                    diffs.append('xmm%d' % k)
            for a, b in blobs:
                if a + len(b) > WINDOW:
                    continue
                ours = ctypes.string_at(ctypes.addressof(mem) + a, len(b))
                theirs = bytes(emu.mem_read(a, len(b)))
                if ours != theirs:
                    diffs.append('mem@%08x' % a)
            if pair:
                taken_u = emu.reg_read(X.UC_X86_REG_EIP) != code_at + length
                taken_o = stopped == 1 and why.value.startswith(b'unreachable')
                if taken_u != taken_o:
                    diffs.append('branch ours=%d unicorn=%d' % (taken_o, taken_u))
                if stopped == 1 and not why.value.startswith(b'unreachable'):
                    diffs.append('stopped: ' + why.value.decode())
            elif stopped:
                diffs.append('stopped: ' + why.value.decode())
            if diffs:
                key = T.CODENAME[ins.code] + (' + ' + T.MNAME[units[i][1][1].mnemonic] if pair else '')
                failures[key].append((va, ' '.join(diffs[:6])))
                break
    os.makedirs(OUTD, exist_ok=True)
    with open(os.path.join(OUTD, 'report.txt'), 'w') as f:
        for k, v in sorted(failures.items()):
            f.write('%s %d  e.g. %08x %s\n' % (k, len(v), v[0][0], v[0][1]))
    print('%d samples, %d failing forms' % (len(units), len(failures)))
    for k, v in sorted(failures.items())[:80]:
        fmt = ix.Formatter(ix.FormatterSyntax.INTEL)
        ins = tr.decode(v[0][0])
        print('%-40s %-34s %s' % (k, fmt.format(ins)[:34], v[0][1]))


if __name__ == '__main__':
    main()
