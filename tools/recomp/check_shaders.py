"""Checks the console's compiled shaders against exact maths (docs/RECOMP_PLAN.md, "Next" 1). Runs in WSL:

    wsl -d CTR360-XSDK -- python3 tools/recomp/check_shaders.py [p05 ...]

For each converted program (build/recomp/glsl/pNN.vert/.frag, kept by tools/recomp/shaders.py with
ISAAC360_SHADER_GLSL=1), the fragment shader is compiled as the console build compiles it (xost through
vulkan4xenon's options, optimised) and run on xost's simulator (xenos-toolchain/xdis/simulate.py), which models the
GPU's maths: single rounding, its sin/cos/rcp, NaN through selects, kills. The reference is tools/recomp/glsl_ref.py
running the same GLSL with each operation rounded to 32-bit floats (a GPU's precision, not the console's quirks). Inputs are what the game draws with: per program, the push constants and vertices
of real draws (ISAAC360_SHADER_SAMPLES, build/recomp/shader_samples.txt, from tools/recomp/coverage.py), the vertex
shader run on the reference for the varyings at each vertex, the triangles' centres and the edges' midpoints. Both
sides sample one smooth texture function whose alpha reaches 0 and 1 in places.

A colour component counts as different when the two differ by more than 2/255 after the [0, 1] clamp an RGBA8
target applies (a NaN stores as 0); depth when it differs by more than 1e-5 or one side drops the pixel and the other
doesn't. Prints per program the points run and the differences, with the worst inputs; exit 1 when any differ.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'recomp'))
XOST = os.environ.get('XOST', '/opt/isaac360-xsdk/deps/xsdk/xenos-toolchain')
GLSLANG = os.environ.get('GLSLANG', '/opt/ctr360-xsdk/glslang/build-host/StandAlone/glslang')
sys.path.insert(0, XOST)

import glsl_ref  # noqa: E402
import xdis.container as container  # noqa: E402
import xdis.simulate as simulate  # noqa: E402
import xdis.xenos_ucode as xu  # noqa: E402
import xsc.native as native  # noqa: E402
from xasm.program import XSDK_REGISTERS  # noqa: E402

GLSL = os.path.join(ROOT, 'build', 'recomp', 'glsl')
SAMPLES = os.path.join(ROOT, 'build', 'recomp', 'shader_samples.txt')
EXPORT_DEPTH = int(xu.ExportRegister.kPSDepth)
TEX_SIZE = [256, 256]
MAX_POINTS = 160
TOL = 2.0 / 255.0


def texel(binding, u, v):
    """A smooth texture, the same for both sides; alpha is exactly 0 and 1 over parts of it."""
    b = binding * 0.7
    def s(x):
        return math.sin(x) if math.isfinite(x) else float('nan')
    r = 0.5 + 0.5 * s(2.3 * u + 1.7 * v + b)
    g = 0.5 + 0.5 * s(1.9 * u - 2.9 * v + 1.0 + b)
    bl = 0.5 + 0.5 * s(-2.1 * u + 1.3 * v + 2.0 + b)
    a = min(max(1.6 * s(1.7 * u + 2.3 * v + 0.4 + b) + 0.35, 0.0), 1.0)
    return [r, g, bl, a]


def preprocessed(path):
    r = subprocess.run([GLSLANG, '-E', path], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit('glslang -E failed on %s: %s' % (path, r.stdout + r.stderr))
    return r.stdout


def spirv(path):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'x.spv')
        r = subprocess.run([GLSLANG, '-V', path, '-o', out], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit('glslang -V failed on %s: %s' % (path, r.stdout + r.stderr))
        return open(out, 'rb').read()


def program_attribs():
    """[(name, location, components)] per program, in build/recomp/gl_shaders.inc's GLVK_PROGRAMS order."""
    rows = []
    for line in open(os.path.join(ROOT, 'build', 'recomp', 'gl_shaders.inc')):
        if line.lstrip().startswith('{0x'):
            meta = line.split('"')[1]
            rows.append([(n, int(l), int(c)) for n, l, c in (item.split(':') for item in meta.split(',') if item)])
    return rows


def read_samples():
    """{program index: (attribs [(name, loc, comps)], [(push floats, [vertex floats])])}"""
    out, cur = {}, None
    if not os.path.isfile(SAMPLES):
        return out
    for line in open(SAMPLES):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'P':
            cur = int(parts[1])
            out[cur] = ([], [])
        elif parts[0] == 'A':
            for item in (parts[1].split(',') if len(parts) > 1 else []):
                n, loc, c = item.split(':')
                out[cur][0].append((n, int(loc), int(c)))
        elif parts[0] == 'U':
            out[cur][1].append([[float(x) for x in parts[1:]], []])
        elif parts[0] == 'V':
            out[cur][1][-1][1] = [float(x) for x in parts[1:]]
    return out


# For programs no run has drawn yet: inputs in the ranges the game gives each attribute (by name), the screen's
# orthographic Transform as the game sets it for 960x540.
SYNTH = {'Position': lambda r: [r.uniform(0, 960), r.uniform(0, 540), -1.46],
         'TexCoord': lambda r: [r.random(), r.random()],
         'TextureSize': lambda r: [float(r.choice((128, 256, 512, 1024)))] * 2,
         'PixelationAmount': lambda r: [0.0 if r.random() < 0.5 else r.random()],
         'ClipPlane': lambda r: [0.0, 0.0, -1.0] if r.random() < 0.6 else [r.uniform(-1, 1), r.uniform(-1, 1), r.uniform(0, 500)],
         'ColorOffsetIn': lambda r: [r.uniform(-0.5, 0.5) for _ in range(3)],
         'Ratio': lambda r: [r.uniform(0.5, 2.0) for _ in range(4)],
         'Time': lambda r: [float(r.randint(0, 3000))],
         'Exposure': lambda r: [r.uniform(0.5, 2.0)], 'Gamma': lambda r: [r.uniform(0.8, 1.2)],
         'Brightness': lambda r: [r.uniform(-0.2, 0.2)], 'Contrast': lambda r: [r.uniform(0.8, 1.2)],
         'ScreenSize': lambda r: [960.0, 540.0, 1024.0, 1024.0], 'ScreenSizePow2': lambda r: [1024.0, 1024.0, 1.0],
         'PlayerVel': lambda r: [r.uniform(-1, 1), r.uniform(-1, 1)],
         'CurrentDir': lambda r: [0.0, 0.0] if r.random() < 0.5 else [r.uniform(-1, 1), r.uniform(-1, 1)]}
TRANSFORM = [2 / 960.0, 0, 0, 0, 0, 2 / 540.0, 0, 0, 0, 0, -0.001, 0, -1, -1, 0, 1]


def synthetic(index, attribs):
    import random
    r = random.Random(index)
    draws = []
    for _ in range(8):
        verts = []
        per_draw = {name: SYNTH.get(name, lambda r, c=c: [r.random() for _ in range(c)])(r)
                    for name, loc, c in attribs if name not in ('Position', 'TexCoord')}
        for _ in range(6):
            for name, loc, c in attribs:
                v = SYNTH[name](r) if name in ('Position', 'TexCoord') else per_draw[name]
                verts.extend((list(v) + [0.0] * c)[:c])
        draws.append([list(TRANSFORM), verts])
    return attribs, draws


def clamp01(x):
    return 0.0 if (x is None or math.isnan(x)) else min(max(x, 0.0), 1.0)


def vec4(v):
    c = glsl_ref.comps(v)
    return [float(x) for x in c] + [0.0] * (4 - len(c))


def blend(points, weights):
    out = {}
    for loc in points[0]:
        out[loc] = [sum(w * p[loc][i] for p, w in zip(points, weights)) for i in range(4)]
    return out


def fragment_points(index, attribs, draws, vs):
    """[(varyings by location, frag coord, push)] from the program's sampled draws."""
    stride = sum(c for _, _, c in attribs)
    out_locs = {name: loc for name, (ty, loc) in vs.prog['outputs'].items()}
    points = []
    for push, verts in draws:
        corners = []
        for k in range(len(verts) // stride if stride else 0):
            vals, inputs, at = verts[k * stride:(k + 1) * stride], {}, 0
            for name, loc, c in attribs:
                inputs[name] = glsl_ref.Vec(vals[at:at + c]) if c > 1 else vals[at]
                at += c
            g = vs.run(inputs, push)
            vary = {loc: vec4(g[name]) for name, loc in out_locs.items()}
            pos = vec4(g['gl_Position'])
            w = pos[3] if pos[3] else 1.0
            coord = [(pos[0] / w * 0.5 + 0.5) * 1280.0, (pos[1] / w * 0.5 + 0.5) * 720.0]
            vary['coord'] = [math.floor(coord[0]) if math.isfinite(coord[0]) else 0.0,
                             math.floor(coord[1]) if math.isfinite(coord[1]) else 0.0, 0.0, 0.0]
            corners.append(vary)
        for c in corners:
            points.append((c, push))
        for t in range(0, len(corners) - 2, 3):
            tri = corners[t:t + 3]
            points.append((blend(tri, [1 / 3.0] * 3), push))
            points.append((blend(tri[:2], [0.5, 0.5]), push))
            points.append((blend([tri[1], tri[2]], [0.25, 0.75]), push))
    if len(points) > MAX_POINTS:
        step = len(points) / float(MAX_POINTS)
        points = [points[int(i * step)] for i in range(MAX_POINTS)]
    return points


def check_vertex(tag, vs, vblob, vliterals, vresources, attribs, draws, fs_locs):
    """The vertex shader, linked to its fragment shader as the console links them, on the sampled vertices: the
    position export and every interpolator against the reference. Each fetch reads the input its vertex element
    names: the element's usage and index give the location as vulkan4xenon assigns them (src/pipeline.c,
    location_usage: 0 POSITION, 1 COLOR, 2 TEXCOORD0, n TEXCOORDn-2), the nth fetch in slot order."""
    by_loc = {loc: name for name, (ty, loc) in vs.prog['inputs'].items()}
    words = struct.unpack('>%dI' % (len(vblob) // 4), vblob)
    record = words[6] // 4
    location_of_slot = {w & 0xFFF: (0 if (w >> 12 & 0xF) == 0 else 1 if (w >> 12 & 0xF) == 10 else
                                    (w >> 16 & 0xF) + 2 if (w >> 16 & 0xF) else 2)
                        for w in words[record + 10:record + 10 + words[record + 7]]}
    # the simulator hands each fetch instruction over: its words name its slot
    slot_of = {}
    for rec in xu._decode_program(container.Container(vblob).ucode_dwords, 'vertex')[0]:
        if rec.kind == 'vfetch':
            d = rec.raw
            slot_of[tuple(d)] = rec.offset
    out_locs = {name: loc for name, (ty, loc) in vs.prog['outputs'].items()}
    push_reg = None
    for kind, dset, binding, reg, regs in vresources:
        if kind == 'push':
            push_reg = (reg, regs)
        else:
            return '%s.vert: resource %s not modelled' % (tag, kind), True
    stride = sum(c for _, _, c in attribs)
    runs = bad = 0
    worst = None
    for push, verts in draws[:12]:
        for k in range(len(verts) // stride if stride else 0):
            vals, inputs, at = verts[k * stride:(k + 1) * stride], {}, 0
            for name, loc, c in attribs:
                inputs[name] = vals[at:at + c]
                at += c
            ref = vs.run({n: (glsl_ref.Vec(v) if len(v) > 1 else v[0]) for n, v in inputs.items()}, push)
            consts = dict(vliterals)
            if push_reg:
                reg, regs = push_reg
                for n in range(regs):
                    consts[reg + n] = (push[4 * n:4 * n + 4] + [0.0] * 4)[:4]

            def fetch(n, f, i):
                name = by_loc.get(location_of_slot.get(slot_of.get((f.dword_0, f.dword_1, f.dword_2))))
                v = list(inputs.get(name, [0.0]))
                return (v + [0.0, 0.0, 0.0, 1.0][len(v):])[:4]
            res = simulate.run(vblob, {0: [0.0, 0.0, 0.0, 0.0]}, consts, vertex_fetch=fetch)
            runs += 1
            pairs = [('position', res.exports.get(int(xu.ExportRegister.kVSPosition)), vec4(ref['gl_Position']))]
            for name, loc in out_locs.items():
                if loc in fs_locs:                  # the link drops what the fragment shader doesn't read
                    n = glsl_ref.TYPES[vs.prog['outputs'][name][0]][1]   # the components the output declares
                    got = res.exports.get(loc)
                    pairs.append((name, got[:n] if got else None, vec4(ref[name])[:n]))
            diff = 0.0
            for name, got, want in pairs:
                if got is None:
                    diff = max(diff, 1.0)
                    continue
                for a, b in zip(got, want):
                    d = abs(a - b) / max(1.0, abs(b)) if not (math.isnan(a) or math.isnan(b)) else (0.0 if math.isnan(a) and math.isnan(b) else 1.0)
                    diff = max(diff, d)
            if diff > 1e-4:
                bad += 1
                if worst is None or diff > worst[0]:
                    worst = (diff, [(n, g, w) for n, g, w in pairs])
    line = '%s.vert: %d vertices, %d differ' % (tag, runs, bad)
    if worst:
        line += '\n    worst: ' + '; '.join('%s console %s exact %s' % (n, ['%.4f' % x for x in (g or [])],
                                                                   ['%.4f' % x for x in w]) for n, g, w in worst[1])
    return line, bad > 0


def check(tag, index, samples):
    vs_src = preprocessed(os.path.join(GLSL, tag + '.vert'))
    fs_path = os.path.join(GLSL, tag + '.frag')
    fs_src = preprocessed(fs_path)
    vs = glsl_ref.Interp(vs_src, single=True)
    fs = glsl_ref.Interp(fs_src, texture=texel, texture_size=lambda b: TEX_SIZE, single=True)
    blob, manifest, resources = native.compile_spirv(spirv(fs_path), XSDK_REGISTERS.pixel, XSDK_REGISTERS.vertex, True)
    records, _ = native.records(blob)
    params_reg = len(records)
    literals = {e[0]: list(e[1]) for e in manifest if len(e) == 2}
    slots = {}
    push_reg = None
    for kind, dset, binding, reg, regs in resources:
        if kind == 'sampler':
            slots[reg] = binding
        elif kind == 'push':
            push_reg = (reg, regs)
        else:
            return '%s: resource %s not modelled' % (tag, kind), True
    note = ''
    if index in samples:
        attribs, draws = samples[index]
    else:
        attribs, draws = synthetic(index, program_attribs()[index])
        note = ' (synthetic inputs: no run drew it yet)'

    in_names = {loc: name for name, (ty, loc) in fs.prog['inputs'].items()}
    in_types = {loc: ty for name, (ty, loc) in fs.prog['inputs'].items()}
    vpath = os.path.join(GLSL, tag + '.vert')
    vblob, vmanifest, vresources = native.compile_spirv(spirv(vpath), XSDK_REGISTERS.pixel, XSDK_REGISTERS.vertex, True)
    vliterals = {e[0]: list(e[1]) for e in vmanifest if len(e) == 2}
    vline, vbad = check_vertex(tag, vs, native.link(vblob, blob), vliterals, vresources, attribs, draws,
                               set(in_names))
    points = fragment_points(index, attribs, draws, vs)
    bad, worst, nan_sim = 0, None, 0
    for vary, push in points:
        consts = dict(literals)
        if push_reg:
            reg, regs = push_reg
            for n in range(regs):
                consts[reg + n] = (push[4 * n:4 * n + 4] + [0.0] * 4)[:4]
        consts[native.FRAG_COORD_CONSTANT] = [0.5, 0.5, 0.0, 0.0]
        regs_in = {loc: vary[loc] for loc in in_names if loc in vary}
        regs_in[params_reg] = vary['coord']
        inputs = {}
        for loc, name in in_names.items():
            if loc in vary:
                kind, n = glsl_ref.TYPES[in_types[loc]]
                inputs[name] = glsl_ref.Vec(vary[loc][:n]) if n > 1 else vary[loc][0]
        frag = [vary['coord'][0] + 0.5, vary['coord'][1] + 0.5, 0.5, 1.0]
        try:
            g = fs.run(inputs, push, frag)
            want = vec4(g['boi_out_FragColor'])
            want_depth = float(g['gl_FragDepth'])
        except Exception as e:
            return '%s: reference failed: %s' % (tag, e), True
        res = simulate.run(blob, regs_in, consts, texture_fetch=lambda slot, c: texel(slots.get(slot, 0), c[0], c[1]))
        got = res.exports.get(0, [float('nan')] * 4)
        got_depth = res.exports.get(EXPORT_DEPTH, [None])[0]
        if any(math.isnan(x) for x in got) and not any(math.isnan(x) for x in want):
            nan_sim += 1
        diff = max(abs(clamp01(a) - clamp01(b)) for a, b in zip(got, want))
        ddiff = 0.0 if got_depth is None else abs((got_depth or 0.0) - want_depth)
        dropped = (got_depth == 0.0) != (want_depth == 0.0) or res.killed
        if diff > TOL or ddiff > 1e-5 or dropped:
            bad += 1
            score = diff + ddiff + (1.0 if dropped else 0.0)
            if worst is None or score > worst[0]:
                worst = (score, got, want, got_depth, want_depth, inputs, push[16:24] if len(push) > 16 else [])
    line = vline + note + '\n%s.frag: %d points, %d differ' % (tag, len(points), bad)
    if nan_sim:
        line += ', %d NaN on the console only' % nan_sim
    if worst:
        _, got, want, gd, wd, inputs, upush = worst
        line += '\n    worst: console %s depth %s\n           exact   %s depth %s\n    inputs %s\n    push[16:] %s' % (
            ['%.4f' % x for x in got], gd, ['%.4f' % x for x in want], wd,
            {k: (glsl_ref.comps(v)) for k, v in inputs.items()}, upush)
    return line, bad > 0 or vbad


def main():
    samples = read_samples()
    tags = sys.argv[1:] or sorted(f[:-5] for f in os.listdir(GLSL) if f.endswith('.frag') and f[1:-5].isdigit())
    failed = False
    for tag in tags:
        line, bad = check(tag, int(tag[1:]), samples)
        failed = failed or bad
        print(line, flush=True)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
