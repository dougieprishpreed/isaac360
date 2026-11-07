"""Translates game/isaac-ng.exe's code to C (docs/RECOMP_PLAN.md, phase 2), using build/recomp/program.json.

One C function per guest function (`F_<addr>(X86* c)`), registers and arithmetic flags as locals (written back to
the context around calls), flags computed only where a later instruction reads them, compare + branch fused.
Guest memory goes through src/recomp/x86.h's RD/WR (byte-swapped on the big-endian console). An instruction the
translator does not handle becomes a call to x86_unimpl naming it, so the program builds and the first use stops
with the address.

    tools/.venv/Scripts/python tools/recomp/translate.py [--only 00401000,...] [--per-file 150]
Output: build/recomp/out/fNNN.c, functions.c (the address table), imports.txt, stats.
"""
import argparse
import re
import json
import os
import sys
from collections import defaultdict, Counter

import iced_x86 as ix
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAG = os.environ.get('BOI360_TAG', '')
EXE = os.path.join(ROOT, os.environ.get('BOI360_IMAGE', os.path.join('game', 'isaac-ng.exe')))
PROG = os.path.join(ROOT, 'build', 'recomp', TAG, 'program.json') if TAG else os.path.join(ROOT, 'build', 'recomp', 'program.json')
OUT = os.path.join(ROOT, 'build', 'recomp', 'out_' + TAG) if TAG else os.path.join(ROOT, 'build', 'recomp', 'out')
SUFFIX = ('_' + TAG) if TAG else ''
# BOI360_TRACE_FUNCS=addr,addr: those functions log their entry (arguments) and returns (eax) at run time
TRACE = set(int(x, 16) for x in os.environ.get('BOI360_TRACE_FUNCS', '').split(',') if x)

M = ix.Mnemonic
R = ix.Register
OK = ix.OpKind
FC = ix.FlowControl
MNAME = {v: k for k, v in vars(ix.Mnemonic).items() if not k.startswith('_')}
RNAME = {v: k for k, v in vars(ix.Register).items() if not k.startswith('_')}
CODENAME = {v: k for k, v in vars(ix.Code).items() if not k.startswith('_')}

CF, PF, AF, ZF, SF, OF = 1, 2, 4, 8, 16, 32
FLAGNAME = {CF: 'cf', PF: 'pf', AF: 'af', ZF: 'zf', SF: 'sf', OF: 'of'}
ALLF = CF | PF | AF | ZF | SF | OF


def rf_to_mask(v):
    m = 0
    if v & ix.RflagsBits.CF: m |= CF
    if v & ix.RflagsBits.PF: m |= PF
    if v & ix.RflagsBits.AF: m |= AF
    if v & ix.RflagsBits.ZF: m |= ZF
    if v & ix.RflagsBits.SF: m |= SF
    if v & ix.RflagsBits.OF: m |= OF
    return m


GPR32 = {R.EAX: 'eax', R.ECX: 'ecx', R.EDX: 'edx', R.EBX: 'ebx', R.ESP: 'esp', R.EBP: 'ebp', R.ESI: 'esi', R.EDI: 'edi'}
GPR16 = {R.AX: 'eax', R.CX: 'ecx', R.DX: 'edx', R.BX: 'ebx', R.SP: 'esp', R.BP: 'ebp', R.SI: 'esi', R.DI: 'edi'}
GPR8L = {R.AL: 'eax', R.CL: 'ecx', R.DL: 'edx', R.BL: 'ebx'}
GPR8H = {R.AH: 'eax', R.CH: 'ecx', R.DH: 'edx', R.BH: 'ebx'}
XMM = {getattr(R, 'XMM%d' % i): i for i in range(8)}
REGS = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']

COND = {  # condition code -> expression over flag locals
    'O': 'of', 'NO': '!of', 'B': 'cf', 'AE': '!cf', 'E': 'zf', 'NE': '!zf', 'BE': '(cf|zf)', 'A': '(!cf&&!zf)',
    'S': 'sf', 'NS': '!sf', 'P': 'pf', 'NP': '!pf', 'L': '(sf!=of)', 'GE': '(sf==of)', 'LE': '(zf||sf!=of)',
    'G': '(!zf&&sf==of)'}
COND_READS = {'O': OF, 'NO': OF, 'B': CF, 'AE': CF, 'E': ZF, 'NE': ZF, 'BE': CF | ZF, 'A': CF | ZF, 'S': SF, 'NS': SF,
              'P': PF, 'NP': PF, 'L': SF | OF, 'GE': SF | OF, 'LE': ZF | SF | OF, 'G': ZF | SF | OF}


def cond_of(name):
    """'JNE' -> 'NE', 'SETAE' -> 'AE', 'CMOVBE' -> 'BE'."""
    for p in ('CMOV', 'SET', 'J'):
        if name.startswith(p):
            c = name[len(p):]
            return {'Z': 'E', 'NZ': 'NE', 'C': 'B', 'NC': 'AE', 'NAE': 'B', 'NB': 'AE', 'NA': 'BE', 'NBE': 'A',
                    'PE': 'P', 'PO': 'NP', 'NGE': 'L', 'NL': 'GE', 'NG': 'LE', 'NLE': 'G'}.get(c, c)
    return None


class Unhandled(Exception):
    pass


class Img:
    def __init__(self):
        pe = pefile.PE(EXE, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.data = pe.get_memory_mapped_image()

    def u32(self, va):
        o = va - self.base
        return int.from_bytes(self.data[o:o + 4], 'little')


class Fn:
    """Translates one function."""

    def __init__(self, tr, start):
        self.tr = tr
        self.start = start
        self.out = []
        self.insns = {}
        self.succ = {}
        self.labels = set()

    # ------------------------------------------------------------------ discovery (instruction graph)
    def discover(self):
        tr = self.tr
        work = [self.start]
        while work:
            va = work.pop()
            while va not in self.insns:
                if not tr.in_text(va) or va in tr.table_bytes:
                    self.insns[va] = None                     # unreachable in practice (after a noreturn call)
                    self.succ[va] = []
                    break
                ins = tr.decode(va)
                if ins.code == ix.Code.INVALID:
                    self.insns[va] = None
                    self.succ[va] = []
                    break
                self.insns[va] = ins
                fc = ins.flow_control
                nxt = va + ins.len
                if fc == FC.NEXT or fc == FC.CALL or fc == FC.INDIRECT_CALL:
                    if va in tr.noreturn_calls or (fc == FC.CALL and ins.near_branch_target in tr.noreturn_funcs):
                        self.succ[va] = []
                        break
                    if nxt != self.start and nxt in tr.funcs and nxt not in self.insns and fc == FC.NEXT:
                        # falls into another function's entry
                        self.succ[va] = [('tail', nxt)]
                        break
                    self.succ[va] = [nxt]
                    va = nxt
                    continue
                if fc == FC.CONDITIONAL_BRANCH:
                    t = ins.near_branch_target
                    self.succ[va] = [nxt, t]
                    self.labels.add(t)
                    work.append(t)
                    va = nxt
                    continue
                if fc == FC.UNCONDITIONAL_BRANCH:
                    t = ins.near_branch_target
                    if t != self.start and t in tr.funcs:
                        self.succ[va] = [('tail', t)]
                    else:
                        self.succ[va] = [t]
                        self.labels.add(t)
                        work.append(t)
                    break
                if fc == FC.INDIRECT_BRANCH:
                    tbl = tr.jump_tables.get(va)
                    if tbl:
                        self.succ[va] = list(tbl)
                        for t in tbl:
                            self.labels.add(t)
                            work.append(t)
                    else:
                        self.succ[va] = []
                    break
                self.succ[va] = []                            # ret, int3, other
                break
        self.order = sorted(a for a, i in self.insns.items())

    # ------------------------------------------------------------------ flag liveness
    def liveness(self):
        rd, wr = {}, {}
        for va, ins in self.insns.items():
            if ins is None:
                rd[va], wr[va] = 0, 0
                continue
            r = rf_to_mask(ins.rflags_read)
            w = rf_to_mask(ins.rflags_modified)
            if ins.flow_control in (FC.CALL, FC.INDIRECT_CALL):
                w = ALLF                                       # callees leave flags undefined
            rd[va], wr[va] = r, w
        live_in = {va: 0 for va in self.insns}
        live_out = {va: 0 for va in self.insns}
        changed = True
        order = sorted(self.insns, reverse=True)
        while changed:
            changed = False
            for va in order:
                out = 0
                for s in self.succ.get(va, []):
                    if isinstance(s, tuple):
                        continue
                    out |= live_in.get(s, 0)
                new_in = rd[va] | (out & ~wr[va])
                if out != live_out[va] or new_in != live_in[va]:
                    live_out[va], live_in[va] = out, new_in
                    changed = True
        self.live_out = live_out

    # ------------------------------------------------------------------ operands
    def ea(self, ins):
        parts = []
        if ins.memory_base != R.NONE:
            if ins.memory_base not in GPR32:
                raise Unhandled('base ' + RNAME[ins.memory_base])
            parts.append(GPR32[ins.memory_base])
        if ins.memory_index != R.NONE:
            if ins.memory_index not in GPR32:
                raise Unhandled('index ' + RNAME[ins.memory_index])
            s = ins.memory_index_scale
            parts.append(GPR32[ins.memory_index] + ('*%d' % s if s != 1 else ''))
        d = ins.memory_displacement & 0xffffffff
        if d or not parts:
            parts.append('0x%xu' % d)
        e = '+'.join(parts)
        if ins.memory_segment == R.FS:
            e = 'c->fs_base+' + e
        elif ins.memory_segment == R.GS:
            raise Unhandled('gs')
        return '(uint32_t)(' + e + ')'

    def opsize(self, ins, i):
        k = ins.op_kind(i)
        if k == OK.REGISTER:
            r = ins.op_register(i)
            if r in GPR32: return 4
            if r in GPR16: return 2
            if r in GPR8L or r in GPR8H: return 1
            raise Unhandled('reg ' + RNAME[r])
        if k == OK.MEMORY:
            return ix.MemorySizeInfo  # placeholder, not used
        return None

    def msize(self, ins):
        ms = ins.memory_size
        info = ix.MemorySizeExt.size(ms) if hasattr(ix, 'MemorySizeExt') else None
        return info

    def get(self, ins, i, size):
        """Read operand i as an unsigned C expression of `size` bytes."""
        k = ins.op_kind(i)
        if k == OK.REGISTER:
            r = ins.op_register(i)
            if r in GPR32: return GPR32[r]
            if r in GPR16: return '(uint16_t)%s' % GPR16[r]
            if r in GPR8L: return '(uint8_t)%s' % GPR8L[r]
            if r in GPR8H: return '(uint8_t)(%s>>8)' % GPR8H[r]
            raise Unhandled('reg ' + RNAME[r])
        if k == OK.MEMORY:
            a = self.ea(ins)
            return {1: 'RD8(%s)', 2: 'RD16(%s)', 4: 'RD32(%s)'}[size] % a
        if k in (OK.IMMEDIATE8, OK.IMMEDIATE16, OK.IMMEDIATE32, OK.IMMEDIATE8TO16, OK.IMMEDIATE8TO32, OK.IMMEDIATE8_2ND):
            v = ins.immediate(i) & ((1 << (8 * size)) - 1)
            return '0x%xu' % v
        raise Unhandled('op kind %d' % k)

    def put(self, ins, i, size, expr):
        k = ins.op_kind(i)
        if k == OK.REGISTER:
            r = ins.op_register(i)
            if r in GPR32: return '%s=%s;' % (GPR32[r], expr)
            if r in GPR16: n = GPR16[r]; return '%s=(%s&0xffff0000u)|(uint16_t)(%s);' % (n, n, expr)
            if r in GPR8L: n = GPR8L[r]; return '%s=(%s&0xffffff00u)|(uint8_t)(%s);' % (n, n, expr)
            if r in GPR8H: n = GPR8H[r]; return '%s=(%s&0xffff00ffu)|((uint32_t)(uint8_t)(%s)<<8);' % (n, n, expr)
            raise Unhandled('reg ' + RNAME[r])
        if k == OK.MEMORY:
            a = self.ea(ins)
            return {1: 'WR8(%s,(uint8_t)(%s));', 2: 'WR16(%s,(uint16_t)(%s));', 4: 'WR32(%s,(uint32_t)(%s));'}[size] % (a, expr)
        raise Unhandled('put kind %d' % k)

    def size_of(self, ins):
        """Operand size of an integer instruction from operand 0 (register or memory)."""
        k = ins.op0_kind
        if k == OK.REGISTER:
            r = ins.op0_register
            if r in GPR32: return 4
            if r in GPR16: return 2
            if r in GPR8L or r in GPR8H: return 1
            raise Unhandled('reg ' + RNAME[r])
        if k == OK.MEMORY:
            n = MEMSIZE.get(ins.memory_size)
            if n in (1, 2, 4):
                return n
            raise Unhandled('memsize')
        raise Unhandled('size')

    def xmm(self, ins, i):
        r = ins.op_register(i)
        if r not in XMM:
            raise Unhandled('xmm ' + RNAME.get(r, '?'))
        return 'c->x[%d]' % XMM[r]

    # ------------------------------------------------------------------ flags helpers
    def flags_logic(self, r, n, need):
        out = []
        m = (1 << (8 * n)) - 1
        if need & CF: out.append('cf=0;')
        if need & OF: out.append('of=0;')
        if need & AF: out.append('af=0;')
        if need & ZF: out.append('zf=((%s)&0x%xu)==0;' % (r, m))
        if need & SF: out.append('sf=((%s)>>%d)&1;' % (r, 8 * n - 1))
        if need & PF: out.append('pf=X86_PARITY(%s);' % r)
        return out

    def flags_szp(self, r, n, need):
        m = (1 << (8 * n)) - 1
        out = []
        if need & ZF: out.append('zf=((%s)&0x%xu)==0;' % (r, m))
        if need & SF: out.append('sf=((%s)>>%d)&1;' % (r, 8 * n - 1))
        if need & PF: out.append('pf=X86_PARITY(%s);' % r)
        return out

    # ------------------------------------------------------------------ emission
    def emit(self, s):
        self.out.append(s)

    def spill(self):
        return 'SPILL();'

    def call_direct(self, va, ins, target):
        ret = va + ins.len
        name = self.tr.func_name(target)
        if name is None:
            return 'esp-=4;WR32(esp,0x%xu);SPILL();x86_call(c,0x%xu);FILL();' % (ret, target)
        return 'esp-=4;WR32(esp,0x%xu);SPILL();%s(c);FILL();' % (ret, name)

    def translate_insn(self, va, ins, nxt_va):
        tr = self.tr
        mn = ins.mnemonic
        name = MNAME[mn]
        need = self.live_out[va] & rf_to_mask(ins.rflags_modified)
        E = self.emit
        cname = CODENAME[ins.code]

        def jump_to(t):
            if t in self.insns and self.insns[t] is not None:
                return 'goto L_%x;' % t
            return 'x86_unreachable(c,0x%xu);' % t

        # ---- control flow
        if mn == M.RET:
            pop = ins.immediate16 if ins.op_count else 0
            trace = 'x86_trace_leave(c,0x%xu,eax);' % self.start if self.start in TRACE else ''
            E('esp+=%d;SPILL();%sreturn;' % (4 + pop, trace))
            return
        if mn == M.CALL:
            if ins.op0_kind in (OK.NEAR_BRANCH32, OK.NEAR_BRANCH16):
                E(self.call_direct(va, ins, ins.near_branch_target))
                return
            ret = va + ins.len
            if ins.op0_kind == OK.MEMORY and ins.memory_base == R.NONE and ins.memory_index == R.NONE:
                slot = ins.memory_displacement & 0xffffffff
                if slot in tr.imports:
                    E('esp-=4;WR32(esp,0x%xu);SPILL();x86_import(c,0x%xu);FILL(); /* %s */' % (ret, slot, tr.imports[slot]))
                    return
            tgt = self.get(ins, 0, 4)
            E('{uint32_t t_=%s;esp-=4;WR32(esp,0x%xu);SPILL();x86_call(c,t_);FILL();}' % (tgt, ret))
            return
        if ins.flow_control == FC.CONDITIONAL_BRANCH:
            t = ins.near_branch_target
            if name in ('JECXZ', 'JCXZ'):
                E('if(ecx==0) %s' % jump_to(t))
                return
            if name in ('LOOP', 'LOOPE', 'LOOPNE'):
                extra = {'LOOP': '', 'LOOPE': '&&zf', 'LOOPNE': '&&!zf'}[name]
                E('ecx--; if(ecx!=0%s) %s' % (extra, jump_to(t)))
                return
            cc = cond_of(name)
            E('if(%s) %s' % (self.fused.pop(va, COND[cc]), jump_to(t)))
            return
        if mn == M.JMP:
            if ins.op0_kind in (OK.NEAR_BRANCH32, OK.NEAR_BRANCH16):
                t = ins.near_branch_target
                s = self.succ[va][0]
                if isinstance(s, tuple):
                    n = tr.func_name(t)
                    E('SPILL();%s(c);return;' % n if n else 'SPILL();x86_call(c,0x%xu);return;' % t)
                else:
                    E(jump_to(t))
                return
            tbl = tr.jump_tables.get(va)
            if ins.op0_kind == OK.MEMORY and ins.memory_base == R.NONE and ins.memory_index == R.NONE:
                slot = ins.memory_displacement & 0xffffffff
                if slot in tr.imports:
                    E('SPILL();x86_import(c,0x%xu);return; /* %s */' % (slot, tr.imports[slot]))
                    return
            tgt = self.get(ins, 0, 4)
            if tbl:
                cases = ''.join('case 0x%xu:%s' % (t, jump_to(t)) for t in sorted(set(tbl)))
                E('switch(%s){%sdefault:x86_bad_jump(c,0x%xu,%s);}' % (tgt, cases, va, tgt))
            else:
                E('{uint32_t t_=%s;SPILL();x86_call(c,t_);return;}' % tgt)
            return
        if mn == M.INT3:
            E('x86_unreachable(c,0x%xu);' % va)
            return
        if mn in (M.NOP, M.WAIT, M.PAUSE, M.LFENCE, M.MFENCE, M.SFENCE, M.PREFETCHNTA, M.PREFETCHT0, M.PREFETCHT1,
                  M.PREFETCHT2, M.EMMS):
            return

        # ---- data movement
        if mn == M.MOV:
            k0, k1 = ins.op0_kind, ins.op1_kind
            if (k0 == OK.REGISTER and ins.op0_register in (R.ES, R.CS, R.SS, R.DS, R.FS, R.GS)) or \
               (k1 == OK.REGISTER and ins.op1_register in (R.ES, R.CS, R.SS, R.DS, R.FS, R.GS)):
                raise Unhandled('segment mov')
            n = self.size_of(ins)
            E(self.put(ins, 0, n, self.get(ins, 1, n)))
            return
        if mn == M.LEA:
            E('%s=%s;' % (GPR32[ins.op0_register], self.ea(ins)) if ins.op0_register in GPR32 else
              self.put(ins, 0, 2, self.ea(ins)))
            return
        if mn in (M.MOVZX, M.MOVSX):
            n = MEMSIZE[ins.memory_size] if ins.op1_kind == OK.MEMORY else (1 if ins.op1_register in GPR8L or ins.op1_register in GPR8H else 2)
            v = self.get(ins, 1, n)
            if mn == M.MOVSX:
                v = '(uint32_t)(int32_t)(%s)(%s)' % ('int8_t' if n == 1 else 'int16_t', v)
            d = 4 if ins.op0_register in GPR32 else 2
            E(self.put(ins, 0, d, v))
            return
        if mn == M.PUSH:
            if ins.op0_kind == OK.REGISTER and ins.op0_register not in GPR32:
                raise Unhandled('push ' + RNAME[ins.op0_register])
            v = self.get(ins, 0, 4)
            if ins.op0_kind in (OK.IMMEDIATE8TO32, OK.IMMEDIATE32, OK.IMMEDIATE8):
                v = '0x%xu' % (ins.immediate(0) & 0xffffffff)
            E('{uint32_t v_=%s;esp-=4;WR32(esp,v_);}' % v)
            return
        if mn == M.POP:
            if ins.op0_kind == OK.REGISTER:
                if ins.op0_register not in GPR32:
                    raise Unhandled('pop ' + RNAME[ins.op0_register])
                r = GPR32[ins.op0_register]
                E('{uint32_t v_=RD32(esp);esp+=4;%s=v_;}' % r)
            else:
                E('{uint32_t v_=RD32(esp);esp+=4;%s}' % self.put(ins, 0, 4, 'v_'))
            return
        if mn == M.XCHG:
            n = self.size_of(ins)
            E('{uint32_t a_=%s,b_=%s;%s%s}' % (self.get(ins, 0, n), self.get(ins, 1, n), self.put(ins, 0, n, 'b_'), self.put(ins, 1, n, 'a_')))
            return
        if mn == M.CDQ:
            E('edx=(uint32_t)((int32_t)eax>>31);')
            return
        if mn == M.CWDE:
            E('eax=(uint32_t)(int32_t)(int16_t)eax;')
            return
        if mn == M.CBW:
            E('eax=(eax&0xffff0000u)|(uint16_t)(int16_t)(int8_t)eax;')
            return
        if mn == M.LEAVE:
            E('esp=ebp;ebp=RD32(esp);esp+=4;')
            return
        if mn == M.LAHF:
            E('eax=(eax&0xffff00ffu)|((uint32_t)((sf<<7)|(zf<<6)|(af<<4)|(pf<<2)|2|cf)<<8);')
            return
        if mn == M.SAHF:
            E('{uint32_t h_=(eax>>8)&0xff;sf=(h_>>7)&1;zf=(h_>>6)&1;af=(h_>>4)&1;pf=(h_>>2)&1;cf=h_&1;}')
            return
        if mn == M.PUSHFD:
            E('{uint32_t f_=2|cf|(pf<<2)|(af<<4)|(zf<<6)|(sf<<7)|(c->df<<10)|(of<<11);esp-=4;WR32(esp,f_);}')
            return
        if mn == M.POPFD:
            E('{uint32_t f_=RD32(esp);esp+=4;cf=f_&1;pf=(f_>>2)&1;af=(f_>>4)&1;zf=(f_>>6)&1;sf=(f_>>7)&1;c->df=(f_>>10)&1;of=(f_>>11)&1;}')
            return
        if mn == M.PUSHAD:
            E('{uint32_t s_=esp;esp-=32;WR32(esp+28,eax);WR32(esp+24,ecx);WR32(esp+20,edx);WR32(esp+16,ebx);WR32(esp+12,s_);WR32(esp+8,ebp);WR32(esp+4,esi);WR32(esp,edi);}')
            return
        if mn == M.POPAD:
            E('edi=RD32(esp);esi=RD32(esp+4);ebp=RD32(esp+8);ebx=RD32(esp+16);edx=RD32(esp+20);ecx=RD32(esp+24);eax=RD32(esp+28);esp+=32;')
            return
        if mn in (M.CLD, M.STD):
            E('c->df=%d;' % (1 if mn == M.STD else 0))
            return
        if mn in (M.CLC, M.STC, M.CMC):
            E({M.CLC: 'cf=0;', M.STC: 'cf=1;', M.CMC: 'cf=!cf;'}[mn])
            return
        if mn == M.BSWAP:
            r = GPR32[ins.op0_register]
            E('%s=__builtin_bswap32(%s);' % (r, r))
            return
        if name.startswith('SET'):
            cc = cond_of(name)
            E(self.put(ins, 0, 1, '(%s)?1:0' % self.fused.pop(va, COND[cc])))
            return
        if name.startswith('CMOV'):
            cc = cond_of(name)
            n = 4 if ins.op0_register in GPR32 else 2
            E('if(%s){%s}' % (self.fused.pop(va, COND[cc]), self.put(ins, 0, n, self.get(ins, 1, n))))
            return

        # ---- integer arithmetic
        if mn in (M.ADD, M.SUB, M.CMP, M.ADC, M.SBB):
            n = self.size_of(ins)
            m = (1 << (8 * n)) - 1
            T = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            a, b = self.get(ins, 0, n), self.get(ins, 1, n)
            if mn == M.CMP and va in self.fusable:
                return                                        # folded into the consumer
            out = ['{%s a_=%s,b_=%s;' % (T, a, b)]
            if mn == M.ADD: out.append('%s r_=(%s)(a_+b_);' % (T, T))
            elif mn == M.ADC: out.append('%s r_=(%s)(a_+b_+cf);' % (T, T))
            elif mn == M.SBB: out.append('%s r_=(%s)(a_-b_-cf);' % (T, T))
            else: out.append('%s r_=(%s)(a_-b_);' % (T, T))
            fl = []
            sb = 8 * n - 1
            if need & CF:
                fl.append({M.ADD: 'cf=r_<a_;', M.ADC: 'cf=((uint64_t)a_+b_+cf)>0x%xu;' % m, M.SUB: 'cf=a_<b_;', M.CMP: 'cf=a_<b_;',
                           M.SBB: 'cf=(uint64_t)a_<(uint64_t)b_+cf;'}[mn])
            if need & OF:
                if mn in (M.ADD, M.ADC): fl.append('of=((~(a_^b_)&(a_^r_))>>%d)&1;' % sb)
                else: fl.append('of=(((a_^b_)&(a_^r_))>>%d)&1;' % sb)
            if need & AF: fl.append('af=((a_^b_^r_)>>4)&1;')
            # cf must be computed before r_ is stored only for adc/sbb which read the old cf: done above with r_
            out += self.flags_szp('r_', n, need)
            out = out[:2] + [f for f in fl] + out[2:]
            if mn != M.CMP:
                out.append(self.put(ins, 0, n, 'r_'))
            out.append('}')
            if mn in (M.ADC, M.SBB) and need & CF:
                # the carry formula reads the old cf: compute it before r_ uses... r_ already used old cf; reorder
                pass
            E(''.join(out))
            return
        if mn in (M.AND, M.OR, M.XOR, M.TEST):
            n = self.size_of(ins)
            T = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            if mn == M.TEST and va in self.fusable:
                return
            if mn == M.XOR and ins.op0_kind == OK.REGISTER and ins.op1_kind == OK.REGISTER and ins.op0_register == ins.op1_register:
                E(self.put(ins, 0, n, '0') + ''.join(self.flags_logic('0u', n, need)))
                return
            op = {M.AND: '&', M.OR: '|', M.XOR: '^', M.TEST: '&'}[mn]
            out = '{%s r_=(%s)(%s%s%s);' % (T, T, self.get(ins, 0, n), op, self.get(ins, 1, n))
            out += ''.join(self.flags_logic('r_', n, need))
            if mn != M.TEST:
                out += self.put(ins, 0, n, 'r_')
            E(out + '}')
            return
        if mn in (M.INC, M.DEC):
            n = self.size_of(ins)
            T = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            sm = 1 << (8 * n - 1)
            out = '{%s a_=%s;%s r_=(%s)(a_%s1);' % (T, self.get(ins, 0, n), T, T, '+' if mn == M.INC else '-')
            if need & OF: out += 'of=r_==0x%xu;' % sm if mn == M.INC else 'of=a_==0x%xu;' % sm
            if need & AF: out += 'af=((a_^r_^1)>>4)&1;'
            out += ''.join(self.flags_szp('r_', n, need))
            E(out + self.put(ins, 0, n, 'r_') + '}')
            return
        if mn in (M.NEG, M.NOT):
            n = self.size_of(ins)
            T = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            if mn == M.NOT:
                E(self.put(ins, 0, n, '~%s' % self.get(ins, 0, n)))
                return
            sm = 1 << (8 * n - 1)
            out = '{%s a_=%s;%s r_=(%s)(0u-a_);' % (T, self.get(ins, 0, n), T, T)
            if need & CF: out += 'cf=a_!=0;'
            if need & OF: out += 'of=a_==0x%xu;' % sm
            if need & AF: out += 'af=((a_^r_)>>4)&1;'
            out += ''.join(self.flags_szp('r_', n, need))
            E(out + self.put(ins, 0, n, 'r_') + '}')
            return
        if mn in (M.SHL, M.SHR, M.SAR, M.ROL, M.ROR, M.SAL):
            n = self.size_of(ins)
            T = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            bits = 8 * n
            cnt = self.get(ins, 1, 1) if ins.op1_kind == OK.REGISTER else '%du' % (ins.immediate(1) & 0xff)
            out = '{%s a_=%s;uint32_t k_=(%s)&31;if(k_){%s r_;' % (T, self.get(ins, 0, n), cnt, T)
            if mn in (M.SHL, M.SAL):
                out += 'r_=(%s)((uint32_t)a_<<k_);' % T
                if need & CF: out += 'cf=k_<=%d?(((uint32_t)a_>>(%d-k_))&1):0;' % (bits, bits)
                if need & OF: out += 'of=((r_>>%d)&1)^(k_<=%d?(((uint32_t)a_>>(%d-k_))&1):0);' % (bits - 1, bits, bits)
            elif mn == M.SHR:
                out += 'r_=(%s)(k_<%d?((uint32_t)a_>>k_):0);' % (T, bits)
                if need & CF: out += 'cf=((uint32_t)a_>>(k_-1))&1;'
                if need & OF: out += 'of=(a_>>%d)&1;' % (bits - 1)
            elif mn == M.SAR:
                ST = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}[n]
                out += 'r_=(%s)((int32_t)(%s)a_>>(k_<%d?k_:%d));' % (T, ST, bits, bits - 1)
                if need & CF: out += 'cf=((int32_t)(%s)a_>>(k_-1<%d?k_-1:%d))&1;' % (ST, bits, bits - 1)
                if need & OF: out += 'of=0;'
            elif mn == M.ROL:
                out += 'uint32_t q_=k_%%%d;r_=(%s)(q_?(((uint32_t)a_<<q_)|((uint32_t)a_>>(%d-q_))):a_);' % (bits, T, bits)
                if need & CF: out += 'cf=r_&1;'
                if need & OF: out += 'of=((r_>>%d)&1)^(r_&1);' % (bits - 1)
            elif mn == M.ROR:
                out += 'uint32_t q_=k_%%%d;r_=(%s)(q_?(((uint32_t)a_>>q_)|((uint32_t)a_<<(%d-q_))):a_);' % (bits, T, bits)
                if need & CF: out += 'cf=(r_>>%d)&1;' % (bits - 1)
                if need & OF: out += 'of=((r_>>%d)^(r_>>%d))&1;' % (bits - 1, bits - 2)
            if mn not in (M.ROL, M.ROR):
                out += ''.join(self.flags_szp('r_', n, need))
                if need & AF: out += 'af=0;'
            out += self.put(ins, 0, n, 'r_') + '}}'
            E(out)
            return
        if mn in (M.SHLD, M.SHRD):
            if self.size_of(ins) != 4: raise Unhandled('shld16')
            cnt = self.get(ins, 2, 1) if ins.op2_kind == OK.REGISTER else '%du' % (ins.immediate(2) & 0xff)
            a, b = self.get(ins, 0, 4), self.get(ins, 1, 4)
            if mn == M.SHLD:
                core = 'r_=(a_<<k_)|(b_>>(32-k_));'
                cfx = 'cf=(a_>>(32-k_))&1;'
            else:
                core = 'r_=(a_>>k_)|(b_<<(32-k_));'
                cfx = 'cf=(a_>>(k_-1))&1;'
            out = '{uint32_t a_=%s,b_=%s,k_=(%s)&31;if(k_){uint32_t r_;%s' % (a, b, cnt, core)
            if need & CF: out += cfx
            if need & OF: out += 'of=((r_^a_)>>31)&1;'
            out += ''.join(self.flags_szp('r_', 4, need))
            E(out + self.put(ins, 0, 4, 'r_') + '}}')
            return
        if mn in (M.RCR, M.RCL):
            n = self.size_of(ins)
            if n != 4 or ins.op1_kind == OK.REGISTER or (ins.immediate(1) & 31) != 1:
                raise Unhandled('rcx')
            a = self.get(ins, 0, 4)
            if mn == M.RCR:
                E('{uint32_t a_=%s;uint32_t r_=(a_>>1)|((uint32_t)cf<<31);%sof=((r_>>31)^(r_>>30))&1;cf=a_&1;%s}' % (a, '', self.put(ins, 0, 4, 'r_')))
            else:
                E('{uint32_t a_=%s;uint32_t r_=(a_<<1)|cf;cf=a_>>31;of=(r_>>31)^cf;%s}' % (a, self.put(ins, 0, 4, 'r_')))
            return
        if mn == M.IMUL:
            if ins.op_count == 1:
                n = self.size_of(ins)
                if n == 4:
                    E('{int64_t p_=(int64_t)(int32_t)eax*(int32_t)%s;eax=(uint32_t)p_;edx=(uint32_t)((uint64_t)p_>>32);cf=of=(p_!=(int32_t)p_);}' % self.get(ins, 0, 4))
                elif n == 1:
                    E('{int32_t p_=(int8_t)eax*(int8_t)%s;eax=(eax&0xffff0000u)|(uint16_t)p_;cf=of=(p_!=(int8_t)p_);}' % self.get(ins, 0, 1))
                else:
                    E('{int32_t p_=(int16_t)eax*(int16_t)%s;eax=(eax&0xffff0000u)|(uint16_t)p_;edx=(edx&0xffff0000u)|(uint16_t)(p_>>16);cf=of=(p_!=(int16_t)p_);}' % self.get(ins, 0, 2))
                return
            n = 4 if ins.op0_register in GPR32 else 2
            if n != 4: raise Unhandled('imul16')
            if ins.op_count == 2:
                a, b = self.get(ins, 0, 4), self.get(ins, 1, 4)
            else:
                a, b = self.get(ins, 1, 4), '0x%xu' % (ins.immediate(2) & 0xffffffff)
            out = '{int64_t p_=(int64_t)(int32_t)%s*(int32_t)%s;' % (a, b)
            if need & (CF | OF): out += 'cf=of=(p_!=(int32_t)p_);'
            if need & ZF: out += 'zf=((uint32_t)p_)==0;'
            if need & SF: out += 'sf=((uint32_t)p_)>>31;'
            if need & PF: out += 'pf=X86_PARITY((uint32_t)p_);'
            E(out + self.put(ins, 0, 4, '(uint32_t)p_') + '}')
            return
        if mn == M.MUL:
            n = self.size_of(ins)
            if n == 4:
                E('{uint64_t p_=(uint64_t)eax*%s;eax=(uint32_t)p_;edx=(uint32_t)(p_>>32);cf=of=(edx!=0);}' % self.get(ins, 0, 4))
            elif n == 1:
                E('{uint32_t p_=(uint8_t)eax*(uint32_t)%s;eax=(eax&0xffff0000u)|(uint16_t)p_;cf=of=(p_>>8)!=0;}' % self.get(ins, 0, 1))
            else:
                E('{uint32_t p_=(uint16_t)eax*(uint32_t)%s;eax=(eax&0xffff0000u)|(uint16_t)p_;edx=(edx&0xffff0000u)|(p_>>16);cf=of=(p_>>16)!=0;}' % self.get(ins, 0, 2))
            return
        if mn in (M.DIV, M.IDIV):
            n = self.size_of(ins)
            d = self.get(ins, 0, n)
            if n == 4:
                if mn == M.DIV:
                    E('{uint32_t d_=%s;uint64_t v_=((uint64_t)edx<<32)|eax;if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(uint32_t)(v_/d_);edx=(uint32_t)(v_%%d_);}' % (d, va))
                else:
                    E('{int32_t d_=(int32_t)%s;int64_t v_=(int64_t)(((uint64_t)edx<<32)|eax);if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(uint32_t)(int32_t)(v_/d_);edx=(uint32_t)(int32_t)(v_%%d_);}' % (d, va))
            elif n == 1:
                if mn == M.DIV:
                    E('{uint32_t d_=%s,v_=(uint16_t)eax;if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(eax&0xffff0000u)|((v_%%d_)<<8)|(uint8_t)(v_/d_);}' % (d, va))
                else:
                    E('{int32_t d_=(int8_t)%s,v_=(int16_t)eax;if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(eax&0xffff0000u)|((uint32_t)(uint8_t)(v_%%d_)<<8)|(uint8_t)(v_/d_);}' % (d, va))
            else:
                if mn == M.DIV:
                    E('{uint32_t d_=%s,v_=((edx&0xffff)<<16)|(eax&0xffff);if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(eax&0xffff0000u)|(uint16_t)(v_/d_);edx=(edx&0xffff0000u)|(uint16_t)(v_%%d_);}' % (d, va))
                else:
                    E('{int32_t d_=(int16_t)%s,v_=(int32_t)(((edx&0xffff)<<16)|(eax&0xffff));if(!d_)x86_unimpl(c,0x%xu,"divide by zero");eax=(eax&0xffff0000u)|(uint16_t)(v_/d_);edx=(edx&0xffff0000u)|(uint16_t)(v_%%d_);}' % (d, va))
            return
        if mn in (M.BT, M.BTS, M.BTR, M.BTC):
            n = self.size_of(ins)
            if n != 4: raise Unhandled('bt16')
            if ins.op0_kind == OK.MEMORY and ins.op1_kind == OK.REGISTER:
                # bit string addressing
                a = self.ea(ins)
                bit = GPR32[ins.op1_register]
                op = {M.BT: '', M.BTS: 'WR32(p_,v_|m_);', M.BTR: 'WR32(p_,v_&~m_);', M.BTC: 'WR32(p_,v_^m_);'}[mn]
                E('{int32_t o_=(int32_t)%s;uint32_t p_=%s+(uint32_t)((o_>>5)*4);uint32_t v_=RD32(p_),m_=1u<<(o_&31);cf=(v_&m_)!=0;%s}' % (bit, a, op))
                return
            bitx = self.get(ins, 1, 4) if ins.op1_kind == OK.REGISTER else '0x%xu' % (ins.immediate(1) & 0xff)
            op = {M.BT: '', M.BTS: self.put(ins, 0, 4, 'v_|m_'), M.BTR: self.put(ins, 0, 4, 'v_&~m_'), M.BTC: self.put(ins, 0, 4, 'v_^m_')}[mn]
            E('{uint32_t v_=%s,m_=1u<<((%s)&31);cf=(v_&m_)!=0;%s}' % (self.get(ins, 0, 4), bitx, op))
            return
        if mn in (M.BSF, M.BSR):
            v = self.get(ins, 1, 4)
            if mn == M.BSF:
                E('{uint32_t v_=%s;zf=v_==0;if(v_)%s}' % (v, self.put(ins, 0, 4, '__builtin_ctz(v_)')))
            else:
                E('{uint32_t v_=%s;zf=v_==0;if(v_)%s}' % (v, self.put(ins, 0, 4, '31-__builtin_clz(v_)')))
            return
        if mn == M.XADD:
            n = self.size_of(ins)
            if n != 4: raise Unhandled('xadd')
            out = '{uint32_t a_=%s,b_=%s,r_=a_+b_;' % (self.get(ins, 0, 4), self.get(ins, 1, 4))
            if need & CF: out += 'cf=r_<a_;'
            if need & OF: out += 'of=((~(a_^b_)&(a_^r_))>>31)&1;'
            out += ''.join(self.flags_szp('r_', 4, need))
            E(out + self.put(ins, 1, 4, 'a_') + self.put(ins, 0, 4, 'r_') + '}')
            return
        if mn == M.CMPXCHG:
            n = self.size_of(ins)
            if n != 4: raise Unhandled('cmpxchg')
            # flags as CMP eax, dest; then the exchange
            E('{uint32_t a_=eax,b_=%s,r_=a_-b_;cf=a_<b_;of=(((a_^b_)&(a_^r_))>>31)&1;af=((a_^b_^r_)>>4)&1;zf=r_==0;sf=r_>>31;pf=X86_PARITY(r_);'
              'if(zf){%s}else eax=b_;}' % (self.get(ins, 0, 4), self.put(ins, 0, 4, self.get(ins, 1, 4))))
            return
        if mn == M.CPUID:
            E('SPILL();x86_cpuid(c);FILL();')
            return
        if mn == M.RDTSC:
            E('SPILL();x86_unimpl(c,0x%xu,"rdtsc");' % va)
            return

        # ---- string instructions
        if cname.startswith(('STOSB', 'STOSW', 'STOSD', 'MOVSB_', 'MOVSW_', 'MOVSD_M32', 'CMPSB', 'SCASB', 'LODSB', 'LODSD')):
            rep = ins.has_rep_prefix or ins.has_repe_prefix
            repne = ins.has_repne_prefix
            step = 'c->df?-%d:%d'
            if mn in (M.STOSB, M.STOSW, M.STOSD):
                n = {M.STOSB: 1, M.STOSW: 2, M.STOSD: 4}[mn]
                body = '%s;edi+=(%s);' % ({1: 'WR8(edi,(uint8_t)eax)', 2: 'WR16(edi,(uint16_t)eax)', 4: 'WR32(edi,eax)'}[n], step % (n, n))
            elif mn in (M.MOVSB, M.MOVSW, M.MOVSD):
                n = {M.MOVSB: 1, M.MOVSW: 2, M.MOVSD: 4}[mn]
                body = '%s;esi+=(%s);edi+=(%s);' % ({1: 'WR8(edi,RD8(esi))', 2: 'WR16(edi,RD16(esi))', 4: 'WR32(edi,RD32(esi))'}[n],
                                                    step % (n, n), step % (n, n))
            elif mn == M.LODSB:
                body = 'eax=(eax&0xffffff00u)|RD8(esi);esi+=(%s);' % (step % (1, 1))
                rep = False
            elif mn == M.LODSD:
                body = 'eax=RD32(esi);esi+=(%s);' % (step % (4, 4))
                rep = False
            elif mn == M.CMPSB:
                body = '{uint8_t a_=RD8(esi),b_=RD8(edi);uint8_t r_=a_-b_;cf=a_<b_;zf=r_==0;sf=r_>>7;of=(((a_^b_)&(a_^r_))>>7)&1;pf=X86_PARITY(r_);esi+=(%s);edi+=(%s);}' % (step % (1, 1), step % (1, 1))
                if rep or repne:
                    E('while(ecx){%secx--;if(%szf)break;}' % (body, '' if repne else '!'))
                    return
                E(body)
                return
            elif mn == M.SCASB:
                body = '{uint8_t a_=(uint8_t)eax,b_=RD8(edi);uint8_t r_=a_-b_;cf=a_<b_;zf=r_==0;sf=r_>>7;of=(((a_^b_)&(a_^r_))>>7)&1;pf=X86_PARITY(r_);edi+=(%s);}' % (step % (1, 1))
                if rep or repne:
                    E('while(ecx){%secx--;if(%szf)break;}' % (body, '' if repne else '!'))
                    return
                E(body)
                return
            else:
                raise Unhandled('string ' + name)
            if rep:
                E('while(ecx){%secx--;}' % body)
            else:
                E(body)
            return

        # ---- SSE / SSE2
        r = self.sse(va, ins, name, cname, need)
        if r is not None:
            E(r)
            return
        r = self.x87(va, ins, name, cname)
        if r is not None:
            E(r)
            return
        raise Unhandled(name)

    # ------------------------------------------------------------------ SSE
    def xsrc(self, ins, i, bits):
        """Source operand i as an X86Xmm lvalue/temporary name; memory is loaded into a temporary s_."""
        if ins.op_kind(i) == OK.REGISTER:
            return None, self.xmm(ins, i)
        a = self.ea(ins)
        if bits == 32: return 'X86Xmm s_;s_.u[0]=RD32(%s);' % a, 's_'
        if bits == 64: return 'X86Xmm s_;s_.q[0]=RD64(%s);' % a, 's_'
        return 'X86Xmm s_;RDX(%s,&s_);' % a, 's_'

    def sse(self, va, ins, name, cname, need):
        mn = ins.mnemonic
        k0, k1 = ins.op0_kind, ins.op1_kind if ins.op_count > 1 else None
        def dst(): return self.xmm(ins, 0)
        if name.startswith('V'):
            return 'x86_unimpl(c,0x%xu,"%s");' % (va, name)            # AVX: behind CPUID, never taken
        # moves
        if mn in (M.MOVAPS, M.MOVUPS, M.MOVDQA, M.MOVDQU, M.MOVAPD, M.MOVUPD, M.LDDQU):
            if k0 == OK.MEMORY:
                return 'WRX(%s,&%s);' % (self.ea(ins), self.xmm(ins, 1))
            if k1 == OK.MEMORY:
                return 'RDX(%s,&%s);' % (self.ea(ins), dst())
            return '%s=%s;' % (dst(), self.xmm(ins, 1))
        if mn == M.MOVSS:
            if k0 == OK.MEMORY: return 'WR32(%s,%s.u[0]);' % (self.ea(ins), self.xmm(ins, 1))
            if k1 == OK.MEMORY: return '{X86Xmm* d_=&%s;d_->u[0]=RD32(%s);d_->u[1]=d_->u[2]=d_->u[3]=0;}' % (dst(), self.ea(ins))
            return '%s.u[0]=%s.u[0];' % (dst(), self.xmm(ins, 1))
        if mn == M.MOVSD and cname.startswith('MOVSD_XMM'):
            if k0 == OK.MEMORY: return 'WR64(%s,%s.q[0]);' % (self.ea(ins), self.xmm(ins, 1))
            if k1 == OK.MEMORY: return '{X86Xmm* d_=&%s;d_->q[0]=RD64(%s);d_->q[1]=0;}' % (dst(), self.ea(ins))
            return '%s.q[0]=%s.q[0];' % (dst(), self.xmm(ins, 1))
        if mn == M.MOVD:
            if k0 == OK.REGISTER and ins.op0_register in XMM:
                return '{X86Xmm* d_=&%s;d_->u[0]=%s;d_->u[1]=d_->u[2]=d_->u[3]=0;}' % (dst(), self.get(ins, 1, 4))
            return self.put(ins, 0, 4, '%s.u[0]' % self.xmm(ins, 1))
        if mn == M.MOVQ:
            if k0 == OK.MEMORY: return 'WR64(%s,%s.q[0]);' % (self.ea(ins), self.xmm(ins, 1))
            if k1 == OK.MEMORY: return '{X86Xmm* d_=&%s;d_->q[0]=RD64(%s);d_->q[1]=0;}' % (dst(), self.ea(ins))
            return '{X86Xmm* d_=&%s;d_->q[0]=%s.q[0];d_->q[1]=0;}' % (dst(), self.xmm(ins, 1))
        if mn in (M.MOVLPD, M.MOVLPS):
            if k0 == OK.MEMORY: return 'WR64(%s,%s.q[0]);' % (self.ea(ins), self.xmm(ins, 1))
            return '%s.q[0]=RD64(%s);' % (dst(), self.ea(ins))
        if mn in (M.MOVHPD, M.MOVHPS):
            if k0 == OK.MEMORY: return 'WR64(%s,%s.q[1]);' % (self.ea(ins), self.xmm(ins, 1))
            return '%s.q[1]=RD64(%s);' % (dst(), self.ea(ins))
        if mn == M.MOVHLPS:
            return '%s.q[0]=%s.q[1];' % (dst(), self.xmm(ins, 1))
        if mn == M.MOVLHPS:
            return '%s.q[1]=%s.q[0];' % (dst(), self.xmm(ins, 1))
        if mn == M.MOVMSKPS:
            return self.put(ins, 0, 4, '((%s.u[0]>>31)|((%s.u[1]>>31)<<1)|((%s.u[2]>>31)<<2)|((%s.u[3]>>31)<<3))' % ((self.xmm(ins, 1),) * 4))
        # scalar arithmetic
        scal = {M.ADDSS: '+', M.SUBSS: '-', M.MULSS: '*', M.DIVSS: '/'}
        if mn in scal:
            pre, s = self.xsrc(ins, 1, 32)
            return '{%s%s.f[0]=%s.f[0]%s%s.f[0];}' % (pre or '', dst(), dst(), scal[mn], s)
        scald = {M.ADDSD: '+', M.SUBSD: '-', M.MULSD: '*', M.DIVSD: '/'}
        if mn in scald:
            pre, s = self.xsrc(ins, 1, 64)
            return '{%s%s.d[0]=%s.d[0]%s%s.d[0];}' % (pre or '', dst(), dst(), scald[mn], s)
        if mn in (M.MINSS, M.MAXSS):
            pre, s = self.xsrc(ins, 1, 32)
            op = '<' if mn == M.MINSS else '>'
            return '{%sfloat a_=%s.f[0],b_=%s.f[0];%s.f[0]=(a_%sb_)?a_:b_;}' % (pre or '', dst(), s, dst(), op)
        if mn in (M.MINSD, M.MAXSD):
            pre, s = self.xsrc(ins, 1, 64)
            op = '<' if mn == M.MINSD else '>'
            return '{%sdouble a_=%s.d[0],b_=%s.d[0];%s.d[0]=(a_%sb_)?a_:b_;}' % (pre or '', dst(), s, dst(), op)
        if mn == M.SQRTSS:
            pre, s = self.xsrc(ins, 1, 32)
            return '{%s%s.f[0]=sqrtf(%s.f[0]);}' % (pre or '', dst(), s)
        if mn == M.SQRTSD:
            pre, s = self.xsrc(ins, 1, 64)
            return '{%s%s.d[0]=sqrt(%s.d[0]);}' % (pre or '', dst(), s)
        if mn in (M.RCPSS, M.RSQRTSS):
            pre, s = self.xsrc(ins, 1, 32)
            f = '1.0f/%s.f[0]' if mn == M.RCPSS else '1.0f/sqrtf(%s.f[0])'
            return '{%s%s.f[0]=%s;}' % (pre or '', dst(), f % s)
        packed = {M.ADDPS: '+', M.SUBPS: '-', M.MULPS: '*', M.DIVPS: '/'}
        if mn in packed:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<4;i_++)%s.f[i_]=%s.f[i_]%s%s.f[i_];}' % (pre or '', dst(), dst(), packed[mn], s)
        packedd = {M.ADDPD: '+', M.SUBPD: '-', M.MULPD: '*', M.DIVPD: '/'}
        if mn in packedd:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<2;i_++)%s.d[i_]=%s.d[i_]%s%s.d[i_];}' % (pre or '', dst(), dst(), packedd[mn], s)
        if mn in (M.MINPS, M.MAXPS):
            pre, s = self.xsrc(ins, 1, 128)
            op = '<' if mn == M.MINPS else '>'
            return '{%sfor(int i_=0;i_<4;i_++){float a_=%s.f[i_],b_=%s.f[i_];%s.f[i_]=(a_%sb_)?a_:b_;}}' % (pre or '', dst(), s, dst(), op)
        if mn == M.SQRTPS:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<4;i_++)%s.f[i_]=sqrtf(%s.f[i_]);}' % (pre or '', dst(), s)
        bitw = {M.XORPS: '^', M.XORPD: '^', M.PXOR: '^', M.ANDPS: '&', M.ANDPD: '&', M.PAND: '&', M.ORPS: '|', M.ORPD: '|', M.POR: '|'}
        if mn in bitw:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%s%s.q[0]%s=%s.q[0];%s.q[1]%s=%s.q[1];}' % (pre or '', dst(), bitw[mn], s, dst(), bitw[mn], s)
        if mn in (M.ANDNPS, M.ANDNPD, M.PANDN):
            pre, s = self.xsrc(ins, 1, 128)
            return '{%s%s.q[0]=~%s.q[0]&%s.q[0];%s.q[1]=~%s.q[1]&%s.q[1];}' % (pre or '', dst(), dst(), s, dst(), dst(), s)
        # compares setting flags
        if mn in (M.COMISS, M.UCOMISS, M.COMISD, M.UCOMISD):
            bits = 32 if mn in (M.COMISS, M.UCOMISS) else 64
            pre, s = self.xsrc(ins, 1, bits)
            f = 'f' if bits == 32 else 'd'
            return ('{%s%s a_=%s.%s[0],b_=%s.%s[0];if(a_!=a_||b_!=b_){zf=pf=cf=1;}else{zf=a_==b_;pf=0;cf=a_<b_;}of=sf=af=0;}' %
                    (pre or '', 'float' if bits == 32 else 'double', dst(), f, s, f))
        if mn in (M.CMPSS, M.CMPPS) or (mn == M.CMPSD and cname.startswith('CMPSD_XMM')):
            imm = ins.immediate(2) & 7
            ops = ['a_==b_', 'a_<b_', 'a_<=b_', '(a_!=a_||b_!=b_)', '!(a_==b_)', '!(a_<b_)', '!(a_<=b_)', '!(a_!=a_||b_!=b_)']
            if mn == M.CMPSS:
                pre, s = self.xsrc(ins, 1, 32)
                return '{%sfloat a_=%s.f[0],b_=%s.f[0];%s.u[0]=(%s)?0xffffffffu:0;}' % (pre or '', dst(), s, dst(), ops[imm])
            if mn == M.CMPPS:
                pre, s = self.xsrc(ins, 1, 128)
                return '{%sfor(int i_=0;i_<4;i_++){float a_=%s.f[i_],b_=%s.f[i_];%s.u[i_]=(%s)?0xffffffffu:0;}}' % (pre or '', dst(), s, dst(), ops[imm])
            pre, s = self.xsrc(ins, 1, 64)
            return '{%sdouble a_=%s.d[0],b_=%s.d[0];%s.q[0]=(%s)?~0ull:0;}' % (pre or '', dst(), s, dst(), ops[imm])
        # conversions
        if mn == M.CVTSI2SS:
            return '%s.f[0]=(float)(int32_t)%s;' % (dst(), self.get(ins, 1, 4))
        if mn == M.CVTSI2SD:
            return '%s.d[0]=(double)(int32_t)%s;' % (dst(), self.get(ins, 1, 4))
        if mn in (M.CVTTSS2SI, M.CVTSS2SI):
            pre, s = self.xsrc(ins, 1, 32)
            conv = 'x86_f2i_trunc' if mn == M.CVTTSS2SI else 'x86_f2i_round'
            return '{%s%s}' % (pre or '', self.put(ins, 0, 4, '%s((double)%s.f[0])' % (conv, s)))
        if mn in (M.CVTTSD2SI, M.CVTSD2SI):
            pre, s = self.xsrc(ins, 1, 64)
            conv = 'x86_f2i_trunc' if mn == M.CVTTSD2SI else 'x86_f2i_round'
            return '{%s%s}' % (pre or '', self.put(ins, 0, 4, '%s(%s.d[0])' % (conv, s)))
        if mn == M.CVTSS2SD:
            pre, s = self.xsrc(ins, 1, 32)
            return '{%s%s.d[0]=(double)%s.f[0];}' % (pre or '', dst(), s)
        if mn == M.CVTSD2SS:
            pre, s = self.xsrc(ins, 1, 64)
            return '{%s%s.f[0]=(float)%s.d[0];}' % (pre or '', dst(), s)
        if mn == M.CVTDQ2PS:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;for(int i_=0;i_<4;i_++)t_.f[i_]=(float)%s.i[i_];%s=t_;}' % (pre or '', s, dst())
        if mn == M.CVTDQ2PD:
            pre, s = self.xsrc(ins, 1, 64)
            return '{%sX86Xmm t_;t_.d[0]=(double)%s.i[0];t_.d[1]=(double)%s.i[1];%s=t_;}' % (pre or '', s, s, dst())
        if mn == M.CVTPD2PS:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.f[0]=(float)%s.d[0];t_.f[1]=(float)%s.d[1];t_.q[1]=0;%s=t_;}' % (pre or '', s, s, dst())
        if mn == M.CVTPS2PD:
            pre, s = self.xsrc(ins, 1, 64)
            return '{%sX86Xmm t_;t_.d[0]=(double)%s.f[0];t_.d[1]=(double)%s.f[1];%s=t_;}' % (pre or '', s, s, dst())
        if mn in (M.CVTTPS2DQ, M.CVTPS2DQ):
            pre, s = self.xsrc(ins, 1, 128)
            conv = 'x86_f2i_trunc' if mn == M.CVTTPS2DQ else 'x86_f2i_round'
            return '{%sX86Xmm t_;for(int i_=0;i_<4;i_++)t_.u[i_]=%s((double)%s.f[i_]);%s=t_;}' % (pre or '', conv, s, dst())
        # shuffles
        if mn == M.SHUFPS:
            imm = ins.immediate(2) & 0xff
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.u[0]=%s.u[%d];t_.u[1]=%s.u[%d];t_.u[2]=%s.u[%d];t_.u[3]=%s.u[%d];%s=t_;}' % (
                pre or '', dst(), imm & 3, dst(), (imm >> 2) & 3, s, (imm >> 4) & 3, s, (imm >> 6) & 3, dst())
        if mn == M.PSHUFD:
            imm = ins.immediate(2) & 0xff
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.u[0]=%s.u[%d];t_.u[1]=%s.u[%d];t_.u[2]=%s.u[%d];t_.u[3]=%s.u[%d];%s=t_;}' % (
                pre or '', s, imm & 3, s, (imm >> 2) & 3, s, (imm >> 4) & 3, s, (imm >> 6) & 3, dst())
        if mn in (M.PSHUFLW, M.PSHUFHW):
            imm = ins.immediate(2) & 0xff
            pre, s = self.xsrc(ins, 1, 128)
            o = 0 if mn == M.PSHUFLW else 4
            sel = ''.join('t_.w[%d]=%s.w[%d];' % (o + i, s, o + ((imm >> (2 * i)) & 3)) for i in range(4))
            return '{%sX86Xmm t_=%s;%s%s=t_;}' % (pre or '', s, sel, dst())
        if mn in (M.UNPCKLPS, M.PUNPCKLDQ):
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.u[0]=%s.u[0];t_.u[1]=%s.u[0];t_.u[2]=%s.u[1];t_.u[3]=%s.u[1];%s=t_;}' % (pre or '', dst(), s, dst(), s, dst())
        if mn in (M.UNPCKHPS, M.PUNPCKHDQ):
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.u[0]=%s.u[2];t_.u[1]=%s.u[2];t_.u[2]=%s.u[3];t_.u[3]=%s.u[3];%s=t_;}' % (pre or '', dst(), s, dst(), s, dst())
        if mn in (M.UNPCKLPD, M.PUNPCKLQDQ):
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.q[0]=%s.q[0];t_.q[1]=%s.q[0];%s=t_;}' % (pre or '', dst(), s, dst())
        if mn in (M.UNPCKHPD, M.PUNPCKHQDQ):
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sX86Xmm t_;t_.q[0]=%s.q[1];t_.q[1]=%s.q[1];%s=t_;}' % (pre or '', dst(), s, dst())
        if mn in (M.PUNPCKLBW, M.PUNPCKHBW, M.PUNPCKLWD, M.PUNPCKHWD):
            pre, s = self.xsrc(ins, 1, 128)
            if mn in (M.PUNPCKLBW, M.PUNPCKHBW):
                o = 0 if mn == M.PUNPCKLBW else 8
                body = ''.join('t_.b[%d]=%s.b[%d];t_.b[%d]=%s.b[%d];' % (2 * i, dst(), o + i, 2 * i + 1, s, o + i) for i in range(8))
            else:
                o = 0 if mn == M.PUNPCKLWD else 4
                body = ''.join('t_.w[%d]=%s.w[%d];t_.w[%d]=%s.w[%d];' % (2 * i, dst(), o + i, 2 * i + 1, s, o + i) for i in range(4))
            return '{%sX86Xmm t_;%s%s=t_;}' % (pre or '', body, dst())
        if mn in (M.PSRLDQ, M.PSLLDQ):
            n = ins.immediate(1) & 0xff
            if mn == M.PSRLDQ:
                return '{X86Xmm t_;memset(&t_,0,16);for(int i_=0;i_+%d<16;i_++)t_.b[i_]=%s.b[i_+%d];%s=t_;}' % (n, dst(), n, dst())
            return '{X86Xmm t_;memset(&t_,0,16);for(int i_=%d;i_<16;i_++)t_.b[i_]=%s.b[i_-%d];%s=t_;}' % (n, dst(), n, dst())
        lane = {M.PADDB: ('b', 16, '+'), M.PADDW: ('w', 8, '+'), M.PADDD: ('u', 4, '+'), M.PADDQ: ('q', 2, '+'),
                M.PSUBB: ('b', 16, '-'), M.PSUBW: ('w', 8, '-'), M.PSUBD: ('u', 4, '-'), M.PSUBQ: ('q', 2, '-')}
        if mn in lane:
            f, n, op = lane[mn]
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<%d;i_++)%s.%s[i_]=%s.%s[i_]%s%s.%s[i_];}' % (pre or '', n, dst(), f, dst(), f, op, s, f)
        if mn in (M.PCMPEQB, M.PCMPEQW, M.PCMPEQD, M.PCMPGTD):
            f, n = {M.PCMPEQB: ('b', 16), M.PCMPEQW: ('w', 8), M.PCMPEQD: ('u', 4), M.PCMPGTD: ('i', 4)}[mn]
            pre, s = self.xsrc(ins, 1, 128)
            op = '>' if mn == M.PCMPGTD else '=='
            full = {'b': '0xff', 'w': '0xffff', 'u': '0xffffffffu', 'i': '-1'}[f]
            return '{%sfor(int i_=0;i_<%d;i_++)%s.%s[i_]=(%s.%s[i_]%s%s.%s[i_])?%s:0;}' % (pre or '', n, dst(), f, dst(), f, op, s, f, full)
        if mn in (M.PSRLW, M.PSRLD, M.PSRLQ, M.PSLLW, M.PSLLD, M.PSLLQ, M.PSRAW, M.PSRAD) and ins.op1_kind in (OK.IMMEDIATE8,):
            k = ins.immediate(1) & 0xff
            f, n, bits = {M.PSRLW: ('w', 8, 16), M.PSLLW: ('w', 8, 16), M.PSRAW: ('sw', 8, 16), M.PSRLD: ('u', 4, 32), M.PSLLD: ('u', 4, 32),
                          M.PSRAD: ('i', 4, 32), M.PSRLQ: ('q', 2, 64), M.PSLLQ: ('q', 2, 64)}[mn]
            if mn in (M.PSRAW, M.PSRAD):
                k = min(k, bits - 1)
                return 'for(int i_=0;i_<%d;i_++)%s.%s[i_]>>=%d;' % (n, dst(), f, k)
            if k >= bits:
                return 'memset(&%s,0,16);' % dst()
            op = '>>' if mn in (M.PSRLW, M.PSRLD, M.PSRLQ) else '<<'
            return 'for(int i_=0;i_<%d;i_++)%s.%s[i_]%s=%d;' % (n, dst(), f, op, k)
        if mn in (M.PSRLW, M.PSRLD, M.PSRLQ, M.PSLLW, M.PSLLD, M.PSLLQ, M.PSRAW, M.PSRAD):
            # count from an xmm register or memory: its low 64 bits
            f, n, bits = {M.PSRLW: ('w', 8, 16), M.PSLLW: ('w', 8, 16), M.PSRAW: ('sw', 8, 16), M.PSRLD: ('u', 4, 32), M.PSLLD: ('u', 4, 32),
                          M.PSRAD: ('i', 4, 32), M.PSRLQ: ('q', 2, 64), M.PSLLQ: ('q', 2, 64)}[mn]
            pre, s = self.xsrc(ins, 1, 128)
            if mn in (M.PSRAW, M.PSRAD):
                return '{%suint64_t k_=%s.q[0];if(k_>%d)k_=%d;for(int i_=0;i_<%d;i_++)%s.%s[i_]>>=(int)k_;}' % (
                    pre or '', s, bits - 1, bits - 1, n, dst(), f)
            op = '>>' if mn in (M.PSRLW, M.PSRLD, M.PSRLQ) else '<<'
            return '{%suint64_t k_=%s.q[0];if(k_>=%d)memset(&%s,0,16);else for(int i_=0;i_<%d;i_++)%s.%s[i_]%s=(int)k_;}' % (
                pre or '', s, bits, dst(), n, dst(), f, op)
        if mn in (M.PADDUSB, M.PSUBUSB):
            pre, s = self.xsrc(ins, 1, 128)
            e = 'int v_=%s.b[i_]%s%s.b[i_];%s.b[i_]=v_<0?0:v_>255?255:v_;' % (dst(), '+' if mn == M.PADDUSB else '-', s, dst())
            return '{%sfor(int i_=0;i_<16;i_++){%s}}' % (pre or '', e)
        if mn == M.PACKSSWB:
            pre, s = self.xsrc(ins, 1, 128)
            return ('{%sX86Xmm t_;for(int i_=0;i_<8;i_++){int v_=%s.sw[i_];t_.sb[i_]=v_<-128?-128:v_>127?127:v_;v_=%s.sw[i_];t_.sb[i_+8]=v_<-128?-128:v_>127?127:v_;}%s=t_;}'
                    % (pre or '', dst(), s, dst()))
        if mn == M.PCMPGTB:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<16;i_++)%s.b[i_]=(%s.sb[i_]>%s.sb[i_])?0xff:0;}' % (pre or '', dst(), dst(), s)
        if mn == M.PACKUSWB:
            pre, s = self.xsrc(ins, 1, 128)
            return ('{%sX86Xmm t_;for(int i_=0;i_<8;i_++){int v_=%s.sw[i_];t_.b[i_]=v_<0?0:v_>255?255:v_;v_=%s.sw[i_];t_.b[i_+8]=v_<0?0:v_>255?255:v_;}%s=t_;}'
                    % (pre or '', dst(), s, dst()))
        if mn == M.PMULHW:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<8;i_++)%s.sw[i_]=(int16_t)(((int32_t)%s.sw[i_]*%s.sw[i_])>>16);}' % (pre or '', dst(), dst(), s)
        if mn == M.PMULHUW:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<8;i_++)%s.w[i_]=(uint16_t)(((uint32_t)%s.w[i_]*%s.w[i_])>>16);}' % (pre or '', dst(), dst(), s)
        if mn == M.PMULLW:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<8;i_++)%s.w[i_]=(uint16_t)(%s.w[i_]*%s.w[i_]);}' % (pre or '', dst(), dst(), s)
        if mn == M.PADDSW:
            pre, s = self.xsrc(ins, 1, 128)
            return '{%sfor(int i_=0;i_<8;i_++){int v_=%s.sw[i_]+%s.sw[i_];%s.sw[i_]=v_<-32768?-32768:v_>32767?32767:v_;}}' % (pre or '', dst(), s, dst())
        if mn == M.STMXCSR:
            return 'WR32(%s,c->mxcsr);' % self.ea(ins)
        if mn == M.LDMXCSR:
            return 'c->mxcsr=RD32(%s);' % self.ea(ins)
        if name in ('PMULLD', 'PMAXSD', 'PABSD', 'PSHUFB', 'PALIGNR', 'PMADDUBSW', 'PCMPEQW', 'PSLLW', 'PSRLW'):
            return 'x86_unimpl(c,0x%xu,"%s");' % (va, name)      # SSSE3/SSE4 paths behind CPUID
        return None

    # ------------------------------------------------------------------ x87
    def fmem(self, ins):
        ms = MEMSIZE.get(ins.memory_size)
        a = self.ea(ins)
        return ms, a

    def x87(self, va, ins, name, cname):
        mn = ins.mnemonic
        def sti(i):
            r = ins.op_register(i)
            return int(RNAME[r][2:])
        memop = ins.op_count >= 1 and ins.op0_kind == OK.MEMORY
        if mn == M.FLD:
            if memop:
                n, a = self.fmem(ins)
                if n == 4: return 'X87_PUSH(c,RDF(%s));' % a
                if n == 8: return 'X87_PUSH(c,RDD(%s));' % a
                return 'X87_PUSH(c,RDF80(%s));' % a
            return '{double v_=ST(%d);X87_PUSH(c,v_);}' % sti(0)
        if mn in (M.FILD,):
            n, a = self.fmem(ins)
            t = {2: '(int16_t)RD16(%s)', 4: '(int32_t)RD32(%s)', 8: '(int64_t)RD64(%s)'}[n] % a
            return 'X87_PUSH(c,(double)%s);' % t
        if mn in (M.FST, M.FSTP):
            pop = 'X87_POP(c);' if mn == M.FSTP else ''
            if memop:
                n, a = self.fmem(ins)
                if n == 4: return 'WRF(%s,(float)ST(0));%s' % (a, pop)
                if n == 8: return 'WRD(%s,ST(0));%s' % (a, pop)
                return 'WRF80(%s,ST(0));%s' % (a, pop)
            return 'ST(%d)=ST(0);%s' % (sti(0), pop)
        if mn in (M.FIST, M.FISTP, M.FISTTP):
            n, a = self.fmem(ins)
            pop = 'X87_POP(c);' if mn != M.FIST else ''
            conv = 'x86_x87_round(c,ST(0))' if mn != M.FISTTP else 'trunc(ST(0))'
            w = {2: 'WR16(%s,x86_fist16(%s));', 4: 'WR32(%s,x86_fist32(%s));', 8: 'WR64(%s,x86_fist64(%s));'}[n] % (a, conv)
            return w + pop
        if mn in (M.FLD1, M.FLDZ, M.FLDPI, M.FLDL2E, M.FLDLN2, M.FLDL2T, M.FLDLG2):
            v = {M.FLD1: '1.0', M.FLDZ: '0.0', M.FLDPI: '3.14159265358979323846', M.FLDL2E: '1.44269504088896340736',
                 M.FLDLN2: '0.693147180559945309417', M.FLDL2T: '3.32192809488736234787', M.FLDLG2: '0.301029995663981195214'}[mn]
            return 'X87_PUSH(c,%s);' % v
        arith = {M.FADD: '+', M.FMUL: '*', M.FSUB: '-', M.FDIV: '/', M.FSUBR: 'r-', M.FDIVR: 'r/',
                 M.FADDP: '+', M.FMULP: '*', M.FSUBP: '-', M.FDIVP: '/', M.FSUBRP: 'r-', M.FDIVRP: 'r/',
                 M.FIADD: '+', M.FIMUL: '*', M.FISUB: '-', M.FIDIV: '/', M.FISUBR: 'r-', M.FIDIVR: 'r/'}
        if mn in arith:
            op = arith[mn]
            rev = op.startswith('r')
            op = op[-1]
            if memop:
                n, a = self.fmem(ins)
                if mn in (M.FIADD, M.FIMUL, M.FISUB, M.FIDIV, M.FISUBR, M.FIDIVR):
                    v = {2: '(double)(int16_t)RD16(%s)', 4: '(double)(int32_t)RD32(%s)'}[n] % a
                else:
                    v = {4: '(double)RDF(%s)', 8: 'RDD(%s)'}[n] % a
                return 'ST(0)=%s;' % (('%s%s ST(0)' % (v, op)) if rev else ('ST(0)%s%s' % (op, v)))
            pop = mn in (M.FADDP, M.FMULP, M.FSUBP, M.FDIVP, M.FSUBRP, M.FDIVRP)
            if ins.op_count == 0:
                d, s = 1, 0
            else:
                d, s = sti(0), sti(1)
            expr = ('ST(%d)%sST(%d)' % (s, op, d)) if rev else ('ST(%d)%sST(%d)' % (d, op, s))
            return 'ST(%d)=%s;%s' % (d, expr, 'X87_POP(c);' if pop else '')
        if mn == M.FCHS: return 'ST(0)=-ST(0);'
        if mn == M.FABS: return 'ST(0)=fabs(ST(0));'
        if mn == M.FSQRT: return 'ST(0)=sqrt(ST(0));'
        if mn == M.FSIN: return 'ST(0)=sin(ST(0));'
        if mn == M.FCOS: return 'ST(0)=cos(ST(0));'
        if mn == M.FSINCOS: return '{double v_=ST(0);ST(0)=sin(v_);X87_PUSH(c,cos(v_));}'
        if mn == M.FPTAN: return 'ST(0)=tan(ST(0));X87_PUSH(c,1.0);'
        if mn == M.FPATAN: return '{double y_=ST(1),x_=ST(0);X87_POP(c);ST(0)=atan2(y_,x_);}'
        if mn == M.FRNDINT: return 'ST(0)=x86_x87_round(c,ST(0));'
        if mn == M.FSCALE: return 'ST(0)=ldexp(ST(0),(int)trunc(ST(1)));'
        if mn == M.F2XM1: return 'ST(0)=exp2(ST(0))-1.0;'
        if mn == M.FYL2X: return '{double x_=ST(0);X87_POP(c);ST(0)=ST(0)*log2(x_);}'
        if mn == M.FPREM or mn == M.FPREM1: return 'ST(0)=fmod(ST(0),ST(1));c->fsw&=~0x400;'
        if mn == M.FXCH:
            i = sti(1) if ins.op_count == 2 else 1
            return '{double t_=ST(0);ST(0)=ST(%d);ST(%d)=t_;}' % (i, i)
        if mn in (M.FCOM, M.FCOMP, M.FCOMPP, M.FUCOM, M.FUCOMP, M.FUCOMPP, M.FICOM, M.FICOMP, M.FTST):
            if mn == M.FTST:
                v = '0.0'
            elif memop:
                n, a = self.fmem(ins)
                if mn in (M.FICOM, M.FICOMP):
                    v = {2: '(double)(int16_t)RD16(%s)', 4: '(double)(int32_t)RD32(%s)'}[n] % a
                else:
                    v = {4: '(double)RDF(%s)', 8: 'RDD(%s)'}[n] % a
            elif ins.op_count == 0:
                v = 'ST(1)'
            else:
                v = 'ST(%d)' % sti(1)
            pops = {M.FCOMP: 1, M.FUCOMP: 1, M.FICOMP: 1, M.FCOMPP: 2, M.FUCOMPP: 2}.get(mn, 0)
            return '{double a_=ST(0),b_=%s;c->fsw=(c->fsw&~0x4500)|((a_!=a_||b_!=b_)?0x4500:(a_<b_)?0x100:(a_==b_)?0x4000:0);%s}' % (
                v, 'X87_POP(c);' * pops)
        if mn in (M.FCOMI, M.FCOMIP, M.FUCOMI, M.FUCOMIP):
            i = sti(1)
            pop = 'X87_POP(c);' if mn in (M.FCOMIP, M.FUCOMIP) else ''
            return '{double a_=ST(0),b_=ST(%d);if(a_!=a_||b_!=b_){zf=pf=cf=1;}else{zf=a_==b_;pf=0;cf=a_<b_;}of=sf=af=0;%s}' % (i, pop)
        if mn == M.FNSTSW:
            v = '(uint16_t)((c->fsw&~0x3800)|((c->top&7)<<11))'
            if ins.op0_kind == OK.REGISTER:
                return 'eax=(eax&0xffff0000u)|%s;' % v
            return 'WR16(%s,%s);' % (self.ea(ins), v)
        if mn == M.FNSTCW:
            return 'WR16(%s,c->fcw);' % self.ea(ins)
        if mn == M.FLDCW:
            return 'c->fcw=RD16(%s);' % self.ea(ins)
        if mn in (M.FNINIT, M.FNCLEX):
            return 'c->fsw=0;' if mn == M.FNCLEX else 'c->fsw=0;c->fcw=0x27f;c->top=0;'
        if mn == M.FFREE or mn == M.FDECSTP or mn == M.FINCSTP:
            return {M.FFREE: '', M.FDECSTP: 'c->top=(c->top-1)&7;', M.FINCSTP: 'c->top=(c->top+1)&7;'}[mn]
        return None

    # ------------------------------------------------------------------ fusion of cmp/test with the next consumer
    def find_fusions(self):
        self.fusable = set()
        self.fused = {}
        for va in self.order:
            ins = self.insns[va]
            if ins is None or ins.mnemonic not in (M.CMP, M.TEST):
                continue
            nxt = va + ins.len
            if self.succ.get(va) != [nxt]:
                continue
            nins = self.insns.get(nxt)
            if nins is None or nxt in self.labels:
                continue
            nm = MNAME[nins.mnemonic]
            cc = cond_of(nm) if nins.flow_control == FC.CONDITIONAL_BRANCH or nm.startswith(('SET', 'CMOV')) else None
            if not cc or nm in ('JECXZ', 'JCXZ') or nm.startswith('LOOP'):
                continue
            if self.live_out[nxt] & rf_to_mask(ins.rflags_modified):
                continue                                       # the flags are read again later
            try:
                n = self.size_of(ins)
                a, b = self.get(ins, 0, n), self.get(ins, 1, n)
            except Unhandled:
                continue
            S = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}[n]
            U = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[n]
            if ins.mnemonic == M.CMP:
                A, B = '(%s)(%s)' % (U, a), '(%s)(%s)' % (U, b)
                SA, SB = '(%s)%s' % (S, A), '(%s)%s' % (S, B)
                e = {'E': '%s==%s' % (A, B), 'NE': '%s!=%s' % (A, B), 'B': '%s<%s' % (A, B), 'AE': '%s>=%s' % (A, B),
                     'BE': '%s<=%s' % (A, B), 'A': '%s>%s' % (A, B), 'L': '%s<%s' % (SA, SB), 'GE': '%s>=%s' % (SA, SB),
                     'LE': '%s<=%s' % (SA, SB), 'G': '%s>%s' % (SA, SB),
                     'S': '(%s)(%s-%s)<0' % (S, A, B), 'NS': '(%s)(%s-%s)>=0' % (S, A, B)}.get(cc)
            else:
                X = '(%s)((%s)&(%s))' % (U, a, b)
                SX = '(%s)%s' % (S, X)
                e = {'E': '%s==0' % X, 'NE': '%s!=0' % X, 'S': '%s<0' % SX, 'NS': '%s>=0' % SX, 'L': '%s<0' % SX,
                     'GE': '%s>=0' % SX, 'LE': '%s<=0' % SX, 'G': '%s>0' % SX, 'BE': '%s==0' % X, 'A': '%s!=0' % X,
                     'B': '0', 'AE': '1'}.get(cc)
            if e is None:
                continue
            self.fusable.add(va)
            self.fused[nxt] = '(' + e + ')'

    # ------------------------------------------------------------------ the whole function
    def translate(self):
        self.discover()
        self.liveness()
        self.find_fusions()
        name = self.tr.func_name(self.start)
        body = []
        unhandled = Counter()
        prev_falls = None
        for va in self.order:
            ins = self.insns[va]
            if prev_falls is not None and prev_falls != va:
                body.append('goto L_%x;' % prev_falls if prev_falls in self.insns and self.insns[prev_falls] is not None
                            else 'x86_unreachable(c,0x%xu);' % prev_falls)
            prev_falls = None
            body.append('L_%x:' % va)
            if ins is None:
                body.append('x86_unreachable(c,0x%xu);' % va)
                continue
            self.out = []
            try:
                self.translate_insn(va, ins, va + ins.len)
            except Unhandled as e:
                unhandled[str(e)] += 1
                self.out = ['x86_unimpl(c,0x%xu,"%s");' % (va, str(e).replace('"', ''))]
            # lock-prefixed read-modify-writes (and xchg with memory, locked on x86) under the runtime's one guest
            # lock, which also orders memory as x86 does around them (the console's PowerPC orders more weakly)
            if ins.has_lock_prefix or (ins.mnemonic == M.XCHG and any(
                    ins.op_kind(i) == OK.MEMORY for i in range(ins.op_count))):
                self.out = ['x86_lock();'] + self.out + ['x86_unlock();']
            body.append(''.join(self.out))
            succ = self.succ.get(va, [])
            fc = ins.flow_control
            if fc in (FC.NEXT, FC.CALL, FC.INDIRECT_CALL, FC.CONDITIONAL_BRANCH) and succ and not isinstance(succ[0], tuple):
                prev_falls = va + ins.len
            elif succ and isinstance(succ[0], tuple) and fc == FC.NEXT:
                n = self.tr.func_name(succ[0][1])
                body.append('SPILL();%s(c);return;' % n)
        if prev_falls is not None:
            body.append('goto L_%x;' % prev_falls if prev_falls in self.insns and self.insns[prev_falls] is not None
                        else 'x86_unreachable(c,0x%xu);' % prev_falls)
        text = '\n'.join(body)
        refs = set(int(x, 16) for x in re.findall(r'L_([0-9a-f]+);', text)) | {self.start}
        # drop labels nobody jumps to (keeps compilers quiet and the files smaller)
        lines = []
        for line in body:
            if line.startswith('L_') and line.endswith(':'):
                va = int(line[2:-1], 16)
                if va not in refs:
                    continue
            lines.append(line)
        # code below the entry (a shared tail reached by a jump) comes first in address order: enter at the start
        entry = '' if self.order and self.order[0] == self.start else 'goto L_%x;\n' % self.start
        if self.start in TRACE:
            entry = 'x86_trace_enter(c,0x%xu);\n' % self.start + entry
        src = 'void %s(X86* c){REGS();\n%s%s\n}\n' % (name, entry, '\n'.join(lines))
        return src, unhandled


MEMSIZE = {}


def init_memsize():
    for n, v in vars(ix.MemorySize).items():
        if n.startswith('_'):
            continue
        size = None
        if n in ('UINT8', 'INT8'): size = 1
        elif n in ('UINT16', 'INT16', 'WORD_OFFSET'): size = 2
        elif n in ('UINT32', 'INT32', 'DWORD_OFFSET', 'FLOAT32', 'SEG_PTR16'): size = 4
        elif n in ('UINT64', 'INT64', 'FLOAT64', 'QWORD_OFFSET', 'SEG_PTR32'): size = 8
        elif n in ('FLOAT80', 'BCD', 'PACKED80'): size = 10
        elif n in ('UINT128', 'PACKED128_FLOAT32', 'PACKED128_FLOAT64', 'PACKED128_UINT8'): size = 16
        if size:
            MEMSIZE[v] = size


class Translator:
    def __init__(self):
        init_memsize()
        self.img = Img()
        p = json.load(open(PROG))
        self.p = p
        self.text_lo, self.text_hi = p['text']
        self.funcs = {int(k, 16) for k in p['functions']}
        self.jump_tables = {int(k, 16): v for k, v in p['jump_tables'].items()}
        self.imports = {int(k, 16): v for k, v in p['imports'].items()}
        self.noreturn_calls = set(p['noreturn_calls'])
        self.noreturn_funcs = set()
        self.table_bytes = set(p.get('table_bytes', []))
        self.code = self.img.data[self.text_lo - self.img.base:self.text_hi - self.img.base]

    def in_text(self, va):
        return self.text_lo <= va < self.text_hi

    def decode(self, va):
        o = va - self.text_lo
        return ix.Decoder(32, self.code[o:o + 16], ip=va).decode()

    def func_name(self, va):
        return 'F_%x' % va if va in self.funcs else None


PRELUDE = '''#include "x86.h"
#define REGS() uint32_t eax=c->r[0],ecx=c->r[1],edx=c->r[2],ebx=c->r[3],esp=c->r[4],ebp=c->r[5],esi=c->r[6],edi=c->r[7];\\
    uint8_t cf=c->cf,pf=c->pf,af=c->af,zf=c->zf,sf=c->sf,of=c->of;(void)cf;(void)pf;(void)af;(void)zf;(void)sf;(void)of
#define SPILL() (c->r[0]=eax,c->r[1]=ecx,c->r[2]=edx,c->r[3]=ebx,c->r[4]=esp,c->r[5]=ebp,c->r[6]=esi,c->r[7]=edi,\\
    c->cf=cf,c->pf=pf,c->af=af,c->zf=zf,c->sf=sf,c->of=of)
#define FILL() (eax=c->r[0],ecx=c->r[1],edx=c->r[2],ebx=c->r[3],esp=c->r[4],ebp=c->r[5],esi=c->r[6],edi=c->r[7],\\
    cf=c->cf,pf=c->pf,af=c->af,zf=c->zf,sf=c->sf,of=c->of)
'''

# An XMM register's canonical form is its two 64-bit halves as native integers (RDX/WRX), so narrower lanes are
# found through x86.h's X32/X16/X8, which flip the index within each half on a big-endian host (the console).
LANES = [(re.compile(r'\.(u|i|f)\[(?!X32\()([^\[\]]+)\]'), r'.\1[X32(\2)]'),
         (re.compile(r'\.(w|sw)\[(?!X16\()([^\[\]]+)\]'), r'.\1[X16(\2)]'),
         (re.compile(r'\.(b|sb)\[(?!X8\()([^\[\]]+)\]'), r'.\1[X8(\2)]')]


def lanes(text):
    for pattern, repl in LANES:
        text = pattern.sub(repl, text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='')
    ap.add_argument('--per-file', type=int, default=150)
    args = ap.parse_args()
    tr = Translator()
    starts = sorted(tr.funcs)
    if args.only:
        starts = [int(x, 16) for x in args.only.split(',')]
    os.makedirs(OUT, exist_ok=True)
    total = Counter()
    files = []
    decls = []
    chunk = []
    nfile = 0

    def flush():
        nonlocal chunk, nfile
        if not chunk:
            return
        path = os.path.join(OUT, 'f%03d.c' % nfile)
        text = (PRELUDE + '#include "decls.h"\n' + lanes(''.join(chunk))).encode()
        # unchanged files keep their time stamps, so the build recompiles only what changed
        if not os.path.exists(path) or open(path, 'rb').read() != text:
            with open(path, 'wb') as fp:
                fp.write(text)
        files.append(path)
        nfile += 1
        chunk = []

    for i, s in enumerate(starts):
        src, unh = Fn(tr, s).translate()
        total.update(unh)
        chunk.append(src)
        decls.append('void F_%x(X86* c);' % s)
        if len(chunk) >= args.per_file:
            flush()
        if i % 2000 == 0:
            print('%d/%d' % (i, len(starts)), file=sys.stderr)
    flush()
    # files from an earlier, larger translation would define functions twice
    for name in os.listdir(OUT):
        if name.startswith('f') and name.endswith('.c') and name[1:-2].isdigit() and int(name[1:-2]) >= nfile:
            os.remove(os.path.join(OUT, name))
    with open(os.path.join(OUT, 'decls.h'), 'w', newline='\n') as fp:
        fp.write('#pragma once\n' + '\n'.join('void F_%x(X86* c);' % s for s in sorted(tr.funcs)) + '\n')
    with open(os.path.join(OUT, 'functions.c'), 'w', newline='\n') as fp:
        fp.write('#include "x86.h"\n#include "decls.h"\n')
        fp.write('const X86FuncEntry x86_functions%s[]={\n' % SUFFIX)
        fp.write(''.join('{0x%xu,F_%x},\n' % (s, s) for s in sorted(tr.funcs)))
        fp.write('};\nconst uint32_t x86_function_count%s=%d;\n' % (SUFFIX, len(tr.funcs)))
    with open(os.path.join(OUT, 'unhandled.txt'), 'w') as fp:
        for k, v in total.most_common():
            fp.write('%6d %s\n' % (v, k))
    print('files %d, unhandled forms: %s' % (len(files), ', '.join('%s:%d' % kv for kv in total.most_common(25))))


if __name__ == '__main__':
    main()
