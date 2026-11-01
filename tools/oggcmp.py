# Compares two Ogg Opus files: OpusHead, every audio packet and the final granule position.
# OpusTags (vendor/comments) is metadata and is not compared. Prints one line ending in OK or DIFF.
import sys

def read(fn):
    d = open(fn, 'rb').read()
    i, pk, cur, last_gr = 0, [], b'', None
    while i < len(d):
        if d[i:i+4] != b'OggS':
            raise SystemExit('%s: not an Ogg file' % fn)
        gr = int.from_bytes(d[i+6:i+14], 'little', signed=True)
        n = d[i+26]
        p = i+27+n
        for s in d[i+27:i+27+n]:
            cur += d[p:p+s]
            p += s
            if s < 255:
                pk.append(cur)
                cur = b''
        if gr != -1:
            last_gr = gr
        i = p
    return pk, last_gr

a, ga = read(sys.argv[1])
b, gb = read(sys.argv[2])
diff = [k for k in range(2, max(len(a), len(b))) if k >= len(a) or k >= len(b) or a[k] != b[k]]
head = len(a) > 0 and len(b) > 0 and a[0] == b[0]
ok = not diff and head and ga == gb
print('packets %d/%d, differing audio packets: %d first: %s, OpusHead %s, end granule %s/%s: %s' %
      (len(a), len(b), len(diff), diff[:10], 'same' if head else 'DIFFERENT', ga, gb, 'OK' if ok else 'DIFF'))
sys.exit(0 if ok else 1)
