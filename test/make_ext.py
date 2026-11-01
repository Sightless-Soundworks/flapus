# Builds the extra test inputs in ext/ from st44.wav: other sample rates, 8/24-bit PCM and
# 3..12 channel files (the multichannel ones are also relabelled to 8/12/16 kHz).
import array, os, wave

def write(name, rate, ch, data, width=2):
    o = wave.open(os.path.join('ext', name), 'wb')
    o.setnchannels(ch)
    o.setsampwidth(width)
    o.setframerate(rate)
    o.writeframes(data)
    o.close()

os.makedirs('ext', exist_ok=True)
w = wave.open('st44.wav')
s = array.array('h', w.readframes(w.getnframes()))
w.close()
L, R = s[0::2], s[1::2]
seg = 44100*8
for r in (8000, 11025, 12000, 16000, 22050, 24000, 32000, 96000):
    write('st%d.wav' % r, r, 2, s[:seg*2].tobytes())
    write('mo%d.wav' % r, r, 1, L[:seg].tobytes())
write('u8.wav', 44100, 2, bytes(((x >> 8)+128) & 255 for x in s[:seg*2]), 1)
b = bytearray()
for x in s[:seg*2]:
    b += (((x << 8) | ((x*7) & 255)) & 0xffffff).to_bytes(3, 'little')
write('s24.wav', 44100, 2, bytes(b), 3)
for ch in (3, 4, 5, 6, 7, 8, 10, 12):
    d = array.array('h')
    for i in range(44100*5):
        for c in range(ch):
            d.append(L[i] if c % 2 == 0 else R[i]//(c+1))
    write('ch%d.wav' % ch, 44100, ch, d.tobytes())
    if ch in (3, 6, 8):
        for r in (8000, 12000, 16000):
            write('ch%d_%d.wav' % (ch, r), r, ch, d.tobytes())
print('ext/ written')
