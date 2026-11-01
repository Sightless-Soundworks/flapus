# Builds the single-file library flapus.h from src/.
# Optional arguments replace the list of Opus encoder parts (for experiments).
import os, re, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(root, 'src')
base = ['common.c', 'tables_celt.c', 'celt_common.c', 'celt_dsp.c', 'celt_dec.c', 'silk_common.c',
        'silk_dec.c', 'opus_dec.c', 'io.c', 'file_dec.c', 'flac_dec.c', 'flac_enc.c']
enc = sys.argv[1:] or ['enc_x87.c', 'enc_celt_dsp.c', 'enc_celt.c', 'enc_bands.c', 'enc_celt_main.c', 'enc_analysis.c',
                       'enc_front.c', 'enc_opus.c', 'enc_silk_fix.c', 'enc_silk_flp.c', 'enc_silk_flp2.c',
                       'enc_silk_flp3.c', 'enc_silk.c']
tail = ['convert.c']
def load(name):
    txt = open(os.path.join(src, name), encoding='utf-8').read()
    # inline local includes
    return re.sub(r'^#include "([^"]+)"\s*$', lambda m: load(m.group(1)), txt, flags=re.M)
out = [load('api.h')]
for p in base:
    out.append('\n/* ======== %s ======== */\n' % p)
    out.append(load(p))
out.append('\n#ifdef FLAPUS_HAVE_ENCODER\n')
for p in enc:
    out.append('\n/* ======== %s ======== */\n' % p)
    out.append(load(p))
out.append('\n#endif /* FLAPUS_HAVE_ENCODER */\n')
for p in tail:
    out.append('\n/* ======== %s ======== */\n' % p)
    out.append(load(p))
out.append('\n#endif /* FLAPUS_IMPLEMENTATION_DONE */\n#endif /* FLAPUS_IMPLEMENTATION */\n')
open(os.path.join(root, 'flapus.h'), 'w', encoding='utf-8', newline='\n').write(''.join(out))
print('wrote flapus.h')
