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
   the file functions), and the create functions return NULL and set *error.
   fl_error_text() describes an FL_ERR_* code.
   For single Opus packets see the packet decoder and encoder sections below.

   Threads: there is no shared state between objects, so different conversions,
   decoders and encoders can run in different threads at the same time. One object
   must not be used by two threads at once. The default Ogg serial number of Opus
   output comes from rand(); set opt->serialno if the C library's rand() is not
   thread-safe.

   More documentation: README.md in https://github.com/Sightless-Soundworks/flapus

   Opus: encoder and decoder based on libopus 1.6.1, made for the best possible encoding
   and decoding. Compile with floating point contraction disabled (-ffp-contract=off);
   never use -ffast-math.
   FLAC: own encoder (smallest files, see README) and a complete RFC 9639 decoder.

   The Opus part is derived from the Opus reference implementation
   (RFC 6716, libopus, BSD license, Copyright Xiph.Org, Skype, Octasic,
   Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding,
   Erik de Castro Lopo, Mozilla, Amazon), opus-tools, opusfile and libopusenc (BSD,
   Xiph.Org Foundation and contributors) and the Speex resampler (BSD, Jean-Marc Valin,
   Thorvald Natvig).
*/
#ifndef FLAPUS_H
#define FLAPUS_H

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
   int dither;         /* 1 = shaped dither (default), 0 = plain rounding */
   /* FLAC output */
   int flac_level;     /* 0 (fastest) .. 8 (smallest); default 5 */
   int flac_lax;       /* 1 = allow streams outside the FLAC subset (LPC order 32 etc.) */
   int threads;        /* FLAC encoder threads: 1 (default), 0 = one per CPU; the output does not depend on it */
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
   int padding;        /* free bytes reserved after the Opus tags: 512 (default), 0 = none */
   long long serialno; /* Ogg serial number of Opus output, 0..4294967295; -1 (default) = from rand() */
   /* Progress during fl_convert_file (NULL = none). done: 0..1 (-1 if the length is unknown),
      ratio: output size / input PCM size so far (-1 for WAV output). Called with done = 1
      when the conversion has finished. */
   void (*progress)(void *user, double done, double ratio);
   void *progress_user;
} fl_options;

/* Fills options with the defaults (automatic Opus settings, FLAC level 5, one thread). */
FLDEF void fl_options_init(fl_options *opt);

/* Converts between any of the formats. The input format is detected from the content.
   Opus input is decoded to 16-bit at the original rate, FLAC output is lossless.
   opt may be NULL.
   Paths: on Windows a path that is valid UTF-8 is opened with _wfopen, so file names
   outside the ANSI code page work. Any other path goes to fopen unchanged. The path "-"
   is stdin for input and stdout for output (set opt->format). stdout cannot seek back,
   so a WAV header gets the length from the source (the maximum if unknown) and the FLAC
   STREAMINFO has no MD5 and frame sizes. The same applies to every function below that
   takes a path. */
FLDEF int fl_convert_file(const char *in_path, const char *out_path, const fl_options *opt);

/* Decoded audio in memory: interleaved samples in WAV/FLAC channel order, right-justified
   to 'bits' bits (float bit patterns if is_float). */
typedef struct {
   int channels, rate, bits, is_float;
   long long frames;
   int *samples;
} fl_audio;

/* Reads any input file into memory (opt: Opus decoding options, may be NULL). */
FLDEF int fl_read_audio(const char *path, const fl_options *opt, fl_audio *audio);
/* Writes audio to a WAV, FLAC or Opus file (format from opt->format or the extension). */
FLDEF int fl_write_audio(const char *path, const fl_audio *audio, const fl_options *opt);
FLDEF void fl_audio_free(fl_audio *audio);

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
   The Opus encoder needs GCC or Clang on x86/x86-64 (80-bit long double and
   x87 inline assembly). Elsewhere, or with FLAPUS_NO_ENCODER, it is left out (FLAC and
   all decoders are portable C99).
   On 32-bit x86, float and double arithmetic must use SSE2 (FLT_EVAL_METHOD 0, the
   default of w64devkit 2.11 and later). With x87 arithmetic (-mfpmath=387, the default
   of older 32-bit toolchains) the encoder rounds differently than designed and some
   files encode differently, so the implementation stops with an error instead. Build
   with -msse2 -mfpmath=sse, or define FLAPUS_NO_ENCODER.
   The default encoder uses rcpps/rsqrtps and the x87 transcendental instructions,
   whose results differ between CPU makers, so its output differs between Intel and
   AMD. Define FLAPUS_PORTABLE_MATH for output that is the same on every x86 CPU:
   Intel's rcpps/rsqrtps from tables, and software replacements for the x87
   instructions (at most 2.4 ulp of the 64-bit mantissa); see README. */
#if !defined(FLAPUS_NO_ENCODER) && defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
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
#if defined(FLAPUS_HAVE_ENCODER) && defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ != 0
#error "flapus: the Opus encoder needs SSE2 float arithmetic. Compile with -msse2 -mfpmath=sse, or define FLAPUS_NO_ENCODER."
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("fp-contract=off")
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
