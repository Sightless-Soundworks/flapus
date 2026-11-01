# Compares the PCM data of two 16-bit WAV files.
import sys, struct
def pcm(fn):
    d = open(fn, 'rb').read()
    i = 12
    while i < len(d):
        cid, sz = d[i:i+4], struct.unpack('<I', d[i+4:i+8])[0]
        if cid == b'fmt ': ch = struct.unpack('<H', d[i+10:i+12])[0]
        if cid == b'data': return ch, d[i+8:i+8+sz]
        i += 8 + sz + (sz & 1)
a_ch, a = pcm(sys.argv[1]); b_ch, b = pcm(sys.argv[2])
na, nb = len(a)//2, len(b)//2
x = struct.unpack('<%dh' % na, a); y = struct.unpack('<%dh' % nb, b)
n = min(na, nb); diffs = [i for i in range(n) if x[i] != y[i]]
print('samples', na, nb, 'differing', len(diffs), end=' ')
if diffs:
    m = max(abs(x[i]-y[i]) for i in diffs)
    print('first frame', diffs[0]//a_ch, 'maxdiff', m, 'first vals', [(x[i], y[i]) for i in diffs[:6]])
else: print('IDENTICAL' if na == nb else '')
