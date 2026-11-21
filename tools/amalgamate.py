# Builds the single-file library flapus.h from src/.
import os, re, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(root, 'src')
base = ['common.c', 'math.c', 'tables_celt.c', 'celt_common.c', 'celt_dsp.c', 'celt_dec.c', 'silk_common.c',
        'silk_dec.c', 'opus_dec.c', 'io.c', 'file_dec.c', 'flac_dec.c', 'flac_enc.c']
enc = ['enc_libopus.c', 'enc_front.c']
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
# the license texts go at the end, so flapus.h is complete on its own
lic = open(os.path.join(root, 'LICENSE'), encoding='utf-8').read()
if '/*' in lic or '*/' in lic: sys.exit('LICENSE must not contain /* or */')
out.append('\n/*\n' + lic.rstrip('\n') + '\n*/\n')
open(os.path.join(root, 'flapus.h'), 'w', encoding='utf-8', newline='\n').write(''.join(out))
print('wrote flapus.h')
