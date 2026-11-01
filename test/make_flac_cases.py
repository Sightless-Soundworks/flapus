# Writes edge-case WAV files for the FLAC tests into the given folder (default flac_cases/).
import math, os, random, struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flac_cases')
os.makedirs(out, exist_ok=True)
rnd = random.Random(1)

def wav(name, rate, ch, bits, frames, gen, valid=None, ext=False):
    """gen(i, c) returns a sample in [-2^(b-1), 2^(b-1)) for b = valid or bits."""
    b = valid or bits
    data = bytearray()
    for i in range(frames):
        for c in range(ch):
            v = gen(i, c) << (bits - b)
            if bits == 8:
                data.append((v + 128) & 255)
            else:
                data += struct.pack('<i', v)[:bits // 8]
    bpf = bits // 8 * ch
    if ext or valid or ch > 2:
        mask = [4, 3, 7, 0x33, 0x607, 0x60F, 0x70F, 0x63F][ch - 1] if ch <= 8 else 0
        fmt = struct.pack('<HHIIHHHHI', 0xFFFE, ch, rate, rate * bpf, bpf, bits, 22, b, mask) + \
              bytes([1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71])
    else:
        fmt = struct.pack('<HHIIHH', 1, ch, rate, rate * bpf, bpf, bits)
    pad = b'\0' if len(data) & 1 else b''
    body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + bytes(data) + pad
    open(os.path.join(out, name + '.wav'), 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)

def tone(bits, f=440.0, rate=44100, amp=0.5, noise=0.0):
    m = (1 << (bits - 1)) - 1
    def g(i, c):
        v = amp * math.sin(2 * math.pi * f * (c + 1) * i / rate) + noise * (rnd.random() - 0.5)
        return max(-m - 1, min(m, int(round(v * m))))
    return g

def noise(bits):
    return lambda i, c: rnd.randrange(-(1 << (bits - 1)), 1 << (bits - 1))

for n in [1, 2, 15, 16, 17, 255, 4095, 4096, 4097, 65536, 65537, 70000]:
    wav('len%d' % n, 44100, 2, 16, n, tone(16, noise=0.01))
wav('silence', 44100, 2, 16, 20000, lambda i, c: 0)
wav('dc', 44100, 1, 16, 20000, lambda i, c: -1234)
wav('square', 44100, 2, 16, 20000, lambda i, c: 32767 if (i // 50) & 1 else -32768)
wav('noise16', 44100, 2, 16, 20000, noise(16))
wav('noise24', 48000, 2, 24, 20000, noise(24))
wav('wasted', 44100, 2, 16, 20000, lambda i, c: tone(16, noise=0.02)(i, c) & ~15)
wav('u8', 22050, 1, 8, 20000, tone(8, noise=0.05))
wav('s24', 96000, 2, 24, 30000, tone(24, noise=0.001))
wav('s32', 48000, 2, 32, 20000, tone(32, noise=0.0001))
wav('n32', 48000, 1, 32, 5000, noise(32))
wav('v20', 44100, 2, 24, 20000, tone(20, noise=0.01), valid=20)
wav('v12', 44100, 2, 16, 20000, tone(12, noise=0.01), valid=12)
for ch in range(3, 9):
    wav('ch%d' % ch, 44100, ch, 16, 10000, tone(16, noise=0.01))
for r in [8000, 11025, 44101, 96000, 192000, 384000, 700000]:
    wav('rate%d' % r, r, 1, 16, 10000, tone(16, f=1000, rate=r, noise=0.01))
wav('fullscale', 44100, 2, 16, 20000, lambda i, c: (32767, -32768)[(i * 7 + c) % 2])
print('wrote', len(os.listdir(out)), 'files to', out)
