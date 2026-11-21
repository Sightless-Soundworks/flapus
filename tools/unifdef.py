# Minimal unifdef: resolve #if blocks whose condition depends only on known symbols.
import sys, re, os
DEF = set("""OPUS_BUILD HAVE_LRINTF HAVE_LRINT VAR_ARRAYS OPUS_HAVE_RTCD OPUS_X86_MAY_HAVE_SSE OPUS_X86_MAY_HAVE_SSE2
OPUS_X86_MAY_HAVE_SSE4_1 OPUS_X86_MAY_HAVE_AVX2 CPU_INFO_BY_C ENABLE_HARDENING""".split())
UNDEF = set("""FIXED_POINT ENABLE_QEXT ENABLE_DEEP_PLC ENABLE_DRED ENABLE_OSCE ENABLE_OSCE_BWE ENABLE_OSCE_TRAINING_DATA
CUSTOM_MODES ENABLE_OPUS_CUSTOM_API FUZZING ENABLE_ASSERTIONS OPUS_ARM_ASM OPUS_ARM_MAY_HAVE_NEON_INTR OPUS_ARM_PRESUME_NEON_INTR
OPUS_ARM_MAY_HAVE_NEON OPUS_ARM_MAY_HAVE_MEDIA OPUS_ARM_MAY_HAVE_EDSP OPUS_ARM_PRESUME_AARCH64_NEON_INTR OPUS_ARM_MAY_HAVE_DOTPROD
OPUS_ARM_INLINE_ASM OPUS_ARM_INLINE_EDSP OPUS_ARM_INLINE_MEDIA OPUS_ARM_INLINE_NEON OPUS_ARM_PRESUME_NEON MIPSr1_ASM __mips_dsp __mips
DISABLE_FLOAT_API RESYNTH MEASURE_NORM_MSE ENABLE_RES24 FIXED_DEBUG OPUS_CHECK_ASM HAVE_ARM_NE10 DISABLE_UPDATE_DRAFT
FLOAT_APPROX DUMP_MODES_FROM_STATIC TRAINING DEBUG_ANALYSIS MLP_TRAINING ENABLE_LOSSGEN COMPILE_DEP_TEST
OVERRIDE_CELT_MAXABS16 OVERRIDE_CELT_MAXABS32 SMALL_FOOTPRINT ENABLE_EXPERIMENTAL_AMBISONICS ENABLE_DEBUG_ENC
silk_DEBUG SAVE_ALL_INTERNAL_DATA TIC_TOC DEBUG_SPEECH_ACTIVITY OPUS_FAST_INT64_DISABLED NONTHREADSAFE_PSEUDOSTACK USE_ALLOCA CWRS_EXTRA_ROWS""".split())
VAL = {}
def evaluate(expr):
    e = re.sub(r'/\*.*?\*/', '', expr).split('//')[0].strip()
    unknown = []
    def rd(m):
        s = m.group(1)
        if s in DEF: return ' 1 '
        if s in UNDEF: return ' 0 '
        unknown.append('defined_' + s); return ' defined_%s ' % s
    e = re.sub(r'defined\s*\(\s*(\w+)\s*\)', rd, e)
    e = re.sub(r'defined\s+(\w+)', rd, e)
    # bare identifiers: known values, else unknown macros
    values = []
    def bare(m):
        s = m.group()
        if s.startswith('defined_'): return s
        if s in VAL: return ' %d ' % VAL[s]
        if s in DEF: return ' 1 '
        if s in UNDEF: return ' 0 '
        values.append(s); return s
    # a function-like macro is unknown too
    calls = []
    def call(m):
        calls.append('call_%d' % len(calls)); values.append(calls[-1]); return ' %s ' % calls[-1]
    e = re.sub(r'\b(?!defined_)[A-Za-z_]\w*\s*\([^()]*\)', call, e)
    e = re.sub(r'\b(?!call_)[A-Za-z_]\w*\b', bare, e)
    e = e.replace('&&', ' and ').replace('||', ' or ')
    e = re.sub(r'!(?!=)', ' not ', e)
    names = sorted(set(unknown))
    if len(names) > 6: return None
    # known when the result does not depend on whether the unknown macros are defined,
    # and needs no unknown value (the && and || that decide without one are fine)
    res = set()
    for bits in range(1 << len(names)):
        env = {n: (bits >> i) & 1 for i, n in enumerate(names)}
        env.update({n: Unknown() for n in values})
        try: res.add(bool(eval(e, {}, env)))
        except Exception: return None
        if len(res) > 1: return None
    return res.pop()

class Unknown:
    def _fail(self, *a): raise ValueError('unknown macro value')
    __bool__ = __int__ = __index__ = __neg__ = __pos__ = __invert__ = _fail
    __eq__ = __ne__ = __lt__ = __le__ = __gt__ = __ge__ = _fail
    __add__ = __radd__ = __sub__ = __rsub__ = __mul__ = __rmul__ = __and__ = __rand__ = _fail
    __or__ = __ror__ = __lshift__ = __rlshift__ = __rshift__ = __rrshift__ = _fail

def simplify(expr):
    # known macros in a condition that stays become their values
    keep = []
    def d(m):
        n = m.group(1) or m.group(2)
        if n in DEF: return ' 1 '
        if n in UNDEF: return ' 0 '
        keep.append(m.group(0)); return '\x00%d\x00' % (len(keep)-1)
    e = re.sub(r'defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)', d, expr)
    def b(m):
        n = m.group(0)
        if n in VAL: return str(VAL[n])
        if n in DEF: return '1'
        if n in UNDEF: return '0'
        return n
    e = re.sub(r'\b[A-Za-z_]\w*\b(?!\s*\()', b, e)
    e = re.sub(r'\x00(\d+)\x00', lambda m: keep[int(m.group(1))], e)
    return re.sub(r'\s+', ' ', e).strip()

def kept(ln):
    m = re.match(r'(\s*#\s*)(if|elif)\b(.*)', ln)
    if not m: return ln
    return m.group(1)+m.group(2)+' '+simplify(re.sub(r'/\*.*?\*/', '', m.group(3)).split('//')[0])

def process(lines):
    out = []; stack = []  # entries: [mode, taken] mode: 'keep' (unknown cond, emit directive), 'eval'
    def active(): return all(s['on'] for s in stack)
    for ln in lines:
        m = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', ln)
        if not m:
            if active(): out.append(ln)
            continue
        d, rest = m.group(1), m.group(2).strip()
        if d in ('if', 'ifdef', 'ifndef'):
            if d == 'ifdef': v = evaluate('defined(%s)' % rest.split()[0])
            elif d == 'ifndef':
                v = evaluate('defined(%s)' % rest.split()[0]); v = None if v is None else not v
            else: v = evaluate(rest)
            if v is None:
                stack.append({'known': False, 'on': True, 'taken': False})
                if active(): out.append(kept(ln))
            else:
                stack.append({'known': True, 'on': v, 'taken': v})
        elif d == 'elif':
            s = stack[-1]
            if not s['known']:
                if all(x['on'] for x in stack[:-1]): out.append(kept(ln))
                continue
            v = evaluate(rest)
            if v is None:  # degrade: emit as #if
                s['known'] = False; s['on'] = not s['taken']
                if all(x['on'] for x in stack[:-1]) and s['on']: out.append(kept(ln.replace('elif', 'if', 1)))
                continue
            s['on'] = (not s['taken']) and v; s['taken'] = s['taken'] or v
        elif d == 'else':
            s = stack[-1]
            if not s['known']:
                if all(x['on'] for x in stack[:-1]): out.append(ln)
                continue
            s['on'] = not s['taken']; s['taken'] = True
        else:
            s = stack.pop()
            if not s['known'] and active(): out.append(ln)
    return out
if __name__ == '__main__':
    # the decoder's x86-64 view, for tools/gen_tables.py
    DEF |= {'OPUS_X86_PRESUME_SSE', 'OPUS_X86_PRESUME_SSE2', '__SSE__', '__SSE2__'}
    src, dst = sys.argv[1], sys.argv[2]
    for root, _, files in os.walk(src):
        if any(p in root for p in ('dnn', 'tests', '.git', 'training', 'arm', 'mips', 'doc', 'cmake', 'meson', 'fixed')): continue
        for f in files:
            if not f.endswith(('.c', '.h')): continue
            p = os.path.join(root, f); rel = os.path.relpath(p, src)
            lines = open(p, encoding='latin-1').read().split('\n')
            try: res = process(lines)
            except Exception as ex: print("skip", rel, ex); continue
            txt = '\n'.join(res)
            txt = re.sub(r'\n{3,}', '\n\n', txt)
            os.makedirs(os.path.join(dst, os.path.dirname(rel)), exist_ok=True)
            open(os.path.join(dst, rel), 'w', encoding='latin-1').write(txt)
