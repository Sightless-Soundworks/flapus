# flapus

flapus is an Opus and FLAC encoder and decoder written in C. It works as a
single-header library (`flapus.h`) and as a command-line tool (`flapus`), and it
needs nothing besides the C library. The aim is the best possible encoding and
decoding.

| Format | Encoder | Decoder |
|--------|---------|---------|
| Ogg Opus | libopus 1.6.1 itself, built in, for the **best possible encoding** | based on libopus 1.6.1 and opusfile 0.12, made for the **best possible decoding** |
| FLAC | own encoder built for the **smallest files** (0.6-1.9 % below `flac -8` within the FLAC subset, 2-7 % with `--lax`) | complete RFC 9639 decoder, byte-identical WAV output to `flac -d` |

Any input converts to any output: WAV, FLAC, Ogg FLAC (input only) and Ogg Opus. That
includes FLAC → Opus, Opus → FLAC and FLAC → FLAC (recompression).

Please Note: this tool has been created with the help of AI, so not everything might be perfect. All contributions and feedback are very welcome.

## Folder layout

```
flapus.h           the single-file library (generated from src/ and LICENSE)
flapus_cli.c       command line tool
src/               library sources (see NOTES.md for the file map)
tools/             amalgamate.py, comparison scripts, generators for the tables and the Opus encoder
test/              regression scripts and test signals
NOTES.md           implementation notes (Opus encoder generation, FLAC encoder design)
LICENSE            BSD license and upstream copyright notices
```

## Building

Any C99 compiler: GCC, Clang or MSVC, on any CPU.

```bash
gcc -O2 -o flapus flapus_cli.c -pthread -lm     # Linux, macOS (clang works the same)
cl /O2 flapus_cli.c                              # MSVC
```

- Never use `-ffast-math`. Fused multiply-add is turned off by a pragma in the header
  (GCC, Clang, MSVC); with another compiler, turn off floating point contraction
  yourself.
- The output is the same on every platform: same input and options, same bytes, from
  Windows on x86 to macOS on Apple Silicon. On 32-bit x86 that needs SSE2 arithmetic
  (`-msse2 -mfpmath=sse`, the default of w64devkit); with other float arithmetic the
  files are just as good, but not byte for byte the same. This holds for Opus and FLAC
  output; decoded WAV can differ in the last bit between C libraries (see Platforms).
- On Windows, threads use the Win32 API; `-pthread` is only needed elsewhere.
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
- **Checks:** the workflow verifies that `flapus.h` matches `src/`, checks the imports
  and the subsystem version of both exes, and runs the tests. It encodes
  deterministic test signals (`tools/testsignal.py`) with both builds and fails if the
  Opus files differ.
- **Release:** a push to `main` with a new version publishes both exes and `flapus.h`
  as the release `V<version>` (for example `V1.0`), with the commits since the
  previous release as release notes. A push that keeps the version publishes
  nothing. Each workflow run also keeps the two exes as artifacts.

The builds are reproducible: the same commit and devkit give the same exe, byte for
byte, on any machine. To check a release, build it in the devkit shell and compare
with the SHA-256 that GitHub shows next to each release asset:

```bash
gcc -O2 -ffp-contract=off -msse2 -mfpmath=sse \
    -fno-ident -s -Wl,--no-insert-timestamp -Wl,--gc-sections -o flapus-x86.exe flapus_cli.c
```

For x64, leave out `-msse2 -mfpmath=sse`. `flapus --version` shows the version,
whether the Opus encoder is built in, the target and the compiler. The version is also
`FLAPUS_VERSION` in `flapus.h`.

To release a new version, change `FLAPUS_VERSION_MAJOR`, `FLAPUS_VERSION_MINOR` or
`FLAPUS_VERSION_PATCH` in `src/api.h` (1, 1, 0 is version 1.1), run
`python tools/amalgamate.py` and push both files to `main`.

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
flapus -f music.flac music.flac               # recompress in place
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
- `-f` or `--force` overwrites an existing output file. Without it flapus stops with an
  error, as `flac` does.
- `-s` or `--silent` turns the progress display off.
- `-h` or `--help` shows the options.
- `-V` or `--version` shows the version, whether the Opus encoder is built in, and the
  target and compiler.
- `--delete-input-file` deletes the input file after a successful conversion. It is
  off by default, and nothing is deleted when an error occurs. It cannot be combined
  with stdin input, or with an output that is the input file itself.

The output is written to a temporary file next to it (`name.1a2b3c4d.tmp`), which
replaces the output file only when the conversion has succeeded. A failed conversion
leaves an existing file as it was, and a file can be converted onto itself, for example
to recompress a FLAC file in place.

**FLAC output options**

- `--level N` sets the effort: 0 (fastest) to 8 (smallest). The default is 5.
- `--lax` allows streams outside the FLAC subset: LPC order up to 32 and blocks up to
  16384 samples. The files are valid FLAC and decode with libFLAC and FFmpeg, but some
  hardware players only support the subset.
- `--no-seektable` leaves out the seek table (a point every 10 seconds by default).
- `--padding` reserves 8192 free bytes for later tag edits, `--padding N` N bytes. By
  default there is no padding.
- `--threads N` sets the number of encoder threads. The default is 0, one per CPU (in
  the library, `threads` defaults to 1). The output does not depend on it.

**Sample format options** (WAV and FLAC output from any input, and Opus output from
Opus input)

- `--bits N` sets the output bits: 16, 24 or 32. The default is 16 from Opus and the
  input's bit depth otherwise.
  - More bits than the input has are exact.
  - Fewer bits are rounded. To 16 bits, opusdec's shaped dither is added, except where
    the dropped bits are all zero (16-bit audio in a 24-bit file comes back exactly).
  - From Opus, the decoded samples are soft-clipped first, as in opusdec. Above 16 bits
    they are rounded without dither; the rounding error is then far below audibility.
- `--float` writes 32-bit float samples (WAV only, or Opus again): from Opus exactly as
  decoded, without clipping; from integer input exact up to 24 bits.
- `--no-dither` uses plain rounding instead of shaped dither.

**Opus input options**

- `--rate N` sets the output rate. The default is the original input rate.

**Opus output options**

`--bitrate N` (kbit/s, default automatic), `--quality N` (0-10, default 6), `--vbr`,
`--cvbr`, `--hard-cbr`, `--music`, `--speech`, `--comp N`, `--framesize N` (2.5 to
120 ms), `--expect-loss N`, `--downmix-mono`, `--downmix-stereo`, `--no-phase-inv`,
`--padding` (512 free bytes after the tags for later tag edits, as opusenc reserves;
`--padding N` for N bytes; by default none) and `--serial N` (Ogg serial number,
default random).

With a fixed `--serial N`, the output depends only on the input and the options: same
input, same bytes, on every platform.

**The encoder is libopus 1.6.1.** `src/enc_libopus.c` is generated from the libopus
sources by `tools/gen_opus_enc.py`: the float encoder as it is, with only the plain C
code paths (no SIMD intrinsics, no run-time CPU detection), every name prefixed and
static, and what the multistream encoder does not use left out. Around it,
`src/enc_front.c` does what opusenc and libopusenc 0.3 do: reading, downmix,
resampling, LPC extension of the end, Ogg pages. It follows the current opus-tools
(git, November 2026): the last release, 0.2, dates from 2018, and opusenc has changed
since (frame sizes up to 120 ms, up to 750 kbit/s per channel, more than 8 channels
downmixed to mono only, ReplayGain reference loudness in LUFS).

- libm's `log`, `log10`, `exp`, `pow`, `sin`, `cos` and `acos` are not exactly
  specified, so their last bit differs between C libraries. flapus uses its own from
  fdlibm (`src/math.c`, at most 1 ulp, log10 2), which are plain double arithmetic and
  give the same results everywhere. `sqrt`, `floor`, `fabs` and `lrint` are exact in
  every C library.
- The output is byte-identical to opusenc built the same way (plain C, no fused
  multiply-add, the same `src/math.c` functions); `tools/build_ref_opusenc.sh` builds
  that opusenc from the opus, libopusenc, opus-tools and libogg sources. An opusenc
  built with SIMD (or with another libm) writes slightly different packets of the same
  quality, because its sums are added in another order.

Encoding speed for 3 minutes of 16-bit stereo music at 44.1 kHz (default bitrate,
complexity 10, one core of an x86-64 server, GCC 13 `-O2`):

| Encoder | Time |
|---------|-----:|
| opusenc, libopus with SSE4.1/AVX2 | 1.98 s |
| opusenc, libopus plain C | 2.29 s |
| flapus | 2.27 s |

### FLAC compression

The table compares file sizes with `flac -8 --no-padding --no-seektable` (flac 1.5.0) on
30-second excerpts of music and speech, both without padding and seek table:

| Material | level 4 | level 5 (default) | level 8 | level 8 `--lax` |
|----------|--------:|------------------:|--------:|----------------:|
| music, 24-bit | -1.31 % | -1.42 % | -1.48 % | -3.13 % |
| music, 16-bit | -0.79 % | -0.83 % | -0.87 % | -3.60 % |
| music, 16-bit | -0.54 % | -0.57 % | -0.59 % | -2.15 % |
| drums, 24-bit | -1.27 % | -1.29 % | -1.33 % | -3.01 % |
| vocals, 16-bit | -1.40 % | -1.45 % | -1.51 % | -3.41 % |
| speech, 16-bit | -1.63 % | -1.85 % | -1.91 % | -7.10 % |

The table below shows encoding speed for 16-bit stereo music, as multiples of real time,
on a 2-core Xeon at 2.1 GHz. The command line tool uses every CPU by default.

| Level | 1 thread (`--threads 1`) | 2 threads |
|-------|-------------------------:|----------:|
| 3 | 11.6x | 19.7x |
| 4 | 5.5x | 8.4x |
| 5 (default) | 2.0x | 3.7x |
| 8 | 0.56x | 1.0x |

From level 5 to 8 the files shrink by only about 0.05 %, at less than a third of the speed.
Use `--level 8` when the last bytes matter more than time.

Above 48 kHz the encoder uses LPC orders up to 32 and blocks up to 16384 samples (both
within the subset there), and searches the block sizes on a coarser grid. At level 5 a
96 kHz 24-bit stereo file then encodes at about 0.8x real time with one thread; 96 to
384 kHz material from the IETF FLAC test files encoded 7.7 times faster than with the
fine grid, with 0.06 % smaller files.

Like `flac`, flapus writes a seek table with a point every 10 seconds
(`--no-seektable` leaves it out). Padding is off by default, so the file holds nothing
but audio and metadata; `--padding` adds 8192 bytes, so tags and pictures can be added
later without rewriting the whole file. A `VORBIS_COMMENT` block is written only when
there are tags.

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

Step by step, for players and long files: `fl_reader` decodes any input in pieces and
can seek (exact for WAV and FLAC; Opus restarts 80 ms early, as opusfile does), and
`fl_writer` writes WAV, FLAC or Opus in pieces:

```c
fl_reader *r = fl_reader_open("in.flac", NULL, &err);    /* or _open_memory, _open_io */
fl_info info;
fl_reader_info(r, &info);                               /* channels, rate, bits, frames */
fl_reader_seek(r, 30*(long long)info.rate);              /* to 0:30 */
int got = fl_reader_read(r, pcm, 4096);                  /* frames; 0 at the end */
fl_reader_close(r);

fl_writer *w = fl_writer_open("out.flac", &info, NULL, &err);
fl_writer_write(w, pcm, got);
err = fl_writer_close(w);                                /* finishes the file */
```

Every path function has an `_io` variant with read/write/seek callbacks (`fl_io`), for
memory, archives or network streams.

FLAC frames, with the block sizes up to the caller (for example for streaming):

```c
fl_flac_frame_encoder *fe = fl_flac_frame_encoder_create(2, 44100, 16, NULL, &err);
int bytes = fl_flac_frame_encode(fe, pcm, 4096, frame, sizeof(frame));
fl_flac_frame_encoder_streaminfo(fe, info34);            /* 34 bytes for STREAMINFO */
fl_flac_frame_decoder *fd = fl_flac_frame_decoder_create(info34, &err);
got = fl_flac_frame_decode(fd, frame, bytes, pcm, 65535, &used);
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

All functions return a negative `FL_ERR_*` code on error, and `fl_error_text()` gives a
short description of it. Options outside their documented range give `FL_ERR_BAD_ARG`.
The command line tool checks every value and names the option that is wrong. See the
comments in `src/api.h` (the top of `flapus.h`) for the full API.

Paths are UTF-8 on Windows: a path that is valid UTF-8 is opened with `_wfopen`, so
file names outside the ANSI code page work. Any other path, such as an ANSI `argv`
string, goes to `fopen` unchanged. The path `-` is stdin or stdout, as on the command
line.

### Platforms

- **FLAC and all decoders**: portable C99. Threads use the Win32 API on Windows and
  pthreads elsewhere. Define `FLAPUS_NO_THREADS` to build without threads. On x86 the
  FLAC encoder uses SSE2 when the compiler targets it (always on x86-64, with `-msse2` on
  32-bit); the output is the same either way.
- **Opus encoder**: portable C99 as well (libopus 1.6.1, see above), on every compiler
  and CPU. Define `FLAPUS_NO_ENCODER` to leave it out; `FLAPUS_HAVE_ENCODER` tells
  whether it is there.
- **Same bytes everywhere**: the Opus and FLAC encoders use the math functions of
  `src/math.c` instead of libm, so their files do not depend on the platform. The
  decoders follow opusdec and use libm's `sin`, `cos` and `exp2` for their tables, so
  decoded audio can differ in the last bit between C libraries.

## Supported formats

**Input**

- **WAV**: 8/16/24/32-bit PCM or 32-bit float, `WAVE_FORMAT_EXTENSIBLE` with
  valid-bits, sample rates up to 1048575 Hz (the FLAC limit). RF64/BW64 and Sony Wave64
  files (above 4 GB) are read as well.
- **FLAC**: any bit depth from 4 to 32 bits, 1-8 channels, fixed or variable block
  size. Ogg FLAC and leading ID3v2 tags are accepted. The MD5 is checked, and a
  mismatch is reported as `FL_ERR_CHECKSUM`.
- **Ogg Opus**: channel mapping families 0, 1, 2 and 255, chained and multiplexed
  files.
- Ogg files are read page by page, so memory use does not grow with the file size (from
  stdin they are first read into memory, since they are scanned twice).

**FLAC output**

- FLAC takes integer PCM only, from 4 to 32 bits and 1-8 channels; float WAV input needs
  `--bits`. 32-bit audio gets LPC and stereo decorrelation (33-bit side channel) like
  any other bit depth.
- Tags and pictures carry over between FLAC and Opus:
  - FLAC pictures become `METADATA_BLOCK_PICTURE` comments in Opus, and back.
  - On Opus → FLAC the Opus encoder tags are dropped.
  - On FLAC → FLAC, `APPLICATION` and `CUESHEET` blocks are also kept.

**Opus output**

- Every input can be encoded, including FLAC, at sample rates from 1000 Hz up. Unusual
  rates above about 200 kHz that would need a resampling table of more than 2^24 entries
  (64 MB) are refused.
- FLAC ReplayGain tags become the OpusHead output gain and `R128_TRACK_GAIN`.
- Surround: family 1 is used for 3-8 channels, family 255 above that.

**WAV output**

- From Opus: 16-bit at the original rate (`--bits`, `--float` and `--rate` change that),
  `WAVE_FORMAT_EXTENSIBLE` for 3-8 channel surround and above 16 bits.
- From FLAC: the original bit depth (`--bits` and `--float` change that), with flac's
  header (`WAVE_FORMAT_EXTENSIBLE` above 2 channels or 16 bits).
- Above 4 GB of samples the file is RF64 (EBU Tech 3306), which ffmpeg, Audacity and
  most editors read. When the length is not known in advance (a WAV stream on stdin), the
  header becomes RF64 at the end if needed.

**Not supported**

- AIFF and raw input.
- Ogg FLAC output.
- Ambisonics Opus encoding and Opus mapping family 3 decoding.
- Metadata options for Opus output (title, pictures).

## Verification

The checks in `test/` compare flapus with the established tools (opusenc, opusdec,
flac). The opusenc to compare with is the one `tools/build_ref_opusenc.sh` builds (see
the Opus output options); pass it in the `OPUSENC` variable.

- The Opus test signals are not part of the repository. `grid.sh`, `make_ext.py` and
  `run_flac_opus.sh` need 20-second WAV clips in `test/`: `st44.wav` (16-bit stereo,
  44.1 kHz), `st48.wav` (48 kHz), and `mono44.wav`, `mono16.wav`, `mono8.wav` (mono at
  44.1, 16 and 8 kHz). Run `python make_ext.py` in `test/` first to build `ext/`.
  `run_flac.sh` generates its own inputs.
- `grid.sh` (uses `run_cmp.sh`): 116 Opus encoder cases, compared with opusenc; all
  give byte-identical audio packets.
  - Covers bitrates 6-600 kbit/s, frame sizes 2.5-120 ms, VBR/CVBR/CBR, complexity,
    SILK/hybrid/CELT, mono/stereo, 8-96 kHz input, 8/24-bit input, all options, and
    3-12 channel surround.
  - `make_ext.py` builds the extra inputs. The `OPUSENC` variable selects the opusenc
    binary.
- `run_flac_opus.sh`: FLAC → Opus, compared with opusenc reading the same FLAC file
  (this needs an opusenc built with libFLAC).
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
- `run_dec.sh` decodes the Opus test files (`mand*.opus`) and compares with opusdec's
  WAV files (`mand*.wav`), which must be in the folder above the project.
- `test/api`: a two-file program using only the public API. It also checks that reading
  in pieces, after seeks, from memory and through callbacks gives the same samples as
  `fl_read_audio()`, that `fl_writer` and `fl_convert_io()` give the same files as the
  path functions, and that FLAC frames round-trip.
- `test/math.c`: the `src/math.c` functions against libm's long double versions at
  200000 arguments each, plus large arguments of `sin`/`cos` (up to 2^1024, among them
  the double closest to a multiple of pi/2) and the special values of C99 Annex F.
- `test/bitreader.c`: unary codes that end exactly on the last bit of a full 64-bit
  cache. Such a code used to shift the cache by 64 (undefined in C; on x86 the bit
  stayed put), which flipped a bit 64 positions later. A real FLAC file with a Rice
  parameter of 3 and quotients up to 76 decoded wrongly because of it, and its MD5
  check failed. libFLAC decodes the same file correctly. On a 3179-file speech
  corpus that one file was the only one affected.
- The builds for x86 (32- and 64-bit) and ARM64, with GCC and Clang, give the same Opus
  packets and the same FLAC files.
