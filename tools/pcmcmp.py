# pcmcmp.py a.wav b.wav : compares rate, channels and every sample value (any PCM layout).
import struct, sys

def read(fn):
    d = open(fn, 'rb').read()
    assert d[:4] == b'RIFF' and d[8:12] == b'WAVE', fn
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        ck, n = d[i:i+4], struct.unpack('<I', d[i+4:i+8])[0]
        if ck == b'fmt ':
            fmt = d[i+8:i+8+n]
        elif ck == b'data':
            data = d[i+8:i+8+n]
            break
        i += 8 + n + (n & 1)
    tag, ch, rate = struct.unpack('<HHI', fmt[:8])
    bits = struct.unpack('<H', fmt[14:16])[0]
    valid = bits
    if tag == 0xFFFE:
        valid = struct.unpack('<H', fmt[18:20])[0] or bits
    w = bits // 8
    n = len(data) // w
    if w == 1:
        s = [x - 128 for x in data[:n]]
    elif w == 2:
        s = list(struct.unpack('<%dh' % n, data[:2*n]))
    elif w == 4:
        s = list(struct.unpack('<%di' % n, data[:4*n]))
    else:
        s = [int.from_bytes(data[3*k:3*k+3], 'little', signed=True) for k in range(n)]
    sh = bits - valid
    return rate, ch, [v >> sh for v in s]

ra, ca, a = read(sys.argv[1])
rb, cb, b = read(sys.argv[2])
if (ra, ca) != (rb, cb):
    print('DIFF format', (ra, ca), (rb, cb))
elif a != b:
    k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    print('DIFF samples: lengths %d/%d, first difference at %d' % (len(a), len(b), k))
else:
    print('IDENTICAL')
