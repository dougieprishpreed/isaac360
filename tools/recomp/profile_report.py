"""Names the functions behind a BOI360_PROFILE log's samples (docs/RECOMP_PLAN.md).

The profiler (runtime.cpp) samples every guest thread each millisecond: the shim it is in and the guest code that
called it, or in game code the innermost return address on its stack. A return address follows the call of the
function that was running, so each is shown as that callee and the function holding the call site. The log's
blocks (one each 600 frames) are averaged per thread, from --from-block on. The "locks:" lines (contended critical
sections, kernel32 shims) are summed, their waiting and holding code named the same way.

    tools/.venv/Scripts/python tools/recomp/profile_report.py work/console09/boi360.log [--from-block N] [--top 30]
"""
import argparse
import bisect
import json
import os
import re
from collections import Counter, defaultdict

import iced_x86
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IMAGES = [  # (path, analysis), as the runtime loads them
    ('game/isaac-ng.exe', 'build/recomp/program.json'),
    ('build/recomp/dlls/lua5.3.3r.dll', 'build/recomp/lua/program.json'),
    ('build/recomp/dlls/msvcp140.dll', 'build/recomp/msvcp/program.json'),
]


class Image:
    def __init__(self, path, analysis):
        pe = pefile.PE(os.path.join(ROOT, path), fast_load=True)
        self.name = os.path.basename(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.data = pe.get_memory_mapped_image()
        self.funcs = sorted(int(k, 16) for k in json.load(open(os.path.join(ROOT, analysis)))['functions'])

    def holds(self, va):
        return self.base <= va < self.base + len(self.data)

    def function_of(self, va):
        i = bisect.bisect_right(self.funcs, va) - 1
        return self.funcs[i] if i >= 0 else None

    def call_before(self, ret):
        """The call instruction that ends at ret: (target or None, text)."""
        for n in range(2, 8):
            start = ret - n
            code = self.data[start - self.base:ret - self.base]
            ins = iced_x86.Decoder(32, code, ip=start).decode()
            if ins.len == n and ins.flow_control in (iced_x86.FlowControl.CALL, iced_x86.FlowControl.INDIRECT_CALL):
                target = ins.near_branch_target if ins.flow_control == iced_x86.FlowControl.CALL else None
                return target, iced_x86.Formatter(iced_x86.FormatterSyntax.INTEL).format(ins)
        return None, '?'


class Namer:
    def __init__(self):
        self.images = [Image(p, a) for p, a in IMAGES]

    def site(self, ret):
        """A return address as 'callee, called from caller+offset'."""
        img = next((i for i in self.images if i.holds(ret)), None)
        if not img:
            return '%08x' % ret
        target, text = img.call_before(ret)
        caller = img.function_of(ret) or 0
        callee = ('F_%x' % target) if target else text
        return '%s from F_%x+%x' % (callee, caller, ret - caller)

    def caller(self, ret):
        """The function holding a return address, with the offset."""
        img = next((i for i in self.images if i.holds(ret)), None)
        if not img:
            return '%08x' % ret
        caller = img.function_of(ret) or 0
        return 'F_%x+%x' % (caller, ret - caller)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--from-block', type=int, default=0)
    ap.add_argument('--top', type=int, default=30)
    args = ap.parse_args()
    namer = Namer()
    blocks = defaultdict(int)                 # thread -> blocks seen from --from-block
    seen = defaultdict(int)                   # thread -> blocks seen
    shares = defaultdict(Counter)             # thread -> key -> summed share
    game = defaultdict(float)
    locks = {}
    for line in open(args.log, errors='replace'):
        line = line.rstrip()
        m = re.match(r'profile(?: (.+?))?: (\d+) samples, ([\d.]+)% in game code', line)
        if m:
            t = m.group(1) or 'main'
            seen[t] += 1
            if seen[t] > args.from_block:
                blocks[t] += 1
                game[t] += float(m.group(3))
            continue
        m = re.match(r'profile(?: (.+?))?:\s+([\d.]+)% (?:in (\S+)(?: from ([0-9a-f]{8}))?|at ([0-9a-f]{8}))', line)
        if m:
            t = m.group(1) or 'main'
            if seen[t] <= args.from_block:
                continue
            if m.group(3):
                key = ('shim', m.group(3), int(m.group(4), 16) if m.group(4) else 0)
            else:
                key = ('at', '', int(m.group(5), 16))
            shares[t][key] += float(m.group(2))
            continue
        m = re.match(r'locks: ([0-9a-f]{8}) waited (\d+) times, ([\d.]+) ms \(longest ([\d.]+) ms: waiting from '
                     r'([0-9a-f]{8}) ([0-9a-f]{8}), held from ([0-9a-f]{8}) ([0-9a-f]{8})\)', line)
        if m:
            a = int(m.group(1), 16)
            e = locks.setdefault(a, {'waits': 0, 'ms': 0.0, 'max': 0.0, 'sites': None})
            e['waits'] += int(m.group(2))
            e['ms'] += float(m.group(3))
            if float(m.group(4)) >= e['max']:
                e['max'] = float(m.group(4))
                e['sites'] = [int(m.group(i), 16) for i in range(5, 9)]
    for t in sorted(blocks, key=lambda t: (t != 'main', t)):
        n = blocks[t]
        print('== %s: %d blocks, %.1f%% in game code' % (t, n, game[t] / n))
        for key, share in shares[t].most_common(args.top if t == 'main' else 10):
            kind, shim, at = key
            if kind == 'shim':
                print('%6.1f%%  %s, called in %s' % (share / n, shim, namer.caller(at) if at else '?'))
            else:
                print('%6.1f%%  %s' % (share / n, namer.site(at) if at else 'game code, no return address'))
    if locks:
        print('== contended locks (summed)')
        for a, e in sorted(locks.items(), key=lambda kv: -kv[1]['ms'])[:10]:
            w0, w1, h0, h1 = e['sites']
            print('%08x: %d waits, %.0f ms, longest %.1f ms; waiting in %s < %s; held from %s < %s' % (
                a, e['waits'], e['ms'], e['max'], namer.caller(w0), namer.caller(w1), namer.caller(h0), namer.caller(h1)))


if __name__ == '__main__':
    main()
