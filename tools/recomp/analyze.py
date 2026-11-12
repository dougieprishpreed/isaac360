"""Code discovery for the static recompilation of game/isaac-ng.exe (docs/RECOMP_PLAN.md, phase 1).

Recursive descent from every known code address:
  * the entry point, exports, TLS callbacks and SafeSEH handlers;
  * every relocation target inside .text (the image has a full relocation table: every absolute code address,
    whether a vtable slot, a callback pushed as an immediate or a switch-table entry, carries one);
  * every direct call target found while decoding.
Switch tables (MSVC puts them in .text after the function) are read through their relocation sites, so their
bytes are never decoded as code. Writes build/recomp/program.json and prints coverage statistics.

    tools/.venv/Scripts/python tools/recomp/analyze.py
"""
import json
import os
import struct
import sys
from collections import defaultdict

import iced_x86 as ix
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# ISAAC360_IMAGE / ISAAC360_TAG select another game binary (the Lua DLL as rebase.py rebased it:
# build/recomp/dlls/lua5.3.3r.dll, tag lua)
TAG = os.environ.get('ISAAC360_TAG', '')
EXE = os.path.join(ROOT, os.environ.get('ISAAC360_IMAGE', os.path.join('game', 'isaac-ng.exe')))
OUT = os.path.join(ROOT, 'build', 'recomp', TAG) if TAG else os.path.join(ROOT, 'build', 'recomp')

# Imports that never return: decoding stops after a call to them.
NORETURN_IMPORTS = {'_CxxThrowException', 'exit', '_exit', 'abort', 'ExitProcess', 'TerminateProcess', '_invalid_parameter_noinfo_noreturn',
                    'longjmp', '__std_terminate', 'terminate', '_invoke_watson', '__fastfail'}
# instructions user-mode compiled code never holds: a start known only from a pointer that decodes into one is data
DATA_LIKE = {ix.Mnemonic.AAS, ix.Mnemonic.AAA, ix.Mnemonic.AAM, ix.Mnemonic.AAD, ix.Mnemonic.DAA, ix.Mnemonic.DAS,
             ix.Mnemonic.ARPL, ix.Mnemonic.BOUND, ix.Mnemonic.INSB, ix.Mnemonic.INSW, ix.Mnemonic.INSD,
             ix.Mnemonic.OUTSB, ix.Mnemonic.OUTSW, ix.Mnemonic.OUTSD, ix.Mnemonic.IN, ix.Mnemonic.OUT,
             ix.Mnemonic.IRETD, ix.Mnemonic.IRET, ix.Mnemonic.RETF, ix.Mnemonic.HLT, ix.Mnemonic.INTO,
             ix.Mnemonic.LES, ix.Mnemonic.LDS, ix.Mnemonic.SALC, ix.Mnemonic.POPA, ix.Mnemonic.PUSHA}
NORETURN_SUFFIX = ('_Xlength_error@std@@YAXPBD@Z', '_Xout_of_range@std@@YAXPBD@Z', '_Xbad_alloc@std@@YAXXZ',
                   '_Xinvalid_argument@std@@YAXPBD@Z', '_Xbad_function_call@std@@YAXXZ', '_Xruntime_error@std@@YAXPBD@Z',
                   '_Xoverflow_error@std@@YAXPBD@Z')


class Image:
    def __init__(self, path):
        self.pe = pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.data = pe.get_memory_mapped_image()
        text = pe.sections[0]
        assert text.Name.startswith(b'.text')
        self.text_lo = self.base + text.VirtualAddress
        self.text_hi = self.text_lo + text.Misc_VirtualSize
        self.sections = [(s.Name.rstrip(b'\0').decode(), self.base + s.VirtualAddress, self.base + s.VirtualAddress + s.Misc_VirtualSize)
                         for s in pe.sections]
        self.relocs = set()                                  # addresses of relocated dwords
        for b in pe.DIRECTORY_ENTRY_BASERELOC:
            for e in b.entries:
                if e.type == 3:
                    self.relocs.add(self.base + e.rva)
        self.imports = {}                                    # IAT slot -> "dll!name"
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            for i in d.imports:
                name = i.name.decode() if i.name else '#%d' % i.ordinal
                self.imports[i.address] = d.dll.decode() + '!' + name

    def u32(self, va):
        o = va - self.base
        return struct.unpack_from('<I', self.data, o)[0]

    def in_text(self, va):
        return self.text_lo <= va < self.text_hi

    def section(self, va):
        for n, lo, hi in self.sections:
            if lo <= va < hi:
                return n
        return None


def imported_names(images, dll):
    """The names (and ordinals) these images import from dll."""
    names = set()
    for path in images:
        pe = pefile.PE(os.path.join(ROOT, path))
        for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
            if e.dll.decode('latin1').lower() == dll.lower():
                for i in e.imports:
                    names.add(i.name.decode('latin1') if i.name else i.ordinal)
    return names


def seeds(img):
    pe = img.pe
    out = {img.base + pe.OPTIONAL_HEADER.AddressOfEntryPoint: 'entry'}
    # ISAAC360_EXPORTS_FROM=image,image...: only the exports those images import are roots (msvcp140.dll: the game
    # takes 34 of its 1,124, and the console holds every function translated)
    used = None
    if os.environ.get('ISAAC360_EXPORTS_FROM'):
        used = imported_names(os.environ['ISAAC360_EXPORTS_FROM'].split(','), os.path.basename(EXE))
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            va = img.base + e.address
            name = (e.name or b'').decode('latin1')
            # decorated data exports: globals (@@3), static members (@@2), vtables (@@6B), vbtables (@@7B), RTTI (@@8)
            if any(t in name for t in ('@@3', '@@2', '@@6B', '@@7B', '@@8')):
                continue
            if used is not None and (name or e.ordinal) not in used:
                continue
            if img.in_text(va):
                out[va] = 'export'
    if hasattr(pe, 'DIRECTORY_ENTRY_TLS'):
        cb = pe.DIRECTORY_ENTRY_TLS.struct.AddressOfCallBacks
        while cb and img.u32(cb):
            out[img.u32(cb)] = 'tls'
            cb += 4
    lc = getattr(pe, 'DIRECTORY_ENTRY_LOAD_CONFIG', None)
    if lc and lc.struct.SEHandlerTable:
        for i in range(lc.struct.SEHandlerCount):
            rva = img.u32(lc.struct.SEHandlerTable + 4 * i)
            out[img.base + rva] = 'seh'
    return out


def main():
    img = Image(EXE)
    code = img.data[img.text_lo - img.base:img.text_hi - img.base]
    reloc_code = {}                                          # relocated pointer -> where it was found
    data_pointers = set()                                    # code addresses relocated outside .text
    for site in img.relocs:
        tgt = img.u32(site)
        if img.in_text(tgt):
            reloc_code.setdefault(tgt, site)
            if not img.in_text(site):
                data_pointers.add(tgt)

    insns = {}                                               # va -> (length, mnemonic text) for decoded code
    owner = {}                                               # instruction va -> function start
    jump_tables = {}                                         # jmp va -> [targets]
    table_bytes = set()                                      # .text bytes that are switch-table data
    funcs = {}                                               # start -> {'blocks': set, 'calls': set, 'kind'}
    unresolved = []                                          # indirect jumps without a table
    noreturn_calls = set()

    starts = dict(seeds(img))
    queue = list(starts)
    decoder_cache = {}

    def decode_at(va):
        o = va - img.text_lo
        d = ix.Decoder(32, code[o:o + 16], ip=va)
        return d.decode()

    def read_table(base, limit=None):
        targets = []
        a = base
        while a in img.relocs and img.in_text(img.u32(a)) and (limit is None or len(targets) < limit):
            targets.append(img.u32(a))
            if img.in_text(a):
                table_bytes.update(range(a, a + 4))
            a += 4
        return targets

    def explore(start):
        f = funcs[start]
        work = [start]
        seen = f['blocks']
        while work:
            va = work.pop()
            if va in seen:
                continue
            seen.add(va)
            last_cmp = None                                   # bound of a switch: cmp reg, imm before ja
            prev = None
            while True:
                if not img.in_text(va) or va in table_bytes:
                    f['bad'].append(va)
                    break
                if va != start and va in starts and starts[va] != 'call-mid':
                    # flowed into another function's start: a tail continuation into it. Code shared by two
                    # functions without such a start is simply translated in both.
                    f['tails'].add(va)
                    break
                ins = decode_at(va)
                if ins.code == ix.Code.INVALID or (f['kind'] == 'pointer' and ins.mnemonic in DATA_LIKE):
                    f['bad'].append(va)
                    break
                if ins.mnemonic == ix.Mnemonic.CMP and ins.op1_kind in (ix.OpKind.IMMEDIATE8TO32, ix.OpKind.IMMEDIATE32, ix.OpKind.IMMEDIATE8):
                    last_cmp = ins.immediate32 & 0xffffffff
                p_ins, prev = prev, ins
                insns[va] = ins.len
                owner.setdefault(va, start)
                fc = ins.flow_control
                nxt = va + ins.len
                if fc == ix.FlowControl.NEXT:
                    va = nxt
                    if va in seen:
                        break
                    continue
                if fc == ix.FlowControl.RETURN:
                    break
                if fc == ix.FlowControl.INTERRUPT:          # int3 / int 29h
                    break
                if fc == ix.FlowControl.CONDITIONAL_BRANCH:
                    work.append(ins.near_branch_target)
                    va = nxt
                    if va in seen:
                        break
                    continue
                if fc == ix.FlowControl.UNCONDITIONAL_BRANCH:
                    t = ins.near_branch_target
                    if t in starts and t != start:
                        f['tails'].add(t)
                    else:
                        work.append(t)
                    break
                if fc == ix.FlowControl.CALL:
                    t = ins.near_branch_target
                    f['calls'].add(t)
                    if t not in starts:
                        starts[t] = 'call'
                        queue.append(t)
                    va = nxt
                    continue
                if fc == ix.FlowControl.INDIRECT_CALL:
                    if ins.memory_base == ix.Register.NONE and ins.memory_index == ix.Register.NONE:
                        slot = ins.memory_displacement
                        name = img.imports.get(slot)
                        if name:
                            f['imports'].add(name)
                            short = name.split('!', 1)[1]
                            if short in NORETURN_IMPORTS or short.endswith(NORETURN_SUFFIX):
                                noreturn_calls.add(va)
                                break
                    va = nxt
                    continue
                if fc == ix.FlowControl.INDIRECT_BRANCH:
                    if ins.memory_base == ix.Register.NONE and ins.memory_index == ix.Register.NONE:
                        name = img.imports.get(ins.memory_displacement)
                        if name:                             # jmp [__imp_x]: an import thunk
                            f['imports'].add(name)
                            break
                    if ins.op0_kind == ix.OpKind.MEMORY and ins.memory_index != ix.Register.NONE and ins.memory_index_scale == 4:
                        tbl = ins.memory_displacement & 0xffffffff
                        limit = last_cmp + 1 if last_cmp is not None and last_cmp < 4096 else None
                        # two-level switch: movzx idx, byte [idx + bytes]; jmp [idx*4 + table]
                        if p_ins is not None and p_ins.mnemonic == ix.Mnemonic.MOVZX and p_ins.op1_kind == ix.OpKind.MEMORY                                 and p_ins.memory_size == ix.MemorySize.UINT8 and limit:
                            bt = p_ins.memory_displacement & 0xffffffff
                            if img.in_text(bt):
                                idx = img.data[bt - img.base:bt - img.base + limit]
                                table_bytes.update(range(bt, bt + limit))
                                limit = max(idx) + 1
                        targets = read_table(tbl, limit)
                        if targets:
                            jump_tables[va] = targets
                            work.extend(targets)
                            break
                    unresolved.append(va)
                    break
                # XBEGIN etc.
                f['bad'].append(va)
                break

    while queue:
        s = queue.pop()
        if s in funcs:
            continue
        if not img.in_text(s):
            continue
        funcs[s] = {'blocks': set(), 'calls': set(), 'tails': set(), 'imports': set(), 'bad': [], 'kind': starts.get(s, '?')}
        explore(s)

    # Relocated code pointers that are not switch tables or their entries are address-taken code: vtable slots,
    # callbacks. Iterate: functions found this way can hold more tables.
    while True:
        table_targets = {t for ts in jump_tables.values() for t in ts}
        extra = [t for t in reloc_code if t not in funcs and t not in table_targets and t not in table_bytes
                 and t not in insns]
        if not extra:
            break
        for t in extra:
            starts[t] = 'pointer'
            queue.append(t)
        while queue:
            s = queue.pop()
            if s in funcs or not img.in_text(s) or s in table_bytes:
                continue
            funcs[s] = {'blocks': set(), 'calls': set(), 'tails': set(), 'imports': set(), 'bad': [], 'kind': starts.get(s, '?')}
            explore(s)
    # a start later found to be table data was a misread pointer
    for s in [s for s in funcs if s in table_bytes]:
        del funcs[s]
    # images that merge read-only data into .text (msvcp140.dll) relocate pointers to strings and vtables too: a
    # start known only from such a pointer whose decoding runs into invalid code is data, not a function
    # (repeated: data misread as code also "calls" other misread data). A pointer kept in a data section (.rdata,
    # .data: Lua's luaL_Reg tables, vtables) is a real function even when its decoding runs on past a call that
    # never returns (string.format ends in luaL_error, followed by a switch table); only where read-only data has
    # its own section (msvcp140's .data also points at the strings merged into its .text)
    separate_rdata = any(n == '.rdata' for n, _, _ in img.sections)

    def from_data(start):
        return separate_rdata and start in data_pointers

    dropped = []
    while True:
        called = set()
        for f in funcs.values():
            called |= f['calls'] | f['tails']
        drop = [s for s, f in funcs.items() if f['kind'] == 'pointer' and f['bad'] and s not in called
                and not from_data(s)]
        if not drop:
            break
        if os.environ.get('ISAAC360_ANALYZE_DEBUG'):
            for s in drop:
                print('drop %08x: bad at %s' % (s, ' '.join('%08x' % b for b in funcs[s]['bad'][:4])))
        for s in drop:
            del funcs[s]
        dropped += drop
    # what remains must be reachable from trusted starts; misread data "calls" random addresses, and those
    # call-only starts are dropped with it
    roots = [s for s, f in funcs.items() if f['kind'] not in ('pointer', 'call') or
             (f['kind'] == 'pointer' and (not f['bad'] or from_data(s)))]
    reach, work = set(), list(roots)
    while work:
        s = work.pop()
        if s in reach or s not in funcs:
            continue
        reach.add(s)
        work.extend(funcs[s]['calls'] | funcs[s]['tails'])
    unreached = [s for s in funcs if s not in reach]
    for s in unreached:
        del funcs[s]
    dropped += unreached
    if dropped:
        print('dropped %d pointer-only starts that decode into data (and what only they reach)' % len(dropped))
    # pointers into the middle of decoded code (not table entries): report
    mid = [t for t in reloc_code if t not in funcs and t not in table_targets and t not in table_bytes]

    covered = sum(insns.values()) + len(table_bytes)
    text_size = img.text_hi - img.text_lo
    pad = sum(1 for b in code if b == 0xCC)
    kinds = defaultdict(int)
    for f in funcs.values():
        kinds[f['kind']] += 1
    bad = sum(1 for f in funcs.values() if f['bad'])
    imports_used = set()
    for f in funcs.values():
        imports_used |= f['imports']
    print('functions %d (%s)' % (len(funcs), dict(kinds)))
    print('decoded %d bytes + %d table bytes of %d (.text), %.2f%%; 0xCC bytes %d' %
          (sum(insns.values()), len(table_bytes), text_size, 100.0 * covered / text_size, pad))
    print('jump tables %d, unresolved indirect jumps %d, functions with bad decode %d, pointers into code middles %d' %
          (len(jump_tables), len(unresolved), bad, len(mid)))
    print('imports referenced %d of %d' % (len(imports_used), len(img.imports)))

    os.makedirs(OUT, exist_ok=True)
    prog = {
        'image_base': img.base, 'text': [img.text_lo, img.text_hi],
        'sections': img.sections,
        'functions': {('%08x' % s): {'kind': f['kind'], 'blocks': sorted(f['blocks']), 'calls': sorted(f['calls']),
                                       'tails': sorted(f['tails']), 'imports': sorted(f['imports']), 'bad': f['bad']}
                      for s, f in funcs.items()},
        'jump_tables': {('%08x' % k): v for k, v in jump_tables.items()},
        'unresolved_indirect_jumps': sorted(unresolved),
        'pointers_into_code': sorted(mid),
        'imports': {('%08x' % k): v for k, v in img.imports.items()},
        'noreturn_calls': sorted(noreturn_calls),
        'table_bytes': sorted(table_bytes),
    }
    with open(os.path.join(OUT, 'program.json'), 'w') as fp:
        json.dump(prog, fp)
    # gaps: undecoded non-padding .text ranges
    gaps = []
    a = img.text_lo
    covered_set = set()
    for va, ln in insns.items():
        covered_set.update(range(va, va + ln))
    covered_set |= table_bytes
    run = None
    for va in range(img.text_lo, img.text_hi):
        b = code[va - img.text_lo]
        if va in covered_set or b in (0xCC, 0x90):
            if run and va - run >= 8:
                gaps.append((run, va))
            run = None
        elif run is None:
            run = va
    gaps.sort(key=lambda g: g[0] - g[1])
    print('undecoded non-padding runs >= 8 bytes: %d, %d bytes' % (len(gaps), sum(b - a for a, b in gaps)))
    with open(os.path.join(OUT, 'gaps.txt'), 'w') as fp:
        for a, b in gaps:
            fp.write('%08x %d\n' % (a, b - a))


if __name__ == '__main__':
    main()
