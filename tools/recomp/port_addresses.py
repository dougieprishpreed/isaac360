"""Finds the port's addresses of one isaac-ng.exe in another (a game update): the functions the runtime hooks or
calls (translate.py HOST and PATCHES, guest_call targets) and the data it reads.

A function is found by its first bytes: the old image's code from the old address, with every operand that differs
between builds masked (base relocations: absolute addresses; near call/jump displacements), searched in the new
image's code; the window grows until one place matches. Data addresses are found through code that uses them: an
instruction of a found function that names the old address names the new one at the same offset in the new function.

    tools/.venv/Scripts/python tools/recomp/port_addresses.py [--old game_v175/isaac-ng.exe] [--new game/isaac-ng.exe]
"""
import argparse
import os
import re
import struct
import sys

import iced_x86 as ix
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# name -> old address (v1.7.5): functions
FUNCS = {
    'png_read_filter_row': 0x9b2b60, 'inflate': 0x9af810, 'inflateEnd': 0x9af540,
    'vorbis_get_samples_short_interleaved': 0x9aa630, 'vorbis_set_file_offset': 0x9a3e50,
    'vorbis_pump_first_frame': 0x9a7910, 'vorbis_deinit': 0x9a99c0,
    'lockobj_lock': 0x94f870, 'lockobj_unlock': 0x94f930,
    'refcount_add': 0x407770, 'refcount_release': 0x407710, 'refcount_use': 0x407790, 'refcount_peek': 0x959c30,
    'image_load': 0x98cd10, 'image_decode': 0x98ced0, 'image_pixels': 0x99d030,
    'isaac_seed': 0x9aee80, 'isaac_round': 0x9aef40, 'isaac_xor': 0x99ce00, 'tinfl': 0x99b210,
    'archive_read': 0x988670, 'archive_seek': 0x9885b0, 'archive_next_block': 0x988190, 'archive_mount': 0x950420,
    'pool_alloc': 0x9863b0, 'name_intern': 0x94fb90, 'free_9cd2d5': 0x9cd2d5,
    'add_costume': 0x710f70, 'set_color': 0x677f50, 'sprite_load': 0x406d50, 'boss_intro': 0x7d6ed0,
    'player_eval_colour': 0x716840,
}
# name -> (old address, the function whose code uses it): data
DATA = {
    'game_ptr': (0xbc9aec, 'boss_intro'),
    'image_pool': (0xbc3fc8, 'image_load'),
    'name_table': (0xba3bd0, 'image_load'),
    'stream_vtable': (0xb44954, None),
    'player_vtable': (0xb2cd60, None),
    'lockobj_vtable': (0xb3d508, None),
}


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()
        text = self.pe.sections[0]
        self.text_va = self.base + text.VirtualAddress
        self.text = self.data[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
        self.relocs = set()
        for block in getattr(self.pe, 'DIRECTORY_ENTRY_BASERELOC', []):
            for e in block.entries:
                if e.type == 3:
                    self.relocs.add(self.base + e.rva)

    def u32(self, va):
        return struct.unpack_from('<I', self.data, va - self.base)[0]

    def code(self, va, n):
        return self.text[va - self.text_va:va - self.text_va + n]


def pattern(img, va, n):
    """The regex of the n code bytes at va, operands that move between builds masked; and the instructions."""
    raw = img.code(va, n + 16)
    dec = ix.Decoder(32, raw, ip=va)
    mask = []
    insns = []
    for ins in dec:
        if ins.ip - va >= n:
            break
        b = bytearray(raw[ins.ip - va:ins.ip - va + ins.len])
        m = [False] * ins.len
        for k in range(ins.len):
            if ins.ip + k in img.relocs:
                for j in range(k, min(k + 4, ins.len)):
                    m[j] = True
        if ins.flow_control in (ix.FlowControl.CALL, ix.FlowControl.UNCONDITIONAL_BRANCH,
                                ix.FlowControl.CONDITIONAL_BRANCH) and ins.op0_kind in (
                ix.OpKind.NEAR_BRANCH32, ix.OpKind.NEAR_BRANCH16):
            size = 4 if ins.len >= 5 else 1
            for j in range(ins.len - size, ins.len):
                m[j] = True
        insns.append(ins)
        for k in range(ins.len):
            mask.append(b'.' if m[k] else re.escape(bytes([b[k]])))
    return re.compile(b''.join(mask), re.DOTALL), insns


def find_func(old, new, va):
    for n in (24, 48, 96, 192, 384):
        rx, _ = pattern(old, va, n)
        hits = [m.start() for m in rx.finditer(new.text)]
        if len(hits) == 1:
            return new.text_va + hits[0], n
        if not hits:
            return None, n
    return 'many', n


def find_data(old, new, va, func_old, func_new):
    """The new address an instruction of the found function names, where the old one named va."""
    n = 0x400
    old_insns = list(ix.Decoder(32, old.code(func_old, n), ip=func_old))
    new_insns = list(ix.Decoder(32, new.code(func_new, n), ip=func_new))
    for io, inn in zip(old_insns, new_insns):
        if io.mnemonic != inn.mnemonic:
            break
        for k in range(io.len):
            if io.ip + k in old.relocs and old.u32(io.ip + k) == va:
                return new.u32(inn.ip + k)
            if io.ip + k in old.relocs and va - 0x40 <= old.u32(io.ip + k) < va + 0x40:
                return new.u32(inn.ip + k) + (va - old.u32(io.ip + k))
    return None


def call_lists(img, program):
    """{function: [direct call targets in the order of their sites]} from analyze.py's program.json blocks."""
    out = {}
    for key, f in program['functions'].items():
        sites = []
        for b in f['blocks']:
            dec = ix.Decoder(32, img.code(b, 0x2000), ip=b)
            for ins in dec:
                if ins.mnemonic == ix.Mnemonic.CALL and ins.op0_kind == ix.OpKind.NEAR_BRANCH32:
                    sites.append((ins.ip, ins.near_branch_target))
                if ins.flow_control in (ix.FlowControl.RETURN, ix.FlowControl.UNCONDITIONAL_BRANCH,
                                        ix.FlowControl.INDIRECT_BRANCH, ix.FlowControl.CONDITIONAL_BRANCH,
                                        ix.FlowControl.INTERRUPT, ix.FlowControl.EXCEPTION):
                    break
        out[int(key, 16)] = [t for _, t in sorted(set(sites))]
    return out


def propagate(old, new, pairs, old_prog, new_prog):
    """More function pairs from the call graph: a matched pair whose call lists have the same length pairs its callees
    in order; a function called once from a matched caller pairs with what the new caller calls at that place."""
    oc, nc = call_lists(old, old_prog), call_lists(new, new_prog)
    pairs = dict(pairs)
    taken = set(pairs.values())
    for _ in range(12):
        added = 0
        for o, n in list(pairs.items()):
            a, b = oc.get(o, []), nc.get(n, [])
            if len(a) != len(b) or not a:
                continue
            for x, y in zip(a, b):
                if x not in pairs and y not in taken:
                    pairs[x] = y
                    taken.add(y)
                    added += 1
        if not added:
            break
    return pairs


def main():
    import json
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(ROOT, 'game_v175', 'isaac-ng.exe'))
    ap.add_argument('--new', default=os.path.join(ROOT, 'game', 'isaac-ng.exe'))
    ap.add_argument('--old-program', default=os.path.join(ROOT, 'build', 'recomp', 'v175', 'program.json'))
    ap.add_argument('--new-program', default=os.path.join(ROOT, 'build', 'recomp', 'program.json'))
    args = ap.parse_args()
    old, new = Image(args.old), Image(args.new)
    old_prog, new_prog = json.load(open(args.old_program)), json.load(open(args.new_program))
    # every old function's first bytes, for the seeds of the call graph
    seeds = {}
    for key in old_prog['functions']:
        va = int(key, 16)
        rx, _ = pattern(old, va, 48)
        hits = [m.start() for m in rx.finditer(new.text)]
        if len(hits) == 1:
            seeds[va] = new.text_va + hits[0]
    print('%d of %d functions matched by their first 48 bytes' % (len(seeds), len(old_prog['functions'])))
    pairs = propagate(old, new, seeds, old_prog, new_prog)
    print('%d after the call graph' % len(pairs))
    # functions keep their order within an object file: between two matched neighbours with as many functions
    # between them in both images, the ones between pair in order
    olds = sorted(int(k, 16) for k in old_prog['functions'])
    news = sorted(int(k, 16) for k in new_prog['functions'])
    import bisect
    anchors = sorted((o, n) for o, n in pairs.items())
    added = 0
    for (oa, na), (ob, nb) in zip(anchors, anchors[1:]):
        if nb <= na:
            continue
        mid_o = olds[bisect.bisect_right(olds, oa):bisect.bisect_left(olds, ob)]
        mid_n = news[bisect.bisect_right(news, na):bisect.bisect_left(news, nb)]
        if mid_o and len(mid_o) == len(mid_n) and len(mid_o) <= 8:
            for x, y in zip(mid_o, mid_n):
                if x not in pairs:
                    pairs[x] = y
                    added += 1
    print('%d after the neighbours (%d added)' % (len(pairs), added))
    with open(os.path.join(ROOT, 'build', 'recomp', 'port_pairs.json'), 'w') as f:
        json.dump({'%08x' % o: '%08x' % n for o, n in sorted(pairs.items())}, f)
    found = {}
    for name, va in FUNCS.items():
        hit, n = find_func(old, new, va)
        how = '%d bytes' % n
        if not isinstance(hit, int) and va in pairs:
            hit, how = pairs[va], 'call graph'
        found[name] = hit if isinstance(hit, int) else None
        print('%-40s %08x -> %s (%s)' % (name, va, ('%08x' % hit) if isinstance(hit, int) else hit, how))
    for name, (va, func) in DATA.items():
        hit = None
        if func and found.get(func):
            hit = find_data(old, new, va, FUNCS[func], found[func])
        print('%-40s %08x -> %s' % (name, va, ('%08x' % hit) if hit else None))


if __name__ == '__main__':
    main()
