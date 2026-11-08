# flapus

flapus is an Opus and FLAC encoder and decoder written in C. It works as a
single-header library (`flapus.h`) and as a command-line tool (`flapus`), and it
needs nothing besides the C library. The aim is the best possible encoding and
decoding.

| Format | Encoder | Decoder |
|--------|---------|---------|
| Ogg Opus | based on libopus 1.6.1, made for the **best possible encoding** | based on libopus 1.6.1 and opusfile 0.12, made for the **best possible decoding** |
| FLAC | own encoder built for the **smallest files** (0.6-1.9 % below `flac -8` within the FLAC subset, 2-7 % with `--lax`) | complete RFC 9639 decoder, byte-identical WAV output to `flac -d` |

Any input converts to any output: WAV, FLAC, Ogg FLAC (input only) and Ogg Opus. That
includes FLAC → Opus, Opus → FLAC and FLAC → FLAC (recompression).

## Folder layout

```
flapus.h           the single-file library (generated from src/ and LICENSE)
flapus_cli.c       command line tool
src/               library sources (see NOTES.md for the file map)
tools/             amalgamate.py, comparison scripts, generators for tables and SILK code
test/              regression scripts and test signals
NOTES.md           implementation notes (Opus precision, FLAC encoder design)
LICENSE            BSD license and upstream copyright notices
```

## Building

GCC or Clang (tested with w64devkit GCC on Windows, 64- and 32-bit):

```bash
gcc -O2 -ffp-contract=off -o flapus.exe flapus_cli.c
```

- `-ffp-contract=off` is required: it disables fused multiply-add. Never use
  `-ffast-math`. The header also sets this with a pragma.
- On 32-bit x86, float arithmetic must use SSE2, which is the default of w64devkit 2.11
  and later. With x87 arithmetic (`-mfpmath=387`, the default of older 32-bit
  toolchains) the encoder rounds differently than designed and some files encode
  differently, so the header stops with an error. Add `-msse2 -mfpmath=sse`, or define
  `FLAPUS_NO_ENCODER` if you only need the decoders.
- On Linux or macOS, add `-pthread -lm`.
- After changing anything in `src/` or `LICENSE`, regenerate the header first:

```bash
python tools/amalgamate.py
```

## Official builds

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds `flapus-x86.exe`
(32-bit) and `flapus-x64.exe` (64-bit) for Windows on every push. Each is a single
file that needs no DLLs beyond Windows' own.

- **Toolchain:** w64devkit 2.11.0 (MinGW-w64 with the system `msvcrt.dll`), pinned
  by sha256.
- **Compatibility:** the exes import only `KERNEL32.dll` and `msvcrt.dll`, so they
  also run on older Windows versions. The x86 build needs a CPU with SSE2.
- **Encoder:** built with `FLAPUS_PORTABLE_MATH`, so the Opus output is the same on
  every x86 CPU.
- **Checks:** the workflow verifies that `flapus.h` matches `src/`, checks the imports
  and the subsystem version of both exes, and runs the four tests. It encodes
  deterministic test signals (`tools/testsignal.py`) with both builds and fails if the
  Opus files differ.
- **Release:** every push to `main` publishes both exes and `flapus.h` as the
  pre-release `latest`, and a tag `v*` publishes them as a versioned release. Each
  workflow run also keeps the two exes as artifacts.

The builds are reproducible: the same commit and devkit give the same exe, byte for
byte, on any machine. To check a release, build it in the devkit shell and compare
with the SHA-256 that GitHub shows next to each release asset:

```bash
gcc -O2 -ffp-contract=off -msse2 -mfpmath=sse -DFLAPUS_PORTABLE_MATH \
    -fno-ident -s -Wl,--no-insert-timestamp -o flapus-x86.exe flapus_cli.c
```

For x64, leave out `-msse2 -mfpmath=sse`. `flapus --version` shows the version,
how the encoder was built and the target. The version is also `FLAPUS_VERSION` in
`flapus.h`.

## Command line

```bash
flapus [options] input output
```

flapus detects the input format from the file content. The output format follows the
file extension (`.wav`, `.flac`, `.opus`) unless `--format wav|flac|opus` is given.

```bash
flapus music.wav music.flac                   # FLAC subset file (level 5)
flapus --lax music.wav music.flac             # even smaller, outside the FLAC subset
flapus music.flac music.opus                  # FLAC -> Opus
flapus --bitrate 96 music.wav music.opus      # Opus at 96 kbit/s
flapus music.opus music.flac                  # Opus -> FLAC
flapus old.flac new.flac                      # recompress, tags and pictures are kept
flapus --rate 48000 music.opus music48k.wav
flapus --delete-input-file music.wav music.flac   # remove music.wav after success
ffmpeg -i video.mkv -f wav - | flapus - audio.opus   # read from stdin
flapus --format wav music.flac - | lame - music.mp3  # write to stdout
```

`-` as input reads stdin, and `-` as output writes stdout; the output format is then
given with `--format`. stdout cannot seek back, so the headers are written once: a WAV
header gets the length from the source (the maximum if it is unknown, as for Opus
input), and the FLAC STREAMINFO has no MD5 and no frame sizes, like `flac` writing to
stdout.

While it works, flapus shows progress on stderr the way flac does:

```
music.wav: 42% complete, ratio=0.541
music.wav: wrote 3723033 bytes, ratio=0.704
```

Decoding shows only the percentage and ends with `done`.

**General options**

- `--format wav|flac|opus` sets the output format instead of the extension (needed
  for stdout).
- `-s` or `--silent` turns the progress display off.
- `-V` or `--version` shows the version, how the Opus encoder was built (portable
  math or not) and the target.
- `--delete-input-file` deletes the input file after a successful conversion. It is
  off by default, and nothing is deleted when an error occurs. It cannot be combined
  with stdin input.

**FLAC output options**

- `--level N` sets the effort: 0 (fastest) to 8 (smallest). The default is 5.
- `--lax` allows streams outside the FLAC subset: LPC order up to 32 and blocks up to
  16384 samples. The files are valid FLAC and decode with libFLAC and FFmpeg, but some
  hardware players only support the subset.
- `--threads N` sets the number of encoder threads. The default is 1, and 0 means one
  per CPU. The output does not depend on it.

**Opus input options**

- `--rate N` sets the output rate. The default is the original input rate.
- `--no-dither` uses plain rounding instead of shaped dither.

**Opus output options**

`--bitrate N` (kbit/s, default automatic), `--quality N` (0-10, default 6), `--vbr`,
`--cvbr`, `--hard-cbr`, `--music`, `--speech`, `--comp N`, `--framesize N` (2.5 to
120 ms), `--expect-loss N`, `--downmix-mono`, `--downmix-stereo`, `--no-phase-inv`,
`--padding N` (free bytes after the tags, default 512, 0 = none) and `--serial N` (Ogg
serial number, default random).

With a fixed `--serial N`, the output depends only on the input and the options: same
input, same bytes, on the same kind of CPU.

**Not across CPU vendors.** The default encoder uses two kinds of instructions whose
results are up to the CPU:

- `rcpps` and `rsqrtps` in the CELT PVQ search (libopus's `op_pvq_search_sse2`).
  They are approximations with 12 significant bits, and AMD's bits differ from
  Intel's. These are what changed the output: the default build wrote different audio
  packets on an AMD EPYC 7763 than on an Intel Core i7-13700K for 2982 of the 3179
  files of a speech corpus.
- The x87 transcendental instructions `fsin`, `fcos`, `fyl2x`, `fyl2xp1` and `f2xm1`
  (through MinGW's libm). Only the basic operations and `fsqrt` are exact in IEEE
  terms. A probe over 20000 fixed inputs gave different results for all five on the
  two CPUs, but on that corpus it did not change a single packet.

**`FLAPUS_PORTABLE_MATH`** makes the encoder the same on every x86 CPU. Define it before
the implementation, and:

- `rcpps`/`rsqrtps` come from Intel's values in two tables of 2048 entries
  (`src/enc_rcp_tables.c`). On the Intel CPU, `rcpps` depends only on the top 11
  mantissa bits of its input and `rsqrtps` on the top 10 plus the exponent parity.
  `tools/gen_rcp.c` builds the tables there and checks the emulation against the
  hardware for all 2^32 inputs of both instructions.
- The x87 transcendentals (plus `fpatan` inside MinGW's `acosl`) are replaced by
  functions that only use operations IEEE 754 defines exactly: +, -, *, / and `fsqrt`
  on long double, and exact exponent manipulation. Measured against 50-digit
  reference values, 4000 inputs per function:

| Function | Portable, max error | x87 on Intel, max error |
|----------|--------------------:|------------------------:|
| ln       | 0.66 ulp | 0.76 ulp |
| ln(1+x)  | 1.63 ulp | 0.81 ulp |
| 2^x-1    | 2.36 ulp | 0.68 ulp |
| sin (arguments up to ±400) | 1.60 ulp | 18794 ulp |
| cos      | 1.76 ulp | 153 ulp |
| acos     | 1.93 ulp | 152 ulp |

(ulp of the 64-bit mantissa.) The results are rounded to double or float before the
encoder uses them, so the difference rarely shows.

On the 3179-file corpus the portable build wrote byte-identical files to the default
build on Intel, and the same files on the AMD runner as on the Intel machine. The PVQ
part is exact by construction; the x87 part is measured, so other material can in a
rare frame take another decision than the default build. What is guaranteed is the
same bytes on every CPU.

Only the Opus encoder is covered. The Opus decoder and the FLAC encoder still call
libm's `sin`, `cos` and `log`, so their output can still depend on the CPU.

### FLAC compression

The table compares file sizes with `flac -8 --no-padding --no-seektable` (flac 1.5.0) on
30-second excerpts of music and speech:

| Material | level 4 | level 5 (default) | level 8 | level 8 `--lax` |
|----------|--------:|------------------:|--------:|----------------:|
| music, 24-bit | -1.31 % | -1.42 % | -1.48 % | -3.13 % |
| music, 16-bit | -0.79 % | -0.83 % | -0.87 % | -3.60 % |
| music, 16-bit | -0.54 % | -0.57 % | -0.59 % | -2.15 % |
| drums, 24-bit | -1.27 % | -1.29 % | -1.33 % | -3.01 % |
| vocals, 16-bit | -1.40 % | -1.45 % | -1.51 % | -3.41 % |
| speech, 16-bit | -1.63 % | -1.85 % | -1.91 % | -7.10 % |

The table below shows encoding speed for 16-bit stereo, as multiples of real time:

| Level | 1 thread (default) | 24 threads (`--threads 0`) |
|-------|-------------------:|---------------------------:|
| 3 | 13x | |
| 4 | 4.6x | 40x |
| 5 (default) | 1.6x | |
| 8 | 0.43x | 5x |

From level 5 to 8 the files shrink by only about 0.05 %, at about a quarter of the speed.
Use `--level 8` when the last bytes matter more than time.

flapus also omits padding and seek tables, and writes a `VORBIS_COMMENT` block only
when there are tags. That makes the files smaller than `flac` defaults by about 8 KB
more.

## Library use

In exactly one C file:

```c
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"
```

Every other file just includes `flapus.h`. The declarations are C++-compatible; compile
the implementation as C. `flapus.h` is complete on its own: the license texts are at its
end.

File conversion, the same as the CLI:

```c
fl_options opt;
fl_options_init(&opt);              /* defaults: automatic Opus settings, FLAC level 5 */
opt.bitrate = 64000;                /* used for Opus output */
int err = fl_convert_file("in.flac", "out.opus", &opt);
```

Audio in memory (any input format, WAV/FLAC/Opus output):

```c
fl_audio a;                                   /* interleaved int samples, a.bits bits */
err = fl_read_audio("in.opus", NULL, &a);
err = fl_write_audio("out.flac", &a, NULL);   /* format from the extension or opt->format */
fl_audio_free(&a);
```

Opus packets (48 kHz interleaved float):

```c
fl_opus_encoder *enc = fl_opus_encoder_create(channels, &opt, &err);
int n = fl_opus_encoder_frame_size(enc);               /* samples per channel per packet */
int len = fl_opus_encode_float(enc, pcm, packet, sizeof(packet));

int family, streams, coupled; unsigned char map[255];
fl_opus_encoder_layout(enc, &family, &streams, &coupled, map);
fl_opus_ms_decoder *dec = fl_opus_ms_decoder_create(48000, channels, streams, coupled, map, &err);
int got = fl_opus_ms_decode_float(dec, packet, len, out, 5760); /* len 0 = packet loss concealment */
```

All functions return a negative `FL_ERR_*` code on error. See the comments in `src/api.h`
(the top of `flapus.h`) for the full API.

Paths are UTF-8 on Windows: a path that is valid UTF-8 is opened with `_wfopen`, so
file names outside the ANSI code page work. Any other path, such as an ANSI `argv`
string, goes to `fopen` unchanged. The path `-` is stdin or stdout, as on the command
line.

### Platforms

- **FLAC and all decoders**: portable C99. Threads use the Win32 API on Windows and
  pthreads elsewhere. Define `FLAPUS_NO_THREADS` to build without threads.
- **Opus encoder**: needs GCC or Clang on x86 or x86-64. It computes with 80-bit x87
  extended precision, through `long double` and a few x87 instructions.
  - On other compilers or CPUs (MSVC, ARM) the Opus encoder is left out automatically,
    and `FLAPUS_HAVE_ENCODER` is not defined.
  - Define `FLAPUS_NO_ENCODER` to leave it out deliberately.

## Supported formats

**Input**

- **WAV**: 8/16/24/32-bit PCM or 32-bit float, `WAVE_FORMAT_EXTENSIBLE` with
  valid-bits.
- **FLAC**: any bit depth from 4 to 32 bits, 1-8 channels, fixed or variable block
  size. Ogg FLAC and leading ID3v2 tags are accepted. The MD5 is checked, and a
  mismatch is reported as `FL_ERR_CHECKSUM`.
- **Ogg Opus**: channel mapping families 0, 1, 2 and 255, chained and multiplexed
  files.

**FLAC output**

- FLAC takes integer PCM only, from 4 to 32 bits and 1-8 channels.
- Tags and pictures carry over between FLAC and Opus:
  - FLAC pictures become `METADATA_BLOCK_PICTURE` comments in Opus, and back.
  - On Opus → FLAC the Opus encoder tags are dropped.
  - On FLAC → FLAC, `APPLICATION` and `CUESHEET` blocks are also kept.

**Opus output**

- Every input can be encoded, including FLAC.
- FLAC ReplayGain tags become the OpusHead output gain and `R128_TRACK_GAIN`.
- Surround: family 1 is used for 3-8 channels, family 255 above that.

**WAV output**

- From Opus: 16-bit at the original rate, `WAVE_FORMAT_EXTENSIBLE` for 3-8 channel
  surround.
- From FLAC: the original bit depth, with flac's header (`WAVE_FORMAT_EXTENSIBLE` above
  2 channels or 16 bits).

**Not supported**

- AIFF and raw input.
- Ogg FLAC output.
- Ambisonics Opus encoding and Opus mapping family 3 decoding.
- Metadata options for Opus output (title, pictures).

## Verification

The checks in `test/` compare flapus with the established tools (opusenc, opusdec,
flac). Run them from a shell in which opusenc works (on this machine it fails when the
w64devkit `bin` folder comes first in `PATH`).

- The Opus test signals are not part of the repository. `grid.sh`, `make_ext.py` and
  `run_flac_opus.sh` need 20-second WAV clips in `test/`: `st44.wav` (16-bit stereo,
  44.1 kHz), `st48.wav` (48 kHz), and `mono44.wav`, `mono16.wav`, `mono8.wav` (mono at
  44.1, 16 and 8 kHz). Run `python make_ext.py` in `test/` first to build `ext/`.
  `run_flac.sh` generates its own inputs.
- `grid.sh` (uses `run_cmp.sh`): 115 Opus encoder cases, compared with opusenc.
  - Covers bitrates 6-256 kbit/s, all frame sizes, VBR/CVBR/CBR, complexity,
    SILK/hybrid/CELT, mono/stereo, 8-96 kHz input, 8/24-bit input, all options, and
    3-12 channel surround.
  - `make_ext.py` builds the extra inputs. The `OPUSENC` variable selects the opusenc
    binary.
- `run_flac_opus.sh`: FLAC → Opus, compared with opusenc reading the same FLAC file.
  This covers flac- and flapus-made files, 8-24 bit, 3-8 channels, 96 kHz and the
  ReplayGain rules.
- `run_flac.sh`: 38 edge cases (`make_flac_cases.py`) at levels 0, 3, 8 and `--lax`.
  - Every file passes `flac -t`.
  - Both flapus and `flac -d` decode every sample back exactly.
  - The cases cover 1 to 70000 samples, silence, noise, full scale, 8/12/16/20/24/32
    bits, 3-8 channels, and rates from 8 kHz to 700 kHz.
- The FLAC decoder gives byte-identical WAV files to `flac -d` on 88 files encoded with
  different flac settings (`-0` to `-8ep`, `--lax -l 32 -b 16384`, block size 16 to
  65535). FFmpeg decodes flapus FLAC files exactly as well.
- `run_ref.sh` / `run_dec.sh` re-encode and decode the Opus test files (`mand*.opus`,
  `mand*.wav`), which must be in the folder above the project.
- `test/api`: a two-file program using only the public API.
- `test/rcp_tables.c`: the `rcpps`/`rsqrtps` tables of `FLAPUS_PORTABLE_MATH`, on any
  CPU: both ends of every table block within Intel's bound (relative error at most
  1.5*2^-12), plus hardware values and special cases. The full check against the
  hardware, all 2^32 inputs, is in `tools/gen_rcp.c` and needs an Intel CPU.
- `test/portable_math.c`: the `FLAPUS_PORTABLE_MATH` functions against exact values
  (60-digit decimal arithmetic) at 20 points, limit 3 ulp. Passes at -O0, -O2 and -Oz;
  cutting one series short makes it fail, as it should.
- `test/bitreader.c`: unary codes that end exactly on the last bit of a full 64-bit
  cache. Such a code used to shift the cache by 64 (undefined in C; on x86 the bit
  stayed put), which flipped a bit 64 positions later. A real FLAC file with a Rice
  parameter of 3 and quotients up to 76 decoded wrongly because of it, and its MD5
  check failed. libFLAC decodes the same file correctly. On a 3179-file speech
  corpus that one file was the only one affected.
- The 32-bit build gives the same Opus packets and the same FLAC files.
