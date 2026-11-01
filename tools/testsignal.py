# Writes deterministic test signals for the release cross-check.
# Integer arithmetic only, so the bytes are the same everywhere (no libm).
#
#   python tools/testsignal.py <directory>
#
# stereo44.wav  16-bit stereo 44.1 kHz, 8 s: triangle sweep, noise bursts, silence
# mono48.wav    24-bit mono 48 kHz, 6 s: stepped square tones and noise
import os, struct, sys

def lcg(state):
    state = (state * 6364136223846793005 + 1442695040888963407) & (2**64 - 1)
    return state, (state >> 33) & 0xFFFF            # 16 random bits

def wav(path, rate, channels, bits, frames):
    data = bytearray()
    for fr in frames:
        for v in fr:
            data += (v & ((1 << bits) - 1)).to_bytes(bits // 8, 'little')
    ba = channels * bits // 8
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * ba, ba, bits)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<I', len(fmt)) + fmt)
        f.write(b'data' + struct.pack('<I', len(data)) + data)

def stereo44():
    rate, n, s, phase = 44100, 44100 * 8, 12345, 0
    for i in range(n):
        step = 20 + (i * 1200) // n                  # sweep: phase step grows
        phase = (phase + step) & 0xFFFF
        tri = (phase if phase < 0x8000 else 0xFFFF - phase) - 0x4000   # -16384..16383
        s, r = lcg(s)
        noise = (r - 0x8000) >> 2 if (i // 11025) % 3 == 1 else 0
        if (i // 22050) % 7 == 6:
            left = right = 0                         # silence
        else:
            left = (tri * 3) // 4 + noise
            right = (tri // 2) - noise // 2
        yield (max(-32768, min(32767, left)), max(-32768, min(32767, right)))

def mono48():
    rate, n, s = 48000, 48000 * 6, 777
    for i in range(n):
        period = 40 + 20 * ((i // 12000) % 6)
        sq = 2000000 if (i % period) < period // 2 else -2000000
        s, r = lcg(s)
        noise = (r - 0x8000) * 16 if (i // 8000) % 4 == 3 else 0
        yield (max(-8388608, min(8388607, sq + noise)),)

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
wav(os.path.join(out, 'stereo44.wav'), 44100, 2, 16, stereo44())
wav(os.path.join(out, 'mono48.wav'), 48000, 1, 24, mono48())
