# Encodes the deterministic test signals with a flapus binary and prints one SHA-256
# over all Opus and FLAC files. Every build, on every system, must print the same.
#
#   python test/crosscheck.py <flapus binary> [<work directory>]
#
# Decoded WAV files are only checked for errors: the decoders use libm.
import hashlib, os, subprocess, sys

exe = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else 'signals')
here = os.path.dirname(os.path.abspath(__file__))
subprocess.run([sys.executable, os.path.join(here, '..', 'tools', 'testsignal.py'), work], check=True)

cases = [  # output name, input, options
    ('stereo44.opus', 'stereo44.wav', []),
    ('stereo44-24k.opus', 'stereo44.wav', ['--bitrate', '24']),
    ('stereo44-60ms.opus', 'stereo44.wav', ['--bitrate', '64', '--framesize', '60', '--hard-cbr']),
    ('stereo44-mono.opus', 'stereo44.wav', ['--downmix-mono', '--bitrate', '12']),
    ('mono48.opus', 'mono48.wav', []),
    ('mono48-speech.opus', 'mono48.wav', ['--speech', '--bitrate', '10', '--comp', '5']),
    ('stereo44.flac', 'stereo44.wav', []),
    ('stereo44-8.flac', 'stereo44.wav', ['--level', '8', '--lax']),
    ('mono48.flac', 'mono48.wav', []),
    ('stereo44-dec.wav', 'stereo44.opus', []),
    ('mono48-dec.flac', 'mono48.opus', []),
]
lines = []
for out, src, opts in cases:
    o = os.path.join(work, out)
    cmd = [exe, '-s', '-f', '--serial', '1'] if out.endswith('.opus') else [exe, '-s', '-f']
    subprocess.run(cmd+opts+[os.path.join(work, src), o], check=True)
    if out.endswith('.wav') or out.endswith('-dec.flac'):
        continue
    lines.append('%s  %s' % (hashlib.sha256(open(o, 'rb').read()).hexdigest(), out))
print('\n'.join(lines))
digest = hashlib.sha256('\n'.join(lines).encode()).hexdigest()
print('digest', digest)
if os.environ.get('GITHUB_OUTPUT'):
    with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
        f.write('digest=%s\n' % digest)
