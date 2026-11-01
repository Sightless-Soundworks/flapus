# Extracts constant tables from a libopus source tree into src/tables_*.c.
# Usage: python gen_tables.py <flattened libopus dir> <out dir>
import re, sys, os
SRC, OUT = sys.argv[1], sys.argv[2]
TYPES = {'opus_int8':'fl_i8','opus_uint8':'fl_u8','opus_int16':'fl_i16','opus_uint16':'fl_u16',
         'opus_int32':'fl_i32','opus_uint32':'fl_u32','unsigned char':'fl_u8','signed char':'fl_i8',
         'celt_coef':'float','opus_val16':'float','kiss_twiddle_cpx':'fl_cpx','silk_NLSF_CB_struct':'fl_nlsf_cb'}
def read(f): return open(os.path.join(SRC, f), encoding='latin-1').read()
def grab(f, name):
    t = read(f)
    m = re.search(r'(?:static\s+)?const\s+(?!silk_DWORD_ALIGN)([\w ]+?)\s*(\*\s*const\s+)?\b' + re.escape(name) + r'\b\s*((?:\[[^\]]*\]\s*)*)=\s*', t)
    if not m: raise SystemExit('missing ' + name + ' in ' + f)
    i = m.end(); depth = 0; j = i
    while True:
        c = t[j]
        if c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    body = t[i:j+1]
    typ = m.group(1).strip(); ptr = m.group(2)
    typ = TYPES.get(typ, typ)
    dims = m.group(3)
    return typ, ptr, dims, body
def emit(out, f, name, newname=None, dims=None):
    typ, ptr, d, body = grab(f, name)
    newname = newname or 'fl_' + re.sub(r'^silk_', '', name)
    d = dims if dims is not None else d
    d = re.sub(r'\s+', '', d)
    body = re.sub(r'\bsilk_', 'fl_', body)
    for k, v in TYPES.items(): body = re.sub(r'\b' + k + r'\b', v, body)
    if ptr: out.append('static const %s *const %s%s = %s;\n' % (typ, newname, d, body))
    else: out.append('static const %s %s%s = %s;\n' % (typ, newname, d, body))
celt = []
emit(celt, 'celt/modes.c', 'eband5ms', dims='[22]')
emit(celt, 'celt/modes.c', 'band_allocation', dims='[231]')
for n in ['window120', 'logN400', 'cache_index50', 'cache_bits50', 'cache_caps50', 'fft_twiddles48000_960',
          'fft_bitrev480', 'fft_bitrev240', 'fft_bitrev120', 'fft_bitrev60', 'mdct_twiddles960']:
    emit(celt, 'celt/static_modes_float.h', n)
emit(celt, 'celt/quant_bands.c', 'eMeans', 'fl_eMeans')
emit(celt, 'celt/quant_bands.c', 'e_prob_model')
emit(celt, 'celt/rate.c', 'LOG2_FRAC_TABLE', 'fl_log2_frac_table')
t = read('celt/cwrs.c')
m = re.search(r'CELT_PVQ_U_DATA\[1272\]=\{(?:\s*#endif)?(.*?)\};', t, re.S)
celt.append('static const fl_u32 fl_pvq_u_data[1272] = {\n' + m.group(1) + '};\n')
open(os.path.join(OUT, 'tables_celt.c'), 'w').write('/* CELT constant tables (from the Opus reference). */\n' + '\n'.join(celt))
silk = []
files = ['silk/tables_LTP.c', 'silk/tables_NLSF_CB_NB_MB.c', 'silk/tables_NLSF_CB_WB.c', 'silk/tables_gain.c',
         'silk/tables_other.c', 'silk/tables_pitch_lag.c', 'silk/tables_pulses_per_block.c', 'silk/pitch_est_tables.c',
         'silk/table_LSF_cos.c', 'silk/resampler_rom.c']
for f in files:
    t = read(f)
    for m in re.finditer(r'^(?:silk_DWORD_ALIGN\s+)?(?:static\s+)?const\s+[\w ]+?\s*(?:\*\s*const\s+)?\b(\w+)\s*(?:\[[^\]]*\]\s*)*=', t, re.M):
        emit(silk, f, m.group(1))
txt = '\n'.join(silk)
open(os.path.join(OUT, 'tables_silk.c'), 'w').write('/* SILK constant tables (from the Opus reference). */\n' + txt)
