"""Names the functions behind a BOI360_PROFILE log's samples (docs/RECOMP_PLAN.md).

The profiler (runtime.cpp) samples the main guest thread every millisecond: the shim it is in, or in game code the
innermost return address on its stack. A return address follows the call of the function that was running, so each
is shown as that callee and the function holding the call site. The log's blocks (one each 600 frames) are summed,
or only those from --from-block on.

    tools/.venv/Scripts/python tools/recomp/profile_report.py work/console08/boi360.log [--from-block N] [--top 40]
"""
import argparse
import bisect
import json
import os
import re
from collections import Counter

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
            d = iced_x86.Decoder(32, code, ip=start)
            ins = d.decode()
            if ins.len == n and ins.flow_control in (iced_x86.FlowControl.CALL, iced_x86.FlowControl.INDIRECT_CALL):
                target = ins.near_branch_target if ins.flow_control == iced_x86.FlowControl.CALL else None
                f = iced_x86.Formatter(iced_x86.FormatterSyntax.INTEL)
                return target, f.format(ins)
        return None, '?'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--from-block', type=int, default=0)
    ap.add_argument('--top', type=int, default=40)
    args = ap.parse_args()
    images = [Image(p, a) for p, a in IMAGES]
    counts, shims, total, block = Counter(), Counter(), 0, -1
    for line in open(args.log, errors='replace'):
        m = re.match(r'profile: (\d+) samples', line)
        if m:
            block += 1
            if block >= args.from_block:
                total += int(m.group(1))
            continue
        if block < args.from_block:
            continue
        m = re.match(r'profile:\s+([\d.]+)% (at|in) (.*)', line.strip())
        if not m:
            continue
        share = float(m.group(1))              # of its block; blocks are about equal, so shares are averaged
        if m.group(2) == 'at':
            counts[int(m.group(3), 16)] += share
        else:
            shims[m.group(3)] += share
    blocks = block - args.from_block + 1
    if blocks <= 0:
        raise SystemExit('no profile blocks in ' + args.log)
    print('%d blocks, %d samples; shares averaged over the blocks' % (blocks, total))
    rows = []
    for name, share in shims.items():
        rows.append((share / blocks, 'shim  ' + name))
    for ret, share in counts.items():
        img = next((i for i in images if i.holds(ret)), None)
        if not img:
            rows.append((share / blocks, '%08x (outside the images)' % ret))
            continue
        target, text = img.call_before(ret)
        caller = img.function_of(ret)
        callee = ('F_%x' % target) if target else text
        rows.append((share / blocks, '%-14s called from F_%x+%x (%s)' % (callee, caller or 0, ret - (caller or 0), img.name)))
    rows.sort(reverse=True)
    for share, text in rows[:args.top]:
        print('%5.1f%%  %s' % (share, text))
    by_callee = Counter()
    for share, text in rows:
        by_callee[text.split()[0] if not text.startswith('shim') else text] += share
    print('\nby function:')
    for text, share in by_callee.most_common(25):
        print('%5.1f%%  %s' % (share, text))


if __name__ == '__main__':
    main()
