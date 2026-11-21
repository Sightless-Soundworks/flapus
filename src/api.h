/*
   flapus.h - single-file Opus and FLAC encoder/decoder (Ogg Opus, FLAC, Ogg FLAC, WAV),
   no dependencies besides the C library.

   Copyright (c) 2026 Sightless Soundworks. Three-clause BSD license; the full text and
   the licenses of the projects below are at the end of this file.

   Usage as a library: in exactly one C file write
      #define FLAPUS_IMPLEMENTATION
      #include "flapus.h"
   Other files include the header without the define. On Linux and macOS, link with
   -pthread -lm.

   Example (error checks left out):
      fl_options opt;
      fl_audio a;
      fl_options_init(&opt);
      opt.bitrate = 96000;
      fl_convert_file("in.flac", "out.opus", &opt);
      fl_read_audio("in.opus", NULL, &a);
      fl_write_audio("out.flac", &a, NULL);
      fl_audio_free(&a);
   fl_convert_file() converts any input format to the format of the output extension,
   fl_read_audio() reads any input into memory, and fl_write_audio() writes it out.
   On error, the functions that return int give a negative value (an FL_ERR_* code for
   the file functions), and the create functions return NULL and set *error. Options
   outside the ranges below give FL_ERR_BAD_ARG.
   fl_error_text() describes an FL_ERR_* code.
   For single Opus packets see the packet decoder and encoder sections below.

   Threads: there is no shared state between objects, so different conversions,
   decoders and encoders can run in different threads at the same time. One object
   must not be used by two threads at once.

   More documentation: README.md in https://github.com/Sightless-Soundworks/flapus

   Opus: the libopus 1.6.1 encoder itself and a decoder based on libopus 1.6.1. Compile
   with floating point contraction disabled (-ffp-contract=off); never use -ffast-math.
   FLAC: own encoder (smallest files, see README) and a complete RFC 9639 decoder.

   The Opus part is derived from the Opus reference implementation
   (RFC 6716, libopus, BSD license, Copyright Xiph.Org, Skype, Octasic,
   Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding,
   Erik de Castro Lopo, Mozilla, Amazon), opus-tools, opusfile and libopusenc (BSD,
   Xiph.Org Foundation and contributors) and the Speex resampler (BSD, Jean-Marc Valin,
   Thorvald Natvig). The math functions in src/math.c are from fdlibm (Sun Microsystems).
*/
#ifndef FLAPUS_H
#define FLAPUS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLDEF
#define FLDEF
#endif

/* Version, set only here in src/api.h. FLAPUS_VERSION is "MAJOR.MINOR", plus ".PATCH"
   if PATCH is not 0. */
#define FLAPUS_VERSION_MAJOR 1
#define FLAPUS_VERSION_MINOR 1
#define FLAPUS_VERSION_PATCH 0

#define FLAPUS_STR_(x) #x
#define FLAPUS_STR(x) FLAPUS_STR_(x)
#if FLAPUS_VERSION_PATCH
#define FLAPUS_VERSION FLAPUS_STR(FLAPUS_VERSION_MAJOR) "." FLAPUS_STR(FLAPUS_VERSION_MINOR) "." FLAPUS_STR(FLAPUS_VERSION_PATCH)
#else
#define FLAPUS_VERSION FLAPUS_STR(FLAPUS_VERSION_MAJOR) "." FLAPUS_STR(FLAPUS_VERSION_MINOR)
#endif

/* Error codes */
#define FL_OK 0
#define FL_ERR_IO -1
#define FL_ERR_BAD_FILE -2
#define FL_ERR_UNSUPPORTED -3
#define FL_ERR_ALLOC -4
#define FL_ERR_BAD_ARG -5
#define FL_ERR_CHECKSUM -6  /* decoded FLAC audio does not match the MD5 in its header */
#define FL_ERR_INTERNAL -7  /* encoder self-check failed (please report) */

/* Short English description of FL_OK or an FL_ERR_* code ("unknown error" for other values). */
FLDEF const char *fl_error_text(int err);

/* ---- File conversion: WAV, FLAC, Ogg FLAC (input only), Ogg Opus ---- */
#define FL_FORMAT_AUTO 0  /* output format from the file extension (.wav, .flac, .opus) */
#define FL_FORMAT_WAV 1
#define FL_FORMAT_FLAC 2
#define FL_FORMAT_OPUS 3

typedef struct {
   int format;         /* output format, FL_FORMAT_* */
   /* Opus input */
   int rate;           /* output rate, 0 = original input rate */
   /* Sample format of WAV and FLAC output (and fl_read_audio), from any input */
   int bits;           /* 16, 24 or 32; 0 (default) = 16 for Opus input, else the input's */
   int dither;         /* reducing to 16 bits: 1 = shaped dither (default), 0 = plain rounding */
   int as_float;       /* 1 = 32-bit float samples (WAV output), Opus as decoded without clipping */
   /* FLAC output */
   int flac_level;     /* 0 (fastest) .. 8 (smallest); default 5 */
   int flac_lax;       /* 1 = allow streams outside the FLAC subset (LPC order 32 etc.) */
   int threads;        /* FLAC encoder threads: 1 (default), 0 = one per CPU; the output does not depend on it */
   int flac_seektable; /* 1 = a seek point every 10 s (default), 0 = none */
   /* Opus output */
   int bitrate;        /* bits per second (0 = automatic: 64 kbps per stream + 32 kbps per coupled pair) */
   int complexity;     /* 0..10, default 10 */
   int frame_ms_x10;   /* frame size in 0.1 ms units: 25, 50, 100, 200 (default), 400, 600, 800, 1000, 1200 */
   int vbr;            /* 1 = VBR (default), 0 = hard CBR */
   int cvbr;           /* 1 = constrained VBR */
   double quality;     /* 0..10 relative to the automatic bitrate (-1 = unset, needs bitrate 0) */
   int signal;         /* 0 = auto, 1 = music, 2 = speech */
   int expect_loss;    /* expected packet loss in percent, 0..100 */
   int no_phase_inv;   /* 1 = disable intensity stereo phase inversion */
   int downmix;        /* 0 = auto (surround is downmixed below 16 kbps per channel), 1 = mono, 2 = stereo */
   const char *comment_options; /* value of the ENCODER_OPTIONS tag (NULL = none) */
   int padding;        /* free bytes after the tags for later editing (Opus: after OpusTags, FLAC: a
                          PADDING block), 0 (default) = none */
   long long serialno; /* Ogg serial number of Opus output, 0..4294967295; -1 (default) = random */
   int keep_time;      /* fl_convert_file: 1 = the output gets the input's modification time, and
                          from FLAC to FLAC or Opus to Opus also its creation time; 0 (default) = not */
   /* Progress during fl_convert_file (NULL = none). done: 0..1 (-1 if the length is unknown),
      ratio: output size / input size so far, as flac counts it: the PCM size for WAV input,
      the file size for FLAC and Opus input (-1 for WAV output or an unknown size). Called
      with done = 1 when the conversion has finished. */
   void (*progress)(void *user, double done, double ratio);
   void *progress_user;
} fl_options;

/* Fills options with the defaults (automatic Opus settings, FLAC level 5, one thread). */
FLDEF void fl_options_init(fl_options *opt);

/* Converts between any of the formats. The input format is detected from the content.
   Opus input is decoded to 16-bit at the original rate unless the Opus input options
   below say otherwise; FLAC output is lossless.
   opt may be NULL.
   Paths: on Windows a path that is valid UTF-8 is opened with _wfopen, so file names
   outside the ANSI code page work. Any other path goes to fopen unchanged. The path "-"
   is stdin for input and stdout for output (set opt->format). stdout cannot seek back,
   so a WAV header gets the length from the source (the maximum if unknown) and the FLAC
   STREAMINFO has no MD5 and frame sizes. The same applies to every function below that
   takes a path.
   Output goes to a temporary file in the same folder that replaces out_path only on
   success, so an existing file survives a failed conversion and in_path may be out_path. */
FLDEF int fl_convert_file(const char *in_path, const char *out_path, const fl_options *opt);

/* Decoded audio in memory: interleaved samples in WAV/FLAC channel order, right-justified
   to 'bits' bits (float bit patterns if is_float). */
typedef struct {
   int channels, rate, bits, is_float;
   long long frames;
   int *samples;
} fl_audio;

/* Reads any input file into memory (opt: rate, bits, dither and as_float, may be NULL). */
FLDEF int fl_read_audio(const char *path, const fl_options *opt, fl_audio *audio);
/* Writes audio to a WAV, FLAC or Opus file (format from opt->format or the extension). */
FLDEF int fl_write_audio(const char *path, const fl_audio *audio, const fl_options *opt);
FLDEF void fl_audio_free(fl_audio *audio);

/* ---- Custom input and output ----
   Instead of a path, every function above and below can take an fl_io. read returns the
   bytes read (0 at the end), write the bytes written (fewer means an error), and seek
   moves to an absolute offset and returns 0, or -1 on failure. seek may be NULL:
   - input that cannot seek works for every format, but Ogg input is then held in memory
     and fl_reader_seek() can only move forward;
   - output that cannot seek is written like stdout (see fl_convert_file).
   The WAV writer also reads back output when data above 4 GB arrives that was not known
   in advance; without read it then stops with FL_ERR_IO. Output functions need
   opt->format. */
typedef struct {
   void *user;
   size_t (*read)(void *user, void *buf, size_t n);
   size_t (*write)(void *user, const void *buf, size_t n);
   int (*seek)(void *user, long long offset);
} fl_io;

FLDEF int fl_convert_io(const fl_io *in, const fl_io *out, const fl_options *opt);
FLDEF int fl_read_audio_io(const fl_io *in, const fl_options *opt, fl_audio *audio);
FLDEF int fl_write_audio_io(const fl_io *out, const fl_audio *audio, const fl_options *opt);

/* ---- Step-by-step decoding of any input (WAV, FLAC, Ogg FLAC, Ogg Opus) ---- */
typedef struct fl_reader fl_reader;

/* Stream format; frames is -1 if unknown and approximate for Opus. */
typedef struct {
   int channels, rate, bits, is_float;
   long long frames;
} fl_info;

/* opt: rate, bits, dither and as_float, may be NULL. The memory variant reads data in
   place, so it must stay valid until fl_reader_close(). */
FLDEF fl_reader *fl_reader_open(const char *path, const fl_options *opt, int *error);
FLDEF fl_reader *fl_reader_open_io(const fl_io *in, const fl_options *opt, int *error);
FLDEF fl_reader *fl_reader_open_memory(const void *data, size_t size, const fl_options *opt, int *error);
FLDEF void fl_reader_info(const fl_reader *r, fl_info *info);
/* Vorbis comments "NAME=value" (FLAC pictures as METADATA_BLOCK_PICTURE). */
FLDEF int fl_reader_tag_count(const fl_reader *r);
FLDEF const char *fl_reader_tag(const fl_reader *r, int index);
/* Reads up to max_frames frames into pcm (interleaved, like fl_audio). Returns the frames
   read, 0 at the end, or an FL_ERR_* code. FL_ERR_CHECKSUM comes at the end of a FLAC
   file whose MD5 differs (only if it was read from the start without seeking). */
FLDEF int fl_reader_read(fl_reader *r, int *pcm, int max_frames);
/* Moves to frame (at the output rate). WAV and FLAC are exact. Opus restarts decoding
   80 ms before the target, as opusfile does, so the samples can differ slightly from a
   decoding from the start. Input that cannot seek can only move forward. */
FLDEF int fl_reader_seek(fl_reader *r, long long frame);
FLDEF void fl_reader_close(fl_reader *r);

/* ---- Step-by-step writing of WAV, FLAC or Opus ----
   info: the format of the samples (frames may be -1, then a FLAC file gets no seek
   table). fl_writer_close() finishes the file and returns FL_OK or the first error. For
   files the output is renamed into place on success, as in fl_convert_file(). */
typedef struct fl_writer fl_writer;
FLDEF fl_writer *fl_writer_open(const char *path, const fl_info *info, const fl_options *opt, int *error);
FLDEF fl_writer *fl_writer_open_io(const fl_io *out, const fl_info *info, const fl_options *opt, int *error);
FLDEF int fl_writer_write(fl_writer *w, const int *pcm, int frames);
FLDEF int fl_writer_close(fl_writer *w);

/* ---- FLAC frames ----
   The frame encoder codes each block as one frame with the full subframe search of
   opt->flac_level (the block sizes are the caller's). Frames carry sample numbers, so the
   sizes may vary. fl_flac_frame_encoder_streaminfo() gives the 34-byte STREAMINFO
   contents for the frames so far (block and frame sizes, total and MD5). */
typedef struct fl_flac_frame_encoder fl_flac_frame_encoder;
FLDEF fl_flac_frame_encoder *fl_flac_frame_encoder_create(int channels, int rate, int bits, const fl_options *opt, int *error);
/* frames: 16..65535 (1 is allowed for the last frame). Returns the frame length in bytes,
   or an FL_ERR_* code (FL_ERR_BAD_ARG if max_bytes is too small: frames*channels*4+64
   is always enough). */
FLDEF int fl_flac_frame_encode(fl_flac_frame_encoder *enc, const int *pcm, int frames, unsigned char *out, int max_bytes);
FLDEF void fl_flac_frame_encoder_streaminfo(const fl_flac_frame_encoder *enc, unsigned char info[34]);
FLDEF void fl_flac_frame_encoder_destroy(fl_flac_frame_encoder *enc);

/* The frame decoder takes the STREAMINFO contents (34 bytes). fl_flac_frame_decode()
   decodes the frame at the start of data into pcm (interleaved; max_frames holds the
   largest block) and returns its sample count, with the frame length in *used, or
   FL_ERR_BAD_FILE. */
typedef struct fl_flac_frame_decoder fl_flac_frame_decoder;
FLDEF fl_flac_frame_decoder *fl_flac_frame_decoder_create(const unsigned char info[34], int *error);
FLDEF int fl_flac_frame_decode(fl_flac_frame_decoder *dec, const unsigned char *data, int len, int *pcm, int max_frames, int *used);
FLDEF void fl_flac_frame_decoder_destroy(fl_flac_frame_decoder *dec);

/* ---- Opus packet decoder ---- */
typedef struct fl_opus_decoder fl_opus_decoder;

/* Creates a decoder for 8000/12000/16000/24000/48000 Hz and 1 or 2 channels. */
FLDEF fl_opus_decoder *fl_opus_decoder_create(int sample_rate, int channels, int *error);
FLDEF void fl_opus_decoder_destroy(fl_opus_decoder *dec);
FLDEF void fl_opus_decoder_reset_state(fl_opus_decoder *dec);
/* Sets the output gain in Q8 dB (as in the OpusHead header). */
FLDEF void fl_opus_decoder_set_gain(fl_opus_decoder *dec, int gain_q8);
/* Decodes one packet (packet=NULL or len=0 conceals a lost packet) into interleaved
   float samples. pcm holds max_frame_size samples per channel: sample_rate/25*3 (120 ms,
   5760 at 48 kHz) fits every packet. For a lost packet, max_frame_size is the length
   to conceal (a multiple of 2.5 ms). Returns samples per channel or a negative error. */
FLDEF int fl_opus_decode_float(fl_opus_decoder *dec, const unsigned char *packet, int len, float *pcm, int max_frame_size);

/* ---- Opus multistream packet decoder (Ogg Opus channel mapping families 1, 2, 255) ---- */
typedef struct fl_opus_ms_decoder fl_opus_ms_decoder;

/* streams/coupled/mapping are the OpusHead fields; mapping has one entry per channel
   (255 = silent channel). Family 0 is streams=1, coupled=channels-1, mapping {0,1}. */
FLDEF fl_opus_ms_decoder *fl_opus_ms_decoder_create(int sample_rate, int channels, int streams, int coupled,
                                          const unsigned char *mapping, int *error);
FLDEF void fl_opus_ms_decoder_destroy(fl_opus_ms_decoder *dec);
FLDEF void fl_opus_ms_decoder_reset_state(fl_opus_ms_decoder *dec);
FLDEF void fl_opus_ms_decoder_set_gain(fl_opus_ms_decoder *dec, int gain_q8);
/* Same contract as fl_opus_decode_float, output interleaved over all channels. */
FLDEF int fl_opus_ms_decode_float(fl_opus_ms_decoder *dec, const unsigned char *packet, int len, float *pcm, int max_frame_size);

/* ---- Opus encoder ----
   libopus 1.6.1 (float) in plain C99, for every compiler and CPU. Define
   FLAPUS_NO_ENCODER to leave it out. The output is the same on every platform when
   float and double arithmetic has no excess precision (FLT_EVAL_METHOD 0; on 32-bit x86
   build with -msse2 -mfpmath=sse) and no contraction (see above). */
#ifndef FLAPUS_NO_ENCODER
#define FLAPUS_HAVE_ENCODER 1
#endif

#ifdef FLAPUS_HAVE_ENCODER

/* Packet encoder: 48 kHz interleaved float input, one Opus (multistream) packet per call.
   Channel layout: family 0 (1-2 ch), 1 (3-8 ch, Vorbis order), 255 (9-255 ch).
   opt may be NULL; opt->downmix is ignored here. */
typedef struct fl_opus_encoder fl_opus_encoder;
FLDEF fl_opus_encoder *fl_opus_encoder_create(int channels, const fl_options *opt, int *error);
FLDEF void fl_opus_encoder_destroy(fl_opus_encoder *enc);
/* Samples per channel that every fl_opus_encode_float() call consumes. */
FLDEF int fl_opus_encoder_frame_size(const fl_opus_encoder *enc);
/* Encoder delay in samples (OpusHead pre-skip). */
FLDEF int fl_opus_encoder_lookahead(const fl_opus_encoder *enc);
/* OpusHead mapping fields; mapping receives one byte per channel. Returns the channel count. */
FLDEF int fl_opus_encoder_layout(const fl_opus_encoder *enc, int *family, int *streams, int *coupled, unsigned char *mapping);
/* Encodes fl_opus_encoder_frame_size() samples per channel. max_bytes: up to 7664 per stream is
   always enough. Returns the packet length or a negative error. For an Ogg Opus file,
   OpusHead takes the pre-skip from fl_opus_encoder_lookahead() and the channel fields
   from fl_opus_encoder_layout(); fl_convert_file() writes complete files. */
FLDEF int fl_opus_encode_float(fl_opus_encoder *enc, const float *pcm, unsigned char *packet, int max_bytes);
#endif /* FLAPUS_HAVE_ENCODER */

#ifdef __cplusplus
}
#endif
#endif /* FLAPUS_H */

#ifdef FLAPUS_IMPLEMENTATION
#ifndef FLAPUS_IMPLEMENTATION_DONE
#define FLAPUS_IMPLEMENTATION_DONE
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("fp-contract=off")
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
