"""A small GLSL interpreter in double precision: the reference tools/recomp/check_shaders.py compares the console's
compiled shaders with. It runs the converted programs (tools/recomp/shaders.py, after glslang -E): floats, ints, bools,
vectors, mat2-4, swizzles, user functions with overloads, if/for/while, the built-in functions those programs call.
Values: float/int/bool, Vec (list of components, kind 'f'/'i'/'b'), Mat (columns). NaN and infinities come out as
GLSL gives them instead of raising.
"""
import math
import re
import struct

INF = float('inf')
NAN = float('nan')


class Vec:
    __slots__ = ('c', 'kind')

    def __init__(self, c, kind='f'):
        self.c = list(c)
        self.kind = kind

    def __repr__(self):
        return 'Vec(%s)' % self.c


class Mat:
    __slots__ = ('cols',)

    def __init__(self, cols):
        self.cols = [list(c) for c in cols]

    def __repr__(self):
        return 'Mat(%s)' % self.cols


class Sampler:
    def __init__(self, binding):
        self.binding = binding


TYPES = {'void': None, 'float': ('f', 1), 'int': ('i', 1), 'uint': ('i', 1), 'bool': ('b', 1),
         'vec2': ('f', 2), 'vec3': ('f', 3), 'vec4': ('f', 4), 'ivec2': ('i', 2), 'ivec3': ('i', 3), 'ivec4': ('i', 4),
         'bvec2': ('b', 2), 'bvec3': ('b', 3), 'bvec4': ('b', 4), 'mat2': ('m', 2), 'mat3': ('m', 3), 'mat4': ('m', 4),
         'sampler2D': ('s', 1)}
QUALIFIERS = {'const', 'in', 'out', 'inout', 'uniform', 'flat', 'smooth', 'highp', 'mediump', 'lowp', 'centroid',
              'invariant'}
SWIZ = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3, 's': 0, 't': 1, 'p': 2, 'q': 3}

TOKEN = re.compile(r'\s*(?:(\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\.\d+(?:[eE][-+]?\d+)?[fF]?|\d+[eE][-+]?\d+[fF]?|\d+[uU]?)'
                   r'|([A-Za-z_]\w*)|(\+\+|--|\+=|-=|\*=|/=|==|!=|<=|>=|&&|\|\||\^\^|[-+*/%<>=!?:;,.(){}\[\]&|^~]))')


def tokenize(src):
    src = '\n'.join(l for l in src.split('\n') if not l.lstrip().startswith('#'))
    src = re.sub(r'//[^\n]*', '', src)
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    out, pos = [], 0
    while True:
        m = TOKEN.match(src, pos)
        if not m:
            if src[pos:].strip():
                raise SyntaxError('glsl_ref: cannot read %r' % src[pos:pos + 40])
            return out
        pos = m.end()
        if m.group(1):
            t = m.group(1).rstrip('fFuU')
            out.append(('num', float(t) if ('.' in t or 'e' in t or 'E' in t) else int(t)))
        elif m.group(2):
            out.append(('id', m.group(2)))
        else:
            out.append(('op', m.group(3)))


# ---- parser: tuples (kind, ...) ---------------------------------------------------------------------------------
class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ('eof', None)

    def next(self):
        tok = self.peek()
        self.i += 1
        return tok

    def accept(self, v):
        if self.peek()[1] == v and self.peek()[0] != 'num':
            self.i += 1
            return True
        return False

    def expect(self, v):
        tok = self.next()
        if tok[1] != v:
            raise SyntaxError('glsl_ref: wanted %r, got %r near token %d' % (v, tok, self.i))
        return tok

    def skip_layout(self):
        if self.accept('layout'):
            depth = 0
            while True:
                tok = self.next()
                if tok[1] == '(':
                    depth += 1
                elif tok[1] == ')':
                    depth -= 1
                    if depth == 0:
                        return

    def is_type(self, k=0):
        tok = self.peek(k)
        return tok[0] == 'id' and tok[1] in TYPES

    def program(self):
        """{'functions': {name: [(rtype, [(ptype, pname, qual)], body)]}, 'globals': [...], 'push': {...}}"""
        prog = {'functions': {}, 'globals': [], 'inputs': {}, 'outputs': {}, 'samplers': {}, 'push': {}}
        while self.peek()[0] != 'eof':
            if self.accept(';'):
                continue
            if self.peek()[1] == 'precision':
                while not self.accept(';'):
                    self.next()
                continue
            loc = None
            if self.peek()[1] == 'layout':
                start = self.i
                self.skip_layout()
                text = ' '.join(str(x[1]) for x in self.t[start:self.i])
                m = re.search(r'location = (\d+)', text)
                loc = int(m.group(1)) if m else None
                b = re.search(r'binding = (\d+)', text)
                binding = int(b.group(1)) if b else None
            quals = []
            while self.peek()[0] == 'id' and self.peek()[1] in QUALIFIERS:
                quals.append(self.next()[1])
            if 'uniform' in quals and self.peek()[0] == 'id' and self.peek()[1] not in TYPES:
                # the push constant block: uniform BoiPush { layout(offset = N) type name; ... } instance;
                self.next()
                self.expect('{')
                members = {}
                while not self.accept('}'):
                    start = self.i
                    self.skip_layout()
                    text = ' '.join(str(x[1]) for x in self.t[start:self.i])
                    off = int(re.search(r'offset = (\d+)', text).group(1))
                    ty = self.next()[1]
                    name = self.next()[1]
                    self.expect(';')
                    members[name] = (ty, off)
                inst = self.next()[1]
                self.expect(';')
                prog['push'] = members
                prog['push_instance'] = inst
                continue
            ty = self.next()[1]
            name = self.next()[1]
            if self.accept('('):
                params = []
                if not self.accept(')'):
                    while True:
                        q = None
                        while self.peek()[1] in QUALIFIERS:
                            q = self.next()[1]
                        pty = self.next()[1]
                        if pty == 'void' and self.peek()[1] == ')':
                            self.next()
                            break
                        pname = self.next()[1]
                        params.append((pty, pname, q))
                        if self.accept(')'):
                            break
                        self.expect(',')
                if self.accept(';'):
                    continue
                body = self.block()
                prog['functions'].setdefault(name, []).append((ty, params, body))
                continue
            if 'in' in quals:
                prog['inputs'][name] = (ty, loc)
                self.expect(';')
                continue
            if 'out' in quals:
                prog['outputs'][name] = (ty, loc)
                self.expect(';')
                continue
            if ty == 'sampler2D':
                prog['samplers'][name] = binding
                self.expect(';')
                continue
            decls = self.declarators(ty, name)
            prog['globals'].append(('decl', ty, decls))
        return prog

    def declarators(self, ty, name):
        decls = []
        while True:
            init = self.expr() if self.accept('=') else None
            decls.append((name, init))
            if self.accept(';'):
                return decls
            self.expect(',')
            name = self.next()[1]

    def block(self):
        self.expect('{')
        stmts = []
        while not self.accept('}'):
            stmts.append(self.statement())
        return ('block', stmts)

    def statement(self):
        tok = self.peek()
        if tok[1] == '{':
            return self.block()
        if tok[1] == ';':
            self.next()
            return ('block', [])
        if tok[1] == 'if' and tok[0] == 'id':
            self.next()
            self.expect('(')
            cond = self.expr()
            self.expect(')')
            then = self.statement()
            other = self.statement() if self.accept('else') else None
            return ('if', cond, then, other)
        if tok[1] == 'for' and tok[0] == 'id':
            self.next()
            self.expect('(')
            init = self.statement()
            cond = None if self.peek()[1] == ';' else self.expr()
            self.expect(';')
            step = None if self.peek()[1] == ')' else self.expr()
            self.expect(')')
            return ('for', init, cond, step, self.statement())
        if tok[1] == 'while' and tok[0] == 'id':
            self.next()
            self.expect('(')
            cond = self.expr()
            self.expect(')')
            return ('for', ('block', []), cond, None, self.statement())
        if tok[1] in ('return', 'break', 'continue', 'discard') and tok[0] == 'id':
            self.next()
            value = None
            if tok[1] == 'return' and self.peek()[1] != ';':
                value = self.expr()
            self.expect(';')
            return (tok[1], value)
        while self.peek()[0] == 'id' and self.peek()[1] in QUALIFIERS:
            self.next()
        if self.is_type() and self.peek(1)[0] == 'id':
            ty = self.next()[1]
            name = self.next()[1]
            return ('decl', ty, self.declarators(ty, name))
        e = self.expr()
        self.expect(';')
        return ('expr', e)

    # expressions
    def expr(self):
        e = self.assign()
        while self.peek()[1] == ',' and False:
            pass
        return e

    def assign(self):
        lhs = self.ternary()
        tok = self.peek()
        if tok[0] == 'op' and tok[1] in ('=', '+=', '-=', '*=', '/='):
            self.next()
            return ('assign', tok[1], lhs, self.assign())
        return lhs

    def ternary(self):
        c = self.binary(0)
        if self.accept('?'):
            a = self.assign()
            self.expect(':')
            b = self.assign()
            return ('?', c, a, b)
        return c

    LEVELS = [('||',), ('^^',), ('&&',), ('==', '!='), ('<', '>', '<=', '>='), ('+', '-'), ('*', '/', '%')]

    def binary(self, level):
        if level == len(self.LEVELS):
            return self.unary()
        e = self.binary(level + 1)
        while self.peek()[0] == 'op' and self.peek()[1] in self.LEVELS[level]:
            op = self.next()[1]
            e = ('bin', op, e, self.binary(level + 1))
        return e

    def unary(self):
        tok = self.peek()
        if tok[0] == 'op' and tok[1] in ('-', '+', '!', '++', '--'):
            self.next()
            return ('un', tok[1], self.unary())
        return self.postfix()

    def postfix(self):
        e = self.primary()
        while True:
            if self.accept('.'):
                e = ('member', e, self.next()[1])
            elif self.accept('['):
                i = self.expr()
                self.expect(']')
                e = ('index', e, i)
            elif self.peek()[1] in ('++', '--') and self.peek()[0] == 'op':
                e = ('post', self.next()[1], e)
            else:
                return e

    def primary(self):
        tok = self.next()
        if tok[0] == 'num':
            return ('lit', tok[1])
        if tok[1] == '(':
            e = self.expr()
            self.expect(')')
            return e
        if tok[0] == 'id':
            if tok[1] in ('true', 'false'):
                return ('lit', tok[1] == 'true')
            if self.accept('('):
                args = []
                if not self.accept(')'):
                    while True:
                        args.append(self.assign())
                        if self.accept(')'):
                            break
                        self.expect(',')
                return ('call', tok[1], args)
            return ('var', tok[1])
        raise SyntaxError('glsl_ref: unexpected %r' % (tok,))


# ---- values -----------------------------------------------------------------------------------------------------
def comps(v):
    if isinstance(v, Vec):
        return v.c
    if isinstance(v, Mat):
        return [x for c in v.cols for x in c]
    return [v]


def dim(v):
    return len(v.c) if isinstance(v, Vec) else (100 + len(v.cols) if isinstance(v, Mat) else 1)


def make(kind, n, values):
    """A value of a type from a flat list of components (constructor rules)."""
    if kind == 'm':
        if len(values) == 1:
            return Mat([[values[0] if i == j else 0.0 for i in range(n)] for j in range(n)])
        return Mat([[float(values[j * n + i]) for i in range(n)] for j in range(n)])
    if len(values) == 1 and n > 1:
        values = values * n
    conv = float if kind == 'f' else (int if kind == 'i' else bool)
    vals = []
    for x in values[:n]:
        if kind == 'i' and isinstance(x, float):
            vals.append(int(math.trunc(x)) if math.isfinite(x) else 0)
        else:
            vals.append(conv(x))
    return vals[0] if n == 1 else Vec(vals, kind)


def convert(ty, v):
    kind, n = TYPES[ty]
    if kind == 'm' or kind == 's':
        return v
    return make(kind, n, comps(v))


def default(ty):
    kind, n = TYPES[ty]
    if kind == 'm':
        return make('m', n, [0.0])
    return make(kind, n, [0.0 if kind == 'f' else (0 if kind == 'i' else False)])


def fdiv(a, b):
    if isinstance(a, int) and isinstance(b, int) and not isinstance(a, bool):
        return int(a / b) if b else 0
    a, b = float(a), float(b)
    if b == 0.0:
        if a == 0.0 or math.isnan(a):
            return NAN
        return math.copysign(INF, a) * math.copysign(1.0, b)
    try:
        return a / b
    except OverflowError:
        return INF


def fmul(a, b):
    try:
        return a * b
    except OverflowError:
        return INF


SCALAR_OPS = {'+': lambda a, b: a + b, '-': lambda a, b: a - b, '*': fmul, '/': fdiv,
              '<': lambda a, b: a < b, '>': lambda a, b: a > b, '<=': lambda a, b: a <= b, '>=': lambda a, b: a >= b,
              '%': lambda a, b: a % b}


def mat_vec(m, v):
    n = len(m.cols)
    return Vec([sum(m.cols[j][i] * v.c[j] for j in range(n)) for i in range(n)])


def vec_mat(v, m):
    return Vec([sum(v.c[i] * col[i] for i in range(len(col))) for col in m.cols])


def binop(op, a, b):
    if op == '*' and isinstance(a, Mat):
        if isinstance(b, Vec):
            return mat_vec(a, b)
        if isinstance(b, Mat):
            return Mat([mat_vec(a, Vec(c)).c for c in b.cols])
        return Mat([[x * b for x in c] for c in a.cols])
    if op == '*' and isinstance(b, Mat):
        if isinstance(a, Vec):
            return vec_mat(a, b)
        return Mat([[a * x for x in c] for c in b.cols])
    if isinstance(a, Mat) or isinstance(b, Mat):
        f = SCALAR_OPS[op]
        if isinstance(a, Mat) and isinstance(b, Mat):
            return Mat([[f(x, y) for x, y in zip(c, d)] for c, d in zip(a.cols, b.cols)])
        if isinstance(a, Mat):
            return Mat([[f(x, b) for x in c] for c in a.cols])
        return Mat([[f(a, x) for x in c] for c in b.cols])
    if op == '==':
        return comps(a) == comps(b)
    if op == '!=':
        return comps(a) != comps(b)
    if op == '&&':
        return bool(a) and bool(b)
    if op == '||':
        return bool(a) or bool(b)
    if op == '^^':
        return bool(a) != bool(b)
    f = SCALAR_OPS[op]
    if isinstance(a, Vec) or isinstance(b, Vec):
        n = len(a.c) if isinstance(a, Vec) else len(b.c)
        ca = a.c if isinstance(a, Vec) else [a] * n
        cb = b.c if isinstance(b, Vec) else [b] * n
        kind = a.kind if isinstance(a, Vec) else b.kind
        res = [f(x, y) for x, y in zip(ca, cb)]
        return Vec(res, 'b' if op in ('<', '>', '<=', '>=') else kind)
    return f(a, b)


def mapc(f, *args):
    """f over the components of vector arguments (scalars broadcast)."""
    n = max(len(a.c) if isinstance(a, Vec) else 1 for a in args)
    if n == 1 and not any(isinstance(a, Vec) for a in args):
        return f(*args)
    cs = [a.c if isinstance(a, Vec) else [a] * n for a in args]
    return Vec([f(*xs) for xs in zip(*cs)])


def safe(f):
    def g(*xs):
        try:
            return f(*xs)
        except (ValueError, ZeroDivisionError):
            return NAN
        except OverflowError:
            return INF
    return g


def _pow(x, y):
    if x < 0.0:
        return NAN
    if x == 0.0:
        return 0.0 if y > 0.0 else (INF if y < 0.0 else NAN)
    return math.pow(x, y)


def _log(x):
    return -INF if x == 0.0 else (NAN if x < 0.0 else math.log(x))


def _sqrt(x):
    return NAN if x < 0.0 else math.sqrt(x)


def _isqrt(x):
    return INF if x == 0.0 else (NAN if x < 0.0 else 1.0 / math.sqrt(x))


def _mod(x, y):
    q = fdiv(x, y)
    return x - y * math.floor(q) if y != 0.0 and math.isfinite(q) else NAN


def _fract(x):
    return x - math.floor(x) if math.isfinite(x) else NAN


def _floor(x):
    return float(math.floor(x)) if math.isfinite(x) else x


def _ceil(x):
    return float(math.ceil(x)) if math.isfinite(x) else x


def _sign(x):
    return 1.0 if x > 0 else (-1.0 if x < 0 else 0.0)


def _clamp(x, lo, hi):
    return min(max(x, lo), hi)


def _smooth(e0, e1, x):
    t = _clamp(fdiv(x - e0, e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _atan(y, x=None):
    if x is None:
        return math.atan(y)
    return math.atan2(y, x)


def dot(a, b):
    return sum(x * y for x, y in zip(comps(a), comps(b)))


def builtin(name, args, interp):
    if name in TYPES and TYPES[name]:
        kind, n = TYPES[name]
        if kind == 'm' and len(args) == 1 and isinstance(args[0], Mat):
            m = args[0]
            return Mat([[m.cols[j][i] if j < len(m.cols) and i < len(m.cols) else (1.0 if i == j else 0.0)
                         for i in range(n)] for j in range(n)])
        flat = []
        for a in args:
            flat.extend(comps(a))
        return make(kind, n, flat)
    A = args
    simple = {'sin': safe(math.sin), 'cos': safe(math.cos), 'tan': safe(math.tan), 'asin': safe(math.asin),
              'acos': safe(math.acos), 'exp': safe(math.exp), 'exp2': safe(lambda x: 2.0 ** x), 'log': _log,
              'log2': lambda x: _log(x) / math.log(2.0) if x > 0 else _log(x), 'sqrt': _sqrt, 'inversesqrt': _isqrt,
              'abs': abs, 'sign': _sign, 'floor': _floor, 'ceil': _ceil, 'fract': _fract,
              'radians': lambda x: x * math.pi / 180.0, 'degrees': lambda x: x * 180.0 / math.pi,
              'trunc': lambda x: float(math.trunc(x)) if math.isfinite(x) else x}
    if name in simple:
        return mapc(simple[name], A[0])
    if name == 'atan':
        return mapc(safe(_atan), *A)
    if name == 'pow':
        return mapc(safe(_pow), A[0], A[1])
    if name == 'mod':
        return mapc(_mod, A[0], A[1])
    if name == 'min':
        return mapc(min, A[0], A[1])
    if name == 'max':
        return mapc(max, A[0], A[1])
    if name == 'clamp':
        return mapc(_clamp, *A)
    if name == 'step':
        return mapc(lambda e, x: 0.0 if x < e else 1.0, A[0], A[1])
    if name == 'smoothstep':
        return mapc(_smooth, *A)
    if name == 'mix':
        if isinstance(A[2], bool) or (isinstance(A[2], Vec) and A[2].kind == 'b'):
            return mapc(lambda x, y, s: y if s else x, *A)
        return mapc(lambda x, y, t: x * (1.0 - t) + y * t, *A)
    if name == 'dot':
        return dot(A[0], A[1])
    if name == 'length':
        return _sqrt(dot(A[0], A[0]))
    if name == 'distance':
        d = binop('-', A[0], A[1])
        return _sqrt(dot(d, d))
    if name == 'normalize':
        l = _sqrt(dot(A[0], A[0]))
        return mapc(lambda x: fdiv(x, l), A[0])
    if name == 'cross':
        a, b = A[0].c, A[1].c
        return Vec([a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]])
    if name == 'reflect':
        d = dot(A[1], A[0])
        return binop('-', A[0], binop('*', 2.0 * d, A[1]))
    if name == 'refract':
        i, n, eta = A
        d = dot(n, i)
        k = 1.0 - eta * eta * (1.0 - d * d)
        if k < 0.0:
            return mapc(lambda x: 0.0, i)
        return binop('-', binop('*', eta, i), binop('*', eta * d + math.sqrt(k), n))
    if name in ('lessThan', 'greaterThan', 'lessThanEqual', 'greaterThanEqual', 'equal', 'notEqual'):
        op = {'lessThan': '<', 'greaterThan': '>', 'lessThanEqual': '<=', 'greaterThanEqual': '>=',
              'equal': '==', 'notEqual': '!='}[name]
        return Vec([binop(op, x, y) for x, y in zip(A[0].c, A[1].c)], 'b')
    if name == 'any':
        return any(A[0].c)
    if name == 'all':
        return all(A[0].c)
    if name == 'not':
        return Vec([not x for x in A[0].c], 'b')
    if name in ('texture', 'texture2D'):
        uv = comps(A[1])
        return Vec(interp.texture(A[0].binding, uv[0], uv[1]))
    if name == 'textureSize':
        return Vec(interp.texture_size(A[0].binding), 'i')
    raise NameError('glsl_ref: no built-in %s' % name)


def _f32(x):
    if not isinstance(x, float) or not math.isfinite(x):
        return x
    if abs(x) > 3.4028234663852886e38:
        return math.copysign(INF, x)
    return struct.unpack('<f', struct.pack('<f', x))[0]


def round32(v):
    if isinstance(v, float):
        return _f32(v)
    if isinstance(v, Vec) and v.kind == 'f':
        return Vec([_f32(x) for x in v.c])
    if isinstance(v, Mat):
        return Mat([[_f32(x) for x in c] for c in v.cols])
    return v


class Return(Exception):
    def __init__(self, value):
        self.value = value


class Break(Exception):
    pass


class Continue(Exception):
    pass


class Interp:
    """One program. run(inputs, push) -> outputs; inputs by name, push as floats (the push constant block)."""

    def __init__(self, src, texture=None, texture_size=None, single=False):
        self.prog = Parser(tokenize(src)).program()
        self.single = single                # each expression's value rounded to 32-bit floats, as a GPU keeps it
        self.texture = texture or (lambda b, u, v: [0.0, 0.0, 0.0, 1.0])
        self.texture_size = texture_size or (lambda b: [256, 256])
        self.steps = 0

    def run(self, inputs, push, frag_coord=None):
        self.scopes = [{}]
        g = self.scopes[0]
        self.types = [{}]
        for name, (ty, loc) in self.prog['inputs'].items():
            g[name] = convert(ty, inputs.get(name, default(ty))) if name in inputs else default(ty)
            self.types[0][name] = ty
        for name, (ty, loc) in self.prog['outputs'].items():
            g[name] = default(ty)
            self.types[0][name] = ty
        for name, binding in self.prog['samplers'].items():
            g[name] = Sampler(binding)
        if self.prog['push']:
            members = {}
            for name, (ty, off) in self.prog['push'].items():
                kind, n = TYPES[ty]
                count = n * n if kind == 'm' else n
                members[name] = make(kind, n, push[off // 4: off // 4 + count]) if kind == 'm' else \
                    make(kind, n, push[off // 4: off // 4 + count])
            g[self.prog['push_instance']] = members
        g['gl_FragCoord'] = Vec(frag_coord or [0.5, 0.5, 0.5, 1.0])
        g['gl_FragDepth'] = 0.0
        g['gl_Position'] = Vec([0.0] * 4)
        self.types[0].update({'gl_FragCoord': 'vec4', 'gl_FragDepth': 'float', 'gl_Position': 'vec4'})
        for stmt in self.prog['globals']:
            self.exec(stmt)
        self.call('main', [])
        return g

    # scopes
    def lookup(self, name):
        for s in reversed(self.scopes):
            if name in s:
                return s
        raise NameError('glsl_ref: no variable %s' % name)

    def type_of(self, name):
        for s in reversed(self.types):
            if name in s:
                return s[name]
        return None

    def call(self, name, args):
        overloads = self.prog['functions'].get(name)
        if not overloads:
            return builtin(name, args, self)
        chosen = None
        for rtype, params, body in overloads:
            if len(params) == len(args) and all(
                    TYPES[p[0]][0] == 's' or (TYPES[p[0]][1] if TYPES[p[0]][0] != 'm' else 100 + TYPES[p[0]][1]) == dim(a)
                    for p, a in zip(params, args)):
                chosen = (rtype, params, body)
                break
        if chosen is None:
            chosen = overloads[0]
        rtype, params, body = chosen
        frame, types = {}, {}
        for (pty, pname, q), a in zip(params, args):
            frame[pname] = convert(pty, a) if TYPES[pty][0] not in ('s',) else a
            types[pname] = pty
        saved = (self.scopes, self.types)
        self.scopes = [self.scopes[0], frame]
        self.types = [self.types[0], types]
        try:
            self.exec(body)
            value = None
        except Return as r:
            value = r.value
        finally:
            self.scopes, self.types = saved
        if rtype != 'void' and value is not None:
            value = convert(rtype, value)
        return value

    def exec(self, s):
        k = s[0]
        self.steps += 1
        if self.steps > 2000000:
            raise RuntimeError('glsl_ref: runaway program')
        if k == 'block':
            self.scopes.append({})
            self.types.append({})
            try:
                for x in s[1]:
                    self.exec(x)
            finally:
                self.scopes.pop()
                self.types.pop()
        elif k == 'decl':
            ty = s[1]
            for name, init in s[2]:
                self.scopes[-1][name] = convert(ty, self.eval(init)) if init is not None else default(ty)
                self.types[-1][name] = ty
        elif k == 'expr':
            self.eval(s[1])
        elif k == 'if':
            if self.eval(s[1]):
                self.exec(s[2])
            elif s[3] is not None:
                self.exec(s[3])
        elif k == 'for':
            self.scopes.append({})
            self.types.append({})
            try:
                self.exec(s[1])
                while s[2] is None or self.eval(s[2]):
                    try:
                        self.exec(s[4])
                    except Break:
                        break
                    except Continue:
                        pass
                    if s[3] is not None:
                        self.eval(s[3])
            finally:
                self.scopes.pop()
                self.types.pop()
        elif k == 'return':
            raise Return(self.eval(s[1]) if s[1] is not None else None)
        elif k == 'break':
            raise Break()
        elif k == 'continue':
            raise Continue()
        elif k == 'discard':
            raise RuntimeError('glsl_ref: a discard (the converted programs have none)')
        else:
            raise RuntimeError('glsl_ref: statement %s' % k)

    # lvalues: (get, set)
    def ref(self, e):
        if e[0] == 'var':
            scope = self.lookup(e[1])
            name = e[1]
            ty = self.type_of(name)

            def setter(v):
                scope[name] = convert(ty, v) if ty and TYPES.get(ty) and TYPES[ty][0] != 's' else v
            return (lambda: scope[name]), setter
        if e[0] == 'member':
            get, put = self.ref(e[1])
            idx = [SWIZ[ch] for ch in e[2]]

            def setter(v):
                base = get()
                c = list(base.c)
                vals = comps(v)
                for i, j in enumerate(idx):
                    c[j] = vals[i] if len(vals) > 1 else vals[0]
                put(Vec(c, base.kind))
            return (lambda: self.swizzle(get(), e[2])), setter
        if e[0] == 'index':
            get, put = self.ref(e[1])
            i = self.eval(e[2])
            i = int(i) if isinstance(i, int) or math.isfinite(i) else 0

            def setter(v):
                base = get()
                if isinstance(base, Mat):
                    cols = [list(c) for c in base.cols]
                    cols[i] = comps(v)
                    put(Mat(cols))
                else:
                    c = list(base.c)
                    c[i] = v
                    put(Vec(c, base.kind))
            return (lambda: self.index(get(), i)), setter
        raise RuntimeError('glsl_ref: not assignable: %r' % (e,))

    def swizzle(self, v, sw):
        if isinstance(v, dict):
            return v[sw]
        c = comps(v)
        picked = [c[SWIZ[ch]] for ch in sw]
        return picked[0] if len(picked) == 1 else Vec(picked, v.kind if isinstance(v, Vec) else 'f')

    def index(self, v, i):
        if not isinstance(i, int):
            i = int(i) if math.isfinite(i) else 0
        if isinstance(v, Mat):
            return Vec(v.cols[i])
        return v.c[i]

    def eval(self, e):
        v = self._eval(e)
        return round32(v) if self.single else v

    def _eval(self, e):
        k = e[0]
        if k == 'lit':
            return e[1]
        if k == 'var':
            return self.lookup(e[1])[e[1]]
        if k == 'bin':
            if e[1] == '&&':
                return bool(self.eval(e[2])) and bool(self.eval(e[3]))
            if e[1] == '||':
                return bool(self.eval(e[2])) or bool(self.eval(e[3]))
            return binop(e[1], self.eval(e[2]), self.eval(e[3]))
        if k == 'un':
            if e[1] in ('++', '--'):
                get, put = self.ref(e[2])
                v = binop('+' if e[1] == '++' else '-', get(), 1)
                put(v)
                return get()
            v = self.eval(e[2])
            if e[1] == '-':
                return mapc(lambda x: -x, v) if not isinstance(v, Mat) else Mat([[-x for x in c] for c in v.cols])
            if e[1] == '!':
                return not v
            return v
        if k == 'post':
            get, put = self.ref(e[2])
            old = get()
            put(binop('+' if e[1] == '++' else '-', old, 1))
            return old
        if k == 'assign':
            get, put = self.ref(e[2])
            v = self.eval(e[3])
            if e[1] != '=':
                v = binop(e[1][0], get(), v)
            put(v)
            return get()
        if k == '?':
            return self.eval(e[2]) if self.eval(e[1]) else self.eval(e[3])
        if k == 'member':
            return self.swizzle(self.eval(e[1]), e[2])
        if k == 'index':
            return self.index(self.eval(e[1]), self.eval(e[2]))
        if k == 'call':
            return self.call(e[1], [self.eval(a) for a in e[2]])
        raise RuntimeError('glsl_ref: expression %s' % k)
