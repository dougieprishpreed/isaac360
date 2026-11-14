"""Register flow across calls, for translate.py: what each function reads on entry (LIVE_IN) and may leave changed
(CLOBBER), so a call saves to the CPU state only what its callee can read or change and reloads only what it changed
and the caller reads after. Every call saved all eight registers and six flags and reloaded them: 379,000 sites in
the game's code, a measured ~27% of the translated code (the console holds all 95 MB of it in memory).

- Flags: translate.py's flag liveness already takes every call as leaving the flags undefined (a callee's flags are
  never read after it returns), so no call reloads them; a callee that reads one on entry still gets it.
- A return still writes all eight registers back: a function's own calls leave other values in the CPU state, and
  a caller reloads from there what the function's tail callee changed.
- Registers a function saves and restores are not changed by it, and the saving push is not a read: a caller that
  did not save the value gets a stale one pushed and popped back, and keeps its own. Followed along every path (the
  compiler often saves one only after an early exit): a register is saved by a push while it still holds its entry
  value, restored by a pop after that, and preserved when it holds its entry value at every exit. A push of a
  register the function neither changes nor pops back on every path is a read (an argument passed on), not a save.
- The compiler's frame helpers (they write the exception chain, fs:[0]) save and restore the caller's registers for
  it: __SEH_prolog4 pushes ebx, esi and edi and stores ebp, its epilogue pops them. A leaf helper's pushes and
  register stores are saves for its caller; a helper's pops of registers it never pushed restore them for its
  caller (and a function that only tail-calls one does too); the caller's flow takes the call as that save or pop.
- A function that never returns, or captures the registers for a crash report (it stores the segment registers:
  the cookie check's failure report), reads only eax, ecx and edx as far as its callers go.
- A callee's writes that do not depend on what the register held (MUSTW: on every path, a mov, lea, xor r,r...)
  end the caller's value: what the caller held there before the call is not read by it.
- An indirect call or an import (a virtual call, a callback, another image's export, a shim) saves and reloads the
  32-bit conventions' argument and scratch registers, eax, ecx and edx: what the compiler assumed of any callee it
  did not know. report() lists the address-taken functions whose own flow reaches further.
- A function the translator patches (PATCHES) is opaque: it reads and changes everything, and its own calls save
  and reload everything.
- Each register is three lanes (bits 0-7, 8-15, 16-31), so a byte write (xor bl,bl) is not a read of the rest;
  calls still save and reload whole registers, any of whose lanes the callee reads or changes.
The sets are a fixpoint over the call graph from empty ones (they only grow; the saved registers only shrink).
"""
import iced_x86 as ix

R = ix.Register
M = ix.Mnemonic
OK = ix.OpKind
FC = ix.FlowControl
A = ix.OpAccess

EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI = (1 << i for i in range(8))
ALLR = 0xff
REGS = ALLR & ~ESP
ALLF = 0x3f
REGNAME = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
GPR = {R.EAX: EAX, R.ECX: ECX, R.EDX: EDX, R.EBX: EBX, R.ESP: ESP, R.EBP: EBP, R.ESI: ESI, R.EDI: EDI}
SAVABLE = {R.EBX: EBX, R.ESI: ESI, R.EDI: EDI, R.EBP: EBP}
IND = EAX | ECX | EDX
FACT = ix.InstructionInfoFactory()
HIGH8 = (R.AH, R.CH, R.DH, R.BH)


def lanes(regs):
    """A register mask's lanes: register i's are bits 3i (0-7), 3i+1 (8-15), 3i+2 (16-31)."""
    m = 0
    for i in range(8):
        if regs >> i & 1:
            m |= 7 << (3 * i)
    return m


def regs_of(ln):
    """The registers any of whose lanes are in ln."""
    m = 0
    for i in range(8):
        if ln >> (3 * i) & 7:
            m |= 1 << i
    return m


INDL, ESPL, REGSL = lanes(IND), lanes(ESP), lanes(REGS)


def effects(ins):
    """The register lanes an instruction reads and writes; a conditional write may keep the old value, so it reads."""
    rd = wr = 0
    for u in FACT.info(ins).used_registers():
        b = GPR.get(ix.RegisterExt.full_register32(u.register))
        if not b:
            continue
        i = b.bit_length() - 1
        size = ix.RegisterExt.size(u.register)
        ln = (7 if size == 4 else 3 if size == 2 else 2 if u.register in HIGH8 else 1) << (3 * i)
        a = u.access
        if a in (A.READ, A.COND_READ):
            rd |= ln
        elif a == A.WRITE:
            wr |= ln
        elif a in (A.READ_WRITE, A.READ_COND_WRITE, A.COND_WRITE):
            rd |= ln
            wr |= ln
    k = constant_write(ins)
    if k:
        rd &= ~k
    return rd, wr


def constant_write(ins):
    """The lanes of a register an instruction sets whatever it held: or r,-1, and r,0, sbb r,r (-CF) and the
    register forms iced already takes as writes alone (xor r,r, sub r,r)."""
    if ins.op_count != 2 or ins.op0_kind != OK.REGISTER or not ix.RegisterExt.is_gpr32(ins.op0_register):
        return 0
    r0, mn = ins.op0_register, ins.mnemonic
    imm = ins.immediate(1) & 0xffffffff if ins.op1_kind in (OK.IMMEDIATE32, OK.IMMEDIATE8TO32) else None
    if (mn == M.OR and imm == 0xffffffff) or (mn == M.AND and imm == 0) or \
            (mn in (M.SBB, M.XOR, M.SUB) and ins.op1_kind == OK.REGISTER and ins.op1_register == r0):
        return lanes(GPR[r0])
    return 0


class Flow:
    """One function's instructions reduced to register effects and call kinds (from a discovered translate.Fn)."""

    def __init__(self, fn, tr, flags_of, opaque, hooked=False):
        self.start = fn.start
        self.order = fn.order
        self.opaque = opaque
        self.hooked = hooked                        # a host function may take its call (translate.py HOST)
        self.rd, self.wr, self.kind, self.succ, self.frd, self.fwr = {}, {}, {}, {}, {}, {}
        self.push, self.pop, self.leave, self.store = {}, {}, set(), {}
        self.seh = False
        self.capture = False
        self.safe_wr = {}                           # lanes written with a constant, a stack address or xor r,r
        self.callees, self.tails = set(), set()
        self.xmm = 0                                # the XMM registers its instructions name (translate.py xmm_reach)
        for va in fn.order:
            ins = fn.insns[va]
            succ = fn.succ.get(va, [])
            self.succ[va] = [s for s in succ if not isinstance(s, tuple)]
            self.kind[va] = None
            if ins is None:
                self.rd[va] = self.wr[va] = self.frd[va] = self.fwr[va] = 0
                continue
            for i in range(ins.op_count):
                if ins.op_kind(i) == OK.REGISTER and ix.RegisterExt.is_xmm(ins.op_register(i)):
                    self.xmm |= 1 << (ins.op_register(i) - R.XMM0)
            self.rd[va], self.wr[va] = effects(ins)
            self.frd[va], self.fwr[va] = flags_of(ins)
            kind = None
            mn = ins.mnemonic
            if mn == M.RET:
                kind = ('ret',)
            elif mn == M.CALL:
                t = ins.near_branch_target if ins.op0_kind in (OK.NEAR_BRANCH32, OK.NEAR_BRANCH16) else None
                kind = ('call', t) if t is not None and t in tr.funcs else ('icall',)
            elif mn == M.JMP:
                if succ and isinstance(succ[0], tuple):
                    t = succ[0][1]
                    kind = ('tail', t) if t in tr.funcs else ('itail',)
                elif ins.op0_kind not in (OK.NEAR_BRANCH32, OK.NEAR_BRANCH16) and va not in tr.jump_tables:
                    kind = ('itail',)
            elif succ and isinstance(succ[0], tuple):
                kind = ('fall', succ[0][1])           # the instruction, then the next function's entry
            self.kind[va] = kind
            if kind and kind[0] == 'call':
                self.callees.add(kind[1])
            elif kind and kind[0] in ('tail', 'fall'):
                self.tails.add(kind[1])
            if ins.op_count and ins.op0_kind == OK.REGISTER and ins.op0_register in SAVABLE:
                if mn == M.PUSH:
                    self.push[va] = SAVABLE[ins.op0_register]
                elif mn == M.POP:
                    self.pop[va] = SAVABLE[ins.op0_register]
            if mn == M.LEAVE:
                self.leave.add(va)
            if ins.op_count >= 2 and ins.op0_kind == OK.REGISTER and ix.RegisterExt.is_gpr32(ins.op0_register):
                r0 = ins.op0_register
                if (mn == M.MOV and ins.op1_kind in (OK.IMMEDIATE32, OK.IMMEDIATE8TO32)) or \
                        (mn == M.MOV and ins.op1_kind == OK.REGISTER and ins.op1_register == R.ESP) or \
                        (mn == M.LEA and ins.memory_base == R.ESP and ins.memory_index == R.NONE) or \
                        constant_write(ins):
                    self.safe_wr[va] = lanes(GPR[r0])
            if mn == M.MOV and ins.op0_kind == OK.MEMORY and ins.op1_kind == OK.REGISTER and \
                    ix.RegisterExt.is_segment_register(ins.op1_register):
                self.capture = True
            if mn == M.MOV and ins.op0_kind == OK.MEMORY:
                if ins.memory_segment == R.FS and ins.memory_base == R.NONE and ins.memory_displacement == 0:
                    self.seh = True
                elif ins.op1_kind == OK.REGISTER and ins.op1_register in SAVABLE:
                    self.store[va] = SAVABLE[ins.op1_register]
        self.written = 0
        for va in self.order:
            self.written |= self.wr[va]
        self.written &= ~ESPL
        self.has_icall = any(k and k[0] == 'icall' for k in self.kind.values())
        self.has_itail = any(k and k[0] == 'itail' for k in self.kind.values())
        self.leaf = not (self.callees or self.tails or self.has_icall or self.has_itail)
        self.written_np = 0                         # written other than by the pops that restore saved registers
        for va in self.order:
            if va not in self.pop and va not in self.leave:
                self.written_np |= self.wr[va]
        self.pred = {}
        for va in self.order:
            for t in self.succ[va]:
                self.pred.setdefault(t, []).append(va)
        self.exits = [va for va in self.order if self.kind[va] and self.kind[va][0] in ('ret', 'tail', 'itail', 'fall')]


class RegFlow:
    def __init__(self):
        self.flows = {}
        self.li, self.cl, self.fin, self.sv = {}, {}, {}, {}
        self.live_out = {}
        self.leaves, self.restores = {}, {}         # registers a frame helper saves / restores for its caller
        self.mw = {}                                # lanes f writes on every path, whatever they held (MUSTW)

    def add(self, flow):
        self.flows[flow.start] = flow

    def changes(self, f, va):
        """What the instruction at va may change: its own writes, or a call's callee's."""
        k = f.kind[va]
        if k and k[0] == 'call':
            return f.wr[va] | self.cl.get(k[1], 0)
        if k and k[0] == 'icall':
            return f.wr[va] | INDL
        return f.wr[va]

    def saved(self, f):
        """Registers f preserves (its entry value at every exit) and the reads that only save them: (mask, {address:
        register}); for a frame helper also what it saves or restores for its caller (self.leaves, self.restores).
        Per register and path: CLEAN (its entry value, never pushed), SAVED (pushed while it held its entry value,
        here or by a helper), RESTORED (popped back after that), DIRTY (changed before any save, or popped unsaved),
        MIXED (paths disagree: pushed on one, not on another); `holds`: it has its entry value now; `popped`: a pop of
        it happened on some path; `foreign`: popped unsaved (the caller's value) and kept since."""
        if f.opaque:
            self.leaves[f.start], self.restores[f.start] = 0, 0
            return 0, {}
        CLEAN, SAVED, RESTORED, MIXED, DIRTY = 0, 1, 2, 3, 4
        regs = (EBX, EBP, ESI, EDI)
        helper = f.seh and f.leaf
        state_in = {f.start: ((CLEAN,) * 4, 0xff, 0, 0)}
        out = {}
        saves, stores = {}, {}
        fragment = False                    # pops a register it never pushed: another function's epilogue
        work = [f.start]

        def meet(a, b):
            if a == b:
                return a
            if DIRTY in (a, b):
                return DIRTY
            if {a, b} == {CLEAN, RESTORED}:
                return RESTORED                          # neither path holds a save on the stack
            return MIXED

        while work:
            va = work.pop()
            st, holds, popped, foreign = state_in[va]
            st = list(st)
            push, pop = f.push.get(va, 0), f.pop.get(va, EBP if va in f.leave else 0)
            k = f.kind[va]
            g = k[1] if k and k[0] == 'call' else None
            hpush = self.leaves.get(g, 0) if g is not None else 0
            hpop = self.restores.get(g, 0) if g is not None else 0
            ch = regs_of(self.changes(f, va))
            for i, b in enumerate(regs):
                if helper and f.store.get(va) == b and st[i] == CLEAN:
                    stores[va] = b
                if push == b or hpush & b:
                    if st[i] in (CLEAN, RESTORED) and holds & b:
                        st[i] = SAVED
                        if push == b:
                            saves[va] = b
                    if hpush & b and ch & b:
                        holds &= ~b
                elif pop == b or hpop & b:
                    popped |= b
                    if st[i] == SAVED:
                        st[i] = RESTORED
                        holds |= b
                    else:
                        if st[i] == CLEAN:
                            foreign |= b
                            fragment = True
                        holds &= ~b
                        st[i] = DIRTY
                elif ch & b:
                    holds &= ~b
                    foreign &= ~b
                    if st[i] != SAVED:
                        st[i] = DIRTY
            new = (tuple(st), holds, popped, foreign)
            if out.get(va) == new:
                continue
            out[va] = new
            for t in f.succ[va]:
                old = state_in.get(t)
                if old is None:
                    state_in[t] = new
                else:
                    m = (tuple(meet(a, b) for a, b in zip(old[0], new[0])), old[1] & new[1], old[2] | new[2],
                         old[3] & new[3])
                    if m == old:
                        continue
                    state_in[t] = m
                work.append(t)
        keep, left, back, forward, popped_any, pop_every, reached = 0xff, 0xff, 0xff, 0xff, 0, 0xff, False
        for va in f.exits:
            if va not in out:
                continue
            reached = True
            k = f.kind[va]
            st, holds, popped, foreign = out[va] if k[0] == 'fall' else state_in[va]
            keep &= holds
            popped_any |= popped
            back &= foreign
            clean = 0
            for i, b in enumerate(regs):
                if st[i] in (SAVED, MIXED):                 # a push still on the stack on some path here
                    pop_every &= ~b
                if st[i] != SAVED:
                    left &= ~b
                if st[i] == CLEAN:
                    clean |= b
            forward &= (self.restores.get(k[1], 0) & clean) if k[0] == 'tail' else 0
        pres = leaves = 0
        for i, b in enumerate(regs):
            if keep & b:
                pres |= b
            if helper and reached and left & b and keep & b and not popped_any & b:
                leaves |= b
        for b in stores.values():
            leaves |= b
        self.leaves[f.start] = leaves
        self.restores[f.start] = (back if f.seh else forward) & (EBX | EBP | ESI | EDI) if reached else 0
        # a push saves a register the function changes, or one it pops back on every path that pushed it; else it
        # passes the value on; a helper's saves are its caller's
        changed = regs_of(f.written_np)
        for g in f.callees:
            changed |= regs_of(self.cl.get(g, 0))
        if f.has_icall:
            changed |= IND
        if not reached:
            pop_every = 0
        if fragment and not f.seh:
            # its pushes may be a callee's arguments and its pops the other function's saves (F_6875c7, boot 13's PC
            # check): no pairing, every register it writes is changed
            return 0, {}
        reads = {v: b for v, b in saves.items() if pres & b and (helper or changed & b or pop_every & b)}
        if helper:
            reads.update(stores)
        return pres, reads

    def must_write(self, f):
        """Lanes f writes on every path to every exit with values that do not depend on what they held."""
        if f.opaque:
            return 0
        mw = self.mw
        defined_in = {f.start: 0}
        out = {}
        work = [f.start]
        while work:
            va = work.pop()
            d = defined_in[va]
            k = f.kind[va]
            # only values that cannot be what the caller held (a constant, a stack address, xor r,r): a load, a pop
            # or a callee's other writes may bring a saved entry value back
            if k and k[0] == 'call':
                d = (d & ~self.cl.get(k[1], REGSL)) | mw.get(k[1], REGSL)
            elif k and k[0] == 'icall':
                d &= ~(f.wr[va] | INDL)
            else:
                d = (d & ~f.wr[va]) | f.safe_wr.get(va, 0)
            if out.get(va) == d:
                continue
            out[va] = d
            for t in f.succ[va]:
                old = defined_in.get(t)
                n = d if old is None else old & d
                if n != old:
                    defined_in[t] = n
                    work.append(t)
        res = REGSL
        reached = False
        for va in f.exits:
            if va not in out:
                continue
            reached = True
            k = f.kind[va]
            d = out[va] if k[0] == 'fall' else defined_in[va]
            if k[0] in ('tail', 'fall'):
                d |= mw.get(k[1], REGSL)
            res &= d
        return res if reached else REGSL

    def clobber(self, f, saved):
        c = f.written
        for g in f.callees:
            c |= self.cl.get(g, 0)
        if f.has_icall:
            c |= INDL
        c &= ~lanes(saved)
        for g in f.tails:
            c |= self.cl.get(g, 0)
        if f.has_itail:
            c |= INDL
        return c

    def solve(self, f, mycl, pushes):
        li = self.li
        live_in, live_out = {}, {}
        changed = True
        order = list(reversed(f.order))
        while changed:
            changed = False
            for va in order:
                out = 0
                for s in f.succ[va]:
                    out |= live_in.get(s, 0)
                k = f.kind[va]
                rd = f.rd[va] & ~lanes(pushes.get(va, 0))
                if k is None:
                    inn = rd | (out & ~f.wr[va])
                elif k[0] == 'call':
                    inn = (out & ~self.mw.get(k[1], REGSL)) | li.get(k[1], 0) | rd
                elif k[0] == 'icall':
                    inn = out | INDL | rd
                elif k[0] == 'ret':
                    out = mycl
                    inn = rd | out
                elif k[0] == 'tail':
                    out = li.get(k[1], 0) | (mycl & ~self.mw.get(k[1], REGSL))
                    inn = rd | out
                elif k[0] == 'itail':
                    out = INDL | mycl
                    inn = rd | out
                else:                                         # 'fall': the instruction, then the tail
                    out = li.get(k[1], 0) | (mycl & ~self.mw.get(k[1], REGSL))
                    inn = rd | (out & ~f.wr[va])
                inn &= ~ESPL
                if live_in.get(va) != inn or live_out.get(va) != out:
                    live_in[va], live_out[va] = inn, out
                    changed = True
        return live_in.get(f.start, 0), live_out

    def flag_in(self, f):
        """Flags f reads on entry (calls leave them undefined, as translate.py's liveness takes them)."""
        live = {}
        changed = True
        while changed:
            changed = False
            for va in reversed(f.order):
                out = 0
                for s in f.succ[va]:
                    out |= live.get(s, 0)
                k = f.kind[va]
                w = ALLF if k and k[0] in ('call', 'icall') else f.fwr[va]
                if k and k[0] in ('ret', 'tail', 'itail'):
                    out = 0
                inn = f.frd[va] | (out & ~w)
                if live.get(va) != inn:
                    live[va] = inn
                    changed = True
        return live.get(f.start, 0)

    def run(self):
        callers = {}
        for s, f in self.flows.items():
            for g in f.callees | f.tails:
                callers.setdefault(g, set()).add(s)
            self.fin[s] = ALLF if f.opaque else self.flag_in(f)
        work = sorted(self.flows)
        queued = set(work)
        steps = 0
        while work:
            s = work.pop()
            queued.discard(s)
            f = self.flows[s]
            steps += 1
            summary = (self.leaves.get(s), self.restores.get(s), self.mw.get(s))
            if f.opaque:
                li, cl, lo, sv = REGSL, REGSL, {}, 0
                self.mw[s] = 0
            else:
                sv, pushes = self.saved(f)
                cl = self.clobber(f, sv)
                self.mw[s] = 0 if f.hooked else self.must_write(f) & cl   # a hook writes what it writes
                li, lo = self.solve(f, cl, pushes)
                if not f.exits or f.capture:
                    li &= INDL
            self.live_out[s] = lo
            self.sv[s] = sv
            if li != self.li.get(s) or cl != self.cl.get(s) or \
                    summary != (self.leaves.get(s), self.restores.get(s), self.mw.get(s)):
                self.li[s], self.cl[s] = li, cl
                for c in callers.get(s, ()):
                    if c not in queued:
                        queued.add(c)
                        work.append(c)
        return steps

    # ---------------------------------------------------------------- what translate.py emits
    def site(self, fstart, va):
        """(registers saved, flags saved, registers reloaded) for the call or exit at va; None: all of them. Even a
        call that saves everything reloads only what its callee changes: a callee's return writes back only that,
        and the rest of the CPU state holds whatever its own calls left there (boot 13's PC check: the opaque archive
        mount, F_950420, reloaded a stale esi and wrote through it)."""
        f = self.flows.get(fstart)
        if f is None:
            return None
        k = f.kind.get(va)
        if k is None:
            return None
        if f.opaque:
            if k[0] == 'call':
                return ALLR, ALLF, regs_of(self.cl.get(k[1], REGSL)) | ESP
            if k[0] == 'icall':
                return ALLR, ALLF, IND | ESP
            return None
        lo = self.live_out[fstart].get(va, 0)
        mycl = self.cl[fstart]
        if k[0] == 'call':
            g = k[1]
            cl = self.cl.get(g, REGSL)
            keep = cl & ~self.mw.get(g, 0) & lo                 # changed on some path only: the old value may stay
            return regs_of(self.li.get(g, REGSL) | keep) | ESP, self.fin.get(g, ALLF), regs_of(cl & lo) | ESP
        if k[0] == 'icall':
            return IND | ESP, 0, regs_of(INDL & lo) | ESP
        if k[0] == 'ret':
            # every register: what the function did not change is its entry value, but its own calls may have left
            # anything in the CPU state, where a caller reloading the changes of a function that tail-called this
            # one finds them (boot 13's PC check: F_9afc40, the tail of F_9b2360, left the poison of an indirect
            # call in ebp, and F_99d8d0 returned with esp from it)
            return ALLR, 0, 0
        if k[0] in ('tail', 'fall'):
            g = k[1]
            return regs_of(self.li.get(g, REGSL) | (mycl & ~self.mw.get(g, 0))) | ESP, self.fin.get(g, ALLF), 0
        return IND | regs_of(mycl) | ESP, 0, 0                 # 'itail'

    def report(self, kinds):
        """Address-taken or exported functions whose flow reaches past the conventions' registers, for review."""
        odd = []
        for s in sorted(self.flows):
            if kinds.get(s) not in ('pointer', 'export', 'entry'):
                continue
            extra_in = regs_of(self.li.get(s, 0)) & ~IND
            extra_cl = regs_of(self.cl.get(s, 0)) & ~IND
            if extra_in or extra_cl or self.fin.get(s):
                odd.append((s, extra_in, extra_cl, self.fin.get(s, 0)))
        return odd


def mask_names(m):
    return ','.join(REGNAME[i] for i in range(8) if m >> i & 1)
