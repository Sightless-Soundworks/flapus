# Generates src/enc_silk_fix.c: fixed-point SILK encoder parts ported from flattened libopus 1.6.1.
# OPUS_FLAT = libopus source tree with conditionals resolved by tools/unifdef.py (float x86 build).
# Usage: OPUS_FLAT=<dir> python tools/gen_silk_enc.py silk_encode_frame_FLP silk_encode_do_VAD_FLP
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FL = os.path.join(os.environ.get('OPUS_FLAT', 'flat32s'), 'silk') + '/'
OUT = os.path.join(ROOT, 'src', 'enc_silk_fix.c')
existing_src = open(os.path.join(ROOT, 'src', 'silk_common.c')).read() + open(os.path.join(ROOT, 'src', 'silk_dec.c')).read()
tables_src = open(os.path.join(ROOT, 'src', 'tables_silk.c')).read()
# names handled elsewhere (hand-written float code or already ported)
skip_funcs = set(re.findall(r'static [^(;=]*?\b(silk_\w+)\s*\(', existing_src))
skip_funcs |= set(sys.argv[1:])
table_names = set(re.findall(r'\boc_(\w+)\s*(?:\[|=)', tables_src))

def read(fn):
    return open(FL + fn, encoding='latin-1').read()

def strip_pp(s):
    out = []
    for l in s.split('\n'):
        if re.match(r'\s*#\s*(include|ifdef|ifndef|endif|else|if|elif|pragma|undef)\b', l): continue
        out.append(l)
    return '\n'.join(out)

def split_funcs(src):
    """Returns list of (name, text) for top-level function definitions and other chunks."""
    src = strip_pp(src)
    res = []
    i = 0
    n = len(src)
    while i < n:
        m = re.compile(r'^([A-Za-z_][^;{}()]*?\b(\w+)\s*\(([^;{}]*?)\)\s*)\{', re.M | re.S).search(src, i)
        if not m:
            res.append((None, src[i:]))
            break
        # balance braces
        j = m.end()
        depth = 1
        while depth:
            c = src[j]
            if c == '{': depth += 1
            elif c == '}': depth -= 1
            j += 1
        res.append((None, src[i:m.start()]))
        res.append((m.group(2), src[m.start():j]))
        i = j
    return res

macro_names_existing = set(re.findall(r'#define\s+(\w+)', existing_src + open(os.path.join(ROOT, 'tools', 'silk_enc_prelude.c')).read()))
def collect_macros(fn):
    s = read(fn)
    lines = s.replace('\\\n', '\x01').split('\n')
    out = []
    for l in lines:
        m = re.match(r'\s*#define\s+(\w+)', l)
        if not m: continue
        name = m.group(1)
        if name in macro_names_existing or name.endswith('_H') or name in ('OPUS_INLINE',): continue
        if name.startswith('silk_') and re.match(r'\s*#define\s+\w+\(', l) is None and 'silk_' in l.split(name, 1)[1]:
            pass
        macro_names_existing.add(name)
        out.append(l.replace('\x01', '\\\n'))
    return out

macros = []
for f in ['define.h', 'tuning_parameters.h', 'pitch_est_defines.h', 'SigProc_FIX.h', 'macros.h', 'main.h', 'resampler_private.h']:
    macros += collect_macros(f)
macros = [m for m in macros if not re.match(r'\s*#define\s+(silk_\w+)\(', m) or re.match(r'\s*#define\s+(silk_\w+)\(', m).group(1) not in skip_funcs]

files = ['init_encoder.c', 'check_control_input.c', 'control_audio_bandwidth.c', 'control_SNR.c', 'control_codec.c',
         'HP_variable_cutoff.c', 'LP_variable_cutoff.c', 'biquad_alt.c', 'sigm_Q15.c', 'ana_filt_bank_1.c', 'VAD.c',
         'inner_prod_aligned.c', 'stereo_find_predictor.c', 'stereo_quant_pred.c', 'stereo_encode_pred.c', 'stereo_LR_to_MS.c',
         'A2NLSF.c', 'NLSF_VQ_weights_laroia.c', 'NLSF_VQ.c', 'NLSF_del_dec_quant.c', 'NLSF_encode.c', 'interpolate.c',
         'process_NLSFs.c', 'VQ_WMat_EC.c', 'quant_LTP_gains.c', 'encode_indices.c', 'encode_pulses.c', 'NSQ.h', 'NSQ.c',
         'NSQ_del_dec.c', 'resampler_down2.c', 'resampler_down2_3.c', 'sort.c', 'enc_API.c']
body = []
protos_auto = []
chunks = []
for f in files:
    for name, text in split_funcs(read(f)):
        if name is None:
            t = text.strip()
            if t: chunks.append(t)
            continue
        if name in skip_funcs: continue
        text = re.sub(r'^(?!static)', 'static ', text, count=1) if not text.lstrip().startswith('static') else text
        body.append(text)
        protos_auto.append(text[:text.index('{')].rstrip() + ';')

structs = []
def grab_typedef(fn, name):
    src = strip_pp(read(fn))
    m = re.search(r'\}\s*' + name + r'\s*;', src)
    end = m.end()
    depth = 0
    k = m.start()
    while True:
        c = src[k]
        if c == '}': depth += 1
        elif c == '{':
            depth -= 1
            if depth == 0: break
        k -= 1
    st = src.rfind('typedef', 0, k)
    return src[st:end]
for fn, nm in [('structs.h', 'silk_nsq_state'), ('structs.h', 'silk_VAD_state'), ('structs.h', 'silk_LP_state'),
               ('structs.h', 'stereo_enc_state'), ('structs.h', 'SideInfoIndices'), ('structs.h', 'silk_encoder_state'),
               ('float/structs_FLP.h', 'silk_shape_state_FLP'), ('float/structs_FLP.h', 'silk_encoder_state_FLP'),
               ('float/structs_FLP.h', 'silk_encoder_control_FLP'), ('float/structs_FLP.h', 'silk_encoder')]:
    structs.append(grab_typedef(fn, nm))

NL = chr(10)
text = open(os.path.join(ROOT, 'tools', 'silk_enc_prelude.c')).read() + NL + NL.join(macros) + NL*2 + (NL*2).join(structs) + NL*2 + open(os.path.join(ROOT, 'tools', 'silk_enc_protos.c')).read() + NL*2 + (NL*2).join(chunks) + NL*2 + NL.join(protos_auto) + NL*2 + (NL*2).join(body) + NL
# Types
for a, b in [('opus_int64', 'fl_i64'), ('opus_int32', 'fl_i32'), ('opus_int16', 'fl_i16'), ('opus_int8', 'fl_i8'),
             ('opus_uint64', 'fl_u64'), ('opus_uint32', 'fl_u32'), ('opus_uint16', 'fl_u16'), ('opus_uint8', 'fl_u8'),
             ('opus_uint', 'unsigned'), ('opus_int', 'int'), ('silk_float', 'float'),
             ('silk_resampler_state_struct', 'silk_resampler_state'), ('silk_NLSF_CB_struct', 'fl_nlsf_cb'),
             ('silk_EncControlStruct', 'fl_silk_ctrl'), ('OPUS_INLINE', 'inline'), ('ec_enc', 'ec_ctx'),
             ('silk_encoder\\b', 'fl_silk_encoder')]:
    text = re.sub(r'\b' + a + r'\b' if not a.endswith('\\b') else r'\b' + a, b, text)
for t in sorted(table_names, key=len, reverse=True):
    text = re.sub(r'\bsilk_' + t + r'\b', 'fl_' + t, text)
# Indentation 4 -> 3 spaces
def reind(l):
    m = re.match(r'^( +)', l)
    if not m: return l
    n = len(m.group(1))
    return ' ' * (n * 3 // 4) + l[n:]
text = '\n'.join(reind(l) for l in text.split('\n'))
hdr = '/* ---- SILK encoder: fixed-point parts (ported from libopus, see LICENSE notes) ---- */\n'
open(OUT, 'w', newline='\n').write(hdr + text)
print('wrote', OUT, len(text.split('\n')), 'lines')
