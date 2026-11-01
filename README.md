# flapus

flapus is an Opus and FLAC encoder and decoder written in C. It works as a
single-header library (`flapus.h`) and as a command-line tool (`flapus.exe`), and it
needs nothing besides the C library.

| Format | Encoder | Decoder |
|--------|---------|---------|
| Ogg Opus | **bit-identical** to `opusenc` (opus-tools 0.2-53, libopus 1.6.1, 32-bit x87 Windows build) | **bit-identical** to `opusdec` (opus-tools 0.2, opusfile 0.12, libopus 1.6.1) |
| FLAC | own encoder built for the **smallest files** (0.6-1.9 % below `flac -8` within the FLAC subset, 2-7 % with `--lax`) | complete RFC 9639 decoder, byte-identical WAV output to `flac -d` |

Any input converts to any output: WAV, FLAC, Ogg FLAC (input only) and Ogg Opus. That
includes FLAC → Opus, Opus → FLAC and FLAC → FLAC (recompression).

For Opus, only metadata differs from the reference tools: the vendor string and the
`ENCODER` comment.

## Folder layout

```
flapus.h           the single-file library (generated from src/ by tools/amalgamate.py)
flapus_cli.c       command line tool
flapus.exe         prebuilt 64-bit Windows CLI
src/               library sources (see NOTES.md for the file map)
tools/             amalgamate.py, comparison scripts, generators for tables and SILK code
test/              regression scripts and test signals
NOTES.md           implementation notes (Opus bit-exactness, FLAC encoder design)
LICENSE            BSD license and upstream copyright notices
```

## Building

GCC or Clang (tested with w64devkit GCC on Windows, 64- and 32-bit):

```bash
gcc -O2 -ffp-contract=off -o flapus.exe flapus_cli.c
```

- `-ffp-contract=off` is required: it disables fused multiply-add. Never use
  `-ffast-math`. The header also sets this with a pragma.
- On Linux or macOS, add `-pthread -lm`.
- After changing anything in `src/`, regenerate the header first:

```bash
python tools/amalgamate.py
```

## Command line

```bash
flapus [options] input output
```

flapus detects the input format from the file content. The output format follows the
file extension (`.wav`, `.flac`, `.opus`) unless `--format wav|flac|opus` is given.

```bash
flapus music.wav music.flac                   # FLAC, smallest subset file (level 8)
flapus --lax music.wav music.flac             # even smaller, outside the FLAC subset
flapus music.flac music.opus                  # FLAC -> Opus, identical to opusenc music.flac
flapus --bitrate 96 music.wav music.opus      # Opus at 96 kbit/s
flapus music.opus music.flac                  # Opus -> FLAC (decoded like opusdec)
flapus old.flac new.flac                      # recompress, tags and pictures are kept
flapus --rate 48000 music.opus music48k.wav
flapus --delete-input-file music.wav music.flac   # remove music.wav after success
```

While it works, flapus shows progress on stderr the way flac does:

```
music.wav: 42% complete, ratio=0.541
music.wav: wrote 3723033 bytes, ratio=0.704
```

Decoding shows only the percentage and ends with `done`.

**General options**

- `--format wav|flac|opus` sets the output format instead of the extension.
- `-s` or `--silent` turns the progress display off.
- `--delete-input-file` deletes the input file after a successful conversion. It is
  off by default, and nothing is deleted when an error occurs.

**FLAC output options**

- `--level N` sets the effort: 0 (fastest) to 8 (smallest, the default).
- `--lax` allows streams outside the FLAC subset: LPC order up to 32 and blocks up to
  16384 samples. The files are valid FLAC and decode with libFLAC and FFmpeg, but some
  hardware players only support the subset.
- `--threads N` sets the number of encoder threads. The default is 1, and 0 means one
  per CPU. The output does not depend on it.

**Opus input options** (as `opusdec`)

- `--rate N` sets the output rate. The default is the original input rate.
- `--no-dither` uses plain rounding instead of shaped dither.

**Opus output options**

These have the same meaning and defaults as in `opusenc`: `--bitrate N` (kbit/s),
`--quality N`, `--vbr`, `--cvbr`, `--hard-cbr`, `--music`, `--speech`, `--comp N`,
`--framesize N` (2.5 to 120 ms), `--expect-loss N`, `--downmix-mono`,
`--downmix-stereo`, `--no-phase-inv`.

### FLAC compression

The table compares file sizes with `flac -8 --no-padding --no-seektable` (flac 1.5.0) on
30-second excerpts of music and speech:

| Material | level 4 | level 8 (default) | `--lax` |
|----------|--------:|------------------:|--------:|
| music, 24-bit | -1.31 % | -1.48 % | -3.13 % |
| music, 16-bit | -0.79 % | -0.87 % | -3.60 % |
| music, 16-bit | -0.54 % | -0.59 % | -2.15 % |
| drums, 24-bit | -1.27 % | -1.33 % | -3.01 % |
| vocals, 16-bit | -1.40 % | -1.51 % | -3.41 % |
| speech, 16-bit | -1.63 % | -1.91 % | -7.10 % |

The table below shows encoding speed for 16-bit stereo, as multiples of real time:

| Level | 1 thread (default) | 24 threads (`--threads 0`) |
|-------|-------------------:|---------------------------:|
| 3 | 13x | |
| 4 | 4.6x | 40x |
| 5 | 1.6x | |
| 8 (default) | 0.43x | 5x |

From level 5 to 8 the files shrink by only about 0.05 %. `--level 5` is therefore a good
choice when time matters.

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
the implementation as C.

File conversion, the same as the CLI:

```c
fl_options opt;
fl_options_init(&opt);              /* defaults: opusdec/opusenc settings, FLAC level 8 */
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

### Platforms

- **FLAC and all decoders**: portable C99. Threads use the Win32 API on Windows and
  pthreads elsewhere. Define `FLAPUS_NO_THREADS` to build without threads.
- **Opus encoder**: needs GCC or Clang on x86 or x86-64. The reference encoder is a
  32-bit x87 build, and its 80-bit register arithmetic is reproduced with `long double`
  and a few x87 instructions.
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
- **Ogg Opus**: channel mapping families 0, 1, 2 and 255. Chained and multiplexed files
  are handled like opusdec.

**FLAC output**

- FLAC takes integer PCM only, from 4 to 32 bits and 1-8 channels.
- Tags and pictures carry over between FLAC and Opus:
  - FLAC pictures become `METADATA_BLOCK_PICTURE` comments in Opus, and back.
  - On Opus → FLAC the Opus encoder tags are dropped.
  - On FLAC → FLAC, `APPLICATION` and `CUESHEET` blocks are also kept.

**Opus output**

- Output works like opusenc for every input, including FLAC.
- FLAC ReplayGain tags become the OpusHead output gain and `R128_TRACK_GAIN`.
- Surround: family 1 is used for 3-8 channels, family 255 above that.

**WAV output**

- From Opus: 16-bit, with opusdec's header and channel order.
- From FLAC: the original bit depth, with flac's header (`WAVE_FORMAT_EXTENSIBLE` above
  2 channels or 16 bits).

**Not supported**

- AIFF and raw input.
- Ogg FLAC output.
- Ambisonics Opus encoding and Opus mapping family 3 decoding.
- opusenc metadata options (title, pictures, padding).

## Verification

All checks compare against the real reference tools (`test/`). Run them from a shell in
which opusenc works (on this machine it fails when the w64devkit `bin` folder comes first
in `PATH`).

- The Opus test signals are not part of the repository. `grid.sh`, `make_ext.py` and
  `run_flac_opus.sh` need 20-second WAV clips in `test/`: `st44.wav` (16-bit stereo,
  44.1 kHz), `st48.wav` (48 kHz), and `mono44.wav`, `mono16.wav`, `mono8.wav` (mono at
  44.1, 16 and 8 kHz). Run `python make_ext.py` in `test/` first to build `ext/`.
  `run_flac.sh` generates its own inputs.
- `grid.sh` (uses `run_cmp.sh`): 115 Opus encoder cases. Every audio packet, the
  OpusHead and the end granule are identical to opusenc.
  - Covers bitrates 6-256 kbit/s, all frame sizes, VBR/CVBR/CBR, complexity,
    SILK/hybrid/CELT, mono/stereo, 8-96 kHz input, 8/24-bit input, all options, and
    3-12 channel surround.
  - `make_ext.py` builds the extra inputs. The `OPUSENC` variable selects the reference
    binary.
- `run_flac_opus.sh`: FLAC → Opus is identical to opusenc reading the same FLAC file.
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
- `run_ref.sh` / `run_dec.sh` re-encode and decode the original reference files
  (`mand*.opus`, `mand*.wav`), which must be in the folder above the project.
- `test/api`: a two-file program using only the public API.
- The 32-bit build gives the same Opus packets and the same FLAC files.
