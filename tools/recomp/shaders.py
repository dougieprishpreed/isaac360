"""Converts the game's GLSL 1.10 shaders to Vulkan SPIR-V for the GL-on-Vulkan layer (src/recomp/gl_vk.cpp).

The game compiles its shaders at run time from sources in its archives (resources/shaders, resources-dlc3/shaders)
and a few built into the exe. Neither the console nor vulkan4xenon has a GLSL front end at run time, so every
program the game can link is converted here, and the layer finds it again by the FNV-1a 64 hash of the sources
the game hands glShaderSource.

Sources: the archive files named in evidence/packages/*.json (x.vs pairs with x.fs in the same folder), plus the
built-in ones captured by a run with ISAAC360_DUMP_SHADERS=1 (build/recomp/shaders/NNN.glsl and programs.txt, one
linked program a line).

Conversion, per program (vertex + fragment):
- attributes get locations in declaration order; the layer answers glGetAttribLocation with them;
- varyings take the vertex shader's declaration order, the fragment shader follows it by name;
- non-sampler uniforms go to one push-constant block (Transform: a mat4 must be a block member for xost);
- samplers are combined image samplers at set 0, bindings in declaration order (fragment, then vertex);
- gl_FragColor becomes an output, texture2D texture, clip z is remapped to Vulkan's 0..1;
- the fixed-function alpha test the game keeps on (GREATER 0) ends each fragment shader.
Everything is drawn in GL's own row order (row 0 = GL's bottom row), so gl_FragCoord, viewports and render
targets need no flips; the layer flips once when it presents.

Output: build/recomp/gl_shaders.inc (SPIR-V words + per-program metadata strings), and each module as
build/recomp/spv/glvk_N.<stage>.spv for the console's shader bundle (vulkan4xenon's tools/build_shaders.py).

    py -3 tools/recomp/shaders.py
"""
import glob
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DUMPS = os.path.join(ROOT, 'build', 'recomp', 'shaders')
OUT = os.path.join(ROOT, 'build', 'recomp', 'gl_shaders.inc')
SPV_DIR = os.path.join(ROOT, 'build', 'recomp', 'spv')
GLSLANG = os.environ.get('GLSLANG', '/opt/ctr360-xsdk/glslang/build-host/StandAlone/glslang')
DISTRO = os.environ.get('ISAAC360_WSL_DISTRO', 'CTR360-XSDK')

DECL = re.compile(r'^[ \t]*(attribute|varying|uniform)[ \t]+(?:(?:lowp|mediump|highp)[ \t]+)?(\w+)[ \t]+([\w \t,\[\]]+?)[ \t]*;[^\n]*$', re.M)
SIZES = {'float': (4, 4, 1), 'vec2': (8, 8, 2), 'vec3': (12, 16, 3), 'vec4': (16, 16, 4), 'mat4': (64, 16, 16),
         'int': (4, 4, 1), 'bool': (4, 4, 1)}


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def decls(src):
    out = []
    for m in DECL.finditer(src):
        for name in m.group(3).split(','):
            name = name.strip()
            if name:
                out.append((m.group(1), m.group(2), name, m.start(), m.end()))
    return out


def strip_decls(src, kinds):
    """Removes the declarations of the given kinds (their lines become blank, keeping line numbers)."""
    return DECL.sub(lambda m: '' if m.group(1) in kinds else m.group(0), src)


def push_block(uniforms):
    """[(type, name)] -> (glsl block, [(name, offset, size, type)])."""
    if not uniforms:
        return '', []
    off, meta, members = 0, [], []
    for t, n in uniforms:
        size, align, _ = SIZES[t]
        off = (off + align - 1) & ~(align - 1)
        meta.append((n, off, size, t))
        members.append('layout(offset = %d) %s %s;' % (off, t, n))
        off += size
    if off > 128:
        raise SystemExit('push constants over 128 bytes: %r' % meta)
    glsl = 'layout(push_constant) uniform BoiPush { %s } boi_pc;\n' % ' '.join(members)
    glsl += ''.join('#define %s boi_pc.%s\n' % (n, n) for _, n in uniforms)
    return glsl, meta


def header():
    return '#version 450\n#define texture2D texture\n'


# xost (vulkan4xenon's shader compiler) traps (memory notes from the other xSDK ports): sin/cos are the bare
# hardware ops, right only in -pi..pi, so the angle is wrapped first; a second length() in a shader is
# miscompiled, sqrt(dot()) is not. The helpers stand in for the built-ins in the game's code.
SAFE_HELPERS = '''float boi_wrap(float x) { return x - 6.28318531 * floor(x * 0.159154943 + 0.5); }
vec2 boi_wrap(vec2 x) { return x - 6.28318531 * floor(x * 0.159154943 + 0.5); }
vec3 boi_wrap(vec3 x) { return x - 6.28318531 * floor(x * 0.159154943 + 0.5); }
vec4 boi_wrap(vec4 x) { return x - 6.28318531 * floor(x * 0.159154943 + 0.5); }
#define boi_sin(x) sin(boi_wrap(x))
#define boi_cos(x) cos(boi_wrap(x))
float boi_length(float v) { return abs(v); }
float boi_length(vec2 v) { return sqrt(dot(v, v)); }
float boi_length(vec3 v) { return sqrt(dot(v, v)); }
float boi_length(vec4 v) { return sqrt(dot(v, v)); }
#define boi_distance(a, b) boi_length((a) - (b))
'''
SAFE_NAMES = [('sin', 'boi_sin'), ('cos', 'boi_cos'), ('length', 'boi_length'), ('distance', 'boi_distance')]


def call_args(src, open_at):
    """The top-level arguments of the call whose '(' is at open_at, and the index after its ')'."""
    depth, args, start = 0, [], open_at + 1
    for i in range(open_at, len(src)):
        ch = src[i]
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                args.append(src[start:i])
                return args, i + 1
        elif ch == ',' and depth == 1:
            args.append(src[start:i])
            start = i + 1
    return None, open_at


def small_powers(src):
    """pow(x, 2.0|3.0|4.0) -> multiplications, as the PC's drivers fold them: right for a negative x too (pow's
    exp2(y * log2(x)) gives NaN there), and xost needs far fewer registers (coloroffset_gold: 144 -> under 64).
    Every other pow -> boi_powf, exp2(log2(x) * y) spelt out: xost's own pow leaves a wrong value in the other
    lanes of a vector built from pow results (vec2(a * pow(s, 0.4), b * pow(s, 0.4)): y wrong; water_v2's waves,
    found by tools/recomp/check_shaders.py), where the same maths written out is exact."""
    out, i = [], 0
    for m in re.finditer(r'\bpow\s*\(', src):
        if m.start() < i:
            continue
        args, end = call_args(src, m.end() - 1)
        if not args or len(args) != 2:
            continue
        if re.fullmatch(r'\s*[234]\.0*\s*', args[1]):
            out.append(src[i:m.start()] + 'boi_pow%d(%s)' % (int(float(args[1])), small_powers(args[0])))
        else:
            out.append(src[i:m.start()] + 'boi_powf(%s, %s)' % (small_powers(args[0]), small_powers(args[1])))
        i = end
    return ''.join(out) + src[i:]


POW_HELPERS = ''.join('%s boi_pow2(%s x) { return x * x; }\n%s boi_pow3(%s x) { return x * x * x; }\n'
                      '%s boi_pow4(%s x) { %s y = x * x; return y * y; }\n' % ((t,) * 7)
                      for t in ('float', 'vec2', 'vec3', 'vec4'))
POW_HELPERS += ''.join('%s boi_powf(%s x, %s y) { return exp2(log2(x) * y); }\n' % ((t,) * 3)
                       for t in ('float', 'vec2', 'vec3', 'vec4'))


def safe_calls(src):
    for name, repl in SAFE_NAMES:
        src = re.sub(r'\b%s(\s*\()' % name, repl + r'\1', src)
    return small_powers(src)


# a local vector declared without a value and then filled a component at a time beside an if/else is refused by
# xost ("part of %N stored before the whole of it"): such declarations start at zero (GLSL leaves them undefined)
UNINIT = re.compile(r'^([ \t]*)((?:lowp|mediump|highp)[ \t]+)?(float|vec[234]|mat[234]|int)[ \t]+(\w+)[ \t]*;', re.M)


def init_locals(src):
    return UNINIT.sub(lambda m: '%s%s%s %s = %s(0);' % (m.group(1), m.group(2) or '', m.group(3), m.group(4), m.group(3)), src)


def convert_vertex(src, uniforms, samplers):
    # GLSL 1.10 lets a shader read what it wrote to its outputs; xost cannot load from an output. So every output
    # (the varyings, gl_Position) is a private global the shader works on, copied out once at the end
    d = decls(src)
    attrs = [(t, n) for k, t, n, _, _ in d if k == 'attribute']
    vary = [(t, n) for k, t, n, _, _ in d if k == 'varying']
    g = header()
    for i, (t, n) in enumerate(attrs):
        g += 'layout(location = %d) in %s %s;\n' % (i, t, n)
    for i, (t, n) in enumerate(vary):
        g += 'layout(location = %d) out %s boi_out_%s;\n%s %s;\n' % (i, t, n, t, n)
    g += 'vec4 boi_Position;\n#define gl_Position boi_Position\n'
    # the window depth each fragment's alpha test needs, right after the game's varyings (gl_FragCoord's pixel
    # parameters follow the inputs in the 16 interpolators)
    g += 'layout(location = %d) out float boi_depth;\n' % len(vary)
    g += push_block(uniforms)[0]
    for b, (t, n) in samplers:
        g += 'layout(set = 0, binding = %d) uniform %s %s;\n' % (b, t, n)
    g += SAFE_HELPERS + POW_HELPERS
    g += '#define main boi_main\n#line 1\n'
    g += safe_calls(init_locals(strip_decls(src, ('attribute', 'varying', 'uniform'))))
    # clip z from GL's -w..w to Vulkan's 0..w; the window depth for the fragment shader's alpha test
    g += '\n#undef main\n#undef gl_Position\nvoid main() {\n    boi_main();\n'
    g += ''.join('    boi_out_%s = %s;\n' % (n, n) for _, n in vary)
    g += '    gl_Position = vec4(boi_Position.xy, (boi_Position.z + boi_Position.w) * 0.5, boi_Position.w);\n'
    g += '    boi_depth = clamp((boi_Position.z / boi_Position.w) * 0.5 + 0.5, 0.0, 1.0);\n}\n'
    return g, attrs, [n for _, n in vary]


# Programs whose depth tail stays the branch it was (the kill flag joined by &&), which keeps xost's optimiser off
# them: dizzy's angle wrap (x - 2 pi floor(x / 2 pi + 0.5) at large times) fused into one multiply-add by the
# optimiser moves its colours past check_shaders.py's 2/255 at 4 of 96 points.
BRANCH_TAIL = {'resources-dlc3/shaders/dizzy'}


def convert_fragment(src, vs_vary, uniforms, samplers, label=''):
    d = decls(src)
    g = header()
    g += 'layout(location = 0) out vec4 boi_out_FragColor;\nvec4 boi_FragColor;\n#define gl_FragColor boi_FragColor\n'
    kills = re.search(r'\bdiscard\b', src) is not None or label in BRANCH_TAIL
    g += 'layout(location = %d) in float boi_depth;\n' % len(vs_vary)
    if kills:
        g += 'float boi_keep = 1.0;\n'
    loc = {n: i for i, n in enumerate(vs_vary)}
    extra = len(vs_vary) + 1
    for k, t, n, _, _ in d:
        if k == 'varying':
            if n not in loc:
                loc[n] = extra
                extra += 1
            g += 'layout(location = %d) in %s %s;\n' % (loc[n], t, n)
    g += push_block(uniforms)[0]
    for b, (t, n) in samplers:
        g += 'layout(set = 0, binding = %d) uniform %s %s;\n' % (b, t, n)
    g += SAFE_HELPERS + POW_HELPERS
    # the game's own discards set a flag (the shader runs on; what it writes is dropped below)
    body = re.sub(r'\bdiscard\b', 'boi_keep = 0.0', init_locals(strip_decls(src, ('varying', 'uniform'))))
    g += '#define main boi_main\n#line 1\n' + safe_calls(body)
    # The compatibility profile's alpha test (on once, GREATER 0, never changed) keeps transparent texels out of
    # the depth buffer. xost refuses a discard beside the shaders' own if/else (and recurses without end on
    # some), so a fragment to drop is written at depth 0 instead: the game always draws with the depth test
    # GREATER against a depth cleared to 0 (-1000 clamped), so it fails the test and writes neither colour nor
    # depth, as a discarded one (gl_vk.cpp logs a draw with other depth state). A select, not a branch: a test
    # joined by && (as the kill flag once was) is one for xost, and its optimiser then leaves the whole shader as
    # written (colormod 49 instructions, 12 without), so a discard clears a factor instead.
    g += '\n#undef main\nvoid main() {\n    boi_main();\n'
    if label in BRANCH_TAIL:
        g += '    gl_FragDepth = mix(0.0, boi_depth, boi_keep > 0.0 && boi_FragColor.a > 0.0);\n'
    else:
        g += '    gl_FragDepth = (boi_FragColor.a > 0.0 ? boi_depth : 0.0)%s;\n' % (' * boi_keep' if kills else '')
    g += '    boi_out_FragColor = boi_FragColor;\n}\n'
    return g


# the layer's own present pass: the default framebuffer (GL row order) to the display, flipped. Each output pixel is
# the average of the frame's pixels under it, weighted by how much of it each covers: the console's 960x540 picture
# grows by 4/3 to 1280x720 with every pixel the same size and only the edges between pixels blended, where nearest
# pixels or one bilinear tap of a doubled frame give some pixels one column and others two. A footprint narrower
# than a texel crosses at most one texel edge, so one bilinear tap, placed by the share of each side, is the
# average. ISAAC360_PRESENT_FILTER=doubled (gl_vk.cpp) gives console boot 11's look instead: the frame doubled with
# nearest pixels, then one bilinear tap (what a 1920x1080 window gave). Both are worked out, one is kept (no branch).
# size: the frame's width and height, 1 / those; step.xy: texels an output pixel spans (under 1 here); step.z: 1
# for doubled
PRESENT_VS = '''#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }
'''
PRESENT_FS = '''#version 450
layout(set = 0, binding = 0) uniform sampler2D image;
layout(push_constant) uniform Present { vec4 size; vec4 step; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 color;
void main() {
    vec2 u = v_uv * pc.size.xy;
    vec2 r = pc.step.xy * 0.5;
    vec2 edge = floor(u + r);
    vec2 before = clamp((edge - (u - r)) / pc.step.xy, 0.0, 1.0);
    vec2 coverage = edge + 0.5 - before;
    vec2 p = u * 2.0 - 0.5;
    vec2 i0 = floor(p);
    vec2 t0 = floor(i0 * 0.5);
    vec2 t1 = floor((i0 + 1.0) * 0.5);
    vec2 doubled = t0 + 0.5 + (t1 - t0) * (p - i0);
    color = vec4(texture(image, mix(coverage, doubled, pc.step.z) * pc.size.zw).rgb, 1.0);
}
'''
# where the display holds the frame a whole number of times (960x540 on a 1920x1080 display) the nearest texel is
# the area average already: one sample, against PRESENT_FS's ~20 instructions over 2 million pixels a frame
PRESENT_COPY_FS = '''#version 450
layout(set = 0, binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 color;
void main() { color = vec4(texture(image, v_uv).rgb, 1.0); }
'''


# Rewrites of single shaders xost cannot take as they are, the same maths: (shader, pattern, replacement).
# water_v2.fs: its "if (current) { ... }" after the long gradient chain sends xost's lowering past its depth limit
# under control flow; computed always and selected (the skipped terms are finite for a zero direction).
REWRITES = [
    ('resources/shaders/water_v2', re.compile(
        r'if\s*\(\s*CurrentDir0\.x\s*!=\s*0\.0\s*\|\|\s*CurrentDir0\.y\s*!=\s*0\.0\s*\)\s*\{\s*'
        r'gradientSum\s*\*=\s*0\.5\s*;\s*'
        r'gradientSum\s*-=\s*(waterGradientLinear\([^;]*\))\s*;\s*'
        r'gradientSum\s*-=\s*(waterGradientLinear\([^;]*\))\s*;\s*\}'),
     r'{ vec2 boi_current = gradientSum * 0.5 - \1 - \2; '
     r'gradientSum = mix(gradientSum, boi_current, bvec2(CurrentDir0.x != 0.0 || CurrentDir0.y != 0.0)); }'),
    # Each wave term's sine, cosine and power once, not once per component: the same operations in the same order on
    # each value (xost does not share them: 358 lines for the six terms, the floors' water in Downpour and Dross)
    ('resources/shaders/water_v2', re.compile(
        r'return vec2\(\s*exponent \* d\.x \* amp \* pow\(0\.5 \* \(sin\(dot\(d, pos\) \* freq \+ time \* speed\) '
        r'\+ 1\.0\), exponent - 1\.0\)\s*\* cos\(dot\(d, pos\) \* freq \+ time \* speed\),\s*'
        r'exponent \* d\.y \* amp \* pow\(0\.5 \* \(sin\(dot\(d, pos\) \* freq \+ time \* speed\) \+ 1\.0\), '
        r'exponent - 1\.0\)\s*\* cos\(dot\(d, pos\) \* freq \+ time \* speed\)\s*\);'),
     r'float boi_a = dot(d, pos) * freq + time * speed; '
     r'float boi_p = pow(0.5 * (sin(boi_a) + 1.0), exponent - 1.0); float boi_c = cos(boi_a); '
     r'return vec2(exponent * d.x * amp * boi_p * boi_c, exponent * d.y * amp * boi_p * boi_c);'),
    ('resources/shaders/water_v2', re.compile(
        r'return vec2\(\s*exponent \* dir\.x \* amp \* pow\(0\.5 \* \(sin\(dot\(dir, pos\) \* freq \+ time \* '
        r'spd\.x\) \+ 1\.0\), exponent - 1\.0\)\s*\* cos\(dot\(dir, pos\) \* freq \+ time \* spd\.x\),\s*'
        r'exponent \* dir\.y \* amp \* pow\(0\.5 \* \(sin\(dot\(dir, pos\) \* freq \+ time \* spd\.y\) \+ 1\.0\), '
        r'exponent - 1\.0\)\s*\* cos\(dot\(dir, pos\) \* freq \+ time \* spd\.y\)\s*\);'),
     r'float boi_ax = dot(dir, pos) * freq + time * spd.x; float boi_ay = dot(dir, pos) * freq + time * spd.y; '
     r'return vec2(exponent * dir.x * amp * pow(0.5 * (sin(boi_ax) + 1.0), exponent - 1.0) * cos(boi_ax), '
     r'exponent * dir.y * amp * pow(0.5 * (sin(boi_ay) + 1.0), exponent - 1.0) * cos(boi_ay));'),
]


# Shaders whose code xost makes too dear, written out again with the same operations in the same order on each
# value. hq4x.fs (the game's Filter option, the frame's last pass over the whole 960x540 picture): xost made 285
# lines of it (222 ALU instructions, 45 of them moves, 29 registers); with it boot 16's frames were 20-32 ms, 16.7
# without. Its twelve coordinates as vec4 pairs from one vec4 of offsets (each the same sum as the game's) take 263
# (202, 28 moves). Packing the weights into vec4 lanes cost more moves than it saved (272).

def bloom_fs():
    """The game's bloom (every pill shows it for a second) adds over the whole picture the squares of 48 texels, a
    block 6 wide and 8 high two texels apart (OutRatio: 2/1024), 0.05 each, and the console spent 36 ms a frame on
    it (49 reads a pixel, 53 registers: few pixels in flight). The block's sum is a sum of row sums: gl_vk.cpp runs
    BLOOM_ROWS_FS first, each texel's six squares along its row into an image of the scene's size (Texture1, at a
    sixth so they fit 8 bits), and this adds eight of those down the column: 15 reads a pixel. The taps sit on texel
    centres, so the rows pass reads the same texels; the 8-bit sums are within 1.2/255 of the game's at worst."""
    out = [
        '#ifndef GL_ES',
        '#  define lowp',
        '#endif',
        '',
        'varying lowp vec4 Color0;',
        'varying vec2 TexCoord0;',
        'varying float OutBloomAmount;',
        'varying vec2 OutRatio;',
        'uniform sampler2D Texture0;',
        'uniform sampler2D Texture1;',
        '',
        'void main(void)',
        '{',
        '\tvec2 texcoord = vec2(TexCoord0);',
        '\tgl_FragColor = Color0 * texture2D(Texture0, texcoord);',
        '\tvec4 base = texcoord.xyxy;',
        '\tvec4 ratio = OutRatio.xyxy;',
        '\tvec4 a = base + vec4(0.0, -4.0, 0.0, -3.0) * ratio;',
        '\tvec4 b = base + vec4(0.0, -2.0, 0.0, -1.0) * ratio;',
        '\tvec4 c = base + vec4(0.0, 0.0, 0.0, 1.0) * ratio;',
        '\tvec4 d = base + vec4(0.0, 2.0, 0.0, 3.0) * ratio;',
        '\tvec4 sum = texture2D(Texture1, a.xy) + texture2D(Texture1, a.zw) + texture2D(Texture1, b.xy) '
        '+ texture2D(Texture1, b.zw) + texture2D(Texture1, c.xy) + texture2D(Texture1, c.zw) '
        '+ texture2D(Texture1, d.xy) + texture2D(Texture1, d.zw);',
        '\tgl_FragColor += sum * (6.0 * 0.05) * OutBloomAmount;',
        '}',
    ]
    return '\n'.join(out) + '\n'


BLOOM_FS = bloom_fs()

# bloom's rows (gl_vk.cpp bloom_rows): each texel's six squares along its row, the game's taps at -3..2 times
# OutRatio.x, at a sixth; a quad over the area, the texel from gl_FragCoord (rows in GL order, as the targets are)
BLOOM_ROWS_FS = '''#version 450
layout(set = 0, binding = 0) uniform sampler2D image;
layout(push_constant) uniform Rows { vec4 size; vec4 step; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 color;
void main() {
    vec4 base = (gl_FragCoord.xy * pc.size.xy).xyxy;
    vec4 r = vec4(pc.step.x, 0.0, pc.step.x, 0.0);
    vec4 a = base + vec4(-3.0, 0.0, -2.0, 0.0) * r;
    vec4 b = base + vec4(-1.0, 0.0, 0.0, 0.0) * r;
    vec4 c = base + vec4(1.0, 0.0, 2.0, 0.0) * r;
    vec4 t0 = texture(image, a.xy), t1 = texture(image, a.zw), t2 = texture(image, b.xy);
    vec4 t3 = texture(image, b.zw), t4 = texture(image, c.xy), t5 = texture(image, c.zw);
    color = (t0 * t0 + t1 * t1 + t2 * t2 + t3 * t3 + t4 * t4 + t5 * t5) * (1.0 / 6.0);
}
'''

REPLACE = {
    'resources/shaders/hq4x': '''varying vec2 TexCoord0;
varying vec3 ScreenSizePow2Out;
uniform sampler2D Texture0;

void main(void)
{
	vec2 uv = TexCoord0;
	uv -= mod(TexCoord0, vec2(1.0, 1.0) / ScreenSizePow2Out.xy);

	const float mx = 1.0;
	const float k = -1.10;
	const float max_w = 0.75;
	const float min_w = 0.03;
	const float lum_add = 0.33;
	const vec3 dt = vec3(1.0, 1.0, 1.0);

	vec3 c = texture2D(Texture0, uv).xyz;

	float x = ScreenSizePow2Out.z / ScreenSizePow2Out.x;
	float y = ScreenSizePow2Out.z / ScreenSizePow2Out.y;

	vec4 dg = vec4(x, y, -x, y);
	vec4 sd = dg * 0.5;
	vec4 u = uv.xyxy;
	vec4 ti = u - sd;
	vec4 tj = u + sd;
	vec4 to = u - dg;
	vec4 tp = u + dg;
	vec4 ts = u + vec4(0.0, -y, x, 0.0);
	vec4 tt = u + vec4(0.0, y, -x, 0.0);

	vec3 i1 = texture2D(Texture0, ti.xy).xyz;
	vec3 i2 = texture2D(Texture0, ti.zw).xyz;
	vec3 i3 = texture2D(Texture0, tj.xy).xyz;
	vec3 i4 = texture2D(Texture0, tj.zw).xyz;

	vec3 o1 = texture2D(Texture0, to.xy).xyz;
	vec3 o2 = texture2D(Texture0, to.zw).xyz;
	vec3 o3 = texture2D(Texture0, tp.xy).xyz;
	vec3 o4 = texture2D(Texture0, tp.zw).xyz;

	vec3 s1 = texture2D(Texture0, ts.xy).xyz;
	vec3 s2 = texture2D(Texture0, ts.zw).xyz;
	vec3 s3 = texture2D(Texture0, tt.xy).xyz;
	vec3 s4 = texture2D(Texture0, tt.zw).xyz;

	float ko1 = dot(abs(o1-c),dt);
	float ko2 = dot(abs(o2-c),dt);
	float ko3 = dot(abs(o3-c),dt);
	float ko4 = dot(abs(o4-c),dt);

	float k1=min(dot(abs(i1-i3),dt),max(ko1,ko3));
	float k2=min(dot(abs(i2-i4),dt),max(ko2,ko4));

	float w1 = k2; if(ko3<ko1) w1*=ko3/ko1;
	float w2 = k1; if(ko4<ko2) w2*=ko4/ko2;
	float w3 = k2; if(ko1<ko3) w3*=ko1/ko3;
	float w4 = k1; if(ko2<ko4) w4*=ko2/ko4;

	c=(w1*o1+w2*o2+w3*o3+w4*o4+0.001*c)/(w1+w2+w3+w4+0.001);
	w1 = k*dot(abs(i1-c)+abs(i3-c),dt)/(0.125*dot(i1+i3,dt)+lum_add);
	w2 = k*dot(abs(i2-c)+abs(i4-c),dt)/(0.125*dot(i2+i4,dt)+lum_add);
	w3 = k*dot(abs(s1-c)+abs(s3-c),dt)/(0.125*dot(s1+s3,dt)+lum_add);
	w4 = k*dot(abs(s2-c)+abs(s4-c),dt)/(0.125*dot(s2+s4,dt)+lum_add);

	w1 = clamp(w1+mx,min_w,max_w);
	w2 = clamp(w2+mx,min_w,max_w);
	w3 = clamp(w3+mx,min_w,max_w);
	w4 = clamp(w4+mx,min_w,max_w);

	gl_FragColor = vec4((w1*(i1+i3)+w2*(i2+i4)+w3*(s1+s3)+w4*(s2+s4)+c)/(2.0*(w1+w2+w3+w4)+1.0), 1.0);
}
''',
    'resources/shaders/bloom': BLOOM_FS,
}

# coloroffset.fs (nearly every sprite) works out its clip plane, pixelation, colorize, colour offset and colour
# reduction for each pixel; with them all off (ColorizeIn.a, ColorOffsetIn, PixelationAmount 0, the clip plane 0 0
# and z <= 0) its colour is Color0 times the texel exactly (mix by 0, adding 0), and the Death card's smoke over the
# whole room took the console's GPU to 20.5 ms. gl_vk.cpp draws those draws with this program instead (the same
# vertex stage), checking each vertex as it packs them.
PLAIN_VARIANTS = {
    'resources/shaders/coloroffset': '''varying vec4 Color0;
varying vec2 TexCoord0;
uniform sampler2D Texture0;

void main(void)
{
	gl_FragColor = Color0 * texture2D(Texture0, TexCoord0);
}
''',
}


def rewrite(label, stage_src):
    if label in REPLACE:
        return REPLACE[label]
    for name, pattern, repl in REWRITES:
        if label == name:
            stage_src, n = pattern.subn(repl, stage_src)
            if n != 1:
                raise SystemExit('rewrite for %s matched %d times' % (label, n))
    return stage_src


def program(vs, fs, label=''):
    """Both stages' GLSL 450 and the layer's metadata for one linked program."""
    fs = rewrite(label, fs)
    vd, fd = decls(vs), decls(fs)
    uniforms, samplers, seen = [], [], set()
    for k, t, n, _, _ in fd + vd:
        if k != 'uniform' or n in seen:
            continue
        seen.add(n)
        if t.startswith('sampler'):
            samplers.append((len(samplers), (t, n)))
        else:
            uniforms.append((t, n))
    # the vertex stage declares the uniforms it uses; both declare the whole block at the same offsets
    vglsl, attrs, vary = convert_vertex(vs, uniforms, [s for s in samplers if s[1][1] in {n for _, _, n, _, _ in vd}])
    fglsl = convert_fragment(fs, vary, uniforms, [s for s in samplers if s[1][1] in {n for _, _, n, _, _ in fd}],
                             label)
    meta = {
        'attribs': ','.join('%s:%d:%d' % (n, i, SIZES[t][2]) for i, (t, n) in enumerate(attrs)),
        'samplers': ','.join('%s:%d' % (n, b) for b, (_, n) in samplers),
        'uniforms': ','.join('%s:%d:%d' % (n, o, s) for n, o, s, _ in push_block(uniforms)[1]),
    }
    return vglsl, fglsl, meta


def wsl_path(p):
    p = os.path.abspath(p).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]


def compile_spv(glsl, stage, tmp, tag):
    src = os.path.join(tmp, tag + '.' + stage)
    dst = src + '.spv'
    open(src, 'wb').write(glsl.encode())
    if os.environ.get('ISAAC360_SHADER_GLSL'):        # keep the converted sources (build/recomp/glsl) to read
        keep = os.path.join(ROOT, 'build', 'recomp', 'glsl')
        os.makedirs(keep, exist_ok=True)
        open(os.path.join(keep, tag + '.' + stage), 'wb').write(glsl.encode())
    if os.path.isabs(GLSLANG) and os.path.exists(GLSLANG):
        cmd = [GLSLANG, '-V', '-o', dst, src]
    else:
        cmd = ['wsl.exe', '-d', DISTRO, '--', GLSLANG, '-V', '-o', wsl_path(dst), wsl_path(src)]
    r = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    if r.returncode != 0:
        sys.stderr.write(glsl + '\n' + r.stdout + r.stderr)
        raise SystemExit('glslang failed on ' + tag + '.' + stage)
    data = open(dst, 'rb').read()
    return struct.unpack('<%dI' % (len(data) // 4), data)


def sources():
    """[(label, vs bytes, fs bytes)] for every program the game can link."""
    progs, files = [], {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'evidence', 'packages', '*.json'))):
        for e in json.load(open(f)).get('entries', []):
            n = e.get('resolved_name') or ''
            if '/shaders/' in n.lower() and n.lower().endswith(('.vs', '.fs')):
                files[n.lower()] = os.path.join(ROOT, e['output'])
    for n in sorted(files):
        if n.endswith('.vs') and n[:-3] + '.fs' in files:
            progs.append((n[:-3], open(files[n], 'rb').read(), open(files[n[:-3] + '.fs'], 'rb').read()))
    listing = os.path.join(DUMPS, 'programs.txt')
    if os.path.exists(listing):
        for line in open(listing):
            ids = line.split()
            if len(ids) == 2:
                rd = lambda i: open(os.path.join(DUMPS, i + '.glsl'), 'rb').read()
                progs.append(('dump/' + '+'.join(ids), rd(ids[0]), rd(ids[1])))
    else:
        print('note: no', os.path.relpath(listing, ROOT), '(run the game once with ISAAC360_DUMP_SHADERS=1 for the built-in shaders)')
    unique, seen = [], set()
    for label, vs, fs in progs:
        key = (fnv64(vs), fnv64(fs))
        if key not in seen:
            seen.add(key)
            unique.append((label, vs, fs))
    return unique


def main():
    progs = sources()
    blobs, blob_index, stages = [], {}, []

    def blob(words, stage):
        key = tuple(words)
        if key not in blob_index:
            blob_index[key] = len(blobs)
            blobs.append(words)
            stages.append(stage)
        return blob_index[key]

    rows = []
    os.makedirs(os.path.join(ROOT, 'build', 'recomp'), exist_ok=True)
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, 'build', 'recomp')) as tmp:
        for i, (label, vs, fs) in enumerate(progs):
            vglsl, fglsl, meta = program(vs.decode('latin-1'), fs.decode('latin-1'), label)
            tag = 'p%02d' % i
            v = blob(compile_spv(vglsl, 'vert', tmp, tag), 'vert')
            f = blob(compile_spv(fglsl, 'frag', tmp, tag), 'frag')
            rows.append((label, fnv64(vs), fnv64(fs), v, f, meta))
            print('%-40s vs %5d words, fs %5d words  %s' % (label, len(blobs[v]), len(blobs[f]), meta['attribs']))
            if label in PLAIN_VARIANTS:                 # its plain twin: label#plain, never linked by the game
                plain = PLAIN_VARIANTS[label]
                vglsl, fglsl, pmeta = program(vs.decode('latin-1'), plain, label + '#plain')
                if pmeta != meta:
                    raise SystemExit('the plain variant of %s lays out its inputs differently' % label)
                pv = blob(compile_spv(vglsl, 'vert', tmp, tag + 'p'), 'vert')
                pf = blob(compile_spv(fglsl, 'frag', tmp, tag + 'p'), 'frag')
                rows.append((label + '#plain', fnv64(vs), fnv64(plain.encode()), pv, pf, pmeta))
                print('%-40s fs %5d words' % (label + '#plain', len(blobs[pf])))
        present = (blob(compile_spv(PRESENT_VS, 'vert', tmp, 'present'), 'vert'),
                   blob(compile_spv(PRESENT_FS, 'frag', tmp, 'present'), 'frag'),
                   blob(compile_spv(PRESENT_COPY_FS, 'frag', tmp, 'present_copy'), 'frag'),
                   blob(compile_spv(BLOOM_ROWS_FS, 'frag', tmp, 'bloom_rows'), 'frag'))
    out = ['// Generated by tools/recomp/shaders.py from the game\'s shaders: do not edit.']
    for i, words in enumerate(blobs):
        out.append('static const uint32_t GLVK_SPV_%d[%d] = {' % (i, len(words)))
        for j in range(0, len(words), 8):
            out.append('    ' + ', '.join('0x%08x' % w for w in words[j:j + 8]) + ',')
        out.append('};')
    out.append('static const GlVkProgramDesc GLVK_PROGRAMS[] = {')
    for label, hv, hf, v, f, meta in rows:
        out.append('    {0x%016xull, 0x%016xull, GLVK_SPV_%d, %d, GLVK_SPV_%d, %d, "%s", "%s", "%s", "%s"},' % (
            hv, hf, v, len(blobs[v]), f, len(blobs[f]), meta['attribs'], meta['samplers'], meta['uniforms'], label))
    out.append('};')
    out.append('#define GLVK_PRESENT_VS GLVK_SPV_%d' % present[0])
    out.append('#define GLVK_PRESENT_FS GLVK_SPV_%d' % present[1])
    out.append('#define GLVK_PRESENT_COPY_FS GLVK_SPV_%d' % present[2])
    out.append('#define GLVK_BLOOM_ROWS_FS GLVK_SPV_%d' % present[3])
    open(OUT, 'wb').write(('\n'.join(out) + '\n').encode('ascii'))
    os.makedirs(SPV_DIR, exist_ok=True)
    for name in os.listdir(SPV_DIR):
        if name.endswith('.spv'):
            os.remove(os.path.join(SPV_DIR, name))
    for i, words in enumerate(blobs):
        open(os.path.join(SPV_DIR, 'glvk_%d.%s.spv' % (i, stages[i])), 'wb').write(struct.pack('<%dI' % len(words), *words))
    print('%d programs, %d SPIR-V modules -> %s' % (len(rows), len(blobs), os.path.relpath(OUT, ROOT)))


if __name__ == '__main__':
    main()
