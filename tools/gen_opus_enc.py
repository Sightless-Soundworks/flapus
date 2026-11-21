# Generates src/enc_libopus.c: the libopus float encoder as one block of plain C.
#
#   python tools/gen_opus_enc.py <libopus 1.6.1 source dir>
#
# The sources are taken as they are: conditionals resolved for a plain C float build (no
# intrinsics, no run-time CPU detection, alloca for temporary arrays), comments removed,
# headers placed in include order, every file-scope name prefixed with oe_ and made
# static, the math functions replaced with the portable ones of src/math.c, and whatever
# the multistream encoder does not reach left out. The result computes exactly what
# libopus built the same way computes, on every platform. Needs tools/unifdef.py.
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unifdef

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'enc_libopus.c')
PREFIX = 'oe_'
DIRS = ['celt', 'silk', 'silk/float', 'src', 'include']
# entry points used by src/enc_front.c
ROOTS = ['opus_multistream_surround_encoder_get_size', 'opus_multistream_surround_encoder_init',
         'opus_multistream_encoder_ctl', 'opus_multistream_encode_float']
MATH = {'log': 'fl_log', 'log10': 'fl_log10', 'exp': 'fl_exp', 'pow': 'fl_pow', 'sin': 'fl_sin',
        'cos': 'fl_cos', 'acos': 'fl_acos'}
# names from C and the C library that stay as they are
KEEP = set('''auto break case char const continue default do double else enum extern float for goto if inline int long
register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while
_Bool __inline __inline__ __restrict __attribute__ __builtin_expect __builtin_clz __builtin_alloca __GNUC__
__GNUC_MINOR__ __clang__ _MSC_VER _WIN32 __STDC_VERSION__ __SSE__ __SSE2__ __x86_64__ __i386__ _M_X64 _M_IX86
__cplusplus defined size_t NULL memcpy memmove memset malloc calloc realloc free abs labs sqrt sqrtf floor floorf
fabs fabsf lrint lrintf ceil va_list va_start va_arg va_end alloca _alloca INT_MAX INT_MIN SHRT_MAX SHRT_MIN
UINT_MAX'''.split())


SRC = None


def resolve_conditionals():
    unifdef.DEF = set('OPUS_BUILD USE_ALLOCA HAVE_LRINT HAVE_LRINTF'.split())
    unifdef.UNDEF -= unifdef.DEF
    unifdef.UNDEF |= set('''__cplusplus VAR_ARRAYS NONTHREADSAFE_PSEUDOSTACK HAVE_CONFIG_H OPUS_HAVE_RTCD CPU_INFO_BY_C
ENABLE_HARDENING OPUS_X86_MAY_HAVE_SSE OPUS_X86_MAY_HAVE_SSE2 OPUS_X86_MAY_HAVE_SSE4_1 OPUS_X86_MAY_HAVE_AVX2
OPUS_X86_PRESUME_SSE OPUS_X86_PRESUME_SSE2 OPUS_X86_PRESUME_SSE4_1 OPUS_X86_PRESUME_AVX2 OPUS_X86_MAY_HAVE_SSE4_1
ENABLE_ASSERTIONS ENABLE_UPDATE_DRAFT ENABLE_DEEP_PLC ENABLE_DRED ENABLE_OSCE ENABLE_QEXT OPUS_CHECK_ASM
NON_STATIC_COMB_FILTER_CONST_C SKIP_CONFIG_H USE_SMALL_DIV_TABLE CUSTOM_MODES_ONLY RADIX_TWO_ONLY USE_WEIGHTS_FILE
silk_short_prediction_create_arch_coef OPUS_WILL_BE_SLOW FIXED_POINT_DEBUG'''.split())
    # a name tested but never defined in the sources is a build option, and plain C sets none;
    # names with a leading underscore come from the compiler and stay
    tested, defined = set(), set()
    for d in DIRS:
        for root, sub, fs in os.walk(os.path.join(SRC, d)):
            sub[:] = [x for x in sub if x not in ('arm', 'x86', 'mips', 'xtensa', 'fixed', 'tests')]
            for f in fs:
                if f.endswith(('.c', '.h')):
                    t = strip_comments(open(os.path.join(root, f), encoding='latin-1').read().replace('\\\n', ''))
                    for m in re.finditer(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)', t, re.M):
                        tested |= set(re.findall(r'(?<![\w.])[A-Za-z_]\w*', m.group(1)))
                    defined |= set(re.findall(r'^\s*#\s*define\s+(\w+)', t, re.M))
    # limits.h and float.h macros come from the C library
    libc = re.compile(r'(U?(INT|LONG|LLONG|SHRT)|S?CHAR|UCHAR)_(MAX|MIN)$|CHAR_BIT$|SIZE_MAX$|(FLT|DBL|LDBL)_')
    unifdef.UNDEF |= {n for n in tested-defined-unifdef.DEF if not n.startswith('_') and n != 'defined' and not libc.match(n)}
    # defined to 0 in debug.h; 64-bit products give the same results either way
    unifdef.UNDEF |= {'OVERRIDE_FFT', 'HAVE_ALLOCA_H'}  # set only with HAVE_ARM_NE10
    unifdef.DEF |= {'CELT_ENCODER_C', 'CELT_DECODER_C'}  # all prototypes; unused ones are dropped
    unifdef.DEF.add('OPUS_WILL_BE_SLOW')  # no message in unoptimized builds
    unifdef.VAL.update(SILK_DEBUG=0, SILK_TIC_TOC=0, OPUS_FAST_INT64=1)
    # M_PI comes from math.h on glibc (which the reference uses); here it is fixed
    unifdef.DEF.add('M_PI')


COMMENT = re.compile(r'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|/\*.*?\*/|//[^\n]*', re.S)


def strip_comments(text):
    # comments become one space, or their newlines, so that line structure stays
    return COMMENT.sub(lambda m: m.group(1) or (' ' if '\n' not in m.group(0) else '\n'*m.group(0).count('\n')), text)


def read(src, rel):
    text = strip_comments(open(os.path.join(src, rel), encoding='latin-1').read().replace('\\\n', ''))
    return '\n'.join(unifdef.process(text.split('\n')))


# ---- tokens ----
TOK = re.compile(r'''(?P<ws>[ \t]+)|(?P<nl>\n)|(?P<com>/\*.*?\*/|//[^\n]*)|(?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
|(?P<num>\.?\d(?:[\w.]|[eEpP][+-])*)|(?P<id>[A-Za-z_]\w*)|(?P<pp>\#)|(?P<op>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!=<>]=|.)''', re.S | re.X)


def tokenize(text):
    """List of (kind, text); preprocessor lines become one ('pp', line) token."""
    text = text.replace('\\\n', ' ')
    out, line_start = [], True
    for m in TOK.finditer(text):
        k, v = m.lastgroup, m.group()
        if k == 'com':
            if '\n' in v and not line_start:
                out.append(('nl', '\n'))
            continue
        if k == 'pp' and line_start:
            out.append(('pp_start', v))
            line_start = False
            continue
        out.append((k, v))
        if k == 'nl':
            line_start = True
        elif k != 'ws':
            line_start = False
    # join preprocessor lines
    res, i = [], 0
    while i < len(out):
        if out[i][0] == 'pp_start':
            j = i
            while j < len(out) and out[j][0] != 'nl':
                j += 1
            res.append(('pp', out[i:j]))
            i = j
        else:
            res.append(out[i])
            i += 1
    return res


def pp_text(toks):
    return ''.join(v for _, v in toks).strip()


# ---- file-scope statements ----
def statements(toks):
    """Splits non-preprocessor tokens into file-scope statements: (tokens, is_function)."""
    res, cur, depth, i = [], [], 0, 0
    while i < len(toks):
        k, v = toks[i]
        if k == 'pp':
            if cur and not any(t[0] not in ('ws', 'nl') for t in cur):
                res.append((cur, None))
                cur = []
            if cur:
                cur.append((k, v))
            else:
                res.append(([(k, v)], 'pp'))
            i += 1
            continue
        cur.append((k, v))
        if v == '{':
            depth += 1
        elif v == '}':
            depth -= 1
            if depth == 0:
                sig = [t for t in cur if t[0] not in ('ws', 'nl', 'pp')]
                # a function body ends a function definition; else a struct/initializer goes on to ';'
                j = len(sig)-1
                d = 0
                while j >= 0:
                    if sig[j][1] == '}': d += 1
                    elif sig[j][1] == '{':
                        d -= 1
                        if d == 0: break
                    j -= 1
                if j > 0 and sig[j-1][1] == ')' and '=' not in [t[1] for t in sig[:j]] and sig[0][1] not in ('typedef', 'struct', 'union', 'enum'):
                    res.append((cur, 'func'))
                    cur = []
        elif v == ';' and depth == 0:
            res.append((cur, 'decl'))
            cur = []
        i += 1
    if cur:
        res.append((cur, None))
    return res


def sig_tokens(st):
    return [v for k, v in st if k not in ('ws', 'nl', 'pp')]


def declared_names(st, kind):
    """Names a file-scope statement declares (functions, objects, typedefs, tags, enum constants)."""
    s = sig_tokens(st)
    names = []
    if not s:
        return names
    # tags and enum constants
    for i, v in enumerate(s):
        if v in ('struct', 'union', 'enum') and i+1 < len(s) and re.match(r'[A-Za-z_]', s[i+1]):
            if i+2 < len(s) and s[i+2] == '{':
                names.append(s[i+1])
        if v == 'enum':
            j = i+1
            while j < len(s) and s[j] != '{': j += 1
            if j < len(s):
                d, j = 1, j+1
                expect = True
                while j < len(s) and d:
                    if s[j] == '{': d += 1
                    elif s[j] == '}': d -= 1
                    elif d == 1 and expect and re.match(r'[A-Za-z_]', s[j]):
                        names.append(s[j])
                        expect = False
                    elif d == 1 and s[j] == ',':
                        expect = True
                    j += 1
    if kind == 'func':
        body = s.index('{') if '{' in s else len(s)
        head = s[:body]
        p = head.index('(')
        names.append(head[p-1])
        return names
    # declarators at brace depth 0
    d = pd = 0
    i = 0
    while i < len(s):
        v = s[i]
        if v == '{': d += 1
        elif v == '}': d -= 1
        elif d == 0:
            if v == '(':
                # function pointer declarator: ( * name )
                if i+2 < len(s) and s[i+1] == '*' and re.match(r'[A-Za-z_]', s[i+2]):
                    names.append(s[i+2])
                pd += 1
            elif v == ')':
                pd -= 1
            elif pd == 0 and re.match(r'[A-Za-z_]\w*$', v) and i+1 < len(s) and s[i+1] in ('(', '[', '=', ',', ';') \
                    and v not in KEEP and v not in ('struct', 'union', 'enum'):
                names.append(v)
                if s[i+1] == '=':
                    # skip the initializer
                    j, dd = i+2, 0
                    while j < len(s):
                        if s[j] in '{(': dd += 1
                        elif s[j] in '})': dd -= 1
                        elif dd == 0 and s[j] in (',', ';'): break
                        j += 1
                    i = j-1
        i += 1
    return names


def main():
    global SRC
    src = SRC = sys.argv[1]
    resolve_conditionals()
    t = ''.join(open(os.path.join(src, f)).read()+'\n' for f in ('celt_sources.mk', 'silk_sources.mk', 'opus_sources.mk'))
    groups = dict(re.findall(r'^(\w+)\s*=\s*(.*)$', t.replace('\\\n', ' '), re.M))
    cfiles = []
    for g in ('CELT_SOURCES', 'SILK_SOURCES', 'SILK_SOURCES_FLOAT', 'OPUS_SOURCES', 'OPUS_SOURCES_FLOAT'):
        cfiles += groups[g].split()
    # headers in include order
    texts, order, seen = {}, [], set()

    def find(name, cur):
        for d in [os.path.dirname(cur)]+DIRS:
            p = os.path.normpath(os.path.join(d, name))
            if os.path.exists(os.path.join(src, p)):
                return p
        return None

    def visit(rel):
        if rel in seen:
            return
        seen.add(rel)
        texts[rel] = read(src, rel)
        for m in re.finditer(r'^\s*#\s*include\s*"([^"]+)"', texts[rel], re.M):
            h = find(m.group(1), rel)
            if h and h.endswith('.h'):
                visit(h)
        if rel.endswith('.h'):
            order.append(rel)
    for c in cfiles:
        visit(c)
    files = order+cfiles
    toks = {f: tokenize(texts[f]) for f in files}
    # local includes go: the amalgamation holds them (static_modes_float.h is included by modes.c in place)
    for f in files:
        out = []
        for k, v in toks[f]:
            if k == 'pp':
                line = pp_text(v)
                m = re.match(r'#\s*include\s*"([^"]+)"', line)
                if m:
                    h = find(m.group(1), f)
                    if h and h.endswith('.h') and h in order:
                        out.append(('pp', [('inc', h)]))
                        continue
                    if h and not h.endswith('.h'):
                        raise SystemExit('include of a non-header: ' + h)
            out.append((k, v))
        toks[f] = out
    # names to prefix: macros and file-scope declarations
    names, file_statics = set(), {}
    stmts = {}
    for f in files:
        stmts[f] = statements(toks[f])
        for st, kind in stmts[f]:
            if kind == 'pp':
                m = re.match(r'#\s*define\s+([A-Za-z_]\w*)', pp_text(st[0][1]))
                if m:
                    names.add(m.group(1))
            elif kind in ('decl', 'func'):
                dn = declared_names(st, kind)
                names.update(dn)
                if f.endswith('.c') and 'static' in sig_tokens(st)[:3]:
                    for n in dn:
                        file_statics.setdefault(n, set()).add(f)
    names -= KEEP
    names -= set(MATH)
    names.discard('M_PI')
    # macros defined in a .c file and anywhere else are renamed per file
    defs = {}
    for f in files:
        for st, kind in stmts[f]:
            if kind == 'pp':
                m = re.match(r'#\s*define\s+([A-Za-z_]\w*)', pp_text(st[0][1]))
                if m:
                    defs.setdefault(m.group(1), set()).add(f)
    local = {f: set() for f in files}
    local_name = {}
    for n, fs in defs.items():
        if len(fs) > 1:
            for f in fs:
                if f.endswith('.c'):
                    local[f].add(n)
    # a macro two headers define differently gets one name per header; a file uses the one it sees
    bodies = {}
    for f in files:
        if f.endswith('.h'):
            for st, kind in stmts[f]:
                if kind == 'pp':
                    m = re.match(r'#\s*define\s+([A-Za-z_]\w*)(.*)', pp_text(st[0][1]))
                    if m:
                        bodies.setdefault(m.group(1), {}).setdefault(re.sub(r'\s+', '', m.group(2)), set()).add(f)
    incl = {}
    def closure(f):
        if f not in incl:
            incl[f] = {f}
            for st, kind in stmts[f]:
                if kind == 'pp' and st[0][1] and st[0][1][0][0] == 'inc':
                    incl[f] |= closure(st[0][1][0][1])
        return incl[f]
    for n, b in bodies.items():
        hs = set().union(*b.values())
        if len(b) < 2 or len(hs) < 2:
            continue
        for f in files:
            seen = closure(f) & hs
            if len(seen) > 1:
                raise SystemExit('%s sees %s from %s' % (f, n, sorted(seen)))
            if seen:
                h = seen.pop()
                local[f].add(n)
                local_name[(f, n)] = PREFIX+n+'_'+os.path.basename(h)[:-2]
    if any(len(fs) > 1 for fs in file_statics.values()):
        raise SystemExit('static names in several files: %s' % [n for n, fs in file_statics.items() if len(fs) > 1])

    def rename(f, toks):
        out = []
        for k, v in toks:
            if k == 'pp' and isinstance(v, list):
                v = rename(f, v)
            elif k == 'id':
                if v == 'M_PI':
                    v = '3.14159265358979323846'
                elif v in local[f]:
                    v = local_name.get((f, v), PREFIX+v+'_'+os.path.basename(f)[:-2])
                elif v in names:
                    v = PREFIX+v
            out.append((k, v))
        return out

    # reachability: a name reaches what its definition (or macro body) mentions
    uses, defined_by = {}, {}
    for f in files:
        for idx, (st, kind) in enumerate(stmts[f]):
            if kind == 'pp':
                m = re.match(r'#\s*define\s+([A-Za-z_]\w*)(.*)', pp_text(st[0][1]))
                if m:
                    uses.setdefault(m.group(1), set()).update(re.findall(r'[A-Za-z_]\w*', m.group(2)))
            elif kind in ('decl', 'func') and not is_function_decl(st) and 'extern' not in sig_tokens(st):
                ids = set(v for k, v in st if k == 'id')
                for n in declared_names(st, kind):
                    defined_by.setdefault(n, set()).add((f, idx))
                    uses.setdefault(n, set()).update(ids)
    reach, todo = set(), list(ROOTS)
    while todo:
        n = todo.pop()
        if n not in reach:
            reach.add(n)
            todo += list(uses.get(n, ()))
    keep = set()
    for n in reach:
        keep |= defined_by.get(n, set())

    def emit(st):
        res = ''
        for k, v in st:
            res += pp_text(v)+'\n' if k == 'pp' and isinstance(v, list) else v
        return res

    # the .c files in order, each header in place where it is first included, like the
    # preprocessor would; extern data declarations go, so every definition can be static,
    # and a file that defines data another uses comes first
    cs = [f for f in files if f.endswith('.c')]
    data = {}
    for f in files:
        for idx, (st, kind) in enumerate(stmts[f]):
            if kind == 'decl' and not is_type_decl(st) and not is_function_decl(st) and 'extern' not in sig_tokens(st):
                for n in declared_names(st, kind):
                    data[n] = f
    def dep_files(f, seen):
        res = set()
        for idx, (st, kind) in enumerate(stmts[f]):
            if kind == 'pp' and st[0][1] and st[0][1][0][0] == 'inc':
                h = st[0][1][0][1]
                if h not in seen:
                    seen.add(h)
                    res |= dep_files(h, seen)
            elif kind == 'func' or (kind == 'decl' and (f, idx) in keep):
                for n in set(v for k, v in st if k == 'id'):
                    if n in data and n in reach:
                        res.add(data[n])
        return res
    order_c, placed = [], set()
    def place(f, path):
        if f in placed:
            return
        if f in path:
            raise SystemExit('data dependency cycle: %s' % (path+[f]))
        for g in sorted((g for g in dep_files(f, set()) if g.endswith('.c')), key=cs.index):
            if g != f:
                place(g, path+[f])
        placed.add(f)
        order_c.append(f)
    for f in cs:
        place(f, [])
    out, done = [], set()
    def expand(f):
        if f.endswith('.c'):
            out.append('\n/* %s */\n' % f)
        for idx, (st0, kind) in enumerate(stmts[f]):
            if kind == 'pp' and st0[0][1] and st0[0][1][0][0] == 'inc':
                h = st0[0][1][0][1]
                if h not in done:
                    done.add(h)
                    expand(h)
                continue
            st = rename(f, st0)
            if kind in ('pp', None):
                out.append(emit(st))
            elif kind == 'decl' and is_type_decl(st):
                out.append(emit(st))
            elif kind == 'decl' and is_function_decl(st):
                if any(n in reach and n in defined_by for n in declared_names(st0, kind)):
                    out.append('\n'+emit(make_static(st)))
            elif kind == 'decl' and 'extern' in sig_tokens(st):
                continue
            elif (f, idx) in keep:
                out.append('\n'+emit(make_static(st)))
    for f in order_c:
        expand(f)
    text = ''.join(out)
    text = re.sub(r'\b(log|log10|exp|pow|sin|cos|acos)(\s*\()', lambda m: MATH[m.group(1)]+m.group(2), text)
    text = re.sub(r'[ \t]+\n', '\n', text)
    text = re.sub(r'\n{3,}', '\n\n', text)
    text = re.sub(r'(?<![\w.])alloca\(', 'oe_alloca(', text)
    text = drop_unused_macros(text)
    hdr = ('/* Opus encoder: libopus 1.6.1, float, generated by tools/gen_opus_enc.py. Do not edit. */\n'
           '#if defined(__GNUC__)\n#define oe_alloca __builtin_alloca\n#elif !defined(_WIN32)\n'
           '#include <alloca.h>\n#define oe_alloca alloca\n#endif\n'
           '/* GCC finds variables in libopus that look uninitialized but are not */\n'
           '#if defined(__GNUC__) && !defined(__clang__)\n#pragma GCC diagnostic push\n'
           '#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"\n#endif\n')
    text += '#if defined(__GNUC__) && !defined(__clang__)\n#pragma GCC diagnostic pop\n#endif\n'
    open(OUT, 'w', newline='\n').write(hdr+text)
    print('wrote', OUT, text.count('\n'), 'lines,', len(keep), 'definitions')


def drop_unused_macros(text):
    """Removes the #define and #undef lines of macros that nothing mentions: neither this
    file nor the other sources in src/ (enc_front.c uses the ctl macros). Repeated, as a
    removed macro may have been the only user of another."""
    other = set()
    for f in os.listdir(os.path.join(ROOT, 'src')):
        if f.endswith(('.c', '.h')) and f != os.path.basename(OUT):
            other |= set(re.findall(r'[A-Za-z_]\w*', strip_comments(open(os.path.join(ROOT, 'src', f), encoding='utf-8').read())))
    pp = re.compile(r'\s*#\s*(?:define|undef)\s+([A-Za-z_]\w*)')
    while True:
        lines = text.split('\n')
        count, defined = {}, set()
        for ln in lines:
            m = pp.match(ln)
            ids = re.findall(r'[A-Za-z_]\w*', ln[m.end():] if m else ln)
            if m and ln.lstrip()[1:].lstrip().startswith('define'):
                defined.add(m.group(1))
            for n in ids:
                count[n] = count.get(n, 0)+1
        dead = {n for n in defined if not count.get(n) and n not in other}
        if not dead:
            return text
        text = '\n'.join(ln for ln in lines if not (pp.match(ln) and pp.match(ln).group(1) in dead))


def is_type_decl(st):
    s = sig_tokens(st)
    return bool(s) and (s[0] == 'typedef' or (s[0] in ('struct', 'union', 'enum') and '(' not in s and '=' not in s
                                             and (s[1] == '{' or s[2] == '{' or len(s) == 3)))


def is_function_decl(st):
    s = sig_tokens(st)
    if '=' in s or '(' not in s or '{' in s:
        return False
    p = s.index('(')
    return p > 0 and re.match(r'[A-Za-z_]', s[p-1]) is not None and s[p+1] != '*'


def make_static(st):
    s = list(st)
    while s and s[0][0] in ('ws', 'nl'):
        s.pop(0)
    s = [t for t in s if not (t[0] == 'id' and t[1] in ('extern', 'oe_OPUS_EXPORT', 'oe_OPUS_CUSTOM_EXPORT', 'oe_OPUS_CUSTOM_EXPORT_STATIC', 'oe_OPUS_CUSTOM_NOSTATIC'))]
    if 'static' not in sig_tokens(s)[:4]:
        s = [('id', 'static'), ('ws', ' ')]+s
    return s


if __name__ == '__main__':
    main()
