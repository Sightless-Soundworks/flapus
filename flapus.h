/*
   flapus.h - single-file Opus and FLAC encoder/decoder (Ogg Opus, FLAC, Ogg FLAC, WAV),
   no dependencies besides the C library.

   Usage as a library: in exactly one C file write
      #define FLAPUS_IMPLEMENTATION
      #include "flapus.h"
   Other files include the header without the define.

   Opus: bit-exact with libopus 1.6.1 (decoder: x86-64 SSE/AVX2 code paths of opusdec,
   encoder: 32-bit x87 build of opusenc). Compile with floating point contraction
   disabled (-ffp-contract=off); never use -ffast-math.
   FLAC: own encoder (smallest files, see README) and a complete RFC 9639 decoder.

   The Opus DSP algorithms are derived from the Opus reference implementation
   (RFC 6716, libopus, BSD license, Copyright Xiph.Org, Skype, Octasic,
   Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding,
   Erik de Castro Lopo) and the Speex resampler (BSD, Jean-Marc Valin).
*/
#ifndef FLAPUS_H
#define FLAPUS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLDEF
#define FLDEF
#endif

#define FLAPUS_VERSION_MAJOR 0
#define FLAPUS_VERSION_MINOR 1
#define FLAPUS_VERSION_PATCH 0
#define FLAPUS_VERSION "0.1.0"

/* Error codes */
#define FL_OK 0
#define FL_ERR_IO -1
#define FL_ERR_BAD_FILE -2
#define FL_ERR_UNSUPPORTED -3
#define FL_ERR_ALLOC -4
#define FL_ERR_BAD_ARG -5
#define FL_ERR_CHECKSUM -6  /* decoded FLAC audio does not match the MD5 in its header */
#define FL_ERR_INTERNAL -7  /* encoder self-check failed (please report) */

/* ---- File conversion: WAV, FLAC, Ogg FLAC (input only), Ogg Opus ---- */
#define FL_FORMAT_AUTO 0  /* output format from the file extension (.wav, .flac, .opus) */
#define FL_FORMAT_WAV 1
#define FL_FORMAT_FLAC 2
#define FL_FORMAT_OPUS 3

typedef struct {
   int format;         /* output format, FL_FORMAT_* */
   /* Opus input (decoded like opusdec) */
   int rate;           /* output rate, 0 = original input rate */
   int dither;         /* 1 = shaped dither (default), 0 = plain rounding */
   /* FLAC output */
   int flac_level;     /* 0 (fastest) .. 8 (smallest); default 5 */
   int flac_lax;       /* 1 = allow streams outside the FLAC subset (LPC order 32 etc.) */
   int threads;        /* FLAC encoder threads: 1 (default), 0 = one per CPU; the output does not depend on it */
   /* Opus output (opusenc options) */
   int bitrate;        /* bits per second (0 = opusenc default: 64 kbps per stream + 32 kbps per coupled pair) */
   int complexity;     /* 0..10, default 10 */
   int frame_ms_x10;   /* frame size in 0.1 ms units: 25, 50, 100, 200 (default), 400, 600, 800, 1000, 1200 */
   int vbr;            /* 1 = VBR (default), 0 = hard CBR */
   int cvbr;           /* 1 = constrained VBR */
   double quality;     /* 0..10 relative to the automatic bitrate (-1 = unset, needs bitrate 0) */
   int signal;         /* 0 = auto, 1 = music, 2 = speech */
   int expect_loss;    /* expected packet loss in percent, 0..100 */
   int no_phase_inv;   /* 1 = disable intensity stereo phase inversion */
   int downmix;        /* 0 = auto (opusenc rule), 1 = mono, 2 = stereo */
   const char *comment_options; /* value of the ENCODER_OPTIONS tag (NULL = none) */
   int padding;        /* free bytes reserved after the Opus tags: 512 (default, as opusenc), 0 = none */
   long long serialno; /* Ogg serial number of Opus output, 0..4294967295; -1 (default) = from rand() */
   /* Progress during fl_convert_file (NULL = none). done: 0..1 (-1 if the length is unknown),
      ratio: output size / input PCM size so far (-1 for WAV output). Called with done = 1
      when the conversion has finished. */
   void (*progress)(void *user, double done, double ratio);
   void *progress_user;
} fl_options;

/* Fills options with the defaults (opusdec/opusenc defaults, FLAC level 5, one thread). */
FLDEF void fl_options_init(fl_options *opt);

/* Converts between any of the formats. The input format is detected from the content.
   Opus output is bit-exact with opusenc for the same samples, Opus input is decoded
   like opusdec (16-bit, original rate), FLAC output is lossless. opt may be NULL.
   Paths: on Windows a path that is valid UTF-8 is opened with _wfopen, so file names
   outside the ANSI code page work. Any other path goes to fopen unchanged. The same
   applies to every function below that takes a path. */
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
   float samples. Returns samples per channel or a negative error. */
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
   The bit-exact encoder needs GCC or Clang on x86/x86-64 (80-bit long double and
   x87 inline assembly). Elsewhere, or with FLAPUS_NO_ENCODER, it is left out (FLAC and
   all decoders are portable C99).
   On 32-bit x86, float and double arithmetic must use SSE2 (FLT_EVAL_METHOD 0, the
   default of w64devkit 2.11 and later). With x87 arithmetic (-mfpmath=387, the default
   of older 32-bit toolchains) some files encode differently from opusenc, so the
   implementation stops with an error instead. Build with -msse2 -mfpmath=sse, or
   define FLAPUS_NO_ENCODER.
   Like opusenc, the default encoder uses rcpps/rsqrtps and the x87 transcendental
   instructions, whose results differ between CPU makers, so its output differs between
   Intel and AMD. Define FLAPUS_PORTABLE_MATH for output that is the same on every x86
   CPU: Intel's rcpps/rsqrtps from tables, and software replacements for the x87
   instructions (at most 2.4 ulp of the 64-bit mantissa); see README. */
#if !defined(FLAPUS_NO_ENCODER) && defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#define FLAPUS_HAVE_ENCODER 1
#endif

#ifdef FLAPUS_HAVE_ENCODER

/* Packet encoder: 48 kHz interleaved float input, one Opus (multistream) packet per call.
   Matches libopus opus_multistream_encode_float() of the same 32-bit x87 build.
   Channel layout as opusenc: family 0 (1-2 ch), 1 (3-8 ch, Vorbis order), 255 (9-255 ch).
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
   always enough. Returns the packet length or a negative error. */
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
#error "flapus: the Opus encoder is only bit-exact with SSE2 float arithmetic. Compile with -msse2 -mfpmath=sse, or define FLAPUS_NO_ENCODER."
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("fp-contract=off")
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

/* ======== common.c ======== */
/* ---- Common types, helpers and the range coder ---- */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

typedef signed char fl_i8;
typedef unsigned char fl_u8;
typedef short fl_i16;
typedef unsigned short fl_u16;
typedef int fl_i32;
typedef unsigned int fl_u32;
typedef long long fl_i64;
typedef unsigned long long fl_u64;
typedef struct { float r, i; } fl_cpx;

#define FL_MIN(a,b) ((a) < (b) ? (a) : (b))
#define FL_MAX(a,b) ((a) > (b) ? (a) : (b))
#define IMIN(a,b) ((a) < (b) ? (a) : (b))
#define IMAX(a,b) ((a) > (b) ? (a) : (b))
#define FL_FRAC_MUL16(a,b) ((16384+((fl_i32)(fl_i16)(a)*(fl_i16)(b)))>>15)
#define FL_CLEAR(p,n) memset((p), 0, (n)*sizeof(*(p)))
#define FL_COPY(d,s,n) memmove((d), (s), (n)*sizeof(*(d)))

/* UTF-8 paths on Windows. A path that is valid UTF-8 is opened with the wide API, so
   names outside the ANSI code page work. Anything else (a path in the ANSI code page,
   as argv gives it) goes to fopen/remove unchanged, as before. */
#ifdef _WIN32
/* The path as UTF-16 (free it), or NULL if it is not valid UTF-8. */
static wchar_t *fl_widen(const char *path) {
   int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
   wchar_t *w;
   if (n <= 0 || !(w = (wchar_t*)malloc(sizeof(wchar_t)*(size_t)n))) return NULL;
   MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
   return w;
}
#endif

static FILE *fl_fopen(const char *path, const char *mode) {
#ifdef _WIN32
   wchar_t *w = fl_widen(path);
   if (w) {
      wchar_t wmode[8];
      FILE *f;
      int i;
      for (i = 0; mode[i] && i < 7; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
      wmode[i] = 0;
      f = _wfopen(w, wmode);
      free(w);
      return f;
   }
#endif
   return fopen(path, mode);
}

static int fl_remove(const char *path) {
#ifdef _WIN32
   wchar_t *w = fl_widen(path);
   if (w) {
      int r = _wremove(w);
      free(w);
      return r;
   }
#endif
   return remove(path);
}

/* Number of bits needed to represent x (0 for x==0). */
static int fl_ilog(fl_u32 x) {
   int r = 0;
   while (x) { r++; x >>= 1; }
   return r;
}
#if defined(__GNUC__)
#define EC_ILOG(x) (32 - __builtin_clz(x))
#else
#define EC_ILOG(x) fl_ilog(x)
#endif

/* Float to int with round-to-nearest-even (matches lrintf). */
static int fl_float2int(float x) { return (int)lrintf(x); }

/* ---- Range coder (RFC 6716 section 4.1 / 5.1) ---- */
#define EC_SYM_BITS 8
#define EC_CODE_BITS 32
#define EC_SYM_MAX ((1U<<EC_SYM_BITS)-1)
#define EC_CODE_SHIFT (EC_CODE_BITS-EC_SYM_BITS-1)
#define EC_CODE_TOP (1U<<(EC_CODE_BITS-1))
#define EC_CODE_BOT (EC_CODE_TOP>>EC_SYM_BITS)
#define EC_CODE_EXTRA ((EC_CODE_BITS-2)%EC_SYM_BITS+1)
#define EC_WINDOW_SIZE 32
#define EC_UINT_BITS 8
#define BITRES 3

typedef struct {
   unsigned char *buf;
   fl_u32 storage;
   fl_u32 end_offs;
   fl_u32 end_window;
   int nend_bits;
   int nbits_total;
   fl_u32 offs;
   fl_u32 rng;
   fl_u32 val;
   fl_u32 ext;
   int rem;
   int error;
} ec_ctx;

static int ec_tell(ec_ctx *e) { return e->nbits_total - EC_ILOG(e->rng); }

static fl_u32 ec_tell_frac(ec_ctx *e) {
   static const unsigned correction[8] = {35733, 38967, 42495, 46340, 50535, 55109, 60097, 65535};
   fl_u32 nbits, r;
   int l;
   unsigned b;
   nbits = (fl_u32)e->nbits_total << BITRES;
   l = EC_ILOG(e->rng);
   r = e->rng >> (l-16);
   b = (r>>12)-8;
   b += r > correction[b];
   l = (l<<3)+b;
   return nbits-l;
}

/* Decoder */
static int ec_read_byte(ec_ctx *d) { return d->offs < d->storage ? d->buf[d->offs++] : 0; }
static int ec_read_byte_from_end(ec_ctx *d) {
   return d->end_offs < d->storage ? d->buf[d->storage - ++(d->end_offs)] : 0;
}
static void ec_dec_normalize(ec_ctx *d) {
   while (d->rng <= EC_CODE_BOT) {
      int sym;
      d->nbits_total += EC_SYM_BITS;
      d->rng <<= EC_SYM_BITS;
      sym = d->rem;
      d->rem = ec_read_byte(d);
      sym = (sym<<EC_SYM_BITS|d->rem)>>(EC_SYM_BITS-EC_CODE_EXTRA);
      d->val = ((d->val<<EC_SYM_BITS)+(EC_SYM_MAX&~sym))&(EC_CODE_TOP-1);
   }
}
static void ec_dec_init(ec_ctx *d, unsigned char *buf, fl_u32 storage) {
   d->buf = buf;
   d->storage = storage;
   d->end_offs = 0;
   d->end_window = 0;
   d->nend_bits = 0;
   d->nbits_total = EC_CODE_BITS+1-((EC_CODE_BITS-EC_CODE_EXTRA)/EC_SYM_BITS)*EC_SYM_BITS;
   d->offs = 0;
   d->rng = 1U<<EC_CODE_EXTRA;
   d->rem = ec_read_byte(d);
   d->val = d->rng-1-(d->rem>>(EC_SYM_BITS-EC_CODE_EXTRA));
   d->error = 0;
   d->ext = 0;
   ec_dec_normalize(d);
}
static unsigned ec_decode(ec_ctx *d, unsigned ft) {
   unsigned s;
   d->ext = d->rng/ft;
   s = (unsigned)(d->val/d->ext);
   return ft-FL_MIN(s+1, ft);
}
static unsigned ec_decode_bin(ec_ctx *d, unsigned bits) {
   unsigned s;
   d->ext = d->rng>>bits;
   s = (unsigned)(d->val/d->ext);
   return (1U<<bits)-FL_MIN(s+1U, 1U<<bits);
}
static void ec_dec_update(ec_ctx *d, unsigned fl, unsigned fh, unsigned ft) {
   fl_u32 s = d->ext*(ft-fh);
   d->val -= s;
   d->rng = fl > 0 ? d->ext*(fh-fl) : d->rng-s;
   ec_dec_normalize(d);
}
static int ec_dec_bit_logp(ec_ctx *d, unsigned logp) {
   fl_u32 r = d->rng, v = d->val, s = r>>logp;
   int ret = v < s;
   if (!ret) d->val = v-s;
   d->rng = ret ? s : r-s;
   ec_dec_normalize(d);
   return ret;
}
static int ec_dec_icdf(ec_ctx *d, const unsigned char *icdf, unsigned ftb) {
   fl_u32 s = d->rng, v = d->val, r = s>>ftb, t;
   int ret = -1;
   do {
      t = s;
      s = r*icdf[++ret];
   } while (v < s);
   d->val = v-s;
   d->rng = t-s;
   ec_dec_normalize(d);
   return ret;
}
static fl_u32 ec_dec_bits(ec_ctx *d, unsigned bits) {
   fl_u32 window = d->end_window, ret;
   int available = d->nend_bits;
   if ((unsigned)available < bits) {
      do {
         window |= (fl_u32)ec_read_byte_from_end(d)<<available;
         available += EC_SYM_BITS;
      } while (available <= EC_WINDOW_SIZE-EC_SYM_BITS);
   }
   ret = window&(((fl_u32)1<<bits)-1U);
   window >>= bits;
   available -= bits;
   d->end_window = window;
   d->nend_bits = available;
   d->nbits_total += bits;
   return ret;
}
static fl_u32 ec_dec_uint(ec_ctx *d, fl_u32 ft) {
   unsigned ft1, s;
   int ftb;
   ft--;
   ftb = EC_ILOG(ft);
   if (ftb > EC_UINT_BITS) {
      fl_u32 t;
      ftb -= EC_UINT_BITS;
      ft1 = (unsigned)(ft>>ftb)+1;
      s = ec_decode(d, ft1);
      ec_dec_update(d, s, s+1, ft1);
      t = (fl_u32)s<<ftb|ec_dec_bits(d, ftb);
      if (t <= ft) return t;
      d->error = 1;
      return ft;
   } else {
      ft++;
      s = ec_decode(d, (unsigned)ft);
      ec_dec_update(d, s, s+1, (unsigned)ft);
      return s;
   }
}

/* Encoder */
static int ec_write_byte(ec_ctx *e, unsigned v) {
   if (e->offs+e->end_offs >= e->storage) return -1;
   e->buf[e->offs++] = (unsigned char)v;
   return 0;
}
static int ec_write_byte_at_end(ec_ctx *e, unsigned v) {
   if (e->offs+e->end_offs >= e->storage) return -1;
   e->buf[e->storage - ++(e->end_offs)] = (unsigned char)v;
   return 0;
}
static void ec_enc_carry_out(ec_ctx *e, int c) {
   if (c != EC_SYM_MAX) {
      int carry = c>>EC_SYM_BITS;
      if (e->rem >= 0) e->error |= ec_write_byte(e, e->rem+carry);
      if (e->ext > 0) {
         unsigned sym = (EC_SYM_MAX+carry)&EC_SYM_MAX;
         do e->error |= ec_write_byte(e, sym);
         while (--(e->ext) > 0);
      }
      e->rem = c&EC_SYM_MAX;
   } else e->ext++;
}
static void ec_enc_normalize(ec_ctx *e) {
   while (e->rng <= EC_CODE_BOT) {
      ec_enc_carry_out(e, (int)(e->val>>EC_CODE_SHIFT));
      e->val = (e->val<<EC_SYM_BITS)&(EC_CODE_TOP-1);
      e->rng <<= EC_SYM_BITS;
      e->nbits_total += EC_SYM_BITS;
   }
}
static void ec_enc_init(ec_ctx *e, unsigned char *buf, fl_u32 size) {
   e->buf = buf;
   e->end_offs = 0;
   e->end_window = 0;
   e->nend_bits = 0;
   e->nbits_total = EC_CODE_BITS+1;
   e->offs = 0;
   e->rng = EC_CODE_TOP;
   e->rem = -1;
   e->val = 0;
   e->ext = 0;
   e->storage = size;
   e->error = 0;
}
static void ec_encode(ec_ctx *e, unsigned fl, unsigned fh, unsigned ft) {
   fl_u32 r = e->rng/ft;
   if (fl > 0) {
      e->val += e->rng-r*(ft-fl);
      e->rng = r*(fh-fl);
   } else e->rng -= r*(ft-fh);
   ec_enc_normalize(e);
}
static void ec_encode_bin(ec_ctx *e, unsigned fl, unsigned fh, unsigned bits) {
   fl_u32 r = e->rng>>bits;
   if (fl > 0) {
      e->val += e->rng-r*((1U<<bits)-fl);
      e->rng = r*(fh-fl);
   } else e->rng -= r*((1U<<bits)-fh);
   ec_enc_normalize(e);
}
static void ec_enc_bit_logp(ec_ctx *e, int val, unsigned logp) {
   fl_u32 r = e->rng, l = e->val, s = r>>logp;
   r -= s;
   if (val) e->val = l+r;
   e->rng = val ? s : r;
   ec_enc_normalize(e);
}
static void ec_enc_icdf(ec_ctx *e, int s, const unsigned char *icdf, unsigned ftb) {
   fl_u32 r = e->rng>>ftb;
   if (s > 0) {
      e->val += e->rng-r*icdf[s-1];
      e->rng = r*(icdf[s-1]-icdf[s]);
   } else e->rng -= r*icdf[s];
   ec_enc_normalize(e);
}
static void ec_enc_bits(ec_ctx *e, fl_u32 fl, unsigned bits) {
   fl_u32 window = e->end_window;
   int used = e->nend_bits;
   if (used+bits > EC_WINDOW_SIZE) {
      do {
         e->error |= ec_write_byte_at_end(e, (unsigned)window&EC_SYM_MAX);
         window >>= EC_SYM_BITS;
         used -= EC_SYM_BITS;
      } while (used >= EC_SYM_BITS);
   }
   window |= fl<<used;
   used += bits;
   e->end_window = window;
   e->nend_bits = used;
   e->nbits_total += bits;
}
static void ec_enc_uint(ec_ctx *e, fl_u32 fl, fl_u32 ft) {
   unsigned ft1, fl1;
   int ftb;
   ft--;
   ftb = EC_ILOG(ft);
   if (ftb > EC_UINT_BITS) {
      ftb -= EC_UINT_BITS;
      ft1 = (ft>>ftb)+1;
      fl1 = (unsigned)(fl>>ftb);
      ec_encode(e, fl1, fl1+1, ft1);
      ec_enc_bits(e, fl&(((fl_u32)1<<ftb)-1U), ftb);
   } else ec_encode(e, fl, fl+1, ft+1);
}
static void ec_enc_patch_initial_bits(ec_ctx *e, unsigned val, unsigned nbits) {
   int shift = EC_SYM_BITS-nbits;
   unsigned mask = ((1<<nbits)-1)<<shift;
   if (e->offs > 0) e->buf[0] = (unsigned char)((e->buf[0]&~mask)|val<<shift);
   else if (e->rem >= 0) e->rem = (e->rem&~mask)|val<<shift;
   else if (e->rng <= (EC_CODE_TOP>>nbits))
      e->val = (e->val&~((fl_u32)mask<<EC_CODE_SHIFT))|(fl_u32)val<<(EC_CODE_SHIFT+shift);
   else e->error = -1;
}
static void ec_enc_shrink(ec_ctx *e, fl_u32 size) {
   memmove(e->buf+size-e->end_offs, e->buf+e->storage-e->end_offs, e->end_offs);
   e->storage = size;
}
static void ec_enc_done(ec_ctx *e) {
   fl_u32 window, msk, end;
   int used, l;
   l = EC_CODE_BITS-EC_ILOG(e->rng);
   msk = (EC_CODE_TOP-1)>>l;
   end = (e->val+msk)&~msk;
   if ((end|msk) >= e->val+e->rng) {
      l++;
      msk >>= 1;
      end = (e->val+msk)&~msk;
   }
   while (l > 0) {
      ec_enc_carry_out(e, (int)(end>>EC_CODE_SHIFT));
      end = (end<<EC_SYM_BITS)&(EC_CODE_TOP-1);
      l -= EC_SYM_BITS;
   }
   if (e->rem >= 0 || e->ext > 0) ec_enc_carry_out(e, 0);
   window = e->end_window;
   used = e->nend_bits;
   while (used >= EC_SYM_BITS) {
      e->error |= ec_write_byte_at_end(e, (unsigned)window&EC_SYM_MAX);
      window >>= EC_SYM_BITS;
      used -= EC_SYM_BITS;
   }
   if (!e->error) {
      if (e->buf) memset(e->buf+e->offs, 0, e->storage-e->offs-e->end_offs);
      if (used > 0) {
         if (e->end_offs >= e->storage) e->error = -1;
         else {
            l = -l;
            if (e->offs+e->end_offs >= e->storage && l < used) {
               window &= (1<<l)-1;
               e->error = -1;
            }
            e->buf[e->storage-e->end_offs-1] |= (unsigned char)window;
         }
      }
   }
}

/* ======== tables_celt.c ======== */
/* CELT constant tables (from the Opus reference). */
static const fl_i16 fl_eband5ms[22] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100
};

static const fl_u8 fl_band_allocation[231] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 90, 80, 75, 69, 63, 56, 49, 40, 34, 29, 20, 18, 10, 0, 0, 0, 0, 0, 0, 0, 0,
110,100, 90, 84, 78, 71, 65, 58, 51, 45, 39, 32, 26, 20, 12, 0, 0, 0, 0, 0, 0,
118,110,103, 93, 86, 80, 75, 70, 65, 59, 53, 47, 40, 31, 23, 15, 4, 0, 0, 0, 0,
126,119,112,104, 95, 89, 83, 78, 72, 66, 60, 54, 47, 39, 32, 25, 17, 12, 1, 0, 0,
134,127,120,114,103, 97, 91, 85, 78, 72, 66, 60, 54, 47, 41, 35, 29, 23, 16, 10, 1,
144,137,130,124,113,107,101, 95, 88, 82, 76, 70, 64, 57, 51, 45, 39, 33, 26, 15, 1,
152,145,138,132,123,117,111,105, 98, 92, 86, 80, 74, 67, 61, 55, 49, 43, 36, 20, 1,
162,155,148,142,133,127,121,115,108,102, 96, 90, 84, 77, 71, 65, 59, 53, 46, 30, 1,
172,165,158,152,143,137,131,125,118,112,106,100, 94, 87, 81, 75, 69, 63, 56, 45, 20,
200,200,200,200,200,200,200,200,198,193,188,183,178,173,168,163,158,153,148,129,104,
};

static const float fl_window120[120] = {
6.7286966e-05f, 0.00060551348f, 0.0016815970f, 0.0032947962f, 0.0054439943f,
0.0081276923f, 0.011344001f, 0.015090633f, 0.019364886f, 0.024163635f,
0.029483315f, 0.035319905f, 0.041668911f, 0.048525347f, 0.055883718f,
0.063737999f, 0.072081616f, 0.080907428f, 0.090207705f, 0.099974111f,
0.11019769f, 0.12086883f, 0.13197729f, 0.14351214f, 0.15546177f,
0.16781389f, 0.18055550f, 0.19367290f, 0.20715171f, 0.22097682f,
0.23513243f, 0.24960208f, 0.26436860f, 0.27941419f, 0.29472040f,
0.31026818f, 0.32603788f, 0.34200931f, 0.35816177f, 0.37447407f,
0.39092462f, 0.40749142f, 0.42415215f, 0.44088423f, 0.45766484f,
0.47447104f, 0.49127978f, 0.50806798f, 0.52481261f, 0.54149077f,
0.55807973f, 0.57455701f, 0.59090049f, 0.60708841f, 0.62309951f,
0.63891306f, 0.65450896f, 0.66986776f, 0.68497077f, 0.69980010f,
0.71433873f, 0.72857055f, 0.74248043f, 0.75605425f, 0.76927895f,
0.78214257f, 0.79463430f, 0.80674445f, 0.81846456f, 0.82978733f,
0.84070669f, 0.85121779f, 0.86131698f, 0.87100183f, 0.88027111f,
0.88912479f, 0.89756398f, 0.90559094f, 0.91320904f, 0.92042270f,
0.92723738f, 0.93365955f, 0.93969656f, 0.94535671f, 0.95064907f,
0.95558353f, 0.96017067f, 0.96442171f, 0.96834849f, 0.97196334f,
0.97527906f, 0.97830883f, 0.98106616f, 0.98356480f, 0.98581869f,
0.98784191f, 0.98964856f, 0.99125274f, 0.99266849f, 0.99390969f,
0.99499004f, 0.99592297f, 0.99672162f, 0.99739874f, 0.99796667f,
0.99843728f, 0.99882195f, 0.99913147f, 0.99937606f, 0.99956527f,
0.99970802f, 0.99981248f, 0.99988613f, 0.99993565f, 0.99996697f,
0.99998518f, 0.99999457f, 0.99999859f, 0.99999982f, 1.0000000f,
};

static const fl_i16 fl_logN400[21] = {
0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 16, 16, 16, 21, 21, 24, 29, 34, 36, };

static const fl_i16 fl_cache_index50[105] = {
-1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 41, 41, 41,
82, 82, 123, 164, 200, 222, 0, 0, 0, 0, 0, 0, 0, 0, 41,
41, 41, 41, 123, 123, 123, 164, 164, 240, 266, 283, 295, 41, 41, 41,
41, 41, 41, 41, 41, 123, 123, 123, 123, 240, 240, 240, 266, 266, 305,
318, 328, 336, 123, 123, 123, 123, 123, 123, 123, 123, 240, 240, 240, 240,
305, 305, 305, 318, 318, 343, 351, 358, 364, 240, 240, 240, 240, 240, 240,
240, 240, 305, 305, 305, 305, 343, 343, 343, 351, 351, 370, 376, 382, 387,
};

static const fl_u8 fl_cache_bits50[392] = {
40, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 40, 15, 23, 28,
31, 34, 36, 38, 39, 41, 42, 43, 44, 45, 46, 47, 47, 49, 50,
51, 52, 53, 54, 55, 55, 57, 58, 59, 60, 61, 62, 63, 63, 65,
66, 67, 68, 69, 70, 71, 71, 40, 20, 33, 41, 48, 53, 57, 61,
64, 66, 69, 71, 73, 75, 76, 78, 80, 82, 85, 87, 89, 91, 92,
94, 96, 98, 101, 103, 105, 107, 108, 110, 112, 114, 117, 119, 121, 123,
124, 126, 128, 40, 23, 39, 51, 60, 67, 73, 79, 83, 87, 91, 94,
97, 100, 102, 105, 107, 111, 115, 118, 121, 124, 126, 129, 131, 135, 139,
142, 145, 148, 150, 153, 155, 159, 163, 166, 169, 172, 174, 177, 179, 35,
28, 49, 65, 78, 89, 99, 107, 114, 120, 126, 132, 136, 141, 145, 149,
153, 159, 165, 171, 176, 180, 185, 189, 192, 199, 205, 211, 216, 220, 225,
229, 232, 239, 245, 251, 21, 33, 58, 79, 97, 112, 125, 137, 148, 157,
166, 174, 182, 189, 195, 201, 207, 217, 227, 235, 243, 251, 17, 35, 63,
86, 106, 123, 139, 152, 165, 177, 187, 197, 206, 214, 222, 230, 237, 250,
25, 31, 55, 75, 91, 105, 117, 128, 138, 146, 154, 161, 168, 174, 180,
185, 190, 200, 208, 215, 222, 229, 235, 240, 245, 255, 16, 36, 65, 89,
110, 128, 144, 159, 173, 185, 196, 207, 217, 226, 234, 242, 250, 11, 41,
74, 103, 128, 151, 172, 191, 209, 225, 241, 255, 9, 43, 79, 110, 138,
163, 186, 207, 227, 246, 12, 39, 71, 99, 123, 144, 164, 182, 198, 214,
228, 241, 253, 9, 44, 81, 113, 142, 168, 192, 214, 235, 255, 7, 49,
90, 127, 160, 191, 220, 247, 6, 51, 95, 134, 170, 203, 234, 7, 47,
87, 123, 155, 184, 212, 237, 6, 52, 97, 137, 174, 208, 240, 5, 57,
106, 151, 192, 231, 5, 59, 111, 158, 202, 243, 5, 55, 103, 147, 187,
224, 5, 60, 113, 161, 206, 248, 4, 65, 122, 175, 224, 4, 67, 127,
182, 234, };

static const fl_u8 fl_cache_caps50[168] = {
224, 224, 224, 224, 224, 224, 224, 224, 160, 160, 160, 160, 185, 185, 185,
178, 178, 168, 134, 61, 37, 224, 224, 224, 224, 224, 224, 224, 224, 240,
240, 240, 240, 207, 207, 207, 198, 198, 183, 144, 66, 40, 160, 160, 160,
160, 160, 160, 160, 160, 185, 185, 185, 185, 193, 193, 193, 183, 183, 172,
138, 64, 38, 240, 240, 240, 240, 240, 240, 240, 240, 207, 207, 207, 207,
204, 204, 204, 193, 193, 180, 143, 66, 40, 185, 185, 185, 185, 185, 185,
185, 185, 193, 193, 193, 193, 193, 193, 193, 183, 183, 172, 138, 65, 39,
207, 207, 207, 207, 207, 207, 207, 207, 204, 204, 204, 204, 201, 201, 201,
188, 188, 176, 141, 66, 40, 193, 193, 193, 193, 193, 193, 193, 193, 193,
193, 193, 193, 194, 194, 194, 184, 184, 173, 139, 65, 39, 204, 204, 204,
204, 204, 204, 204, 204, 201, 201, 201, 201, 198, 198, 198, 187, 187, 175,
140, 66, 40, };

static const fl_cpx fl_fft_twiddles48000_960[480] = {
{1.0000000f, -0.0000000f}, {0.99991433f, -0.013089596f},
{0.99965732f, -0.026176948f}, {0.99922904f, -0.039259816f},
{0.99862953f, -0.052335956f}, {0.99785892f, -0.065403129f},
{0.99691733f, -0.078459096f}, {0.99580493f, -0.091501619f},
{0.99452190f, -0.10452846f}, {0.99306846f, -0.11753740f},
{0.99144486f, -0.13052619f}, {0.98965139f, -0.14349262f},
{0.98768834f, -0.15643447f}, {0.98555606f, -0.16934950f},
{0.98325491f, -0.18223553f}, {0.98078528f, -0.19509032f},
{0.97814760f, -0.20791169f}, {0.97534232f, -0.22069744f},
{0.97236992f, -0.23344536f}, {0.96923091f, -0.24615329f},
{0.96592583f, -0.25881905f}, {0.96245524f, -0.27144045f},
{0.95881973f, -0.28401534f}, {0.95501994f, -0.29654157f},
{0.95105652f, -0.30901699f}, {0.94693013f, -0.32143947f},
{0.94264149f, -0.33380686f}, {0.93819134f, -0.34611706f},
{0.93358043f, -0.35836795f}, {0.92880955f, -0.37055744f},
{0.92387953f, -0.38268343f}, {0.91879121f, -0.39474386f},
{0.91354546f, -0.40673664f}, {0.90814317f, -0.41865974f},
{0.90258528f, -0.43051110f}, {0.89687274f, -0.44228869f},
{0.89100652f, -0.45399050f}, {0.88498764f, -0.46561452f},
{0.87881711f, -0.47715876f}, {0.87249601f, -0.48862124f},
{0.86602540f, -0.50000000f}, {0.85940641f, -0.51129309f},
{0.85264016f, -0.52249856f}, {0.84572782f, -0.53361452f},
{0.83867057f, -0.54463904f}, {0.83146961f, -0.55557023f},
{0.82412619f, -0.56640624f}, {0.81664156f, -0.57714519f},
{0.80901699f, -0.58778525f}, {0.80125381f, -0.59832460f},
{0.79335334f, -0.60876143f}, {0.78531693f, -0.61909395f},
{0.77714596f, -0.62932039f}, {0.76884183f, -0.63943900f},
{0.76040597f, -0.64944805f}, {0.75183981f, -0.65934582f},
{0.74314483f, -0.66913061f}, {0.73432251f, -0.67880075f},
{0.72537437f, -0.68835458f}, {0.71630194f, -0.69779046f},
{0.70710678f, -0.70710678f}, {0.69779046f, -0.71630194f},
{0.68835458f, -0.72537437f}, {0.67880075f, -0.73432251f},
{0.66913061f, -0.74314483f}, {0.65934582f, -0.75183981f},
{0.64944805f, -0.76040597f}, {0.63943900f, -0.76884183f},
{0.62932039f, -0.77714596f}, {0.61909395f, -0.78531693f},
{0.60876143f, -0.79335334f}, {0.59832460f, -0.80125381f},
{0.58778525f, -0.80901699f}, {0.57714519f, -0.81664156f},
{0.56640624f, -0.82412619f}, {0.55557023f, -0.83146961f},
{0.54463904f, -0.83867057f}, {0.53361452f, -0.84572782f},
{0.52249856f, -0.85264016f}, {0.51129309f, -0.85940641f},
{0.50000000f, -0.86602540f}, {0.48862124f, -0.87249601f},
{0.47715876f, -0.87881711f}, {0.46561452f, -0.88498764f},
{0.45399050f, -0.89100652f}, {0.44228869f, -0.89687274f},
{0.43051110f, -0.90258528f}, {0.41865974f, -0.90814317f},
{0.40673664f, -0.91354546f}, {0.39474386f, -0.91879121f},
{0.38268343f, -0.92387953f}, {0.37055744f, -0.92880955f},
{0.35836795f, -0.93358043f}, {0.34611706f, -0.93819134f},
{0.33380686f, -0.94264149f}, {0.32143947f, -0.94693013f},
{0.30901699f, -0.95105652f}, {0.29654157f, -0.95501994f},
{0.28401534f, -0.95881973f}, {0.27144045f, -0.96245524f},
{0.25881905f, -0.96592583f}, {0.24615329f, -0.96923091f},
{0.23344536f, -0.97236992f}, {0.22069744f, -0.97534232f},
{0.20791169f, -0.97814760f}, {0.19509032f, -0.98078528f},
{0.18223553f, -0.98325491f}, {0.16934950f, -0.98555606f},
{0.15643447f, -0.98768834f}, {0.14349262f, -0.98965139f},
{0.13052619f, -0.99144486f}, {0.11753740f, -0.99306846f},
{0.10452846f, -0.99452190f}, {0.091501619f, -0.99580493f},
{0.078459096f, -0.99691733f}, {0.065403129f, -0.99785892f},
{0.052335956f, -0.99862953f}, {0.039259816f, -0.99922904f},
{0.026176948f, -0.99965732f}, {0.013089596f, -0.99991433f},
{6.1232340e-17f, -1.0000000f}, {-0.013089596f, -0.99991433f},
{-0.026176948f, -0.99965732f}, {-0.039259816f, -0.99922904f},
{-0.052335956f, -0.99862953f}, {-0.065403129f, -0.99785892f},
{-0.078459096f, -0.99691733f}, {-0.091501619f, -0.99580493f},
{-0.10452846f, -0.99452190f}, {-0.11753740f, -0.99306846f},
{-0.13052619f, -0.99144486f}, {-0.14349262f, -0.98965139f},
{-0.15643447f, -0.98768834f}, {-0.16934950f, -0.98555606f},
{-0.18223553f, -0.98325491f}, {-0.19509032f, -0.98078528f},
{-0.20791169f, -0.97814760f}, {-0.22069744f, -0.97534232f},
{-0.23344536f, -0.97236992f}, {-0.24615329f, -0.96923091f},
{-0.25881905f, -0.96592583f}, {-0.27144045f, -0.96245524f},
{-0.28401534f, -0.95881973f}, {-0.29654157f, -0.95501994f},
{-0.30901699f, -0.95105652f}, {-0.32143947f, -0.94693013f},
{-0.33380686f, -0.94264149f}, {-0.34611706f, -0.93819134f},
{-0.35836795f, -0.93358043f}, {-0.37055744f, -0.92880955f},
{-0.38268343f, -0.92387953f}, {-0.39474386f, -0.91879121f},
{-0.40673664f, -0.91354546f}, {-0.41865974f, -0.90814317f},
{-0.43051110f, -0.90258528f}, {-0.44228869f, -0.89687274f},
{-0.45399050f, -0.89100652f}, {-0.46561452f, -0.88498764f},
{-0.47715876f, -0.87881711f}, {-0.48862124f, -0.87249601f},
{-0.50000000f, -0.86602540f}, {-0.51129309f, -0.85940641f},
{-0.52249856f, -0.85264016f}, {-0.53361452f, -0.84572782f},
{-0.54463904f, -0.83867057f}, {-0.55557023f, -0.83146961f},
{-0.56640624f, -0.82412619f}, {-0.57714519f, -0.81664156f},
{-0.58778525f, -0.80901699f}, {-0.59832460f, -0.80125381f},
{-0.60876143f, -0.79335334f}, {-0.61909395f, -0.78531693f},
{-0.62932039f, -0.77714596f}, {-0.63943900f, -0.76884183f},
{-0.64944805f, -0.76040597f}, {-0.65934582f, -0.75183981f},
{-0.66913061f, -0.74314483f}, {-0.67880075f, -0.73432251f},
{-0.68835458f, -0.72537437f}, {-0.69779046f, -0.71630194f},
{-0.70710678f, -0.70710678f}, {-0.71630194f, -0.69779046f},
{-0.72537437f, -0.68835458f}, {-0.73432251f, -0.67880075f},
{-0.74314483f, -0.66913061f}, {-0.75183981f, -0.65934582f},
{-0.76040597f, -0.64944805f}, {-0.76884183f, -0.63943900f},
{-0.77714596f, -0.62932039f}, {-0.78531693f, -0.61909395f},
{-0.79335334f, -0.60876143f}, {-0.80125381f, -0.59832460f},
{-0.80901699f, -0.58778525f}, {-0.81664156f, -0.57714519f},
{-0.82412619f, -0.56640624f}, {-0.83146961f, -0.55557023f},
{-0.83867057f, -0.54463904f}, {-0.84572782f, -0.53361452f},
{-0.85264016f, -0.52249856f}, {-0.85940641f, -0.51129309f},
{-0.86602540f, -0.50000000f}, {-0.87249601f, -0.48862124f},
{-0.87881711f, -0.47715876f}, {-0.88498764f, -0.46561452f},
{-0.89100652f, -0.45399050f}, {-0.89687274f, -0.44228869f},
{-0.90258528f, -0.43051110f}, {-0.90814317f, -0.41865974f},
{-0.91354546f, -0.40673664f}, {-0.91879121f, -0.39474386f},
{-0.92387953f, -0.38268343f}, {-0.92880955f, -0.37055744f},
{-0.93358043f, -0.35836795f}, {-0.93819134f, -0.34611706f},
{-0.94264149f, -0.33380686f}, {-0.94693013f, -0.32143947f},
{-0.95105652f, -0.30901699f}, {-0.95501994f, -0.29654157f},
{-0.95881973f, -0.28401534f}, {-0.96245524f, -0.27144045f},
{-0.96592583f, -0.25881905f}, {-0.96923091f, -0.24615329f},
{-0.97236992f, -0.23344536f}, {-0.97534232f, -0.22069744f},
{-0.97814760f, -0.20791169f}, {-0.98078528f, -0.19509032f},
{-0.98325491f, -0.18223553f}, {-0.98555606f, -0.16934950f},
{-0.98768834f, -0.15643447f}, {-0.98965139f, -0.14349262f},
{-0.99144486f, -0.13052619f}, {-0.99306846f, -0.11753740f},
{-0.99452190f, -0.10452846f}, {-0.99580493f, -0.091501619f},
{-0.99691733f, -0.078459096f}, {-0.99785892f, -0.065403129f},
{-0.99862953f, -0.052335956f}, {-0.99922904f, -0.039259816f},
{-0.99965732f, -0.026176948f}, {-0.99991433f, -0.013089596f},
{-1.0000000f, -1.2246468e-16f}, {-0.99991433f, 0.013089596f},
{-0.99965732f, 0.026176948f}, {-0.99922904f, 0.039259816f},
{-0.99862953f, 0.052335956f}, {-0.99785892f, 0.065403129f},
{-0.99691733f, 0.078459096f}, {-0.99580493f, 0.091501619f},
{-0.99452190f, 0.10452846f}, {-0.99306846f, 0.11753740f},
{-0.99144486f, 0.13052619f}, {-0.98965139f, 0.14349262f},
{-0.98768834f, 0.15643447f}, {-0.98555606f, 0.16934950f},
{-0.98325491f, 0.18223553f}, {-0.98078528f, 0.19509032f},
{-0.97814760f, 0.20791169f}, {-0.97534232f, 0.22069744f},
{-0.97236992f, 0.23344536f}, {-0.96923091f, 0.24615329f},
{-0.96592583f, 0.25881905f}, {-0.96245524f, 0.27144045f},
{-0.95881973f, 0.28401534f}, {-0.95501994f, 0.29654157f},
{-0.95105652f, 0.30901699f}, {-0.94693013f, 0.32143947f},
{-0.94264149f, 0.33380686f}, {-0.93819134f, 0.34611706f},
{-0.93358043f, 0.35836795f}, {-0.92880955f, 0.37055744f},
{-0.92387953f, 0.38268343f}, {-0.91879121f, 0.39474386f},
{-0.91354546f, 0.40673664f}, {-0.90814317f, 0.41865974f},
{-0.90258528f, 0.43051110f}, {-0.89687274f, 0.44228869f},
{-0.89100652f, 0.45399050f}, {-0.88498764f, 0.46561452f},
{-0.87881711f, 0.47715876f}, {-0.87249601f, 0.48862124f},
{-0.86602540f, 0.50000000f}, {-0.85940641f, 0.51129309f},
{-0.85264016f, 0.52249856f}, {-0.84572782f, 0.53361452f},
{-0.83867057f, 0.54463904f}, {-0.83146961f, 0.55557023f},
{-0.82412619f, 0.56640624f}, {-0.81664156f, 0.57714519f},
{-0.80901699f, 0.58778525f}, {-0.80125381f, 0.59832460f},
{-0.79335334f, 0.60876143f}, {-0.78531693f, 0.61909395f},
{-0.77714596f, 0.62932039f}, {-0.76884183f, 0.63943900f},
{-0.76040597f, 0.64944805f}, {-0.75183981f, 0.65934582f},
{-0.74314483f, 0.66913061f}, {-0.73432251f, 0.67880075f},
{-0.72537437f, 0.68835458f}, {-0.71630194f, 0.69779046f},
{-0.70710678f, 0.70710678f}, {-0.69779046f, 0.71630194f},
{-0.68835458f, 0.72537437f}, {-0.67880075f, 0.73432251f},
{-0.66913061f, 0.74314483f}, {-0.65934582f, 0.75183981f},
{-0.64944805f, 0.76040597f}, {-0.63943900f, 0.76884183f},
{-0.62932039f, 0.77714596f}, {-0.61909395f, 0.78531693f},
{-0.60876143f, 0.79335334f}, {-0.59832460f, 0.80125381f},
{-0.58778525f, 0.80901699f}, {-0.57714519f, 0.81664156f},
{-0.56640624f, 0.82412619f}, {-0.55557023f, 0.83146961f},
{-0.54463904f, 0.83867057f}, {-0.53361452f, 0.84572782f},
{-0.52249856f, 0.85264016f}, {-0.51129309f, 0.85940641f},
{-0.50000000f, 0.86602540f}, {-0.48862124f, 0.87249601f},
{-0.47715876f, 0.87881711f}, {-0.46561452f, 0.88498764f},
{-0.45399050f, 0.89100652f}, {-0.44228869f, 0.89687274f},
{-0.43051110f, 0.90258528f}, {-0.41865974f, 0.90814317f},
{-0.40673664f, 0.91354546f}, {-0.39474386f, 0.91879121f},
{-0.38268343f, 0.92387953f}, {-0.37055744f, 0.92880955f},
{-0.35836795f, 0.93358043f}, {-0.34611706f, 0.93819134f},
{-0.33380686f, 0.94264149f}, {-0.32143947f, 0.94693013f},
{-0.30901699f, 0.95105652f}, {-0.29654157f, 0.95501994f},
{-0.28401534f, 0.95881973f}, {-0.27144045f, 0.96245524f},
{-0.25881905f, 0.96592583f}, {-0.24615329f, 0.96923091f},
{-0.23344536f, 0.97236992f}, {-0.22069744f, 0.97534232f},
{-0.20791169f, 0.97814760f}, {-0.19509032f, 0.98078528f},
{-0.18223553f, 0.98325491f}, {-0.16934950f, 0.98555606f},
{-0.15643447f, 0.98768834f}, {-0.14349262f, 0.98965139f},
{-0.13052619f, 0.99144486f}, {-0.11753740f, 0.99306846f},
{-0.10452846f, 0.99452190f}, {-0.091501619f, 0.99580493f},
{-0.078459096f, 0.99691733f}, {-0.065403129f, 0.99785892f},
{-0.052335956f, 0.99862953f}, {-0.039259816f, 0.99922904f},
{-0.026176948f, 0.99965732f}, {-0.013089596f, 0.99991433f},
{-1.8369702e-16f, 1.0000000f}, {0.013089596f, 0.99991433f},
{0.026176948f, 0.99965732f}, {0.039259816f, 0.99922904f},
{0.052335956f, 0.99862953f}, {0.065403129f, 0.99785892f},
{0.078459096f, 0.99691733f}, {0.091501619f, 0.99580493f},
{0.10452846f, 0.99452190f}, {0.11753740f, 0.99306846f},
{0.13052619f, 0.99144486f}, {0.14349262f, 0.98965139f},
{0.15643447f, 0.98768834f}, {0.16934950f, 0.98555606f},
{0.18223553f, 0.98325491f}, {0.19509032f, 0.98078528f},
{0.20791169f, 0.97814760f}, {0.22069744f, 0.97534232f},
{0.23344536f, 0.97236992f}, {0.24615329f, 0.96923091f},
{0.25881905f, 0.96592583f}, {0.27144045f, 0.96245524f},
{0.28401534f, 0.95881973f}, {0.29654157f, 0.95501994f},
{0.30901699f, 0.95105652f}, {0.32143947f, 0.94693013f},
{0.33380686f, 0.94264149f}, {0.34611706f, 0.93819134f},
{0.35836795f, 0.93358043f}, {0.37055744f, 0.92880955f},
{0.38268343f, 0.92387953f}, {0.39474386f, 0.91879121f},
{0.40673664f, 0.91354546f}, {0.41865974f, 0.90814317f},
{0.43051110f, 0.90258528f}, {0.44228869f, 0.89687274f},
{0.45399050f, 0.89100652f}, {0.46561452f, 0.88498764f},
{0.47715876f, 0.87881711f}, {0.48862124f, 0.87249601f},
{0.50000000f, 0.86602540f}, {0.51129309f, 0.85940641f},
{0.52249856f, 0.85264016f}, {0.53361452f, 0.84572782f},
{0.54463904f, 0.83867057f}, {0.55557023f, 0.83146961f},
{0.56640624f, 0.82412619f}, {0.57714519f, 0.81664156f},
{0.58778525f, 0.80901699f}, {0.59832460f, 0.80125381f},
{0.60876143f, 0.79335334f}, {0.61909395f, 0.78531693f},
{0.62932039f, 0.77714596f}, {0.63943900f, 0.76884183f},
{0.64944805f, 0.76040597f}, {0.65934582f, 0.75183981f},
{0.66913061f, 0.74314483f}, {0.67880075f, 0.73432251f},
{0.68835458f, 0.72537437f}, {0.69779046f, 0.71630194f},
{0.70710678f, 0.70710678f}, {0.71630194f, 0.69779046f},
{0.72537437f, 0.68835458f}, {0.73432251f, 0.67880075f},
{0.74314483f, 0.66913061f}, {0.75183981f, 0.65934582f},
{0.76040597f, 0.64944805f}, {0.76884183f, 0.63943900f},
{0.77714596f, 0.62932039f}, {0.78531693f, 0.61909395f},
{0.79335334f, 0.60876143f}, {0.80125381f, 0.59832460f},
{0.80901699f, 0.58778525f}, {0.81664156f, 0.57714519f},
{0.82412619f, 0.56640624f}, {0.83146961f, 0.55557023f},
{0.83867057f, 0.54463904f}, {0.84572782f, 0.53361452f},
{0.85264016f, 0.52249856f}, {0.85940641f, 0.51129309f},
{0.86602540f, 0.50000000f}, {0.87249601f, 0.48862124f},
{0.87881711f, 0.47715876f}, {0.88498764f, 0.46561452f},
{0.89100652f, 0.45399050f}, {0.89687274f, 0.44228869f},
{0.90258528f, 0.43051110f}, {0.90814317f, 0.41865974f},
{0.91354546f, 0.40673664f}, {0.91879121f, 0.39474386f},
{0.92387953f, 0.38268343f}, {0.92880955f, 0.37055744f},
{0.93358043f, 0.35836795f}, {0.93819134f, 0.34611706f},
{0.94264149f, 0.33380686f}, {0.94693013f, 0.32143947f},
{0.95105652f, 0.30901699f}, {0.95501994f, 0.29654157f},
{0.95881973f, 0.28401534f}, {0.96245524f, 0.27144045f},
{0.96592583f, 0.25881905f}, {0.96923091f, 0.24615329f},
{0.97236992f, 0.23344536f}, {0.97534232f, 0.22069744f},
{0.97814760f, 0.20791169f}, {0.98078528f, 0.19509032f},
{0.98325491f, 0.18223553f}, {0.98555606f, 0.16934950f},
{0.98768834f, 0.15643447f}, {0.98965139f, 0.14349262f},
{0.99144486f, 0.13052619f}, {0.99306846f, 0.11753740f},
{0.99452190f, 0.10452846f}, {0.99580493f, 0.091501619f},
{0.99691733f, 0.078459096f}, {0.99785892f, 0.065403129f},
{0.99862953f, 0.052335956f}, {0.99922904f, 0.039259816f},
{0.99965732f, 0.026176948f}, {0.99991433f, 0.013089596f},
};

static const fl_i16 fl_fft_bitrev480[480] = {
0, 96, 192, 288, 384, 32, 128, 224, 320, 416, 64, 160, 256, 352, 448,
8, 104, 200, 296, 392, 40, 136, 232, 328, 424, 72, 168, 264, 360, 456,
16, 112, 208, 304, 400, 48, 144, 240, 336, 432, 80, 176, 272, 368, 464,
24, 120, 216, 312, 408, 56, 152, 248, 344, 440, 88, 184, 280, 376, 472,
4, 100, 196, 292, 388, 36, 132, 228, 324, 420, 68, 164, 260, 356, 452,
12, 108, 204, 300, 396, 44, 140, 236, 332, 428, 76, 172, 268, 364, 460,
20, 116, 212, 308, 404, 52, 148, 244, 340, 436, 84, 180, 276, 372, 468,
28, 124, 220, 316, 412, 60, 156, 252, 348, 444, 92, 188, 284, 380, 476,
1, 97, 193, 289, 385, 33, 129, 225, 321, 417, 65, 161, 257, 353, 449,
9, 105, 201, 297, 393, 41, 137, 233, 329, 425, 73, 169, 265, 361, 457,
17, 113, 209, 305, 401, 49, 145, 241, 337, 433, 81, 177, 273, 369, 465,
25, 121, 217, 313, 409, 57, 153, 249, 345, 441, 89, 185, 281, 377, 473,
5, 101, 197, 293, 389, 37, 133, 229, 325, 421, 69, 165, 261, 357, 453,
13, 109, 205, 301, 397, 45, 141, 237, 333, 429, 77, 173, 269, 365, 461,
21, 117, 213, 309, 405, 53, 149, 245, 341, 437, 85, 181, 277, 373, 469,
29, 125, 221, 317, 413, 61, 157, 253, 349, 445, 93, 189, 285, 381, 477,
2, 98, 194, 290, 386, 34, 130, 226, 322, 418, 66, 162, 258, 354, 450,
10, 106, 202, 298, 394, 42, 138, 234, 330, 426, 74, 170, 266, 362, 458,
18, 114, 210, 306, 402, 50, 146, 242, 338, 434, 82, 178, 274, 370, 466,
26, 122, 218, 314, 410, 58, 154, 250, 346, 442, 90, 186, 282, 378, 474,
6, 102, 198, 294, 390, 38, 134, 230, 326, 422, 70, 166, 262, 358, 454,
14, 110, 206, 302, 398, 46, 142, 238, 334, 430, 78, 174, 270, 366, 462,
22, 118, 214, 310, 406, 54, 150, 246, 342, 438, 86, 182, 278, 374, 470,
30, 126, 222, 318, 414, 62, 158, 254, 350, 446, 94, 190, 286, 382, 478,
3, 99, 195, 291, 387, 35, 131, 227, 323, 419, 67, 163, 259, 355, 451,
11, 107, 203, 299, 395, 43, 139, 235, 331, 427, 75, 171, 267, 363, 459,
19, 115, 211, 307, 403, 51, 147, 243, 339, 435, 83, 179, 275, 371, 467,
27, 123, 219, 315, 411, 59, 155, 251, 347, 443, 91, 187, 283, 379, 475,
7, 103, 199, 295, 391, 39, 135, 231, 327, 423, 71, 167, 263, 359, 455,
15, 111, 207, 303, 399, 47, 143, 239, 335, 431, 79, 175, 271, 367, 463,
23, 119, 215, 311, 407, 55, 151, 247, 343, 439, 87, 183, 279, 375, 471,
31, 127, 223, 319, 415, 63, 159, 255, 351, 447, 95, 191, 287, 383, 479,
};

static const fl_i16 fl_fft_bitrev240[240] = {
0, 48, 96, 144, 192, 16, 64, 112, 160, 208, 32, 80, 128, 176, 224,
4, 52, 100, 148, 196, 20, 68, 116, 164, 212, 36, 84, 132, 180, 228,
8, 56, 104, 152, 200, 24, 72, 120, 168, 216, 40, 88, 136, 184, 232,
12, 60, 108, 156, 204, 28, 76, 124, 172, 220, 44, 92, 140, 188, 236,
1, 49, 97, 145, 193, 17, 65, 113, 161, 209, 33, 81, 129, 177, 225,
5, 53, 101, 149, 197, 21, 69, 117, 165, 213, 37, 85, 133, 181, 229,
9, 57, 105, 153, 201, 25, 73, 121, 169, 217, 41, 89, 137, 185, 233,
13, 61, 109, 157, 205, 29, 77, 125, 173, 221, 45, 93, 141, 189, 237,
2, 50, 98, 146, 194, 18, 66, 114, 162, 210, 34, 82, 130, 178, 226,
6, 54, 102, 150, 198, 22, 70, 118, 166, 214, 38, 86, 134, 182, 230,
10, 58, 106, 154, 202, 26, 74, 122, 170, 218, 42, 90, 138, 186, 234,
14, 62, 110, 158, 206, 30, 78, 126, 174, 222, 46, 94, 142, 190, 238,
3, 51, 99, 147, 195, 19, 67, 115, 163, 211, 35, 83, 131, 179, 227,
7, 55, 103, 151, 199, 23, 71, 119, 167, 215, 39, 87, 135, 183, 231,
11, 59, 107, 155, 203, 27, 75, 123, 171, 219, 43, 91, 139, 187, 235,
15, 63, 111, 159, 207, 31, 79, 127, 175, 223, 47, 95, 143, 191, 239,
};

static const fl_i16 fl_fft_bitrev120[120] = {
0, 24, 48, 72, 96, 8, 32, 56, 80, 104, 16, 40, 64, 88, 112,
4, 28, 52, 76, 100, 12, 36, 60, 84, 108, 20, 44, 68, 92, 116,
1, 25, 49, 73, 97, 9, 33, 57, 81, 105, 17, 41, 65, 89, 113,
5, 29, 53, 77, 101, 13, 37, 61, 85, 109, 21, 45, 69, 93, 117,
2, 26, 50, 74, 98, 10, 34, 58, 82, 106, 18, 42, 66, 90, 114,
6, 30, 54, 78, 102, 14, 38, 62, 86, 110, 22, 46, 70, 94, 118,
3, 27, 51, 75, 99, 11, 35, 59, 83, 107, 19, 43, 67, 91, 115,
7, 31, 55, 79, 103, 15, 39, 63, 87, 111, 23, 47, 71, 95, 119,
};

static const fl_i16 fl_fft_bitrev60[60] = {
0, 12, 24, 36, 48, 4, 16, 28, 40, 52, 8, 20, 32, 44, 56,
1, 13, 25, 37, 49, 5, 17, 29, 41, 53, 9, 21, 33, 45, 57,
2, 14, 26, 38, 50, 6, 18, 30, 42, 54, 10, 22, 34, 46, 58,
3, 15, 27, 39, 51, 7, 19, 31, 43, 55, 11, 23, 35, 47, 59,
};

static const float fl_mdct_twiddles960[1800] = {
0.99999992f, 0.99999322f, 0.99997582f, 0.99994771f, 0.99990889f,
0.99985936f, 0.99979913f, 0.99972818f, 0.99964653f, 0.99955418f,
0.99945112f, 0.99933736f, 0.99921289f, 0.99907773f, 0.99893186f,
0.99877530f, 0.99860804f, 0.99843009f, 0.99824144f, 0.99804211f,
0.99783209f, 0.99761138f, 0.99737998f, 0.99713791f, 0.99688516f,
0.99662173f, 0.99634763f, 0.99606285f, 0.99576741f, 0.99546131f,
0.99514455f, 0.99481713f, 0.99447905f, 0.99413033f, 0.99377096f,
0.99340095f, 0.99302029f, 0.99262901f, 0.99222709f, 0.99181455f,
0.99139139f, 0.99095760f, 0.99051321f, 0.99005821f, 0.98959261f,
0.98911641f, 0.98862961f, 0.98813223f, 0.98762427f, 0.98710573f,
0.98657662f, 0.98603694f, 0.98548670f, 0.98492591f, 0.98435457f,
0.98377269f, 0.98318028f, 0.98257734f, 0.98196387f, 0.98133989f,
0.98070539f, 0.98006040f, 0.97940491f, 0.97873893f, 0.97806247f,
0.97737554f, 0.97667813f, 0.97597027f, 0.97525196f, 0.97452320f,
0.97378401f, 0.97303439f, 0.97227435f, 0.97150389f, 0.97072303f,
0.96993178f, 0.96913014f, 0.96831812f, 0.96749573f, 0.96666298f,
0.96581987f, 0.96496643f, 0.96410265f, 0.96322854f, 0.96234412f,
0.96144939f, 0.96054437f, 0.95962906f, 0.95870347f, 0.95776762f,
0.95682151f, 0.95586515f, 0.95489856f, 0.95392174f, 0.95293471f,
0.95193746f, 0.95093003f, 0.94991241f, 0.94888462f, 0.94784667f,
0.94679856f, 0.94574032f, 0.94467195f, 0.94359346f, 0.94250486f,
0.94140618f, 0.94029741f, 0.93917857f, 0.93804967f, 0.93691073f,
0.93576176f, 0.93460276f, 0.93343375f, 0.93225475f, 0.93106577f,
0.92986681f, 0.92865789f, 0.92743903f, 0.92621024f, 0.92497153f,
0.92372291f, 0.92246440f, 0.92119602f, 0.91991776f, 0.91862966f,
0.91733172f, 0.91602395f, 0.91470637f, 0.91337900f, 0.91204185f,
0.91069493f, 0.90933825f, 0.90797184f, 0.90659570f, 0.90520986f,
0.90381432f, 0.90240910f, 0.90099422f, 0.89956969f, 0.89813553f,
0.89669174f, 0.89523836f, 0.89377538f, 0.89230284f, 0.89082074f,
0.88932910f, 0.88782793f, 0.88631726f, 0.88479710f, 0.88326746f,
0.88172836f, 0.88017982f, 0.87862185f, 0.87705448f, 0.87547771f,
0.87389156f, 0.87229606f, 0.87069121f, 0.86907704f, 0.86745357f,
0.86582080f, 0.86417876f, 0.86252747f, 0.86086694f, 0.85919719f,
0.85751824f, 0.85583010f, 0.85413281f, 0.85242636f, 0.85071078f,
0.84898610f, 0.84725232f, 0.84550947f, 0.84375756f, 0.84199662f,
0.84022666f, 0.83844771f, 0.83665977f, 0.83486287f, 0.83305704f,
0.83124228f, 0.82941862f, 0.82758608f, 0.82574467f, 0.82389442f,
0.82203535f, 0.82016748f, 0.81829082f, 0.81640540f, 0.81451123f,
0.81260835f, 0.81069676f, 0.80877649f, 0.80684755f, 0.80490998f,
0.80296379f, 0.80100899f, 0.79904562f, 0.79707370f, 0.79509323f,
0.79310425f, 0.79110678f, 0.78910084f, 0.78708644f, 0.78506362f,
0.78303239f, 0.78099277f, 0.77894479f, 0.77688847f, 0.77482382f,
0.77275088f, 0.77066967f, 0.76858020f, 0.76648250f, 0.76437659f,
0.76226250f, 0.76014024f, 0.75800984f, 0.75587132f, 0.75372471f,
0.75157003f, 0.74940730f, 0.74723654f, 0.74505779f, 0.74287105f,
0.74067635f, 0.73847373f, 0.73626320f, 0.73404478f, 0.73181850f,
0.72958438f, 0.72734245f, 0.72509273f, 0.72283525f, 0.72057002f,
0.71829708f, 0.71601644f, 0.71372814f, 0.71143220f, 0.70912863f,
0.70681747f, 0.70449874f, 0.70217247f, 0.69983868f, 0.69749739f,
0.69514863f, 0.69279243f, 0.69042881f, 0.68805780f, 0.68567941f,
0.68329369f, 0.68090064f, 0.67850031f, 0.67609270f, 0.67367786f,
0.67125580f, 0.66882656f, 0.66639015f, 0.66394661f, 0.66149595f,
0.65903821f, 0.65657341f, 0.65410159f, 0.65162275f, 0.64913694f,
0.64664418f, 0.64414449f, 0.64163790f, 0.63912444f, 0.63660414f,
0.63407702f, 0.63154311f, 0.62900244f, 0.62645503f, 0.62390091f,
0.62134011f, 0.61877265f, 0.61619857f, 0.61361789f, 0.61103064f,
0.60843685f, 0.60583654f, 0.60322974f, 0.60061648f, 0.59799679f,
0.59537069f, 0.59273822f, 0.59009940f, 0.58745427f, 0.58480284f,
0.58214514f, 0.57948122f, 0.57681109f, 0.57413478f, 0.57145232f,
0.56876374f, 0.56606907f, 0.56336834f, 0.56066158f, 0.55794881f,
0.55523006f, 0.55250537f, 0.54977477f, 0.54703827f, 0.54429592f,
0.54154774f, 0.53879376f, 0.53603401f, 0.53326852f, 0.53049731f,
0.52772043f, 0.52493790f, 0.52214974f, 0.51935599f, 0.51655668f,
0.51375184f, 0.51094149f, 0.50812568f, 0.50530442f, 0.50247775f,
0.49964570f, 0.49680830f, 0.49396558f, 0.49111757f, 0.48826430f,
0.48540580f, 0.48254210f, 0.47967323f, 0.47679923f, 0.47392012f,
0.47103594f, 0.46814671f, 0.46525247f, 0.46235324f, 0.45944907f,
0.45653997f, 0.45362599f, 0.45070714f, 0.44778347f, 0.44485501f,
0.44192178f, 0.43898381f, 0.43604115f, 0.43309382f, 0.43014185f,
0.42718527f, 0.42422412f, 0.42125842f, 0.41828822f, 0.41531353f,
0.41233440f, 0.40935085f, 0.40636291f, 0.40337063f, 0.40037402f,
0.39737313f, 0.39436798f, 0.39135861f, 0.38834505f, 0.38532733f,
0.38230548f, 0.37927953f, 0.37624953f, 0.37321550f, 0.37017747f,
0.36713547f, 0.36408955f, 0.36103972f, 0.35798603f, 0.35492850f,
0.35186718f, 0.34880208f, 0.34573325f, 0.34266072f, 0.33958451f,
0.33650468f, 0.33342123f, 0.33033422f, 0.32724367f, 0.32414961f,
0.32105209f, 0.31795112f, 0.31484675f, 0.31173901f, 0.30862793f,
0.30551354f, 0.30239588f, 0.29927499f, 0.29615089f, 0.29302362f,
0.28989321f, 0.28675969f, 0.28362310f, 0.28048348f, 0.27734085f,
0.27419526f, 0.27104672f, 0.26789529f, 0.26474098f, 0.26158384f,
0.25842390f, 0.25526119f, 0.25209575f, 0.24892761f, 0.24575680f,
0.24258336f, 0.23940732f, 0.23622872f, 0.23304759f, 0.22986396f,
0.22667787f, 0.22348935f, 0.22029844f, 0.21710517f, 0.21390958f,
0.21071169f, 0.20751155f, 0.20430919f, 0.20110463f, 0.19789793f,
0.19468910f, 0.19147819f, 0.18826523f, 0.18505026f, 0.18183330f,
0.17861439f, 0.17539357f, 0.17217088f, 0.16894634f, 0.16571999f,
0.16249186f, 0.15926200f, 0.15603043f, 0.15279719f, 0.14956231f,
0.14632583f, 0.14308778f, 0.13984820f, 0.13660713f, 0.13336459f,
0.13012062f, 0.12687526f, 0.12362854f, 0.12038049f, 0.11713116f,
0.11388057f, 0.11062877f, 0.10737578f, 0.10412163f, 0.10086638f,
0.097610040f, 0.094352658f, 0.091094266f, 0.087834897f, 0.084574589f,
0.081313374f, 0.078051289f, 0.074788367f, 0.071524645f, 0.068260157f,
0.064994938f, 0.061729023f, 0.058462447f, 0.055195244f, 0.051927451f,
0.048659101f, 0.045390231f, 0.042120874f, 0.038851066f, 0.035580842f,
0.032310238f, 0.029039287f, 0.025768025f, 0.022496487f, 0.019224708f,
0.015952723f, 0.012680568f, 0.0094082767f, 0.0061358846f, 0.0028634269f,
-0.00040906153f, -0.0036815456f, -0.0069539902f, -0.010226360f, -0.013498621f,
-0.016770737f, -0.020042673f, -0.023314395f, -0.026585867f, -0.029857055f,
-0.033127923f, -0.036398436f, -0.039668559f, -0.042938257f, -0.046207495f,
-0.049476239f, -0.052744453f, -0.056012102f, -0.059279151f, -0.062545565f,
-0.065811309f, -0.069076349f, -0.072340649f, -0.075604174f, -0.078866890f,
-0.082128761f, -0.085389752f, -0.088649829f, -0.091908956f, -0.095167100f,
-0.098424224f, -0.10168029f, -0.10493528f, -0.10818913f, -0.11144183f,
-0.11469334f, -0.11794361f, -0.12119263f, -0.12444034f, -0.12768673f,
-0.13093174f, -0.13417536f, -0.13741753f, -0.14065824f, -0.14389744f,
-0.14713510f, -0.15037118f, -0.15360565f, -0.15683848f, -0.16006962f,
-0.16329906f, -0.16652674f, -0.16975264f, -0.17297673f, -0.17619896f,
-0.17941930f, -0.18263772f, -0.18585419f, -0.18906866f, -0.19228112f,
-0.19549151f, -0.19869981f, -0.20190598f, -0.20510998f, -0.20831180f,
-0.21151138f, -0.21470869f, -0.21790371f, -0.22109639f, -0.22428671f,
-0.22747462f, -0.23066010f, -0.23384310f, -0.23702361f, -0.24020157f,
-0.24337696f, -0.24654975f, -0.24971989f, -0.25288736f, -0.25605213f,
-0.25921415f, -0.26237339f, -0.26552983f, -0.26868342f, -0.27183413f,
-0.27498193f, -0.27812679f, -0.28126867f, -0.28440754f, -0.28754336f,
-0.29067610f, -0.29380573f, -0.29693221f, -0.30005551f, -0.30317560f,
-0.30629245f, -0.30940601f, -0.31251626f, -0.31562316f, -0.31872668f,
-0.32182679f, -0.32492345f, -0.32801664f, -0.33110631f, -0.33419243f,
-0.33727497f, -0.34035391f, -0.34342920f, -0.34650081f, -0.34956871f,
-0.35263286f, -0.35569324f, -0.35874981f, -0.36180254f, -0.36485139f,
-0.36789634f, -0.37093735f, -0.37397438f, -0.37700741f, -0.38003640f,
-0.38306132f, -0.38608214f, -0.38909883f, -0.39211135f, -0.39511967f,
-0.39812375f, -0.40112358f, -0.40411910f, -0.40711031f, -0.41009715f,
-0.41307959f, -0.41605762f, -0.41903119f, -0.42200027f, -0.42496483f,
-0.42792484f, -0.43088027f, -0.43383109f, -0.43677726f, -0.43971875f,
-0.44265553f, -0.44558757f, -0.44851484f, -0.45143731f, -0.45435494f,
-0.45726770f, -0.46017557f, -0.46307851f, -0.46597650f, -0.46886949f,
-0.47175746f, -0.47464038f, -0.47751821f, -0.48039093f, -0.48325851f,
-0.48612091f, -0.48897811f, -0.49183006f, -0.49467676f, -0.49751815f,
-0.50035422f, -0.50318492f, -0.50601024f, -0.50883014f, -0.51164459f,
-0.51445356f, -0.51725703f, -0.52005495f, -0.52284730f, -0.52563406f,
-0.52841518f, -0.53119065f, -0.53396043f, -0.53672449f, -0.53948280f,
-0.54223533f, -0.54498206f, -0.54772295f, -0.55045797f, -0.55318710f,
-0.55591031f, -0.55862756f, -0.56133883f, -0.56404409f, -0.56674331f,
-0.56943646f, -0.57212351f, -0.57480443f, -0.57747920f, -0.58014778f,
-0.58281015f, -0.58546628f, -0.58811614f, -0.59075970f, -0.59339694f,
-0.59602782f, -0.59865231f, -0.60127040f, -0.60388204f, -0.60648722f,
-0.60908591f, -0.61167807f, -0.61426368f, -0.61684271f, -0.61941514f,
-0.62198093f, -0.62454007f, -0.62709251f, -0.62963824f, -0.63217722f,
-0.63470944f, -0.63723486f, -0.63975345f, -0.64226519f, -0.64477006f,
-0.64726802f, -0.64975905f, -0.65224312f, -0.65472020f, -0.65719027f,
-0.65965331f, -0.66210928f, -0.66455816f, -0.66699992f, -0.66943454f,
-0.67186199f, -0.67428225f, -0.67669528f, -0.67910107f, -0.68149959f,
-0.68389080f, -0.68627470f, -0.68865124f, -0.69102041f, -0.69338218f,
-0.69573652f, -0.69808341f, -0.70042283f, -0.70275474f, -0.70507913f,
-0.70739597f, -0.70970524f, -0.71200690f, -0.71430093f, -0.71658732f,
-0.71886604f, -0.72113705f, -0.72340034f, -0.72565589f, -0.72790366f,
-0.73014364f, -0.73237580f, -0.73460012f, -0.73681657f, -0.73902513f,
-0.74122577f, -0.74341848f, -0.74560322f, -0.74777998f, -0.74994874f,
-0.75210946f, -0.75426212f, -0.75640671f, -0.75854320f, -0.76067157f,
-0.76279178f, -0.76490383f, -0.76700769f, -0.76910334f, -0.77119075f,
-0.77326989f, -0.77534076f, -0.77740333f, -0.77945757f, -0.78150346f,
-0.78354098f, -0.78557011f, -0.78759083f, -0.78960312f, -0.79160694f,
-0.79360229f, -0.79558915f, -0.79756748f, -0.79953727f, -0.80149850f,
-0.80345114f, -0.80539518f, -0.80733060f, -0.80925737f, -0.81117547f,
-0.81308489f, -0.81498559f, -0.81687757f, -0.81876081f, -0.82063527f,
-0.82250095f, -0.82435781f, -0.82620585f, -0.82804505f, -0.82987537f,
-0.83169681f, -0.83350933f, -0.83531294f, -0.83710760f, -0.83889329f,
-0.84067000f, -0.84243770f, -0.84419639f, -0.84594603f, -0.84768662f,
-0.84941812f, -0.85114053f, -0.85285383f, -0.85455799f, -0.85625300f,
-0.85793884f, -0.85961549f, -0.86128294f, -0.86294116f, -0.86459014f,
-0.86622986f, -0.86786031f, -0.86948146f, -0.87109330f, -0.87269581f,
-0.87428898f, -0.87587278f, -0.87744720f, -0.87901223f, -0.88056784f,
-0.88211402f, -0.88365076f, -0.88517803f, -0.88669582f, -0.88820412f,
-0.88970290f, -0.89119216f, -0.89267187f, -0.89414203f, -0.89560260f,
-0.89705359f, -0.89849497f, -0.89992673f, -0.90134885f, -0.90276131f,
-0.90416411f, -0.90555723f, -0.90694065f, -0.90831436f, -0.90967833f,
-0.91103257f, -0.91237705f, -0.91371176f, -0.91503669f, -0.91635181f,
-0.91765712f, -0.91895261f, -0.92023825f, -0.92151404f, -0.92277996f,
-0.92403600f, -0.92528214f, -0.92651837f, -0.92774468f, -0.92896106f,
-0.93016748f, -0.93136395f, -0.93255044f, -0.93372694f, -0.93489345f,
-0.93604994f, -0.93719641f, -0.93833284f, -0.93945922f, -0.94057555f,
-0.94168180f, -0.94277796f, -0.94386403f, -0.94493999f, -0.94600583f,
-0.94706154f, -0.94810711f, -0.94914252f, -0.95016777f, -0.95118284f,
-0.95218773f, -0.95318242f, -0.95416690f, -0.95514117f, -0.95610520f,
-0.95705900f, -0.95800255f, -0.95893583f, -0.95985885f, -0.96077159f,
-0.96167404f, -0.96256619f, -0.96344803f, -0.96431956f, -0.96518076f,
-0.96603162f, -0.96687213f, -0.96770230f, -0.96852209f, -0.96933152f,
-0.97013057f, -0.97091922f, -0.97169748f, -0.97246533f, -0.97322277f,
-0.97396979f, -0.97470637f, -0.97543252f, -0.97614822f, -0.97685347f,
-0.97754825f, -0.97823257f, -0.97890641f, -0.97956977f, -0.98022263f,
-0.98086500f, -0.98149687f, -0.98211822f, -0.98272906f, -0.98332937f,
-0.98391915f, -0.98449840f, -0.98506710f, -0.98562525f, -0.98617285f,
-0.98670988f, -0.98723635f, -0.98775225f, -0.98825757f, -0.98875230f,
-0.98923645f, -0.98971000f, -0.99017295f, -0.99062530f, -0.99106704f,
-0.99149817f, -0.99191868f, -0.99232857f, -0.99272783f, -0.99311645f,
-0.99349445f, -0.99386180f, -0.99421851f, -0.99456457f, -0.99489998f,
-0.99522474f, -0.99553884f, -0.99584227f, -0.99613505f, -0.99641715f,
-0.99668858f, -0.99694934f, -0.99719943f, -0.99743883f, -0.99766756f,
-0.99788559f, -0.99809295f, -0.99828961f, -0.99847558f, -0.99865086f,
-0.99881544f, -0.99896933f, -0.99911252f, -0.99924501f, -0.99936680f,
-0.99947789f, -0.99957827f, -0.99966795f, -0.99974692f, -0.99981519f,
-0.99987275f, -0.99991960f, -0.99995574f, -0.99998118f, -0.99999590f,
0.99999967f, 0.99997289f, 0.99990328f, 0.99979084f, 0.99963557f,
0.99943748f, 0.99919658f, 0.99891288f, 0.99858638f, 0.99821711f,
0.99780508f, 0.99735031f, 0.99685281f, 0.99631261f, 0.99572973f,
0.99510420f, 0.99443605f, 0.99372529f, 0.99297196f, 0.99217610f,
0.99133774f, 0.99045692f, 0.98953366f, 0.98856802f, 0.98756003f,
0.98650973f, 0.98541718f, 0.98428242f, 0.98310549f, 0.98188645f,
0.98062534f, 0.97932224f, 0.97797718f, 0.97659022f, 0.97516144f,
0.97369088f, 0.97217861f, 0.97062469f, 0.96902920f, 0.96739220f,
0.96571376f, 0.96399395f, 0.96223284f, 0.96043052f, 0.95858705f,
0.95670253f, 0.95477702f, 0.95281061f, 0.95080338f, 0.94875543f,
0.94666684f, 0.94453769f, 0.94236808f, 0.94015810f, 0.93790786f,
0.93561743f, 0.93328693f, 0.93091644f, 0.92850608f, 0.92605595f,
0.92356614f, 0.92103677f, 0.91846795f, 0.91585979f, 0.91321239f,
0.91052587f, 0.90780035f, 0.90503595f, 0.90223277f, 0.89939095f,
0.89651059f, 0.89359184f, 0.89063481f, 0.88763962f, 0.88460641f,
0.88153531f, 0.87842644f, 0.87527995f, 0.87209596f, 0.86887462f,
0.86561605f, 0.86232041f, 0.85898782f, 0.85561844f, 0.85221241f,
0.84876987f, 0.84529098f, 0.84177587f, 0.83822471f, 0.83463763f,
0.83101481f, 0.82735639f, 0.82366252f, 0.81993338f, 0.81616911f,
0.81236987f, 0.80853584f, 0.80466718f, 0.80076404f, 0.79682660f,
0.79285503f, 0.78884950f, 0.78481017f, 0.78073723f, 0.77663084f,
0.77249118f, 0.76831844f, 0.76411277f, 0.75987438f, 0.75560344f,
0.75130013f, 0.74696464f, 0.74259715f, 0.73819784f, 0.73376692f,
0.72930457f, 0.72481097f, 0.72028632f, 0.71573083f, 0.71114467f,
0.70652804f, 0.70188116f, 0.69720420f, 0.69249738f, 0.68776090f,
0.68299495f, 0.67819975f, 0.67337550f, 0.66852240f, 0.66364066f,
0.65873050f, 0.65379211f, 0.64882573f, 0.64383154f, 0.63880978f,
0.63376065f, 0.62868438f, 0.62358117f, 0.61845126f, 0.61329485f,
0.60811216f, 0.60290343f, 0.59766888f, 0.59240872f, 0.58712318f,
0.58181249f, 0.57647688f, 0.57111658f, 0.56573181f, 0.56032281f,
0.55488980f, 0.54943303f, 0.54395272f, 0.53844911f, 0.53292243f,
0.52737292f, 0.52180083f, 0.51620638f, 0.51058981f, 0.50495138f,
0.49929132f, 0.49360987f, 0.48790727f, 0.48218377f, 0.47643962f,
0.47067506f, 0.46489034f, 0.45908570f, 0.45326139f, 0.44741768f,
0.44155479f, 0.43567299f, 0.42977253f, 0.42385365f, 0.41791662f,
0.41196169f, 0.40598911f, 0.39999914f, 0.39399204f, 0.38796806f,
0.38192746f, 0.37587050f, 0.36979743f, 0.36370853f, 0.35760405f,
0.35148424f, 0.34534939f, 0.33919973f, 0.33303555f, 0.32685710f,
0.32066465f, 0.31445847f, 0.30823881f, 0.30200595f, 0.29576015f,
0.28950169f, 0.28323082f, 0.27694782f, 0.27065295f, 0.26434649f,
0.25802871f, 0.25169988f, 0.24536026f, 0.23901013f, 0.23264977f,
0.22627944f, 0.21989941f, 0.21350997f, 0.20711138f, 0.20070391f,
0.19428785f, 0.18786347f, 0.18143104f, 0.17499084f, 0.16854314f,
0.16208822f, 0.15562636f, 0.14915783f, 0.14268292f, 0.13620189f,
0.12971502f, 0.12322260f, 0.11672491f, 0.11022221f, 0.10371479f,
0.097202924f, 0.090686897f, 0.084166986f, 0.077643468f, 0.071116625f,
0.064586736f, 0.058054080f, 0.051518937f, 0.044981587f, 0.038442310f,
0.031901387f, 0.025359097f, 0.018815721f, 0.012271538f, 0.0057268303f,
-0.00081812300f, -0.0073630412f, -0.013907644f, -0.020451651f, -0.026994782f,
-0.033536757f, -0.040077295f, -0.046616116f, -0.053152941f, -0.059687488f,
-0.066219479f, -0.072748633f, -0.079274670f, -0.085797312f, -0.092316279f,
-0.098831291f, -0.10534207f, -0.11184834f, -0.11834981f, -0.12484622f,
-0.13133727f, -0.13782270f, -0.14430223f, -0.15077558f, -0.15724246f,
-0.16370261f, -0.17015575f, -0.17660160f, -0.18303989f, -0.18947033f,
-0.19589266f, -0.20230660f, -0.20871187f, -0.21510820f, -0.22149531f,
-0.22787294f, -0.23424080f, -0.24059864f, -0.24694616f, -0.25328311f,
-0.25960920f, -0.26592418f, -0.27222777f, -0.27851969f, -0.28479968f,
-0.29106748f, -0.29732280f, -0.30356539f, -0.30979497f, -0.31601129f,
-0.32221406f, -0.32840304f, -0.33457794f, -0.34073852f, -0.34688450f,
-0.35301562f, -0.35913161f, -0.36523223f, -0.37131719f, -0.37738626f,
-0.38343915f, -0.38947562f, -0.39549541f, -0.40149825f, -0.40748390f,
-0.41345209f, -0.41940257f, -0.42533508f, -0.43124938f, -0.43714520f,
-0.44302229f, -0.44888041f, -0.45471930f, -0.46053871f, -0.46633839f,
-0.47211810f, -0.47787758f, -0.48361659f, -0.48933489f, -0.49503222f,
-0.50070835f, -0.50636303f, -0.51199602f, -0.51760707f, -0.52319595f,
-0.52876243f, -0.53430625f, -0.53982718f, -0.54532499f, -0.55079944f,
-0.55625029f, -0.56167732f, -0.56708028f, -0.57245896f, -0.57781311f,
-0.58314251f, -0.58844693f, -0.59372614f, -0.59897992f, -0.60420805f,
-0.60941029f, -0.61458642f, -0.61973623f, -0.62485949f, -0.62995598f,
-0.63502549f, -0.64006780f, -0.64508268f, -0.65006994f, -0.65502934f,
-0.65996069f, -0.66486377f, -0.66973837f, -0.67458427f, -0.67940128f,
-0.68418919f, -0.68894779f, -0.69367688f, -0.69837625f, -0.70304571f,
-0.70768504f, -0.71229407f, -0.71687258f, -0.72142039f, -0.72593729f,
-0.73042309f, -0.73487761f, -0.73930064f, -0.74369201f, -0.74805152f,
-0.75237898f, -0.75667422f, -0.76093704f, -0.76516727f, -0.76936471f,
-0.77352921f, -0.77766056f, -0.78175861f, -0.78582316f, -0.78985406f,
-0.79385112f, -0.79781417f, -0.80174305f, -0.80563758f, -0.80949760f,
-0.81332295f, -0.81711346f, -0.82086896f, -0.82458930f, -0.82827432f,
-0.83192386f, -0.83553776f, -0.83911587f, -0.84265803f, -0.84616410f,
-0.84963392f, -0.85306735f, -0.85646423f, -0.85982442f, -0.86314779f,
-0.86643418f, -0.86968345f, -0.87289547f, -0.87607009f, -0.87920719f,
-0.88230663f, -0.88536827f, -0.88839199f, -0.89137765f, -0.89432512f,
-0.89723429f, -0.90010502f, -0.90293719f, -0.90573069f, -0.90848539f,
-0.91120117f, -0.91387791f, -0.91651551f, -0.91911385f, -0.92167282f,
-0.92419231f, -0.92667220f, -0.92911240f, -0.93151280f, -0.93387330f,
-0.93619380f, -0.93847419f, -0.94071438f, -0.94291427f, -0.94507377f,
-0.94719279f, -0.94927123f, -0.95130901f, -0.95330604f, -0.95526223f,
-0.95717750f, -0.95905177f, -0.96088496f, -0.96267699f, -0.96442777f,
-0.96613725f, -0.96780534f, -0.96943197f, -0.97101707f, -0.97256058f,
-0.97406243f, -0.97552255f, -0.97694089f, -0.97831737f, -0.97965195f,
-0.98094456f, -0.98219515f, -0.98340367f, -0.98457006f, -0.98569428f,
-0.98677627f, -0.98781599f, -0.98881340f, -0.98976845f, -0.99068110f,
-0.99155132f, -0.99237906f, -0.99316428f, -0.99390697f, -0.99460708f,
-0.99526458f, -0.99587945f, -0.99645166f, -0.99698119f, -0.99746801f,
-0.99791210f, -0.99831344f, -0.99867202f, -0.99898782f, -0.99926082f,
-0.99949102f, -0.99967841f, -0.99982297f, -0.99992470f, -0.99998360f,
0.99999866f, 0.99989157f, 0.99961315f, 0.99916346f, 0.99854256f,
0.99775057f, 0.99678762f, 0.99565388f, 0.99434953f, 0.99287481f,
0.99122996f, 0.98941527f, 0.98743105f, 0.98527764f, 0.98295541f,
0.98046475f, 0.97780610f, 0.97497990f, 0.97198664f, 0.96882685f,
0.96550104f, 0.96200980f, 0.95835373f, 0.95453345f, 0.95054962f,
0.94640291f, 0.94209404f, 0.93762375f, 0.93299280f, 0.92820199f,
0.92325213f, 0.91814408f, 0.91287871f, 0.90745693f, 0.90187965f,
0.89614785f, 0.89026249f, 0.88422459f, 0.87803519f, 0.87169533f,
0.86520612f, 0.85856866f, 0.85178409f, 0.84485357f, 0.83777828f,
0.83055945f, 0.82319831f, 0.81569611f, 0.80805415f, 0.80027373f,
0.79235620f, 0.78430289f, 0.77611520f, 0.76779452f, 0.75934229f,
0.75075995f, 0.74204897f, 0.73321084f, 0.72424708f, 0.71515923f,
0.70594883f, 0.69661748f, 0.68716676f, 0.67759830f, 0.66791374f,
0.65811474f, 0.64820297f, 0.63818013f, 0.62804795f, 0.61780815f,
0.60746249f, 0.59701275f, 0.58646072f, 0.57580819f, 0.56505701f,
0.55420900f, 0.54326604f, 0.53222998f, 0.52110274f, 0.50988620f,
0.49858230f, 0.48719297f, 0.47572016f, 0.46416584f, 0.45253199f,
0.44082059f, 0.42903367f, 0.41717323f, 0.40524131f, 0.39323996f,
0.38117123f, 0.36903718f, 0.35683990f, 0.34458148f, 0.33226402f,
0.31988962f, 0.30746042f, 0.29497853f, 0.28244610f, 0.26986527f,
0.25723821f, 0.24456706f, 0.23185402f, 0.21910124f, 0.20631092f,
0.19348526f, 0.18062644f, 0.16773667f, 0.15481816f, 0.14187312f,
0.12890377f, 0.11591234f, 0.10290104f, 0.089872115f, 0.076827789f,
0.063770300f, 0.050701883f, 0.037624779f, 0.024541229f, 0.011453473f,
-0.0016362454f, -0.014725683f, -0.027812598f, -0.040894747f, -0.053969889f,
-0.067035784f, -0.080090192f, -0.093130877f, -0.10615561f, -0.11916214f,
-0.13214826f, -0.14511174f, -0.15805036f, -0.17096189f, -0.18384413f,
-0.19669487f, -0.20951190f, -0.22229304f, -0.23503609f, -0.24773886f,
-0.26039919f, -0.27301490f, -0.28558383f, -0.29810383f, -0.31057274f,
-0.32298845f, -0.33534881f, -0.34765171f, -0.35989504f, -0.37207670f,
-0.38419461f, -0.39624670f, -0.40823088f, -0.42014512f, -0.43198737f,
-0.44375560f, -0.45544780f, -0.46706195f, -0.47859608f, -0.49004821f,
-0.50141636f, -0.51269860f, -0.52389299f, -0.53499762f, -0.54601058f,
-0.55692998f, -0.56775395f, -0.57848064f, -0.58910822f, -0.59963485f,
-0.61005873f, -0.62037809f, -0.63059115f, -0.64069616f, -0.65069139f,
-0.66057513f, -0.67034568f, -0.68000137f, -0.68954054f, -0.69896157f,
-0.70826283f, -0.71744274f, -0.72649972f, -0.73543221f, -0.74423869f,
-0.75291765f, -0.76146760f, -0.76988708f, -0.77817464f, -0.78632887f,
-0.79434836f, -0.80223175f, -0.80997767f, -0.81758481f, -0.82505187f,
-0.83237755f, -0.83956061f, -0.84659981f, -0.85349396f, -0.86024186f,
-0.86684237f, -0.87329434f, -0.87959669f, -0.88574831f, -0.89174817f,
-0.89759523f, -0.90328850f, -0.90882699f, -0.91420976f, -0.91943588f,
-0.92450446f, -0.92941463f, -0.93416555f, -0.93875641f, -0.94318642f,
-0.94745482f, -0.95156087f, -0.95550388f, -0.95928317f, -0.96289809f,
-0.96634802f, -0.96963238f, -0.97275059f, -0.97570213f, -0.97848649f,
-0.98110318f, -0.98355177f, -0.98583184f, -0.98794298f, -0.98988485f,
-0.99165711f, -0.99325945f, -0.99469160f, -0.99595331f, -0.99704438f,
-0.99796460f, -0.99871383f, -0.99929194f, -0.99969882f, -0.99993441f,
0.99999465f, 0.99956631f, 0.99845292f, 0.99665524f, 0.99417450f,
0.99101241f, 0.98717112f, 0.98265328f, 0.97746197f, 0.97160077f,
0.96507367f, 0.95788516f, 0.95004017f, 0.94154407f, 0.93240267f,
0.92262226f, 0.91220953f, 0.90117161f, 0.88951608f, 0.87725091f,
0.86438452f, 0.85092573f, 0.83688375f, 0.82226822f, 0.80708914f,
0.79135693f, 0.77508236f, 0.75827658f, 0.74095113f, 0.72311786f,
0.70478900f, 0.68597711f, 0.66669509f, 0.64695615f, 0.62677382f,
0.60616193f, 0.58513461f, 0.56370626f, 0.54189158f, 0.51970551f,
0.49716327f, 0.47428029f, 0.45107226f, 0.42755509f, 0.40374490f,
0.37965800f, 0.35531090f, 0.33072029f, 0.30590302f, 0.28087610f,
0.25565668f, 0.23026205f, 0.20470960f, 0.17901686f, 0.15320143f,
0.12728100f, 0.10127334f, 0.075196277f, 0.049067674f, 0.022905443f,
-0.0032724865f, -0.029448173f, -0.055603678f, -0.081721074f, -0.10778246f,
-0.13376998f, -0.15966582f, -0.18545224f, -0.21111155f, -0.23662618f,
-0.26197864f, -0.28715155f, -0.31212766f, -0.33688985f, -0.36142116f,
-0.38570477f, -0.40972403f, -0.43346249f, -0.45690388f, -0.48003212f,
-0.50283138f, -0.52528602f, -0.54738066f, -0.56910015f, -0.59042960f,
-0.61135441f, -0.63186022f, -0.65193299f, -0.67155895f, -0.69072467f,
-0.70941699f, -0.72762312f, -0.74533057f, -0.76252720f, -0.77920124f,
-0.79534126f, -0.81093618f, -0.82597533f, -0.84044840f, -0.85434547f,
-0.86765701f, -0.88037390f, -0.89248743f, -0.90398929f, -0.91487161f,
-0.92512691f, -0.93474818f, -0.94372882f, -0.95206268f, -0.95974404f,
-0.96676764f, -0.97312866f, -0.97882275f, -0.98384601f, -0.98819498f,
-0.99186670f, -0.99485864f, -0.99716875f, -0.99879546f, -0.99973764f,
};

static const float fl_eMeans[25] = {
      6.437500f, 6.250000f, 5.750000f, 5.312500f, 5.062500f,
      4.812500f, 4.500000f, 4.375000f, 4.875000f, 4.687500f,
      4.562500f, 4.437500f, 4.875000f, 4.625000f, 4.312500f,
      4.500000f, 4.375000f, 4.625000f, 4.750000f, 4.437500f,
      3.750000f, 3.750000f, 3.750000f, 3.750000f, 3.750000f
};

static const fl_u8 fl_e_prob_model[4][2][42] = {
   {
      {
          72, 127, 65, 129, 66, 128, 65, 128, 64, 128, 62, 128, 64, 128,
          64, 128, 92, 78, 92, 79, 92, 78, 90, 79, 116, 41, 115, 40,
         114, 40, 132, 26, 132, 26, 145, 17, 161, 12, 176, 10, 177, 11
      },
      {
          24, 179, 48, 138, 54, 135, 54, 132, 53, 134, 56, 133, 55, 132,
          55, 132, 61, 114, 70, 96, 74, 88, 75, 88, 87, 74, 89, 66,
          91, 67, 100, 59, 108, 50, 120, 40, 122, 37, 97, 43, 78, 50
      }
   },
   {
      {
          83, 78, 84, 81, 88, 75, 86, 74, 87, 71, 90, 73, 93, 74,
          93, 74, 109, 40, 114, 36, 117, 34, 117, 34, 143, 17, 145, 18,
         146, 19, 162, 12, 165, 10, 178, 7, 189, 6, 190, 8, 177, 9
      },
      {
          23, 178, 54, 115, 63, 102, 66, 98, 69, 99, 74, 89, 71, 91,
          73, 91, 78, 89, 86, 80, 92, 66, 93, 64, 102, 59, 103, 60,
         104, 60, 117, 52, 123, 44, 138, 35, 133, 31, 97, 38, 77, 45
      }
   },
   {
      {
          61, 90, 93, 60, 105, 42, 107, 41, 110, 45, 116, 38, 113, 38,
         112, 38, 124, 26, 132, 27, 136, 19, 140, 20, 155, 14, 159, 16,
         158, 18, 170, 13, 177, 10, 187, 8, 192, 6, 175, 9, 159, 10
      },
      {
          21, 178, 59, 110, 71, 86, 75, 85, 84, 83, 91, 66, 88, 73,
          87, 72, 92, 75, 98, 72, 105, 58, 107, 54, 115, 52, 114, 55,
         112, 56, 129, 51, 132, 40, 150, 33, 140, 29, 98, 35, 77, 42
      }
   },
   {
      {
          42, 121, 96, 66, 108, 43, 111, 40, 117, 44, 123, 32, 120, 36,
         119, 33, 127, 33, 134, 34, 139, 21, 147, 23, 152, 20, 158, 25,
         154, 26, 166, 21, 173, 16, 184, 13, 184, 10, 150, 13, 139, 15
      },
      {
          22, 178, 63, 114, 74, 82, 84, 83, 92, 82, 103, 62, 96, 72,
          96, 67, 101, 73, 107, 72, 113, 55, 118, 52, 125, 52, 118, 52,
         117, 55, 135, 49, 137, 39, 157, 32, 145, 29, 97, 33, 77, 40
      }
   }
};

static const fl_u8 fl_log2_frac_table[24] = {
   0,
   8,13,
  16,19,21,23,
  24,26,27,28,29,30,31,32,
  32,33,34,34,35,36,36,37,37
};

static const fl_u32 fl_pvq_u_data[1272] = {

  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41,
  43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79,
  81, 83, 85, 87, 89, 91, 93, 95, 97, 99, 101, 103, 105, 107, 109, 111, 113,
  115, 117, 119, 121, 123, 125, 127, 129, 131, 133, 135, 137, 139, 141, 143,
  145, 147, 149, 151, 153, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173,
  175, 177, 179, 181, 183, 185, 187, 189, 191, 193, 195, 197, 199, 201, 203,
  205, 207, 209, 211, 213, 215, 217, 219, 221, 223, 225, 227, 229, 231, 233,
  235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255, 257, 259, 261, 263,
  265, 267, 269, 271, 273, 275, 277, 279, 281, 283, 285, 287, 289, 291, 293,
  295, 297, 299, 301, 303, 305, 307, 309, 311, 313, 315, 317, 319, 321, 323,
  325, 327, 329, 331, 333, 335, 337, 339, 341, 343, 345, 347, 349, 351,
  13, 25, 41, 61, 85, 113, 145, 181, 221, 265, 313, 365, 421, 481, 545, 613,
  685, 761, 841, 925, 1013, 1105, 1201, 1301, 1405, 1513, 1625, 1741, 1861,
  1985, 2113, 2245, 2381, 2521, 2665, 2813, 2965, 3121, 3281, 3445, 3613, 3785,
  3961, 4141, 4325, 4513, 4705, 4901, 5101, 5305, 5513, 5725, 5941, 6161, 6385,
  6613, 6845, 7081, 7321, 7565, 7813, 8065, 8321, 8581, 8845, 9113, 9385, 9661,
  9941, 10225, 10513, 10805, 11101, 11401, 11705, 12013, 12325, 12641, 12961,
  13285, 13613, 13945, 14281, 14621, 14965, 15313, 15665, 16021, 16381, 16745,
  17113, 17485, 17861, 18241, 18625, 19013, 19405, 19801, 20201, 20605, 21013,
  21425, 21841, 22261, 22685, 23113, 23545, 23981, 24421, 24865, 25313, 25765,
  26221, 26681, 27145, 27613, 28085, 28561, 29041, 29525, 30013, 30505, 31001,
  31501, 32005, 32513, 33025, 33541, 34061, 34585, 35113, 35645, 36181, 36721,
  37265, 37813, 38365, 38921, 39481, 40045, 40613, 41185, 41761, 42341, 42925,
  43513, 44105, 44701, 45301, 45905, 46513, 47125, 47741, 48361, 48985, 49613,
  50245, 50881, 51521, 52165, 52813, 53465, 54121, 54781, 55445, 56113, 56785,
  57461, 58141, 58825, 59513, 60205, 60901, 61601,
  63, 129, 231, 377, 575, 833, 1159, 1561, 2047, 2625, 3303, 4089, 4991, 6017,
  7175, 8473, 9919, 11521, 13287, 15225, 17343, 19649, 22151, 24857, 27775,
  30913, 34279, 37881, 41727, 45825, 50183, 54809, 59711, 64897, 70375, 76153,
  82239, 88641, 95367, 102425, 109823, 117569, 125671, 134137, 142975, 152193,
  161799, 171801, 182207, 193025, 204263, 215929, 228031, 240577, 253575,
  267033, 280959, 295361, 310247, 325625, 341503, 357889, 374791, 392217,
  410175, 428673, 447719, 467321, 487487, 508225, 529543, 551449, 573951,
  597057, 620775, 645113, 670079, 695681, 721927, 748825, 776383, 804609,
  833511, 863097, 893375, 924353, 956039, 988441, 1021567, 1055425, 1090023,
  1125369, 1161471, 1198337, 1235975, 1274393, 1313599, 1353601, 1394407,
  1436025, 1478463, 1521729, 1565831, 1610777, 1656575, 1703233, 1750759,
  1799161, 1848447, 1898625, 1949703, 2001689, 2054591, 2108417, 2163175,
  2218873, 2275519, 2333121, 2391687, 2451225, 2511743, 2573249, 2635751,
  2699257, 2763775, 2829313, 2895879, 2963481, 3032127, 3101825, 3172583,
  3244409, 3317311, 3391297, 3466375, 3542553, 3619839, 3698241, 3777767,
  3858425, 3940223, 4023169, 4107271, 4192537, 4278975, 4366593, 4455399,
  4545401, 4636607, 4729025, 4822663, 4917529, 5013631, 5110977, 5209575,
  5309433, 5410559, 5512961, 5616647, 5721625, 5827903, 5935489, 6044391,
  6154617, 6266175, 6379073, 6493319, 6608921, 6725887, 6844225, 6963943,
  7085049, 7207551,
  321, 681, 1289, 2241, 3649, 5641, 8361, 11969, 16641, 22569, 29961, 39041,
  50049, 63241, 78889, 97281, 118721, 143529, 172041, 204609, 241601, 283401,
  330409, 383041, 441729, 506921, 579081, 658689, 746241, 842249, 947241,
  1061761, 1186369, 1321641, 1468169, 1626561, 1797441, 1981449, 2179241,
  2391489, 2618881, 2862121, 3121929, 3399041, 3694209, 4008201, 4341801,
  4695809, 5071041, 5468329, 5888521, 6332481, 6801089, 7295241, 7815849,
  8363841, 8940161, 9545769, 10181641, 10848769, 11548161, 12280841, 13047849,
  13850241, 14689089, 15565481, 16480521, 17435329, 18431041, 19468809,
  20549801, 21675201, 22846209, 24064041, 25329929, 26645121, 28010881,
  29428489, 30899241, 32424449, 34005441, 35643561, 37340169, 39096641,
  40914369, 42794761, 44739241, 46749249, 48826241, 50971689, 53187081,
  55473921, 57833729, 60268041, 62778409, 65366401, 68033601, 70781609,
  73612041, 76526529, 79526721, 82614281, 85790889, 89058241, 92418049,
  95872041, 99421961, 103069569, 106816641, 110664969, 114616361, 118672641,
  122835649, 127107241, 131489289, 135983681, 140592321, 145317129, 150160041,
  155123009, 160208001, 165417001, 170752009, 176215041, 181808129, 187533321,
  193392681, 199388289, 205522241, 211796649, 218213641, 224775361, 231483969,
  238341641, 245350569, 252512961, 259831041, 267307049, 274943241, 282741889,
  290705281, 298835721, 307135529, 315607041, 324252609, 333074601, 342075401,
  351257409, 360623041, 370174729, 379914921, 389846081, 399970689, 410291241,
  420810249, 431530241, 442453761, 453583369, 464921641, 476471169, 488234561,
  500214441, 512413449, 524834241, 537479489, 550351881, 563454121, 576788929,
  590359041, 604167209, 618216201, 632508801,
  1683, 3653, 7183, 13073, 22363, 36365, 56695, 85305, 124515, 177045, 246047,
  335137, 448427, 590557, 766727, 982729, 1244979, 1560549, 1937199, 2383409,
  2908411, 3522221, 4235671, 5060441, 6009091, 7095093, 8332863, 9737793,
  11326283, 13115773, 15124775, 17372905, 19880915, 22670725, 25765455,
  29189457, 32968347, 37129037, 41699767, 46710137, 52191139, 58175189,
  64696159, 71789409, 79491819, 87841821, 96879431, 106646281, 117185651,
  128542501, 140763503, 153897073, 167993403, 183104493, 199284183, 216588185,
  235074115, 254801525, 275831935, 298228865, 322057867, 347386557, 374284647,
  402823977, 433078547, 465124549, 499040399, 534906769, 572806619, 612825229,
  655050231, 699571641, 746481891, 795875861, 847850911, 902506913, 959946283,
  1020274013, 1083597703, 1150027593, 1219676595, 1292660325, 1369097135,
  1449108145, 1532817275, 1620351277, 1711839767, 1807415257, 1907213187,
  2011371957, 2120032959,
  8989, 19825, 40081, 75517, 134245, 227305, 369305, 579125, 880685, 1303777,
  1884961, 2668525, 3707509, 5064793, 6814249, 9041957, 11847485, 15345233,
  19665841, 24957661, 31388293, 39146185, 48442297, 59511829, 72616013,
  88043969, 106114625, 127178701, 151620757, 179861305, 212358985, 249612805,
  292164445, 340600625, 395555537, 457713341, 527810725, 606639529, 695049433,
  793950709, 904317037, 1027188385, 1163673953, 1314955181, 1482288821,
  1667010073, 1870535785, 2094367717,
  48639, 108545, 224143, 433905, 795455, 1392065, 2340495, 3800305, 5984767,
  9173505, 13726991, 20103025, 28875327, 40754369, 56610575, 77500017,
  104692735, 139703809, 184327311, 240673265, 311207743, 398796225, 506750351,
  638878193, 799538175, 993696769, 1226990095, 1505789553, 1837271615,
  2229491905U,
  265729, 598417, 1256465, 2485825, 4673345, 8405905, 14546705, 24331777,
  39490049, 62390545, 96220561, 145198913, 214828609, 312193553, 446304145,
  628496897, 872893441, 1196924561, 1621925137, 2173806145U,
  1462563, 3317445, 7059735, 14218905, 27298155, 50250765, 89129247, 152951073,
  254831667, 413442773, 654862247, 1014889769, 1541911931, 2300409629U,
  3375210671U,
  8097453, 18474633, 39753273, 81270333, 158819253, 298199265, 540279585,
  948062325, 1616336765,
  45046719, 103274625, 224298231, 464387817, 921406335, 1759885185,
  3248227095U,
  251595969, 579168825, 1267854873, 2653649025U,
  1409933619
};

/* ======== celt_common.c ======== */
/* ---- CELT: mode, bit allocation, Laplace and PVQ coding (integer parts) ---- */
#define CELT_NB_EBANDS 21
#define CELT_OVERLAP 120
#define CELT_SHORT_MDCT 120
#define CELT_MAX_LM 3
#define MAX_PERIOD 1024
#define DEC_PITCH_BUF_SIZE 2048
#define COMBFILTER_MINPERIOD 15
#define CELT_LPC_ORDER 24
#define SPREAD_NONE 0
#define SPREAD_LIGHT 1
#define SPREAD_NORMAL 2
#define SPREAD_AGGRESSIVE 3
#define MAX_FINE_BITS 8
#define FINE_OFFSET 21
#define QTHETA_OFFSET 4
#define QTHETA_OFFSET_TWOPHASE 16
#define LOG_MAX_PSEUDO 6
#define ALLOC_STEPS 6

static const unsigned char fl_trim_icdf[11] = {126, 124, 119, 109, 87, 41, 19, 9, 4, 2, 0};
static const unsigned char fl_spread_icdf[4] = {25, 23, 2, 0};
static const unsigned char fl_tapset_icdf[3] = {2, 1, 0};
static const signed char fl_tf_select_table[4][8] = {
   {0, -1, 0, -1, 0,-1, 0,-1}, {0, -1, 0, -2, 1, 0, 1,-1},
   {0, -2, 0, -3, 2, 0, 1,-1}, {0, -2, 0, -3, 3, 0, 1,-1}};
static const float fl_preemph[4] = {0.85000610f, 0.0f, 1.0f, 1.0f};

typedef struct {
   int nfft;
   float scale;
   int shift;
   fl_i16 factors[16];
   const fl_i16 *bitrev;
} fl_fft_state;

static const fl_fft_state fl_fft_states[4] = {
   {480, 0.0020833334f, -1, {5, 96, 3, 32, 4, 8, 2, 4, 4, 1, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev480},
   {240, 0.0041666669f, 1, {5, 48, 3, 16, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev240},
   {120, 0.0083333338f, 2, {5, 24, 3, 8, 2, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev120},
   {60, 0.016666668f, 3, {5, 12, 3, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev60}};

static const fl_u32 *const fl_pvq_u_row[15] = {
   fl_pvq_u_data+0, fl_pvq_u_data+176, fl_pvq_u_data+351, fl_pvq_u_data+525, fl_pvq_u_data+698,
   fl_pvq_u_data+870, fl_pvq_u_data+1041, fl_pvq_u_data+1131, fl_pvq_u_data+1178, fl_pvq_u_data+1207,
   fl_pvq_u_data+1226, fl_pvq_u_data+1240, fl_pvq_u_data+1248, fl_pvq_u_data+1254, fl_pvq_u_data+1257};
#define CELT_PVQ_U(n,k) (fl_pvq_u_row[IMIN(n,k)][IMAX(n,k)])
#define CELT_PVQ_V(n,k) (CELT_PVQ_U(n,k)+CELT_PVQ_U(n,(k)+1))

/* FLOAT_APPROX versions of log2/exp2 (libopus default on x86). */
static const float fl_log2_x_norm_coeff[8] = {
   1.000000000000000000000000000f, 8.88888895511627197265625e-01f,
   8.00000000000000000000000e-01f, 7.27272748947143554687500e-01f,
   6.66666686534881591796875e-01f, 6.15384638309478759765625e-01f,
   5.71428596973419189453125e-01f, 5.33333361148834228515625e-01f};
static const float fl_log2_y_norm_coeff[8] = {
   0.0000000000000000000000000000f, 1.699250042438507080078125e-01f,
   3.219280838966369628906250e-01f, 4.594316184520721435546875e-01f,
   5.849624872207641601562500e-01f, 7.004396915435791015625000e-01f,
   8.073549270629882812500000e-01f, 9.068905711174011230468750e-01f};
static float fl_celt_log2(float x) {
   fl_i32 integer, range_idx;
   union { float f; fl_u32 i; } in;
   in.f = x;
   integer = (fl_i32)(in.i>>23)-127;
   in.i = (fl_i32)in.i-(fl_i32)((fl_u32)integer<<23);
   range_idx = (in.i>>20)&0x7;
   in.f = in.f*fl_log2_x_norm_coeff[range_idx]-1.0625f;
   in.f = 8.74628424644470214843750000e-02f+in.f*(1.357829570770263671875000000000f
         +in.f*(-6.3897705078125000000000000e-01f
         +in.f*(4.01971250772476196289062500e-01f
         +in.f*(-2.8415444493293762207031250e-01f))));
   return integer+in.f+fl_log2_y_norm_coeff[range_idx];
}
static float fl_celt_exp2(float x) {
   fl_i32 integer;
   float frac;
   union { float f; fl_u32 i; } res;
   integer = (int)floor(x);
   if (integer < -50) return 0;
   frac = x-integer;
   res.f = 9.999999403953552246093750000000e-01f+frac*(6.931530833244323730468750000000e-01f
         +frac*(2.401536107063293457031250000000e-01f
         +frac*(5.582631751894950866699218750000e-02f
         +frac*(8.989339694380760192871093750000e-03f
         +frac*(1.877576694823801517486572265625e-03f)))));
   res.i = (fl_u32)((fl_i32)res.i+(fl_i32)((fl_u32)integer<<23))&0x7fffffff;
   return res.f;
}

static fl_u32 fl_lcg_rand(fl_u32 seed) { return 1664525*seed+1013904223; }

static void fl_init_caps(int *cap, int LM, int C) {
   int i;
   for (i = 0; i < CELT_NB_EBANDS; i++) {
      int N = (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM;
      cap[i] = (fl_cache_caps50[CELT_NB_EBANDS*(2*LM+C-1)+i]+64)*C*N>>2;
   }
}

static int fl_get_pulses(int i) { return i < 8 ? i : (8+(i&7))<<((i>>3)-1); }

static int fl_bits2pulses(int band, int LM, int bits) {
   int i, lo, hi;
   const unsigned char *cache;
   LM++;
   cache = fl_cache_bits50+fl_cache_index50[LM*CELT_NB_EBANDS+band];
   lo = 0;
   hi = cache[0];
   bits--;
   for (i = 0; i < LOG_MAX_PSEUDO; i++) {
      int mid = (lo+hi+1)>>1;
      if ((int)cache[mid] >= bits) hi = mid;
      else lo = mid;
   }
   if (bits-(lo == 0 ? -1 : (int)cache[lo]) <= (int)cache[hi]-bits) return lo;
   return hi;
}

static int fl_pulses2bits(int band, int LM, int pulses) {
   const unsigned char *cache;
   LM++;
   cache = fl_cache_bits50+fl_cache_index50[LM*CELT_NB_EBANDS+band];
   return pulses == 0 ? 0 : cache[pulses]+1;
}

static int fl_interp_bits2pulses(int start, int end, int skip_start, const int *bits1, const int *bits2,
      const int *thresh, const int *cap, fl_i32 total, fl_i32 *_balance, int skip_rsv, int *intensity,
      int intensity_rsv, int *dual_stereo, int dual_stereo_rsv, int *bits, int *ebits, int *fine_priority,
      int C, int LM, ec_ctx *ec, int encode, int prev, int signalBandwidth) {
   const fl_i16 *eb = fl_eband5ms;
   fl_i32 psum, left, percoeff, balance;
   int lo, hi, i, j, logM, stereo, codedBands = -1, alloc_floor, done;
   alloc_floor = C<<BITRES;
   stereo = C > 1;
   logM = LM<<BITRES;
   lo = 0;
   hi = 1<<ALLOC_STEPS;
   for (i = 0; i < ALLOC_STEPS; i++) {
      int mid = (lo+hi)>>1;
      psum = 0;
      done = 0;
      for (j = end; j-- > start;) {
         int tmp = bits1[j]+(mid*(fl_i32)bits2[j]>>ALLOC_STEPS);
         if (tmp >= thresh[j] || done) {
            done = 1;
            psum += IMIN(tmp, cap[j]);
         } else if (tmp >= alloc_floor) psum += alloc_floor;
      }
      if (psum > total) hi = mid;
      else lo = mid;
   }
   psum = 0;
   done = 0;
   for (j = end; j-- > start;) {
      int tmp = bits1[j]+((fl_i32)lo*bits2[j]>>ALLOC_STEPS);
      if (tmp < thresh[j] && !done) {
         if (tmp >= alloc_floor) tmp = alloc_floor;
         else tmp = 0;
      } else done = 1;
      tmp = IMIN(tmp, cap[j]);
      bits[j] = tmp;
      psum += tmp;
   }
   for (codedBands = end;; codedBands--) {
      int band_width, band_bits, rem;
      j = codedBands-1;
      if (j <= skip_start) {
         total += skip_rsv;
         break;
      }
      left = total-psum;
      percoeff = (fl_i32)((fl_u32)left/(fl_u32)(eb[codedBands]-eb[start]));
      left -= (eb[codedBands]-eb[start])*percoeff;
      rem = IMAX(left-(eb[j]-eb[start]), 0);
      band_width = eb[codedBands]-eb[j];
      band_bits = (int)(bits[j]+percoeff*band_width+rem);
      if (band_bits >= IMAX(thresh[j], alloc_floor+(1<<BITRES))) {
         if (encode) {
            int depth_threshold;
            if (codedBands > 17) depth_threshold = j < prev ? 7 : 9;
            else depth_threshold = 0;
            if (codedBands <= start+2 || (band_bits > (depth_threshold*band_width<<LM<<BITRES)>>4 && j <= signalBandwidth)) {
               ec_enc_bit_logp(ec, 1, 1);
               break;
            }
            ec_enc_bit_logp(ec, 0, 1);
         } else if (ec_dec_bit_logp(ec, 1)) break;
         psum += 1<<BITRES;
         band_bits -= 1<<BITRES;
      }
      psum -= bits[j]+intensity_rsv;
      if (intensity_rsv > 0) intensity_rsv = fl_log2_frac_table[j-start];
      psum += intensity_rsv;
      if (band_bits >= alloc_floor) {
         psum += alloc_floor;
         bits[j] = alloc_floor;
      } else bits[j] = 0;
   }
   if (intensity_rsv > 0) {
      if (encode) {
         *intensity = IMIN(*intensity, codedBands);
         ec_enc_uint(ec, *intensity-start, codedBands+1-start);
      } else *intensity = start+ec_dec_uint(ec, codedBands+1-start);
   } else *intensity = 0;
   if (*intensity <= start) {
      total += dual_stereo_rsv;
      dual_stereo_rsv = 0;
   }
   if (dual_stereo_rsv > 0) {
      if (encode) ec_enc_bit_logp(ec, *dual_stereo, 1);
      else *dual_stereo = ec_dec_bit_logp(ec, 1);
   } else *dual_stereo = 0;
   left = total-psum;
   percoeff = (fl_i32)((fl_u32)left/(fl_u32)(eb[codedBands]-eb[start]));
   left -= (eb[codedBands]-eb[start])*percoeff;
   for (j = start; j < codedBands; j++) bits[j] += ((int)percoeff*(eb[j+1]-eb[j]));
   for (j = start; j < codedBands; j++) {
      int tmp = (int)IMIN(left, eb[j+1]-eb[j]);
      bits[j] += tmp;
      left -= tmp;
   }
   balance = 0;
   for (j = start; j < codedBands; j++) {
      int N0, N, den, offset, NClogN;
      fl_i32 excess, bit;
      N0 = eb[j+1]-eb[j];
      N = N0<<LM;
      bit = (fl_i32)bits[j]+balance;
      if (N > 1) {
         excess = FL_MAX(bit-cap[j], 0);
         bits[j] = bit-excess;
         den = (C*N+((C == 2 && N > 2 && !*dual_stereo && j < *intensity) ? 1 : 0));
         NClogN = den*(fl_logN400[j]+logM);
         offset = (NClogN>>1)-den*FINE_OFFSET;
         if (N == 2) offset += den<<BITRES>>2;
         if (bits[j]+offset < den*2<<BITRES) offset += NClogN>>2;
         else if (bits[j]+offset < den*3<<BITRES) offset += NClogN>>3;
         ebits[j] = IMAX(0, (bits[j]+offset+(den<<(BITRES-1))));
         ebits[j] = (int)((fl_u32)ebits[j]/(fl_u32)den)>>BITRES;
         if (C*ebits[j] > (bits[j]>>BITRES)) ebits[j] = bits[j]>>stereo>>BITRES;
         ebits[j] = IMIN(ebits[j], MAX_FINE_BITS);
         fine_priority[j] = ebits[j]*(den<<BITRES) >= bits[j]+offset;
         bits[j] -= C*ebits[j]<<BITRES;
      } else {
         excess = FL_MAX(0, bit-(C<<BITRES));
         bits[j] = bit-excess;
         ebits[j] = 0;
         fine_priority[j] = 1;
      }
      if (excess > 0) {
         int extra_fine, extra_bits;
         extra_fine = IMIN(excess>>(stereo+BITRES), MAX_FINE_BITS-ebits[j]);
         ebits[j] += extra_fine;
         extra_bits = extra_fine*C<<BITRES;
         fine_priority[j] = extra_bits >= excess-balance;
         excess -= extra_bits;
      }
      balance = excess;
   }
   *_balance = balance;
   for (; j < end; j++) {
      ebits[j] = bits[j]>>stereo>>BITRES;
      bits[j] = 0;
      fine_priority[j] = ebits[j] < 1;
   }
   return codedBands;
}

static int fl_compute_allocation(int start, int end, const int *offsets, const int *cap, int alloc_trim,
      int *intensity, int *dual_stereo, fl_i32 total, fl_i32 *balance, int *pulses, int *ebits,
      int *fine_priority, int C, int LM, ec_ctx *ec, int encode, int prev, int signalBandwidth) {
   const fl_i16 *eb = fl_eband5ms;
   int lo, hi, len = CELT_NB_EBANDS, j, skip_start, skip_rsv, intensity_rsv, dual_stereo_rsv;
   int bits1[CELT_NB_EBANDS], bits2[CELT_NB_EBANDS], thresh[CELT_NB_EBANDS], trim_offset[CELT_NB_EBANDS];
   total = IMAX(total, 0);
   skip_start = start;
   skip_rsv = total >= 1<<BITRES ? 1<<BITRES : 0;
   total -= skip_rsv;
   intensity_rsv = dual_stereo_rsv = 0;
   if (C == 2) {
      intensity_rsv = fl_log2_frac_table[end-start];
      if (intensity_rsv > total) intensity_rsv = 0;
      else {
         total -= intensity_rsv;
         dual_stereo_rsv = total >= 1<<BITRES ? 1<<BITRES : 0;
         total -= dual_stereo_rsv;
      }
   }
   for (j = start; j < end; j++) {
      thresh[j] = IMAX((C)<<BITRES, (3*(eb[j+1]-eb[j])<<LM<<BITRES)>>4);
      trim_offset[j] = C*(eb[j+1]-eb[j])*(alloc_trim-5-LM)*(end-j-1)*(1<<(LM+BITRES))>>6;
      if ((eb[j+1]-eb[j])<<LM == 1) trim_offset[j] -= C<<BITRES;
   }
   lo = 1;
   hi = 11-1;
   do {
      int done = 0, psum = 0, mid = (lo+hi)>>1;
      for (j = end; j-- > start;) {
         int bitsj, N = eb[j+1]-eb[j];
         bitsj = C*N*fl_band_allocation[mid*len+j]<<LM>>2;
         if (bitsj > 0) bitsj = IMAX(0, bitsj+trim_offset[j]);
         bitsj += offsets[j];
         if (bitsj >= thresh[j] || done) {
            done = 1;
            psum += IMIN(bitsj, cap[j]);
         } else if (bitsj >= C<<BITRES) psum += C<<BITRES;
      }
      if (psum > total) hi = mid-1;
      else lo = mid+1;
   } while (lo <= hi);
   hi = lo--;
   for (j = start; j < end; j++) {
      int bits1j, bits2j, N = eb[j+1]-eb[j];
      bits1j = C*N*fl_band_allocation[lo*len+j]<<LM>>2;
      bits2j = hi >= 11 ? cap[j] : C*N*fl_band_allocation[hi*len+j]<<LM>>2;
      if (bits1j > 0) bits1j = IMAX(0, bits1j+trim_offset[j]);
      if (bits2j > 0) bits2j = IMAX(0, bits2j+trim_offset[j]);
      if (lo > 0) bits1j += offsets[j];
      bits2j += offsets[j];
      if (offsets[j] > 0) skip_start = j;
      bits2j = IMAX(0, bits2j-bits1j);
      bits1[j] = bits1j;
      bits2[j] = bits2j;
   }
   return fl_interp_bits2pulses(start, end, skip_start, bits1, bits2, thresh, cap, total, balance, skip_rsv,
         intensity, intensity_rsv, dual_stereo, dual_stereo_rsv, pulses, ebits, fine_priority, C, LM, ec,
         encode, prev, signalBandwidth);
}

/* Laplace coding of coarse energy */
#define LAPLACE_MINP 1
#define LAPLACE_NMIN 16
static unsigned fl_laplace_get_freq1(unsigned fs0, int decay) {
   unsigned ft = 32768-LAPLACE_MINP*(2*LAPLACE_NMIN)-fs0;
   return ft*(fl_i32)(16384-decay)>>15;
}
static void fl_laplace_encode(ec_ctx *enc, int *value, unsigned fs, int decay) {
   unsigned fl = 0;
   int val = *value;
   if (val) {
      int s, i;
      s = -(val < 0);
      val = (val+s)^s;
      fl = fs;
      fs = fl_laplace_get_freq1(fs, decay);
      for (i = 1; fs > 0 && i < val; i++) {
         fs *= 2;
         fl += fs+2*LAPLACE_MINP;
         fs = (fs*(fl_i32)decay)>>15;
      }
      if (!fs) {
         int di, ndi_max;
         ndi_max = (32768-fl+LAPLACE_MINP-1);
         ndi_max = (ndi_max-s)>>1;
         di = IMIN(val-i, ndi_max-1);
         fl += (2*di+1+s)*LAPLACE_MINP;
         fs = IMIN(LAPLACE_MINP, 32768-fl);
         *value = (i+di+s)^s;
      } else {
         fs += LAPLACE_MINP;
         fl += fs&~s;
      }
   }
   ec_encode_bin(enc, fl, fl+fs, 15);
}
static int fl_laplace_decode(ec_ctx *dec, unsigned fs, int decay) {
   int val = 0;
   unsigned fl, fm;
   fm = ec_decode_bin(dec, 15);
   fl = 0;
   if (fm >= fs) {
      val++;
      fl = fs;
      fs = fl_laplace_get_freq1(fs, decay)+LAPLACE_MINP;
      while (fs > LAPLACE_MINP && fm >= fl+2*fs) {
         fs *= 2;
         fl += fs;
         fs = ((fs-2*LAPLACE_MINP)*(fl_i32)decay)>>15;
         fs += LAPLACE_MINP;
         val++;
      }
      if (fs <= LAPLACE_MINP) {
         int di = (fm-fl)>>1;
         val += di;
         fl += 2*di*LAPLACE_MINP;
      }
      if (fm < fl+fs) val = -val;
      else fl += fs;
   }
   ec_dec_update(dec, fl, IMIN(fl+fs, 32768), 32768);
   return val;
}

/* PVQ codeword index <-> pulse vector */
static fl_u32 fl_icwrs(int n, const int *y) {
   fl_u32 i;
   int j, k;
   j = n-1;
   i = y[j] < 0;
   k = abs(y[j]);
   do {
      j--;
      i += CELT_PVQ_U(n-j, k);
      k += abs(y[j]);
      if (y[j] < 0) i += CELT_PVQ_U(n-j, k+1);
   } while (j > 0);
   return i;
}
static void fl_encode_pulses(const int *y, int n, int k, ec_ctx *enc) {
   ec_enc_uint(enc, fl_icwrs(n, y), CELT_PVQ_V(n, k));
}
/* Returns the squared norm of the decoded vector. */
static float fl_cwrsi(int n, int k, fl_u32 i, int *y) {
   fl_u32 p;
   int s, k0;
   fl_i16 val;
   float yy = 0;
   while (n > 2) {
      fl_u32 q;
      if (k >= n) {
         const fl_u32 *row = fl_pvq_u_row[n];
         p = row[k+1];
         s = -(i >= p);
         i -= p&s;
         k0 = k;
         q = row[n];
         if (q > i) {
            k = n;
            do p = fl_pvq_u_row[--k][n];
            while (p > i);
         } else for (p = row[k]; p > i; p = row[k]) k--;
         i -= p;
         val = (k0-k+s)^s;
         *y++ = val;
         yy = yy+(float)val*(float)val;
      } else {
         p = fl_pvq_u_row[k][n];
         q = fl_pvq_u_row[k+1][n];
         if (p <= i && i < q) {
            i -= p;
            *y++ = 0;
         } else {
            s = -(i >= q);
            i -= q&s;
            k0 = k;
            do p = fl_pvq_u_row[--k][n];
            while (p > i);
            i -= p;
            val = (k0-k+s)^s;
            *y++ = val;
            yy = yy+(float)val*(float)val;
         }
      }
      n--;
   }
   p = 2*k+1;
   s = -(i >= p);
   i -= p&s;
   k0 = k;
   k = (i+1)>>1;
   if (k) i -= 2*k-1;
   val = (k0-k+s)^s;
   *y++ = val;
   yy = yy+(float)val*(float)val;
   s = -(int)i;
   val = (k+s)^s;
   *y = val;
   yy = yy+(float)val*(float)val;
   return yy;
}
static float fl_decode_pulses(int *y, int n, int k, ec_ctx *dec) {
   return fl_cwrsi(n, k, ec_dec_uint(dec, CELT_PVQ_V(n, k)), y);
}

/* Integer helpers shared by the band coders */
static fl_i16 fl_bitexact_cos(fl_i16 x) {
   fl_i32 tmp;
   fl_i16 x2;
   tmp = (4096+((fl_i32)(x)*(x)))>>13;
   x2 = tmp;
   x2 = (32767-x2)+FL_FRAC_MUL16(x2, (-7651+FL_FRAC_MUL16(x2, (8277+FL_FRAC_MUL16(-626, x2)))));
   return 1+x2;
}
static int fl_bitexact_log2tan(int isin, int icos) {
   int lc = EC_ILOG(icos), ls = EC_ILOG(isin);
   icos <<= 15-lc;
   isin <<= 15-ls;
   return (ls-lc)*(1<<11)+FL_FRAC_MUL16(isin, FL_FRAC_MUL16(isin, -2597)+7932)
         -FL_FRAC_MUL16(icos, FL_FRAC_MUL16(icos, -2597)+7932);
}
static int fl_compute_qn(int N, int b, int offset, int pulse_cap, int stereo) {
   static const fl_i16 exp2_table8[8] = {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
   int qn, qb, N2 = 2*N-1;
   if (stereo && N == 2) N2--;
   qb = (b+N2*offset)/N2;
   qb = IMIN(b-pulse_cap-(4<<BITRES), qb);
   qb = IMIN(8<<BITRES, qb);
   if (qb < (1<<BITRES>>1)) qn = 1;
   else {
      qn = exp2_table8[qb&0x7]>>(14-(qb>>BITRES));
      qn = (qn+1)>>1<<1;
   }
   return qn;
}
static fl_u32 fl_isqrt32(fl_u32 val) {
   fl_u32 b, g = 0;
   int bshift = (EC_ILOG(val)-1)>>1;
   b = 1U<<bshift;
   do {
      fl_u32 t = (((fl_u32)g<<1)+b)<<bshift;
      if (t <= val) {
         g += b;
         val -= t;
      }
      b >>= 1;
      bshift--;
   } while (bshift >= 0);
   return g;
}
static unsigned fl_extract_collapse_mask(int *iy, int N, int B) {
   unsigned collapse_mask;
   int N0, i;
   if (B <= 1) return 1;
   N0 = N/B;
   collapse_mask = 0;
   i = 0;
   do {
      int j = 0;
      unsigned tmp = 0;
      do tmp |= iy[i*N0+j];
      while (++j < N0);
      collapse_mask |= (tmp != 0)<<i;
   } while (++i < B);
   return collapse_mask;
}

/* ======== celt_dsp.c ======== */
/* ---- CELT float DSP with SSE/AVX2 evaluation order (decoder side) ---- */

/* Exact fused multiply-add: a*b is exact in double, then round-to-odd for the sum. */
static float fl_fmaf(float a, float b, float c) {
   double p = (double)a*(double)b, s = p+(double)c, bb = s-p;
   double err = (p-(s-bb))+((double)c-bb);
   if (err != 0) {
      fl_u64 bits;
      memcpy(&bits, &s, 8);
      if ((bits&1) == 0) {
         if ((err > 0) == (s > 0)) bits++;
         else bits--;
         memcpy(&s, &bits, 8);
      }
   }
   return (float)s;
}

/* celt_inner_prod_sse: four lanes, then (l0+l2)+(l1+l3), then the tail. */
static float fl_inner_prod(const float *x, const float *y, int N) {
   float s0 = 0, s1 = 0, s2 = 0, s3 = 0, xy;
   int i;
   for (i = 0; i < N-3; i += 4) {
      s0 = s0+x[i]*y[i];
      s1 = s1+x[i+1]*y[i+1];
      s2 = s2+x[i+2]*y[i+2];
      s3 = s3+x[i+3]*y[i+3];
   }
   xy = (s0+s2)+(s1+s3);
   for (; i < N; i++) xy = xy+x[i]*y[i];
   return xy;
}

/* xcorr_kernel_sse */
static void fl_xcorr_kernel(const float *x, const float *y, float sum[4], int len) {
   float a[4], b[4] = {0, 0, 0, 0};
   int j, k;
   for (k = 0; k < 4; k++) a[k] = sum[k];
   for (j = 0; j < len-3; j += 4) {
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      for (k = 0; k < 4; k++) b[k] = b[k]+x[j+1]*y[j+1+k];
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j+2]*y[j+2+k];
      for (k = 0; k < 4; k++) b[k] = b[k]+x[j+3]*y[j+3+k];
   }
   if (j < len) {
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      if (++j < len) {
         for (k = 0; k < 4; k++) b[k] = b[k]+x[j]*y[j+k];
         if (++j < len)
            for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      }
   }
   for (k = 0; k < 4; k++) sum[k] = a[k]+b[k];
}

/* celt_pitch_xcorr_avx2: 8 FMA lanes per lag, then a fixed pairwise reduction. */
static void fl_pitch_xcorr(const float *x, const float *y, float *xcorr, int len, int max_pitch) {
   int i, k, l, j;
   for (i = 0; i < max_pitch-7; i += 8) {
      for (k = 0; k < 8; k++) {
         float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
         const float *yk = y+i+k;
         for (j = 0; j < len-7; j += 8)
            for (l = 0; l < 8; l++) acc[l] = fl_fmaf(x[j+l], yk[j+l], acc[l]);
         if (j != len)
            for (l = 0; l < len-j; l++) acc[l] = fl_fmaf(x[j+l], yk[j+l], acc[l]);
         xcorr[i+k] = ((acc[0]+acc[4])+(acc[1]+acc[5]))+((acc[2]+acc[6])+(acc[3]+acc[7]));
      }
   }
   for (; i < max_pitch; i++) xcorr[i] = fl_inner_prod(x, y+i, len);
}

/* ---- FFT (kiss_fft, float) ---- */
static void fl_kf_bfly2(fl_cpx *Fout, int m, int N) {
   const float tw = 0.7071067812f;
   int i;
   (void)m;
   for (i = 0; i < N; i++) {
      fl_cpx *Fout2 = Fout+4, t;
      t = Fout2[0];
      Fout2[0].r = Fout[0].r-t.r; Fout2[0].i = Fout[0].i-t.i;
      Fout[0].r += t.r; Fout[0].i += t.i;
      t.r = (Fout2[1].r+Fout2[1].i)*tw;
      t.i = (Fout2[1].i-Fout2[1].r)*tw;
      Fout2[1].r = Fout[1].r-t.r; Fout2[1].i = Fout[1].i-t.i;
      Fout[1].r += t.r; Fout[1].i += t.i;
      t.r = Fout2[2].i;
      t.i = -Fout2[2].r;
      Fout2[2].r = Fout[2].r-t.r; Fout2[2].i = Fout[2].i-t.i;
      Fout[2].r += t.r; Fout[2].i += t.i;
      t.r = (Fout2[3].i-Fout2[3].r)*tw;
      t.i = (-(Fout2[3].i+Fout2[3].r))*tw;
      Fout2[3].r = Fout[3].r-t.r; Fout2[3].i = Fout[3].i-t.i;
      Fout[3].r += t.r; Fout[3].i += t.i;
      Fout += 8;
   }
}
#define FL_CMUL(m,a,b) do { (m).r = (a).r*(b).r-(a).i*(b).i; (m).i = (a).r*(b).i+(a).i*(b).r; } while (0)
static void fl_kf_bfly4(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i;
   if (m == 1) {
      for (i = 0; i < N; i++) {
         fl_cpx s0, s1;
         s0.r = Fout[0].r-Fout[2].r; s0.i = Fout[0].i-Fout[2].i;
         Fout[0].r += Fout[2].r; Fout[0].i += Fout[2].i;
         s1.r = Fout[1].r+Fout[3].r; s1.i = Fout[1].i+Fout[3].i;
         Fout[2].r = Fout[0].r-s1.r; Fout[2].i = Fout[0].i-s1.i;
         Fout[0].r += s1.r; Fout[0].i += s1.i;
         s1.r = Fout[1].r-Fout[3].r; s1.i = Fout[1].i-Fout[3].i;
         Fout[1].r = s0.r+s1.i;
         Fout[1].i = s0.i-s1.r;
         Fout[3].r = s0.r-s1.i;
         Fout[3].i = s0.i+s1.r;
         Fout += 4;
      }
   } else {
      int j, m2 = 2*m, m3 = 3*m;
      fl_cpx s[6], *Fout_beg = Fout;
      for (i = 0; i < N; i++) {
         const fl_cpx *tw1, *tw2, *tw3;
         Fout = Fout_beg+i*mm;
         tw3 = tw2 = tw1 = fl_fft_twiddles48000_960;
         for (j = 0; j < m; j++) {
            FL_CMUL(s[0], Fout[m], *tw1);
            FL_CMUL(s[1], Fout[m2], *tw2);
            FL_CMUL(s[2], Fout[m3], *tw3);
            s[5].r = Fout->r-s[1].r; s[5].i = Fout->i-s[1].i;
            Fout->r += s[1].r; Fout->i += s[1].i;
            s[3].r = s[0].r+s[2].r; s[3].i = s[0].i+s[2].i;
            s[4].r = s[0].r-s[2].r; s[4].i = s[0].i-s[2].i;
            Fout[m2].r = Fout->r-s[3].r; Fout[m2].i = Fout->i-s[3].i;
            tw1 += fstride;
            tw2 += fstride*2;
            tw3 += fstride*3;
            Fout->r += s[3].r; Fout->i += s[3].i;
            Fout[m].r = s[5].r+s[4].i;
            Fout[m].i = s[5].i-s[4].r;
            Fout[m3].r = s[5].r-s[4].i;
            Fout[m3].i = s[5].i+s[4].r;
            ++Fout;
         }
      }
   }
}
static void fl_kf_bfly3(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i, k, m2 = 2*m;
   const fl_cpx *tw1, *tw2;
   fl_cpx s[5], epi3, *Fout_beg = Fout;
   epi3 = fl_fft_twiddles48000_960[fstride*m];
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      tw1 = tw2 = fl_fft_twiddles48000_960;
      k = m;
      do {
         FL_CMUL(s[1], Fout[m], *tw1);
         FL_CMUL(s[2], Fout[m2], *tw2);
         s[3].r = s[1].r+s[2].r; s[3].i = s[1].i+s[2].i;
         s[0].r = s[1].r-s[2].r; s[0].i = s[1].i-s[2].i;
         tw1 += fstride;
         tw2 += fstride*2;
         Fout[m].r = Fout->r-s[3].r*.5f;
         Fout[m].i = Fout->i-s[3].i*.5f;
         s[0].r *= epi3.i; s[0].i *= epi3.i;
         Fout->r += s[3].r; Fout->i += s[3].i;
         Fout[m2].r = Fout[m].r+s[0].i;
         Fout[m2].i = Fout[m].i-s[0].r;
         Fout[m].r = Fout[m].r-s[0].i;
         Fout[m].i = Fout[m].i+s[0].r;
         ++Fout;
      } while (--k);
   }
}
static void fl_kf_bfly5(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   fl_cpx *F0, *F1, *F2, *F3, *F4, s[13], ya, yb, *Fout_beg = Fout;
   const fl_cpx *tw = fl_fft_twiddles48000_960;
   int i, u;
   ya = tw[fstride*m];
   yb = tw[fstride*2*m];
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      F0 = Fout; F1 = F0+m; F2 = F0+2*m; F3 = F0+3*m; F4 = F0+4*m;
      for (u = 0; u < m; ++u) {
         s[0] = *F0;
         FL_CMUL(s[1], *F1, tw[u*fstride]);
         FL_CMUL(s[2], *F2, tw[2*u*fstride]);
         FL_CMUL(s[3], *F3, tw[3*u*fstride]);
         FL_CMUL(s[4], *F4, tw[4*u*fstride]);
         s[7].r = s[1].r+s[4].r; s[7].i = s[1].i+s[4].i;
         s[10].r = s[1].r-s[4].r; s[10].i = s[1].i-s[4].i;
         s[8].r = s[2].r+s[3].r; s[8].i = s[2].i+s[3].i;
         s[9].r = s[2].r-s[3].r; s[9].i = s[2].i-s[3].i;
         F0->r = F0->r+(s[7].r+s[8].r);
         F0->i = F0->i+(s[7].i+s[8].i);
         s[5].r = s[0].r+(s[7].r*ya.r+s[8].r*yb.r);
         s[5].i = s[0].i+(s[7].i*ya.r+s[8].i*yb.r);
         s[6].r = s[10].i*ya.i+s[9].i*yb.i;
         s[6].i = -(s[10].r*ya.i+s[9].r*yb.i);
         F1->r = s[5].r-s[6].r; F1->i = s[5].i-s[6].i;
         F4->r = s[5].r+s[6].r; F4->i = s[5].i+s[6].i;
         s[11].r = s[0].r+(s[7].r*yb.r+s[8].r*ya.r);
         s[11].i = s[0].i+(s[7].i*yb.r+s[8].i*ya.r);
         s[12].r = s[9].i*ya.i-s[10].i*yb.i;
         s[12].i = s[10].r*yb.i-s[9].r*ya.i;
         F2->r = s[11].r+s[12].r; F2->i = s[11].i+s[12].i;
         F3->r = s[11].r-s[12].r; F3->i = s[11].i-s[12].i;
         ++F0; ++F1; ++F2; ++F3; ++F4;
      }
   }
}
static void fl_fft_impl(const fl_fft_state *st, fl_cpx *fout) {
   int m2, m, p, L, i, shift, fstride[8];
   shift = st->shift > 0 ? st->shift : 0;
   fstride[0] = 1;
   L = 0;
   do {
      p = st->factors[2*L];
      m = st->factors[2*L+1];
      fstride[L+1] = fstride[L]*p;
      L++;
   } while (m != 1);
   m = st->factors[2*L-1];
   for (i = L-1; i >= 0; i--) {
      m2 = i != 0 ? st->factors[2*i-1] : 1;
      switch (st->factors[2*i]) {
      case 2: fl_kf_bfly2(fout, m, fstride[i]); break;
      case 4: fl_kf_bfly4(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 3: fl_kf_bfly3(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 5: fl_kf_bfly5(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      }
      m = m2;
   }
}

/* Inverse MDCT with windowed overlap (clt_mdct_backward_c). */
static void fl_mdct_backward(float *in, float *out, int overlap, int shift, int stride) {
   const float *trig = fl_mdct_twiddles960, *window = fl_window120;
   int i, N = 1920, N2, N4;
   for (i = 0; i < shift; i++) {
      N >>= 1;
      trig += N;
   }
   N2 = N>>1;
   N4 = N>>2;
   {
      const float *xp1 = in, *xp2 = in+stride*(N2-1), *t = trig;
      float *yp = out+(overlap>>1);
      const fl_i16 *bitrev = fl_fft_states[shift].bitrev;
      for (i = 0; i < N4; i++) {
         int rev = *bitrev++;
         float x1 = *xp1, x2 = *xp2;
         yp[2*rev+1] = x2*t[i]+x1*t[N4+i];
         yp[2*rev] = x1*t[i]-x2*t[N4+i];
         xp1 += 2*stride;
         xp2 -= 2*stride;
      }
   }
   fl_fft_impl(&fl_fft_states[shift], (fl_cpx*)(out+(overlap>>1)));
   {
      float *yp0 = out+(overlap>>1), *yp1 = out+(overlap>>1)+N2-2;
      const float *t = trig;
      for (i = 0; i < (N4+1)>>1; i++) {
         float re, im, yr, yi, t0, t1;
         re = yp0[1];
         im = yp0[0];
         t0 = t[i];
         t1 = t[N4+i];
         yr = re*t0+im*t1;
         yi = re*t1-im*t0;
         re = yp1[1];
         im = yp1[0];
         yp0[0] = yr;
         yp1[1] = yi;
         t0 = t[(N4-i-1)];
         t1 = t[(N2-i-1)];
         yr = re*t0+im*t1;
         yi = re*t1-im*t0;
         yp1[0] = yr;
         yp0[1] = yi;
         yp0 += 2;
         yp1 -= 2;
      }
   }
   {
      float *xp1 = out+overlap-1, *yp1 = out;
      const float *wp1 = window, *wp2 = window+overlap-1;
      for (i = 0; i < overlap/2; i++) {
         float x1 = *xp1, x2 = *yp1;
         *yp1++ = x2*(*wp2)-x1*(*wp1);
         *xp1-- = x2*(*wp1)+x1*(*wp2);
         wp1++;
         wp2--;
      }
   }
}

/* ---- Comb (pitch post-)filter ---- */
static const float fl_comb_gains[3][3] = {
   {0.3066406250f, 0.2170410156f, 0.1296386719f}, {0.4638671875f, 0.2680664062f, 0.f},
   {0.7998046875f, 0.1000976562f, 0.f}};

/* comb_filter_const_sse: processes multiples of 4 samples. */
static void fl_comb_filter_const(float *y, float *x, int T, int N, float g10, float g11, float g12) {
   int i, k;
   for (i = 0; i < N-3; i += 4) {
      for (k = 0; k < 4; k++) {
         int n = i+k;
         float yi = x[n]+g10*x[n-T];
         float yi2 = g11*(x[n-T+1]+x[n-T-1])+g12*(x[n-T+2]+x[n-T-2]);
         y[n] = yi+yi2;
      }
   }
}

static void fl_comb_filter(float *y, float *x, int T0, int T1, int N, float g0, float g1,
      int tapset0, int tapset1, const float *window, int overlap) {
   int i;
   float g00, g01, g02, g10, g11, g12, x0, x1, x2, x3, x4;
   if (g0 == 0 && g1 == 0) {
      if (x != y) FL_COPY(y, x, N);
      return;
   }
   T0 = IMAX(T0, COMBFILTER_MINPERIOD);
   T1 = IMAX(T1, COMBFILTER_MINPERIOD);
   g00 = g0*fl_comb_gains[tapset0][0];
   g01 = g0*fl_comb_gains[tapset0][1];
   g02 = g0*fl_comb_gains[tapset0][2];
   g10 = g1*fl_comb_gains[tapset1][0];
   g11 = g1*fl_comb_gains[tapset1][1];
   g12 = g1*fl_comb_gains[tapset1][2];
   x1 = x[-T1+1];
   x2 = x[-T1];
   x3 = x[-T1-1];
   x4 = x[-T1-2];
   if (g0 == g1 && T0 == T1 && tapset0 == tapset1) overlap = 0;
   for (i = 0; i < overlap; i++) {
      float f;
      x0 = x[i-T1+2];
      f = window[i]*window[i];
      y[i] = x[i]+((1.0f-f)*g00)*x[i-T0]
            +((1.0f-f)*g01)*(x[i-T0+1]+x[i-T0-1])
            +((1.0f-f)*g02)*(x[i-T0+2]+x[i-T0-2])
            +(f*g10)*x2
            +(f*g11)*(x1+x3)
            +(f*g12)*(x0+x4);
      x4 = x3;
      x3 = x2;
      x2 = x1;
      x1 = x0;
   }
   if (g1 == 0) {
      if (x != y) FL_COPY(y+overlap, x+overlap, N-overlap);
      return;
   }
   fl_comb_filter_const(y+i, x+i, T1, N-i, g10, g11, g12);
}

/* ---- LPC helpers used by the CELT PLC ---- */
static void fl_celt_lpc(float *lpc, const float *ac, int p) {
   int i, j;
   float r, error = ac[0];
   FL_CLEAR(lpc, p);
   if (ac[0] > 1e-10f) {
      for (i = 0; i < p; i++) {
         float rr = 0;
         for (j = 0; j < i; j++) rr += lpc[j]*ac[i-j];
         rr += ac[i+1];
         r = -(rr/error);
         lpc[i] = r;
         for (j = 0; j < (i+1)>>1; j++) {
            float tmp1 = lpc[j], tmp2 = lpc[i-1-j];
            lpc[j] = tmp1+r*tmp2;
            lpc[i-1-j] = tmp2+r*tmp1;
         }
         error = error-(r*r)*error;
         if (error <= .001f*ac[0]) break;
      }
   }
}
static void fl_celt_fir(const float *x, const float *num, float *y, int N, int ord) {
   int i, j;
   float rnum[CELT_LPC_ORDER];
   for (i = 0; i < ord; i++) rnum[i] = num[ord-i-1];
   for (i = 0; i < N-3; i += 4) {
      float sum[4];
      sum[0] = x[i]; sum[1] = x[i+1]; sum[2] = x[i+2]; sum[3] = x[i+3];
      fl_xcorr_kernel(rnum, x+i-ord, sum, ord);
      y[i] = sum[0]; y[i+1] = sum[1]; y[i+2] = sum[2]; y[i+3] = sum[3];
   }
   for (; i < N; i++) {
      float sum = x[i];
      for (j = 0; j < ord; j++) sum = sum+rnum[j]*x[i+j-ord];
      y[i] = sum;
   }
}
static void fl_celt_iir(const float *x, const float *den, float *_y, int N, int ord, float *mem) {
   int i, j;
   float rden[CELT_LPC_ORDER], y[1024+CELT_OVERLAP+CELT_LPC_ORDER+8];
   for (i = 0; i < ord; i++) rden[i] = den[ord-i-1];
   for (i = 0; i < ord; i++) y[i] = -mem[ord-i-1];
   for (; i < N+ord; i++) y[i] = 0;
   for (i = 0; i < N-3; i += 4) {
      float sum[4];
      sum[0] = x[i]; sum[1] = x[i+1]; sum[2] = x[i+2]; sum[3] = x[i+3];
      fl_xcorr_kernel(rden, y+i, sum, ord);
      y[i+ord] = -sum[0];
      _y[i] = sum[0];
      sum[1] = sum[1]+y[i+ord]*den[0];
      y[i+ord+1] = -sum[1];
      _y[i+1] = sum[1];
      sum[2] = sum[2]+y[i+ord+1]*den[0];
      sum[2] = sum[2]+y[i+ord]*den[1];
      y[i+ord+2] = -sum[2];
      _y[i+2] = sum[2];
      sum[3] = sum[3]+y[i+ord+2]*den[0];
      sum[3] = sum[3]+y[i+ord+1]*den[1];
      sum[3] = sum[3]+y[i+ord]*den[2];
      y[i+ord+3] = -sum[3];
      _y[i+3] = sum[3];
   }
   for (; i < N; i++) {
      float sum = x[i];
      for (j = 0; j < ord; j++) sum -= rden[j]*y[i+j];
      y[i+ord] = sum;
      _y[i] = sum;
   }
   for (i = 0; i < ord; i++) mem[i] = _y[N-i-1];
}
static void fl_celt_autocorr(const float *x, float *ac, const float *window, int overlap, int lag, int n) {
   float d, xx[DEC_PITCH_BUF_SIZE];
   int i, k, fastN = n-lag;
   const float *xptr;
   if (overlap == 0) xptr = x;
   else {
      for (i = 0; i < n; i++) xx[i] = x[i];
      for (i = 0; i < overlap; i++) {
         float w = window[i];
         xx[i] = x[i]*w;
         xx[n-i-1] = x[n-i-1]*w;
      }
      xptr = xx;
   }
   fl_pitch_xcorr(xptr, xptr, ac, fastN, lag+1);
   for (k = 0; k <= lag; k++) {
      for (i = k+fastN, d = 0; i < n; i++) d = d+xptr[i]*xptr[i-k];
      ac[k] += d;
   }
}

/* ---- Pitch search (used by the CELT PLC) ---- */
static void fl_find_best_pitch(float *xcorr, float *y, int len, int max_pitch, int *best_pitch) {
   int i, j;
   float Syy = 1, best_num[2], best_den[2];
   best_num[0] = -1; best_num[1] = -1;
   best_den[0] = 0; best_den[1] = 0;
   best_pitch[0] = 0; best_pitch[1] = 1;
   for (j = 0; j < len; j++) Syy = Syy+y[j]*y[j];
   for (i = 0; i < max_pitch; i++) {
      if (xcorr[i] > 0) {
         float num, xcorr16 = xcorr[i];
         xcorr16 *= 1e-12f;
         num = xcorr16*xcorr16;
         if (num*best_den[1] > best_num[1]*Syy) {
            if (num*best_den[0] > best_num[0]*Syy) {
               best_num[1] = best_num[0];
               best_den[1] = best_den[0];
               best_pitch[1] = best_pitch[0];
               best_num[0] = num;
               best_den[0] = Syy;
               best_pitch[0] = i;
            } else {
               best_num[1] = num;
               best_den[1] = Syy;
               best_pitch[1] = i;
            }
         }
      }
      Syy += y[i+len]*y[i+len]-y[i]*y[i];
      Syy = FL_MAX(1, Syy);
   }
}
static void fl_celt_fir5(float *x, const float *num, int N) {
   int i;
   float num0 = num[0], num1 = num[1], num2 = num[2], num3 = num[3], num4 = num[4];
   float mem0 = 0, mem1 = 0, mem2 = 0, mem3 = 0, mem4 = 0;
   for (i = 0; i < N; i++) {
      float sum = x[i];
      sum = sum+num0*mem0;
      sum = sum+num1*mem1;
      sum = sum+num2*mem2;
      sum = sum+num3*mem3;
      sum = sum+num4*mem4;
      mem4 = mem3;
      mem3 = mem2;
      mem2 = mem1;
      mem1 = mem0;
      mem0 = x[i];
      x[i] = sum;
   }
}
static void fl_pitch_downsample(float *x[], float *x_lp, int len, int C, int factor) {
   int i, offset = factor/2;
   float ac[5], tmp = 1.0f, lpc[4], lpc2[5], c1 = .8f;
   for (i = 1; i < len; i++)
      x_lp[i] = .25f*x[0][(factor*i-offset)]+.25f*x[0][(factor*i+offset)]+.5f*x[0][factor*i];
   x_lp[0] = .25f*x[0][offset]+.5f*x[0][0];
   if (C == 2) {
      for (i = 1; i < len; i++)
         x_lp[i] += .25f*x[1][(factor*i-offset)]+.25f*x[1][(factor*i+offset)]+.5f*x[1][factor*i];
      x_lp[0] += .25f*x[1][offset]+.5f*x[1][0];
   }
   fl_celt_autocorr(x_lp, ac, NULL, 0, 4, len);
   ac[0] *= 1.0001f;
   for (i = 1; i <= 4; i++) ac[i] -= ac[i]*(.008f*i)*(.008f*i);
   fl_celt_lpc(lpc, ac, 4);
   for (i = 0; i < 4; i++) {
      tmp = .9f*tmp;
      lpc[i] = lpc[i]*tmp;
   }
   lpc2[0] = lpc[0]+.8f;
   lpc2[1] = lpc[1]+c1*lpc[0];
   lpc2[2] = lpc[2]+c1*lpc[1];
   lpc2[3] = lpc[3]+c1*lpc[2];
   lpc2[4] = c1*lpc[3];
   fl_celt_fir5(x_lp, lpc2, len);
}
static void fl_pitch_search(const float *x_lp, float *y, int len, int max_pitch, int *pitch) {
   int i, j, lag, offset, best_pitch[2] = {0, 0};
   float x_lp4[DEC_PITCH_BUF_SIZE/4], y_lp4[DEC_PITCH_BUF_SIZE/2], xcorr[MAX_PERIOD];
   lag = len+max_pitch;
   for (j = 0; j < len>>2; j++) x_lp4[j] = x_lp[2*j];
   for (j = 0; j < lag>>2; j++) y_lp4[j] = y[2*j];
   fl_pitch_xcorr(x_lp4, y_lp4, xcorr, len>>2, max_pitch>>2);
   fl_find_best_pitch(xcorr, y_lp4, len>>2, max_pitch>>2, best_pitch);
   for (i = 0; i < max_pitch>>1; i++) {
      float sum;
      xcorr[i] = 0;
      if (abs(i-2*best_pitch[0]) > 2 && abs(i-2*best_pitch[1]) > 2) continue;
      sum = fl_inner_prod(x_lp, y+i, len>>1);
      xcorr[i] = FL_MAX(-1, sum);
   }
   fl_find_best_pitch(xcorr, y, len>>1, max_pitch>>1, best_pitch);
   if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch>>1)-1) {
      float a = xcorr[best_pitch[0]-1], b = xcorr[best_pitch[0]], c = xcorr[best_pitch[0]+1];
      if ((c-a) > .7f*(b-a)) offset = 1;
      else if ((a-c) > .7f*(b-c)) offset = -1;
      else offset = 0;
   } else offset = 0;
   *pitch = 2*best_pitch[0]-offset;
}

/* ======== celt_dec.c ======== */
/* ---- CELT decoder ---- */
#define DECODE_BUFFER_SIZE DEC_PITCH_BUF_SIZE
#define PLC_PITCH_LAG_MAX 720
#define PLC_PITCH_LAG_MIN 100
#define FRAME_NONE 0
#define FRAME_NORMAL 1
#define FRAME_PLC_NOISE 2
#define FRAME_PLC_PERIODIC 3

static const float fl_pred_coef[4] = {29440/32768., 26112/32768., 21248/32768., 16384/32768.};
static const float fl_beta_coef[4] = {30147/32768., 22282/32768., 12124/32768., 6554/32768.};
static const float fl_beta_intra = 4915/32768.;
static const unsigned char fl_small_energy_icdf[3] = {2, 1, 0};


typedef struct {
   int channels, stream_channels, downsample, start, end, disable_inv;
   fl_u32 rng;
   int error, last_pitch_index, loss_duration, plc_duration, last_frame_type, skip_plc;
   int postfilter_period, postfilter_period_old;
   float postfilter_gain, postfilter_gain_old;
   int postfilter_tapset, postfilter_tapset_old, prefilter_and_fold;
   float preemph_memD[2];
   float decode_mem[2][DECODE_BUFFER_SIZE+CELT_OVERLAP];
   float oldBandE[2*CELT_NB_EBANDS], oldLogE[2*CELT_NB_EBANDS], oldLogE2[2*CELT_NB_EBANDS];
   float backgroundLogE[2*CELT_NB_EBANDS];
   float lpc[2*CELT_LPC_ORDER];
} fl_celt_dec;

static void fl_celt_dec_reset(fl_celt_dec *st) {
   int i;
   st->rng = 0;
   st->error = 0;
   st->last_pitch_index = 0;
   st->loss_duration = 0;
   st->plc_duration = 0;
   st->postfilter_period = st->postfilter_period_old = 0;
   st->postfilter_gain = st->postfilter_gain_old = 0;
   st->postfilter_tapset = st->postfilter_tapset_old = 0;
   st->prefilter_and_fold = 0;
   st->preemph_memD[0] = st->preemph_memD[1] = 0;
   memset(st->decode_mem, 0, sizeof(st->decode_mem));
   FL_CLEAR(st->oldBandE, 2*CELT_NB_EBANDS);
   FL_CLEAR(st->backgroundLogE, 2*CELT_NB_EBANDS);
   FL_CLEAR(st->lpc, 2*CELT_LPC_ORDER);
   for (i = 0; i < 2*CELT_NB_EBANDS; i++) st->oldLogE[i] = st->oldLogE2[i] = -28.f;
   st->skip_plc = 1;
   st->last_frame_type = FRAME_NONE;
}

static void fl_celt_dec_init(fl_celt_dec *st, int Fs, int channels) {
   memset(st, 0, sizeof(*st));
   st->channels = st->stream_channels = channels;
   st->downsample = 48000/Fs;
   st->start = 0;
   st->end = CELT_NB_EBANDS;
   st->disable_inv = channels == 1;
   fl_celt_dec_reset(st);
}

/* ---- Band decoding ---- */
static void fl_exp_rotation1(float *X, int len, int stride, float c, float s) {
   int i;
   float ms = -s, *Xptr = X;
   for (i = 0; i < len-stride; i++) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = c*x2+s*x1;
      *Xptr++ = c*x1+ms*x2;
   }
   Xptr = &X[len-2*stride-1];
   for (i = len-2*stride-1; i >= 0; i--) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = c*x2+s*x1;
      *Xptr-- = c*x1+ms*x2;
   }
}
static void fl_exp_rotation(float *X, int len, int dir, int stride, int K, int spread) {
   static const int SPREAD_FACTOR[3] = {15, 10, 5};
   int i, stride2 = 0, factor;
   float c, s, gain, theta;
   if (2*K >= len || spread == SPREAD_NONE) return;
   factor = SPREAD_FACTOR[spread-1];
   gain = ((float)1.0f*(float)len)/(float)(len+factor*K);
   theta = .5f*(gain*gain);
   c = (float)cos((.5f*3.1415926535897931)*(theta));
   s = (float)cos((.5f*3.1415926535897931)*(1.0f-theta));
   if (len >= 8*stride) {
      stride2 = 1;
      while ((stride2*stride2+stride2)*stride+(stride>>2) < len) stride2++;
   }
   len = len/stride;
   for (i = 0; i < stride; i++) {
      if (dir < 0) {
         if (stride2) fl_exp_rotation1(X+i*len, len, stride2, s, c);
         fl_exp_rotation1(X+i*len, len, 1, c, s);
      } else {
         fl_exp_rotation1(X+i*len, len, 1, c, -s);
         if (stride2) fl_exp_rotation1(X+i*len, len, stride2, s, -c);
      }
   }
}
static void fl_renormalise_vector(float *X, int N, float gain) {
   int i;
   float E = 1e-15f+fl_inner_prod(X, X, N);
   float g = (1.f/(float)sqrt(E))*gain;
   for (i = 0; i < N; i++) X[i] = g*X[i];
}
static unsigned fl_alg_unquant(float *X, int N, int K, int spread, int B, ec_ctx *dec, float gain) {
   int i, iy[176];
   float Ryy, g;
   Ryy = fl_decode_pulses(iy, N, K, dec);
   g = (1.f/(float)sqrt(Ryy))*gain;
   for (i = 0; i < N; i++) X[i] = iy[i]*g;
   fl_exp_rotation(X, N, -1, B, K, spread);
   return fl_extract_collapse_mask(iy, N, B);
}
static void fl_haar1(float *X, int N0, int stride) {
   int i, j;
   N0 >>= 1;
   for (i = 0; i < stride; i++)
      for (j = 0; j < N0; j++) {
         float tmp1 = .70710678f*X[stride*2*j+i], tmp2 = .70710678f*X[stride*(2*j+1)+i];
         X[stride*2*j+i] = tmp1+tmp2;
         X[stride*(2*j+1)+i] = tmp1-tmp2;
      }
}
static const int fl_ordery_table[] = {1, 0, 3, 0, 2, 1, 7, 0, 4, 3, 6, 1, 5, 2,
   15, 0, 8, 7, 12, 3, 11, 4, 14, 1, 9, 6, 13, 2, 10, 5};
static void fl_deinterleave_hadamard(float *X, int N0, int stride, int hadamard) {
   int i, j, N = N0*stride;
   float tmp[176];
   if (hadamard) {
      const int *ordery = fl_ordery_table+stride-2;
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++) tmp[ordery[i]*N0+j] = X[j*stride+i];
   } else {
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++) tmp[i*N0+j] = X[j*stride+i];
   }
   FL_COPY(X, tmp, N);
}
static void fl_interleave_hadamard(float *X, int N0, int stride, int hadamard) {
   int i, j, N = N0*stride;
   float tmp[176];
   if (hadamard) {
      const int *ordery = fl_ordery_table+stride-2;
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++) tmp[j*stride+i] = X[ordery[i]*N0+j];
   } else {
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++) tmp[j*stride+i] = X[i*N0+j];
   }
   FL_COPY(X, tmp, N);
}

typedef struct {
   int i, intensity, spread, tf_change;
   ec_ctx *ec;
   fl_i32 remaining_bits;
   fl_u32 seed;
   int disable_inv, avoid_split_noise;
} fl_band_dctx;

typedef struct { int inv, imid, iside, delta, itheta, qalloc; } fl_split_ctx;

static void fl_compute_theta_dec(fl_band_dctx *ctx, fl_split_ctx *sctx, int N, int *b, int B, int B0,
      int LM, int stereo, int *fill) {
   int qn, itheta = 0, delta, imid, iside, qalloc, pulse_cap, offset, inv = 0;
   fl_i32 tell;
   ec_ctx *ec = ctx->ec;
   int i = ctx->i;
   pulse_cap = fl_logN400[i]+LM*(1<<BITRES);
   offset = (pulse_cap>>1)-(stereo && N == 2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
   qn = fl_compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i >= ctx->intensity) qn = 1;
   tell = ec_tell_frac(ec);
   if (qn != 1) {
      if (stereo && N > 2) {
         int p0 = 3, x = itheta, x0 = qn/2, ft = p0*(x0+1)+x0, fs;
         fs = ec_decode(ec, ft);
         if (fs < (x0+1)*p0) x = fs/p0;
         else x = x0+1+(fs-(x0+1)*p0);
         ec_dec_update(ec, x <= x0 ? p0*x : (x-1-x0)+(x0+1)*p0, x <= x0 ? p0*(x+1) : (x-x0)+(x0+1)*p0, ft);
         itheta = x;
      } else if (B0 > 1 || stereo) {
         itheta = ec_dec_uint(ec, qn+1);
      } else {
         int fs = 1, ft, fl = 0, fm;
         ft = ((qn>>1)+1)*((qn>>1)+1);
         fm = ec_decode(ec, ft);
         if (fm < ((qn>>1)*((qn>>1)+1)>>1)) {
            itheta = (fl_isqrt32(8*(fl_u32)fm+1)-1)>>1;
            fs = itheta+1;
            fl = itheta*(itheta+1)>>1;
         } else {
            itheta = (2*(qn+1)-fl_isqrt32(8*(fl_u32)(ft-fm-1)+1))>>1;
            fs = qn+1-itheta;
            fl = ft-((qn+1-itheta)*(qn+2-itheta)>>1);
         }
         ec_dec_update(ec, fl, fl+fs, ft);
      }
      itheta = (int)((fl_u32)((fl_i32)itheta*16384)/(fl_u32)qn);
   } else if (stereo) {
      if (*b > 2<<BITRES && ctx->remaining_bits > 2<<BITRES) inv = ec_dec_bit_logp(ec, 2);
      else inv = 0;
      if (ctx->disable_inv) inv = 0;
      itheta = 0;
   }
   qalloc = ec_tell_frac(ec)-tell;
   *b -= qalloc;
   if (itheta == 0) {
      imid = 32767;
      iside = 0;
      *fill &= (1<<B)-1;
      delta = -16384;
   } else if (itheta == 16384) {
      imid = 0;
      iside = 32767;
      *fill &= ((1<<B)-1)<<B;
      delta = 16384;
   } else {
      imid = fl_bitexact_cos((fl_i16)itheta);
      iside = fl_bitexact_cos((fl_i16)(16384-itheta));
      delta = FL_FRAC_MUL16((N-1)<<7, fl_bitexact_log2tan(iside, imid));
   }
   sctx->inv = inv;
   sctx->imid = imid;
   sctx->iside = iside;
   sctx->delta = delta;
   sctx->itheta = itheta;
   sctx->qalloc = qalloc;
}

static unsigned fl_quant_band_n1_dec(fl_band_dctx *ctx, float *X, float *Y, float *lowband_out) {
   int c, stereo = Y != NULL;
   float *x = X;
   c = 0;
   do {
      int sign = 0;
      if (ctx->remaining_bits >= 1<<BITRES) {
         sign = ec_dec_bits(ctx->ec, 1);
         ctx->remaining_bits -= 1<<BITRES;
      }
      x[0] = sign ? -1.f : 1.f;
      x = Y;
   } while (++c < 1+stereo);
   if (lowband_out) lowband_out[0] = X[0];
   return 1;
}

static unsigned fl_quant_partition_dec(fl_band_dctx *ctx, float *X, int N, int b, int B, float *lowband,
      int LM, float gain, int fill) {
   const unsigned char *cache;
   int q, curr_bits, B0 = B, i = ctx->i;
   float mid = 0, side = 0;
   unsigned cm = 0;
   cache = fl_cache_bits50+fl_cache_index50[(LM+1)*CELT_NB_EBANDS+i];
   if (LM != -1 && b > cache[cache[0]]+12 && N > 2) {
      int mbits, sbits, delta, itheta, qalloc;
      fl_split_ctx sctx;
      float *Y, *next_lowband2 = NULL;
      fl_i32 rebalance;
      N >>= 1;
      Y = X+N;
      LM -= 1;
      if (B == 1) fill = (fill&1)|(fill<<1);
      B = (B+1)>>1;
      fl_compute_theta_dec(ctx, &sctx, N, &b, B, B0, LM, 0, &fill);
      delta = sctx.delta;
      itheta = sctx.itheta;
      qalloc = sctx.qalloc;
      mid = (1.f/32768)*sctx.imid;
      side = (1.f/32768)*sctx.iside;
      if (B0 > 1 && (itheta&0x3fff)) {
         if (itheta > 8192) delta -= delta>>(4-LM);
         else delta = IMIN(0, delta+(N<<BITRES>>(5-LM)));
      }
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;
      if (lowband) next_lowband2 = lowband+N;
      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = fl_quant_partition_dec(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_quant_partition_dec(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
      } else {
         cm = fl_quant_partition_dec(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_quant_partition_dec(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
      }
   } else {
      q = fl_bits2pulses(i, LM, b);
      curr_bits = fl_pulses2bits(i, LM, q);
      ctx->remaining_bits -= curr_bits;
      while (ctx->remaining_bits < 0 && q > 0) {
         ctx->remaining_bits += curr_bits;
         q--;
         curr_bits = fl_pulses2bits(i, LM, q);
         ctx->remaining_bits -= curr_bits;
      }
      if (q != 0) {
         cm = fl_alg_unquant(X, N, fl_get_pulses(q), ctx->spread, B, ctx->ec, gain);
      } else {
         int j;
         unsigned cm_mask = (unsigned)(1UL<<B)-1;
         fill &= cm_mask;
         if (!fill) FL_CLEAR(X, N);
         else {
            if (lowband == NULL) {
               for (j = 0; j < N; j++) {
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  X[j] = (float)((fl_i32)ctx->seed>>20);
               }
               cm = cm_mask;
            } else {
               for (j = 0; j < N; j++) {
                  float tmp;
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  tmp = 1.0f/256;
                  tmp = (ctx->seed)&0x8000 ? tmp : -tmp;
                  X[j] = lowband[j]+tmp;
               }
               cm = fill;
            }
            fl_renormalise_vector(X, N, gain);
         }
      }
   }
   return cm;
}

static unsigned fl_quant_band_dec(fl_band_dctx *ctx, float *X, int N, int b, int B, float *lowband,
      int LM, float *lowband_out, float gain, float *lowband_scratch, int fill) {
   int N0 = N, N_B = N, N_B0, B0 = B, time_divide = 0, recombine = 0, longBlocks, k, tf_change;
   unsigned cm = 0;
   tf_change = ctx->tf_change;
   longBlocks = B0 == 1;
   N_B = N_B/B;
   if (N == 1) return fl_quant_band_n1_dec(ctx, X, NULL, lowband_out);
   if (tf_change > 0) recombine = tf_change;
   if (lowband_scratch && lowband && (recombine || ((N_B&1) == 0 && tf_change < 0) || B0 > 1)) {
      FL_COPY(lowband_scratch, lowband, N);
      lowband = lowband_scratch;
   }
   for (k = 0; k < recombine; k++) {
      static const unsigned char bit_interleave_table[16] = {0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3};
      if (lowband) fl_haar1(lowband, N>>k, 1<<k);
      fill = bit_interleave_table[fill&0xF]|bit_interleave_table[fill>>4]<<2;
   }
   B >>= recombine;
   N_B <<= recombine;
   while ((N_B&1) == 0 && tf_change < 0) {
      if (lowband) fl_haar1(lowband, N_B, B);
      fill |= fill<<B;
      B <<= 1;
      N_B >>= 1;
      time_divide++;
      tf_change++;
   }
   B0 = B;
   N_B0 = N_B;
   if (B0 > 1 && lowband) fl_deinterleave_hadamard(lowband, N_B>>recombine, B0<<recombine, longBlocks);
   cm = fl_quant_partition_dec(ctx, X, N, b, B, lowband, LM, gain, fill);
   if (B0 > 1) fl_interleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
   N_B = N_B0;
   B = B0;
   for (k = 0; k < time_divide; k++) {
      B >>= 1;
      N_B <<= 1;
      cm |= cm>>B;
      fl_haar1(X, N_B, B);
   }
   for (k = 0; k < recombine; k++) {
      static const unsigned char bit_deinterleave_table[16] = {0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
         0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF};
      cm = bit_deinterleave_table[cm];
      fl_haar1(X, N0>>k, 1<<k);
   }
   B <<= recombine;
   if (lowband_out) {
      int j;
      float n = (float)sqrt(N0);
      for (j = 0; j < N0; j++) lowband_out[j] = n*X[j];
   }
   cm &= (1<<B)-1;
   return cm;
}

static void fl_stereo_merge(float *X, float *Y, float mid, int N) {
   int j;
   float xp, side, El, Er, lgain, rgain;
   xp = fl_inner_prod(Y, X, N);
   side = fl_inner_prod(Y, Y, N);
   xp = mid*xp;
   El = (mid*mid)+side-2*xp;
   Er = (mid*mid)+side+2*xp;
   if (Er < 6e-4f || El < 6e-4f) {
      FL_COPY(Y, X, N);
      return;
   }
   lgain = 1.f/(float)sqrt(El);
   rgain = 1.f/(float)sqrt(Er);
   for (j = 0; j < N; j++) {
      float l = mid*X[j], r = Y[j];
      X[j] = lgain*(l-r);
      Y[j] = rgain*(l+r);
   }
}

static unsigned fl_quant_band_stereo_dec(fl_band_dctx *ctx, float *X, float *Y, int N, int b, int B,
      float *lowband, int LM, float *lowband_out, float *lowband_scratch, int fill) {
   int inv, mbits, sbits, delta, itheta, qalloc, orig_fill;
   float mid, side;
   unsigned cm = 0;
   fl_split_ctx sctx;
   if (N == 1) return fl_quant_band_n1_dec(ctx, X, Y, lowband_out);
   orig_fill = fill;
   fl_compute_theta_dec(ctx, &sctx, N, &b, B, B, LM, 1, &fill);
   inv = sctx.inv;
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;
   mid = (1.f/32768)*sctx.imid;
   side = (1.f/32768)*sctx.iside;
   if (N == 2) {
      int c, sign = 0;
      float *x2, *y2, tmp;
      mbits = b;
      sbits = 0;
      if (itheta != 0 && itheta != 16384) sbits = 1<<BITRES;
      mbits -= sbits;
      c = itheta > 8192;
      ctx->remaining_bits -= qalloc+sbits;
      x2 = c ? Y : X;
      y2 = c ? X : Y;
      if (sbits) sign = ec_dec_bits(ctx->ec, 1);
      sign = 1-2*sign;
      cm = fl_quant_band_dec(ctx, x2, N, mbits, B, lowband, LM, lowband_out, 1.0f, lowband_scratch, orig_fill);
      y2[0] = -sign*x2[1];
      y2[1] = sign*x2[0];
      X[0] = mid*X[0];
      X[1] = mid*X[1];
      Y[0] = side*Y[0];
      Y[1] = side*Y[1];
      tmp = X[0];
      X[0] = tmp-Y[0];
      Y[0] = tmp+Y[0];
      tmp = X[1];
      X[1] = tmp-Y[1];
      Y[1] = tmp+Y[1];
   } else {
      fl_i32 rebalance;
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;
      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = fl_quant_band_dec(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.0f, lowband_scratch, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_quant_band_dec(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
      } else {
         cm = fl_quant_band_dec(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_quant_band_dec(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.0f, lowband_scratch, fill);
      }
   }
   if (N != 2) fl_stereo_merge(X, Y, mid, N);
   if (inv) {
      int j;
      for (j = 0; j < N; j++) Y[j] = -Y[j];
   }
   return cm;
}

static void fl_special_hybrid_folding(float *norm, float *norm2, int start, int M, int dual_stereo) {
   const fl_i16 *eb = fl_eband5ms;
   int n1 = M*(eb[start+1]-eb[start]), n2 = M*(eb[start+2]-eb[start+1]);
   FL_COPY(&norm[n1], &norm[2*n1-n2], n2-n1);
   if (dual_stereo) FL_COPY(&norm2[n1], &norm2[2*n1-n2], n2-n1);
}

static void fl_quant_all_bands_dec(int start, int end, float *X_, float *Y_, unsigned char *collapse_masks,
      int *pulses, int shortBlocks, int spread, int dual_stereo, int intensity, int *tf_res,
      fl_i32 total_bits, fl_i32 balance, ec_ctx *ec, int LM, int codedBands, fl_u32 *seed, int disable_inv) {
   const fl_i16 *eb = fl_eband5ms;
   int i, M, B, lowband_offset, update_lowband = 1, C = Y_ != NULL ? 2 : 1, norm_offset;
   fl_i32 remaining_bits;
   float _norm[2*8*100], *norm, *norm2, *lowband_scratch;
   fl_band_dctx ctx;
   M = 1<<LM;
   B = shortBlocks ? M : 1;
   norm_offset = M*eb[start];
   norm = _norm;
   norm2 = norm+M*eb[CELT_NB_EBANDS-1]-norm_offset;
   lowband_scratch = X_+M*eb[CELT_NB_EBANDS-1];
   lowband_offset = 0;
   ctx.ec = ec;
   ctx.intensity = intensity;
   ctx.seed = *seed;
   ctx.spread = spread;
   ctx.disable_inv = disable_inv;
   ctx.avoid_split_noise = B > 1;
   for (i = start; i < end; i++) {
      fl_i32 tell, curr_balance;
      int b, N, effective_lowband = -1, tf_change, last;
      float *X, *Y;
      unsigned x_cm, y_cm;
      ctx.i = i;
      last = (i == end-1);
      X = X_+M*eb[i];
      Y = Y_ != NULL ? Y_+M*eb[i] : NULL;
      N = M*eb[i+1]-M*eb[i];
      tell = ec_tell_frac(ec);
      if (i != start) balance -= tell;
      remaining_bits = total_bits-tell-1;
      ctx.remaining_bits = remaining_bits;
      if (i <= codedBands-1) {
         curr_balance = balance/IMIN(3, codedBands-i);
         b = IMAX(0, IMIN(16383, IMIN(remaining_bits+1, pulses[i]+curr_balance)));
      } else b = 0;
      if ((M*eb[i]-N >= M*eb[start] || i == start+1) && (update_lowband || lowband_offset == 0))
         lowband_offset = i;
      if (i == start+1) fl_special_hybrid_folding(norm, norm2, start, M, dual_stereo);
      tf_change = tf_res[i];
      ctx.tf_change = tf_change;
      if (i >= CELT_NB_EBANDS) {
         X = norm;
         if (Y_ != NULL) Y = norm;
         lowband_scratch = NULL;
      }
      if (last) lowband_scratch = NULL;
      if (lowband_offset != 0 && (spread != SPREAD_AGGRESSIVE || B > 1 || tf_change < 0)) {
         int fold_start, fold_end, fold_i;
         effective_lowband = IMAX(0, M*eb[lowband_offset]-norm_offset-N);
         fold_start = lowband_offset;
         while (M*eb[--fold_start] > effective_lowband+norm_offset);
         fold_end = lowband_offset-1;
         while (++fold_end < i && M*eb[fold_end] < effective_lowband+norm_offset+N);
         x_cm = y_cm = 0;
         fold_i = fold_start;
         do {
            x_cm |= collapse_masks[fold_i*C+0];
            y_cm |= collapse_masks[fold_i*C+C-1];
         } while (++fold_i < fold_end);
      } else x_cm = y_cm = (1<<B)-1;
      if (dual_stereo && i == intensity) {
         int j;
         dual_stereo = 0;
         for (j = 0; j < M*eb[i]-norm_offset; j++) norm[j] = .5f*(norm[j]+norm2[j]);
      }
      if (dual_stereo) {
         x_cm = fl_quant_band_dec(&ctx, X, N, b/2, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
               last ? NULL : norm+M*eb[i]-norm_offset, 1.0f, lowband_scratch, x_cm);
         y_cm = fl_quant_band_dec(&ctx, Y, N, b/2, B, effective_lowband != -1 ? norm2+effective_lowband : NULL, LM,
               last ? NULL : norm2+M*eb[i]-norm_offset, 1.0f, lowband_scratch, y_cm);
      } else {
         if (Y != NULL) {
            x_cm = fl_quant_band_stereo_dec(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                  LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, x_cm|y_cm);
         } else {
            x_cm = fl_quant_band_dec(&ctx, X, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
                  last ? NULL : norm+M*eb[i]-norm_offset, 1.0f, lowband_scratch, x_cm|y_cm);
         }
         y_cm = x_cm;
      }
      collapse_masks[i*C+0] = (unsigned char)x_cm;
      collapse_masks[i*C+C-1] = (unsigned char)y_cm;
      balance += pulses[i]+tell;
      update_lowband = b > (N<<BITRES);
      ctx.avoid_split_noise = 0;
   }
   *seed = ctx.seed;
}

static void fl_anti_collapse(float *X_, unsigned char *collapse_masks, int LM, int C, int size, int start,
      int end, const float *logE, const float *prev1logE, const float *prev2logE, const int *pulses, fl_u32 seed) {
   const fl_i16 *eb = fl_eband5ms;
   int c, i, j, k;
   for (i = start; i < end; i++) {
      int N0 = eb[i+1]-eb[i], depth;
      float thresh, sqrt_1;
      depth = (int)((fl_u32)(1+pulses[i])/(fl_u32)(eb[i+1]-eb[i]))>>LM;
      thresh = .5f*fl_celt_exp2(-.125f*depth);
      sqrt_1 = 1.f/(float)sqrt(N0<<LM);
      c = 0;
      do {
         float *X, prev1, prev2, Ediff, r;
         int renormalize = 0;
         prev1 = prev1logE[c*CELT_NB_EBANDS+i];
         prev2 = prev2logE[c*CELT_NB_EBANDS+i];
         if (C == 1) {
            prev1 = FL_MAX(prev1, prev1logE[CELT_NB_EBANDS+i]);
            prev2 = FL_MAX(prev2, prev2logE[CELT_NB_EBANDS+i]);
         }
         Ediff = logE[c*CELT_NB_EBANDS+i]-FL_MIN(prev1, prev2);
         Ediff = FL_MAX(0, Ediff);
         r = 2.f*fl_celt_exp2(-Ediff);
         if (LM == 3) r *= 1.41421356f;
         r = FL_MIN(thresh, r);
         r = r*sqrt_1;
         X = X_+c*size+(eb[i]<<LM);
         for (k = 0; k < 1<<LM; k++) {
            if (!(collapse_masks[i*C+c]&1<<k)) {
               for (j = 0; j < N0; j++) {
                  seed = fl_lcg_rand(seed);
                  X[(j<<LM)+k] = (seed&0x8000 ? r : -r);
               }
               renormalize = 1;
            }
         }
         if (renormalize) fl_renormalise_vector(X, N0<<LM, 1.0f);
      } while (++c < C);
   }
}

static void fl_denormalise_bands(const float *X, float *freq, const float *bandLogE, int start, int end,
      int M, int downsample, int silence) {
   const fl_i16 *eb = fl_eband5ms;
   int i, N = M*CELT_SHORT_MDCT, bound = M*eb[end];
   float *f = freq;
   const float *x;
   if (downsample != 1) bound = IMIN(bound, N/downsample);
   if (silence) {
      bound = 0;
      start = end = 0;
   }
   x = X+M*eb[start];
   if (start != 0) {
      for (i = 0; i < M*eb[start]; i++) *f++ = 0;
   } else f += M*eb[start];
   for (i = start; i < end; i++) {
      int j = M*eb[i], band_end = M*eb[i+1];
      float g, lg = bandLogE[i]+fl_eMeans[i];
      g = fl_celt_exp2(FL_MIN(32.f, lg));
      do {
         *f++ = *x*g;
         x++;
      } while (++j < band_end);
   }
   FL_CLEAR(&freq[bound], N-bound);
}

/* ---- Energy decoding ---- */
static void fl_unquant_coarse_energy(int start, int end, float *oldEBands, int intra, ec_ctx *dec, int C, int LM) {
   const unsigned char *prob_model = fl_e_prob_model[LM][intra];
   int i, c;
   float prev[2] = {0, 0}, coef, beta;
   fl_i32 budget, tell;
   if (intra) {
      coef = 0;
      beta = fl_beta_intra;
   } else {
      beta = fl_beta_coef[LM];
      coef = fl_pred_coef[LM];
   }
   budget = dec->storage*8;
   for (i = start; i < end; i++) {
      c = 0;
      do {
         int qi;
         float q, tmp;
         tell = ec_tell(dec);
         if (budget-tell >= 15) {
            int pi = 2*IMIN(i, 20);
            qi = fl_laplace_decode(dec, prob_model[pi]<<7, prob_model[pi+1]<<6);
         } else if (budget-tell >= 2) {
            qi = ec_dec_icdf(dec, fl_small_energy_icdf, 2);
            qi = (qi>>1)^-(qi&1);
         } else if (budget-tell >= 1) qi = -ec_dec_bit_logp(dec, 1);
         else qi = -1;
         q = (float)qi;
         oldEBands[i+c*CELT_NB_EBANDS] = FL_MAX(-9.f, oldEBands[i+c*CELT_NB_EBANDS]);
         tmp = coef*oldEBands[i+c*CELT_NB_EBANDS]+prev[c]+q;
         oldEBands[i+c*CELT_NB_EBANDS] = tmp;
         prev[c] = prev[c]+q-beta*q;
      } while (++c < C);
   }
}
static void fl_unquant_fine_energy(int start, int end, float *oldEBands, int *extra_quant, ec_ctx *dec, int C) {
   int i, c;
   for (i = start; i < end; i++) {
      int extra = extra_quant[i];
      if (extra <= 0) continue;
      if (ec_tell(dec)+C*extra > (fl_i32)dec->storage*8) continue;
      c = 0;
      do {
         int q2 = ec_dec_bits(dec, extra);
         float offset = (q2+.5f)*(1<<(14-extra))*(1.f/16384)-.5f;
         offset *= (1<<14)*(1.f/16384);
         oldEBands[i+c*CELT_NB_EBANDS] += offset;
      } while (++c < C);
   }
}
static void fl_unquant_energy_finalise(int start, int end, float *oldEBands, int *fine_quant, int *fine_priority,
      int bits_left, ec_ctx *dec, int C) {
   int i, prio, c;
   for (prio = 0; prio < 2; prio++) {
      for (i = start; i < end && bits_left >= C; i++) {
         if (fine_quant[i] >= MAX_FINE_BITS || fine_priority[i] != prio) continue;
         c = 0;
         do {
            int q2 = ec_dec_bits(dec, 1);
            float offset = (q2-.5f)*(1<<(14-fine_quant[i]-1))*(1.f/16384);
            oldEBands[i+c*CELT_NB_EBANDS] += offset;
            bits_left--;
         } while (++c < C);
      }
   }
}

static void fl_tf_decode(int start, int end, int isTransient, int *tf_res, int LM, ec_ctx *dec) {
   int i, curr, tf_select, tf_select_rsv, tf_changed, logp;
   fl_u32 budget, tell;
   budget = dec->storage*8;
   tell = ec_tell(dec);
   logp = isTransient ? 2 : 4;
   tf_select_rsv = LM > 0 && tell+logp+1 <= budget;
   budget -= tf_select_rsv;
   tf_changed = curr = 0;
   for (i = start; i < end; i++) {
      if (tell+logp <= budget) {
         curr ^= ec_dec_bit_logp(dec, logp);
         tell = ec_tell(dec);
         tf_changed |= curr;
      }
      tf_res[i] = curr;
      logp = isTransient ? 4 : 5;
   }
   tf_select = 0;
   if (tf_select_rsv && fl_tf_select_table[LM][4*isTransient+0+tf_changed] != fl_tf_select_table[LM][4*isTransient+2+tf_changed])
      tf_select = ec_dec_bit_logp(dec, 1);
   for (i = start; i < end; i++) tf_res[i] = fl_tf_select_table[LM][4*isTransient+2*tf_select+tf_res[i]];
}

/* ---- Synthesis ---- */
static void fl_deemphasis(float *in[], float *pcm, int N, int C, int downsample, float *mem, int accum) {
   int c, j, Nd, apply_downsampling = 0;
   float coef0 = fl_preemph[0], scratch[960];
   if (downsample == 1 && C == 2 && !accum) {
      float *x0 = in[0], *x1 = in[1], m0 = mem[0], m1 = mem[1];
      for (j = 0; j < N; j++) {
         float tmp0 = x0[j]+1e-30f+m0, tmp1 = x1[j]+1e-30f+m1;
         m0 = coef0*tmp0;
         m1 = coef0*tmp1;
         pcm[2*j] = (1/32768.f)*tmp0;
         pcm[2*j+1] = (1/32768.f)*tmp1;
      }
      mem[0] = m0;
      mem[1] = m1;
      return;
   }
   Nd = N/downsample;
   c = 0;
   do {
      float *x = in[c], *y = pcm+c, m = mem[c];
      if (downsample > 1) {
         for (j = 0; j < N; j++) {
            float tmp = x[j]+1e-30f+m;
            m = coef0*tmp;
            scratch[j] = tmp;
         }
         apply_downsampling = 1;
      } else if (accum) {
         for (j = 0; j < N; j++) {
            float tmp = x[j]+m+1e-30f;
            m = coef0*tmp;
            y[j*C] = y[j*C]+(1/32768.f)*tmp;
         }
      } else {
         for (j = 0; j < N; j++) {
            float tmp = x[j]+1e-30f+m;
            m = coef0*tmp;
            y[j*C] = (1/32768.f)*tmp;
         }
      }
      mem[c] = m;
      if (apply_downsampling) {
         if (accum) for (j = 0; j < Nd; j++) y[j*C] = y[j*C]+(1/32768.f)*scratch[j*downsample];
         else for (j = 0; j < Nd; j++) y[j*C] = (1/32768.f)*scratch[j*downsample];
      }
   } while (++c < C);
}

static void fl_celt_synthesis(float *X, float *out_syn[], float *oldBandE, int start, int effEnd, int C, int CC,
      int isTransient, int LM, int downsample, int silence) {
   int c, i, M, b, B, N, NB, shift;
   float freq[960];
   N = CELT_SHORT_MDCT<<LM;
   M = 1<<LM;
   if (isTransient) {
      B = M;
      NB = CELT_SHORT_MDCT;
      shift = CELT_MAX_LM;
   } else {
      B = 1;
      NB = CELT_SHORT_MDCT<<LM;
      shift = CELT_MAX_LM-LM;
   }
   if (CC == 2 && C == 1) {
      float *freq2;
      fl_denormalise_bands(X, freq, oldBandE, start, effEnd, M, downsample, silence);
      freq2 = out_syn[1]+CELT_OVERLAP/2;
      FL_COPY(freq2, freq, N);
      for (b = 0; b < B; b++) fl_mdct_backward(&freq2[b], out_syn[0]+NB*b, CELT_OVERLAP, shift, B);
      for (b = 0; b < B; b++) fl_mdct_backward(&freq[b], out_syn[1]+NB*b, CELT_OVERLAP, shift, B);
   } else if (CC == 1 && C == 2) {
      float *freq2 = out_syn[0]+CELT_OVERLAP/2;
      fl_denormalise_bands(X, freq, oldBandE, start, effEnd, M, downsample, silence);
      fl_denormalise_bands(X+N, freq2, oldBandE+CELT_NB_EBANDS, start, effEnd, M, downsample, silence);
      for (i = 0; i < N; i++) freq[i] = .5f*freq[i]+.5f*freq2[i];
      for (b = 0; b < B; b++) fl_mdct_backward(&freq[b], out_syn[0]+NB*b, CELT_OVERLAP, shift, B);
   } else {
      c = 0;
      do {
         fl_denormalise_bands(X+c*N, freq, oldBandE+c*CELT_NB_EBANDS, start, effEnd, M, downsample, silence);
         for (b = 0; b < B; b++) fl_mdct_backward(&freq[b], out_syn[c]+NB*b, CELT_OVERLAP, shift, B);
      } while (++c < CC);
   }
}

/* ---- Packet loss concealment ---- */
static void fl_prefilter_and_fold(fl_celt_dec *st, int N) {
   int c, i;
   float etmp[CELT_OVERLAP];
   c = 0;
   do {
      float *mem = st->decode_mem[c];
      fl_comb_filter(etmp, mem+DECODE_BUFFER_SIZE-N, st->postfilter_period_old, st->postfilter_period,
            CELT_OVERLAP, -st->postfilter_gain_old, -st->postfilter_gain, st->postfilter_tapset_old,
            st->postfilter_tapset, NULL, 0);
      for (i = 0; i < CELT_OVERLAP/2; i++)
         mem[DECODE_BUFFER_SIZE-N+i] = fl_window120[i]*etmp[CELT_OVERLAP-1-i]
               +fl_window120[CELT_OVERLAP-i-1]*etmp[i];
   } while (++c < st->channels);
}

static void fl_celt_decode_lost(fl_celt_dec *st, int N, int LM) {
   const fl_i16 *eb = fl_eband5ms;
   const int C = st->channels;
   float *decode_mem[2], *out_syn[2];
   int c, i, start = st->start, loss_duration = st->loss_duration, curr_frame_type = FRAME_PLC_PERIODIC;
   c = 0;
   do {
      decode_mem[c] = st->decode_mem[c];
      out_syn[c] = decode_mem[c]+DECODE_BUFFER_SIZE-N;
   } while (++c < C);
   if (st->plc_duration >= 40 || start != 0 || st->skip_plc) curr_frame_type = FRAME_PLC_NOISE;
   if (curr_frame_type == FRAME_PLC_NOISE) {
      float X[2*960], decay;
      fl_u32 seed;
      int end = st->end, effEnd = IMAX(start, IMIN(end, CELT_NB_EBANDS));
      c = 0;
      do FL_COPY(decode_mem[c], decode_mem[c]+N, DECODE_BUFFER_SIZE-N+CELT_OVERLAP);
      while (++c < C);
      if (st->prefilter_and_fold) fl_prefilter_and_fold(st, N);
      decay = loss_duration == 0 ? 1.5f : .5f;
      c = 0;
      do {
         for (i = start; i < end; i++)
            st->oldBandE[c*CELT_NB_EBANDS+i] = FL_MAX(st->backgroundLogE[c*CELT_NB_EBANDS+i], st->oldBandE[c*CELT_NB_EBANDS+i]-decay);
      } while (++c < C);
      seed = st->rng;
      for (c = 0; c < C; c++) {
         for (i = start; i < effEnd; i++) {
            int j, boffs = N*c+(eb[i]<<LM), blen = (eb[i+1]-eb[i])<<LM;
            for (j = 0; j < blen; j++) {
               seed = fl_lcg_rand(seed);
               X[boffs+j] = (float)((fl_i32)seed>>20);
            }
            fl_renormalise_vector(X+boffs, blen, 1.0f);
         }
      }
      st->rng = seed;
      fl_celt_synthesis(X, out_syn, st->oldBandE, start, effEnd, C, C, 0, LM, st->downsample, 0);
      c = 0;
      do {
         st->postfilter_period = IMAX(st->postfilter_period, COMBFILTER_MINPERIOD);
         st->postfilter_period_old = IMAX(st->postfilter_period_old, COMBFILTER_MINPERIOD);
         fl_comb_filter(out_syn[c], out_syn[c], st->postfilter_period_old, st->postfilter_period, CELT_SHORT_MDCT,
               st->postfilter_gain_old, st->postfilter_gain, st->postfilter_tapset_old, st->postfilter_tapset,
               fl_window120, CELT_OVERLAP);
         if (LM != 0)
            fl_comb_filter(out_syn[c]+CELT_SHORT_MDCT, out_syn[c]+CELT_SHORT_MDCT, st->postfilter_period,
                  st->postfilter_period, N-CELT_SHORT_MDCT, st->postfilter_gain, st->postfilter_gain,
                  st->postfilter_tapset, st->postfilter_tapset, fl_window120, CELT_OVERLAP);
      } while (++c < C);
      st->postfilter_period_old = st->postfilter_period;
      st->postfilter_gain_old = st->postfilter_gain;
      st->postfilter_tapset_old = st->postfilter_tapset;
      st->prefilter_and_fold = 0;
      st->skip_plc = 1;
   } else {
      int exc_length, pitch_index;
      float fade = 1.0f, _exc[MAX_PERIOD+CELT_LPC_ORDER], fir_tmp[MAX_PERIOD], *exc;
      if (st->last_frame_type != FRAME_PLC_PERIODIC) {
         float lp_pitch_buf[DECODE_BUFFER_SIZE>>1];
         fl_pitch_downsample(decode_mem, lp_pitch_buf, DECODE_BUFFER_SIZE>>1, C, 2);
         fl_pitch_search(lp_pitch_buf+(PLC_PITCH_LAG_MAX>>1), lp_pitch_buf, DECODE_BUFFER_SIZE-PLC_PITCH_LAG_MAX,
               PLC_PITCH_LAG_MAX-PLC_PITCH_LAG_MIN, &pitch_index);
         pitch_index = PLC_PITCH_LAG_MAX-pitch_index;
         st->last_pitch_index = pitch_index;
      } else {
         pitch_index = st->last_pitch_index;
         fade = .8f;
      }
      exc_length = IMIN(2*pitch_index, MAX_PERIOD);
      exc = _exc+CELT_LPC_ORDER;
      c = 0;
      do {
         float decay, attenuation, S1 = 0, *buf = decode_mem[c];
         int extrapolation_offset, extrapolation_len, j;
         for (i = 0; i < MAX_PERIOD+CELT_LPC_ORDER; i++)
            exc[i-CELT_LPC_ORDER] = buf[DECODE_BUFFER_SIZE-MAX_PERIOD-CELT_LPC_ORDER+i];
         if (st->last_frame_type != FRAME_PLC_PERIODIC) {
            float ac[CELT_LPC_ORDER+1];
            fl_celt_autocorr(exc, ac, fl_window120, CELT_OVERLAP, CELT_LPC_ORDER, MAX_PERIOD);
            ac[0] *= 1.0001f;
            for (i = 1; i <= CELT_LPC_ORDER; i++) ac[i] -= ac[i]*(0.008f*0.008f)*i*i;
            fl_celt_lpc(st->lpc+c*CELT_LPC_ORDER, ac, CELT_LPC_ORDER);
         }
         fl_celt_fir(exc+MAX_PERIOD-exc_length, st->lpc+c*CELT_LPC_ORDER, fir_tmp, exc_length, CELT_LPC_ORDER);
         FL_COPY(exc+MAX_PERIOD-exc_length, fir_tmp, exc_length);
         {
            float E1 = 1, E2 = 1;
            int decay_length = exc_length>>1;
            for (i = 0; i < decay_length; i++) {
               float e = exc[MAX_PERIOD-decay_length+i];
               E1 += e*e;
               e = exc[MAX_PERIOD-2*decay_length+i];
               E2 += e*e;
            }
            E1 = FL_MIN(E1, E2);
            decay = (float)sqrt((float)(E1)/(E2));
         }
         FL_COPY(buf, buf+N, DECODE_BUFFER_SIZE-N);
         extrapolation_offset = MAX_PERIOD-pitch_index;
         extrapolation_len = N+CELT_OVERLAP;
         attenuation = fade*decay;
         for (i = j = 0; i < extrapolation_len; i++, j++) {
            float tmp;
            if (j >= pitch_index) {
               j -= pitch_index;
               attenuation = attenuation*decay;
            }
            buf[DECODE_BUFFER_SIZE-N+i] = attenuation*exc[extrapolation_offset+j];
            tmp = buf[DECODE_BUFFER_SIZE-MAX_PERIOD-N+extrapolation_offset+j];
            S1 += tmp*tmp;
         }
         {
            float lpc_mem[CELT_LPC_ORDER];
            for (i = 0; i < CELT_LPC_ORDER; i++) lpc_mem[i] = buf[DECODE_BUFFER_SIZE-N-1-i];
            fl_celt_iir(buf+DECODE_BUFFER_SIZE-N, st->lpc+c*CELT_LPC_ORDER, buf+DECODE_BUFFER_SIZE-N,
                  extrapolation_len, CELT_LPC_ORDER, lpc_mem);
         }
         {
            float S2 = 0;
            for (i = 0; i < extrapolation_len; i++) {
               float tmp = buf[DECODE_BUFFER_SIZE-N+i];
               S2 += tmp*tmp;
            }
            if (!(S1 > 0.2f*S2)) {
               for (i = 0; i < extrapolation_len; i++) buf[DECODE_BUFFER_SIZE-N+i] = 0;
            } else if (S1 < S2) {
               float ratio = (float)sqrt((float)(S1+1)/(S2+1));
               for (i = 0; i < CELT_OVERLAP; i++) {
                  float tmp_g = 1.0f-fl_window120[i]*(1.0f-ratio);
                  buf[DECODE_BUFFER_SIZE-N+i] = tmp_g*buf[DECODE_BUFFER_SIZE-N+i];
               }
               for (i = CELT_OVERLAP; i < extrapolation_len; i++)
                  buf[DECODE_BUFFER_SIZE-N+i] = ratio*buf[DECODE_BUFFER_SIZE-N+i];
            }
         }
      } while (++c < C);
      st->prefilter_and_fold = 1;
   }
   st->loss_duration = IMIN(10000, loss_duration+(1<<LM));
   st->plc_duration = IMIN(10000, st->plc_duration+(1<<LM));
   st->last_frame_type = curr_frame_type;
}

/* Decodes one CELT frame; data==NULL runs the PLC. Returns samples per channel or <0. */
static int fl_celt_decode(fl_celt_dec *st, const unsigned char *data, int len, float *pcm, int frame_size,
      ec_ctx *dec, int accum) {
   const fl_i16 *eb = fl_eband5ms;
   int c, i, N, spread_decision, shortBlocks, isTransient, intra_ener, LM, M, start, end, effEnd, codedBands;
   int alloc_trim, postfilter_pitch, intensity = 0, dual_stereo = 0, dynalloc_logp, postfilter_tapset;
   int anti_collapse_rsv, anti_collapse_on = 0, silence, C = st->stream_channels, CC = st->channels;
   fl_i32 bits, total_bits, balance, tell;
   float postfilter_gain, max_background_increase, *decode_mem[2], *out_syn[2];
   float X[2*960];
   int fine_quant[CELT_NB_EBANDS], pulses[CELT_NB_EBANDS], cap[CELT_NB_EBANDS], offsets[CELT_NB_EBANDS];
   int fine_priority[CELT_NB_EBANDS], tf_res[CELT_NB_EBANDS];
   unsigned char collapse_masks[2*CELT_NB_EBANDS];
   ec_ctx _dec;
   start = st->start;
   end = st->end;
   frame_size *= st->downsample;
   for (LM = 0; LM <= CELT_MAX_LM; LM++)
      if (CELT_SHORT_MDCT<<LM == frame_size) break;
   if (LM > CELT_MAX_LM) return -1;
   M = 1<<LM;
   if (len < 0 || len > 1275 || pcm == NULL) return -1;
   N = M*CELT_SHORT_MDCT;
   c = 0;
   do {
      decode_mem[c] = st->decode_mem[c];
      out_syn[c] = decode_mem[c]+DECODE_BUFFER_SIZE-N;
   } while (++c < CC);
   effEnd = end;
   if (effEnd > CELT_NB_EBANDS) effEnd = CELT_NB_EBANDS;
   if (data == NULL || len <= 1) {
      fl_celt_decode_lost(st, N, LM);
      fl_deemphasis(out_syn, pcm, N, CC, st->downsample, st->preemph_memD, accum);
      return frame_size/st->downsample;
   }
   if (st->loss_duration == 0) st->skip_plc = 0;
   if (dec == NULL) {
      ec_dec_init(&_dec, (unsigned char*)data, len);
      dec = &_dec;
   }
   if (C == 1)
      for (i = 0; i < CELT_NB_EBANDS; i++) st->oldBandE[i] = FL_MAX(st->oldBandE[i], st->oldBandE[CELT_NB_EBANDS+i]);
   total_bits = len*8;
   tell = ec_tell(dec);
   if (tell >= total_bits) silence = 1;
   else if (tell == 1) silence = ec_dec_bit_logp(dec, 15);
   else silence = 0;
   if (silence) {
      tell = len*8;
      dec->nbits_total += tell-ec_tell(dec);
   }
   postfilter_gain = 0;
   postfilter_pitch = 0;
   postfilter_tapset = 0;
   if (start == 0 && tell+16 <= total_bits) {
      if (ec_dec_bit_logp(dec, 1)) {
         int qg, octave;
         octave = ec_dec_uint(dec, 6);
         postfilter_pitch = (16<<octave)+ec_dec_bits(dec, 4+octave)-1;
         qg = ec_dec_bits(dec, 3);
         if (ec_tell(dec)+2 <= total_bits) postfilter_tapset = ec_dec_icdf(dec, fl_tapset_icdf, 2);
         postfilter_gain = .09375f*(qg+1);
      }
      tell = ec_tell(dec);
   }
   if (LM > 0 && tell+3 <= total_bits) {
      isTransient = ec_dec_bit_logp(dec, 3);
      tell = ec_tell(dec);
   } else isTransient = 0;
   shortBlocks = isTransient ? M : 0;
   intra_ener = tell+3 <= total_bits ? ec_dec_bit_logp(dec, 3) : 0;
   if (!intra_ener && st->loss_duration != 0) {
      c = 0;
      do {
         float safety = 0;
         int missing = IMIN(10, st->loss_duration>>LM);
         if (LM == 0) safety = 1.5f;
         else if (LM == 1) safety = .5f;
         for (i = start; i < end; i++) {
            float *oe = &st->oldBandE[c*CELT_NB_EBANDS+i];
            float E1 = st->oldLogE[c*CELT_NB_EBANDS+i], E2 = st->oldLogE2[c*CELT_NB_EBANDS+i];
            if (*oe < FL_MAX(E1, E2)) {
               float slope, E0 = *oe;
               slope = FL_MAX(E1-E0, .5f*(E2-E0));
               slope = FL_MIN(slope, 2.f);
               E0 -= FL_MAX(0, (1+missing)*slope);
               *oe = FL_MAX(-20.f, E0);
            } else {
               *oe = FL_MIN(FL_MIN(*oe, E1), E2);
            }
            *oe -= safety;
         }
      } while (++c < 2);
   }
   fl_unquant_coarse_energy(start, end, st->oldBandE, intra_ener, dec, C, LM);
   fl_tf_decode(start, end, isTransient, tf_res, LM, dec);
   tell = ec_tell(dec);
   spread_decision = SPREAD_NORMAL;
   if (tell+4 <= total_bits) spread_decision = ec_dec_icdf(dec, fl_spread_icdf, 5);
   fl_init_caps(cap, LM, C);
   dynalloc_logp = 6;
   total_bits <<= BITRES;
   tell = ec_tell_frac(dec);
   for (i = start; i < end; i++) {
      int width, quanta, dynalloc_loop_logp, boost;
      width = C*(eb[i+1]-eb[i])<<LM;
      quanta = IMIN(width<<BITRES, IMAX(6<<BITRES, width));
      dynalloc_loop_logp = dynalloc_logp;
      boost = 0;
      while (tell+(dynalloc_loop_logp<<BITRES) < total_bits && boost < cap[i]) {
         int flag = ec_dec_bit_logp(dec, dynalloc_loop_logp);
         tell = ec_tell_frac(dec);
         if (!flag) break;
         boost += quanta;
         total_bits -= quanta;
         dynalloc_loop_logp = 1;
      }
      offsets[i] = boost;
      if (boost > 0) dynalloc_logp = IMAX(2, dynalloc_logp-1);
   }
   alloc_trim = tell+(6<<BITRES) <= total_bits ? ec_dec_icdf(dec, fl_trim_icdf, 7) : 5;
   bits = (((fl_i32)len*8)<<BITRES)-(fl_i32)ec_tell_frac(dec)-1;
   anti_collapse_rsv = isTransient && LM >= 2 && bits >= ((LM+2)<<BITRES) ? (1<<BITRES) : 0;
   bits -= anti_collapse_rsv;
   codedBands = fl_compute_allocation(start, end, offsets, cap, alloc_trim, &intensity, &dual_stereo, bits, &balance,
         pulses, fine_quant, fine_priority, C, LM, dec, 0, 0, 0);
   fl_unquant_fine_energy(start, end, st->oldBandE, fine_quant, dec, C);
   c = 0;
   do FL_COPY(decode_mem[c], decode_mem[c]+N, DECODE_BUFFER_SIZE-N+CELT_OVERLAP);
   while (++c < CC);
   fl_quant_all_bands_dec(start, end, X, C == 2 ? X+N : NULL, collapse_masks, pulses, shortBlocks, spread_decision,
         dual_stereo, intensity, tf_res, len*(8<<BITRES)-anti_collapse_rsv, balance, dec, LM, codedBands, &st->rng,
         st->disable_inv);
   if (anti_collapse_rsv > 0) anti_collapse_on = ec_dec_bits(dec, 1);
   fl_unquant_energy_finalise(start, end, st->oldBandE, fine_quant, fine_priority, len*8-ec_tell(dec), dec, C);
   if (anti_collapse_on)
      fl_anti_collapse(X, collapse_masks, LM, C, N, start, end, st->oldBandE, st->oldLogE, st->oldLogE2, pulses, st->rng);
   if (silence)
      for (i = 0; i < C*CELT_NB_EBANDS; i++) st->oldBandE[i] = -28.f;
   if (st->prefilter_and_fold) fl_prefilter_and_fold(st, N);
   fl_celt_synthesis(X, out_syn, st->oldBandE, start, effEnd, C, CC, isTransient, LM, st->downsample, silence);
   c = 0;
   do {
      st->postfilter_period = IMAX(st->postfilter_period, COMBFILTER_MINPERIOD);
      st->postfilter_period_old = IMAX(st->postfilter_period_old, COMBFILTER_MINPERIOD);
      fl_comb_filter(out_syn[c], out_syn[c], st->postfilter_period_old, st->postfilter_period, CELT_SHORT_MDCT,
            st->postfilter_gain_old, st->postfilter_gain, st->postfilter_tapset_old, st->postfilter_tapset,
            fl_window120, CELT_OVERLAP);
      if (LM != 0)
         fl_comb_filter(out_syn[c]+CELT_SHORT_MDCT, out_syn[c]+CELT_SHORT_MDCT, st->postfilter_period, postfilter_pitch,
               N-CELT_SHORT_MDCT, st->postfilter_gain, postfilter_gain, st->postfilter_tapset, postfilter_tapset,
               fl_window120, CELT_OVERLAP);
   } while (++c < CC);
   st->postfilter_period_old = st->postfilter_period;
   st->postfilter_gain_old = st->postfilter_gain;
   st->postfilter_tapset_old = st->postfilter_tapset;
   st->postfilter_period = postfilter_pitch;
   st->postfilter_gain = postfilter_gain;
   st->postfilter_tapset = postfilter_tapset;
   if (LM != 0) {
      st->postfilter_period_old = st->postfilter_period;
      st->postfilter_gain_old = st->postfilter_gain;
      st->postfilter_tapset_old = st->postfilter_tapset;
   }
   if (C == 1) FL_COPY(&st->oldBandE[CELT_NB_EBANDS], st->oldBandE, CELT_NB_EBANDS);
   if (!isTransient) {
      FL_COPY(st->oldLogE2, st->oldLogE, 2*CELT_NB_EBANDS);
      FL_COPY(st->oldLogE, st->oldBandE, 2*CELT_NB_EBANDS);
   } else {
      for (i = 0; i < 2*CELT_NB_EBANDS; i++) st->oldLogE[i] = FL_MIN(st->oldLogE[i], st->oldBandE[i]);
   }
   max_background_increase = IMIN(160, st->loss_duration+M)*0.001f;
   for (i = 0; i < 2*CELT_NB_EBANDS; i++)
      st->backgroundLogE[i] = FL_MIN(st->backgroundLogE[i]+max_background_increase, st->oldBandE[i]);
   c = 0;
   do {
      for (i = 0; i < start; i++) {
         st->oldBandE[c*CELT_NB_EBANDS+i] = 0;
         st->oldLogE[c*CELT_NB_EBANDS+i] = st->oldLogE2[c*CELT_NB_EBANDS+i] = -28.f;
      }
      for (i = end; i < CELT_NB_EBANDS; i++) {
         st->oldBandE[c*CELT_NB_EBANDS+i] = 0;
         st->oldLogE[c*CELT_NB_EBANDS+i] = st->oldLogE2[c*CELT_NB_EBANDS+i] = -28.f;
      }
   } while (++c < 2);
   st->rng = dec->rng;
   fl_deemphasis(out_syn, pcm, N, CC, st->downsample, st->preemph_memD, accum);
   st->loss_duration = 0;
   st->plc_duration = 0;
   st->last_frame_type = FRAME_NORMAL;
   st->prefilter_and_fold = 0;
   if (ec_tell(dec) > 8*len) return -3;
   if (dec->error) st->error = 1;
   return frame_size/st->downsample;
}

/* ======== silk_common.c ======== */
/* ---- SILK: fixed-point macros and routines shared by encoder and decoder ---- */
#define silk_int32_MAX 0x7FFFFFFF
#define silk_int32_MIN ((fl_i32)0x80000000)
#define silk_int16_MAX 0x7FFF
#define silk_int16_MIN ((fl_i16)0x8000)
#define silk_int8_MAX 0x7F
#define silk_int8_MIN ((fl_i8)0x80)
#define silk_uint8_MAX 0xFF

#define silk_SMULWB(a32,b32) ((fl_i32)(((a32)*(fl_i64)((fl_i16)(b32)))>>16))
#define silk_SMLAWB(a32,b32,c32) ((fl_i32)((a32)+(((b32)*(fl_i64)((fl_i16)(c32)))>>16)))
#define silk_SMULWT(a32,b32) ((fl_i32)(((a32)*(fl_i64)((b32)>>16))>>16))
#define silk_SMLAWT(a32,b32,c32) ((fl_i32)((a32)+(((b32)*((fl_i64)(c32)>>16))>>16)))
#define silk_SMULBB(a32,b32) ((fl_i32)((fl_i16)(a32))*(fl_i32)((fl_i16)(b32)))
#define silk_SMLABB(a32,b32,c32) ((a32)+((fl_i32)((fl_i16)(b32)))*(fl_i32)((fl_i16)(c32)))
#define silk_SMULBT(a32,b32) ((fl_i32)((fl_i16)(a32))*((b32)>>16))
#define silk_SMLABT(a32,b32,c32) ((a32)+((fl_i32)((fl_i16)(b32)))*((c32)>>16))
#define silk_SMLAL(a64,b32,c32) ((a64)+((fl_i64)(b32)*(fl_i64)(c32)))
#define silk_SMULWW(a32,b32) ((fl_i32)(((fl_i64)(a32)*(b32))>>16))
#define silk_SMLAWW(a32,b32,c32) ((fl_i32)((a32)+(((fl_i64)(b32)*(c32))>>16)))
#define silk_ADD_SAT32(a,b) ((((fl_u32)(a)+(fl_u32)(b))&0x80000000) == 0 ? \
   ((((a)&(b))&0x80000000) != 0 ? silk_int32_MIN : (a)+(b)) : \
   ((((a)|(b))&0x80000000) == 0 ? silk_int32_MAX : (a)+(b)))
#define silk_SUB_SAT32(a,b) ((((fl_u32)(a)-(fl_u32)(b))&0x80000000) == 0 ? \
   (((a)&((b)^0x80000000)&0x80000000) ? silk_int32_MIN : (a)-(b)) : \
   ((((a)^0x80000000)&(b)&0x80000000) ? silk_int32_MAX : (a)-(b)))
#define silk_MUL(a32,b32) ((a32)*(b32))
#define silk_MLA(a32,b32,c32) ((a32)+((b32)*(c32)))
#define silk_SMULTT(a32,b32) (((a32)>>16)*((b32)>>16))
#define silk_SMLATT(a32,b32,c32) ((a32)+((b32)>>16)*((c32)>>16))
#define silk_SMLALBB(a64,b16,c16) ((a64)+(fl_i64)((fl_i32)(b16)*(fl_i32)(c16)))
#define silk_SMULL(a32,b32) ((fl_i64)(a32)*(b32))
#define silk_ADD32_ovflw(a,b) ((fl_i32)((fl_u32)(a)+(fl_u32)(b)))
#define silk_SUB32_ovflw(a,b) ((fl_i32)((fl_u32)(a)-(fl_u32)(b)))
#define silk_MLA_ovflw(a32,b32,c32) silk_ADD32_ovflw((a32), (fl_u32)(b32)*(fl_u32)(c32))
#define silk_SMLABB_ovflw(a32,b32,c32) (silk_ADD32_ovflw((a32), ((fl_i32)((fl_i16)(b32)))*(fl_i32)((fl_i16)(c32))))
#define silk_DIV32_16(a32,b16) ((fl_i32)((a32)/(b16)))
#define silk_DIV32(a32,b32) ((fl_i32)((a32)/(b32)))
#define silk_ADD32(a,b) ((a)+(b))
#define silk_SUB32(a,b) ((a)-(b))
#define silk_SAT16(a) ((a) > silk_int16_MAX ? silk_int16_MAX : ((a) < silk_int16_MIN ? silk_int16_MIN : (a)))
#define silk_SAT32(a) ((a) > silk_int32_MAX ? silk_int32_MAX : ((a) < silk_int32_MIN ? silk_int32_MIN : (a)))
#define silk_ADD_SAT16(a,b) (fl_i16)silk_SAT16(((fl_i32)(a))+(b))
#define silk_SUB_SAT16(a,b) (fl_i16)silk_SAT16(((fl_i32)(a))-(b))
#define silk_POS_SAT32(a) ((a) > silk_int32_MAX ? silk_int32_MAX : (a))
#define silk_ADD_POS_SAT32(a,b) ((((fl_u32)(a)+(fl_u32)(b))&0x80000000) ? silk_int32_MAX : ((a)+(b)))
#define silk_LSHIFT32(a,shift) ((fl_i32)((fl_u32)(a)<<(shift)))
#define silk_LSHIFT(a,shift) silk_LSHIFT32(a, shift)
#define silk_LSHIFT64(a,shift) ((fl_i64)((fl_u64)(a)<<(shift)))
#define silk_RSHIFT(a,shift) ((a)>>(shift))
#define silk_RSHIFT32(a,shift) ((a)>>(shift))
#define silk_RSHIFT64(a,shift) ((a)>>(shift))
#define silk_LIMIT(a,limit1,limit2) ((limit1) > (limit2) ? ((a) > (limit1) ? (limit1) : ((a) < (limit2) ? (limit2) : (a))) \
   : ((a) > (limit2) ? (limit2) : ((a) < (limit1) ? (limit1) : (a))))
#define silk_LIMIT_int silk_LIMIT
#define silk_LIMIT_16 silk_LIMIT
#define silk_LIMIT_32 silk_LIMIT
#define silk_LSHIFT_SAT32(a,shift) (silk_LSHIFT32(silk_LIMIT((a), silk_RSHIFT32(silk_int32_MIN, (shift)), \
   silk_RSHIFT32(silk_int32_MAX, (shift))), (shift)))
#define silk_LSHIFT_ovflw(a,shift) ((fl_i32)((fl_u32)(a)<<(shift)))
#define silk_ADD_LSHIFT32(a,b,shift) ((a)+silk_LSHIFT32((b), (shift)))
#define silk_ADD_RSHIFT32(a,b,shift) ((a)+silk_RSHIFT32((b), (shift)))
#define silk_ADD_RSHIFT_uint(a,b,shift) ((a)+((b)>>(shift)))
#define silk_SUB_LSHIFT32(a,b,shift) ((a)-silk_LSHIFT32((b), (shift)))
#define silk_SUB_RSHIFT32(a,b,shift) ((a)-silk_RSHIFT32((b), (shift)))
#define silk_RSHIFT_ROUND(a,shift) ((shift) == 1 ? ((a)>>1)+((a)&1) : (((a)>>((shift)-1))+1)>>1)
#define silk_RSHIFT_ROUND64(a,shift) ((shift) == 1 ? ((a)>>1)+((a)&1) : (((a)>>((shift)-1))+1)>>1)
#define silk_min(a,b) (((a) < (b)) ? (a) : (b))
#define silk_max(a,b) (((a) > (b)) ? (a) : (b))
#define silk_min_int silk_min
#define silk_max_int silk_max
#define silk_min_16 silk_min
#define silk_max_16 silk_max
#define silk_min_32 silk_min
#define silk_max_32 silk_max
#define silk_abs(a) (((a) > 0) ? (a) : -(a))
#define silk_abs_int32(a) (((a)^((a)>>31))-((a)>>31))
#define SILK_FIX_CONST(C,Q) ((fl_i32)((C)*((fl_i64)1<<(Q))+0.5))
#define silk_RAND(seed) (silk_MLA_ovflw(907633515, (seed), 196314165))
#define silk_SMMUL(a32,b32) (fl_i32)silk_RSHIFT64(silk_SMULL((a32), (b32)), 32)

/* SILK constants */
#define MAX_FRAMES_PER_PACKET 3
#define MAX_FS_KHZ 16
#define MAX_API_FS_KHZ 48
#define TYPE_NO_VOICE_ACTIVITY 0
#define TYPE_UNVOICED 1
#define TYPE_VOICED 2
#define CODE_INDEPENDENTLY 0
#define CODE_INDEPENDENTLY_NO_LTP_SCALING 1
#define CODE_CONDITIONALLY 2
#define STEREO_QUANT_TAB_SIZE 16
#define STEREO_QUANT_SUB_STEPS 5
#define STEREO_INTERP_LEN_MS 8
#define PITCH_EST_MIN_LAG_MS 2
#define PITCH_EST_MAX_LAG_MS 18
#define MAX_NB_SUBFR 4
#define LTP_MEM_LENGTH_MS 20
#define SUB_FRAME_LENGTH_MS 5
#define MAX_SUB_FRAME_LENGTH (SUB_FRAME_LENGTH_MS*MAX_FS_KHZ)
#define MAX_FRAME_LENGTH_MS (SUB_FRAME_LENGTH_MS*MAX_NB_SUBFR)
#define MAX_FRAME_LENGTH (MAX_FRAME_LENGTH_MS*MAX_FS_KHZ)
#define N_LEVELS_QGAIN 64
#define MAX_DELTA_GAIN_QUANT 36
#define MIN_DELTA_GAIN_QUANT -4
#define MIN_QGAIN_DB 2
#define MAX_QGAIN_DB 88
#define OFFSET_VL_Q10 32
#define OFFSET_VH_Q10 100
#define OFFSET_UVL_Q10 100
#define OFFSET_UVH_Q10 240
#define QUANT_LEVEL_ADJUST_Q10 80
#define MAX_LPC_STABILIZE_ITERATIONS 16
#define MAX_PREDICTION_POWER_GAIN 1e4f
#define MAX_LPC_ORDER 16
#define MIN_LPC_ORDER 10
#define LTP_ORDER 5
#define NB_LTP_CBKS 3
#define SHELL_CODEC_FRAME_LENGTH 16
#define LOG2_SHELL_CODEC_FRAME_LENGTH 4
#define MAX_NB_SHELL_BLOCKS (MAX_FRAME_LENGTH/SHELL_CODEC_FRAME_LENGTH)
#define N_RATE_LEVELS 10
#define SILK_MAX_PULSES 16
#define LSF_COS_TAB_SZ_FIX 128
#define NLSF_QUANT_MAX_AMPLITUDE 4
#define NLSF_QUANT_LEVEL_ADJ 0.1
#define TRANSITION_NB 3
#define TRANSITION_NA 2
#define TRANSITION_INT_NUM 5
#define BWE_AFTER_LOSS_Q16 63570
#define CNG_BUF_MASK_MAX 255
#define CNG_GAIN_SMTH_Q16 4634
#define CNG_GAIN_SMTH_THRESHOLD_Q16 46396
#define CNG_NLSF_SMTH_Q16 16348
#define SILK_MAX_ORDER_LPC 24
#define PE_MAX_NB_SUBFR 4
#define PE_MAX_LAG_MS 18
#define PE_MIN_LAG_MS 2
#define PE_NB_CBKS_STAGE2 3
#define PE_NB_CBKS_STAGE2_EXT 11
#define PE_NB_CBKS_STAGE3_MAX 34
#define PE_NB_CBKS_STAGE3_MID 24
#define PE_NB_CBKS_STAGE3_MIN 16
#define PE_NB_CBKS_STAGE3_10MS 12
#define PE_NB_CBKS_STAGE2_10MS 3
#define SILK_PE_MAX_COMPLEX 2
#define RESAMPLER_DOWN_ORDER_FIR0 18
#define RESAMPLER_DOWN_ORDER_FIR1 24
#define RESAMPLER_DOWN_ORDER_FIR2 36
#define RESAMPLER_ORDER_FIR_12 8
#define FLAG_DECODE_NORMAL 0
#define FLAG_PACKET_LOST 1
#define FLAG_DECODE_LBRR 2

typedef struct {
   const fl_i16 nVectors;
   const fl_i16 order;
   const fl_i16 quantStepSize_Q16;
   const fl_i16 invQuantStepSize_Q6;
   const fl_u8 *CB1_NLSF_Q8;
   const fl_i16 *CB1_Wght_Q9;
   const fl_u8 *CB1_iCDF;
   const fl_u8 *pred_Q8;
   const fl_u8 *ec_sel;
   const fl_u8 *ec_iCDF;
   const fl_u8 *ec_Rates_Q5;
   const fl_i16 *deltaMin_Q15;
} fl_nlsf_cb;

/* SILK constant tables (from the Opus reference). */
static const fl_u8 fl_LTP_per_index_iCDF[3] = {
       179, 99, 0
};

static const fl_u8 fl_LTP_gain_iCDF_0[8] = {
        71, 56, 43, 30, 21, 12, 6, 0
};

static const fl_u8 fl_LTP_gain_iCDF_1[16] = {
       199, 165, 144, 124, 109, 96, 84, 71,
        61, 51, 42, 32, 23, 15, 8, 0
};

static const fl_u8 fl_LTP_gain_iCDF_2[32] = {
       241, 225, 211, 199, 187, 175, 164, 153,
       142, 132, 123, 114, 105, 96, 88, 80,
        72, 64, 57, 50, 44, 38, 33, 29,
        24, 20, 16, 12, 9, 5, 2, 0
};

static const fl_u8 fl_LTP_gain_BITS_Q5_0[8] = {
        15, 131, 138, 138, 155, 155, 173, 173
};

static const fl_u8 fl_LTP_gain_BITS_Q5_1[16] = {
        69, 93, 115, 118, 131, 138, 141, 138,
       150, 150, 155, 150, 155, 160, 166, 160
};

static const fl_u8 fl_LTP_gain_BITS_Q5_2[32] = {
       131, 128, 134, 141, 141, 141, 145, 145,
       145, 150, 155, 155, 155, 155, 160, 160,
       160, 160, 166, 166, 173, 173, 182, 192,
       182, 192, 192, 192, 205, 192, 205, 224
};

static const fl_u8 *const fl_LTP_gain_iCDF_ptrs[NB_LTP_CBKS] = {
    fl_LTP_gain_iCDF_0,
    fl_LTP_gain_iCDF_1,
    fl_LTP_gain_iCDF_2
};

static const fl_u8 *const fl_LTP_gain_BITS_Q5_ptrs[NB_LTP_CBKS] = {
    fl_LTP_gain_BITS_Q5_0,
    fl_LTP_gain_BITS_Q5_1,
    fl_LTP_gain_BITS_Q5_2
};

static const fl_i8 fl_LTP_gain_vq_0[8][5] = {
{
         4, 6, 24, 7, 5
},
{
         0, 0, 2, 0, 0
},
{
        12, 28, 41, 13, -4
},
{
        -9, 15, 42, 25, 14
},
{
         1, -2, 62, 41, -9
},
{
       -10, 37, 65, -4, 3
},
{
        -6, 4, 66, 7, -8
},
{
        16, 14, 38, -3, 33
}
};

static const fl_i8 fl_LTP_gain_vq_1[16][5] = {
{
        13, 22, 39, 23, 12
},
{
        -1, 36, 64, 27, -6
},
{
        -7, 10, 55, 43, 17
},
{
         1, 1, 8, 1, 1
},
{
         6, -11, 74, 53, -9
},
{
       -12, 55, 76, -12, 8
},
{
        -3, 3, 93, 27, -4
},
{
        26, 39, 59, 3, -8
},
{
         2, 0, 77, 11, 9
},
{
        -8, 22, 44, -6, 7
},
{
        40, 9, 26, 3, 9
},
{
        -7, 20, 101, -7, 4
},
{
         3, -8, 42, 26, 0
},
{
       -15, 33, 68, 2, 23
},
{
        -2, 55, 46, -2, 15
},
{
         3, -1, 21, 16, 41
}
};

static const fl_i8 fl_LTP_gain_vq_2[32][5] = {
{
        -6, 27, 61, 39, 5
},
{
       -11, 42, 88, 4, 1
},
{
        -2, 60, 65, 6, -4
},
{
        -1, -5, 73, 56, 1
},
{
        -9, 19, 94, 29, -9
},
{
         0, 12, 99, 6, 4
},
{
         8, -19, 102, 46, -13
},
{
         3, 2, 13, 3, 2
},
{
         9, -21, 84, 72, -18
},
{
       -11, 46, 104, -22, 8
},
{
        18, 38, 48, 23, 0
},
{
       -16, 70, 83, -21, 11
},
{
         5, -11, 117, 22, -8
},
{
        -6, 23, 117, -12, 3
},
{
         3, -8, 95, 28, 4
},
{
       -10, 15, 77, 60, -15
},
{
        -1, 4, 124, 2, -4
},
{
         3, 38, 84, 24, -25
},
{
         2, 13, 42, 13, 31
},
{
        21, -4, 56, 46, -1
},
{
        -1, 35, 79, -13, 19
},
{
        -7, 65, 88, -9, -14
},
{
        20, 4, 81, 49, -29
},
{
        20, 0, 75, 3, -17
},
{
         5, -9, 44, 92, -8
},
{
         1, -3, 22, 69, 31
},
{
        -6, 95, 41, -12, 5
},
{
        39, 67, 16, -4, 1
},
{
         0, -6, 120, 55, -36
},
{
       -13, 44, 122, 4, -24
},
{
        81, 5, 11, 3, 7
},
{
         2, 0, 9, 10, 88
}
};

static const fl_i8 *const fl_LTP_vq_ptrs_Q7[NB_LTP_CBKS] = {
    (fl_i8 *)&fl_LTP_gain_vq_0[0][0],
    (fl_i8 *)&fl_LTP_gain_vq_1[0][0],
    (fl_i8 *)&fl_LTP_gain_vq_2[0][0]
};

static const fl_u8 fl_LTP_gain_vq_0_gain[8] = {
      46, 2, 90, 87, 93, 91, 82, 98
};

static const fl_u8 fl_LTP_gain_vq_1_gain[16] = {
     109, 120, 118, 12, 113, 115, 117, 119,
      99, 59, 87, 111, 63, 111, 112, 80
};

static const fl_u8 fl_LTP_gain_vq_2_gain[32] = {
     126, 124, 125, 124, 129, 121, 126, 23,
     132, 127, 127, 127, 126, 127, 122, 133,
     130, 134, 101, 118, 119, 145, 126, 86,
     124, 120, 123, 119, 170, 173, 107, 109
};

static const fl_u8 *const fl_LTP_vq_gain_ptrs_Q7[NB_LTP_CBKS] = {
    &fl_LTP_gain_vq_0_gain[0],
    &fl_LTP_gain_vq_1_gain[0],
    &fl_LTP_gain_vq_2_gain[0]
};

static const fl_i8 fl_LTP_vq_sizes[NB_LTP_CBKS] = {
    8, 16, 32
};

static const fl_u8 fl_NLSF_CB1_NB_MB_Q8[320] = {
        12, 35, 60, 83, 108, 132, 157, 180,
       206, 228, 15, 32, 55, 77, 101, 125,
       151, 175, 201, 225, 19, 42, 66, 89,
       114, 137, 162, 184, 209, 230, 12, 25,
        50, 72, 97, 120, 147, 172, 200, 223,
        26, 44, 69, 90, 114, 135, 159, 180,
       205, 225, 13, 22, 53, 80, 106, 130,
       156, 180, 205, 228, 15, 25, 44, 64,
        90, 115, 142, 168, 196, 222, 19, 24,
        62, 82, 100, 120, 145, 168, 190, 214,
        22, 31, 50, 79, 103, 120, 151, 170,
       203, 227, 21, 29, 45, 65, 106, 124,
       150, 171, 196, 224, 30, 49, 75, 97,
       121, 142, 165, 186, 209, 229, 19, 25,
        52, 70, 93, 116, 143, 166, 192, 219,
        26, 34, 62, 75, 97, 118, 145, 167,
       194, 217, 25, 33, 56, 70, 91, 113,
       143, 165, 196, 223, 21, 34, 51, 72,
        97, 117, 145, 171, 196, 222, 20, 29,
        50, 67, 90, 117, 144, 168, 197, 221,
        22, 31, 48, 66, 95, 117, 146, 168,
       196, 222, 24, 33, 51, 77, 116, 134,
       158, 180, 200, 224, 21, 28, 70, 87,
       106, 124, 149, 170, 194, 217, 26, 33,
        53, 64, 83, 117, 152, 173, 204, 225,
        27, 34, 65, 95, 108, 129, 155, 174,
       210, 225, 20, 26, 72, 99, 113, 131,
       154, 176, 200, 219, 34, 43, 61, 78,
        93, 114, 155, 177, 205, 229, 23, 29,
        54, 97, 124, 138, 163, 179, 209, 229,
        30, 38, 56, 89, 118, 129, 158, 178,
       200, 231, 21, 29, 49, 63, 85, 111,
       142, 163, 193, 222, 27, 48, 77, 103,
       133, 158, 179, 196, 215, 232, 29, 47,
        74, 99, 124, 151, 176, 198, 220, 237,
        33, 42, 61, 76, 93, 121, 155, 174,
       207, 225, 29, 53, 87, 112, 136, 154,
       170, 188, 208, 227, 24, 30, 52, 84,
       131, 150, 166, 186, 203, 229, 37, 48,
        64, 84, 104, 118, 156, 177, 201, 230
};

static const fl_i16 fl_NLSF_CB1_Wght_Q9[320] = {
     2897, 2314, 2314, 2314, 2287, 2287, 2314, 2300, 2327, 2287,
     2888, 2580, 2394, 2367, 2314, 2274, 2274, 2274, 2274, 2194,
     2487, 2340, 2340, 2314, 2314, 2314, 2340, 2340, 2367, 2354,
     3216, 2766, 2340, 2340, 2314, 2274, 2221, 2207, 2261, 2194,
     2460, 2474, 2367, 2394, 2394, 2394, 2394, 2367, 2407, 2314,
     3479, 3056, 2127, 2207, 2274, 2274, 2274, 2287, 2314, 2261,
     3282, 3141, 2580, 2394, 2247, 2221, 2207, 2194, 2194, 2114,
     4096, 3845, 2221, 2620, 2620, 2407, 2314, 2394, 2367, 2074,
     3178, 3244, 2367, 2221, 2553, 2434, 2340, 2314, 2167, 2221,
     3338, 3488, 2726, 2194, 2261, 2460, 2354, 2367, 2207, 2101,
     2354, 2420, 2327, 2367, 2394, 2420, 2420, 2420, 2460, 2367,
     3779, 3629, 2434, 2527, 2367, 2274, 2274, 2300, 2207, 2048,
     3254, 3225, 2713, 2846, 2447, 2327, 2300, 2300, 2274, 2127,
     3263, 3300, 2753, 2806, 2447, 2261, 2261, 2247, 2127, 2101,
     2873, 2981, 2633, 2367, 2407, 2354, 2194, 2247, 2247, 2114,
     3225, 3197, 2633, 2580, 2274, 2181, 2247, 2221, 2221, 2141,
     3178, 3310, 2740, 2407, 2274, 2274, 2274, 2287, 2194, 2114,
     3141, 3272, 2460, 2061, 2287, 2500, 2367, 2487, 2434, 2181,
     3507, 3282, 2314, 2700, 2647, 2474, 2367, 2394, 2340, 2127,
     3423, 3535, 3038, 3056, 2300, 1950, 2221, 2274, 2274, 2274,
     3404, 3366, 2087, 2687, 2873, 2354, 2420, 2274, 2474, 2540,
     3760, 3488, 1950, 2660, 2897, 2527, 2394, 2367, 2460, 2261,
     3028, 3272, 2740, 2888, 2740, 2154, 2127, 2287, 2234, 2247,
     3695, 3657, 2025, 1969, 2660, 2700, 2580, 2500, 2327, 2367,
     3207, 3413, 2354, 2074, 2888, 2888, 2340, 2487, 2247, 2167,
     3338, 3366, 2846, 2780, 2327, 2154, 2274, 2287, 2114, 2061,
     2327, 2300, 2181, 2167, 2181, 2367, 2633, 2700, 2700, 2553,
     2407, 2434, 2221, 2261, 2221, 2221, 2340, 2420, 2607, 2700,
     3038, 3244, 2806, 2888, 2474, 2074, 2300, 2314, 2354, 2380,
     2221, 2154, 2127, 2287, 2500, 2793, 2793, 2620, 2580, 2367,
     3676, 3713, 2234, 1838, 2181, 2753, 2726, 2673, 2513, 2207,
     2793, 3160, 2726, 2553, 2846, 2513, 2181, 2394, 2221, 2181
};

static const fl_u8 fl_NLSF_CB1_iCDF_NB_MB[64] = {
       212, 178, 148, 129, 108, 96, 85, 82,
        79, 77, 61, 59, 57, 56, 51, 49,
        48, 45, 42, 41, 40, 38, 36, 34,
        31, 30, 21, 12, 10, 3, 1, 0,
       255, 245, 244, 236, 233, 225, 217, 203,
       190, 176, 175, 161, 149, 136, 125, 114,
       102, 91, 81, 71, 60, 52, 43, 35,
        28, 20, 19, 18, 12, 11, 5, 0
};

static const fl_u8 fl_NLSF_CB2_SELECT_NB_MB[160] = {
        16, 0, 0, 0, 0, 99, 66, 36,
        36, 34, 36, 34, 34, 34, 34, 83,
        69, 36, 52, 34, 116, 102, 70, 68,
        68, 176, 102, 68, 68, 34, 65, 85,
        68, 84, 36, 116, 141, 152, 139, 170,
       132, 187, 184, 216, 137, 132, 249, 168,
       185, 139, 104, 102, 100, 68, 68, 178,
       218, 185, 185, 170, 244, 216, 187, 187,
       170, 244, 187, 187, 219, 138, 103, 155,
       184, 185, 137, 116, 183, 155, 152, 136,
       132, 217, 184, 184, 170, 164, 217, 171,
       155, 139, 244, 169, 184, 185, 170, 164,
       216, 223, 218, 138, 214, 143, 188, 218,
       168, 244, 141, 136, 155, 170, 168, 138,
       220, 219, 139, 164, 219, 202, 216, 137,
       168, 186, 246, 185, 139, 116, 185, 219,
       185, 138, 100, 100, 134, 100, 102, 34,
        68, 68, 100, 68, 168, 203, 221, 218,
       168, 167, 154, 136, 104, 70, 164, 246,
       171, 137, 139, 137, 155, 218, 219, 139
};

static const fl_u8 fl_NLSF_CB2_iCDF_NB_MB[72] = {
       255, 254, 253, 238, 14, 3, 2, 1,
         0, 255, 254, 252, 218, 35, 3, 2,
         1, 0, 255, 254, 250, 208, 59, 4,
         2, 1, 0, 255, 254, 246, 194, 71,
        10, 2, 1, 0, 255, 252, 236, 183,
        82, 8, 2, 1, 0, 255, 252, 235,
       180, 90, 17, 2, 1, 0, 255, 248,
       224, 171, 97, 30, 4, 1, 0, 255,
       254, 236, 173, 95, 37, 7, 1, 0
};

static const fl_u8 fl_NLSF_CB2_BITS_NB_MB_Q5[72] = {
       255, 255, 255, 131, 6, 145, 255, 255,
       255, 255, 255, 236, 93, 15, 96, 255,
       255, 255, 255, 255, 194, 83, 25, 71,
       221, 255, 255, 255, 255, 162, 73, 34,
        66, 162, 255, 255, 255, 210, 126, 73,
        43, 57, 173, 255, 255, 255, 201, 125,
        71, 48, 58, 130, 255, 255, 255, 166,
       110, 73, 57, 62, 104, 210, 255, 255,
       251, 123, 65, 55, 68, 100, 171, 255
};

static const fl_u8 fl_NLSF_PRED_NB_MB_Q8[18] = {
       179, 138, 140, 148, 151, 149, 153, 151,
       163, 116, 67, 82, 59, 92, 72, 100,
        89, 92
};

static const fl_i16 fl_NLSF_DELTA_MIN_NB_MB_Q15[11] = {
       250, 3, 6, 3, 3, 3, 4, 3,
         3, 3, 461
};

static const fl_nlsf_cb fl_NLSF_CB_NB_MB = {
    32,
    10,
    SILK_FIX_CONST( 0.18, 16 ),
    SILK_FIX_CONST( 1.0 / 0.18, 6 ),
    fl_NLSF_CB1_NB_MB_Q8,
    fl_NLSF_CB1_Wght_Q9,
    fl_NLSF_CB1_iCDF_NB_MB,
    fl_NLSF_PRED_NB_MB_Q8,
    fl_NLSF_CB2_SELECT_NB_MB,
    fl_NLSF_CB2_iCDF_NB_MB,
    fl_NLSF_CB2_BITS_NB_MB_Q5,
    fl_NLSF_DELTA_MIN_NB_MB_Q15,
};

static const fl_u8 fl_NLSF_CB1_WB_Q8[512] = {
         7, 23, 38, 54, 69, 85, 100, 116,
       131, 147, 162, 178, 193, 208, 223, 239,
        13, 25, 41, 55, 69, 83, 98, 112,
       127, 142, 157, 171, 187, 203, 220, 236,
        15, 21, 34, 51, 61, 78, 92, 106,
       126, 136, 152, 167, 185, 205, 225, 240,
        10, 21, 36, 50, 63, 79, 95, 110,
       126, 141, 157, 173, 189, 205, 221, 237,
        17, 20, 37, 51, 59, 78, 89, 107,
       123, 134, 150, 164, 184, 205, 224, 240,
        10, 15, 32, 51, 67, 81, 96, 112,
       129, 142, 158, 173, 189, 204, 220, 236,
         8, 21, 37, 51, 65, 79, 98, 113,
       126, 138, 155, 168, 179, 192, 209, 218,
        12, 15, 34, 55, 63, 78, 87, 108,
       118, 131, 148, 167, 185, 203, 219, 236,
        16, 19, 32, 36, 56, 79, 91, 108,
       118, 136, 154, 171, 186, 204, 220, 237,
        11, 28, 43, 58, 74, 89, 105, 120,
       135, 150, 165, 180, 196, 211, 226, 241,
         6, 16, 33, 46, 60, 75, 92, 107,
       123, 137, 156, 169, 185, 199, 214, 225,
        11, 19, 30, 44, 57, 74, 89, 105,
       121, 135, 152, 169, 186, 202, 218, 234,
        12, 19, 29, 46, 57, 71, 88, 100,
       120, 132, 148, 165, 182, 199, 216, 233,
        17, 23, 35, 46, 56, 77, 92, 106,
       123, 134, 152, 167, 185, 204, 222, 237,
        14, 17, 45, 53, 63, 75, 89, 107,
       115, 132, 151, 171, 188, 206, 221, 240,
         9, 16, 29, 40, 56, 71, 88, 103,
       119, 137, 154, 171, 189, 205, 222, 237,
        16, 19, 36, 48, 57, 76, 87, 105,
       118, 132, 150, 167, 185, 202, 218, 236,
        12, 17, 29, 54, 71, 81, 94, 104,
       126, 136, 149, 164, 182, 201, 221, 237,
        15, 28, 47, 62, 79, 97, 115, 129,
       142, 155, 168, 180, 194, 208, 223, 238,
         8, 14, 30, 45, 62, 78, 94, 111,
       127, 143, 159, 175, 192, 207, 223, 239,
        17, 30, 49, 62, 79, 92, 107, 119,
       132, 145, 160, 174, 190, 204, 220, 235,
        14, 19, 36, 45, 61, 76, 91, 108,
       121, 138, 154, 172, 189, 205, 222, 238,
        12, 18, 31, 45, 60, 76, 91, 107,
       123, 138, 154, 171, 187, 204, 221, 236,
        13, 17, 31, 43, 53, 70, 83, 103,
       114, 131, 149, 167, 185, 203, 220, 237,
        17, 22, 35, 42, 58, 78, 93, 110,
       125, 139, 155, 170, 188, 206, 224, 240,
         8, 15, 34, 50, 67, 83, 99, 115,
       131, 146, 162, 178, 193, 209, 224, 239,
        13, 16, 41, 66, 73, 86, 95, 111,
       128, 137, 150, 163, 183, 206, 225, 241,
        17, 25, 37, 52, 63, 75, 92, 102,
       119, 132, 144, 160, 175, 191, 212, 231,
        19, 31, 49, 65, 83, 100, 117, 133,
       147, 161, 174, 187, 200, 213, 227, 242,
        18, 31, 52, 68, 88, 103, 117, 126,
       138, 149, 163, 177, 192, 207, 223, 239,
        16, 29, 47, 61, 76, 90, 106, 119,
       133, 147, 161, 176, 193, 209, 224, 240,
        15, 21, 35, 50, 61, 73, 86, 97,
       110, 119, 129, 141, 175, 198, 218, 237
};

static const fl_i16 fl_NLSF_CB1_WB_Wght_Q9[512] = {
     3657, 2925, 2925, 2925, 2925, 2925, 2925, 2925, 2925, 2925, 2925, 2925, 2963, 2963, 2925, 2846,
     3216, 3085, 2972, 3056, 3056, 3010, 3010, 3010, 2963, 2963, 3010, 2972, 2888, 2846, 2846, 2726,
     3920, 4014, 2981, 3207, 3207, 2934, 3056, 2846, 3122, 3244, 2925, 2846, 2620, 2553, 2780, 2925,
     3516, 3197, 3010, 3103, 3019, 2888, 2925, 2925, 2925, 2925, 2888, 2888, 2888, 2888, 2888, 2753,
     5054, 5054, 2934, 3573, 3385, 3056, 3085, 2793, 3160, 3160, 2972, 2846, 2513, 2540, 2753, 2888,
     4428, 4149, 2700, 2753, 2972, 3010, 2925, 2846, 2981, 3019, 2925, 2925, 2925, 2925, 2888, 2726,
     3620, 3019, 2972, 3056, 3056, 2873, 2806, 3056, 3216, 3047, 2981, 3291, 3291, 2981, 3310, 2991,
     5227, 5014, 2540, 3338, 3526, 3385, 3197, 3094, 3376, 2981, 2700, 2647, 2687, 2793, 2846, 2673,
     5081, 5174, 4615, 4428, 2460, 2897, 3047, 3207, 3169, 2687, 2740, 2888, 2846, 2793, 2846, 2700,
     3122, 2888, 2963, 2925, 2925, 2925, 2925, 2963, 2963, 2963, 2963, 2925, 2925, 2963, 2963, 2963,
     4202, 3207, 2981, 3103, 3010, 2888, 2888, 2925, 2972, 2873, 2916, 3019, 2972, 3010, 3197, 2873,
     3760, 3760, 3244, 3103, 2981, 2888, 2925, 2888, 2972, 2934, 2793, 2793, 2846, 2888, 2888, 2660,
     3854, 4014, 3207, 3122, 3244, 2934, 3047, 2963, 2963, 3085, 2846, 2793, 2793, 2793, 2793, 2580,
     3845, 4080, 3357, 3516, 3094, 2740, 3010, 2934, 3122, 3085, 2846, 2846, 2647, 2647, 2846, 2806,
     5147, 4894, 3225, 3845, 3441, 3169, 2897, 3413, 3451, 2700, 2580, 2673, 2740, 2846, 2806, 2753,
     4109, 3789, 3291, 3160, 2925, 2888, 2888, 2925, 2793, 2740, 2793, 2740, 2793, 2846, 2888, 2806,
     5081, 5054, 3047, 3545, 3244, 3056, 3085, 2944, 3103, 2897, 2740, 2740, 2740, 2846, 2793, 2620,
     4309, 4309, 2860, 2527, 3207, 3376, 3376, 3075, 3075, 3376, 3056, 2846, 2647, 2580, 2726, 2753,
     3056, 2916, 2806, 2888, 2740, 2687, 2897, 3103, 3150, 3150, 3216, 3169, 3056, 3010, 2963, 2846,
     4375, 3882, 2925, 2888, 2846, 2888, 2846, 2846, 2888, 2888, 2888, 2846, 2888, 2925, 2888, 2846,
     2981, 2916, 2916, 2981, 2981, 3056, 3122, 3216, 3150, 3056, 3010, 2972, 2972, 2972, 2925, 2740,
     4229, 4149, 3310, 3347, 2925, 2963, 2888, 2981, 2981, 2846, 2793, 2740, 2846, 2846, 2846, 2793,
     4080, 4014, 3103, 3010, 2925, 2925, 2925, 2888, 2925, 2925, 2846, 2846, 2846, 2793, 2888, 2780,
     4615, 4575, 3169, 3441, 3207, 2981, 2897, 3038, 3122, 2740, 2687, 2687, 2687, 2740, 2793, 2700,
     4149, 4269, 3789, 3657, 2726, 2780, 2888, 2888, 3010, 2972, 2925, 2846, 2687, 2687, 2793, 2888,
     4215, 3554, 2753, 2846, 2846, 2888, 2888, 2888, 2925, 2925, 2888, 2925, 2925, 2925, 2963, 2888,
     5174, 4921, 2261, 3432, 3789, 3479, 3347, 2846, 3310, 3479, 3150, 2897, 2460, 2487, 2753, 2925,
     3451, 3685, 3122, 3197, 3357, 3047, 3207, 3207, 2981, 3216, 3085, 2925, 2925, 2687, 2540, 2434,
     2981, 3010, 2793, 2793, 2740, 2793, 2846, 2972, 3056, 3103, 3150, 3150, 3150, 3103, 3010, 3010,
     2944, 2873, 2687, 2726, 2780, 3010, 3432, 3545, 3357, 3244, 3056, 3010, 2963, 2925, 2888, 2846,
     3019, 2944, 2897, 3010, 3010, 2972, 3019, 3103, 3056, 3056, 3010, 2888, 2846, 2925, 2925, 2888,
     3920, 3967, 3010, 3197, 3357, 3216, 3291, 3291, 3479, 3704, 3441, 2726, 2181, 2460, 2580, 2607
};

static const fl_u8 fl_NLSF_CB1_iCDF_WB[64] = {
       225, 204, 201, 184, 183, 175, 158, 154,
       153, 135, 119, 115, 113, 110, 109, 99,
        98, 95, 79, 68, 52, 50, 48, 45,
        43, 32, 31, 27, 18, 10, 3, 0,
       255, 251, 235, 230, 212, 201, 196, 182,
       167, 166, 163, 151, 138, 124, 110, 104,
        90, 78, 76, 70, 69, 57, 45, 34,
        24, 21, 11, 6, 5, 4, 3, 0
};

static const fl_u8 fl_NLSF_CB2_SELECT_WB[256] = {
         0, 0, 0, 0, 0, 0, 0, 1,
       100, 102, 102, 68, 68, 36, 34, 96,
       164, 107, 158, 185, 180, 185, 139, 102,
        64, 66, 36, 34, 34, 0, 1, 32,
       208, 139, 141, 191, 152, 185, 155, 104,
        96, 171, 104, 166, 102, 102, 102, 132,
         1, 0, 0, 0, 0, 16, 16, 0,
        80, 109, 78, 107, 185, 139, 103, 101,
       208, 212, 141, 139, 173, 153, 123, 103,
        36, 0, 0, 0, 0, 0, 0, 1,
        48, 0, 0, 0, 0, 0, 0, 32,
        68, 135, 123, 119, 119, 103, 69, 98,
        68, 103, 120, 118, 118, 102, 71, 98,
       134, 136, 157, 184, 182, 153, 139, 134,
       208, 168, 248, 75, 189, 143, 121, 107,
        32, 49, 34, 34, 34, 0, 17, 2,
       210, 235, 139, 123, 185, 137, 105, 134,
        98, 135, 104, 182, 100, 183, 171, 134,
       100, 70, 68, 70, 66, 66, 34, 131,
        64, 166, 102, 68, 36, 2, 1, 0,
       134, 166, 102, 68, 34, 34, 66, 132,
       212, 246, 158, 139, 107, 107, 87, 102,
       100, 219, 125, 122, 137, 118, 103, 132,
       114, 135, 137, 105, 171, 106, 50, 34,
       164, 214, 141, 143, 185, 151, 121, 103,
       192, 34, 0, 0, 0, 0, 0, 1,
       208, 109, 74, 187, 134, 249, 159, 137,
       102, 110, 154, 118, 87, 101, 119, 101,
         0, 2, 0, 36, 36, 66, 68, 35,
        96, 164, 102, 100, 36, 0, 2, 33,
       167, 138, 174, 102, 100, 84, 2, 2,
       100, 107, 120, 119, 36, 197, 24, 0
};

static const fl_u8 fl_NLSF_CB2_iCDF_WB[72] = {
       255, 254, 253, 244, 12, 3, 2, 1,
         0, 255, 254, 252, 224, 38, 3, 2,
         1, 0, 255, 254, 251, 209, 57, 4,
         2, 1, 0, 255, 254, 244, 195, 69,
         4, 2, 1, 0, 255, 251, 232, 184,
        84, 7, 2, 1, 0, 255, 254, 240,
       186, 86, 14, 2, 1, 0, 255, 254,
       239, 178, 91, 30, 5, 1, 0, 255,
       248, 227, 177, 100, 19, 2, 1, 0
};

static const fl_u8 fl_NLSF_CB2_BITS_WB_Q5[72] = {
       255, 255, 255, 156, 4, 154, 255, 255,
       255, 255, 255, 227, 102, 15, 92, 255,
       255, 255, 255, 255, 213, 83, 24, 72,
       236, 255, 255, 255, 255, 150, 76, 33,
        63, 214, 255, 255, 255, 190, 121, 77,
        43, 55, 185, 255, 255, 255, 245, 137,
        71, 43, 59, 139, 255, 255, 255, 255,
       131, 66, 50, 66, 107, 194, 255, 255,
       166, 116, 76, 55, 53, 125, 255, 255
};

static const fl_u8 fl_NLSF_PRED_WB_Q8[30] = {
       175, 148, 160, 176, 178, 173, 174, 164,
       177, 174, 196, 182, 198, 192, 182, 68,
        62, 66, 60, 72, 117, 85, 90, 118,
       136, 151, 142, 160, 142, 155
};

static const fl_i16 fl_NLSF_DELTA_MIN_WB_Q15[17] = {
       100, 3, 40, 3, 3, 3, 5, 14,
        14, 10, 11, 3, 8, 9, 7, 3,
       347
};

static const fl_nlsf_cb fl_NLSF_CB_WB = {
    32,
    16,
    SILK_FIX_CONST( 0.15, 16 ),
    SILK_FIX_CONST( 1.0 / 0.15, 6 ),
    fl_NLSF_CB1_WB_Q8,
    fl_NLSF_CB1_WB_Wght_Q9,
    fl_NLSF_CB1_iCDF_WB,
    fl_NLSF_PRED_WB_Q8,
    fl_NLSF_CB2_SELECT_WB,
    fl_NLSF_CB2_iCDF_WB,
    fl_NLSF_CB2_BITS_WB_Q5,
    fl_NLSF_DELTA_MIN_WB_Q15,
};

static const fl_u8 fl_gain_iCDF[3][N_LEVELS_QGAIN/8] = {
{
       224, 112, 44, 15, 3, 2, 1, 0
},
{
       254, 237, 192, 132, 70, 23, 4, 0
},
{
       255, 252, 226, 155, 61, 11, 2, 0
}
};

static const fl_u8 fl_delta_gain_iCDF[MAX_DELTA_GAIN_QUANT-MIN_DELTA_GAIN_QUANT+1] = {
       250, 245, 234, 203, 71, 50, 42, 38,
        35, 33, 31, 29, 28, 27, 26, 25,
        24, 23, 22, 21, 20, 19, 18, 17,
        16, 15, 14, 13, 12, 11, 10, 9,
         8, 7, 6, 5, 4, 3, 2, 1,
         0
};

static const fl_i16 fl_stereo_pred_quant_Q13[STEREO_QUANT_TAB_SIZE] = {
    -13732, -10050, -8266, -7526, -6500, -5000, -2950, -820,
       820, 2950, 5000, 6500, 7526, 8266, 10050, 13732
};

static const fl_u8 fl_stereo_pred_joint_iCDF[25] = {
    249, 247, 246, 245, 244,
    234, 210, 202, 201, 200,
    197, 174, 82, 59, 56,
     55, 54, 46, 22, 12,
     11, 10, 9, 7, 0
};

static const fl_u8 fl_stereo_only_code_mid_iCDF[2] = { 64, 0 };

static const fl_u8 fl_LBRR_flags_2_iCDF[3] = { 203, 150, 0 };

static const fl_u8 fl_LBRR_flags_3_iCDF[7] = { 215, 195, 166, 125, 110, 82, 0 };

static const fl_u8 *const fl_LBRR_flags_iCDF_ptr[2] = {
    fl_LBRR_flags_2_iCDF,
    fl_LBRR_flags_3_iCDF
};

static const fl_u8 fl_lsb_iCDF[2] = { 120, 0 };

static const fl_u8 fl_LTPscale_iCDF[3] = { 128, 64, 0 };

static const fl_u8 fl_type_offset_VAD_iCDF[4] = {
       232, 158, 10, 0
};

static const fl_u8 fl_type_offset_no_VAD_iCDF[2] = {
       230, 0
};

static const fl_u8 fl_NLSF_interpolation_factor_iCDF[5] = { 243, 221, 192, 181, 0 };

static const fl_i16 fl_Quantization_Offsets_Q10[2][2] = {
    { OFFSET_UVL_Q10, OFFSET_UVH_Q10 }, { OFFSET_VL_Q10, OFFSET_VH_Q10 }
};

static const fl_i16 fl_LTPScales_table_Q14[3] = { 15565, 12288, 8192 };

static const fl_u8 fl_uniform3_iCDF[3] = { 171, 85, 0 };

static const fl_u8 fl_uniform4_iCDF[4] = { 192, 128, 64, 0 };

static const fl_u8 fl_uniform5_iCDF[5] = { 205, 154, 102, 51, 0 };

static const fl_u8 fl_uniform6_iCDF[6] = { 213, 171, 128, 85, 43, 0 };

static const fl_u8 fl_uniform8_iCDF[8] = { 224, 192, 160, 128, 96, 64, 32, 0 };

static const fl_u8 fl_NLSF_EXT_iCDF[7] = { 100, 40, 16, 7, 3, 1, 0 };

static const fl_i32 fl_Transition_LP_B_Q28[TRANSITION_INT_NUM][TRANSITION_NB] = {
{ 250767114, 501534038, 250767114 },
{ 209867381, 419732057, 209867381 },
{ 170987846, 341967853, 170987846 },
{ 131531482, 263046905, 131531482 },
{ 89306658, 178584282, 89306658 }
};

static const fl_i32 fl_Transition_LP_A_Q28[TRANSITION_INT_NUM][TRANSITION_NA] = {
{ 506393414, 239854379 },
{ 411067935, 169683996 },
{ 306733530, 116694253 },
{ 185807084, 77959395 },
{ 35497197, 57401098 }
};

static const fl_u8 fl_pitch_lag_iCDF[2*(PITCH_EST_MAX_LAG_MS-PITCH_EST_MIN_LAG_MS)] = {
       253, 250, 244, 233, 212, 182, 150, 131,
       120, 110, 98, 85, 72, 60, 49, 40,
        32, 25, 19, 15, 13, 11, 9, 8,
         7, 6, 5, 4, 3, 2, 1, 0
};

static const fl_u8 fl_pitch_delta_iCDF[21] = {
       210, 208, 206, 203, 199, 193, 183, 168,
       142, 104, 74, 52, 37, 27, 20, 14,
        10, 6, 4, 2, 0
};

static const fl_u8 fl_pitch_contour_iCDF[34] = {
       223, 201, 183, 167, 152, 138, 124, 111,
        98, 88, 79, 70, 62, 56, 50, 44,
        39, 35, 31, 27, 24, 21, 18, 16,
        14, 12, 10, 8, 6, 4, 3, 2,
         1, 0
};

static const fl_u8 fl_pitch_contour_NB_iCDF[11] = {
       188, 176, 155, 138, 119, 97, 67, 43,
        26, 10, 0
};

static const fl_u8 fl_pitch_contour_10_ms_iCDF[12] = {
       165, 119, 80, 61, 47, 35, 27, 20,
        14, 9, 4, 0
};

static const fl_u8 fl_pitch_contour_10_ms_NB_iCDF[3] = {
       113, 63, 0
};

static const fl_u8 fl_max_pulses_table[4] = {
         8, 10, 12, 16
};

static const fl_u8 fl_pulses_per_block_iCDF[10][18] = {
{
       125, 51, 26, 18, 15, 12, 11, 10,
         9, 8, 7, 6, 5, 4, 3, 2,
         1, 0
},
{
       198, 105, 45, 22, 15, 12, 11, 10,
         9, 8, 7, 6, 5, 4, 3, 2,
         1, 0
},
{
       213, 162, 116, 83, 59, 43, 32, 24,
        18, 15, 12, 9, 7, 6, 5, 3,
         2, 0
},
{
       239, 187, 116, 59, 28, 16, 11, 10,
         9, 8, 7, 6, 5, 4, 3, 2,
         1, 0
},
{
       250, 229, 188, 135, 86, 51, 30, 19,
        13, 10, 8, 6, 5, 4, 3, 2,
         1, 0
},
{
       249, 235, 213, 185, 156, 128, 103, 83,
        66, 53, 42, 33, 26, 21, 17, 13,
        10, 0
},
{
       254, 249, 235, 206, 164, 118, 77, 46,
        27, 16, 10, 7, 5, 4, 3, 2,
         1, 0
},
{
       255, 253, 249, 239, 220, 191, 156, 119,
        85, 57, 37, 23, 15, 10, 6, 4,
         2, 0
},
{
       255, 253, 251, 246, 237, 223, 203, 179,
       152, 124, 98, 75, 55, 40, 29, 21,
        15, 0
},
{
       255, 254, 253, 247, 220, 162, 106, 67,
        42, 28, 18, 12, 9, 6, 4, 3,
         2, 0
}
};

static const fl_u8 fl_pulses_per_block_BITS_Q5[9][18] = {
{
        31, 57, 107, 160, 205, 205, 255, 255,
       255, 255, 255, 255, 255, 255, 255, 255,
       255, 255
},
{
        69, 47, 67, 111, 166, 205, 255, 255,
       255, 255, 255, 255, 255, 255, 255, 255,
       255, 255
},
{
        82, 74, 79, 95, 109, 128, 145, 160,
       173, 205, 205, 205, 224, 255, 255, 224,
       255, 224
},
{
       125, 74, 59, 69, 97, 141, 182, 255,
       255, 255, 255, 255, 255, 255, 255, 255,
       255, 255
},
{
       173, 115, 85, 73, 76, 92, 115, 145,
       173, 205, 224, 224, 255, 255, 255, 255,
       255, 255
},
{
       166, 134, 113, 102, 101, 102, 107, 118,
       125, 138, 145, 155, 166, 182, 192, 192,
       205, 150
},
{
       224, 182, 134, 101, 83, 79, 85, 97,
       120, 145, 173, 205, 224, 255, 255, 255,
       255, 255
},
{
       255, 224, 192, 150, 120, 101, 92, 89,
        93, 102, 118, 134, 160, 182, 192, 224,
       224, 224
},
{
       255, 224, 224, 182, 155, 134, 118, 109,
       104, 102, 106, 111, 118, 131, 145, 160,
       173, 131
}
};

static const fl_u8 fl_rate_levels_iCDF[2][9] = {
{
       241, 190, 178, 132, 87, 74, 41, 14,
         0
},
{
       223, 193, 157, 140, 106, 57, 39, 18,
         0
}
};

static const fl_u8 fl_rate_levels_BITS_Q5[2][9] = {
{
       131, 74, 141, 79, 80, 138, 95, 104,
       134
},
{
        95, 99, 91, 125, 93, 76, 123, 115,
       123
}
};

static const fl_u8 fl_shell_code_table0[152] = {
       128, 0, 214, 42, 0, 235, 128, 21,
         0, 244, 184, 72, 11, 0, 248, 214,
       128, 42, 7, 0, 248, 225, 170, 80,
        25, 5, 0, 251, 236, 198, 126, 54,
        18, 3, 0, 250, 238, 211, 159, 82,
        35, 15, 5, 0, 250, 231, 203, 168,
       128, 88, 53, 25, 6, 0, 252, 238,
       216, 185, 148, 108, 71, 40, 18, 4,
         0, 253, 243, 225, 199, 166, 128, 90,
        57, 31, 13, 3, 0, 254, 246, 233,
       212, 183, 147, 109, 73, 44, 23, 10,
         2, 0, 255, 250, 240, 223, 198, 166,
       128, 90, 58, 33, 16, 6, 1, 0,
       255, 251, 244, 231, 210, 181, 146, 110,
        75, 46, 25, 12, 5, 1, 0, 255,
       253, 248, 238, 221, 196, 164, 128, 92,
        60, 35, 18, 8, 3, 1, 0, 255,
       253, 249, 242, 229, 208, 180, 146, 110,
        76, 48, 27, 14, 7, 3, 1, 0
};

static const fl_u8 fl_shell_code_table1[152] = {
       129, 0, 207, 50, 0, 236, 129, 20,
         0, 245, 185, 72, 10, 0, 249, 213,
       129, 42, 6, 0, 250, 226, 169, 87,
        27, 4, 0, 251, 233, 194, 130, 62,
        20, 4, 0, 250, 236, 207, 160, 99,
        47, 17, 3, 0, 255, 240, 217, 182,
       131, 81, 41, 11, 1, 0, 255, 254,
       233, 201, 159, 107, 61, 20, 2, 1,
         0, 255, 249, 233, 206, 170, 128, 86,
        50, 23, 7, 1, 0, 255, 250, 238,
       217, 186, 148, 108, 70, 39, 18, 6,
         1, 0, 255, 252, 243, 226, 200, 166,
       128, 90, 56, 30, 13, 4, 1, 0,
       255, 252, 245, 231, 209, 180, 146, 110,
        76, 47, 25, 11, 4, 1, 0, 255,
       253, 248, 237, 219, 194, 163, 128, 93,
        62, 37, 19, 8, 3, 1, 0, 255,
       254, 250, 241, 226, 205, 177, 145, 111,
        79, 51, 30, 15, 6, 2, 1, 0
};

static const fl_u8 fl_shell_code_table2[152] = {
       129, 0, 203, 54, 0, 234, 129, 23,
         0, 245, 184, 73, 10, 0, 250, 215,
       129, 41, 5, 0, 252, 232, 173, 86,
        24, 3, 0, 253, 240, 200, 129, 56,
        15, 2, 0, 253, 244, 217, 164, 94,
        38, 10, 1, 0, 253, 245, 226, 189,
       132, 71, 27, 7, 1, 0, 253, 246,
       231, 203, 159, 105, 56, 23, 6, 1,
         0, 255, 248, 235, 213, 179, 133, 85,
        47, 19, 5, 1, 0, 255, 254, 243,
       221, 194, 159, 117, 70, 37, 12, 2,
         1, 0, 255, 254, 248, 234, 208, 171,
       128, 85, 48, 22, 8, 2, 1, 0,
       255, 254, 250, 240, 220, 189, 149, 107,
        67, 36, 16, 6, 2, 1, 0, 255,
       254, 251, 243, 227, 201, 166, 128, 90,
        55, 29, 13, 5, 2, 1, 0, 255,
       254, 252, 246, 234, 213, 183, 147, 109,
        73, 43, 22, 10, 4, 2, 1, 0
};

static const fl_u8 fl_shell_code_table3[152] = {
       130, 0, 200, 58, 0, 231, 130, 26,
         0, 244, 184, 76, 12, 0, 249, 214,
       130, 43, 6, 0, 252, 232, 173, 87,
        24, 3, 0, 253, 241, 203, 131, 56,
        14, 2, 0, 254, 246, 221, 167, 94,
        35, 8, 1, 0, 254, 249, 232, 193,
       130, 65, 23, 5, 1, 0, 255, 251,
       239, 211, 162, 99, 45, 15, 4, 1,
         0, 255, 251, 243, 223, 186, 131, 74,
        33, 11, 3, 1, 0, 255, 252, 245,
       230, 202, 158, 105, 57, 24, 8, 2,
         1, 0, 255, 253, 247, 235, 214, 179,
       132, 84, 44, 19, 7, 2, 1, 0,
       255, 254, 250, 240, 223, 196, 159, 112,
        69, 36, 15, 6, 2, 1, 0, 255,
       254, 253, 245, 231, 209, 176, 136, 93,
        55, 27, 11, 3, 2, 1, 0, 255,
       254, 253, 252, 239, 221, 194, 158, 117,
        76, 42, 18, 4, 3, 2, 1, 0
};

static const fl_u8 fl_shell_code_table_offsets[17] = {
         0, 0, 2, 5, 9, 14, 20, 27,
        35, 44, 54, 65, 77, 90, 104, 119,
       135
};

static const fl_u8 fl_sign_iCDF[42] = {
       254, 49, 67, 77, 82, 93, 99,
       198, 11, 18, 24, 31, 36, 45,
       255, 46, 66, 78, 87, 94, 104,
       208, 14, 21, 32, 42, 51, 66,
       255, 94, 104, 109, 112, 115, 118,
       248, 53, 69, 80, 88, 95, 102
};

static const fl_i8 fl_CB_lags_stage2_10_ms[PE_MAX_NB_SUBFR>>1][PE_NB_CBKS_STAGE2_10MS] = {
    {0, 1, 0},
    {0, 0, 1}
};

static const fl_i8 fl_CB_lags_stage3_10_ms[PE_MAX_NB_SUBFR>>1][PE_NB_CBKS_STAGE3_10MS] = {
    { 0, 0, 1,-1, 1,-1, 2,-2, 2,-2, 3,-3},
    { 0, 1, 0, 1,-1, 2,-1, 2,-2, 3,-2, 3}
};

static const fl_i8 fl_Lag_range_stage3_10_ms[PE_MAX_NB_SUBFR>>1][2] = {
    {-3, 7},
    {-2, 7}
};

static const fl_i8 fl_CB_lags_stage2[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE2_EXT] = {
    {0, 2,-1,-1,-1, 0, 0, 1, 1, 0, 1},
    {0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0,-1, 2, 1, 0, 1, 1, 0, 0,-1,-1}
};

static const fl_i8 fl_CB_lags_stage3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX] = {
    {0, 0, 1,-1, 0, 1,-1, 0,-1, 1,-2, 2,-2,-2, 2,-3, 2, 3,-3,-4, 3,-4, 4, 4,-5, 5,-6,-5, 6,-7, 6, 5, 8,-9},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0,-1, 1, 0, 0, 1,-1, 0, 1,-1,-1, 1,-1, 2, 1,-1, 2,-2,-2, 2,-2, 2, 2, 3,-3},
    {0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1,-1, 1, 0, 0, 2, 1,-1, 2,-1,-1, 2,-1, 2, 2,-1, 3,-2,-2,-2, 3},
    {0, 1, 0, 0, 1, 0, 1,-1, 2,-1, 2,-1, 2, 3,-2, 3,-2,-2, 4, 4,-3, 5,-3,-4, 6,-4, 6, 5,-5, 8,-6,-5,-7, 9}
};

static const fl_i8 fl_Lag_range_stage3[SILK_PE_MAX_COMPLEX+1][PE_MAX_NB_SUBFR][2] = {
    {
        {-5,8},
        {-1,6},
        {-1,6},
        {-4,10}
    },
    {
        {-6,10},
        {-2,6},
        {-1,6},
        {-5,10}
    },
    {
        {-9,12},
        {-3,7},
        {-2,7},
        {-7,13}
    }
};

static const fl_i8 fl_nb_cbk_searchs_stage3[SILK_PE_MAX_COMPLEX+1] = {
    PE_NB_CBKS_STAGE3_MIN,
    PE_NB_CBKS_STAGE3_MID,
    PE_NB_CBKS_STAGE3_MAX
};

static const fl_i16 fl_LSFCosTab_FIX_Q12[LSF_COS_TAB_SZ_FIX+1] = {
            8192, 8190, 8182, 8170,
            8152, 8130, 8104, 8072,
            8034, 7994, 7946, 7896,
            7840, 7778, 7714, 7644,
            7568, 7490, 7406, 7318,
            7226, 7128, 7026, 6922,
            6812, 6698, 6580, 6458,
            6332, 6204, 6070, 5934,
            5792, 5648, 5502, 5352,
            5198, 5040, 4880, 4718,
            4552, 4382, 4212, 4038,
            3862, 3684, 3502, 3320,
            3136, 2948, 2760, 2570,
            2378, 2186, 1990, 1794,
            1598, 1400, 1202, 1002,
             802, 602, 402, 202,
               0, -202, -402, -602,
            -802, -1002, -1202, -1400,
           -1598, -1794, -1990, -2186,
           -2378, -2570, -2760, -2948,
           -3136, -3320, -3502, -3684,
           -3862, -4038, -4212, -4382,
           -4552, -4718, -4880, -5040,
           -5198, -5352, -5502, -5648,
           -5792, -5934, -6070, -6204,
           -6332, -6458, -6580, -6698,
           -6812, -6922, -7026, -7128,
           -7226, -7318, -7406, -7490,
           -7568, -7644, -7714, -7778,
           -7840, -7896, -7946, -7994,
           -8034, -8072, -8104, -8130,
           -8152, -8170, -8182, -8190,
           -8192
};

static const fl_i16 fl_Resampler_3_4_COEFS[2+3*RESAMPLER_DOWN_ORDER_FIR0/2] = {
    -20694, -13867,
       -49, 64, 17, -157, 353, -496, 163, 11047, 22205,
       -39, 6, 91, -170, 186, 23, -896, 6336, 19928,
       -19, -36, 102, -89, -24, 328, -951, 2568, 15909,
};

static const fl_i16 fl_Resampler_2_3_COEFS[2+2*RESAMPLER_DOWN_ORDER_FIR0/2] = {
    -14457, -14019,
        64, 128, -122, 36, 310, -768, 584, 9267, 17733,
        12, 128, 18, -142, 288, -117, -865, 4123, 14459,
};

static const fl_i16 fl_Resampler_1_2_COEFS[2+RESAMPLER_DOWN_ORDER_FIR1/2] = {
       616, -14323,
       -10, 39, 58, -46, -84, 120, 184, -315, -541, 1284, 5380, 9024,
};

static const fl_i16 fl_Resampler_1_3_COEFS[2+RESAMPLER_DOWN_ORDER_FIR2/2] = {
     16102, -15162,
       -13, 0, 20, 26, 5, -31, -43, -4, 65, 90, 7, -157, -248, -44, 593, 1583, 2612, 3271,
};

static const fl_i16 fl_Resampler_1_4_COEFS[2+RESAMPLER_DOWN_ORDER_FIR2/2] = {
     22500, -15099,
         3, -14, -20, -15, 2, 25, 37, 25, -16, -71, -107, -79, 50, 292, 623, 982, 1288, 1464,
};

static const fl_i16 fl_Resampler_1_6_COEFS[2+RESAMPLER_DOWN_ORDER_FIR2/2] = {
     27540, -15257,
        17, 12, 8, 1, -10, -22, -30, -32, -22, 3, 44, 100, 168, 243, 317, 381, 429, 455,
};

static const fl_i16 fl_Resampler_2_3_COEFS_LQ[2+2*2] = {
     -2797, -6507,
      4697, 10739,
      1567, 8276,
};

static const fl_i16 fl_resampler_frac_FIR_12[12][RESAMPLER_ORDER_FIR_12/2] = {
    { 189, -600, 617, 30567 },
    { 117, -159, -1070, 29704 },
    { 52, 221, -2392, 28276 },
    { -4, 529, -3350, 26341 },
    { -48, 758, -3956, 23973 },
    { -80, 905, -4235, 21254 },
    { -99, 972, -4222, 18278 },
    { -107, 967, -3957, 15143 },
    { -103, 896, -3487, 11950 },
    { -91, 773, -2865, 8798 },
    { -71, 611, -2143, 5784 },
    { -46, 425, -1375, 2996 },
};

static const fl_i16 silk_resampler_up2_hq_0[3] = {1746, 14986, 39083-65536};
static const fl_i16 silk_resampler_up2_hq_1[3] = {6854, 25769, 55542-65536};

static fl_i32 silk_CLZ16(fl_i16 in16) { return 32-EC_ILOG(in16<<16|0x8000); }
static fl_i32 silk_CLZ32(fl_i32 in32) { return in32 ? 32-EC_ILOG(in32) : 32; }
static fl_i32 silk_CLZ64(fl_i64 in) {
   fl_i32 in_upper = (fl_i32)silk_RSHIFT64(in, 32);
   if (in_upper == 0) return 32+silk_CLZ32((fl_i32)in);
   return silk_CLZ32(in_upper);
}
static fl_i32 silk_ROR32(fl_i32 a32, int rot) {
   fl_u32 x = (fl_u32)a32, r = (fl_u32)rot, m = (fl_u32)-rot;
   if (rot == 0) return a32;
   else if (rot < 0) return (fl_i32)((x<<m)|(x>>(32-m)));
   else return (fl_i32)((x<<(32-r))|(x>>r));
}
static void silk_CLZ_FRAC(fl_i32 in, fl_i32 *lz, fl_i32 *frac_Q7) {
   fl_i32 lzeros = silk_CLZ32(in);
   *lz = lzeros;
   *frac_Q7 = silk_ROR32(in, 24-lzeros)&0x7f;
}
static fl_i32 silk_SQRT_APPROX(fl_i32 x) {
   fl_i32 y, lz, frac_Q7;
   if (x <= 0) return 0;
   silk_CLZ_FRAC(x, &lz, &frac_Q7);
   y = (lz&1) ? 32768 : 46214;
   y >>= silk_RSHIFT(lz, 1);
   y = silk_SMLAWB(y, y, silk_SMULBB(213, frac_Q7));
   return y;
}
static fl_i32 silk_DIV32_varQ(const fl_i32 a32, const fl_i32 b32, const int Qres) {
   int a_headrm, b_headrm, lshift;
   fl_i32 b32_inv, a32_nrm, b32_nrm, result;
   a_headrm = silk_CLZ32(silk_abs(a32))-1;
   a32_nrm = silk_LSHIFT(a32, a_headrm);
   b_headrm = silk_CLZ32(silk_abs(b32))-1;
   b32_nrm = silk_LSHIFT(b32, b_headrm);
   b32_inv = silk_DIV32_16(silk_int32_MAX>>2, silk_RSHIFT(b32_nrm, 16));
   result = silk_SMULWB(a32_nrm, b32_inv);
   a32_nrm = silk_SUB32_ovflw(a32_nrm, silk_LSHIFT_ovflw(silk_SMMUL(b32_nrm, result), 3));
   result = silk_SMLAWB(result, a32_nrm, b32_inv);
   lshift = 29+a_headrm-b_headrm-Qres;
   if (lshift < 0) return silk_LSHIFT_SAT32(result, -lshift);
   if (lshift < 32) return silk_RSHIFT(result, lshift);
   return 0;
}
static fl_i32 silk_INVERSE32_varQ(const fl_i32 b32, const int Qres) {
   int b_headrm, lshift;
   fl_i32 b32_inv, b32_nrm, err_Q32, result;
   b_headrm = silk_CLZ32(silk_abs(b32))-1;
   b32_nrm = silk_LSHIFT(b32, b_headrm);
   b32_inv = silk_DIV32_16(silk_int32_MAX>>2, silk_RSHIFT(b32_nrm, 16));
   result = silk_LSHIFT(b32_inv, 16);
   err_Q32 = silk_LSHIFT(((fl_i32)1<<29)-silk_SMULWB(b32_nrm, b32_inv), 3);
   result = silk_SMLAWW(result, err_Q32, b32_inv);
   lshift = 61-b_headrm-Qres;
   if (lshift <= 0) return silk_LSHIFT_SAT32(result, -lshift);
   if (lshift < 32) return silk_RSHIFT(result, lshift);
   return 0;
}

static fl_i32 silk_log2lin(const fl_i32 inLog_Q7) {
   fl_i32 out, frac_Q7;
   if (inLog_Q7 < 0) return 0;
   else if (inLog_Q7 >= 3967) return silk_int32_MAX;
   out = silk_LSHIFT(1, silk_RSHIFT(inLog_Q7, 7));
   frac_Q7 = inLog_Q7&0x7F;
   if (inLog_Q7 < 2048)
      out = silk_ADD_RSHIFT32(out, silk_MUL(out, silk_SMLAWB(frac_Q7, silk_SMULBB(frac_Q7, 128-frac_Q7), -174)), 7);
   else
      out = silk_MLA(out, silk_RSHIFT(out, 7), silk_SMLAWB(frac_Q7, silk_SMULBB(frac_Q7, 128-frac_Q7), -174));
   return out;
}
static fl_i32 silk_lin2log(const fl_i32 inLin) {
   fl_i32 lz, frac_Q7;
   silk_CLZ_FRAC(inLin, &lz, &frac_Q7);
   return silk_ADD_LSHIFT32(silk_SMLAWB(frac_Q7, silk_MUL(frac_Q7, 128-frac_Q7), 179), 31-lz, 7);
}

static void silk_insertion_sort_increasing(fl_i32 *a, int *idx, const int L, const int K) {
   fl_i32 value;
   int i, j;
   for (i = 0; i < K; i++) idx[i] = i;
   for (i = 1; i < K; i++) {
      value = a[i];
      for (j = i-1; (j >= 0) && (value < a[j]); j--) {
         a[j+1] = a[j];
         idx[j+1] = idx[j];
      }
      a[j+1] = value;
      idx[j+1] = i;
   }
   for (i = K; i < L; i++) {
      value = a[i];
      if (value < a[K-1]) {
         for (j = K-2; (j >= 0) && (value < a[j]); j--) {
            a[j+1] = a[j];
            idx[j+1] = idx[j];
         }
         a[j+1] = value;
         idx[j+1] = i;
      }
   }
}
static void silk_insertion_sort_increasing_all_values_int16(fl_i16 *a, const int L) {
   int value, i, j;
   for (i = 1; i < L; i++) {
      value = a[i];
      for (j = i-1; (j >= 0) && (value < a[j]); j--) a[j+1] = a[j];
      a[j+1] = value;
   }
}

static void silk_sum_sqr_shift(fl_i32 *energy, int *shift, const fl_i16 *x, int len) {
   int i, shft;
   fl_u32 nrg_tmp;
   fl_i32 nrg;
   shft = 31-silk_CLZ32(len);
   nrg = len;
   for (i = 0; i < len-1; i += 2) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg_tmp = silk_SMLABB_ovflw(nrg_tmp, x[i+1], x[i+1]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   if (i < len) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   shft = silk_max_32(0, shft+3-silk_CLZ32(nrg));
   nrg = 0;
   for (i = 0; i < len-1; i += 2) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg_tmp = silk_SMLABB_ovflw(nrg_tmp, x[i+1], x[i+1]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   if (i < len) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   *shift = shft;
   *energy = nrg;
}

static void silk_bwexpander(fl_i16 *ar, const int d, fl_i32 chirp_Q16) {
   int i;
   fl_i32 chirp_minus_one_Q16 = chirp_Q16-65536;
   for (i = 0; i < d-1; i++) {
      ar[i] = (fl_i16)silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, ar[i]), 16);
      chirp_Q16 += silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, chirp_minus_one_Q16), 16);
   }
   ar[d-1] = (fl_i16)silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, ar[d-1]), 16);
}
static void silk_bwexpander_32(fl_i32 *ar, const int d, fl_i32 chirp_Q16) {
   int i;
   fl_i32 chirp_minus_one_Q16 = chirp_Q16-65536;
   for (i = 0; i < d-1; i++) {
      ar[i] = silk_SMULWW(chirp_Q16, ar[i]);
      chirp_Q16 += silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, chirp_minus_one_Q16), 16);
   }
   ar[d-1] = silk_SMULWW(chirp_Q16, ar[d-1]);
}

static void silk_LPC_analysis_filter(fl_i16 *out, const fl_i16 *in, const fl_i16 *B, const fl_i32 len, const fl_i32 d) {
   int j, ix;
   fl_i32 out32_Q12, out32;
   const fl_i16 *in_ptr;
   for (ix = d; ix < len; ix++) {
      in_ptr = &in[ix-1];
      out32_Q12 = silk_SMULBB(in_ptr[0], B[0]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-1], B[1]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-2], B[2]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-3], B[3]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-4], B[4]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-5], B[5]);
      for (j = 6; j < d; j += 2) {
         out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-j], B[j]);
         out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-j-1], B[j+1]);
      }
      out32_Q12 = silk_SUB32_ovflw(silk_LSHIFT((fl_i32)in_ptr[1], 12), out32_Q12);
      out32 = silk_RSHIFT_ROUND(out32_Q12, 12);
      out[ix] = (fl_i16)silk_SAT16(out32);
   }
   memset(out, 0, d*sizeof(fl_i16));
}

#define INV_QA 24
#define A_LIMIT SILK_FIX_CONST(0.99975, INV_QA)
#define MUL32_FRAC_Q(a32,b32,Q) ((fl_i32)(silk_RSHIFT_ROUND64(silk_SMULL(a32, b32), Q)))
static fl_i32 silk_LPC_inverse_pred_gain_QA(fl_i32 A_QA[SILK_MAX_ORDER_LPC], const int order) {
   int k, n, mult2Q;
   fl_i32 invGain_Q30, rc_Q31, rc_mult1_Q30, rc_mult2, tmp1, tmp2;
   invGain_Q30 = SILK_FIX_CONST(1, 30);
   for (k = order-1; k > 0; k--) {
      if ((A_QA[k] > A_LIMIT) || (A_QA[k] < -A_LIMIT)) return 0;
      rc_Q31 = -silk_LSHIFT(A_QA[k], 31-INV_QA);
      rc_mult1_Q30 = silk_SUB32(SILK_FIX_CONST(1, 30), silk_SMMUL(rc_Q31, rc_Q31));
      invGain_Q30 = silk_LSHIFT(silk_SMMUL(invGain_Q30, rc_mult1_Q30), 2);
      if (invGain_Q30 < SILK_FIX_CONST(1.0f/MAX_PREDICTION_POWER_GAIN, 30)) return 0;
      mult2Q = 32-silk_CLZ32(silk_abs(rc_mult1_Q30));
      rc_mult2 = silk_INVERSE32_varQ(rc_mult1_Q30, mult2Q+30);
      for (n = 0; n < (k+1)>>1; n++) {
         fl_i64 tmp64;
         tmp1 = A_QA[n];
         tmp2 = A_QA[k-n-1];
         tmp64 = silk_RSHIFT_ROUND64(silk_SMULL(silk_SUB_SAT32(tmp1, MUL32_FRAC_Q(tmp2, rc_Q31, 31)), rc_mult2), mult2Q);
         if (tmp64 > silk_int32_MAX || tmp64 < silk_int32_MIN) return 0;
         A_QA[n] = (fl_i32)tmp64;
         tmp64 = silk_RSHIFT_ROUND64(silk_SMULL(silk_SUB_SAT32(tmp2, MUL32_FRAC_Q(tmp1, rc_Q31, 31)), rc_mult2), mult2Q);
         if (tmp64 > silk_int32_MAX || tmp64 < silk_int32_MIN) return 0;
         A_QA[k-n-1] = (fl_i32)tmp64;
      }
   }
   if ((A_QA[k] > A_LIMIT) || (A_QA[k] < -A_LIMIT)) return 0;
   rc_Q31 = -silk_LSHIFT(A_QA[0], 31-INV_QA);
   rc_mult1_Q30 = silk_SUB32(SILK_FIX_CONST(1, 30), silk_SMMUL(rc_Q31, rc_Q31));
   invGain_Q30 = silk_LSHIFT(silk_SMMUL(invGain_Q30, rc_mult1_Q30), 2);
   if (invGain_Q30 < SILK_FIX_CONST(1.0f/MAX_PREDICTION_POWER_GAIN, 30)) return 0;
   return invGain_Q30;
}
static fl_i32 silk_LPC_inverse_pred_gain(const fl_i16 *A_Q12, const int order) {
   int k;
   fl_i32 Atmp_QA[SILK_MAX_ORDER_LPC], DC_resp = 0;
   for (k = 0; k < order; k++) {
      DC_resp += (fl_i32)A_Q12[k];
      Atmp_QA[k] = silk_LSHIFT32((fl_i32)A_Q12[k], INV_QA-12);
   }
   if (DC_resp >= 4096) return 0;
   return silk_LPC_inverse_pred_gain_QA(Atmp_QA, order);
}

static void silk_LPC_fit(fl_i16 *a_QOUT, fl_i32 *a_QIN, const int QOUT, const int QIN, const int d) {
   int i, k, idx = 0;
   fl_i32 maxabs, absval, chirp_Q16;
   for (i = 0; i < 10; i++) {
      maxabs = 0;
      for (k = 0; k < d; k++) {
         absval = silk_abs(a_QIN[k]);
         if (absval > maxabs) {
            maxabs = absval;
            idx = k;
         }
      }
      maxabs = silk_RSHIFT_ROUND(maxabs, QIN-QOUT);
      if (maxabs > silk_int16_MAX) {
         maxabs = silk_min(maxabs, 163838);
         chirp_Q16 = SILK_FIX_CONST(0.999, 16)-silk_DIV32(silk_LSHIFT(maxabs-silk_int16_MAX, 14),
               silk_RSHIFT32(silk_MUL(maxabs, idx+1), 2));
         silk_bwexpander_32(a_QIN, d, chirp_Q16);
      } else break;
   }
   if (i == 10) {
      for (k = 0; k < d; k++) {
         a_QOUT[k] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(a_QIN[k], QIN-QOUT));
         a_QIN[k] = silk_LSHIFT((fl_i32)a_QOUT[k], QIN-QOUT);
      }
   } else {
      for (k = 0; k < d; k++) a_QOUT[k] = (fl_i16)silk_RSHIFT_ROUND(a_QIN[k], QIN-QOUT);
   }
}

#define NLSF2A_QA 16
static void silk_NLSF2A_find_poly(fl_i32 *out, const fl_i32 *cLSF, int dd) {
   int k, n;
   fl_i32 ftmp;
   out[0] = silk_LSHIFT(1, NLSF2A_QA);
   out[1] = -cLSF[0];
   for (k = 1; k < dd; k++) {
      ftmp = cLSF[2*k];
      out[k+1] = silk_LSHIFT(out[k-1], 1)-(fl_i32)silk_RSHIFT_ROUND64(silk_SMULL(ftmp, out[k]), NLSF2A_QA);
      for (n = k; n > 1; n--) out[n] += out[n-2]-(fl_i32)silk_RSHIFT_ROUND64(silk_SMULL(ftmp, out[n-1]), NLSF2A_QA);
      out[1] -= ftmp;
   }
}
static void silk_NLSF2A(fl_i16 *a_Q12, const fl_i16 *NLSF, const int d) {
   static const unsigned char ordering16[16] = {0, 15, 8, 7, 4, 11, 12, 3, 2, 13, 10, 5, 6, 9, 14, 1};
   static const unsigned char ordering10[10] = {0, 9, 6, 3, 4, 5, 8, 1, 2, 7};
   const unsigned char *ordering;
   int k, i, dd;
   fl_i32 cos_LSF_QA[SILK_MAX_ORDER_LPC] = {0}, P[SILK_MAX_ORDER_LPC/2+1], Q[SILK_MAX_ORDER_LPC/2+1];
   fl_i32 Ptmp, Qtmp, f_int, f_frac, cos_val, delta, a32_QA1[SILK_MAX_ORDER_LPC];
   ordering = d == 16 ? ordering16 : ordering10;
   for (k = 0; k < d; k++) {
      f_int = silk_RSHIFT(NLSF[k], 15-7);
      f_frac = NLSF[k]-silk_LSHIFT(f_int, 15-7);
      cos_val = fl_LSFCosTab_FIX_Q12[f_int];
      delta = fl_LSFCosTab_FIX_Q12[f_int+1]-cos_val;
      cos_LSF_QA[ordering[k]] = silk_RSHIFT_ROUND(silk_LSHIFT(cos_val, 8)+silk_MUL(delta, f_frac), 20-NLSF2A_QA);
   }
   dd = silk_RSHIFT(d, 1);
   silk_NLSF2A_find_poly(P, &cos_LSF_QA[0], dd);
   silk_NLSF2A_find_poly(Q, &cos_LSF_QA[1], dd);
   for (k = 0; k < dd; k++) {
      Ptmp = P[k+1]+P[k];
      Qtmp = Q[k+1]-Q[k];
      a32_QA1[k] = -Qtmp-Ptmp;
      a32_QA1[d-k-1] = Qtmp-Ptmp;
   }
   silk_LPC_fit(a_Q12, a32_QA1, 12, NLSF2A_QA+1, d);
   for (i = 0; silk_LPC_inverse_pred_gain(a_Q12, d) == 0 && i < MAX_LPC_STABILIZE_ITERATIONS; i++) {
      silk_bwexpander_32(a32_QA1, d, 65536-silk_LSHIFT(2, i));
      for (k = 0; k < d; k++) a_Q12[k] = (fl_i16)silk_RSHIFT_ROUND(a32_QA1[k], NLSF2A_QA+1-12);
   }
}

static void silk_NLSF_stabilize(fl_i16 *NLSF_Q15, const fl_i16 *NDeltaMin_Q15, const int L) {
   int i, I = 0, k, loops;
   fl_i16 center_freq_Q15;
   fl_i32 diff_Q15, min_diff_Q15, min_center_Q15, max_center_Q15;
   for (loops = 0; loops < 20; loops++) {
      min_diff_Q15 = NLSF_Q15[0]-NDeltaMin_Q15[0];
      I = 0;
      for (i = 1; i <= L-1; i++) {
         diff_Q15 = NLSF_Q15[i]-(NLSF_Q15[i-1]+NDeltaMin_Q15[i]);
         if (diff_Q15 < min_diff_Q15) {
            min_diff_Q15 = diff_Q15;
            I = i;
         }
      }
      diff_Q15 = (1<<15)-(NLSF_Q15[L-1]+NDeltaMin_Q15[L]);
      if (diff_Q15 < min_diff_Q15) {
         min_diff_Q15 = diff_Q15;
         I = L;
      }
      if (min_diff_Q15 >= 0) return;
      if (I == 0) NLSF_Q15[0] = NDeltaMin_Q15[0];
      else if (I == L) NLSF_Q15[L-1] = (1<<15)-NDeltaMin_Q15[L];
      else {
         min_center_Q15 = 0;
         for (k = 0; k < I; k++) min_center_Q15 += NDeltaMin_Q15[k];
         min_center_Q15 += silk_RSHIFT(NDeltaMin_Q15[I], 1);
         max_center_Q15 = 1<<15;
         for (k = L; k > I; k--) max_center_Q15 -= NDeltaMin_Q15[k];
         max_center_Q15 -= silk_RSHIFT(NDeltaMin_Q15[I], 1);
         center_freq_Q15 = (fl_i16)silk_LIMIT_32(silk_RSHIFT_ROUND((fl_i32)NLSF_Q15[I-1]+(fl_i32)NLSF_Q15[I], 1),
               min_center_Q15, max_center_Q15);
         NLSF_Q15[I-1] = center_freq_Q15-silk_RSHIFT(NDeltaMin_Q15[I], 1);
         NLSF_Q15[I] = NLSF_Q15[I-1]+NDeltaMin_Q15[I];
      }
   }
   if (loops == 20) {
      silk_insertion_sort_increasing_all_values_int16(&NLSF_Q15[0], L);
      NLSF_Q15[0] = silk_max_int(NLSF_Q15[0], NDeltaMin_Q15[0]);
      for (i = 1; i < L; i++) NLSF_Q15[i] = silk_max_int(NLSF_Q15[i], silk_ADD_SAT16(NLSF_Q15[i-1], NDeltaMin_Q15[i]));
      NLSF_Q15[L-1] = silk_min_int(NLSF_Q15[L-1], (1<<15)-NDeltaMin_Q15[L]);
      for (i = L-2; i >= 0; i--) NLSF_Q15[i] = silk_min_int(NLSF_Q15[i], NLSF_Q15[i+1]-NDeltaMin_Q15[i+1]);
   }
}

static void silk_NLSF_unpack(fl_i16 ec_ix[], fl_u8 pred_Q8[], const fl_nlsf_cb *cb, const int CB1_index) {
   int i;
   fl_u8 entry;
   const fl_u8 *ec_sel_ptr = &cb->ec_sel[CB1_index*cb->order/2];
   for (i = 0; i < cb->order; i += 2) {
      entry = *ec_sel_ptr++;
      ec_ix[i] = silk_SMULBB(silk_RSHIFT(entry, 1)&7, 2*NLSF_QUANT_MAX_AMPLITUDE+1);
      pred_Q8[i] = cb->pred_Q8[i+(entry&1)*(cb->order-1)];
      ec_ix[i+1] = silk_SMULBB(silk_RSHIFT(entry, 5)&7, 2*NLSF_QUANT_MAX_AMPLITUDE+1);
      pred_Q8[i+1] = cb->pred_Q8[i+(silk_RSHIFT(entry, 4)&1)*(cb->order-1)+1];
   }
}
static void silk_NLSF_residual_dequant(fl_i16 x_Q10[], const fl_i8 indices[], const fl_u8 pred_coef_Q8[],
      const int quant_step_size_Q16, const fl_i16 order) {
   int i, out_Q10, pred_Q10;
   out_Q10 = 0;
   for (i = order-1; i >= 0; i--) {
      pred_Q10 = silk_RSHIFT(silk_SMULBB(out_Q10, (fl_i16)pred_coef_Q8[i]), 8);
      out_Q10 = silk_LSHIFT(indices[i], 10);
      if (out_Q10 > 0) out_Q10 = out_Q10-SILK_FIX_CONST(NLSF_QUANT_LEVEL_ADJ, 10);
      else if (out_Q10 < 0) out_Q10 = out_Q10+SILK_FIX_CONST(NLSF_QUANT_LEVEL_ADJ, 10);
      out_Q10 = silk_SMLAWB(pred_Q10, (fl_i32)out_Q10, quant_step_size_Q16);
      x_Q10[i] = out_Q10;
   }
}
static void silk_NLSF_decode(fl_i16 *pNLSF_Q15, fl_i8 *NLSFIndices, const fl_nlsf_cb *cb) {
   int i;
   fl_u8 pred_Q8[MAX_LPC_ORDER];
   fl_i16 ec_ix[MAX_LPC_ORDER], res_Q10[MAX_LPC_ORDER];
   fl_i32 NLSF_Q15_tmp;
   const fl_u8 *pCB_element;
   const fl_i16 *pCB_Wght_Q9;
   silk_NLSF_unpack(ec_ix, pred_Q8, cb, NLSFIndices[0]);
   silk_NLSF_residual_dequant(res_Q10, &NLSFIndices[1], pred_Q8, cb->quantStepSize_Q16, cb->order);
   pCB_element = &cb->CB1_NLSF_Q8[NLSFIndices[0]*cb->order];
   pCB_Wght_Q9 = &cb->CB1_Wght_Q9[NLSFIndices[0]*cb->order];
   for (i = 0; i < cb->order; i++) {
      NLSF_Q15_tmp = silk_ADD_LSHIFT32(silk_DIV32_16(silk_LSHIFT((fl_i32)res_Q10[i], 14), pCB_Wght_Q9[i]),
            (fl_i16)pCB_element[i], 7);
      pNLSF_Q15[i] = (fl_i16)silk_LIMIT(NLSF_Q15_tmp, 0, 32767);
   }
   silk_NLSF_stabilize(pNLSF_Q15, cb->deltaMin_Q15, cb->order);
}

#define GAIN_OFFSET ((MIN_QGAIN_DB*128)/6+16*128)
#define GAIN_SCALE_Q16 ((65536*(N_LEVELS_QGAIN-1))/(((MAX_QGAIN_DB-MIN_QGAIN_DB)*128)/6))
#define GAIN_INV_SCALE_Q16 ((65536*(((MAX_QGAIN_DB-MIN_QGAIN_DB)*128)/6))/(N_LEVELS_QGAIN-1))
static void silk_gains_quant(fl_i8 ind[MAX_NB_SUBFR], fl_i32 gain_Q16[MAX_NB_SUBFR], fl_i8 *prev_ind,
      const int conditional, const int nb_subfr) {
   int k, double_step_size_threshold;
   for (k = 0; k < nb_subfr; k++) {
      ind[k] = silk_SMULWB(GAIN_SCALE_Q16, silk_lin2log(gain_Q16[k])-GAIN_OFFSET);
      if (ind[k] < *prev_ind) ind[k]++;
      ind[k] = silk_LIMIT_int(ind[k], 0, N_LEVELS_QGAIN-1);
      if (k == 0 && conditional == 0) {
         ind[k] = silk_LIMIT_int(ind[k], *prev_ind+MIN_DELTA_GAIN_QUANT, N_LEVELS_QGAIN-1);
         *prev_ind = ind[k];
      } else {
         ind[k] = ind[k]-*prev_ind;
         double_step_size_threshold = 2*MAX_DELTA_GAIN_QUANT-N_LEVELS_QGAIN+*prev_ind;
         if (ind[k] > double_step_size_threshold)
            ind[k] = double_step_size_threshold+silk_RSHIFT(ind[k]-double_step_size_threshold+1, 1);
         ind[k] = silk_LIMIT_int(ind[k], MIN_DELTA_GAIN_QUANT, MAX_DELTA_GAIN_QUANT);
         if (ind[k] > double_step_size_threshold) {
            *prev_ind += silk_LSHIFT(ind[k], 1)-double_step_size_threshold;
            *prev_ind = silk_min_int(*prev_ind, N_LEVELS_QGAIN-1);
         } else *prev_ind += ind[k];
         ind[k] -= MIN_DELTA_GAIN_QUANT;
      }
      gain_Q16[k] = silk_log2lin(silk_min_32(silk_SMULWB(GAIN_INV_SCALE_Q16, *prev_ind)+GAIN_OFFSET, 3967));
   }
}
static void silk_gains_dequant(fl_i32 gain_Q16[MAX_NB_SUBFR], const fl_i8 ind[MAX_NB_SUBFR], fl_i8 *prev_ind,
      const int conditional, const int nb_subfr) {
   int k, ind_tmp, double_step_size_threshold;
   for (k = 0; k < nb_subfr; k++) {
      if (k == 0 && conditional == 0) {
         *prev_ind = silk_max_int(ind[k], *prev_ind-16);
      } else {
         ind_tmp = ind[k]+MIN_DELTA_GAIN_QUANT;
         double_step_size_threshold = 2*MAX_DELTA_GAIN_QUANT-N_LEVELS_QGAIN+*prev_ind;
         if (ind_tmp > double_step_size_threshold) *prev_ind += silk_LSHIFT(ind_tmp, 1)-double_step_size_threshold;
         else *prev_ind += ind_tmp;
      }
      *prev_ind = silk_LIMIT_int(*prev_ind, 0, N_LEVELS_QGAIN-1);
      gain_Q16[k] = silk_log2lin(silk_min_32(silk_SMULWB(GAIN_INV_SCALE_Q16, *prev_ind)+GAIN_OFFSET, 3967));
   }
}
static fl_i32 silk_gains_ID(const fl_i8 ind[MAX_NB_SUBFR], const int nb_subfr) {
   int k;
   fl_i32 gainsID = 0;
   for (k = 0; k < nb_subfr; k++) gainsID = silk_ADD_LSHIFT32(ind[k], gainsID, 8);
   return gainsID;
}

static void silk_decode_pitch(fl_i16 lagIndex, fl_i8 contourIndex, int pitch_lags[], const int Fs_kHz, const int nb_subfr) {
   int lag, k, min_lag, max_lag, cbk_size;
   const fl_i8 *Lag_CB_ptr;
   if (Fs_kHz == 8) {
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         Lag_CB_ptr = &fl_CB_lags_stage2[0][0];
         cbk_size = PE_NB_CBKS_STAGE2_EXT;
      } else {
         Lag_CB_ptr = &fl_CB_lags_stage2_10_ms[0][0];
         cbk_size = PE_NB_CBKS_STAGE2_10MS;
      }
   } else {
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
         cbk_size = PE_NB_CBKS_STAGE3_MAX;
      } else {
         Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
         cbk_size = PE_NB_CBKS_STAGE3_10MS;
      }
   }
   min_lag = silk_SMULBB(PE_MIN_LAG_MS, Fs_kHz);
   max_lag = silk_SMULBB(PE_MAX_LAG_MS, Fs_kHz);
   lag = min_lag+lagIndex;
   for (k = 0; k < nb_subfr; k++) {
      pitch_lags[k] = lag+Lag_CB_ptr[k*cbk_size+contourIndex];
      pitch_lags[k] = silk_LIMIT(pitch_lags[k], min_lag, max_lag);
   }
}

/* Shell coder */
static void silk_combine_pulses(int *out, const int *in, const int len) {
   int k;
   for (k = 0; k < len; k++) out[k] = in[2*k]+in[2*k+1];
}
static void silk_encode_split(ec_ctx *enc, const int p_child1, const int p, const fl_u8 *shell_table) {
   if (p > 0) ec_enc_icdf(enc, p_child1, &shell_table[fl_shell_code_table_offsets[p]], 8);
}
static void silk_decode_split(fl_i16 *p_child1, fl_i16 *p_child2, ec_ctx *dec, const int p, const fl_u8 *shell_table) {
   if (p > 0) {
      p_child1[0] = ec_dec_icdf(dec, &shell_table[fl_shell_code_table_offsets[p]], 8);
      p_child2[0] = p-p_child1[0];
   } else {
      p_child1[0] = 0;
      p_child2[0] = 0;
   }
}
static void silk_shell_encoder(ec_ctx *enc, const int *pulses0) {
   int pulses1[8], pulses2[4], pulses3[2], pulses4[1];
   silk_combine_pulses(pulses1, pulses0, 8);
   silk_combine_pulses(pulses2, pulses1, 4);
   silk_combine_pulses(pulses3, pulses2, 2);
   silk_combine_pulses(pulses4, pulses3, 1);
   silk_encode_split(enc, pulses3[0], pulses4[0], fl_shell_code_table3);
   silk_encode_split(enc, pulses2[0], pulses3[0], fl_shell_code_table2);
   silk_encode_split(enc, pulses1[0], pulses2[0], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[0], pulses1[0], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[2], pulses1[1], fl_shell_code_table0);
   silk_encode_split(enc, pulses1[2], pulses2[1], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[4], pulses1[2], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[6], pulses1[3], fl_shell_code_table0);
   silk_encode_split(enc, pulses2[2], pulses3[1], fl_shell_code_table2);
   silk_encode_split(enc, pulses1[4], pulses2[2], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[8], pulses1[4], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[10], pulses1[5], fl_shell_code_table0);
   silk_encode_split(enc, pulses1[6], pulses2[3], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[12], pulses1[6], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[14], pulses1[7], fl_shell_code_table0);
}
static void silk_shell_decoder(fl_i16 *pulses0, ec_ctx *dec, const int pulses4) {
   fl_i16 pulses3[2], pulses2[4], pulses1[8];
   silk_decode_split(&pulses3[0], &pulses3[1], dec, pulses4, fl_shell_code_table3);
   silk_decode_split(&pulses2[0], &pulses2[1], dec, pulses3[0], fl_shell_code_table2);
   silk_decode_split(&pulses1[0], &pulses1[1], dec, pulses2[0], fl_shell_code_table1);
   silk_decode_split(&pulses0[0], &pulses0[1], dec, pulses1[0], fl_shell_code_table0);
   silk_decode_split(&pulses0[2], &pulses0[3], dec, pulses1[1], fl_shell_code_table0);
   silk_decode_split(&pulses1[2], &pulses1[3], dec, pulses2[1], fl_shell_code_table1);
   silk_decode_split(&pulses0[4], &pulses0[5], dec, pulses1[2], fl_shell_code_table0);
   silk_decode_split(&pulses0[6], &pulses0[7], dec, pulses1[3], fl_shell_code_table0);
   silk_decode_split(&pulses2[2], &pulses2[3], dec, pulses3[1], fl_shell_code_table2);
   silk_decode_split(&pulses1[4], &pulses1[5], dec, pulses2[2], fl_shell_code_table1);
   silk_decode_split(&pulses0[8], &pulses0[9], dec, pulses1[4], fl_shell_code_table0);
   silk_decode_split(&pulses0[10], &pulses0[11], dec, pulses1[5], fl_shell_code_table0);
   silk_decode_split(&pulses1[6], &pulses1[7], dec, pulses2[3], fl_shell_code_table1);
   silk_decode_split(&pulses0[12], &pulses0[13], dec, pulses1[6], fl_shell_code_table0);
   silk_decode_split(&pulses0[14], &pulses0[15], dec, pulses1[7], fl_shell_code_table0);
}

static void silk_encode_signs(ec_ctx *enc, const fl_i8 pulses[], int length, const int signalType,
      const int quantOffsetType, const int sum_pulses[MAX_NB_SHELL_BLOCKS]) {
   int i, j, p;
   fl_u8 icdf[2];
   const fl_i8 *q_ptr = pulses;
   const fl_u8 *icdf_ptr;
   icdf[1] = 0;
   i = silk_SMULBB(7, silk_ADD_LSHIFT32(quantOffsetType, signalType, 1));
   icdf_ptr = &fl_sign_iCDF[i];
   length = silk_RSHIFT(length+SHELL_CODEC_FRAME_LENGTH/2, LOG2_SHELL_CODEC_FRAME_LENGTH);
   for (i = 0; i < length; i++) {
      p = sum_pulses[i];
      if (p > 0) {
         icdf[0] = icdf_ptr[silk_min(p&0x1F, 6)];
         for (j = 0; j < SHELL_CODEC_FRAME_LENGTH; j++)
            if (q_ptr[j] != 0) ec_enc_icdf(enc, silk_RSHIFT(q_ptr[j], 15)+1, icdf, 8);
      }
      q_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
}
static void silk_decode_signs(ec_ctx *dec, fl_i16 pulses[], int length, const int signalType,
      const int quantOffsetType, const int sum_pulses[MAX_NB_SHELL_BLOCKS]) {
   int i, j, p;
   fl_u8 icdf[2];
   fl_i16 *q_ptr = pulses;
   const fl_u8 *icdf_ptr;
   icdf[1] = 0;
   i = silk_SMULBB(7, silk_ADD_LSHIFT32(quantOffsetType, signalType, 1));
   icdf_ptr = &fl_sign_iCDF[i];
   length = silk_RSHIFT(length+SHELL_CODEC_FRAME_LENGTH/2, LOG2_SHELL_CODEC_FRAME_LENGTH);
   for (i = 0; i < length; i++) {
      p = sum_pulses[i];
      if (p > 0) {
         icdf[0] = icdf_ptr[silk_min(p&0x1F, 6)];
         for (j = 0; j < SHELL_CODEC_FRAME_LENGTH; j++)
            if (q_ptr[j] > 0) q_ptr[j] *= silk_LSHIFT(ec_dec_icdf(dec, icdf, 8), 1)-1;
      }
      q_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
}

/* ---- SILK resampler ---- */
#define SILK_RESAMPLER_MAX_FIR_ORDER 36
#define SILK_RESAMPLER_MAX_IIR_ORDER 6
#define RESAMPLER_MAX_BATCH_SIZE_MS 10
typedef struct {
   fl_i32 sIIR[SILK_RESAMPLER_MAX_IIR_ORDER];
   union { fl_i32 i32[SILK_RESAMPLER_MAX_FIR_ORDER]; fl_i16 i16[SILK_RESAMPLER_MAX_FIR_ORDER]; } sFIR;
   fl_i16 delayBuf[96];
   int resampler_function, batchSize;
   fl_i32 invRatio_Q16;
   int FIR_Order, FIR_Fracs, Fs_in_kHz, Fs_out_kHz, inputDelay;
   const fl_i16 *Coefs;
} silk_resampler_state;

static const fl_i8 silk_delay_matrix_enc[6][3] = {{6, 0, 3}, {0, 7, 3}, {0, 1, 10}, {0, 2, 6}, {18, 10, 12}, {0, 0, 44}};
static const fl_i8 silk_delay_matrix_dec[3][6] = {{4, 0, 2, 0, 0, 0}, {0, 9, 4, 7, 4, 4}, {0, 3, 12, 7, 7, 7}};
#define rateID(R) IMIN(5, ((((R)>>12)-((R) > 16000))>>((R) > 24000))-1)

static int silk_resampler_init(silk_resampler_state *S, fl_i32 Fs_Hz_in, fl_i32 Fs_Hz_out, int forEnc) {
   int up2x;
   memset(S, 0, sizeof(*S));
   if (forEnc) S->inputDelay = silk_delay_matrix_enc[rateID(Fs_Hz_in)][rateID(Fs_Hz_out)];
   else S->inputDelay = silk_delay_matrix_dec[rateID(Fs_Hz_in)][rateID(Fs_Hz_out)];
   S->Fs_in_kHz = silk_DIV32_16(Fs_Hz_in, 1000);
   S->Fs_out_kHz = silk_DIV32_16(Fs_Hz_out, 1000);
   S->batchSize = S->Fs_in_kHz*RESAMPLER_MAX_BATCH_SIZE_MS;
   up2x = 0;
   if (Fs_Hz_out > Fs_Hz_in) {
      if (Fs_Hz_out == silk_MUL(Fs_Hz_in, 2)) S->resampler_function = 1;
      else {
         S->resampler_function = 2;
         up2x = 1;
      }
   } else if (Fs_Hz_out < Fs_Hz_in) {
      S->resampler_function = 3;
      if (silk_MUL(Fs_Hz_out, 4) == silk_MUL(Fs_Hz_in, 3)) {
         S->FIR_Fracs = 3; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR0; S->Coefs = fl_Resampler_3_4_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 3) == silk_MUL(Fs_Hz_in, 2)) {
         S->FIR_Fracs = 2; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR0; S->Coefs = fl_Resampler_2_3_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 2) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR1; S->Coefs = fl_Resampler_1_2_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 3) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_3_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 4) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_4_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 6) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_6_COEFS;
      } else return -1;
   } else S->resampler_function = 0;
   S->invRatio_Q16 = silk_LSHIFT32(silk_DIV32(silk_LSHIFT32(Fs_Hz_in, 14+up2x), Fs_Hz_out), 2);
   while (silk_SMULWW(S->invRatio_Q16, Fs_Hz_out) < silk_LSHIFT32(Fs_Hz_in, up2x)) S->invRatio_Q16++;
   return 0;
}

static void silk_resampler_private_up2_HQ(fl_i32 *S, fl_i16 *out, const fl_i16 *in, fl_i32 len) {
   fl_i32 k, in32, out32_1, out32_2, Y, X;
   for (k = 0; k < len; k++) {
      in32 = silk_LSHIFT((fl_i32)in[k], 10);
      Y = in32-S[0];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_0[0]);
      out32_1 = S[0]+X;
      S[0] = in32+X;
      Y = out32_1-S[1];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_0[1]);
      out32_2 = S[1]+X;
      S[1] = out32_1+X;
      Y = out32_2-S[2];
      X = silk_SMLAWB(Y, Y, silk_resampler_up2_hq_0[2]);
      out32_1 = S[2]+X;
      S[2] = out32_2+X;
      out[2*k] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(out32_1, 10));
      Y = in32-S[3];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_1[0]);
      out32_1 = S[3]+X;
      S[3] = in32+X;
      Y = out32_1-S[4];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_1[1]);
      out32_2 = S[4]+X;
      S[4] = out32_1+X;
      Y = out32_2-S[5];
      X = silk_SMLAWB(Y, Y, silk_resampler_up2_hq_1[2]);
      out32_1 = S[5]+X;
      S[5] = out32_2+X;
      out[2*k+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(out32_1, 10));
   }
}

static fl_i16 *silk_resampler_IIR_FIR_INTERPOL(fl_i16 *out, fl_i16 *buf, fl_i32 max_index_Q16, fl_i32 index_increment_Q16) {
   fl_i32 index_Q16, res_Q15, table_index;
   fl_i16 *buf_ptr;
   for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
      table_index = silk_SMULWB(index_Q16&0xFFFF, 12);
      buf_ptr = &buf[index_Q16>>16];
      res_Q15 = silk_SMULBB(buf_ptr[0], fl_resampler_frac_FIR_12[table_index][0]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[1], fl_resampler_frac_FIR_12[table_index][1]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[2], fl_resampler_frac_FIR_12[table_index][2]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[3], fl_resampler_frac_FIR_12[table_index][3]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[4], fl_resampler_frac_FIR_12[11-table_index][3]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[5], fl_resampler_frac_FIR_12[11-table_index][2]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[6], fl_resampler_frac_FIR_12[11-table_index][1]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[7], fl_resampler_frac_FIR_12[11-table_index][0]);
      *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q15, 15));
   }
   return out;
}
static void silk_resampler_private_IIR_FIR(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   fl_i32 nSamplesIn, max_index_Q16, index_increment_Q16;
   fl_i16 buf[2*RESAMPLER_MAX_BATCH_SIZE_MS*48+RESAMPLER_ORDER_FIR_12];
   memcpy(buf, S->sFIR.i16, RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
   index_increment_Q16 = S->invRatio_Q16;
   while (1) {
      nSamplesIn = silk_min(inLen, S->batchSize);
      silk_resampler_private_up2_HQ(S->sIIR, &buf[RESAMPLER_ORDER_FIR_12], in, nSamplesIn);
      max_index_Q16 = silk_LSHIFT32(nSamplesIn, 16+1);
      out = silk_resampler_IIR_FIR_INTERPOL(out, buf, max_index_Q16, index_increment_Q16);
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if (inLen > 0) memcpy(buf, &buf[nSamplesIn<<1], RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
      else break;
   }
   memcpy(S->sFIR.i16, &buf[nSamplesIn<<1], RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
}

static void silk_resampler_private_AR2(fl_i32 S[], fl_i32 out_Q8[], const fl_i16 in[], const fl_i16 A_Q14[], fl_i32 len) {
   fl_i32 k, out32;
   for (k = 0; k < len; k++) {
      out32 = silk_ADD_LSHIFT32(S[0], (fl_i32)in[k], 8);
      out_Q8[k] = out32;
      out32 = silk_LSHIFT(out32, 2);
      S[0] = silk_SMLAWB(S[1], out32, A_Q14[0]);
      S[1] = silk_SMULWB(out32, A_Q14[1]);
   }
}
static fl_i16 *silk_resampler_down_FIR_INTERPOL(fl_i16 *out, fl_i32 *buf, const fl_i16 *FIR_Coefs, int FIR_Order,
      int FIR_Fracs, fl_i32 max_index_Q16, fl_i32 index_increment_Q16) {
   fl_i32 index_Q16, res_Q6, *buf_ptr, interpol_ind;
   const fl_i16 *ip;
   int j;
   switch (FIR_Order) {
   case RESAMPLER_DOWN_ORDER_FIR0:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         interpol_ind = silk_SMULWB(index_Q16&0xFFFF, FIR_Fracs);
         ip = &FIR_Coefs[RESAMPLER_DOWN_ORDER_FIR0/2*interpol_ind];
         res_Q6 = silk_SMULWB(buf_ptr[0], ip[0]);
         for (j = 1; j < 9; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j], ip[j]);
         ip = &FIR_Coefs[RESAMPLER_DOWN_ORDER_FIR0/2*(FIR_Fracs-1-interpol_ind)];
         for (j = 0; j < 9; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[17-j], ip[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   case RESAMPLER_DOWN_ORDER_FIR1:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         res_Q6 = silk_SMULWB(buf_ptr[0]+buf_ptr[23], FIR_Coefs[0]);
         for (j = 1; j < 12; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j]+buf_ptr[23-j], FIR_Coefs[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   case RESAMPLER_DOWN_ORDER_FIR2:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         res_Q6 = silk_SMULWB(buf_ptr[0]+buf_ptr[35], FIR_Coefs[0]);
         for (j = 1; j < 18; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j]+buf_ptr[35-j], FIR_Coefs[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   }
   return out;
}
static void silk_resampler_private_down_FIR(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   fl_i32 nSamplesIn, max_index_Q16, index_increment_Q16;
   fl_i32 buf[RESAMPLER_MAX_BATCH_SIZE_MS*48+SILK_RESAMPLER_MAX_FIR_ORDER];
   const fl_i16 *FIR_Coefs;
   memcpy(buf, S->sFIR.i32, S->FIR_Order*sizeof(fl_i32));
   FIR_Coefs = &S->Coefs[2];
   index_increment_Q16 = S->invRatio_Q16;
   while (1) {
      nSamplesIn = silk_min(inLen, S->batchSize);
      silk_resampler_private_AR2(S->sIIR, &buf[S->FIR_Order], in, S->Coefs, nSamplesIn);
      max_index_Q16 = silk_LSHIFT32(nSamplesIn, 16);
      out = silk_resampler_down_FIR_INTERPOL(out, buf, FIR_Coefs, S->FIR_Order, S->FIR_Fracs, max_index_Q16,
            index_increment_Q16);
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if (inLen > 1) memcpy(buf, &buf[nSamplesIn], S->FIR_Order*sizeof(fl_i32));
      else break;
   }
   memcpy(S->sFIR.i32, &buf[nSamplesIn], S->FIR_Order*sizeof(fl_i32));
}

static int silk_resampler(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   int nSamples = S->Fs_in_kHz-S->inputDelay;
   memcpy(&S->delayBuf[S->inputDelay], in, nSamples*sizeof(fl_i16));
   switch (S->resampler_function) {
   case 1:
      silk_resampler_private_up2_HQ(S->sIIR, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_up2_HQ(S->sIIR, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   case 2:
      silk_resampler_private_IIR_FIR(S, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_IIR_FIR(S, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   case 3:
      silk_resampler_private_down_FIR(S, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_down_FIR(S, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   default:
      memcpy(out, S->delayBuf, S->Fs_in_kHz*sizeof(fl_i16));
      memcpy(&out[S->Fs_out_kHz], &in[nSamples], (inLen-S->Fs_in_kHz)*sizeof(fl_i16));
   }
   memcpy(S->delayBuf, &in[inLen-S->inputDelay], S->inputDelay*sizeof(fl_i16));
   return 0;
}

/* ======== silk_dec.c ======== */
/* ---- SILK decoder ---- */
#define PLC_BWE_COEF 0.99
#define V_PITCH_GAIN_START_MIN_Q14 11469
#define V_PITCH_GAIN_START_MAX_Q14 15565
#define PLC_MAX_PITCH_LAG_MS 18
#define RAND_BUF_SIZE 128
#define RAND_BUF_MASK (RAND_BUF_SIZE-1)
#define LOG2_INV_LPC_GAIN_HIGH_THRES 3
#define LOG2_INV_LPC_GAIN_LOW_THRES 8
#define PITCH_DRIFT_FAC_Q16 655

typedef struct {
   fl_i8 GainsIndices[MAX_NB_SUBFR];
   fl_i8 LTPIndex[MAX_NB_SUBFR];
   fl_i8 NLSFIndices[MAX_LPC_ORDER+1];
   fl_i16 lagIndex;
   fl_i8 contourIndex, signalType, quantOffsetType, NLSFInterpCoef_Q2, PERIndex, LTP_scaleIndex, Seed;
} silk_side_info;

typedef struct {
   fl_i32 pitchL_Q8;
   fl_i16 LTPCoef_Q14[LTP_ORDER];
   fl_i16 prevLPC_Q12[MAX_LPC_ORDER];
   int last_frame_lost;
   fl_i32 rand_seed;
   fl_i16 randScale_Q14;
   fl_i32 conc_energy;
   int conc_energy_shift;
   fl_i16 prevLTP_scale_Q14;
   fl_i32 prevGain_Q16[2];
   int fs_kHz, nb_subfr, subfr_length;
} silk_plc_state;

typedef struct {
   fl_i32 CNG_exc_buf_Q14[MAX_FRAME_LENGTH];
   fl_i16 CNG_smth_NLSF_Q15[MAX_LPC_ORDER];
   fl_i32 CNG_synth_state[MAX_LPC_ORDER];
   fl_i32 CNG_smth_Gain_Q16, rand_seed;
   int fs_kHz;
} silk_cng_state;

typedef struct {
   /* fields from prev_gain_Q16 on are cleared on reset */
   fl_i32 prev_gain_Q16;
   fl_i32 exc_Q14[MAX_FRAME_LENGTH];
   fl_i32 sLPC_Q14_buf[MAX_LPC_ORDER];
   fl_i16 outBuf[MAX_FRAME_LENGTH+2*MAX_SUB_FRAME_LENGTH];
   int lagPrev;
   fl_i8 LastGainIndex;
   int fs_kHz;
   fl_i32 fs_API_hz;
   int nb_subfr, frame_length, subfr_length, ltp_mem_length, LPC_order;
   fl_i16 prevNLSF_Q15[MAX_LPC_ORDER];
   int first_frame_after_reset;
   const fl_u8 *pitch_lag_low_bits_iCDF, *pitch_contour_iCDF;
   int nFramesDecoded, nFramesPerPacket, ec_prevSignalType;
   fl_i16 ec_prevLagIndex;
   int VAD_flags[MAX_FRAMES_PER_PACKET], LBRR_flag, LBRR_flags[MAX_FRAMES_PER_PACKET];
   silk_resampler_state resampler_state;
   const fl_nlsf_cb *psNLSF_CB;
   silk_side_info indices;
   silk_cng_state sCNG;
   int lossCnt, prevSignalType;
   silk_plc_state sPLC;
} silk_dec_state;

typedef struct {
   int pitchL[MAX_NB_SUBFR];
   fl_i32 Gains_Q16[MAX_NB_SUBFR];
   fl_i16 PredCoef_Q12[2][MAX_LPC_ORDER];
   fl_i16 LTPCoef_Q14[LTP_ORDER*MAX_NB_SUBFR];
   int LTP_scale_Q14;
} silk_dec_ctrl;

typedef struct {
   fl_i16 pred_prev_Q13[2], sMid[2], sSide[2];
} silk_stereo_dec;

typedef struct {
   silk_dec_state channel_state[2];
   silk_stereo_dec sStereo;
   int nChannelsAPI, nChannelsInternal, prev_decode_only_middle;
} silk_decoder;

typedef struct {
   int nChannelsAPI, nChannelsInternal;
   fl_i32 API_sampleRate, internalSampleRate;
   int payloadSize_ms, prevPitchLag;
} silk_dec_control;

static void silk_CNG_Reset(silk_dec_state *psDec) {
   int i, NLSF_step_Q15, NLSF_acc_Q15;
   NLSF_step_Q15 = silk_DIV32_16(silk_int16_MAX, psDec->LPC_order+1);
   NLSF_acc_Q15 = 0;
   for (i = 0; i < psDec->LPC_order; i++) {
      NLSF_acc_Q15 += NLSF_step_Q15;
      psDec->sCNG.CNG_smth_NLSF_Q15[i] = NLSF_acc_Q15;
   }
   psDec->sCNG.CNG_smth_Gain_Q16 = 0;
   psDec->sCNG.rand_seed = 3176576;
}
static void silk_PLC_Reset(silk_dec_state *psDec) {
   psDec->sPLC.pitchL_Q8 = silk_LSHIFT(psDec->frame_length, 8-1);
   psDec->sPLC.prevGain_Q16[0] = SILK_FIX_CONST(1, 16);
   psDec->sPLC.prevGain_Q16[1] = SILK_FIX_CONST(1, 16);
   psDec->sPLC.subfr_length = 20;
   psDec->sPLC.nb_subfr = 2;
}
static void silk_reset_decoder(silk_dec_state *psDec) {
   memset(&psDec->prev_gain_Q16, 0, sizeof(*psDec)-((char*)&psDec->prev_gain_Q16-(char*)psDec));
   psDec->first_frame_after_reset = 1;
   psDec->prev_gain_Q16 = 65536;
   silk_CNG_Reset(psDec);
   silk_PLC_Reset(psDec);
}
static void silk_init_decoder(silk_dec_state *psDec) {
   memset(psDec, 0, sizeof(*psDec));
   silk_reset_decoder(psDec);
}

static int silk_decoder_set_fs(silk_dec_state *psDec, int fs_kHz, fl_i32 fs_API_Hz) {
   int frame_length, ret = 0;
   psDec->subfr_length = silk_SMULBB(SUB_FRAME_LENGTH_MS, fs_kHz);
   frame_length = silk_SMULBB(psDec->nb_subfr, psDec->subfr_length);
   if (psDec->fs_kHz != fs_kHz || psDec->fs_API_hz != fs_API_Hz) {
      ret += silk_resampler_init(&psDec->resampler_state, silk_SMULBB(fs_kHz, 1000), fs_API_Hz, 0);
      psDec->fs_API_hz = fs_API_Hz;
   }
   if (psDec->fs_kHz != fs_kHz || frame_length != psDec->frame_length) {
      if (fs_kHz == 8) {
         psDec->pitch_contour_iCDF = psDec->nb_subfr == MAX_NB_SUBFR ? fl_pitch_contour_NB_iCDF : fl_pitch_contour_10_ms_NB_iCDF;
      } else {
         psDec->pitch_contour_iCDF = psDec->nb_subfr == MAX_NB_SUBFR ? fl_pitch_contour_iCDF : fl_pitch_contour_10_ms_iCDF;
      }
      if (psDec->fs_kHz != fs_kHz) {
         psDec->ltp_mem_length = silk_SMULBB(LTP_MEM_LENGTH_MS, fs_kHz);
         if (fs_kHz == 8 || fs_kHz == 12) {
            psDec->LPC_order = MIN_LPC_ORDER;
            psDec->psNLSF_CB = &fl_NLSF_CB_NB_MB;
         } else {
            psDec->LPC_order = MAX_LPC_ORDER;
            psDec->psNLSF_CB = &fl_NLSF_CB_WB;
         }
         if (fs_kHz == 16) psDec->pitch_lag_low_bits_iCDF = fl_uniform8_iCDF;
         else if (fs_kHz == 12) psDec->pitch_lag_low_bits_iCDF = fl_uniform6_iCDF;
         else psDec->pitch_lag_low_bits_iCDF = fl_uniform4_iCDF;
         psDec->first_frame_after_reset = 1;
         psDec->lagPrev = 100;
         psDec->LastGainIndex = 10;
         psDec->prevSignalType = TYPE_NO_VOICE_ACTIVITY;
         memset(psDec->outBuf, 0, sizeof(psDec->outBuf));
         memset(psDec->sLPC_Q14_buf, 0, sizeof(psDec->sLPC_Q14_buf));
      }
      psDec->fs_kHz = fs_kHz;
      psDec->frame_length = frame_length;
   }
   return ret;
}

static void silk_decode_indices(silk_dec_state *psDec, ec_ctx *dec, int FrameIndex, int decode_LBRR, int condCoding) {
   int i, k, Ix, decode_absolute_lagIndex, delta_lagIndex;
   fl_i16 ec_ix[MAX_LPC_ORDER];
   fl_u8 pred_Q8[MAX_LPC_ORDER];
   silk_side_info *ind = &psDec->indices;
   if (decode_LBRR || psDec->VAD_flags[FrameIndex]) Ix = ec_dec_icdf(dec, fl_type_offset_VAD_iCDF, 8)+2;
   else Ix = ec_dec_icdf(dec, fl_type_offset_no_VAD_iCDF, 8);
   ind->signalType = (fl_i8)silk_RSHIFT(Ix, 1);
   ind->quantOffsetType = (fl_i8)(Ix&1);
   if (condCoding == CODE_CONDITIONALLY) {
      ind->GainsIndices[0] = (fl_i8)ec_dec_icdf(dec, fl_delta_gain_iCDF, 8);
   } else {
      ind->GainsIndices[0] = (fl_i8)silk_LSHIFT(ec_dec_icdf(dec, fl_gain_iCDF[ind->signalType], 8), 3);
      ind->GainsIndices[0] += (fl_i8)ec_dec_icdf(dec, fl_uniform8_iCDF, 8);
   }
   for (i = 1; i < psDec->nb_subfr; i++) ind->GainsIndices[i] = (fl_i8)ec_dec_icdf(dec, fl_delta_gain_iCDF, 8);
   ind->NLSFIndices[0] = (fl_i8)ec_dec_icdf(dec, &psDec->psNLSF_CB->CB1_iCDF[(ind->signalType>>1)*psDec->psNLSF_CB->nVectors], 8);
   silk_NLSF_unpack(ec_ix, pred_Q8, psDec->psNLSF_CB, ind->NLSFIndices[0]);
   for (i = 0; i < psDec->psNLSF_CB->order; i++) {
      Ix = ec_dec_icdf(dec, &psDec->psNLSF_CB->ec_iCDF[ec_ix[i]], 8);
      if (Ix == 0) Ix -= ec_dec_icdf(dec, fl_NLSF_EXT_iCDF, 8);
      else if (Ix == 2*NLSF_QUANT_MAX_AMPLITUDE) Ix += ec_dec_icdf(dec, fl_NLSF_EXT_iCDF, 8);
      ind->NLSFIndices[i+1] = (fl_i8)(Ix-NLSF_QUANT_MAX_AMPLITUDE);
   }
   if (psDec->nb_subfr == MAX_NB_SUBFR) ind->NLSFInterpCoef_Q2 = (fl_i8)ec_dec_icdf(dec, fl_NLSF_interpolation_factor_iCDF, 8);
   else ind->NLSFInterpCoef_Q2 = 4;
   if (ind->signalType == TYPE_VOICED) {
      decode_absolute_lagIndex = 1;
      if (condCoding == CODE_CONDITIONALLY && psDec->ec_prevSignalType == TYPE_VOICED) {
         delta_lagIndex = (fl_i16)ec_dec_icdf(dec, fl_pitch_delta_iCDF, 8);
         if (delta_lagIndex > 0) {
            delta_lagIndex = delta_lagIndex-9;
            ind->lagIndex = (fl_i16)(psDec->ec_prevLagIndex+delta_lagIndex);
            decode_absolute_lagIndex = 0;
         }
      }
      if (decode_absolute_lagIndex) {
         ind->lagIndex = (fl_i16)ec_dec_icdf(dec, fl_pitch_lag_iCDF, 8)*silk_RSHIFT(psDec->fs_kHz, 1);
         ind->lagIndex += (fl_i16)ec_dec_icdf(dec, psDec->pitch_lag_low_bits_iCDF, 8);
      }
      psDec->ec_prevLagIndex = ind->lagIndex;
      ind->contourIndex = (fl_i8)ec_dec_icdf(dec, psDec->pitch_contour_iCDF, 8);
      ind->PERIndex = (fl_i8)ec_dec_icdf(dec, fl_LTP_per_index_iCDF, 8);
      for (k = 0; k < psDec->nb_subfr; k++)
         ind->LTPIndex[k] = (fl_i8)ec_dec_icdf(dec, fl_LTP_gain_iCDF_ptrs[ind->PERIndex], 8);
      if (condCoding == CODE_INDEPENDENTLY) ind->LTP_scaleIndex = (fl_i8)ec_dec_icdf(dec, fl_LTPscale_iCDF, 8);
      else ind->LTP_scaleIndex = 0;
   }
   psDec->ec_prevSignalType = ind->signalType;
   ind->Seed = (fl_i8)ec_dec_icdf(dec, fl_uniform4_iCDF, 8);
}

static void silk_decode_pulses(ec_ctx *dec, fl_i16 pulses[], const int signalType, const int quantOffsetType, const int frame_length) {
   int i, j, k, iter, abs_q, nLS, RateLevelIndex;
   int sum_pulses[MAX_NB_SHELL_BLOCKS], nLshifts[MAX_NB_SHELL_BLOCKS];
   fl_i16 *pulses_ptr;
   const fl_u8 *cdf_ptr;
   RateLevelIndex = ec_dec_icdf(dec, fl_rate_levels_iCDF[signalType>>1], 8);
   iter = silk_RSHIFT(frame_length, LOG2_SHELL_CODEC_FRAME_LENGTH);
   if (iter*SHELL_CODEC_FRAME_LENGTH < frame_length) iter++;
   cdf_ptr = fl_pulses_per_block_iCDF[RateLevelIndex];
   for (i = 0; i < iter; i++) {
      nLshifts[i] = 0;
      sum_pulses[i] = ec_dec_icdf(dec, cdf_ptr, 8);
      while (sum_pulses[i] == SILK_MAX_PULSES+1) {
         nLshifts[i]++;
         sum_pulses[i] = ec_dec_icdf(dec, fl_pulses_per_block_iCDF[N_RATE_LEVELS-1]+(nLshifts[i] == 10), 8);
      }
   }
   for (i = 0; i < iter; i++) {
      if (sum_pulses[i] > 0) silk_shell_decoder(&pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)], dec, sum_pulses[i]);
      else memset(&pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)], 0, SHELL_CODEC_FRAME_LENGTH*sizeof(pulses[0]));
   }
   for (i = 0; i < iter; i++) {
      if (nLshifts[i] > 0) {
         nLS = nLshifts[i];
         pulses_ptr = &pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)];
         for (k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++) {
            abs_q = pulses_ptr[k];
            for (j = 0; j < nLS; j++) {
               abs_q = silk_LSHIFT(abs_q, 1);
               abs_q += ec_dec_icdf(dec, fl_lsb_iCDF, 8);
            }
            pulses_ptr[k] = abs_q;
         }
         sum_pulses[i] |= nLS<<5;
      }
   }
   silk_decode_signs(dec, pulses, frame_length, signalType, quantOffsetType, sum_pulses);
}

static void silk_decode_parameters(silk_dec_state *psDec, silk_dec_ctrl *ctrl, int condCoding) {
   int i, k, Ix;
   fl_i16 pNLSF_Q15[MAX_LPC_ORDER], pNLSF0_Q15[MAX_LPC_ORDER];
   const fl_i8 *cbk_ptr_Q7;
   silk_gains_dequant(ctrl->Gains_Q16, psDec->indices.GainsIndices, &psDec->LastGainIndex,
         condCoding == CODE_CONDITIONALLY, psDec->nb_subfr);
   silk_NLSF_decode(pNLSF_Q15, psDec->indices.NLSFIndices, psDec->psNLSF_CB);
   silk_NLSF2A(ctrl->PredCoef_Q12[1], pNLSF_Q15, psDec->LPC_order);
   if (psDec->first_frame_after_reset == 1) psDec->indices.NLSFInterpCoef_Q2 = 4;
   if (psDec->indices.NLSFInterpCoef_Q2 < 4) {
      for (i = 0; i < psDec->LPC_order; i++)
         pNLSF0_Q15[i] = psDec->prevNLSF_Q15[i]+silk_RSHIFT(silk_MUL(psDec->indices.NLSFInterpCoef_Q2,
               pNLSF_Q15[i]-psDec->prevNLSF_Q15[i]), 2);
      silk_NLSF2A(ctrl->PredCoef_Q12[0], pNLSF0_Q15, psDec->LPC_order);
   } else {
      memcpy(ctrl->PredCoef_Q12[0], ctrl->PredCoef_Q12[1], psDec->LPC_order*sizeof(fl_i16));
   }
   memcpy(psDec->prevNLSF_Q15, pNLSF_Q15, psDec->LPC_order*sizeof(fl_i16));
   if (psDec->lossCnt) {
      silk_bwexpander(ctrl->PredCoef_Q12[0], psDec->LPC_order, BWE_AFTER_LOSS_Q16);
      silk_bwexpander(ctrl->PredCoef_Q12[1], psDec->LPC_order, BWE_AFTER_LOSS_Q16);
   }
   if (psDec->indices.signalType == TYPE_VOICED) {
      silk_decode_pitch(psDec->indices.lagIndex, psDec->indices.contourIndex, ctrl->pitchL, psDec->fs_kHz, psDec->nb_subfr);
      cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[psDec->indices.PERIndex];
      for (k = 0; k < psDec->nb_subfr; k++) {
         Ix = psDec->indices.LTPIndex[k];
         for (i = 0; i < LTP_ORDER; i++)
            ctrl->LTPCoef_Q14[k*LTP_ORDER+i] = silk_LSHIFT(cbk_ptr_Q7[Ix*LTP_ORDER+i], 7);
      }
      Ix = psDec->indices.LTP_scaleIndex;
      ctrl->LTP_scale_Q14 = fl_LTPScales_table_Q14[Ix];
   } else {
      memset(ctrl->pitchL, 0, psDec->nb_subfr*sizeof(int));
      memset(ctrl->LTPCoef_Q14, 0, LTP_ORDER*psDec->nb_subfr*sizeof(fl_i16));
      psDec->indices.PERIndex = 0;
      ctrl->LTP_scale_Q14 = 0;
   }
}

static void silk_decode_core(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 xq[], const fl_i16 pulses[MAX_FRAME_LENGTH]) {
   int i, j, k, lag = 0, start_idx, sLTP_buf_idx, NLSF_interpolation_flag, signalType;
   fl_i16 *A_Q12, *B_Q14, *pxq, A_Q12_tmp[MAX_LPC_ORDER];
   fl_i16 sLTP[MAX_FRAME_LENGTH*2];
   fl_i32 sLTP_Q15[MAX_FRAME_LENGTH*3], res_Q14[MAX_SUB_FRAME_LENGTH], sLPC_Q14[MAX_SUB_FRAME_LENGTH+MAX_LPC_ORDER];
   fl_i32 LTP_pred_Q13, LPC_pred_Q10, Gain_Q10, inv_gain_Q31, gain_adj_Q16, rand_seed, offset_Q10;
   fl_i32 *pred_lag_ptr, *pexc_Q14, *pres_Q14;
   offset_Q10 = fl_Quantization_Offsets_Q10[psDec->indices.signalType>>1][psDec->indices.quantOffsetType];
   NLSF_interpolation_flag = psDec->indices.NLSFInterpCoef_Q2 < 1<<2;
   rand_seed = psDec->indices.Seed;
   for (i = 0; i < psDec->frame_length; i++) {
      rand_seed = silk_RAND(rand_seed);
      psDec->exc_Q14[i] = silk_LSHIFT((fl_i32)pulses[i], 14);
      if (psDec->exc_Q14[i] > 0) psDec->exc_Q14[i] -= QUANT_LEVEL_ADJUST_Q10<<4;
      else if (psDec->exc_Q14[i] < 0) psDec->exc_Q14[i] += QUANT_LEVEL_ADJUST_Q10<<4;
      psDec->exc_Q14[i] += offset_Q10<<4;
      if (rand_seed < 0) psDec->exc_Q14[i] = -psDec->exc_Q14[i];
      rand_seed = silk_ADD32_ovflw(rand_seed, pulses[i]);
   }
   memcpy(sLPC_Q14, psDec->sLPC_Q14_buf, MAX_LPC_ORDER*sizeof(fl_i32));
   pexc_Q14 = psDec->exc_Q14;
   pxq = xq;
   sLTP_buf_idx = psDec->ltp_mem_length;
   for (k = 0; k < psDec->nb_subfr; k++) {
      pres_Q14 = res_Q14;
      A_Q12 = ctrl->PredCoef_Q12[k>>1];
      memcpy(A_Q12_tmp, A_Q12, psDec->LPC_order*sizeof(fl_i16));
      B_Q14 = &ctrl->LTPCoef_Q14[k*LTP_ORDER];
      signalType = psDec->indices.signalType;
      Gain_Q10 = silk_RSHIFT(ctrl->Gains_Q16[k], 6);
      inv_gain_Q31 = silk_INVERSE32_varQ(ctrl->Gains_Q16[k], 47);
      if (ctrl->Gains_Q16[k] != psDec->prev_gain_Q16) {
         gain_adj_Q16 = silk_DIV32_varQ(psDec->prev_gain_Q16, ctrl->Gains_Q16[k], 16);
         for (i = 0; i < MAX_LPC_ORDER; i++) sLPC_Q14[i] = silk_SMULWW(gain_adj_Q16, sLPC_Q14[i]);
      } else gain_adj_Q16 = (fl_i32)1<<16;
      psDec->prev_gain_Q16 = ctrl->Gains_Q16[k];
      if (psDec->lossCnt && psDec->prevSignalType == TYPE_VOICED && psDec->indices.signalType != TYPE_VOICED && k < MAX_NB_SUBFR/2) {
         memset(B_Q14, 0, LTP_ORDER*sizeof(fl_i16));
         B_Q14[LTP_ORDER/2] = SILK_FIX_CONST(0.25, 14);
         signalType = TYPE_VOICED;
         ctrl->pitchL[k] = psDec->lagPrev;
      }
      if (signalType == TYPE_VOICED) {
         lag = ctrl->pitchL[k];
         if (k == 0 || (k == 2 && NLSF_interpolation_flag)) {
            start_idx = psDec->ltp_mem_length-lag-psDec->LPC_order-LTP_ORDER/2;
            if (k == 2) memcpy(&psDec->outBuf[psDec->ltp_mem_length], xq, 2*psDec->subfr_length*sizeof(fl_i16));
            silk_LPC_analysis_filter(&sLTP[start_idx], &psDec->outBuf[start_idx+k*psDec->subfr_length], A_Q12,
                  psDec->ltp_mem_length-start_idx, psDec->LPC_order);
            if (k == 0) inv_gain_Q31 = silk_LSHIFT(silk_SMULWB(inv_gain_Q31, ctrl->LTP_scale_Q14), 2);
            for (i = 0; i < lag+LTP_ORDER/2; i++)
               sLTP_Q15[sLTP_buf_idx-i-1] = silk_SMULWB(inv_gain_Q31, sLTP[psDec->ltp_mem_length-i-1]);
         } else if (gain_adj_Q16 != (fl_i32)1<<16) {
            for (i = 0; i < lag+LTP_ORDER/2; i++)
               sLTP_Q15[sLTP_buf_idx-i-1] = silk_SMULWW(gain_adj_Q16, sLTP_Q15[sLTP_buf_idx-i-1]);
         }
      }
      if (signalType == TYPE_VOICED) {
         pred_lag_ptr = &sLTP_Q15[sLTP_buf_idx-lag+LTP_ORDER/2];
         for (i = 0; i < psDec->subfr_length; i++) {
            LTP_pred_Q13 = 2;
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[0], B_Q14[0]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-1], B_Q14[1]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-2], B_Q14[2]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-3], B_Q14[3]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-4], B_Q14[4]);
            pred_lag_ptr++;
            pres_Q14[i] = silk_ADD_LSHIFT32(pexc_Q14[i], LTP_pred_Q13, 1);
            sLTP_Q15[sLTP_buf_idx] = silk_LSHIFT(pres_Q14[i], 1);
            sLTP_buf_idx++;
         }
      } else pres_Q14 = pexc_Q14;
      for (i = 0; i < psDec->subfr_length; i++) {
         LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
         for (j = 0; j < psDec->LPC_order; j++)
            LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, sLPC_Q14[MAX_LPC_ORDER+i-j-1], A_Q12_tmp[j]);
         sLPC_Q14[MAX_LPC_ORDER+i] = silk_ADD_SAT32(pres_Q14[i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
         pxq[i] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(sLPC_Q14[MAX_LPC_ORDER+i], Gain_Q10), 8));
      }
      memcpy(sLPC_Q14, &sLPC_Q14[psDec->subfr_length], MAX_LPC_ORDER*sizeof(fl_i32));
      pexc_Q14 += psDec->subfr_length;
      pxq += psDec->subfr_length;
   }
   memcpy(psDec->sLPC_Q14_buf, sLPC_Q14, MAX_LPC_ORDER*sizeof(fl_i32));
}

/* ---- SILK packet loss concealment ---- */
static const fl_i16 HARM_ATT_Q15[2] = {32440, 31130};
static const fl_i16 PLC_RAND_ATTENUATE_V_Q15[2] = {31130, 26214};
static const fl_i16 PLC_RAND_ATTENUATE_UV_Q15[2] = {32440, 29491};

static void silk_PLC_update(silk_dec_state *psDec, silk_dec_ctrl *ctrl) {
   fl_i32 LTP_Gain_Q14, temp_LTP_Gain_Q14;
   int i, j;
   silk_plc_state *psPLC = &psDec->sPLC;
   psDec->prevSignalType = psDec->indices.signalType;
   LTP_Gain_Q14 = 0;
   if (psDec->indices.signalType == TYPE_VOICED) {
      for (j = 0; j*psDec->subfr_length < ctrl->pitchL[psDec->nb_subfr-1]; j++) {
         if (j == psDec->nb_subfr) break;
         temp_LTP_Gain_Q14 = 0;
         for (i = 0; i < LTP_ORDER; i++) temp_LTP_Gain_Q14 += ctrl->LTPCoef_Q14[(psDec->nb_subfr-1-j)*LTP_ORDER+i];
         if (temp_LTP_Gain_Q14 > LTP_Gain_Q14) {
            LTP_Gain_Q14 = temp_LTP_Gain_Q14;
            memcpy(psPLC->LTPCoef_Q14, &ctrl->LTPCoef_Q14[silk_SMULBB(psDec->nb_subfr-1-j, LTP_ORDER)], LTP_ORDER*sizeof(fl_i16));
            psPLC->pitchL_Q8 = silk_LSHIFT(ctrl->pitchL[psDec->nb_subfr-1-j], 8);
         }
      }
      memset(psPLC->LTPCoef_Q14, 0, LTP_ORDER*sizeof(fl_i16));
      psPLC->LTPCoef_Q14[LTP_ORDER/2] = LTP_Gain_Q14;
      if (LTP_Gain_Q14 < V_PITCH_GAIN_START_MIN_Q14) {
         int scale_Q10;
         fl_i32 tmp = silk_LSHIFT(V_PITCH_GAIN_START_MIN_Q14, 10);
         scale_Q10 = silk_DIV32(tmp, silk_max(LTP_Gain_Q14, 1));
         for (i = 0; i < LTP_ORDER; i++)
            psPLC->LTPCoef_Q14[i] = silk_RSHIFT(silk_SMULBB(psPLC->LTPCoef_Q14[i], scale_Q10), 10);
      } else if (LTP_Gain_Q14 > V_PITCH_GAIN_START_MAX_Q14) {
         int scale_Q14;
         fl_i32 tmp = silk_LSHIFT(V_PITCH_GAIN_START_MAX_Q14, 14);
         scale_Q14 = silk_DIV32(tmp, silk_max(LTP_Gain_Q14, 1));
         for (i = 0; i < LTP_ORDER; i++)
            psPLC->LTPCoef_Q14[i] = silk_RSHIFT(silk_SMULBB(psPLC->LTPCoef_Q14[i], scale_Q14), 14);
      }
   } else {
      psPLC->pitchL_Q8 = silk_LSHIFT(silk_SMULBB(psDec->fs_kHz, 18), 8);
      memset(psPLC->LTPCoef_Q14, 0, LTP_ORDER*sizeof(fl_i16));
   }
   memcpy(psPLC->prevLPC_Q12, ctrl->PredCoef_Q12[1], psDec->LPC_order*sizeof(fl_i16));
   psPLC->prevLTP_scale_Q14 = ctrl->LTP_scale_Q14;
   memcpy(psPLC->prevGain_Q16, &ctrl->Gains_Q16[psDec->nb_subfr-2], 2*sizeof(fl_i32));
   psPLC->subfr_length = psDec->subfr_length;
   psPLC->nb_subfr = psDec->nb_subfr;
}

static void silk_PLC_energy(fl_i32 *energy1, int *shift1, fl_i32 *energy2, int *shift2, const fl_i32 *exc_Q14,
      const fl_i32 *prevGain_Q10, int subfr_length, int nb_subfr) {
   int i, k;
   fl_i16 exc_buf[2*MAX_SUB_FRAME_LENGTH], *exc_buf_ptr = exc_buf;
   for (k = 0; k < 2; k++) {
      for (i = 0; i < subfr_length; i++)
         exc_buf_ptr[i] = (fl_i16)silk_SAT16(silk_RSHIFT(silk_SMULWW(exc_Q14[i+(k+nb_subfr-2)*subfr_length], prevGain_Q10[k]), 8));
      exc_buf_ptr += subfr_length;
   }
   silk_sum_sqr_shift(energy1, shift1, exc_buf, subfr_length);
   silk_sum_sqr_shift(energy2, shift2, &exc_buf[subfr_length], subfr_length);
}

static void silk_PLC_conceal(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[]) {
   int i, j, k, lag, idx, sLTP_buf_idx, shift1, shift2;
   fl_i32 rand_seed, harm_Gain_Q15, rand_Gain_Q15, inv_gain_Q30, energy1, energy2, *rand_ptr, *pred_lag_ptr;
   fl_i32 LPC_pred_Q10, LTP_pred_Q12, *sLPC_Q14_ptr, prevGain_Q10[2];
   fl_i16 rand_scale_Q14, *B_Q14, A_Q12[MAX_LPC_ORDER], sLTP[MAX_FRAME_LENGTH*2];
   fl_i32 sLTP_Q14[MAX_FRAME_LENGTH*3];
   silk_plc_state *psPLC = &psDec->sPLC;
   prevGain_Q10[0] = silk_RSHIFT(psPLC->prevGain_Q16[0], 6);
   prevGain_Q10[1] = silk_RSHIFT(psPLC->prevGain_Q16[1], 6);
   if (psDec->first_frame_after_reset) memset(psPLC->prevLPC_Q12, 0, sizeof(psPLC->prevLPC_Q12));
   silk_PLC_energy(&energy1, &shift1, &energy2, &shift2, psDec->exc_Q14, prevGain_Q10, psDec->subfr_length, psDec->nb_subfr);
   if (silk_RSHIFT(energy1, shift2) < silk_RSHIFT(energy2, shift1))
      rand_ptr = &psDec->exc_Q14[silk_max_int(0, (psPLC->nb_subfr-1)*psPLC->subfr_length-RAND_BUF_SIZE)];
   else
      rand_ptr = &psDec->exc_Q14[silk_max_int(0, psPLC->nb_subfr*psPLC->subfr_length-RAND_BUF_SIZE)];
   B_Q14 = psPLC->LTPCoef_Q14;
   rand_scale_Q14 = psPLC->randScale_Q14;
   harm_Gain_Q15 = HARM_ATT_Q15[silk_min_int(1, psDec->lossCnt)];
   if (psDec->prevSignalType == TYPE_VOICED) rand_Gain_Q15 = PLC_RAND_ATTENUATE_V_Q15[silk_min_int(1, psDec->lossCnt)];
   else rand_Gain_Q15 = PLC_RAND_ATTENUATE_UV_Q15[silk_min_int(1, psDec->lossCnt)];
   silk_bwexpander(psPLC->prevLPC_Q12, psDec->LPC_order, SILK_FIX_CONST(PLC_BWE_COEF, 16));
   memcpy(A_Q12, psPLC->prevLPC_Q12, psDec->LPC_order*sizeof(fl_i16));
   if (psDec->lossCnt == 0) {
      rand_scale_Q14 = 1<<14;
      if (psDec->prevSignalType == TYPE_VOICED) {
         for (i = 0; i < LTP_ORDER; i++) rand_scale_Q14 -= B_Q14[i];
         rand_scale_Q14 = silk_max_16(3277, rand_scale_Q14);
         rand_scale_Q14 = (fl_i16)silk_RSHIFT(silk_SMULBB(rand_scale_Q14, psPLC->prevLTP_scale_Q14), 14);
      } else {
         fl_i32 invGain_Q30, down_scale_Q30;
         invGain_Q30 = silk_LPC_inverse_pred_gain(psPLC->prevLPC_Q12, psDec->LPC_order);
         down_scale_Q30 = silk_min_32(silk_RSHIFT((fl_i32)1<<30, LOG2_INV_LPC_GAIN_HIGH_THRES), invGain_Q30);
         down_scale_Q30 = silk_max_32(silk_RSHIFT((fl_i32)1<<30, LOG2_INV_LPC_GAIN_LOW_THRES), down_scale_Q30);
         down_scale_Q30 = silk_LSHIFT(down_scale_Q30, LOG2_INV_LPC_GAIN_HIGH_THRES);
         rand_Gain_Q15 = silk_RSHIFT(silk_SMULWB(down_scale_Q30, rand_Gain_Q15), 14);
      }
   }
   rand_seed = psPLC->rand_seed;
   lag = silk_RSHIFT_ROUND(psPLC->pitchL_Q8, 8);
   sLTP_buf_idx = psDec->ltp_mem_length;
   idx = psDec->ltp_mem_length-lag-psDec->LPC_order-LTP_ORDER/2;
   silk_LPC_analysis_filter(&sLTP[idx], &psDec->outBuf[idx], A_Q12, psDec->ltp_mem_length-idx, psDec->LPC_order);
   inv_gain_Q30 = silk_INVERSE32_varQ(psPLC->prevGain_Q16[1], 46);
   inv_gain_Q30 = silk_min(inv_gain_Q30, silk_int32_MAX>>1);
   for (i = idx+psDec->LPC_order; i < psDec->ltp_mem_length; i++) sLTP_Q14[i] = silk_SMULWB(inv_gain_Q30, sLTP[i]);
   for (k = 0; k < psDec->nb_subfr; k++) {
      pred_lag_ptr = &sLTP_Q14[sLTP_buf_idx-lag+LTP_ORDER/2];
      for (i = 0; i < psDec->subfr_length; i++) {
         LTP_pred_Q12 = 2;
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[0], B_Q14[0]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-1], B_Q14[1]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-2], B_Q14[2]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-3], B_Q14[3]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-4], B_Q14[4]);
         pred_lag_ptr++;
         rand_seed = silk_RAND(rand_seed);
         idx = silk_RSHIFT(rand_seed, 25)&RAND_BUF_MASK;
         sLTP_Q14[sLTP_buf_idx] = silk_LSHIFT32(silk_SMLAWB(LTP_pred_Q12, rand_ptr[idx], rand_scale_Q14), 2);
         sLTP_buf_idx++;
      }
      for (j = 0; j < LTP_ORDER; j++) B_Q14[j] = silk_RSHIFT(silk_SMULBB(harm_Gain_Q15, B_Q14[j]), 15);
      rand_scale_Q14 = silk_RSHIFT(silk_SMULBB(rand_scale_Q14, rand_Gain_Q15), 15);
      psPLC->pitchL_Q8 = silk_SMLAWB(psPLC->pitchL_Q8, psPLC->pitchL_Q8, PITCH_DRIFT_FAC_Q16);
      psPLC->pitchL_Q8 = silk_min_32(psPLC->pitchL_Q8, silk_LSHIFT(silk_SMULBB(PLC_MAX_PITCH_LAG_MS, psDec->fs_kHz), 8));
      lag = silk_RSHIFT_ROUND(psPLC->pitchL_Q8, 8);
   }
   sLPC_Q14_ptr = &sLTP_Q14[psDec->ltp_mem_length-MAX_LPC_ORDER];
   memcpy(sLPC_Q14_ptr, psDec->sLPC_Q14_buf, MAX_LPC_ORDER*sizeof(fl_i32));
   for (i = 0; i < psDec->frame_length; i++) {
      LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
      for (j = 0; j < psDec->LPC_order; j++)
         LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, sLPC_Q14_ptr[MAX_LPC_ORDER+i-j-1], A_Q12[j]);
      sLPC_Q14_ptr[MAX_LPC_ORDER+i] = silk_ADD_SAT32(sLPC_Q14_ptr[MAX_LPC_ORDER+i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
      frame[i] = (fl_i16)silk_SAT16(silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(sLPC_Q14_ptr[MAX_LPC_ORDER+i], prevGain_Q10[1]), 8)));
   }
   memcpy(psDec->sLPC_Q14_buf, &sLPC_Q14_ptr[psDec->frame_length], MAX_LPC_ORDER*sizeof(fl_i32));
   psPLC->rand_seed = rand_seed;
   psPLC->randScale_Q14 = rand_scale_Q14;
   for (i = 0; i < MAX_NB_SUBFR; i++) ctrl->pitchL[i] = lag;
}

static void silk_PLC(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[], int lost) {
   if (psDec->fs_kHz != psDec->sPLC.fs_kHz) {
      silk_PLC_Reset(psDec);
      psDec->sPLC.fs_kHz = psDec->fs_kHz;
   }
   if (lost) {
      silk_PLC_conceal(psDec, ctrl, frame);
      psDec->lossCnt++;
   } else silk_PLC_update(psDec, ctrl);
}

static void silk_PLC_glue_frames(silk_dec_state *psDec, fl_i16 frame[], int length) {
   int i, energy_shift;
   fl_i32 energy;
   silk_plc_state *psPLC = &psDec->sPLC;
   if (psDec->lossCnt) {
      silk_sum_sqr_shift(&psPLC->conc_energy, &psPLC->conc_energy_shift, frame, length);
      psPLC->last_frame_lost = 1;
   } else {
      if (psDec->sPLC.last_frame_lost) {
         silk_sum_sqr_shift(&energy, &energy_shift, frame, length);
         if (energy_shift > psPLC->conc_energy_shift)
            psPLC->conc_energy = silk_RSHIFT(psPLC->conc_energy, energy_shift-psPLC->conc_energy_shift);
         else if (energy_shift < psPLC->conc_energy_shift)
            energy = silk_RSHIFT(energy, psPLC->conc_energy_shift-energy_shift);
         if (energy > psPLC->conc_energy) {
            fl_i32 frac_Q24, LZ, gain_Q16, slope_Q16;
            LZ = silk_CLZ32(psPLC->conc_energy);
            LZ = LZ-1;
            psPLC->conc_energy = silk_LSHIFT(psPLC->conc_energy, LZ);
            energy = silk_RSHIFT(energy, silk_max_32(24-LZ, 0));
            frac_Q24 = silk_DIV32(psPLC->conc_energy, silk_max(energy, 1));
            gain_Q16 = silk_LSHIFT(silk_SQRT_APPROX(frac_Q24), 4);
            slope_Q16 = silk_DIV32_16(((fl_i32)1<<16)-gain_Q16, length);
            slope_Q16 = silk_LSHIFT(slope_Q16, 2);
            for (i = 0; i < length; i++) {
               frame[i] = silk_SMULWB(gain_Q16, frame[i]);
               gain_Q16 += slope_Q16;
               if (gain_Q16 > (fl_i32)1<<16) break;
            }
         }
      }
      psPLC->last_frame_lost = 0;
   }
}

/* ---- Comfort noise generation ---- */
static void silk_CNG_exc(fl_i32 exc_Q14[], fl_i32 exc_buf_Q14[], int length, fl_i32 *rand_seed) {
   fl_i32 seed;
   int i, idx, exc_mask = CNG_BUF_MASK_MAX;
   while (exc_mask > length) exc_mask = silk_RSHIFT(exc_mask, 1);
   seed = *rand_seed;
   for (i = 0; i < length; i++) {
      seed = silk_RAND(seed);
      idx = (int)(silk_RSHIFT(seed, 24)&exc_mask);
      exc_Q14[i] = exc_buf_Q14[idx];
   }
   *rand_seed = seed;
}
static void silk_CNG(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[], int length) {
   int i, j, subfr;
   fl_i32 LPC_pred_Q10, max_Gain_Q16, gain_Q16, gain_Q10;
   fl_i16 A_Q12[MAX_LPC_ORDER];
   silk_cng_state *psCNG = &psDec->sCNG;
   if (psDec->fs_kHz != psCNG->fs_kHz) {
      silk_CNG_Reset(psDec);
      psCNG->fs_kHz = psDec->fs_kHz;
   }
   if (psDec->lossCnt == 0 && psDec->prevSignalType == TYPE_NO_VOICE_ACTIVITY) {
      for (i = 0; i < psDec->LPC_order; i++)
         psCNG->CNG_smth_NLSF_Q15[i] += silk_SMULWB((fl_i32)psDec->prevNLSF_Q15[i]-(fl_i32)psCNG->CNG_smth_NLSF_Q15[i], CNG_NLSF_SMTH_Q16);
      max_Gain_Q16 = 0;
      subfr = 0;
      for (i = 0; i < psDec->nb_subfr; i++) {
         if (ctrl->Gains_Q16[i] > max_Gain_Q16) {
            max_Gain_Q16 = ctrl->Gains_Q16[i];
            subfr = i;
         }
      }
      memmove(&psCNG->CNG_exc_buf_Q14[psDec->subfr_length], psCNG->CNG_exc_buf_Q14, (psDec->nb_subfr-1)*psDec->subfr_length*sizeof(fl_i32));
      memcpy(psCNG->CNG_exc_buf_Q14, &psDec->exc_Q14[subfr*psDec->subfr_length], psDec->subfr_length*sizeof(fl_i32));
      for (i = 0; i < psDec->nb_subfr; i++) {
         psCNG->CNG_smth_Gain_Q16 += silk_SMULWB(ctrl->Gains_Q16[i]-psCNG->CNG_smth_Gain_Q16, CNG_GAIN_SMTH_Q16);
         if (silk_SMULWW(psCNG->CNG_smth_Gain_Q16, CNG_GAIN_SMTH_THRESHOLD_Q16) > ctrl->Gains_Q16[i])
            psCNG->CNG_smth_Gain_Q16 = ctrl->Gains_Q16[i];
      }
   }
   if (psDec->lossCnt) {
      fl_i32 CNG_sig_Q14[MAX_FRAME_LENGTH+MAX_LPC_ORDER];
      gain_Q16 = silk_SMULWW(psDec->sPLC.randScale_Q14, psDec->sPLC.prevGain_Q16[1]);
      if (gain_Q16 >= (1<<21) || psCNG->CNG_smth_Gain_Q16 > (1<<23)) {
         gain_Q16 = silk_SMULTT(gain_Q16, gain_Q16);
         gain_Q16 = silk_SUB_LSHIFT32(silk_SMULTT(psCNG->CNG_smth_Gain_Q16, psCNG->CNG_smth_Gain_Q16), gain_Q16, 5);
         gain_Q16 = silk_LSHIFT32(silk_SQRT_APPROX(gain_Q16), 16);
      } else {
         gain_Q16 = silk_SMULWW(gain_Q16, gain_Q16);
         gain_Q16 = silk_SUB_LSHIFT32(silk_SMULWW(psCNG->CNG_smth_Gain_Q16, psCNG->CNG_smth_Gain_Q16), gain_Q16, 5);
         gain_Q16 = silk_LSHIFT32(silk_SQRT_APPROX(gain_Q16), 8);
      }
      gain_Q10 = silk_RSHIFT(gain_Q16, 6);
      silk_CNG_exc(CNG_sig_Q14+MAX_LPC_ORDER, psCNG->CNG_exc_buf_Q14, length, &psCNG->rand_seed);
      silk_NLSF2A(A_Q12, psCNG->CNG_smth_NLSF_Q15, psDec->LPC_order);
      memcpy(CNG_sig_Q14, psCNG->CNG_synth_state, MAX_LPC_ORDER*sizeof(fl_i32));
      for (i = 0; i < length; i++) {
         LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
         for (j = 0; j < psDec->LPC_order; j++)
            LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, CNG_sig_Q14[MAX_LPC_ORDER+i-j-1], A_Q12[j]);
         CNG_sig_Q14[MAX_LPC_ORDER+i] = silk_ADD_SAT32(CNG_sig_Q14[MAX_LPC_ORDER+i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
         frame[i] = (fl_i16)silk_ADD_SAT16(frame[i], silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(CNG_sig_Q14[MAX_LPC_ORDER+i], gain_Q10), 8)));
      }
      memcpy(psCNG->CNG_synth_state, &CNG_sig_Q14[length], MAX_LPC_ORDER*sizeof(fl_i32));
   } else {
      memset(psCNG->CNG_synth_state, 0, psDec->LPC_order*sizeof(fl_i32));
   }
}

static int silk_decode_frame(silk_dec_state *psDec, ec_ctx *dec, fl_i16 pOut[], fl_i32 *pN, int lostFlag, int condCoding) {
   silk_dec_ctrl ctrl;
   int L, mv_len;
   L = psDec->frame_length;
   ctrl.LTP_scale_Q14 = 0;
   if (lostFlag == FLAG_DECODE_NORMAL || (lostFlag == FLAG_DECODE_LBRR && psDec->LBRR_flags[psDec->nFramesDecoded] == 1)) {
      fl_i16 pulses[(MAX_FRAME_LENGTH+SHELL_CODEC_FRAME_LENGTH-1)&~(SHELL_CODEC_FRAME_LENGTH-1)];
      silk_decode_indices(psDec, dec, psDec->nFramesDecoded, lostFlag, condCoding);
      silk_decode_pulses(dec, pulses, psDec->indices.signalType, psDec->indices.quantOffsetType, psDec->frame_length);
      silk_decode_parameters(psDec, &ctrl, condCoding);
      silk_decode_core(psDec, &ctrl, pOut, pulses);
      mv_len = psDec->ltp_mem_length-psDec->frame_length;
      memmove(psDec->outBuf, &psDec->outBuf[psDec->frame_length], mv_len*sizeof(fl_i16));
      memcpy(&psDec->outBuf[mv_len], pOut, psDec->frame_length*sizeof(fl_i16));
      silk_PLC(psDec, &ctrl, pOut, 0);
      psDec->lossCnt = 0;
      psDec->prevSignalType = psDec->indices.signalType;
      psDec->first_frame_after_reset = 0;
   } else {
      silk_PLC(psDec, &ctrl, pOut, 1);
      mv_len = psDec->ltp_mem_length-psDec->frame_length;
      memmove(psDec->outBuf, &psDec->outBuf[psDec->frame_length], mv_len*sizeof(fl_i16));
      memcpy(&psDec->outBuf[mv_len], pOut, psDec->frame_length*sizeof(fl_i16));
   }
   silk_CNG(psDec, &ctrl, pOut, L);
   silk_PLC_glue_frames(psDec, pOut, L);
   psDec->lagPrev = ctrl.pitchL[psDec->nb_subfr-1];
   *pN = L;
   return 0;
}

static void silk_stereo_decode_pred(ec_ctx *dec, fl_i32 pred_Q13[]) {
   int n, ix[2][3];
   fl_i32 low_Q13, step_Q13;
   n = ec_dec_icdf(dec, fl_stereo_pred_joint_iCDF, 8);
   ix[0][2] = silk_DIV32_16(n, 5);
   ix[1][2] = n-5*ix[0][2];
   for (n = 0; n < 2; n++) {
      ix[n][0] = ec_dec_icdf(dec, fl_uniform3_iCDF, 8);
      ix[n][1] = ec_dec_icdf(dec, fl_uniform5_iCDF, 8);
   }
   for (n = 0; n < 2; n++) {
      ix[n][0] += 3*ix[n][2];
      low_Q13 = fl_stereo_pred_quant_Q13[ix[n][0]];
      step_Q13 = silk_SMULWB(fl_stereo_pred_quant_Q13[ix[n][0]+1]-low_Q13, SILK_FIX_CONST(0.5/STEREO_QUANT_SUB_STEPS, 16));
      pred_Q13[n] = silk_SMLABB(low_Q13, step_Q13, 2*ix[n][1]+1);
   }
   pred_Q13[0] -= pred_Q13[1];
}

static void silk_stereo_MS_to_LR(silk_stereo_dec *state, fl_i16 x1[], fl_i16 x2[], const fl_i32 pred_Q13[], int fs_kHz, int frame_length) {
   int n, denom_Q16, delta0_Q13, delta1_Q13;
   fl_i32 sum, diff, pred0_Q13, pred1_Q13;
   memcpy(x1, state->sMid, 2*sizeof(fl_i16));
   memcpy(x2, state->sSide, 2*sizeof(fl_i16));
   memcpy(state->sMid, &x1[frame_length], 2*sizeof(fl_i16));
   memcpy(state->sSide, &x2[frame_length], 2*sizeof(fl_i16));
   pred0_Q13 = state->pred_prev_Q13[0];
   pred1_Q13 = state->pred_prev_Q13[1];
   denom_Q16 = silk_DIV32_16((fl_i32)1<<16, STEREO_INTERP_LEN_MS*fs_kHz);
   delta0_Q13 = silk_RSHIFT_ROUND(silk_SMULBB(pred_Q13[0]-state->pred_prev_Q13[0], denom_Q16), 16);
   delta1_Q13 = silk_RSHIFT_ROUND(silk_SMULBB(pred_Q13[1]-state->pred_prev_Q13[1], denom_Q16), 16);
   for (n = 0; n < STEREO_INTERP_LEN_MS*fs_kHz; n++) {
      pred0_Q13 += delta0_Q13;
      pred1_Q13 += delta1_Q13;
      sum = silk_LSHIFT(silk_ADD_LSHIFT32(x1[n]+(fl_i32)x1[n+2], x1[n+1], 1), 9);
      sum = silk_SMLAWB(silk_LSHIFT((fl_i32)x2[n+1], 8), sum, pred0_Q13);
      sum = silk_SMLAWB(sum, silk_LSHIFT((fl_i32)x1[n+1], 11), pred1_Q13);
      x2[n+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(sum, 8));
   }
   pred0_Q13 = pred_Q13[0];
   pred1_Q13 = pred_Q13[1];
   for (n = STEREO_INTERP_LEN_MS*fs_kHz; n < frame_length; n++) {
      sum = silk_LSHIFT(silk_ADD_LSHIFT32(x1[n]+(fl_i32)x1[n+2], x1[n+1], 1), 9);
      sum = silk_SMLAWB(silk_LSHIFT((fl_i32)x2[n+1], 8), sum, pred0_Q13);
      sum = silk_SMLAWB(sum, silk_LSHIFT((fl_i32)x1[n+1], 11), pred1_Q13);
      x2[n+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(sum, 8));
   }
   state->pred_prev_Q13[0] = pred_Q13[0];
   state->pred_prev_Q13[1] = pred_Q13[1];
   for (n = 0; n < frame_length; n++) {
      sum = x1[n+1]+(fl_i32)x2[n+1];
      diff = x1[n+1]-(fl_i32)x2[n+1];
      x1[n+1] = (fl_i16)silk_SAT16(sum);
      x2[n+1] = (fl_i16)silk_SAT16(diff);
   }
}

static void silk_dec_reset_all(silk_decoder *d) {
   int n;
   for (n = 0; n < 2; n++) silk_reset_decoder(&d->channel_state[n]);
   memset(&d->sStereo, 0, sizeof(d->sStereo));
   d->prev_decode_only_middle = 0;
}
static void silk_dec_init_all(silk_decoder *d) {
   int n;
   memset(d, 0, sizeof(*d));
   for (n = 0; n < 2; n++) silk_init_decoder(&d->channel_state[n]);
}

/* Decodes one SILK frame (10 or 20 ms) into float output at the API rate. */
static int silk_Decode(silk_decoder *psDec, silk_dec_control *decControl, int lostFlag, int newPacketFlag,
      ec_ctx *dec, float *samplesOut, fl_i32 *nSamplesOut) {
   int i, n, decode_only_middle = 0, ret = 0, has_side, stereo_to_mono;
   fl_i32 nSamplesOutDec = 0, LBRR_symbol, MS_pred_Q13[2] = {0, 0};
   fl_i16 samplesOut1_tmp_storage[2*(MAX_FRAME_LENGTH+2)], *samplesOut1_tmp[2];
   fl_i16 samplesOut2_tmp[MAX_API_FS_KHZ*MAX_FRAME_LENGTH_MS], *resample_out_ptr;
   silk_dec_state *channel_state = psDec->channel_state;
   if (newPacketFlag)
      for (n = 0; n < decControl->nChannelsInternal; n++) channel_state[n].nFramesDecoded = 0;
   if (decControl->nChannelsInternal > psDec->nChannelsInternal) silk_init_decoder(&channel_state[1]);
   stereo_to_mono = decControl->nChannelsInternal == 1 && psDec->nChannelsInternal == 2 &&
         (decControl->internalSampleRate == 1000*channel_state[0].fs_kHz);
   if (channel_state[0].nFramesDecoded == 0) {
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         int fs_kHz_dec;
         if (decControl->payloadSize_ms == 0 || decControl->payloadSize_ms == 10) {
            channel_state[n].nFramesPerPacket = 1;
            channel_state[n].nb_subfr = 2;
         } else if (decControl->payloadSize_ms == 20) {
            channel_state[n].nFramesPerPacket = 1;
            channel_state[n].nb_subfr = 4;
         } else if (decControl->payloadSize_ms == 40) {
            channel_state[n].nFramesPerPacket = 2;
            channel_state[n].nb_subfr = 4;
         } else if (decControl->payloadSize_ms == 60) {
            channel_state[n].nFramesPerPacket = 3;
            channel_state[n].nb_subfr = 4;
         } else return -203;
         fs_kHz_dec = (decControl->internalSampleRate>>10)+1;
         if (fs_kHz_dec != 8 && fs_kHz_dec != 12 && fs_kHz_dec != 16) return -200;
         ret += silk_decoder_set_fs(&channel_state[n], fs_kHz_dec, decControl->API_sampleRate);
      }
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 2 && (psDec->nChannelsAPI == 1 || psDec->nChannelsInternal == 1)) {
      memset(psDec->sStereo.pred_prev_Q13, 0, sizeof(psDec->sStereo.pred_prev_Q13));
      memset(psDec->sStereo.sSide, 0, sizeof(psDec->sStereo.sSide));
      memcpy(&channel_state[1].resampler_state, &channel_state[0].resampler_state, sizeof(silk_resampler_state));
   }
   psDec->nChannelsAPI = decControl->nChannelsAPI;
   psDec->nChannelsInternal = decControl->nChannelsInternal;
   if (lostFlag != FLAG_PACKET_LOST && channel_state[0].nFramesDecoded == 0) {
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         for (i = 0; i < channel_state[n].nFramesPerPacket; i++) channel_state[n].VAD_flags[i] = ec_dec_bit_logp(dec, 1);
         channel_state[n].LBRR_flag = ec_dec_bit_logp(dec, 1);
      }
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         memset(channel_state[n].LBRR_flags, 0, sizeof(channel_state[n].LBRR_flags));
         if (channel_state[n].LBRR_flag) {
            if (channel_state[n].nFramesPerPacket == 1) channel_state[n].LBRR_flags[0] = 1;
            else {
               LBRR_symbol = ec_dec_icdf(dec, fl_LBRR_flags_iCDF_ptr[channel_state[n].nFramesPerPacket-2], 8)+1;
               for (i = 0; i < channel_state[n].nFramesPerPacket; i++) channel_state[n].LBRR_flags[i] = silk_RSHIFT(LBRR_symbol, i)&1;
            }
         }
      }
      if (lostFlag == FLAG_DECODE_NORMAL) {
         for (i = 0; i < channel_state[0].nFramesPerPacket; i++) {
            for (n = 0; n < decControl->nChannelsInternal; n++) {
               if (channel_state[n].LBRR_flags[i]) {
                  fl_i16 pulses[MAX_FRAME_LENGTH];
                  int condCoding;
                  if (decControl->nChannelsInternal == 2 && n == 0) {
                     silk_stereo_decode_pred(dec, MS_pred_Q13);
                     if (channel_state[1].LBRR_flags[i] == 0) decode_only_middle = ec_dec_icdf(dec, fl_stereo_only_code_mid_iCDF, 8);
                  }
                  condCoding = (i > 0 && channel_state[n].LBRR_flags[i-1]) ? CODE_CONDITIONALLY : CODE_INDEPENDENTLY;
                  silk_decode_indices(&channel_state[n], dec, i, 1, condCoding);
                  silk_decode_pulses(dec, pulses, channel_state[n].indices.signalType,
                        channel_state[n].indices.quantOffsetType, channel_state[n].frame_length);
               }
            }
         }
      }
   }
   if (decControl->nChannelsInternal == 2) {
      if (lostFlag == FLAG_DECODE_NORMAL ||
            (lostFlag == FLAG_DECODE_LBRR && channel_state[0].LBRR_flags[channel_state[0].nFramesDecoded] == 1)) {
         silk_stereo_decode_pred(dec, MS_pred_Q13);
         if ((lostFlag == FLAG_DECODE_NORMAL && channel_state[1].VAD_flags[channel_state[0].nFramesDecoded] == 0) ||
               (lostFlag == FLAG_DECODE_LBRR && channel_state[1].LBRR_flags[channel_state[0].nFramesDecoded] == 0))
            decode_only_middle = ec_dec_icdf(dec, fl_stereo_only_code_mid_iCDF, 8);
         else decode_only_middle = 0;
      } else {
         for (n = 0; n < 2; n++) MS_pred_Q13[n] = psDec->sStereo.pred_prev_Q13[n];
      }
   }
   if (decControl->nChannelsInternal == 2 && decode_only_middle == 0 && psDec->prev_decode_only_middle == 1) {
      memset(psDec->channel_state[1].outBuf, 0, sizeof(psDec->channel_state[1].outBuf));
      memset(psDec->channel_state[1].sLPC_Q14_buf, 0, sizeof(psDec->channel_state[1].sLPC_Q14_buf));
      psDec->channel_state[1].lagPrev = 100;
      psDec->channel_state[1].LastGainIndex = 10;
      psDec->channel_state[1].prevSignalType = TYPE_NO_VOICE_ACTIVITY;
      psDec->channel_state[1].first_frame_after_reset = 1;
   }
   samplesOut1_tmp[0] = samplesOut1_tmp_storage;
   samplesOut1_tmp[1] = samplesOut1_tmp_storage+channel_state[0].frame_length+2;
   if (lostFlag == FLAG_DECODE_NORMAL) has_side = !decode_only_middle;
   else has_side = !psDec->prev_decode_only_middle || (decControl->nChannelsInternal == 2 && lostFlag == FLAG_DECODE_LBRR &&
         channel_state[1].LBRR_flags[channel_state[1].nFramesDecoded] == 1);
   for (n = 0; n < decControl->nChannelsInternal; n++) {
      if (n == 0 || has_side) {
         int FrameIndex, condCoding;
         FrameIndex = channel_state[0].nFramesDecoded-n;
         if (FrameIndex <= 0) condCoding = CODE_INDEPENDENTLY;
         else if (lostFlag == FLAG_DECODE_LBRR) condCoding = channel_state[n].LBRR_flags[FrameIndex-1] ? CODE_CONDITIONALLY : CODE_INDEPENDENTLY;
         else if (n > 0 && psDec->prev_decode_only_middle) condCoding = CODE_INDEPENDENTLY_NO_LTP_SCALING;
         else condCoding = CODE_CONDITIONALLY;
         ret += silk_decode_frame(&channel_state[n], dec, &samplesOut1_tmp[n][2], &nSamplesOutDec, lostFlag, condCoding);
      } else {
         memset(&samplesOut1_tmp[n][2], 0, nSamplesOutDec*sizeof(fl_i16));
      }
      channel_state[n].nFramesDecoded++;
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 2) {
      silk_stereo_MS_to_LR(&psDec->sStereo, samplesOut1_tmp[0], samplesOut1_tmp[1], MS_pred_Q13, channel_state[0].fs_kHz, nSamplesOutDec);
   } else {
      memcpy(samplesOut1_tmp[0], psDec->sStereo.sMid, 2*sizeof(fl_i16));
      memcpy(psDec->sStereo.sMid, &samplesOut1_tmp[0][nSamplesOutDec], 2*sizeof(fl_i16));
   }
   *nSamplesOut = silk_DIV32(nSamplesOutDec*decControl->API_sampleRate, silk_SMULBB(channel_state[0].fs_kHz, 1000));
   resample_out_ptr = samplesOut2_tmp;
   for (n = 0; n < silk_min(decControl->nChannelsAPI, decControl->nChannelsInternal); n++) {
      ret += silk_resampler(&channel_state[n].resampler_state, resample_out_ptr, &samplesOut1_tmp[n][1], nSamplesOutDec);
      if (decControl->nChannelsAPI == 2) {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[n+2*i] = resample_out_ptr[i]*(1/32768.f);
      } else {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[i] = resample_out_ptr[i]*(1/32768.f);
      }
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 1) {
      if (stereo_to_mono) {
         ret += silk_resampler(&channel_state[1].resampler_state, resample_out_ptr, &samplesOut1_tmp[0][1], nSamplesOutDec);
         for (i = 0; i < *nSamplesOut; i++) samplesOut[1+2*i] = resample_out_ptr[i]*(1/32768.f);
      } else {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[1+2*i] = samplesOut[0+2*i];
      }
   }
   if (channel_state[0].prevSignalType == TYPE_VOICED) {
      static const int mult_tab[3] = {6, 4, 3};
      decControl->prevPitchLag = channel_state[0].lagPrev*mult_tab[(channel_state[0].fs_kHz-8)>>2];
   } else decControl->prevPitchLag = 0;
   if (lostFlag == FLAG_PACKET_LOST) {
      for (i = 0; i < psDec->nChannelsInternal; i++) psDec->channel_state[i].LastGainIndex = 10;
   } else psDec->prev_decode_only_middle = decode_only_middle;
   return ret;
}

/* ======== opus_dec.c ======== */
/* ---- Opus decoder (SILK/CELT/hybrid glue, RFC 6716 section 4.5) ---- */
#define MODE_SILK_ONLY 1000
#define MODE_HYBRID 1001
#define MODE_CELT_ONLY 1002
#define FL_BANDWIDTH_NARROWBAND 1101
#define FL_BANDWIDTH_MEDIUMBAND 1102
#define FL_BANDWIDTH_WIDEBAND 1103
#define FL_BANDWIDTH_SUPERWIDEBAND 1104
#define FL_BANDWIDTH_FULLBAND 1105

struct fl_opus_decoder {
   int channels;
   fl_i32 Fs;
   int decode_gain;
   silk_dec_control DecControl;
   int stream_channels, bandwidth, mode, prev_mode, frame_size, prev_redundancy, last_packet_duration;
   fl_u32 rangeFinal;
   silk_decoder silk;
   fl_celt_dec celt;
};

static int fl_packet_get_samples_per_frame(const unsigned char *data, fl_i32 Fs) {
   int audiosize;
   if (data[0]&0x80) {
      audiosize = ((data[0]>>3)&0x3);
      audiosize = (Fs<<audiosize)/400;
   } else if ((data[0]&0x60) == 0x60) {
      audiosize = (data[0]&0x08) ? Fs/50 : Fs/100;
   } else {
      audiosize = ((data[0]>>3)&0x3);
      if (audiosize == 3) audiosize = Fs*60/1000;
      else audiosize = (Fs<<audiosize)/100;
   }
   return audiosize;
}
static int fl_packet_get_mode(const unsigned char *data) {
   if (data[0]&0x80) return MODE_CELT_ONLY;
   if ((data[0]&0x60) == 0x60) return MODE_HYBRID;
   return MODE_SILK_ONLY;
}
static int fl_packet_get_bandwidth(const unsigned char *data) {
   int bandwidth;
   if (data[0]&0x80) {
      bandwidth = FL_BANDWIDTH_MEDIUMBAND+((data[0]>>5)&0x3);
      if (bandwidth == FL_BANDWIDTH_MEDIUMBAND) bandwidth = FL_BANDWIDTH_NARROWBAND;
   } else if ((data[0]&0x60) == 0x60) {
      bandwidth = (data[0]&0x10) ? FL_BANDWIDTH_FULLBAND : FL_BANDWIDTH_SUPERWIDEBAND;
   } else {
      bandwidth = FL_BANDWIDTH_NARROWBAND+((data[0]>>5)&0x3);
   }
   return bandwidth;
}
static int fl_parse_size(const unsigned char *data, fl_i32 len, fl_i16 *size) {
   if (len < 1) { *size = -1; return -1; }
   if (data[0] < 252) { *size = data[0]; return 1; }
   if (len < 2) { *size = -1; return -1; }
   *size = 4*data[1]+data[0];
   return 2;
}
/* Splits a packet into frames. self_delimited: multistream framing (RFC 6716 appendix B).
   packet_offset receives the total packet length including padding. Returns the frame count or <0. */
static int fl_packet_parse(const unsigned char *data, fl_i32 len, int self_delimited, fl_i16 size[48], int *payload_offset, fl_i32 *packet_offset) {
   int i, bytes, count, cbr, framesize;
   unsigned char ch, toc;
   fl_i32 last_size, pad = 0;
   const unsigned char *data0 = data;
   if (len <= 0) return -4;
   framesize = fl_packet_get_samples_per_frame(data, 48000);
   cbr = 0;
   toc = *data++;
   len--;
   last_size = len;
   switch (toc&0x3) {
   case 0:
      count = 1;
      break;
   case 1:
      count = 2;
      cbr = 1;
      if (!self_delimited) {
         if (len&0x1) return -4;
         last_size = len/2;
         size[0] = (fl_i16)last_size;
      }
      break;
   case 2:
      count = 2;
      bytes = fl_parse_size(data, len, size);
      len -= bytes;
      if (size[0] < 0 || size[0] > len) return -4;
      data += bytes;
      last_size = len-size[0];
      break;
   default:
      if (len < 1) return -4;
      ch = *data++;
      count = ch&0x3F;
      if (count <= 0 || framesize*(fl_i32)count > 5760) return -4;
      len--;
      if (ch&0x40) {
         int p;
         do {
            int tmp;
            if (len <= 0) return -4;
            p = *data++;
            len--;
            tmp = p == 255 ? 254 : p;
            len -= tmp;
            pad += tmp;
         } while (p == 255);
      }
      if (len < 0) return -4;
      cbr = !(ch&0x80);
      if (!cbr) {
         last_size = len;
         for (i = 0; i < count-1; i++) {
            bytes = fl_parse_size(data, len, size+i);
            len -= bytes;
            if (size[i] < 0 || size[i] > len) return -4;
            data += bytes;
            last_size -= bytes+size[i];
         }
         if (last_size < 0) return -4;
      } else if (!self_delimited) {
         last_size = len/count;
         if (last_size*count != len) return -4;
         for (i = 0; i < count-1; i++) size[i] = (fl_i16)last_size;
      }
      break;
   }
   if (self_delimited) {
      bytes = fl_parse_size(data, len, size+count-1);
      len -= bytes;
      if (size[count-1] < 0 || size[count-1] > len) return -4;
      data += bytes;
      if (cbr) {
         if (size[count-1]*count > len) return -4;
         for (i = 0; i < count-1; i++) size[i] = size[count-1];
      } else if (bytes+size[count-1] > last_size) return -4;
   } else {
      if (last_size > 1275) return -4;
      size[count-1] = (fl_i16)last_size;
   }
   if (payload_offset) *payload_offset = (int)(data-data0);
   for (i = 0; i < count; i++) data += size[i];
   if (packet_offset) *packet_offset = pad+(fl_i32)(data-data0);
   return count;
}

static void fl_smooth_fade(const float *in1, const float *in2, float *out, int overlap, int channels, fl_i32 Fs) {
   int i, c, inc = 48000/Fs;
   for (c = 0; c < channels; c++) {
      for (i = 0; i < overlap; i++) {
         float w = fl_window120[i*inc]*fl_window120[i*inc];
         out[i*channels+c] = w*in2[i*channels+c]+(1.0f-w)*in1[i*channels+c];
      }
   }
}

static void fl_opus_decoder_reset(struct fl_opus_decoder *st) {
   st->stream_channels = st->channels;
   st->bandwidth = st->mode = st->prev_mode = st->prev_redundancy = st->last_packet_duration = 0;
   st->rangeFinal = 0;
   fl_celt_dec_reset(&st->celt);
   silk_dec_reset_all(&st->silk);
   st->frame_size = st->Fs/400;
}

static int fl_opus_decoder_init_state(struct fl_opus_decoder *st, fl_i32 Fs, int channels) {
   if ((Fs != 48000 && Fs != 24000 && Fs != 16000 && Fs != 12000 && Fs != 8000) || (channels != 1 && channels != 2))
      return -1;
   memset(st, 0, sizeof(*st));
   st->stream_channels = st->channels = channels;
   st->Fs = Fs;
   st->DecControl.API_sampleRate = Fs;
   st->DecControl.nChannelsAPI = channels;
   silk_dec_init_all(&st->silk);
   fl_celt_dec_init(&st->celt, Fs, channels);
   st->prev_mode = 0;
   st->frame_size = Fs/400;
   return 0;
}

static int fl_decode_frame(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size, int decode_fec) {
   int i, silk_ret = 0, celt_ret = 0, audiosize, mode, bandwidth, transition = 0, start_band, redundancy = 0;
   int redundancy_bytes = 0, celt_to_silk = 0, c, F2_5, F5, F10, F20, celt_accum;
   fl_i32 silk_frame_size;
   ec_ctx dec;
   float pcm_transition_buf[960*2], *pcm_transition = NULL, redundant_audio[480];
   fl_u32 redundant_rng = 0;
   fl_celt_dec *celt = &st->celt;
   F20 = st->Fs/50;
   F10 = F20>>1;
   F5 = F10>>1;
   F2_5 = F5>>1;
   if (frame_size < F2_5) return -2;
   frame_size = IMIN(frame_size, st->Fs/25*3);
   if (len <= 1) {
      data = NULL;
      frame_size = IMIN(frame_size, st->frame_size);
   }
   if (data != NULL) {
      audiosize = st->frame_size;
      mode = st->mode;
      bandwidth = st->bandwidth;
      ec_dec_init(&dec, (unsigned char*)data, len);
   } else {
      audiosize = frame_size;
      mode = st->prev_redundancy ? MODE_CELT_ONLY : st->prev_mode;
      bandwidth = 0;
      if (mode == 0) {
         for (i = 0; i < audiosize*st->channels; i++) pcm[i] = 0;
         return audiosize;
      }
      if (audiosize > F20) {
         do {
            int ret = fl_decode_frame(st, NULL, 0, pcm, IMIN(audiosize, F20), 0);
            if (ret < 0) return ret;
            pcm += ret*st->channels;
            audiosize -= ret;
         } while (audiosize > 0);
         return frame_size;
      } else if (audiosize < F20) {
         if (audiosize > F10) audiosize = F10;
         else if (mode != MODE_SILK_ONLY && audiosize > F5 && audiosize < F10) audiosize = F5;
      }
   }
   celt_accum = (mode != MODE_CELT_ONLY);
   if (data != NULL && st->prev_mode > 0 && ((mode == MODE_CELT_ONLY && st->prev_mode != MODE_CELT_ONLY && !st->prev_redundancy)
         || (mode != MODE_CELT_ONLY && st->prev_mode == MODE_CELT_ONLY))) {
      transition = 1;
   }
   if (transition && mode == MODE_CELT_ONLY) {
      pcm_transition = pcm_transition_buf;
      fl_decode_frame(st, NULL, 0, pcm_transition, IMIN(F5, audiosize), 0);
   }
   if (audiosize > frame_size) return -1;
   frame_size = audiosize;
   if (mode != MODE_CELT_ONLY) {
      int lost_flag, decoded_samples, pcm_too_small;
      float *pcm_ptr, pcm_silk[960*2];
      pcm_too_small = (frame_size < F10);
      pcm_ptr = pcm_too_small ? pcm_silk : pcm;
      if (st->prev_mode == MODE_CELT_ONLY) silk_dec_reset_all(&st->silk);
      st->DecControl.payloadSize_ms = IMAX(10, 1000*audiosize/st->Fs);
      if (data != NULL) {
         st->DecControl.nChannelsInternal = st->stream_channels;
         if (mode == MODE_SILK_ONLY) {
            if (bandwidth == FL_BANDWIDTH_NARROWBAND) st->DecControl.internalSampleRate = 8000;
            else if (bandwidth == FL_BANDWIDTH_MEDIUMBAND) st->DecControl.internalSampleRate = 12000;
            else st->DecControl.internalSampleRate = 16000;
         } else st->DecControl.internalSampleRate = 16000;
      }
      lost_flag = data == NULL ? 1 : 2*!!decode_fec;
      decoded_samples = 0;
      do {
         int first_frame = decoded_samples == 0;
         silk_ret = silk_Decode(&st->silk, &st->DecControl, lost_flag, first_frame, &dec, pcm_ptr, &silk_frame_size);
         if (silk_ret) {
            if (lost_flag) {
               silk_frame_size = frame_size;
               for (i = 0; i < frame_size*st->channels; i++) pcm_ptr[i] = 0;
            } else return -3;
         }
         pcm_ptr += silk_frame_size*st->channels;
         decoded_samples += silk_frame_size;
      } while (decoded_samples < frame_size);
      if (pcm_too_small) FL_COPY(pcm, pcm_silk, frame_size*st->channels);
   }
   start_band = 0;
   if (!decode_fec && mode != MODE_CELT_ONLY && data != NULL && ec_tell(&dec)+17+20*(mode == MODE_HYBRID) <= 8*len) {
      if (mode == MODE_HYBRID) redundancy = ec_dec_bit_logp(&dec, 12);
      else redundancy = 1;
      if (redundancy) {
         celt_to_silk = ec_dec_bit_logp(&dec, 1);
         redundancy_bytes = mode == MODE_HYBRID ? (fl_i32)ec_dec_uint(&dec, 256)+2 : len-((ec_tell(&dec)+7)>>3);
         len -= redundancy_bytes;
         if (len*8 < ec_tell(&dec)) {
            len = 0;
            redundancy_bytes = 0;
            redundancy = 0;
         }
         dec.storage -= redundancy_bytes;
      }
   }
   if (mode != MODE_CELT_ONLY) start_band = 17;
   if (redundancy) transition = 0;
   if (transition && mode != MODE_CELT_ONLY) {
      pcm_transition = pcm_transition_buf;
      fl_decode_frame(st, NULL, 0, pcm_transition, IMIN(F5, audiosize), 0);
   }
   if (bandwidth) {
      int endband = 21;
      switch (bandwidth) {
      case FL_BANDWIDTH_NARROWBAND: endband = 13; break;
      case FL_BANDWIDTH_MEDIUMBAND:
      case FL_BANDWIDTH_WIDEBAND: endband = 17; break;
      case FL_BANDWIDTH_SUPERWIDEBAND: endband = 19; break;
      case FL_BANDWIDTH_FULLBAND: endband = 21; break;
      }
      celt->end = endband;
   }
   celt->stream_channels = st->stream_channels;
   if (redundancy && celt_to_silk) {
      celt->start = 0;
      fl_celt_decode(celt, data+len, redundancy_bytes, redundant_audio, F5, NULL, 0);
      redundant_rng = celt->rng;
   }
   celt->start = start_band;
   if (mode != MODE_SILK_ONLY) {
      int celt_frame_size = IMIN(F20, frame_size);
      if (mode != st->prev_mode && st->prev_mode > 0 && !st->prev_redundancy) fl_celt_dec_reset(celt);
      celt_ret = fl_celt_decode(celt, decode_fec ? NULL : data, len, pcm, celt_frame_size, &dec, celt_accum);
      st->rangeFinal = celt->rng;
   } else {
      unsigned char silence[2] = {0xFF, 0xFF};
      if (!celt_accum)
         for (i = 0; i < frame_size*st->channels; i++) pcm[i] = 0;
      if (st->prev_mode == MODE_HYBRID && !(redundancy && celt_to_silk && st->prev_redundancy)) {
         celt->start = 0;
         fl_celt_decode(celt, silence, 2, pcm, F2_5, NULL, celt_accum);
      }
      st->rangeFinal = dec.rng;
   }
   if (redundancy && !celt_to_silk) {
      fl_celt_dec_reset(celt);
      celt->start = 0;
      fl_celt_decode(celt, data+len, redundancy_bytes, redundant_audio, F5, NULL, 0);
      redundant_rng = celt->rng;
      fl_smooth_fade(pcm+st->channels*(frame_size-F2_5), redundant_audio+st->channels*F2_5,
            pcm+st->channels*(frame_size-F2_5), F2_5, st->channels, st->Fs);
   }
   if (redundancy && celt_to_silk && (st->prev_mode != MODE_SILK_ONLY || st->prev_redundancy)) {
      for (c = 0; c < st->channels; c++)
         for (i = 0; i < F2_5; i++) pcm[st->channels*i+c] = redundant_audio[st->channels*i+c];
      fl_smooth_fade(redundant_audio+st->channels*F2_5, pcm+st->channels*F2_5, pcm+st->channels*F2_5, F2_5, st->channels, st->Fs);
   }
   if (transition) {
      if (audiosize >= F5) {
         for (i = 0; i < st->channels*F2_5; i++) pcm[i] = pcm_transition[i];
         fl_smooth_fade(pcm_transition+st->channels*F2_5, pcm+st->channels*F2_5, pcm+st->channels*F2_5, F2_5, st->channels, st->Fs);
      } else {
         fl_smooth_fade(pcm_transition, pcm, pcm, F2_5, st->channels, st->Fs);
      }
   }
   if (st->decode_gain) {
      float gain = fl_celt_exp2(6.48814081e-4f*st->decode_gain);
      for (i = 0; i < frame_size*st->channels; i++) pcm[i] = pcm[i]*gain;
   }
   if (len <= 1) st->rangeFinal = 0;
   else st->rangeFinal ^= redundant_rng;
   st->prev_mode = mode;
   st->prev_redundancy = redundancy && !celt_to_silk;
   return celt_ret < 0 ? celt_ret : audiosize;
}

/* Decodes one packet (NULL for loss) into interleaved float. Returns samples per channel. */
static int fl_decode_native(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size, int self_delimited, fl_i32 *packet_offset) {
   int i, nb_samples, count, offset, packet_frame_size, packet_bandwidth, packet_mode, packet_stream_channels;
   fl_i16 size[48];
   if (frame_size <= 0) return -1;
   if (len == 0 || data == NULL) {
      int pcm_count = 0;
      if (frame_size%(st->Fs/400) != 0) return -1;
      do {
         int ret = fl_decode_frame(st, NULL, 0, pcm+pcm_count*st->channels, frame_size-pcm_count, 0);
         if (ret < 0) return ret;
         pcm_count += ret;
      } while (pcm_count < frame_size);
      st->last_packet_duration = pcm_count;
      return pcm_count;
   } else if (len < 0) return -1;
   packet_mode = fl_packet_get_mode(data);
   packet_bandwidth = fl_packet_get_bandwidth(data);
   packet_frame_size = fl_packet_get_samples_per_frame(data, st->Fs);
   packet_stream_channels = (data[0]&0x4) ? 2 : 1;
   count = fl_packet_parse(data, len, self_delimited, size, &offset, packet_offset);
   if (count < 0) return count;
   data += offset;
   if (count*packet_frame_size > frame_size) return -2;
   st->mode = packet_mode;
   st->bandwidth = packet_bandwidth;
   st->frame_size = packet_frame_size;
   st->stream_channels = packet_stream_channels;
   nb_samples = 0;
   for (i = 0; i < count; i++) {
      int ret = fl_decode_frame(st, data, size[i], pcm+nb_samples*st->channels, frame_size-nb_samples, 0);
      if (ret < 0) return ret;
      data += size[i];
      nb_samples += ret;
   }
   st->last_packet_duration = nb_samples;
   return nb_samples;
}

static int fl_opus_decode_float_internal(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size) {
   return fl_decode_native(st, data, len, pcm, frame_size, 0, NULL);
}

/* Soft clipping used by opusdec before 16-bit conversion. */
static void fl_pcm_soft_clip(float *_x, int N, int C, float *declip_mem) {
   int c, i;
   float *x;
   if (C < 1 || N < 1 || !_x || !declip_mem) return;
   for (i = 0; i < N*C; i++) {
      float v = FL_MAX(-2.0f, _x[i]);
      _x[i] = FL_MIN(2.0f, v);
   }
   for (c = 0; c < C; c++) {
      float a, x0;
      int curr;
      x = _x+c;
      a = declip_mem[c];
      for (i = 0; i < N; i++) {
         if (x[i*C]*a >= 0) break;
         x[i*C] = x[i*C]+a*x[i*C]*x[i*C];
      }
      curr = 0;
      x0 = x[0];
      while (1) {
         int start, end, special = 0, peak_pos;
         float maxval;
         for (i = curr; i < N; i++)
            if (x[i*C] > 1 || x[i*C] < -1) break;
         if (i == N) {
            a = 0;
            break;
         }
         peak_pos = i;
         start = end = i;
         maxval = (float)fabs(x[i*C]);
         while (start > 0 && x[i*C]*x[(start-1)*C] >= 0) start--;
         while (end < N && x[i*C]*x[end*C] >= 0) {
            if ((float)fabs(x[end*C]) > maxval) {
               maxval = (float)fabs(x[end*C]);
               peak_pos = end;
            }
            end++;
         }
         special = (start == 0 && x[i*C]*x[0] >= 0);
         a = (maxval-1)/(maxval*maxval);
         a += a*2.4e-7f;
         if (x[i*C] > 0) a = -a;
         for (i = start; i < end; i++) x[i*C] = x[i*C]+a*x[i*C]*x[i*C];
         if (special && peak_pos >= 2) {
            float delta, offset = x0-x[0];
            delta = offset/peak_pos;
            for (i = curr; i < peak_pos; i++) {
               offset -= delta;
               x[i*C] += offset;
               x[i*C] = FL_MAX(-1.f, FL_MIN(1.f, x[i*C]));
            }
         }
         curr = end;
         if (curr == N) break;
      }
      declip_mem[c] = a;
   }
}

/* ---- Public low-level decoder API ---- */
FLDEF fl_opus_decoder *fl_opus_decoder_create(int sample_rate, int channels, int *error) {
   fl_opus_decoder *st = (fl_opus_decoder*)malloc(sizeof(*st));
   if (!st) {
      if (error) *error = FL_ERR_ALLOC;
      return NULL;
   }
   if (fl_opus_decoder_init_state(st, sample_rate, channels) < 0) {
      free(st);
      if (error) *error = FL_ERR_BAD_ARG;
      return NULL;
   }
   if (error) *error = FL_OK;
   return st;
}
FLDEF void fl_opus_decoder_destroy(fl_opus_decoder *dec) { free(dec); }
FLDEF void fl_opus_decoder_reset_state(fl_opus_decoder *dec) { fl_opus_decoder_reset(dec); }
FLDEF void fl_opus_decoder_set_gain(fl_opus_decoder *dec, int gain_q8) {
   dec->decode_gain = gain_q8 < -32768 ? -32768 : (gain_q8 > 32767 ? 32767 : gain_q8);
}
FLDEF int fl_opus_decode_float(fl_opus_decoder *dec, const unsigned char *packet, int len, float *pcm, int max_frame_size) {
   return fl_opus_decode_float_internal(dec, packet, len, pcm, max_frame_size);
}

/* ---- Multistream decoder (RFC 7845 channel mapping, libopus opus_multistream_decoder.c) ---- */
struct fl_opus_ms_decoder {
   int channels, streams, coupled;
   fl_i32 Fs;
   unsigned char mapping[255];
   struct fl_opus_decoder **dec;
   float *buf;
};

FLDEF void fl_opus_ms_decoder_destroy(fl_opus_ms_decoder *st) {
   int s;
   if (!st) return;
   if (st->dec)
      for (s = 0; s < st->streams; s++) free(st->dec[s]);
   free(st->dec);
   free(st->buf);
   free(st);
}

FLDEF fl_opus_ms_decoder *fl_opus_ms_decoder_create(int sample_rate, int channels, int streams, int coupled, const unsigned char *mapping, int *error) {
   fl_opus_ms_decoder *st;
   int c, s;
   if (error) *error = FL_ERR_BAD_ARG;
   if (channels < 1 || channels > 255 || streams < 1 || coupled < 0 || coupled > streams || streams+coupled > 255) return NULL;
   for (c = 0; c < channels; c++)
      if (mapping[c] >= streams+coupled && mapping[c] != 255) return NULL;
   st = (fl_opus_ms_decoder*)calloc(1, sizeof(*st));
   if (error) *error = FL_ERR_ALLOC;
   if (!st) return NULL;
   st->channels = channels;
   st->streams = streams;
   st->coupled = coupled;
   st->Fs = sample_rate;
   memcpy(st->mapping, mapping, channels);
   st->dec = (struct fl_opus_decoder**)calloc(streams, sizeof(*st->dec));
   st->buf = (float*)malloc(sizeof(float)*2*(sample_rate/25*3));
   if (!st->dec || !st->buf) {
      fl_opus_ms_decoder_destroy(st);
      return NULL;
   }
   for (s = 0; s < streams; s++) {
      st->dec[s] = (struct fl_opus_decoder*)malloc(sizeof(struct fl_opus_decoder));
      if (!st->dec[s]) {
         fl_opus_ms_decoder_destroy(st);
         return NULL;
      }
      if (fl_opus_decoder_init_state(st->dec[s], sample_rate, s < coupled ? 2 : 1) < 0) {
         fl_opus_ms_decoder_destroy(st);
         if (error) *error = FL_ERR_BAD_ARG;
         return NULL;
      }
   }
   if (error) *error = FL_OK;
   return st;
}

FLDEF void fl_opus_ms_decoder_reset_state(fl_opus_ms_decoder *st) {
   int s;
   for (s = 0; s < st->streams; s++) fl_opus_decoder_reset(st->dec[s]);
}

FLDEF void fl_opus_ms_decoder_set_gain(fl_opus_ms_decoder *st, int gain_q8) {
   int s;
   for (s = 0; s < st->streams; s++) fl_opus_decoder_set_gain(st->dec[s], gain_q8);
}

/* Checks that all streams parse and carry the same duration; returns it. */
static int fl_ms_packet_validate(const unsigned char *data, fl_i32 len, int streams, fl_i32 Fs) {
   int s, samples = 0;
   for (s = 0; s < streams; s++) {
      fl_i16 size[48];
      fl_i32 packet_offset;
      int count, tmp;
      if (len <= 0) return -4;
      count = fl_packet_parse(data, len, s != streams-1, size, NULL, &packet_offset);
      if (count < 0) return count;
      tmp = count*fl_packet_get_samples_per_frame(data, Fs);
      if (s != 0 && samples != tmp) return -4;
      samples = tmp;
      data += packet_offset;
      len -= packet_offset;
   }
   return samples;
}

FLDEF int fl_opus_ms_decode_float(fl_opus_ms_decoder *st, const unsigned char *data, int len, float *pcm, int frame_size) {
   int s, c, i, do_plc = len == 0 || data == NULL;
   if (frame_size <= 0 || len < 0) return FL_ERR_BAD_ARG;
   frame_size = FL_MIN(frame_size, st->Fs/25*3);
   if (!do_plc) {
      int ret;
      if (len < 2*st->streams-1) return FL_ERR_BAD_FILE;
      ret = fl_ms_packet_validate(data, len, st->streams, st->Fs);
      if (ret < 0) return FL_ERR_BAD_FILE;
      if (ret > frame_size) return FL_ERR_BAD_ARG;
   }
   for (s = 0; s < st->streams; s++) {
      fl_i32 packet_offset = 0;
      int ret, sc = s < st->coupled ? 2 : 1;
      if (!do_plc && len <= 0) return FL_ERR_BAD_FILE;
      ret = fl_decode_native(st->dec[s], do_plc ? NULL : data, do_plc ? 0 : len, st->buf, frame_size, s != st->streams-1, &packet_offset);
      if (!do_plc) {
         data += packet_offset;
         len -= packet_offset;
      }
      if (ret <= 0) return ret < 0 ? FL_ERR_BAD_FILE : ret;
      frame_size = ret;
      /* route this stream's channel(s) to every output channel mapped to it */
      for (c = 0; c < st->channels; c++) {
         int m = st->mapping[c], src = -1;
         if (s < st->coupled && m < 2*st->coupled && m>>1 == s) src = m&1;
         else if (s >= st->coupled && m == s+st->coupled) src = 0;
         if (src < 0) continue;
         for (i = 0; i < frame_size; i++) pcm[i*st->channels+c] = st->buf[i*sc+src];
      }
   }
   for (c = 0; c < st->channels; c++)
      if (st->mapping[c] == 255)
         for (i = 0; i < frame_size; i++) pcm[i*st->channels+c] = 0;
   return frame_size;
}

/* ======== io.c ======== */
/* ---- Audio pipeline: stream format, sinks, tags, WAV input/output, MD5, base64 ---- */

/* Reads a whole file into memory. */
static unsigned char *fl_read_file(const char *path, long *size) {
   FILE *f = fl_fopen(path, "rb");
   unsigned char *buf;
   long n;
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   n = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (unsigned char*)malloc(n > 0 ? n : 1);
   if (buf && fread(buf, 1, n, f) != (size_t)n) {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   *size = n;
   return buf;
}

static void fl_put_le32(unsigned char *p, fl_u32 v) { p[0] = v; p[1] = v>>8; p[2] = v>>16; p[3] = v>>24; }
static void fl_put_le16(unsigned char *p, int v) { p[0] = v; p[1] = v>>8; }
static fl_u32 fl_get_le32(const unsigned char *p) { return p[0]|(p[1]<<8)|(p[2]<<16)|((fl_u32)p[3]<<24); }
static int fl_get_le16(const unsigned char *p) { return p[0]|(p[1]<<8); }

/* ---- MD5 (RFC 1321) ---- */
typedef struct {
   fl_u32 h[4];
   fl_u64 len;
   unsigned char buf[64];
} fl_md5;

static void fl_md5_init(fl_md5 *m) {
   m->h[0] = 0x67452301;
   m->h[1] = 0xefcdab89;
   m->h[2] = 0x98badcfe;
   m->h[3] = 0x10325476;
   m->len = 0;
}

static void fl_md5_block(fl_md5 *m, const unsigned char *p) {
   static const fl_u32 K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
   static const unsigned char R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
   fl_u32 w[16], a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
   int i;
   for (i = 0; i < 16; i++) w[i] = fl_get_le32(p+4*i);
   for (i = 0; i < 64; i++) {
      fl_u32 f, t;
      int g;
      if (i < 16) { f = (b&c)|(~b&d); g = i; }
      else if (i < 32) { f = (d&b)|(~d&c); g = (5*i+1)&15; }
      else if (i < 48) { f = b^c^d; g = (3*i+5)&15; }
      else { f = c^(b|~d); g = (7*i)&15; }
      t = d;
      d = c;
      c = b;
      f += a+K[i]+w[g];
      b += (f<<R[(i>>4)*4+(i&3)])|(f>>(32-R[(i>>4)*4+(i&3)]));
      a = t;
   }
   m->h[0] += a;
   m->h[1] += b;
   m->h[2] += c;
   m->h[3] += d;
}

static void fl_md5_update(fl_md5 *m, const unsigned char *p, size_t n) {
   size_t have = (size_t)(m->len&63);
   m->len += n;
   if (have) {
      size_t take = 64-have < n ? 64-have : n;
      memcpy(m->buf+have, p, take);
      p += take;
      n -= take;
      if (have+take < 64) return;
      fl_md5_block(m, m->buf);
   }
   for (; n >= 64; n -= 64, p += 64) fl_md5_block(m, p);
   memcpy(m->buf, p, n);
}

static void fl_md5_final(fl_md5 *m, unsigned char out[16]) {
   unsigned char pad[72];
   fl_u64 bits = m->len*8;
   size_t padn = 64-(size_t)((m->len+8)&63);
   int i;
   memset(pad, 0, sizeof(pad));
   pad[0] = 0x80;
   fl_md5_update(m, pad, padn ? padn : 64);
   for (i = 0; i < 8; i++) pad[i] = (unsigned char)(bits>>(8*i));
   fl_md5_update(m, pad, 8);
   for (i = 0; i < 4; i++) fl_put_le32(out+4*i, m->h[i]);
}

/* Adds interleaved samples to the MD5 the way FLAC defines it (little-endian, (bits+7)/8 bytes). */
static void fl_md5_samples(fl_md5 *m, const fl_i32 *pcm, size_t n, int bits) {
   unsigned char tmp[4096];
   int bytes = (bits+7)/8;
   size_t i = 0;
   while (i < n) {
      size_t k, chunk = (n-i) < sizeof(tmp)/4 ? (n-i) : sizeof(tmp)/4;
      unsigned char *q = tmp;
      for (k = 0; k < chunk; k++) {
         fl_u32 v = (fl_u32)pcm[i+k];
         int b;
         for (b = 0; b < bytes; b++) *q++ = (unsigned char)(v>>(8*b));
      }
      fl_md5_update(m, tmp, (size_t)(q-tmp));
      i += chunk;
   }
}

/* ---- base64 (METADATA_BLOCK_PICTURE comments) ---- */
static char *fl_base64_encode(const unsigned char *p, int n) {
   static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   char *out = (char*)malloc((size_t)(n+2)/3*4+1), *q = out;
   int i;
   if (!out) return NULL;
   for (i = 0; i < n; i += 3) {
      fl_u32 v = (fl_u32)p[i]<<16 | (i+1 < n ? p[i+1]<<8 : 0) | (i+2 < n ? p[i+2] : 0);
      *q++ = tab[v>>18];
      *q++ = tab[(v>>12)&63];
      *q++ = i+1 < n ? tab[(v>>6)&63] : '=';
      *q++ = i+2 < n ? tab[v&63] : '=';
   }
   *q = 0;
   return out;
}

/* Returns the decoded length or -1. */
static int fl_base64_decode(const char *s, int n, unsigned char *out) {
   fl_u32 acc = 0;
   int bits = 0, len = 0, i;
   for (i = 0; i < n; i++) {
      int c = s[i], v;
      if (c >= 'A' && c <= 'Z') v = c-'A';
      else if (c >= 'a' && c <= 'z') v = c-'a'+26;
      else if (c >= '0' && c <= '9') v = c-'0'+52;
      else if (c == '+') v = 62;
      else if (c == '/') v = 63;
      else if (c == '=') break;
      else return -1;
      acc = acc<<6|v;
      bits += 6;
      if (bits >= 8) {
         bits -= 8;
         out[len++] = (unsigned char)(acc>>bits);
      }
   }
   return len;
}

/* ---- Stream description shared by all sources and sinks ---- */
typedef struct {
   int type;  /* FLAC metadata block type */
   int len;
   unsigned char *data;
} fl_block;

typedef struct {
   int channels, rate;
   int bits;             /* significant bits per sample; samples are right-justified */
   int container_bits;   /* sample size of the source file (opusenc "samplesize") */
   int is_float;         /* samples are IEEE float bit patterns (bits = 32) */
   fl_i64 frames;        /* samples per channel, -1 = unknown */
   int opus_family;      /* Opus source: mapping family for the opusdec WAV header, else -1 */
   fl_u32 channel_mask;  /* WAV channel mask, 0 = default for the channel count */
   char **tags;          /* Vorbis comments "NAME=value" (vendor not kept) */
   int ntags;
   fl_block *blocks;     /* other FLAC metadata blocks (APPLICATION, CUESHEET) */
   int nblocks;
} fl_format;

static void fl_format_init(fl_format *f) {
   memset(f, 0, sizeof(*f));
   f->frames = -1;
   f->opus_family = -1;
}

static void fl_format_free(fl_format *f) {
   int i;
   for (i = 0; i < f->ntags; i++) free(f->tags[i]);
   for (i = 0; i < f->nblocks; i++) free(f->blocks[i].data);
   free(f->tags);
   free(f->blocks);
   f->tags = NULL;
   f->blocks = NULL;
   f->ntags = f->nblocks = 0;
}

static int fl_format_add_tag(fl_format *f, const char *s, int len) {
   char **t = (char**)realloc(f->tags, sizeof(char*)*(f->ntags+1));
   if (!t) return -1;
   f->tags = t;
   if (!(t[f->ntags] = (char*)malloc((size_t)len+1))) return -1;
   memcpy(t[f->ntags], s, len);
   t[f->ntags++][len] = 0;
   return 0;
}

static int fl_format_add_block(fl_format *f, int type, const unsigned char *data, int len) {
   fl_block *b = (fl_block*)realloc(f->blocks, sizeof(fl_block)*(f->nblocks+1));
   if (!b) return -1;
   f->blocks = b;
   b += f->nblocks;
   b->type = type;
   b->len = len;
   if (!(b->data = (unsigned char*)malloc(len > 0 ? len : 1))) return -1;
   memcpy(b->data, data, len);
   f->nblocks++;
   return 0;
}

/* Case-insensitive match of a comment field name ("NAME=" prefix). */
static int fl_tag_is(const char *tag, const char *name) {
   size_t i, n = strlen(name);
   for (i = 0; i < n; i++) {
      int a = tag[i], b = name[i];
      if (a >= 'a' && a <= 'z') a -= 32;
      if (a != b) return 0;
   }
   return tag[n] == '=';
}

/* Parses a Vorbis comment block (vendor first) into f->tags. Returns 0 or -1. */
static int fl_parse_comments(fl_format *f, const unsigned char *p, long len) {
   long pos = 4, n, i;
   if (len < 8) return -1;
   pos += fl_get_le32(p);
   if (pos+4 > len || pos < 4) return -1;
   n = (long)fl_get_le32(p+pos);
   pos += 4;
   for (i = 0; i < n; i++) {
      fl_u32 l;
      if (pos+4 > len) return -1;
      l = fl_get_le32(p+pos);
      pos += 4;
      if (l > (fl_u32)(len-pos)) return -1;
      if (fl_format_add_tag(f, (const char*)p+pos, (int)l) < 0) return -1;
      pos += l;
   }
   return 0;
}

/* Sink: receives the decoded stream. Samples are interleaved in WAV/FLAC channel order. */
typedef struct fl_sink fl_sink;
struct fl_sink {
   int (*start)(fl_sink *s, const fl_format *f);
   int (*write)(fl_sink *s, const fl_i32 *pcm, int frames);
   int (*finish)(fl_sink *s);
   void (*destroy)(fl_sink *s);
   fl_i64 out_bytes, out_frames;  /* encoded output so far, for the ratio (0 = not reported) */
};

/* Default WAVE_FORMAT_EXTENSIBLE channel masks (FLAC channel order). */
static fl_u32 fl_default_mask(int channels) {
   static const fl_u32 masks[8] = {4, 3, 7, 0x33, 0x607, 0x60F, 0x70F, 0x63F};
   return channels >= 1 && channels <= 8 ? masks[channels-1] : 0;
}

/* ---- WAV input ---- */
static int fl_run_wav(const char *path, fl_sink *sink) {
   unsigned char hdr[12], ck[8], fmt[40], *buf = NULL;
   fl_i32 *pcm = NULL;
   int have_fmt = 0, ret, bytes = 0, C = 0, shift = 0, unsigned8 = 0;
   fl_i64 left = -1;
   fl_format f;
   FILE *in = fl_fopen(path, "rb");
   fl_format_init(&f);
   if (!in) return FL_ERR_IO;
   ret = FL_ERR_BAD_FILE;
   if (fread(hdr, 1, 12, in) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr+8, "WAVE", 4) != 0) goto done;
   for (;;) {
      fl_u32 len;
      if (fread(ck, 1, 8, in) != 8) goto done;
      len = fl_get_le32(ck+4);
      if (memcmp(ck, "fmt ", 4) == 0) {
         int format, n = len < 40 ? (int)len : 40;
         if (len < 16) goto done;
         memset(fmt, 0, sizeof(fmt));
         if (fread(fmt, 1, n, in) != (size_t)n) goto done;
         if (len > 40) fseek(in, len-40, SEEK_CUR);
         if (len&1) fseek(in, 1, SEEK_CUR);
         format = fl_get_le16(fmt);
         C = fl_get_le16(fmt+2);
         f.rate = (int)fl_get_le32(fmt+4);
         f.container_bits = f.bits = fl_get_le16(fmt+14);
         if (format == 0xFFFE && len >= 40) {
            int valid = fl_get_le16(fmt+18);
            format = fl_get_le16(fmt+24);
            f.channel_mask = fl_get_le32(fmt+20);
            if (valid > 0 && valid < f.bits) f.bits = valid;
         }
         if (format == 3 && f.container_bits == 32) f.is_float = 1;
         else if (format != 1 || (f.container_bits != 8 && f.container_bits != 16 && f.container_bits != 24 && f.container_bits != 32)) {
            ret = FL_ERR_UNSUPPORTED;
            goto done;
         }
         if (f.is_float) f.bits = 32;
         unsigned8 = f.container_bits == 8;
         shift = f.container_bits-f.bits;
         bytes = f.container_bits/8;
         have_fmt = 1;
      } else if (memcmp(ck, "data", 4) == 0) {
         if (!have_fmt) goto done;
         if (len != 0xFFFFFFFFu && len != 0) left = len/((fl_u32)bytes*C);
         break;
      } else {
         fseek(in, len+(len&1), SEEK_CUR);
      }
   }
   if (C < 1 || C > 255 || f.rate <= 0) {
      ret = FL_ERR_UNSUPPORTED;
      goto done;
   }
   if (f.channel_mask == fl_default_mask(C)) f.channel_mask = 0;
   f.channels = C;
   f.frames = left;
   buf = (unsigned char*)malloc((size_t)4096*bytes*C);
   pcm = (fl_i32*)malloc(sizeof(fl_i32)*4096*C);
   if (!buf || !pcm) {
      ret = FL_ERR_ALLOC;
      goto done;
   }
   if ((ret = sink->start(sink, &f)) != FL_OK) goto done;
   for (;;) {
      int want = 4096, n, i;
      if (left >= 0 && want > left) want = (int)left;
      n = want > 0 ? (int)fread(buf, (size_t)bytes*C, want, in) : 0;
      for (i = 0; i < n*C; i++) {
         const unsigned char *p = buf+(size_t)i*bytes;
         fl_i32 v;
         if (bytes == 1) v = (int)p[0]-128;
         else if (bytes == 2) v = (fl_i16)(p[0]|(p[1]<<8));
         else if (bytes == 3) v = (fl_i32)(((fl_u32)p[0]<<8)|((fl_u32)p[1]<<16)|((fl_u32)p[2]<<24))>>8;
         else v = (fl_i32)fl_get_le32(p);
         if (shift) {
            if (v&((1<<shift)-1)) {
               ret = FL_ERR_BAD_FILE;  /* samples use more bits than the header says */
               goto done;
            }
            v >>= shift;
         }
         pcm[i] = v;
      }
      (void)unsigned8;
      if (n > 0 && (ret = sink->write(sink, pcm, n)) != FL_OK) goto done;
      if (left >= 0) left -= n;
      if (n < want || want == 0) break;
   }
   ret = sink->finish(sink);
done:
   fl_format_free(&f);
   free(buf);
   free(pcm);
   fclose(in);
   return ret;
}

/* ---- WAV output ----
   Opus sources get opusdec's header (16-bit, WAVE_FORMAT_EXTENSIBLE for family 1 with
   3..8 channels); other sources get flac's (extensible above 2 channels or 16 bits). */
typedef struct {
   fl_sink base;
   FILE *f;
   int C, rate, bits, container, ext, is_float, hdr_len;
   fl_u32 mask;
   fl_i64 written;
   unsigned char *buf;
   size_t cap;
} fl_wav_sink;

static int fl_wav_header(fl_wav_sink *w, fl_u64 data_bytes) {
   static const unsigned char pcm_guid[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
   unsigned char h[68];
   int bpf = w->container/8*w->C, fmt = w->ext ? 40 : 16, n = 28+fmt;
   fl_u32 db = data_bytes > 0xFFFFFFFFu-n ? 0xFFFFFFFFu-n : (fl_u32)data_bytes;
   memcpy(h, "RIFF", 4);
   fl_put_le32(h+4, db+n-8);
   memcpy(h+8, "WAVEfmt ", 8);
   fl_put_le32(h+16, fmt);
   fl_put_le16(h+20, w->ext ? 0xFFFE : w->is_float ? 3 : 1);
   fl_put_le16(h+22, w->C);
   fl_put_le32(h+24, w->rate);
   fl_put_le32(h+28, (fl_u32)bpf*w->rate);
   fl_put_le16(h+32, bpf);
   fl_put_le16(h+34, w->container);
   if (w->ext) {
      fl_put_le16(h+36, 22);
      fl_put_le16(h+38, w->bits);
      fl_put_le32(h+40, w->mask);
      memcpy(h+44, pcm_guid, 16);
   }
   memcpy(h+n-8, "data", 4);
   fl_put_le32(h+n-4, db);
   w->hdr_len = n;
   return fwrite(h, 1, n, w->f) == (size_t)n ? FL_OK : FL_ERR_IO;
}

static int fl_wav_start(fl_sink *s, const fl_format *f) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   static const fl_u32 opus_masks[8] = {4, 3, 7, 0x33, 0x37, 0x3F, 0x70F, 0x63F};
   w->C = f->channels;
   w->rate = f->rate;
   w->bits = f->bits;
   if (f->is_float) {
      w->container = 32;
      w->is_float = 1;
      return fl_wav_header(w, 0);
   }
   w->container = (f->bits+7)/8*8;
   if (f->opus_family >= 0) {
      w->ext = f->opus_family == 1 && w->C >= 3 && w->C <= 8;
      w->mask = w->ext ? opus_masks[w->C-1] : 0;
   } else {
      w->ext = w->C > 2 || w->bits > 16 || w->bits != w->container;
      w->mask = f->channel_mask ? f->channel_mask : fl_default_mask(w->C);
   }
   return fl_wav_header(w, 0);
}

static int fl_wav_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   int bytes = w->container/8, shift = w->container-w->bits, i, b;
   size_t need = (size_t)n*w->C*bytes;
   unsigned char *q;
   if (w->is_float) shift = 0;
   if (need > w->cap) {
      unsigned char *nb = (unsigned char*)realloc(w->buf, need);
      if (!nb) return FL_ERR_ALLOC;
      w->buf = nb;
      w->cap = need;
   }
   q = w->buf;
   for (i = 0; i < n*w->C; i++) {
      fl_u32 v = (fl_u32)pcm[i]<<shift;
      if (bytes == 1) *q++ = (unsigned char)(v+128);
      else for (b = 0; b < bytes; b++) *q++ = (unsigned char)(v>>(8*b));
   }
   if (fwrite(w->buf, 1, need, w->f) != need) return FL_ERR_IO;
   w->written += n;
   return FL_OK;
}

static int fl_wav_finish(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   fl_u64 data = (fl_u64)w->written*w->C*(w->container/8);
   int ret = FL_OK;
   if (data&1) ret = fputc(0, w->f) == EOF ? FL_ERR_IO : FL_OK;
   fseek(w->f, 0, SEEK_SET);
   if (ret == FL_OK) ret = fl_wav_header(w, data);
   if (fclose(w->f) != 0 && ret == FL_OK) ret = FL_ERR_IO;
   w->f = NULL;
   return ret;
}

static void fl_wav_destroy(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   if (w->f) fclose(w->f);
   free(w->buf);
   free(w);
}

static fl_sink *fl_wav_sink_create(const char *path, int *err) {
   fl_wav_sink *w = (fl_wav_sink*)calloc(1, sizeof(*w));
   if (!w) {
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   if (!(w->f = fl_fopen(path, "wb"))) {
      free(w);
      *err = FL_ERR_IO;
      return NULL;
   }
   w->base.start = fl_wav_start;
   w->base.write = fl_wav_write;
   w->base.finish = fl_wav_finish;
   w->base.destroy = fl_wav_destroy;
   return &w->base;
}

/* ---- Memory output (fl_read_audio) ---- */
typedef struct {
   fl_sink base;
   fl_audio *a;
   fl_i64 cap;
} fl_mem_sink;

static int fl_mem_start(fl_sink *s, const fl_format *f) {
   fl_mem_sink *m = (fl_mem_sink*)s;
   m->a->channels = f->channels;
   m->a->rate = f->rate;
   m->a->bits = f->bits;
   m->a->is_float = f->is_float;
   m->a->frames = 0;
   return FL_OK;
}

static int fl_mem_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_mem_sink *m = (fl_mem_sink*)s;
   fl_audio *a = m->a;
   if (a->frames+n > m->cap) {
      fl_i64 cap = m->cap ? 2*m->cap : 65536;
      int *ns;
      while (cap < a->frames+n) cap *= 2;
      ns = (int*)realloc(a->samples, (size_t)cap*a->channels*sizeof(int));
      if (!ns) return FL_ERR_ALLOC;
      a->samples = ns;
      m->cap = cap;
   }
   memcpy(a->samples+(size_t)a->frames*a->channels, pcm, (size_t)n*a->channels*sizeof(int));
   a->frames += n;
   return FL_OK;
}

static int fl_mem_finish(fl_sink *s) { (void)s; return FL_OK; }
static void fl_mem_destroy(fl_sink *s) { free(s); }

/* ======== file_dec.c ======== */
/* ---- Ogg demuxing, speex-style resampler, dither and WAV output (opusdec behaviour) ---- */

typedef struct {
   unsigned char *data;
   int len;
   fl_i64 granule;  /* granule of the page the packet ends on, -1 if none */
   int eos;
} fl_ogg_packet;

typedef struct {
   fl_ogg_packet *pk;
   int count, cap;
   unsigned char *store;
} fl_ogg_stream;

static void fl_ogg_free(fl_ogg_stream *s) {
   int i;
   for (i = 0; i < s->count; i++) free(s->pk[i].data);
   free(s->pk);
   memset(s, 0, sizeof(*s));
}

/* Collects all packets of the first logical stream. Returns 0 on success. */
static void fl_ogg_add_packet(fl_ogg_stream *s, unsigned char *data, int len) {
   if (s->count == s->cap) {
      s->cap = s->cap ? 2*s->cap : 256;
      s->pk = (fl_ogg_packet*)realloc(s->pk, s->cap*sizeof(*s->pk));
   }
   s->pk[s->count].data = data;
   s->pk[s->count].len = len;
   s->pk[s->count].granule = -1;
   s->pk[s->count].eos = 0;
   s->count++;
}

/* Splits an Ogg file into chained links. In each group of BOS pages the first stream
   whose first packet starts with magic is used (like opusfile); other streams are ignored. */
static int fl_ogg_parse(const unsigned char *buf, long size, const char *magic, fl_ogg_stream **links, int *nlinks) {
   int mlen = (int)strlen(magic);
   long pos = 0;
   fl_u32 serial = 0;
   int have_serial = 0, prev_bos = 0, n = 0;
   unsigned char *cur = NULL;
   int cur_len = 0;
   fl_ogg_stream *L = NULL, *s = NULL;
   while (pos+27 <= size) {
      int nseg, i, hdr, bos;
      long body;
      fl_i64 granule;
      fl_u32 ser;
      if (memcmp(buf+pos, "OggS", 4) != 0) {
         pos++;
         continue;
      }
      nseg = buf[pos+26];
      hdr = 27+nseg;
      if (pos+hdr > size) break;
      granule = 0;
      for (i = 7; i >= 0; i--) granule = (granule<<8)|buf[pos+6+i];
      ser = buf[pos+14]|(buf[pos+15]<<8)|(buf[pos+16]<<16)|((fl_u32)buf[pos+17]<<24);
      bos = (buf[pos+5]&2) != 0;
      body = pos+hdr;
      if (bos) {
         /* a new group of BOS pages starts a new link */
         if (!prev_bos) have_serial = 0;
         if (!have_serial && nseg > 0 && buf[pos+27] >= mlen && body+mlen <= size && memcmp(buf+body, magic, mlen) == 0) {
            fl_ogg_stream *t = (fl_ogg_stream*)realloc(L, (n+1)*sizeof(*L));
            if (!t) break;
            L = t;
            s = &L[n++];
            memset(s, 0, sizeof(*s));
            serial = ser;
            have_serial = 1;
            free(cur);
            cur = NULL;
            cur_len = 0;
         }
      }
      prev_bos = bos;
      if (!have_serial || ser != serial) {
         for (i = 0; i < nseg; i++) body += buf[pos+27+i];
         pos = body;
         continue;
      }
      for (i = 0; i < nseg; i++) {
         int l = buf[pos+27+i];
         unsigned char *t;
         if (body+l > size) {
            pos = size;
            break;
         }
         t = (unsigned char*)realloc(cur, cur_len+l+1);
         if (!t) break;
         cur = t;
         memcpy(cur+cur_len, buf+body, l);
         cur_len += l;
         body += l;
         if (l < 255) {
            fl_ogg_add_packet(s, cur, cur_len);
            cur = NULL;
            cur_len = 0;
         }
      }
      if (pos == size) break;
      /* The page granule belongs to the last packet completed on this page. */
      if (s->count > 0 && cur_len == 0) {
         s->pk[s->count-1].granule = granule;
         s->pk[s->count-1].eos = (buf[pos+5]&4) != 0;
      } else if (s->count > 0 && (buf[pos+5]&4)) {
         s->pk[s->count-1].eos = 1;
      }
      pos = body;
   }
   free(cur);
   *links = L;
   *nlinks = n;
   return n > 0 && L[0].count >= 1 ? 0 : -1;
}

typedef struct {
   int version, channels, pre_skip, gain, mapping_family, streams, coupled;
   fl_u32 input_sample_rate;
   unsigned char mapping[255];
} fl_opus_head;

/* Parses OpusHead. Families 0, 1, 2 and 255 are accepted (2 and 255 use plain routing). */
static int fl_parse_opus_head(const unsigned char *d, int len, fl_opus_head *h) {
   int c;
   if (len < 19 || memcmp(d, "OpusHead", 8) != 0) return -1;
   h->version = d[8];
   h->channels = d[9];
   h->pre_skip = d[10]|(d[11]<<8);
   h->input_sample_rate = d[12]|(d[13]<<8)|(d[14]<<16)|((fl_u32)d[15]<<24);
   h->gain = (fl_i16)(d[16]|(d[17]<<8));
   h->mapping_family = d[18];
   if (h->version > 15 || h->channels < 1) return -1;
   if (h->mapping_family == 0) {
      if (h->channels > 2) return -1;
      h->streams = 1;
      h->coupled = h->channels-1;
      h->mapping[0] = 0;
      h->mapping[1] = 1;
      return 0;
   }
   if (h->mapping_family != 1 && h->mapping_family != 2 && h->mapping_family != 255) return FL_ERR_UNSUPPORTED;
   if (h->mapping_family == 1 && h->channels > 8) return -1;
   if (len < 21+h->channels) return -1;
   h->streams = d[19];
   h->coupled = d[20];
   if (h->streams < 1 || h->coupled > h->streams || h->streams+h->coupled > 255) return -1;
   for (c = 0; c < h->channels; c++) {
      h->mapping[c] = d[21+c];
      if (h->mapping[c] >= h->streams+h->coupled && h->mapping[c] != 255) return -1;
   }
   return 0;
}

/* Duration of an Opus packet in 48 kHz samples, or <=0 if invalid. */
static int fl_packet_duration(const unsigned char *data, int len) {
   int count;
   if (len < 1) return -1;
   switch (data[0]&3) {
   case 0: count = 1; break;
   case 3: if (len < 2) return -1; count = data[1]&0x3F; break;
   default: count = 2;
   }
   return count*fl_packet_get_samples_per_frame(data, 48000);
}

/* ---- Speex resampler (quality 5, float, direct sinc table, SSE summation order) ---- */
static const double fl_kaiser10_table[36] = {
   0.99537781, 1.00000000, 0.99537781, 0.98162644, 0.95908712, 0.92831446,
   0.89005583, 0.84522401, 0.79486424, 0.74011713, 0.68217934, 0.62226347,
   0.56155915, 0.50119680, 0.44221549, 0.38553619, 0.33194107, 0.28205962,
   0.23636152, 0.19515633, 0.15859932, 0.12670280, 0.09935205, 0.07632451,
   0.05731132, 0.04193980, 0.02979584, 0.02044510, 0.01345224, 0.00839739,
   0.00488951, 0.00257636, 0.00115101, 0.00035515, 0.00000000, 0.00000000};

typedef struct {
   fl_u32 num_rate, den_rate, filt_len, mem_alloc_size;
   int int_advance, frac_advance, channels;
   float *sinc_table, *mem;
   fl_i32 last_sample[8];
   fl_u32 samp_frac_num[8];
} fl_resampler;

static double fl_rs_compute_func(float x, const double *table, int oversample) {
   float y, frac;
   double interp[4];
   int ind;
   y = x*oversample;
   ind = (int)floor(y);
   frac = (y-ind);
   interp[3] = -0.1666666667*frac+0.1666666667*(frac*frac*frac);
   interp[2] = frac+0.5*(frac*frac)-0.5*(frac*frac*frac);
   interp[0] = -0.3333333333*frac+0.5*(frac*frac)-0.1666666667*(frac*frac*frac);
   interp[1] = 1.f-interp[3]-interp[2]-interp[0];
   return interp[0]*table[ind]+interp[1]*table[ind+1]+interp[2]*table[ind+2]+interp[3]*table[ind+3];
}
static float fl_rs_sinc(float cutoff, float x, int N) {
   float xx = x*cutoff;
   if (fabs(x) < 1e-6) return cutoff;
   else if (fabs(x) > .5*N) return 0;
   return cutoff*sin(3.14159265358979323846*xx)/(3.14159265358979323846*xx)*fl_rs_compute_func(fabs(2.*x/N), fl_kaiser10_table, 32);
}
static fl_u32 fl_gcd(fl_u32 a, fl_u32 b) {
   while (b != 0) {
      fl_u32 t = a%b;
      a = b;
      b = t;
   }
   return a;
}
/* speex_resampler_init(channels, in, out, 5) followed by skip_zeros(). */
static int fl_resampler_init(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   fl_u32 fact = fl_gcd(in_rate, out_rate), i;
   float cutoff;
   int j;
   memset(r, 0, sizeof(*r));
   r->channels = channels;
   r->num_rate = in_rate/fact;
   r->den_rate = out_rate/fact;
   r->int_advance = r->num_rate/r->den_rate;
   r->frac_advance = r->num_rate%r->den_rate;
   r->filt_len = 80;
   if (r->num_rate > r->den_rate) {
      fl_u32 major = r->filt_len/r->den_rate, remain = r->filt_len%r->den_rate;
      cutoff = 0.922f*r->den_rate/r->num_rate;
      r->filt_len = remain*r->num_rate/r->den_rate+major*r->num_rate;
      r->filt_len = ((r->filt_len-1)&(~0x7))+8;
   } else cutoff = 0.940f;
   r->sinc_table = (float*)malloc(sizeof(float)*r->filt_len*r->den_rate);
   r->mem_alloc_size = r->filt_len-1+160;
   r->mem = (float*)calloc((size_t)channels*r->mem_alloc_size, sizeof(float));
   if (!r->sinc_table || !r->mem) return -1;
   for (i = 0; i < r->den_rate; i++)
      for (j = 0; j < (int)r->filt_len; j++)
         r->sinc_table[i*r->filt_len+j] = fl_rs_sinc(cutoff, ((j-(fl_i32)r->filt_len/2+1)-((float)i)/r->den_rate), r->filt_len);
   for (j = 0; j < channels; j++) r->last_sample[j] = r->filt_len/2;
   return 0;
}
static void fl_resampler_free(fl_resampler *r) {
   free(r->sinc_table);
   free(r->mem);
}
/* inner_product_single from resample_sse.h: 4 lanes over blocks of 8, then (l0+l2)+(l1+l3). */
static float fl_rs_inner(const float *a, const float *b, int len) {
   float s[4] = {0, 0, 0, 0};
   int i, k;
   for (i = 0; i < len; i += 8) {
      for (k = 0; k < 4; k++) s[k] = s[k]+a[i+k]*b[i+k];
      for (k = 0; k < 4; k++) s[k] = s[k]+a[i+4+k]*b[i+4+k];
   }
   return (s[0]+s[2])+(s[1]+s[3]);
}
static void fl_rs_process_native(fl_resampler *r, int ch, fl_u32 *in_len, float *out, fl_u32 *out_len) {
   const int N = r->filt_len;
   int out_sample = 0, j;
   float *mem = r->mem+ch*r->mem_alloc_size;
   fl_i32 last_sample = r->last_sample[ch];
   fl_u32 samp_frac_num = r->samp_frac_num[ch], ilen;
   while (!(last_sample >= (fl_i32)*in_len || out_sample >= (fl_i32)*out_len)) {
      out[r->channels*out_sample++] = fl_rs_inner(&r->sinc_table[samp_frac_num*N], &mem[last_sample], N);
      last_sample += r->int_advance;
      samp_frac_num += r->frac_advance;
      if (samp_frac_num >= r->den_rate) {
         samp_frac_num -= r->den_rate;
         last_sample++;
      }
   }
   r->last_sample[ch] = last_sample;
   r->samp_frac_num[ch] = samp_frac_num;
   if (r->last_sample[ch] < (fl_i32)*in_len) *in_len = r->last_sample[ch];
   *out_len = out_sample;
   r->last_sample[ch] -= *in_len;
   ilen = *in_len;
   for (j = 0; j < N-1; ++j) mem[j] = mem[j+ilen];
}
/* speex_resampler_process_interleaved_float(); in may be NULL for zeros. */
static void fl_resampler_process(fl_resampler *r, const float *in, fl_u32 *in_len, float *out, fl_u32 *out_len) {
   fl_u32 bak_in = *in_len, bak_out = *out_len;
   int ch, C = r->channels;
   for (ch = 0; ch < C; ch++) {
      fl_u32 ilen = bak_in, olen = bak_out, xlen = r->mem_alloc_size-(r->filt_len-1);
      float *x = r->mem+ch*r->mem_alloc_size, *o = out+ch;
      const float *ip = in ? in+ch : NULL;
      const int filt_offs = r->filt_len-1;
      while (ilen && olen) {
         fl_u32 j, ichunk = (ilen > xlen) ? xlen : ilen, ochunk = olen;
         for (j = 0; j < ichunk; ++j) x[j+filt_offs] = ip ? ip[j*C] : 0;
         fl_rs_process_native(r, ch, &ichunk, o, &ochunk);
         ilen -= ichunk;
         olen -= ochunk;
         o += ochunk*C;
         if (ip) ip += ichunk*C;
      }
      *in_len = bak_in-ilen;
      *out_len = bak_out-olen;
   }
}

/* ---- 16-bit conversion with shaped dither (opusdec shape_dither_toshort) ---- */
typedef struct {
   float b_buf[8*4], a_buf[8*4];
   int fs, mute;
   unsigned int rngseed;
} fl_shapestate;

static unsigned int fl_fast_rand(fl_shapestate *ss) {
   ss->rngseed = (ss->rngseed*96314165)+907633515;
   return ss->rngseed;
}
static void fl_shape_dither_toshort(fl_shapestate *ss, short *o, const float *in, int n, int CC) {
   static const float gains[3] = {32768.f-15.f, 32768.f-15.f, 32768.f-3.f};
   static const float fcoef[3][8] = {
      {2.2374f, -.7339f, -.1251f, -.6033f, 0.9030f, .0116f, -.5853f, -.2571f},
      {2.2061f, -.4706f, -.2534f, -.6214f, 1.0587f, .0676f, -.6054f, -.2738f},
      {1.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}};
   int i, rate = ss->fs == 44100 ? 1 : (ss->fs == 48000 ? 0 : 2);
   float gain = gains[rate], *b_buf = ss->b_buf, *a_buf = ss->a_buf;
   int mute = ss->mute;
   if (mute > 64) memset(a_buf, 0, sizeof(float)*CC*4);
   for (i = 0; i < n; i++) {
      int c, pos = i*CC, silent = 1;
      for (c = 0; c < CC; c++) {
         int j, si;
         float r, s, err = 0, r1, r2;
         silent &= in[pos+c] == 0;
         s = in[pos+c]*gain;
         for (j = 0; j < 4; j++) err += fcoef[rate][j]*b_buf[c*4+j]-fcoef[rate][j+4]*a_buf[c*4+j];
         memmove(&a_buf[c*4+1], &a_buf[c*4], sizeof(float)*3);
         memmove(&b_buf[c*4+1], &b_buf[c*4], sizeof(float)*3);
         a_buf[c*4] = err;
         s = s-err;
         r1 = (float)fl_fast_rand(ss)*(1/(float)UINT_MAX);
         r2 = (float)fl_fast_rand(ss)*(1/(float)UINT_MAX);
         r = r1-r2;
         if (mute > 16) r = 0;
         {
            float t = FL_MIN(s+r, 32767);
            o[pos+c] = si = fl_float2int(FL_MAX(-32768, t));
         }
         b_buf[c*4] = (mute > 16) ? 0 : FL_MAX(-1.5f, FL_MIN(si-s, 1.5f));
      }
      mute++;
      if (!silent) mute = 0;
   }
   ss->mute = FL_MIN(mute, 960);
}

/* Converts one chunk to 16-bit (soft clip + dither as in opusdec) and passes it on. */
static int fl_emit_chunk(fl_sink *sink, float *pcm, int n, int C, float *clipmem, fl_shapestate *ss, int dither, fl_i64 *written) {
   short *out;
   fl_i32 *v;
   int i, ret;
   if (n > 0) fl_pcm_soft_clip(pcm, n, C, clipmem);
   out = (short*)malloc(sizeof(short)*(n > 0 ? n*C : 1));
   v = (fl_i32*)malloc(sizeof(fl_i32)*(n > 0 ? n*C : 1));
   if (!out || !v) {
      free(out);
      free(v);
      return FL_ERR_ALLOC;
   }
   if (dither) fl_shape_dither_toshort(ss, out, pcm, n, C);
   else for (i = 0; i < n*C; i++) out[i] = (short)fl_float2int(FL_MAX(-32768, FL_MIN(pcm[i]*32768.f, 32767)));
   for (i = 0; i < n*C; i++) v[i] = out[i];
   ret = n > 0 ? sink->write(sink, v, n) : FL_OK;
   free(out);
   free(v);
   if (ret != FL_OK) return ret;
   *written += n;
   return FL_OK;
}

/* opusdec audio_write(): resample (if needed), soft clip, dither and write one decoded chunk. */
typedef struct {
   fl_sink *sink;
   int C, rate, dither;
   fl_resampler rs;
   int have_rs;
   float clipmem[8];
   fl_shapestate ss;
   fl_i64 link_read, link_out, written;
   float buf[1024*8];
} fl_writer;

static int fl_audio_write(fl_writer *w, float *pcm, int frame_size) {
   fl_i64 maxout;
   int C = w->C;
   maxout = ((w->link_read/48000)*w->rate+(w->link_read%48000)*w->rate/48000)-w->link_out;
   if (maxout < 0) maxout = 0;
   do {
      float *output;
      fl_u32 out_len;
      if (w->have_rs) {
         fl_u32 in_len = frame_size;
         output = w->buf;
         out_len = 1024 < maxout ? 1024 : (fl_u32)maxout;
         fl_resampler_process(&w->rs, pcm, &in_len, w->buf, &out_len);
         if (pcm) pcm += C*in_len;
         frame_size -= in_len;
      } else {
         output = pcm;
         out_len = frame_size < maxout ? (fl_u32)frame_size : (fl_u32)maxout;
         frame_size = 0;
      }
      if (maxout > 0) {
         fl_i64 before = w->written;
         int r = fl_emit_chunk(w->sink, output, out_len, C, w->clipmem, &w->ss, w->dither, &w->written);
         if (r != FL_OK) return r;
         w->link_out += w->written-before;
         maxout -= w->written-before;
      } else {
         /* dither state is still updated for empty chunks */
         if (w->dither) {
            short dummy[1];
            fl_shape_dither_toshort(&w->ss, dummy, output, 0, C);
         }
      }
   } while (frame_size > 0 && maxout > 0);
   return FL_OK;
}

/* Drains the resampler with filt_len/2 zeros (opusdec drain_resampler). */
static int fl_drain_resampler(fl_writer *w) {
   int drain = w->rs.filt_len/2;
   float zeros[100*8];
   memset(zeros, 0, sizeof(zeros));
   do {
      int tmp = FL_MIN(drain, 100);
      int r = fl_audio_write(w, zeros, tmp);
      if (r != FL_OK) return r;
      drain -= tmp;
   } while (drain > 0);
   return FL_OK;
}

/* opusfile stereo downmix matrices for 3..8 channels (Vorbis order). */
static const float fl_stereo_downmix[6][8][2] = {
   {{0.5858f, 0.0f}, {0.4142f, 0.4142f}, {0.0f, 0.5858f}},
   {{0.4226f, 0.0f}, {0.0f, 0.4226f}, {0.366f, 0.2114f}, {0.2114f, 0.336f}},
   {{0.651f, 0.0f}, {0.46f, 0.46f}, {0.0f, 0.651f}, {0.5636f, 0.3254f}, {0.3254f, 0.5636f}},
   {{0.529f, 0.0f}, {0.3741f, 0.3741f}, {0.0f, 0.529f}, {0.4582f, 0.2645f}, {0.2645f, 0.4582f}, {0.3741f, 0.3741f}},
   {{0.4553f, 0.0f}, {0.322f, 0.322f}, {0.0f, 0.4553f}, {0.3943f, 0.2277f}, {0.2277f, 0.3943f}, {0.2788f, 0.2788f}, {0.322f, 0.322f}},
   {{0.3886f, 0.0f}, {0.2748f, 0.2748f}, {0.0f, 0.3886f}, {0.3366f, 0.1943f}, {0.1943f, 0.3366f}, {0.3366f, 0.1943f}, {0.1943f, 0.3366f}, {0.2748f, 0.2748f}}};

/* In-place stereo downmix of n samples with C channels (opusfile op_stereo_filter). */
static void fl_downmix_stereo(float *pcm, int n, int C) {
   int i, c;
   if (C == 2) return;
   if (C == 1) {
      for (i = n-1; i >= 0; i--) pcm[2*i] = pcm[2*i+1] = pcm[i];
      return;
   }
   for (i = 0; i < n; i++) {
      float l = 0, r = 0;
      if (C <= 8) {
         for (c = 0; c < C; c++) {
            l += fl_stereo_downmix[C-3][c][0]*pcm[C*i+c];
            r += fl_stereo_downmix[C-3][c][1]*pcm[C*i+c];
         }
      } else {
         l = pcm[C*i];
         r = pcm[C*i+1];
      }
      pcm[2*i] = l;
      pcm[2*i+1] = r;
   }
}

/* Decodes an Ogg Opus file (all chained links) to 16-bit PCM like opusdec and passes it
   to sink. rate=0 keeps the original rate. */
static int fl_run_opus(const char *in_path, int rate, int dither, fl_sink *sink) {
   static const int wav_permute[8][8] = {
      {0}, {0, 1}, {0, 2, 1}, {0, 1, 2, 3}, {0, 2, 1, 3, 4}, {0, 2, 1, 4, 5, 3},
      {0, 2, 1, 5, 6, 4, 3}, {0, 2, 1, 6, 7, 4, 5, 3}};
   long size;
   unsigned char *file = fl_read_file(in_path, &size);
   fl_ogg_stream *links = NULL;
   int nlinks = 0, li, i, ret = FL_ERR_BAD_FILE, force_stereo = 0, C, maxC = 2, permute = 0;
   fl_opus_head *heads = NULL;
   fl_opus_ms_decoder *dec = NULL;
   fl_writer *w = NULL;
   float *pcm = NULL, *tmp = NULL;
   unsigned char channel_map[8];
   fl_format fmt;
   fl_format_init(&fmt);
   if (!file) return FL_ERR_IO;
   if (fl_ogg_parse(file, size, "OpusHead", &links, &nlinks) < 0) {
      free(file);
      for (li = 0; li < nlinks; li++) fl_ogg_free(&links[li]);
      free(links);
      return FL_ERR_BAD_FILE;
   }
   free(file);
   heads = (fl_opus_head*)malloc(sizeof(*heads)*nlinks);
   if (!heads) {
      ret = FL_ERR_ALLOC;
      goto done;
   }
   for (li = 0; li < nlinks; li++) {
      fl_ogg_stream *s = &links[li];
      int r = s->count >= 2 ? fl_parse_opus_head(s->pk[0].data, s->pk[0].len, &heads[li]) : -1;
      if (r < 0 || s->pk[1].len < 8 || memcmp(s->pk[1].data, "OpusTags", 8) != 0) {
         ret = r == FL_ERR_UNSUPPORTED ? r : FL_ERR_BAD_FILE;
         goto done;
      }
      if (heads[li].channels != heads[0].channels) force_stereo = 1;
      maxC = FL_MAX(maxC, heads[li].channels);
   }
   if (rate == 0) {
      rate = heads[0].input_sample_rate ? (int)heads[0].input_sample_rate : 48000;
      for (li = 1; li < nlinks; li++)
         if ((heads[li].input_sample_rate ? (int)heads[li].input_sample_rate : 48000) != rate) rate = 48000;
   }
   if (rate < 8000 || rate > 192000) rate = 48000;
   C = force_stereo ? 2 : heads[0].channels;
   if (C > 8) {
      ret = FL_ERR_UNSUPPORTED;  /* output state holds at most 8 channels */
      goto done;
   }
   w = (fl_writer*)calloc(1, sizeof(*w));
   pcm = (float*)malloc(sizeof(float)*5760*maxC);
   tmp = (float*)malloc(sizeof(float)*5760*C);
   if (!w || !pcm || !tmp) {
      ret = FL_ERR_ALLOC;
      goto done;
   }
   /* WAV channel order for family 1 (opusdec adjust_wav_mapping) */
   permute = heads[0].mapping_family == 1 && !force_stereo && (C == 3 || C > 4);
   if (permute)
      for (i = 0; i < C; i++) channel_map[wav_permute[C-1][i]] = (unsigned char)i;
   w->sink = sink;
   w->C = C;
   w->rate = rate;
   w->dither = dither;
   w->ss.fs = rate;
   w->ss.mute = 960;
   w->ss.rngseed = 22222;
   fmt.channels = C;
   fmt.rate = rate;
   fmt.bits = fmt.container_bits = 16;
   fmt.opus_family = force_stereo ? 0 : heads[0].mapping_family;
   {
      /* approximate length (for progress): last granule minus pre-skip of every link */
      fl_i64 total = 0;
      for (li = 0; li < nlinks; li++)
         for (i = links[li].count-1; i >= 2; i--)
            if (links[li].pk[i].granule >= 0) {
               total += FL_MAX(0, links[li].pk[i].granule-heads[li].pre_skip);
               break;
            }
      fmt.frames = total*rate/48000;
   }
   fl_parse_comments(&fmt, links[0].pk[1].data+8, links[0].pk[1].len-8);
   if ((ret = sink->start(sink, &fmt)) != FL_OK) goto done;
   for (li = 0; li < nlinks; li++) {
      fl_ogg_stream *s = &links[li];
      fl_opus_head *head = &heads[li];
      int cur_discard = head->pre_skip, HC = head->channels, started = 0;
      fl_i64 prev_gp = -1;
      fl_opus_ms_decoder_destroy(dec);
      dec = fl_opus_ms_decoder_create(48000, HC, head->streams, head->coupled, head->mapping, NULL);
      if (!dec) {
         ret = FL_ERR_BAD_FILE;
         goto done;
      }
      fl_opus_ms_decoder_set_gain(dec, head->gain);
      for (i = 2; i < s->count;) {
         /* Group the packets completed on one page (the last one carries the granule). */
         int first = i, last = i, k, total = 0, eos_trim = 0;
         fl_i64 page_gp, gp;
         while (last < s->count && s->pk[last].granule == -1) last++;
         if (last >= s->count) last = s->count-1;
         for (k = first; k <= last; k++) {
            int d = fl_packet_duration(s->pk[k].data, s->pk[k].len);
            total += d > 0 ? d : 0;
         }
         page_gp = s->pk[last].granule;
         if (prev_gp == -1) {
            /* first audio page; if it is also the last one and holds more audio than its
               granule position, the stream starts at 0 and the end is trimmed (opusfile) */
            prev_gp = page_gp-total;
            if (prev_gp < 0) {
               if (!s->pk[last].eos) {
                  ret = FL_ERR_BAD_FILE;
                  goto done;
               }
               prev_gp = 0;
            }
         }
         if (s->pk[last].eos && page_gp-prev_gp < total) eos_trim = 1;
         else prev_gp = page_gp-total;
         gp = prev_gp;
         for (k = first; k <= last; k++) {
            int dur = fl_packet_duration(s->pk[k].data, s->pk[k].len), trimmed, n, skip;
            float *out = pcm;
            if (dur <= 0) continue;
            n = fl_opus_ms_decode_float(dec, s->pk[k].data, s->pk[k].len, pcm, 5760);
            if (n < 0) {
               ret = FL_ERR_BAD_FILE;
               goto done;
            }
            trimmed = dur;
            if (eos_trim) {
               fl_i64 left = page_gp-gp;
               trimmed = left <= 0 ? 0 : (int)FL_MIN(left, dur);
            }
            gp += dur;
            skip = FL_MIN(trimmed, cur_discard);
            cur_discard -= skip;
            trimmed -= skip;
            if (trimmed <= 0) continue;
            if (skip > 0) memmove(pcm, pcm+skip*HC, sizeof(float)*trimmed*HC);
            if (force_stereo) fl_downmix_stereo(pcm, trimmed, HC);
            if (permute) {
               int j, c;
               for (j = 0; j < trimmed; j++)
                  for (c = 0; c < C; c++) tmp[j*C+c] = pcm[j*C+channel_map[c]];
               out = tmp;
            }
            if (!started) {
               /* new link: drain and drop the resampler, restart the per-link counters */
               if (w->have_rs) {
                  if ((ret = fl_drain_resampler(w)) != FL_OK) goto done;
                  fl_resampler_free(&w->rs);
                  w->have_rs = 0;
               }
               w->link_read = w->link_out = 0;
               if (rate != 48000) {
                  if (fl_resampler_init(&w->rs, C, 48000, rate) < 0) {
                     ret = FL_ERR_ALLOC;
                     goto done;
                  }
                  w->have_rs = 1;
               }
               started = 1;
            }
            w->link_read += trimmed;
            if ((ret = fl_audio_write(w, out, trimmed)) != FL_OK) goto done;
         }
         prev_gp = page_gp;
         i = last+1;
      }
   }
   if (w->have_rs && (ret = fl_drain_resampler(w)) != FL_OK) goto done;
   ret = sink->finish(sink);
done:
   fl_format_free(&fmt);
   if (w) {
      if (w->have_rs) fl_resampler_free(&w->rs);
      free(w);
   }
   fl_opus_ms_decoder_destroy(dec);
   free(pcm);
   free(tmp);
   free(heads);
   for (li = 0; li < nlinks; li++) fl_ogg_free(&links[li]);
   free(links);
   return ret;
}

/* ======== flac_dec.c ======== */
/* ---- FLAC decoder (RFC 9639): native FLAC and Ogg FLAC ---- */

#define FL_FLAC_MAX_BLOCK 65535
#define FL_FLAC_MAX_FRAME (1<<22)  /* worst case frame (65535 samples, 8 x 33-bit verbatim) fits */

typedef struct {
   int min_block, max_block, rate, channels, bits;
   fl_u32 min_frame, max_frame;
   fl_u64 total;
   unsigned char md5[16];
} fl_streaminfo;

static unsigned char fl_crc8_tab[256];
static fl_u16 fl_crc16_tab[256];
static int fl_crc_ready;

static void fl_flac_crc_init(void) {
   int i, k;
   if (fl_crc_ready) return;
   for (i = 0; i < 256; i++) {
      unsigned c8 = (unsigned)i, c16 = (unsigned)i<<8;
      for (k = 0; k < 8; k++) {
         c8 = (c8<<1)^(c8&0x80 ? 0x07 : 0);
         c16 = (c16<<1)^(c16&0x8000 ? 0x8005 : 0);
      }
      fl_crc8_tab[i] = (unsigned char)c8;
      fl_crc16_tab[i] = (fl_u16)c16;
   }
   fl_crc_ready = 1;
}

static unsigned fl_crc8(const unsigned char *p, size_t n) {
   unsigned c = 0;
   while (n--) c = fl_crc8_tab[c^*p++];
   return c;
}

static unsigned fl_crc16(const unsigned char *p, size_t n) {
   unsigned c = 0;
   while (n--) c = ((c<<8)&0xFFFF)^fl_crc16_tab[(c>>8)^*p++];
   return c;
}

static void fl_parse_streaminfo(const unsigned char *p, fl_streaminfo *si) {
   si->min_block = p[0]<<8|p[1];
   si->max_block = p[2]<<8|p[3];
   si->min_frame = (fl_u32)p[4]<<16|p[5]<<8|p[6];
   si->max_frame = (fl_u32)p[7]<<16|p[8]<<8|p[9];
   si->rate = p[10]<<12|p[11]<<4|p[12]>>4;
   si->channels = ((p[12]>>1)&7)+1;
   si->bits = ((p[12]&1)<<4|p[13]>>4)+1;
   si->total = (fl_u64)(p[13]&15)<<32|(fl_u64)p[14]<<24|(fl_u32)p[15]<<16|p[16]<<8|p[17];
   memcpy(si->md5, p+18, 16);
}

/* ---- Bit reader (MSB first) ---- */
typedef struct {
   const unsigned char *p;
   size_t len, pos;  /* pos may run past len; missing bytes read as zero */
   fl_u64 cache;     /* next bits, left-aligned */
   int n;            /* valid bits in cache */
} fl_br;

static void fl_br_init(fl_br *b, const unsigned char *p, size_t len) {
   b->p = p;
   b->len = len;
   b->pos = 0;
   b->cache = 0;
   b->n = 0;
}

static void fl_br_fill(fl_br *b) {
   while (b->n <= 56) {
      b->cache |= (fl_u64)(b->pos < b->len ? b->p[b->pos] : 0)<<(56-b->n);
      b->pos++;
      b->n += 8;
   }
}

/* Bits consumed so far. */
static size_t fl_br_tell(const fl_br *b) { return b->pos*8-b->n; }

static fl_u32 fl_br_bits(fl_br *b, int k) {
   fl_u32 v;
   if (k == 0) return 0;
   if (b->n < k) fl_br_fill(b);
   v = (fl_u32)(b->cache>>(64-k));
   b->cache <<= k;
   b->n -= k;
   return v;
}

static fl_i64 fl_br_sbits(fl_br *b, int k) {
   fl_u64 v;
   if (k == 0) return 0;
   if (k > 32) {
      v = (fl_u64)fl_br_bits(b, k-32)<<32;
      v |= fl_br_bits(b, 32);
   } else v = fl_br_bits(b, k);
   return (fl_i64)(v^((fl_u64)1<<(k-1)))-((fl_i64)1<<(k-1));
}

static int fl_clz64(fl_u64 x) {
#if defined(__GNUC__)
   return __builtin_clzll(x);
#else
   int n = 0;
   while (!(x&((fl_u64)1<<63))) { x <<= 1; n++; }
   return n;
#endif
}

/* Unary code: number of zeros before the next one bit. Returns -1 past the end. */
static fl_i64 fl_br_unary(fl_br *b) {
   fl_i64 q = 0;
   for (;;) {
      if (b->n < 8) fl_br_fill(b);
      if (b->cache) {
         int z = fl_clz64(b->cache);
         if (z < b->n) {
            /* Two shifts, not "<<= z+1": when the one bit is the last of a full
               64-bit cache, z+1 is 64, which is undefined in C. x86 masks the
               count to 0 and leaves the bit in place; the next fill then ORs data
               over it and flips a bit 64 positions on. Seen in a real file with
               a Rice parameter of 3 and quotients up to 76. */
            b->cache = (b->cache<<z)<<1;
            b->n -= z+1;
            return q+z;
         }
      }
      q += b->n;
      b->cache = 0;
      b->n = 0;
      if (b->pos > b->len) return -1;
   }
}

/* ---- Frame decoding ---- */
typedef struct {
   int blocksize, rate, channels, assignment, bits, variable;
   fl_u64 number;
} fl_frame_hdr;

/* Parses a frame header at p. Returns its length, 0 if it is not a valid header. */
static int fl_flac_frame_header(const unsigned char *p, size_t avail, const fl_streaminfo *si, fl_frame_hdr *h) {
   static const int rates[12] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
   static const int sizes[8] = {0, 8, 12, 0, 16, 20, 24, 32};
   int pos, len, i, bs, sr, ch, ss;
   fl_u64 v;
   if (avail < 6 || p[0] != 0xFF || (p[1]&0xFE) != 0xF8) return 0;
   h->variable = p[1]&1;
   bs = p[2]>>4;
   sr = p[2]&15;
   ch = p[3]>>4;
   ss = (p[3]>>1)&7;
   if (bs == 0 || sr == 15 || ch > 10 || ss == 3 || (p[3]&1)) return 0;
   /* UTF-8 style coded frame or sample number */
   v = p[4];
   if (v < 0x80) len = 0;
   else if (v >= 0xC0 && v < 0xE0) { len = 1; v &= 0x1F; }
   else if (v >= 0xE0 && v < 0xF0) { len = 2; v &= 0x0F; }
   else if (v >= 0xF0 && v < 0xF8) { len = 3; v &= 0x07; }
   else if (v >= 0xF8 && v < 0xFC) { len = 4; v &= 0x03; }
   else if (v >= 0xFC && v < 0xFE) { len = 5; v &= 0x01; }
   else if (v == 0xFE) { len = 6; v = 0; }
   else return 0;
   pos = 5;
   if (avail < (size_t)pos+len+5) return 0;
   for (i = 0; i < len; i++) {
      if ((p[pos]&0xC0) != 0x80) return 0;
      v = v<<6|(p[pos++]&0x3F);
   }
   h->number = v;
   if (bs == 1) h->blocksize = 192;
   else if (bs <= 5) h->blocksize = 576<<(bs-2);
   else if (bs == 6) h->blocksize = p[pos++]+1;
   else if (bs == 7) { h->blocksize = (p[pos]<<8|p[pos+1])+1; pos += 2; }
   else h->blocksize = 256<<(bs-8);
   if (sr == 0) h->rate = si->rate;
   else if (sr < 12) h->rate = rates[sr];
   else if (sr == 12) h->rate = p[pos++]*1000;
   else if (sr == 13) { h->rate = p[pos]<<8|p[pos+1]; pos += 2; }
   else { h->rate = (p[pos]<<8|p[pos+1])*10; pos += 2; }
   h->assignment = ch;
   h->channels = ch < 8 ? ch+1 : 2;
   h->bits = ss ? sizes[ss] : si->bits;
   if (fl_crc8(p, pos) != p[pos]) return 0;
   if (h->channels != si->channels || h->bits != si->bits) return 0;
   return pos+1;
}

/* Reads a residual block into r[order..n-1]. Returns 0 or -1. */
static int fl_flac_residual(fl_br *b, fl_i64 *r, int n, int order) {
   int method = (int)fl_br_bits(b, 2), porder, parts, pbits, esc, p, i = order;
   if (method > 1) return -1;
   porder = (int)fl_br_bits(b, 4);
   parts = 1<<porder;
   pbits = method ? 5 : 4;
   esc = (1<<pbits)-1;
   if ((n>>porder)<<porder != n || (n>>porder) < order) return -1;
   for (p = 0; p < parts; p++) {
      int k = (int)fl_br_bits(b, pbits), end = (p+1)*(n>>porder);
      if (k == esc) {
         int nb = (int)fl_br_bits(b, 5);
         for (; i < end; i++) r[i] = fl_br_sbits(b, nb);
      } else {
         for (; i < end; i++) {
            fl_i64 q = fl_br_unary(b);
            fl_u64 u;
            if (q < 0 || q > ((fl_i64)1<<32)) return -1;
            u = (fl_u64)q<<k|fl_br_bits(b, k);
            r[i] = (fl_i64)(u>>1)^-(fl_i64)(u&1);
         }
      }
      if (b->pos > b->len+8) return -1;
   }
   return 0;
}

/* Decodes one subframe of n samples with bps bits into x. Returns 0 or -1. */
static int fl_flac_subframe(fl_br *b, fl_i64 *x, int n, int bps) {
   int type, wasted = 0, i, j;
   if (fl_br_bits(b, 1)) return -1;
   type = (int)fl_br_bits(b, 6);
   if (fl_br_bits(b, 1)) {
      fl_i64 w = fl_br_unary(b);
      if (w < 0 || w+1 >= bps) return -1;
      wasted = (int)w+1;
      bps -= wasted;
   }
   if (type == 0) {
      fl_i64 v = fl_br_sbits(b, bps);
      for (i = 0; i < n; i++) x[i] = v;
   } else if (type == 1) {
      for (i = 0; i < n; i++) x[i] = fl_br_sbits(b, bps);
   } else if (type >= 8 && type <= 12) {
      int order = type-8;
      if (order > n) return -1;
      for (i = 0; i < order; i++) x[i] = fl_br_sbits(b, bps);
      if (fl_flac_residual(b, x, n, order) < 0) return -1;
      switch (order) {
      case 1: for (i = 1; i < n; i++) x[i] += x[i-1]; break;
      case 2: for (i = 2; i < n; i++) x[i] += 2*x[i-1]-x[i-2]; break;
      case 3: for (i = 3; i < n; i++) x[i] += 3*(x[i-1]-x[i-2])+x[i-3]; break;
      case 4: for (i = 4; i < n; i++) x[i] += 4*(x[i-1]+x[i-3])-6*x[i-2]-x[i-4]; break;
      default: break;
      }
   } else if (type >= 32) {
      int order = type-31, prec, shift;
      fl_i64 c[32];
      if (order > n) return -1;
      for (i = 0; i < order; i++) x[i] = fl_br_sbits(b, bps);
      prec = (int)fl_br_bits(b, 4)+1;
      shift = (int)fl_br_sbits(b, 5);
      if (prec == 16 || shift < 0) return -1;
      for (j = 0; j < order; j++) c[j] = fl_br_sbits(b, prec);
      if (fl_flac_residual(b, x, n, order) < 0) return -1;
      for (i = order; i < n; i++) {
         fl_i64 s = 0;
         const fl_i64 *h = x+i-1;
         for (j = 0; j < order; j++) s += c[j]*h[-j];
         x[i] += s>>shift;
      }
   } else return -1;
   if (wasted)
      for (i = 0; i < n; i++) x[i] = (fl_i64)((fl_u64)x[i]<<wasted);
   return 0;
}

typedef struct {
   fl_streaminfo si;
   int cap;  /* largest block the buffers hold */
   fl_i64 *ch[8];
   fl_i32 *out;
} fl_flac_dec;

static void fl_flac_dec_free(fl_flac_dec *d) {
   int c;
   for (c = 0; c < 8; c++) free(d->ch[c]);
   free(d->out);
}

static int fl_flac_dec_alloc(fl_flac_dec *d, int cap) {
   int c;
   d->cap = cap;
   for (c = 0; c < d->si.channels; c++)
      if (!(d->ch[c] = (fl_i64*)malloc(sizeof(fl_i64)*cap))) return -1;
   d->out = (fl_i32*)malloc(sizeof(fl_i32)*cap*d->si.channels);
   return d->out ? 0 : -1;
}

/* Decodes the frame at p into d->out (interleaved). Returns the frame length in bytes
   and the block size in *n, or 0 if there is no valid frame at p. */
static size_t fl_flac_decode_frame(fl_flac_dec *d, const unsigned char *p, size_t avail, int *n) {
   fl_frame_hdr h;
   fl_br b;
   int hl = fl_flac_frame_header(p, avail, &d->si, &h), c, i, C = h.channels, bs;
   size_t len;
   if (hl == 0 || h.blocksize > d->cap) return 0;
   bs = h.blocksize;
   fl_br_init(&b, p+hl, avail-hl);
   for (c = 0; c < C; c++) {
      int side = (h.assignment == 8 && c == 1) || (h.assignment == 9 && c == 0) || (h.assignment == 10 && c == 1);
      if (fl_flac_subframe(&b, d->ch[c], bs, h.bits+side) < 0) return 0;
   }
   len = hl+(fl_br_tell(&b)+7)/8;
   if (len+2 > avail || fl_crc16(p, len) != (unsigned)(p[len]<<8|p[len+1])) return 0;
   {
      fl_i64 *x0 = d->ch[0], *x1 = d->ch[1];
      if (h.assignment == 8) for (i = 0; i < bs; i++) x1[i] = x0[i]-x1[i];
      else if (h.assignment == 9) for (i = 0; i < bs; i++) x0[i] += x1[i];
      else if (h.assignment == 10)
         for (i = 0; i < bs; i++) {
            fl_i64 m = (fl_i64)((fl_u64)x0[i]<<1)|(x1[i]&1), s = x1[i];
            x0[i] = (m+s)>>1;
            x1[i] = (m-s)>>1;
         }
   }
   for (c = 0; c < C; c++) {
      const fl_i64 *x = d->ch[c];
      fl_i32 *o = d->out+c;
      for (i = 0; i < bs; i++) o[i*C] = (fl_i32)x[i];
   }
   *n = bs;
   return len+2;
}

/* Handles one metadata block for the stream format. type 0 fills si. */
static int fl_flac_metadata(fl_format *f, fl_streaminfo *si, int type, const unsigned char *p, long len) {
   if (type == 0) {
      if (len < 34) return FL_ERR_BAD_FILE;
      fl_parse_streaminfo(p, si);
   } else if (type == 4) {
      int i, k = f->ntags;
      if (fl_parse_comments(f, p, len) < 0) return FL_ERR_BAD_FILE;
      /* the channel mask tag is carried in fmt.channel_mask */
      for (i = k; i < f->ntags; i++)
         if (fl_tag_is(f->tags[i], "WAVEFORMATEXTENSIBLE_CHANNEL_MASK")) {
            f->channel_mask = (fl_u32)strtoul(f->tags[i]+34, NULL, 0);
            free(f->tags[i]);
            f->tags[i--] = f->tags[--f->ntags];
         }
   } else if (type == 6) {
      char *b64 = fl_base64_encode(p, (int)len);
      size_t l;
      char *t;
      if (!b64) return FL_ERR_ALLOC;
      l = strlen(b64);
      t = (char*)malloc(l+24);
      if (!t) {
         free(b64);
         return FL_ERR_ALLOC;
      }
      memcpy(t, "METADATA_BLOCK_PICTURE=", 23);
      memcpy(t+23, b64, l);
      fl_format_add_tag(f, t, (int)l+23);
      free(t);
      free(b64);
   } else if (type != 1 && type != 3 && type != 127) {
      if (fl_format_add_block(f, type, p, (int)len) < 0) return FL_ERR_ALLOC;
   }
   return FL_OK;
}

static int fl_flac_start(fl_flac_dec *d, fl_format *f, fl_sink *sink) {
   fl_streaminfo *si = &d->si;
   if (si->channels < 1 || si->bits < 4 || si->rate < 1 || si->max_block < 16) return FL_ERR_BAD_FILE;
   if (fl_flac_dec_alloc(d, FL_FLAC_MAX_BLOCK) < 0) return FL_ERR_ALLOC;
   f->channels = si->channels;
   f->rate = si->rate;
   f->bits = f->container_bits = si->bits;
   f->frames = si->total ? (fl_i64)si->total : -1;
   if (f->channel_mask == fl_default_mask(f->channels)) f->channel_mask = 0;
   return sink->start(sink, f);
}

static int fl_md5_check(fl_md5 *m, const fl_streaminfo *si) {
   static const unsigned char zero[16] = {0};
   unsigned char sum[16];
   fl_md5_final(m, sum);
   return memcmp(si->md5, zero, 16) == 0 || memcmp(si->md5, sum, 16) == 0 ? FL_OK : FL_ERR_CHECKSUM;
}

/* Ogg FLAC (first logical stream, mapping version 1). */
static int fl_run_ogg_flac(const unsigned char *file, long size, fl_sink *sink) {
   fl_ogg_stream *links = NULL;
   int nlinks = 0, i, ret = FL_ERR_BAD_FILE;
   fl_flac_dec d;
   fl_format f;
   fl_md5 md5;
   memset(&d, 0, sizeof(d));
   fl_format_init(&f);
   fl_flac_crc_init();
   fl_md5_init(&md5);
   if (fl_ogg_parse(file, size, "\177FLAC", &links, &nlinks) < 0) goto done;
   {
      fl_ogg_stream *s = &links[0];
      int first = 1;
      if (s->pk[0].len < 51 || memcmp(s->pk[0].data+9, "fLaC", 4) != 0) goto done;
      fl_parse_streaminfo(s->pk[0].data+17, &d.si);
      for (i = 1; i < s->count; i++) {
         const unsigned char *p = s->pk[i].data;
         int len = s->pk[i].len, n;
         if (len > 0 && p[0] != 0xFF) {
            /* header packet: one metadata block */
            if (len < 4) goto done;
            if ((ret = fl_flac_metadata(&f, &d.si, p[0]&127, p+4, len-4)) != FL_OK) goto done;
            continue;
         }
         if (first) {
            if ((ret = fl_flac_start(&d, &f, sink)) != FL_OK) goto done;
            first = 0;
         }
         if (fl_flac_decode_frame(&d, p, len, &n) == 0) {
            ret = FL_ERR_BAD_FILE;
            goto done;
         }
         fl_md5_samples(&md5, d.out, (size_t)n*d.si.channels, d.si.bits);
         if ((ret = sink->write(sink, d.out, n)) != FL_OK) goto done;
      }
      if (first && (ret = fl_flac_start(&d, &f, sink)) != FL_OK) goto done;
      ret = sink->finish(sink);
      if (ret == FL_OK) ret = fl_md5_check(&md5, &d.si);
   }
done:
   for (i = 0; i < nlinks; i++) fl_ogg_free(&links[i]);
   free(links);
   fl_flac_dec_free(&d);
   fl_format_free(&f);
   return ret;
}

/* Buffered file input that keeps at least the requested number of bytes in memory. */
typedef struct {
   FILE *f;
   unsigned char *buf;
   size_t cap, pos, len;
   int eof;
} fl_inbuf;

/* Makes n bytes available at buf+pos (fewer only at end of file). */
static int fl_inbuf_need(fl_inbuf *b, size_t n) {
   if (b->len-b->pos >= n || b->eof) return 0;
   if (b->pos > 0) {
      memmove(b->buf, b->buf+b->pos, b->len-b->pos);
      b->len -= b->pos;
      b->pos = 0;
   }
   if (n > b->cap) {
      size_t cap = n > 2*b->cap ? n : 2*b->cap;
      unsigned char *nb = (unsigned char*)realloc(b->buf, cap);
      if (!nb) return -1;
      b->buf = nb;
      b->cap = cap;
   }
   while (b->len < b->cap && !b->eof) {
      size_t r = fread(b->buf+b->len, 1, b->cap-b->len, b->f);
      b->len += r;
      if (r == 0) b->eof = 1;
   }
   return 0;
}

/* Decodes a FLAC or Ogg FLAC file and passes the samples to sink. */
static int fl_run_flac(const char *path, fl_sink *sink) {
   fl_inbuf in;
   fl_flac_dec d;
   fl_format f;
   fl_md5 md5;
   int ret = FL_ERR_BAD_FILE, last = 0;
   memset(&in, 0, sizeof(in));
   memset(&d, 0, sizeof(d));
   fl_format_init(&f);
   fl_flac_crc_init();
   fl_md5_init(&md5);
   if (!(in.f = fl_fopen(path, "rb"))) return FL_ERR_IO;
   if (fl_inbuf_need(&in, 1<<16) < 0) {
      ret = FL_ERR_ALLOC;
      goto done;
   }
   if (in.len >= 4 && memcmp(in.buf, "OggS", 4) == 0) {
      long size;
      unsigned char *file;
      fclose(in.f);
      in.f = NULL;
      free(in.buf);
      in.buf = NULL;
      if (!(file = fl_read_file(path, &size))) return FL_ERR_IO;
      ret = fl_run_ogg_flac(file, size, sink);
      free(file);
      return ret;
   }
   /* skip ID3v2 tags */
   while (in.len-in.pos >= 10 && memcmp(in.buf+in.pos, "ID3", 3) == 0) {
      const unsigned char *p = in.buf+in.pos;
      size_t skip = 10+((size_t)(p[6]&127)<<21|(p[7]&127)<<14|(p[8]&127)<<7|(p[9]&127))+(p[5]&0x10 ? 10 : 0);
      if (fl_inbuf_need(&in, skip+4) < 0 || in.len-in.pos < skip) goto done;
      in.pos += skip;
   }
   if (in.len-in.pos < 8 || memcmp(in.buf+in.pos, "fLaC", 4) != 0) goto done;
   in.pos += 4;
   while (!last) {
      const unsigned char *p;
      long len;
      int type;
      if (fl_inbuf_need(&in, 4) < 0 || in.len-in.pos < 4) goto done;
      p = in.buf+in.pos;
      last = p[0]>>7;
      type = p[0]&127;
      len = (long)p[1]<<16|p[2]<<8|p[3];
      if (fl_inbuf_need(&in, 4+(size_t)len) < 0) {
         ret = FL_ERR_ALLOC;
         goto done;
      }
      if (in.len-in.pos < 4+(size_t)len) goto done;
      if ((ret = fl_flac_metadata(&f, &d.si, type, in.buf+in.pos+4, len)) != FL_OK) goto done;
      in.pos += 4+len;
      ret = FL_ERR_BAD_FILE;
   }
   if ((ret = fl_flac_start(&d, &f, sink)) != FL_OK) goto done;
   for (;;) {
      size_t used;
      int n;
      if (fl_inbuf_need(&in, FL_FLAC_MAX_FRAME) < 0) {
         ret = FL_ERR_ALLOC;
         goto done;
      }
      if (in.pos >= in.len) break;
      used = fl_flac_decode_frame(&d, in.buf+in.pos, in.len-in.pos, &n);
      if (used == 0) {
         /* not a frame: trailing junk (tags) is ignored, a valid frame later means damage */
         size_t k;
         for (k = in.pos+1; k+1 < in.len; k++)
            if (in.buf[k] == 0xFF && (in.buf[k+1]&0xFE) == 0xF8 && fl_flac_decode_frame(&d, in.buf+k, in.len-k, &n)) {
               ret = FL_ERR_BAD_FILE;
               goto done;
            }
         break;
      }
      in.pos += used;
      fl_md5_samples(&md5, d.out, (size_t)n*d.si.channels, d.si.bits);
      if ((ret = sink->write(sink, d.out, n)) != FL_OK) goto done;
   }
   ret = sink->finish(sink);
   if (ret == FL_OK) ret = fl_md5_check(&md5, &d.si);
done:
   if (in.f) fclose(in.f);
   free(in.buf);
   fl_flac_dec_free(&d);
   fl_format_free(&f);
   return ret;
}

/* ======== flac_enc.c ======== */
/* ---- FLAC encoder: searches block sizes, predictors and residual coding for the smallest file ----

   Per block and channel the encoder compares constant, verbatim, fixed and LPC subframes.
   LPC candidates come from windowed autocorrelation (several apodizations), the covariance
   method (exact least squares), iteratively reweighted least squares that approximates the
   Rice cost, and a hill climb on the quantized coefficients. Rice partitions and parameters
   are chosen exactly. Block sizes are chosen per segment by dynamic programming (variable
   block size stream). Every frame is decoded again and compared before it is written. */

#define FL_FLAC_SEGMENT 65536   /* samples per channel analysed at once */
#define FL_FLAC_MAX_ORDER 32
#define FL_PI 3.14159265358979323846
#define FL_SLACK 288   /* extra entries after every sample buffer */

/* ---- Optional threads: fl_parallel() runs fn(ctx, t, nt) for t = 0..nt-1 ---- */
#if !defined(FLAPUS_NO_THREADS) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define FL_THREADS 1
#elif !defined(FLAPUS_NO_THREADS) && (defined(__unix__) || defined(__APPLE__))
#include <pthread.h>
#include <unistd.h>
#define FL_THREADS 2
#else
#define FL_THREADS 0
#endif
#define FL_MAX_THREADS 64

typedef struct {
   void (*fn)(void *ctx, int t, int nt);
   void *ctx;
   int t, nt;
} fl_job;

#if FL_THREADS == 1
static DWORD WINAPI fl_job_main(LPVOID p) {
   fl_job *j = (fl_job*)p;
   j->fn(j->ctx, j->t, j->nt);
   return 0;
}
#elif FL_THREADS == 2
static void *fl_job_main(void *p) {
   fl_job *j = (fl_job*)p;
   j->fn(j->ctx, j->t, j->nt);
   return NULL;
}
#endif

static void fl_parallel(void (*fn)(void*, int, int), void *ctx, int nt) {
   fl_job jobs[FL_MAX_THREADS];
   int t;
#if FL_THREADS == 1
   HANDLE th[FL_MAX_THREADS];
#elif FL_THREADS == 2
   pthread_t th[FL_MAX_THREADS];
   int ok[FL_MAX_THREADS];
#endif
   for (t = 0; t < nt; t++) {
      jobs[t].fn = fn;
      jobs[t].ctx = ctx;
      jobs[t].t = t;
      jobs[t].nt = nt;
   }
#if FL_THREADS == 1
   for (t = 1; t < nt; t++) th[t] = CreateThread(NULL, 0, fl_job_main, &jobs[t], 0, NULL);
   fn(ctx, 0, nt);
   for (t = 1; t < nt; t++) {
      if (th[t]) {
         WaitForSingleObject(th[t], INFINITE);
         CloseHandle(th[t]);
      } else fn(ctx, t, nt);
   }
#elif FL_THREADS == 2
   for (t = 1; t < nt; t++) ok[t] = pthread_create(&th[t], NULL, fl_job_main, &jobs[t]) == 0;
   fn(ctx, 0, nt);
   for (t = 1; t < nt; t++) {
      if (ok[t]) pthread_join(th[t], NULL);
      else fn(ctx, t, nt);
   }
#else
   for (t = 0; t < nt; t++) fn(ctx, t, nt);
#endif
}

static int fl_cpu_count(void) {
#if FL_THREADS == 1
   SYSTEM_INFO si;
   GetSystemInfo(&si);
   return (int)si.dwNumberOfProcessors;
#elif FL_THREADS == 2
   long n = sysconf(_SC_NPROCESSORS_ONLN);
   return n > 0 ? (int)n : 1;
#else
   return 1;
#endif
}

typedef struct {
   int max_order;     /* LPC order limit */
   int max_porder;    /* Rice partition order limit */
   int max_block;     /* block size limit */
   int unit;          /* block size grid for the search (0 = fixed block size) */
   int fixed_block;   /* block size when unit == 0 */
   int nwin;          /* apodization windows tried in the final search */
   int all_orders;    /* evaluate every LPC order (else the best few by prediction error) */
   int prec_range;    /* coefficient precisions tried around the default (99 = all) */
   int ls;            /* covariance method candidates */
   int irls;          /* IRLS iterations */
   int climb;         /* hill climbing passes */
   int dp_search;     /* block costs: 0 = quick estimate from prefix sums, 1 = light search */
   int coarse;        /* longer blocks are tried in steps of coarse_step only (0 = all) */
   int coarse_step;
   int estimate;      /* report the estimated instead of the exact subframe size */
} fl_flac_cfg;

static void fl_flac_level(fl_flac_cfg *c, int level, int lax, int rate) {
   static const fl_flac_cfg levels[9] = {
      /* order porder block unit fixed nwin all prec ls irls climb dp coarse step */
      {8, 6, 4608, 0, 4096, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {12, 8, 4608, 0, 4096, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0},
      {12, 8, 4608, 0, 4096, 3, 0, 2, 1, 2, 0, 0, 0, 0, 0},
      {12, 8, 4608, 512, 4096, 3, 0, 2, 1, 2, 0, 0, 0, 0, 0},
      {12, 8, 4608, 512, 4096, 3, 0, 99, 0, 3, 2, 1, 0, 0, 0},
      {12, 8, 4608, 256, 4096, 3, 0, 99, 0, 4, 4, 1, 0, 0, 0},
      {12, 8, 4608, 256, 4096, 3, 0, 99, 0, 6, 8, 1, 0, 0, 0},
      {12, 8, 4608, 128, 4096, 3, 0, 99, 0, 6, 8, 1, 2048, 256, 0},
      {12, 8, 4608, 128, 4096, 5, 1, 99, 1, 8, 8, 1, 0, 0, 0}};
   *c = levels[level < 0 ? 0 : level > 8 ? 8 : level];
   if (lax) {
      if (c->max_order > 8) c->max_order = 32;
      c->max_porder = 15;
      c->max_block = rate > 48000 ? 32768 : 16384;
   } else if (rate > 48000) c->max_block = 16384;
   if (rate > 48000) {
      if (c->max_order == 12) c->max_order = 32;
      if (level >= 2) c->max_porder = 15;
   }
   if (c->unit && c->max_block > 4608) {
      /* long blocks: keep the search affordable */
      if (c->unit < 256) c->unit = 256;
      c->coarse = 4096;
      c->coarse_step = 1024;
   }
}

/* ---- Bit writer (MSB first) ---- */
typedef struct {
   unsigned char *buf;
   size_t cap, len;
   fl_u64 acc;
   int n;
} fl_bw;

static int fl_bw_reserve(fl_bw *w, size_t bytes) {
   if (w->len+bytes > w->cap) {
      size_t cap = w->cap ? w->cap : 1<<16;
      unsigned char *nb;
      while (cap < w->len+bytes) cap *= 2;
      if (!(nb = (unsigned char*)realloc(w->buf, cap))) return -1;
      w->buf = nb;
      w->cap = cap;
   }
   return 0;
}

static void fl_bw_put(fl_bw *w, fl_u32 v, int k) {
   if (k == 0) return;
   if (k < 32) v &= (1u<<k)-1;
   w->acc = w->acc<<k|v;
   w->n += k;
   while (w->n >= 8) {
      w->n -= 8;
      w->buf[w->len++] = (unsigned char)(w->acc>>w->n);
   }
}

static void fl_bw_put_signed(fl_bw *w, fl_i32 v, int k) { fl_bw_put(w, (fl_u32)v, k); }

static void fl_bw_zeros(fl_bw *w, fl_u32 q) {
   while (q >= 32) {
      fl_bw_put(w, 0, 32);
      q -= 32;
   }
   fl_bw_put(w, 0, (int)q);
}

static void fl_bw_align(fl_bw *w) {
   if (w->n) fl_bw_put(w, 0, 8-w->n);
}

/* Number of bits needed for x (0 for 0). */
static int fl_bitlen(fl_u32 x) {
#if defined(__GNUC__)
   return x ? 32-__builtin_clz(x) : 0;
#else
   return fl_ilog(x);
#endif
}

/* ---- Rice coding ---- */
static fl_u32 fl_zz(fl_i32 r) { return ((fl_u32)r<<1)^(fl_u32)(r>>31); }

/* Estimated bits of one partition with parameter k (exact for k == 0). */
static fl_u64 fl_rice_est_k(fl_u64 s, fl_u32 n, int k) {
   fl_u64 corr = k ? ((fl_u64)n*((1u<<k)-1))>>1 : 0;
   return (fl_u64)n*(k+1)+((s > corr ? s-corr : 0)>>k);
}

/* Best estimated cost of a partition (Rice parameter or escape). */
static fl_u64 fl_rice_part_est(fl_u64 s, fl_u32 n, fl_u32 umax, int kmax) {
   fl_u64 best, c;
   int k0 = 0, k, eb;
   if (n == 0) return 0;
   if (s > n) k0 = fl_bitlen((fl_u32)FL_MIN(s/n, 0xFFFFFFFFu))-1;
   if (k0 > kmax) k0 = kmax;
   best = fl_rice_est_k(s, n, k0);
   for (k = k0-1; k <= k0+1; k += 2)
      if (k >= 0 && k <= kmax && (c = fl_rice_est_k(s, n, k)) < best) best = c;
   eb = fl_bitlen(umax);
   c = 5+(fl_u64)n*eb;
   return c < best ? c : best;
}

typedef struct {
   fl_u64 *sum;
   fl_u32 *umax;
   int cap;
} fl_rice_ws;

/* Highest usable partition order for a block of n samples and predictor order. */
static int fl_rice_max_porder(int n, int order, int limit) {
   int p = limit;
   while (p > 0 && (((n>>p)<<p) != n || (n>>p) <= order)) p--;
   return p;
}

/* Fast estimate of the residual bits (including the residual header). */
static fl_u64 fl_rice_estimate(fl_rice_ws *ws, const fl_u32 *u, int n, int order, int max_porder, int kmax) {
   int pmax, parts, p, i, pbits = kmax > 14 ? 5 : 4;
   fl_u64 best = ~(fl_u64)0;
   int psize;
   const fl_u32 *q = u;
   while (max_porder > 0 && (n>>max_porder) < 16) max_porder--;
   pmax = fl_rice_max_porder(n, order, max_porder);
   parts = 1<<pmax;
   psize = n>>pmax;
   /* finest level sums */
   for (p = 0; p < parts; p++) {
      int cnt = p == 0 ? psize-order : psize;
      fl_u64 s = 0;
      fl_u32 m = 0;
      for (i = 0; i < cnt; i++) {
         s += q[i];
         m |= q[i];
      }
      q += cnt;
      ws->sum[p] = s;
      ws->umax[p] = m;  /* OR has the same bit length as the maximum */
   }
   for (;;) {
      fl_u64 tot = 6+(fl_u64)parts*pbits;
      for (p = 0; p < parts; p++) {
         fl_u32 cnt = (fl_u32)((n>>pmax)-(p == 0 ? order : 0));
         tot += fl_rice_part_est(ws->sum[p], cnt, ws->umax[p], kmax);
      }
      if (tot < best) best = tot;
      if (pmax == 0) break;
      pmax--;
      parts >>= 1;
      for (p = 0; p < parts; p++) {
         ws->sum[p] = ws->sum[2*p]+ws->sum[2*p+1];
         ws->umax[p] = ws->umax[2*p]|ws->umax[2*p+1];
      }
   }
   return best;
}

/* Exact Rice coding plan of a residual. */
typedef struct {
   int porder, method;
   int *k;     /* parameter per partition; escapes are stored as -1-bits */
   int cap;
} fl_rice_plan;

static fl_u64 fl_rice_part_exact(const fl_u32 *u, fl_u32 n, int k) {
   fl_u64 s = (fl_u64)n*(k+1);
   fl_u32 i;
   for (i = 0; i < n; i++) s += u[i]>>k;
   return s;
}

/* Best exact parameter (or escape) of one partition; returns bits without the parameter field. */
static fl_u64 fl_rice_part_best(const fl_u32 *u, fl_u32 n, int kmax, int *kout) {
   fl_u64 s = 0, best, c;
   fl_u32 m = 0, i;
   int k, k0 = 0, eb;
   if (n == 0) {
      *kout = 0;
      return 0;
   }
   for (i = 0; i < n; i++) {
      s += u[i];
      m |= u[i];
   }
   if (s > n) k0 = fl_bitlen((fl_u32)FL_MIN(s/n, 0xFFFFFFFFu))-1;
   if (k0 > kmax) k0 = kmax;
   best = fl_rice_part_exact(u, n, k0);
   *kout = k0;
   for (k = k0-1; k >= 0 && (c = fl_rice_part_exact(u, n, k)) < best; k--) {
      best = c;
      *kout = k;
   }
   if (*kout == k0)
      for (k = k0+1; k <= kmax && (c = fl_rice_part_exact(u, n, k)) < best; k++) {
         best = c;
         *kout = k;
      }
   eb = fl_bitlen(m);
   c = 5+(fl_u64)n*eb;
   if (c < best) {
      best = c;
      *kout = -1-eb;
   }
   return best;
}

/* Exact plan: best partition order and parameters (method 0 and 1 compared). */
static fl_u64 fl_rice_exact(fl_rice_plan *plan, int *tmp, const fl_u32 *u, int n, int order, int max_porder, int allow_rice2) {
   int pmax = fl_rice_max_porder(n, order, max_porder), method, p, j;
   fl_u64 best = ~(fl_u64)0;
   for (method = 0; method <= allow_rice2; method++) {
      int kmax = method ? 30 : 14, pbits = method ? 5 : 4;
      for (p = 0; p <= pmax; p++) {
         int parts = 1<<p, psize = n>>p;
         const fl_u32 *q = u;
         fl_u64 tot = 6+(fl_u64)parts*pbits;
         for (j = 0; j < parts && tot < best; j++) {
            fl_u32 cnt = (fl_u32)(psize-(j == 0 ? order : 0));
            tot += fl_rice_part_best(q, cnt, kmax, &tmp[j]);
            q += cnt;
         }
         if (tot < best) {
            best = tot;
            plan->porder = p;
            plan->method = method;
            memcpy(plan->k, tmp, sizeof(int)*parts);
         }
      }
   }
   return best;
}

static void fl_rice_write(fl_bw *w, const fl_rice_plan *plan, const fl_i32 *res, int n, int order) {
   int parts = 1<<plan->porder, psize = n>>plan->porder, pbits = plan->method ? 5 : 4, j, i = order;
   fl_bw_put(w, plan->method, 2);
   fl_bw_put(w, plan->porder, 4);
   for (j = 0; j < parts; j++) {
      int k = plan->k[j], end = (j+1)*psize;
      if (k < 0) {
         int eb = -1-k;
         fl_bw_put(w, (1u<<pbits)-1, pbits);
         fl_bw_put(w, eb, 5);
         for (; i < end; i++) fl_bw_put_signed(w, res[i], eb);
      } else {
         fl_bw_put(w, k, pbits);
         for (; i < end; i++) {
            fl_u32 v = fl_zz(res[i]), q = v>>k;
            if (q+1+k <= 32) fl_bw_put(w, (1u<<k|(v&((1u<<k)-1))), (int)q+1+k);
            else {
               fl_bw_zeros(w, q);
               fl_bw_put(w, 1, 1);
               fl_bw_put(w, v, k);
            }
         }
      }
   }
}

/* ---- Subframe description ---- */
typedef struct {
   int type;       /* 0 constant, 1 verbatim, 2 fixed, 3 LPC */
   int order, wasted, prec, shift;
   fl_i32 coef[FL_FLAC_MAX_ORDER];
   fl_u64 bits;    /* exact size of the subframe */
} fl_sf;

/* Per-thread work space for the subframe search. */
typedef struct {
   int cap;
   fl_i32 *x, *res;
   fl_u32 *u;
   fl_i64 *acc;
   double *xd, *wx, *wt, *e;
   fl_rice_ws rws;
   fl_rice_plan plan;
   int *ktmp;
} fl_ws;

static void fl_ws_free(fl_ws *w) {
   free(w->x);
   free(w->res);
   free(w->u);
   free(w->acc);
   free(w->xd);
   free(w->wx);
   free(w->wt);
   free(w->e);
   free(w->rws.sum);
   free(w->rws.umax);
   free(w->plan.k);
   free(w->ktmp);
   memset(w, 0, sizeof(*w));
}

static int fl_ws_alloc(fl_ws *w, int n, int max_porder) {
   int parts = 1<<max_porder;
   if (n <= w->cap) return 0;
   fl_ws_free(w);
   w->cap = n;
   n += FL_SLACK;
   w->x = (fl_i32*)calloc(n, sizeof(fl_i32));
   w->res = (fl_i32*)malloc(sizeof(fl_i32)*n);
   w->u = (fl_u32*)malloc(sizeof(fl_u32)*n);
   w->acc = (fl_i64*)malloc(sizeof(fl_i64)*n);
   w->xd = (double*)calloc(n, sizeof(double));
   w->wx = (double*)malloc(sizeof(double)*n);
   w->wt = (double*)malloc(sizeof(double)*n);
   w->e = (double*)malloc(sizeof(double)*n);
   w->rws.sum = (fl_u64*)malloc(sizeof(fl_u64)*parts);
   w->rws.umax = (fl_u32*)malloc(sizeof(fl_u32)*parts);
   w->plan.k = (int*)malloc(sizeof(int)*parts);
   w->ktmp = (int*)malloc(sizeof(int)*parts);
   return w->x && w->res && w->u && w->acc && w->xd && w->wx && w->wt && w->e &&
          w->rws.sum && w->rws.umax && w->plan.k && w->ktmp ? 0 : -1;
}

/* ---- Prediction ---- */

/* Fixed predictor residual; returns 0 if a residual does not fit in 32 bits. */
static int fl_fixed_residual(const fl_i32 *x, int n, int order, fl_i32 *r) {
   int i;
   for (i = 0; i < order; i++) r[i] = x[i];
   for (i = order; i < n; i++) {
      fl_i64 p;
      switch (order) {
      case 0: p = 0; break;
      case 1: p = x[i-1]; break;
      case 2: p = 2*(fl_i64)x[i-1]-x[i-2]; break;
      case 3: p = 3*((fl_i64)x[i-1]-x[i-2])+x[i-3]; break;
      default: p = 4*((fl_i64)x[i-1]+x[i-3])-6*(fl_i64)x[i-2]-x[i-4]; break;
      }
      p = x[i]-p;
      if (p > 0x7FFFFFFF || p < -0x7FFFFFFF) return 0;
      r[i] = (fl_i32)p;
   }
   return 1;
}

/* Unshifted LPC prediction sums acc[i] = sum c[j]*x[i-1-j] for i >= order. mode 0: 32-bit
   arithmetic is exact, 1: double is exact (xd = x as double), 2: 64-bit. Modes 0 and 1 work
   in chunks of 256 and read and write up to 255 entries past n (buffers have FL_SLACK). */
static void fl_lpc_sums(const fl_i32 *x, const double *xd, int n, const fl_i32 *c, int order, int mode, fl_i64 *acc) {
   int i, j, k;
   if (mode == 0) {
      fl_u32 t[256];
      for (i = order; i < n; i += 256) {
         for (k = 0; k < 256; k++) t[k] = 0;
         for (j = 0; j < order; j++) {
            const fl_i32 *h = x+i-1-j;
            fl_u32 cj = (fl_u32)c[j];
            for (k = 0; k < 256; k++) t[k] += cj*(fl_u32)h[k];
         }
         for (k = 0; k < 256; k++) acc[i+k] = (fl_i32)t[k];
      }
   } else if (mode == 1) {
      double t[256];
      for (i = order; i < n; i += 256) {
         for (k = 0; k < 256; k++) t[k] = 0;
         for (j = 0; j < order; j++) {
            const double *h = xd+i-1-j;
            double cj = c[j];
            for (k = 0; k < 256; k++) t[k] += cj*h[k];
         }
         for (k = 0; k < 256; k++) acc[i+k] = (fl_i64)t[k];
      }
   } else {
      for (i = order; i < n; i++) {
         fl_i64 sum = 0;
         const fl_i32 *h = x+i-1;
         for (j = 0; j < order; j++) sum += (fl_i64)c[j]*h[-j];
         acc[i] = sum;
      }
   }
}

/* Arithmetic mode for fl_lpc_sums. */
static int fl_lpc_mode(int bps, int prec, int order) {
   int need = bps+prec+fl_bitlen(order);
   return need <= 32 ? 0 : need <= 53 ? 1 : 2;
}

/* Residual from prediction sums; returns 0 on 32-bit overflow. */
static int fl_lpc_residual(const fl_i32 *x, int n, int order, int shift, const fl_i64 *acc, fl_i32 *r, fl_u32 *u) {
   int i;
   fl_i64 bad = 0;
   for (i = 0; i < order; i++) r[i] = x[i];
   for (i = order; i < n; i++) {
      fl_i64 v = x[i]-(acc[i]>>shift);
      bad |= (v+0x7FFFFFFF)>>32;  /* nonzero outside [-2^31+1, 2^31-1] */
      r[i] = (fl_i32)v;
      u[i-order] = fl_zz((fl_i32)v);
   }
   return bad == 0;
}

/* Levinson-Durbin: lpc[o-1][0..o-1] for every order o <= max_order; err[o] prediction error. */
static int fl_levinson(const double *R, int max_order, double lpc[][FL_FLAC_MAX_ORDER], double *err) {
   double a[FL_FLAC_MAX_ORDER+1], t[FL_FLAC_MAX_ORDER+1], e = R[0];
   int i, j, o;
   if (!(e > 0)) return 0;
   a[0] = 1;
   for (o = 1; o <= max_order; o++) {
      double k = -R[o];
      for (j = 1; j < o; j++) k -= a[j]*R[o-j];
      k /= e;
      if (!(k > -1.0 && k < 1.0)) return o-1;
      for (j = 1; j < o; j++) t[j] = a[j]+k*a[o-j];
      for (j = 1; j < o; j++) a[j] = t[j];
      a[o] = k;
      e *= 1-k*k;
      if (!(e > 0)) e = R[0]*1e-15;
      for (i = 0; i < o; i++) lpc[o-1][i] = -a[i+1];
      err[o] = e;
   }
   return max_order;
}

/* Quantizes coefficients like libFLAC (error feedback). Returns 0 if not representable. */
static int fl_quantize(const double *lp, int order, int prec, fl_i32 *q, int *shift_out) {
   double cmax = 0, err = 0;
   int i, log2cmax, shift, qmax = (1<<(prec-1))-1, qmin = -qmax-1;
   for (i = 0; i < order; i++) cmax = FL_MAX(cmax, fabs(lp[i]));
   if (!(cmax > 0)) return 0;
   frexp(cmax, &log2cmax);
   shift = prec-1-log2cmax;
   if (shift > 15) shift = 15;
   if (shift < 0) return 0;
   for (i = 0; i < order; i++) {
      long v;
      err += lp[i]*(1<<shift);
      v = lround(err);
      if (v > qmax) v = qmax;
      else if (v < qmin) v = qmin;
      err -= v;
      q[i] = (fl_i32)v;
   }
   *shift_out = shift;
   return 1;
}

/* Apodization windows: 0 tukey(0.5), 1-2 partial_tukey(2), 3-5 punchout_tukey(3) (libFLAC
   definitions), 6 welch, 7 tukey(0.25), 8 hann. */
static void fl_window(double *w, int n, int type) {
   int i, k;
   if (type == 0 || type == 7 || type == 8) {
      double p = type == 0 ? 0.5 : type == 7 ? 0.25 : 1.0;
      int np = (int)(p/2*n);
      for (i = 0; i < n; i++) w[i] = 1;
      for (i = 0; i < np; i++) w[i] = w[n-1-i] = 0.5-0.5*cos(FL_PI*(i+1)/(np+1));
   } else if (type == 6) {
      double h = (n-1)/2.0;
      for (i = 0; i < n; i++) w[i] = 1-((i-h)/(h+1))*((i-h)/(h+1));
   } else if (type <= 2) {
      double units = 1.0/(1.0-0.1)-1.0, start = (type-1)/(2+units), end = (type+units)/(2+units);
      int sn = (int)(start*n), en = (int)(end*n), Np = (int)(0.2/2*(en-sn));
      for (i = 0; i < n; i++) w[i] = i >= sn && i < en ? 1 : 0;
      for (k = 1; k <= Np; k++) {
         if (sn+k-1 < n) w[sn+k-1] = 0.5-0.5*cos(FL_PI*k/Np);
         if (en-k >= 0 && en-k < n) w[en-k] = 0.5-0.5*cos(FL_PI*k/Np);
      }
   } else {
      double units = 1.0/(1.0-0.2)-1.0, start = (type-3)/(3+units), end = (type-2+units)/(3+units);
      int sn = (int)(start*n), en = (int)(end*n), Ns = (int)(0.2/2*sn), Ne = (int)(0.2/2*(n-en));
      for (i = 0; i < n; i++) w[i] = i >= sn && i < en ? 0 : 1;
      for (k = 1; k <= Ns; k++) {
         w[k-1] = 0.5-0.5*cos(FL_PI*k/Ns);
         if (sn-k >= 0) w[sn-k] = 0.5-0.5*cos(FL_PI*k/Ns);
      }
      for (k = 1; k <= Ne; k++) {
         if (en+k-1 < n) w[en+k-1] = 0.5-0.5*cos(FL_PI*k/Ne);
         w[n-k] = 0.5-0.5*cos(FL_PI*k/Ne);
      }
   }
}

/* Window cache keyed by (length, type). */
typedef struct {
   int n, type;
   double *w;
} fl_wcache_entry;

#define FL_WCACHE 128
typedef struct {
   fl_wcache_entry e[FL_WCACHE];
   int next;
} fl_wcache;

static const double *fl_window_get(fl_wcache *c, int n, int type) {
   int i;
   fl_wcache_entry *e;
   for (i = 0; i < FL_WCACHE; i++)
      if (c->e[i].w && c->e[i].n == n && c->e[i].type == type) return c->e[i].w;
   e = &c->e[c->next];
   c->next = (c->next+1)%FL_WCACHE;
   free(e->w);
   e->w = (double*)malloc(sizeof(double)*n);
   if (!e->w) return NULL;
   e->n = n;
   e->type = type;
   fl_window(e->w, n, type);
   return e->w;
}

static void fl_wcache_free(fl_wcache *c) {
   int i;
   for (i = 0; i < FL_WCACHE; i++) free(c->e[i].w);
}

static void fl_autocorr(const double *x, int n, int lags, double *R) {
   int k, i;
   for (k = 0; k <= lags; k++) {
      double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
      for (i = k; i+3 < n; i += 4) {
         s0 += x[i]*x[i-k];
         s1 += x[i+1]*x[i+1-k];
         s2 += x[i+2]*x[i+2-k];
         s3 += x[i+3]*x[i+3-k];
      }
      for (; i < n; i++) s0 += x[i]*x[i-k];
      R[k] = (s0+s1)+(s2+s3);
   }
}

/* Solves the weighted least squares normal equations for one order by Cholesky.
   phi: order x order matrix (row stride FL_FLAC_MAX_ORDER), psi: right side. */
static int fl_cholesky_solve(double phi[][FL_FLAC_MAX_ORDER], const double *psi, int order, double *a) {
   double L[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], y[FL_FLAC_MAX_ORDER], ridge = 0;
   int i, j, k;
   for (i = 0; i < order; i++) ridge += phi[i][i];
   ridge = ridge/order*1e-9+1e-9;
   for (i = 0; i < order; i++) {
      for (j = 0; j <= i; j++) {
         double s = phi[i][j]+(i == j ? ridge : 0);
         for (k = 0; k < j; k++) s -= L[i][k]*L[j][k];
         if (i == j) {
            if (!(s > 0)) return 0;
            L[i][i] = sqrt(s);
         } else L[i][j] = s/L[j][j];
      }
   }
   for (i = 0; i < order; i++) {
      double s = psi[i];
      for (k = 0; k < i; k++) s -= L[i][k]*y[k];
      y[i] = s/L[i][i];
   }
   for (i = order-1; i >= 0; i--) {
      double s = y[i];
      for (k = i+1; k < order; k++) s -= L[k][i]*a[k];
      a[i] = s/L[i][i];
   }
   return 1;
}

/* Weighted covariance-method LPC of the given order (weights wt). */
static int fl_lpc_ls(const double *x, const double *wt, int n, int order, double *a) {
   double phi[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], psi[FL_FLAC_MAX_ORDER];
   int i, j, t;
   if (n <= order*2) return 0;
   for (i = 0; i < order; i++) {
      psi[i] = 0;
      for (j = 0; j <= i; j++) phi[i][j] = 0;
   }
   for (t = order; t < n; t++) {
      double w = wt[t];
      const double *h = x+t-1;
      for (i = 0; i < order; i++) {
         double wi = w*h[-i];
         psi[i] += wi*x[t];
         for (j = 0; j <= i; j++) phi[i][j] += wi*h[-j];
      }
   }
   for (i = 0; i < order; i++)
      for (j = i+1; j < order; j++) phi[i][j] = phi[j][i];
   return fl_cholesky_solve(phi, psi, order, a);
}

/* Unweighted covariance method for every order up to P: phi_ij = sum x[t-1-i]x[t-1-j],
   t = P..n-1, built from its first row with the shift recursion. a[o-1] gets order o.
   Returns the highest order solved. */
static int fl_lpc_ls_all(const double *x, int n, int P, double a[][FL_FLAC_MAX_ORDER]) {
   double phi[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], psi[FL_FLAC_MAX_ORDER];
   int i, j, t, o, got = 0;
   if (n <= P*2) return 0;
   for (j = 0; j < P; j++) {
      double s0 = 0, s1 = 0;
      for (t = P; t < n; t++) {
         s0 += x[t-1]*x[t-1-j];
         s1 += x[t]*x[t-1-j];
      }
      phi[0][j] = s0;
      psi[j] = s1;
   }
   for (i = 1; i < P; i++)
      for (j = i; j < P; j++)
         phi[i][j] = phi[i-1][j-1]+x[P-1-i]*x[P-1-j]-x[n-1-i]*x[n-1-j];
   for (i = 0; i < P; i++)
      for (j = 0; j < i; j++) phi[i][j] = phi[j][i];
   for (o = 1; o <= P; o++)
      if (fl_cholesky_solve(phi, psi, o, a[o-1])) got = o;
      else break;
   return got;
}

/* ---- Subframe search ---- */
typedef struct {
   const fl_flac_cfg *cfg;
   fl_ws *ws;
   fl_wcache *wc;
   int bps, n;
   const fl_i32 *x;
   fl_u64 best_bits;   /* estimated */
   fl_sf best;
} fl_search;

/* Highest coefficient precision that keeps 32-bit decoding for <= 17 bits (libFLAC rule). */
static int fl_max_prec(int bps, int order) {
   int p = 15;
   if (bps <= 17) p = FL_MIN(p, 32-bps-fl_bitlen(order)+1);
   return FL_MAX(p, 5);
}

static int fl_default_prec(int n) {
   return n <= 192 ? 7 : n <= 384 ? 8 : n <= 576 ? 9 : n <= 1152 ? 10 : n <= 2304 ? 11 : n <= 4608 ? 12 : 13;
}

/* Evaluates quantized coefficients; keeps them if they beat the best estimate. */
static fl_u64 fl_try_lpc(fl_search *s, const fl_i32 *q, int order, int prec, int shift) {
   fl_ws *w = s->ws;
   fl_u64 bits;
   fl_lpc_sums(s->x, w->xd, s->n, q, order, fl_lpc_mode(s->bps, prec, order), w->acc);
   if (!fl_lpc_residual(s->x, s->n, order, shift, w->acc, w->res, w->u)) return ~(fl_u64)0;
   bits = 8+(fl_u64)order*s->bps+4+5+(fl_u64)order*prec+
          fl_rice_estimate(&w->rws, w->u, s->n, order, s->cfg->max_porder, s->bps > 16 ? 30 : 14);
   if (bits < s->best_bits) {
      s->best_bits = bits;
      s->best.type = 3;
      s->best.order = order;
      s->best.prec = prec;
      s->best.shift = shift;
      memcpy(s->best.coef, q, sizeof(fl_i32)*order);
   }
   return bits;
}

/* Quantizes at the default precision and nearby ones and evaluates. */
static fl_u64 fl_try_lpc_float(fl_search *s, const double *lp, int order, int range) {
   int p0 = FL_MIN(fl_default_prec(s->n), fl_max_prec(s->bps, order)), lo, hi, prec;
   fl_u64 best = ~(fl_u64)0;
   int pmax = fl_max_prec(s->bps, order);
   lo = range >= 99 ? 5 : FL_MAX(5, p0-range);
   hi = range >= 99 ? pmax : FL_MIN(pmax, p0+range);
   for (prec = lo; prec <= hi; prec++) {
      fl_i32 q[FL_FLAC_MAX_ORDER];
      int shift;
      fl_u64 b;
      if (!fl_quantize(lp, order, prec, q, &shift)) continue;
      b = fl_try_lpc(s, q, order, prec, shift);
      if (b < best) best = b;
   }
   return best;
}

/* Hill climb on the quantized coefficients of the current best LPC candidate. */
static void fl_climb(fl_search *s, int passes) {
   fl_ws *w = s->ws;
   int order = s->best.order, prec = s->best.prec, shift = s->best.shift, n = s->n, pass, j, d;
   int qmax = (1<<(prec-1))-1, qmin = -qmax-1, kmax = s->bps > 16 ? 30 : 14;
   fl_i32 q[FL_FLAC_MAX_ORDER];
   const fl_i32 *x = s->x;
   fl_u64 cur;
   if (s->best.type != 3) return;
   memcpy(q, s->best.coef, sizeof(q[0])*order);
   fl_lpc_sums(x, w->xd, n, q, order, fl_lpc_mode(s->bps, prec, order), w->acc);
   cur = s->best_bits;
   for (pass = 0; pass < passes; pass++) {
      int improved = 0;
      for (j = 0; j < order; j++) {
         for (d = -1; d <= 1; d += 2) {
            int i;
            fl_i64 bad = 0;
            fl_u64 bits;
            if (q[j]+d > qmax || q[j]+d < qmin) continue;
            for (i = order; i < n; i++) {
               fl_i64 v = x[i]-((w->acc[i]+(fl_i64)d*x[i-1-j])>>shift);
               bad |= (v+0x7FFFFFFF)>>32;
               w->u[i-order] = fl_zz((fl_i32)v);
            }
            if (bad) continue;
            bits = 8+(fl_u64)order*s->bps+4+5+(fl_u64)order*prec+
                   fl_rice_estimate(&w->rws, w->u, n, order, s->cfg->max_porder, kmax);
            if (bits < cur) {
               cur = bits;
               q[j] += d;
               for (i = order; i < n; i++) w->acc[i] += (fl_i64)d*x[i-1-j];
               improved = 1;
               break;
            }
         }
      }
      if (!improved) break;
   }
   if (cur < s->best_bits) {
      s->best_bits = cur;
      memcpy(s->best.coef, q, sizeof(q[0])*order);
   }
}

/* Partition-aware IRLS: weights 1/(|e| * mean |e| of its neighbourhood) approximate the
   Rice cost, starting from the given coefficients. */
static void fl_irls(fl_search *s, const double *start, int order, int iters) {
   fl_ws *w = s->ws;
   int n = s->n, it, i, j, seg = FL_MAX(32, n>>4);
   double a[FL_FLAC_MAX_ORDER];
   memcpy(a, start, sizeof(double)*order);
   for (it = 0; it < iters; it++) {
      /* residual of the float predictor */
      for (i = order; i < n; i++) {
         double p = 0;
         for (j = 0; j < order; j++) p += a[j]*w->xd[i-1-j];
         w->e[i] = fabs(w->xd[i]-p);
      }
      for (i = order; i < n; i += seg) {
         int end = FL_MIN(n, i+seg), k;
         double m = 0;
         for (k = i; k < end; k++) m += w->e[k];
         m = m/(end-i)+0.5;
         for (k = i; k < end; k++) w->wt[k] = 1.0/((w->e[k]+0.5)*m);
      }
      if (!fl_lpc_ls(w->xd, w->wt, n, order, a)) return;
      fl_try_lpc_float(s, a, order, s->cfg->prec_range >= 99 ? 99 : 1);
   }
}

/* Full search for one channel of one block. Writes the best subframe to *out (exact bits). */
static void fl_search_subframe(const fl_flac_cfg *cfg, fl_ws *ws, fl_wcache *wc, const fl_i32 *x0, int n, int bps, fl_sf *out) {
   fl_search s;
   fl_i32 orv = 0, *x = ws->x;
   int i, wasted = 0, order, max_order;
   fl_u64 bits;
   for (i = 1; i < n && x0[i] == x0[0]; i++);
   if (i == n) {
      out->type = 0;
      out->wasted = 0;
      out->bits = 8+bps;
      out->coef[0] = x0[0];
      return;
   }
   for (i = 0; i < n; i++) orv |= x0[i];
   while (!(orv&1) && wasted < bps-1) {
      orv >>= 1;
      wasted++;
   }
   for (i = 0; i < n; i++) {
      x[i] = x0[i]>>wasted;
      ws->xd[i] = x[i];
   }
   bps -= wasted;
   memset(&s, 0, sizeof(s));
   s.cfg = cfg;
   s.ws = ws;
   s.wc = wc;
   s.bps = bps;
   s.n = n;
   s.x = x;
   /* verbatim */
   s.best.type = 1;
   s.best_bits = 8+(fl_u64)n*bps;
   /* fixed predictors */
   for (order = 0; order <= 4 && order < n; order++) {
      if (!fl_fixed_residual(x, n, order, ws->res)) continue;
      for (i = order; i < n; i++) ws->u[i-order] = fl_zz(ws->res[i]);
      bits = 8+(fl_u64)order*bps+fl_rice_estimate(&ws->rws, ws->u, n, order, cfg->max_porder, bps > 16 ? 30 : 14);
      if (bits < s.best_bits) {
         s.best_bits = bits;
         s.best.type = 2;
         s.best.order = order;
      }
   }
   /* LPC */
   max_order = FL_MIN(cfg->max_order, n/2-1);
   if (max_order >= 1 && bps < 32) {
      double R[FL_FLAC_MAX_ORDER+1], lpc[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], err[FL_FLAC_MAX_ORDER+1];
      double best_lp[FL_FLAC_MAX_ORDER];
      int win, best_order = 0;
      fl_u64 best_lp_bits = ~(fl_u64)0;
      for (win = 0; win < cfg->nwin; win++) {
         const double *wv = fl_window_get(wc, n, win);
         int got, o;
         if (!wv) continue;
         for (i = 0; i < n; i++) ws->wx[i] = ws->xd[i]*wv[i];
         fl_autocorr(ws->wx, n, max_order, R);
         R[0] *= 1.0+1e-10;
         got = fl_levinson(R, max_order, lpc, err);
         if (got < 1) continue;
         if (cfg->all_orders) {
            for (o = 1; o <= got; o++) {
               fl_u64 b = fl_try_lpc_float(&s, lpc[o-1], o, FL_MIN(cfg->prec_range, 1));
               if (b < best_lp_bits) {
                  best_lp_bits = b;
                  best_order = o;
                  memcpy(best_lp, lpc[o-1], sizeof(double)*o);
               }
            }
         } else {
            /* the orders with the lowest estimated cost from the prediction error */
            double est[FL_FLAC_MAX_ORDER+1];
            int pick[3] = {0, 0, 0}, k;
            for (o = 1; o <= got; o++) {
               double e = err[o] > 0 ? err[o]/n : 1e-9;
               est[o] = (n-o)*0.5*log(e+1e-9)/log(2.0)+o*(bps+fl_default_prec(n));
            }
            for (k = 0; k < 3; k++) {
               double bv = 1e300;
               for (o = 1; o <= got; o++)
                  if (o != pick[0] && o != pick[1] && est[o] < bv) {
                     bv = est[o];
                     pick[k] = o;
                  }
            }
            for (k = 0; k < 3; k++) {
               fl_u64 b;
               if (!pick[k]) continue;
               b = fl_try_lpc_float(&s, lpc[pick[k]-1], pick[k], FL_MIN(cfg->prec_range, 1));
               if (b < best_lp_bits) {
                  best_lp_bits = b;
                  best_order = pick[k];
                  memcpy(best_lp, lpc[pick[k]-1], sizeof(double)*pick[k]);
               }
            }
         }
      }
      if (cfg->ls && n > 64) {
         /* exact least squares for the best order and its neighbours (all orders if asked) */
         int lo = FL_MAX(1, (best_order ? best_order : max_order)-2), hi = FL_MIN(max_order, (best_order ? best_order : max_order)+2), o, got;
         double als[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER];
         if (cfg->all_orders) {
            lo = 1;
            hi = max_order;
         }
         got = fl_lpc_ls_all(ws->xd, n, hi, als);
         for (o = lo; o <= got; o++) {
            double *a = als[o-1];
            fl_u64 b;
            b = fl_try_lpc_float(&s, a, o, FL_MIN(cfg->prec_range, 1));
            if (b < best_lp_bits) {
               best_lp_bits = b;
               best_order = o;
               memcpy(best_lp, a, sizeof(double)*o);
            }
         }
      }
      if (best_order) {
         if (cfg->prec_range > 1) fl_try_lpc_float(&s, best_lp, best_order, cfg->prec_range);
         if (cfg->irls && n > 64) fl_irls(&s, best_lp, best_order, cfg->irls);
         if (cfg->climb) fl_climb(&s, cfg->climb);
      }
   }
   /* exact size of the winner */
   *out = s.best;
   out->wasted = wasted;
   if (cfg->estimate) out->bits = s.best_bits;
   else if (s.best.type == 1) out->bits = 8+(fl_u64)n*bps;
   else {
      int ord = s.best.order;
      if (s.best.type == 2) fl_fixed_residual(x, n, ord, ws->res);
      else {
         fl_lpc_sums(x, ws->xd, n, s.best.coef, ord, fl_lpc_mode(bps, s.best.prec, ord), ws->acc);
         fl_lpc_residual(x, n, ord, s.best.shift, ws->acc, ws->res, ws->u);
      }
      for (i = ord; i < n; i++) ws->u[i-ord] = fl_zz(ws->res[i]);
      out->bits = 8+(fl_u64)ord*bps+(s.best.type == 3 ? 9+(fl_u64)ord*s.best.prec : 0)+
                  fl_rice_exact(&ws->plan, ws->ktmp, ws->u, n, ord, cfg->max_porder, bps > 14);
      if (out->bits >= 8+(fl_u64)n*bps) {
         out->type = 1;
         out->bits = 8+(fl_u64)n*bps;
      }
   }
   if (wasted) out->bits += wasted;
}

/* Writes a subframe chosen by fl_search_subframe. */
static void fl_write_subframe(fl_bw *w, const fl_flac_cfg *cfg, fl_ws *ws, const fl_i32 *x0, int n, int bps, const fl_sf *sf) {
   int i, wasted = sf->wasted, order = sf->order;
   fl_i32 *x = ws->x;
   if (sf->type == 0) {
      fl_bw_put(w, 0, 8);
      fl_bw_put_signed(w, sf->coef[0], bps);
      return;
   }
   for (i = 0; i < n; i++) x[i] = x0[i]>>wasted;
   bps -= wasted;
   fl_bw_put(w, sf->type == 1 ? 1 : sf->type == 2 ? 8+order : 31+order, 7);
   if (wasted) {
      fl_bw_put(w, 1, 1);
      fl_bw_zeros(w, (fl_u32)wasted-1);
      fl_bw_put(w, 1, 1);
   } else fl_bw_put(w, 0, 1);
   if (sf->type == 1) {
      for (i = 0; i < n; i++) fl_bw_put_signed(w, x[i], bps);
      return;
   }
   for (i = 0; i < order; i++) fl_bw_put_signed(w, x[i], bps);
   if (sf->type == 2) fl_fixed_residual(x, n, order, ws->res);
   else {
      fl_bw_put(w, sf->prec-1, 4);
      fl_bw_put(w, sf->shift, 5);
      for (i = 0; i < order; i++) fl_bw_put_signed(w, sf->coef[i], sf->prec);
      for (i = 0; i < n; i++) ws->xd[i] = x[i];
      fl_lpc_sums(x, ws->xd, n, sf->coef, order, fl_lpc_mode(bps, sf->prec, order), ws->acc);
      fl_lpc_residual(x, n, order, sf->shift, ws->acc, ws->res, ws->u);
   }
   for (i = order; i < n; i++) ws->u[i-order] = fl_zz(ws->res[i]);
   fl_rice_exact(&ws->plan, ws->ktmp, ws->u, n, order, cfg->max_porder, bps > 14);
   fl_rice_write(w, &ws->plan, ws->res, n, order);
}

/* ---- Quick cost estimate for the block size search ----
   Rectangular-window autocorrelations of any block come from prefix sums over the
   segment: P[k][i] = sum over m < i of y[m]*y[m-k], y = x scaled to at most 20 bits. */
#define FL_DP_ORDER 12

typedef struct {
   fl_i64 *P[FL_DP_ORDER+1];
   int cap;
} fl_prefix;

static int fl_prefix_build(fl_prefix *pf, const fl_i32 *x, int len, int bps) {
   int k, i, sh = bps > 20 ? bps-20 : 0;
   if (len+1 > pf->cap) {
      for (k = 0; k <= FL_DP_ORDER; k++) {
         free(pf->P[k]);
         if (!(pf->P[k] = (fl_i64*)malloc(sizeof(fl_i64)*(len+1)))) return -1;
      }
      pf->cap = len+1;
   }
   for (k = 0; k <= FL_DP_ORDER; k++) {
      fl_i64 *P = pf->P[k], acc = 0;
      for (i = 0; i <= k && i <= len; i++) P[i] = 0;
      for (i = k; i < len; i++) {
         acc += (fl_i64)(x[i]>>sh)*(x[i-k]>>sh);
         P[i+1] = acc;
      }
   }
   return 0;
}

static void fl_prefix_free(fl_prefix *pf) {
   int k;
   for (k = 0; k <= FL_DP_ORDER; k++) free(pf->P[k]);
}

static fl_u64 fl_estimate_channel(const fl_flac_cfg *cfg, fl_ws *ws, const fl_prefix *pf, const fl_i32 *seg, int off, int n, int bps) {
   const fl_i32 *x = seg+off;
   fl_u64 best = 8+(fl_u64)n*bps, b;
   int i, max_order = FL_MIN(FL_MIN(cfg->max_order, FL_DP_ORDER), n/2-1);
   int kmax = bps > 16 ? 30 : 14;
   for (i = 1; i < n && x[i] == x[0]; i++);
   if (i == n) return 8+bps;
   if (fl_fixed_residual(x, n, 2, ws->res)) {
      for (i = 2; i < n; i++) ws->u[i-2] = fl_zz(ws->res[i]);
      b = 8+2*(fl_u64)bps+fl_rice_estimate(&ws->rws, ws->u, n, 2, cfg->max_porder, kmax);
      if (b < best) best = b;
   }
   if (max_order >= 1 && bps < 32) {
      double R[FL_FLAC_MAX_ORDER+1], lpc[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], err[FL_FLAC_MAX_ORDER+1], bv = 1e300;
      int got, o, k, bo = 0;
      for (k = 0; k <= max_order; k++) R[k] = (double)(pf->P[k][off+n]-pf->P[k][off+k]);
      R[0] = R[0]*(1.0+1e-9)+1e-9;
      got = fl_levinson(R, max_order, lpc, err);
      for (o = 1; o <= got; o++) {
         double e = err[o] > 0 ? err[o]/n : 1e-9;
         double est = (n-o)*0.5*log(e+1e-9)/log(2.0)+o*(bps+fl_default_prec(n));
         if (est < bv) {
            bv = est;
            bo = o;
         }
      }
      if (bo) {
         fl_search s;
         memset(&s, 0, sizeof(s));
         s.cfg = cfg;
         s.ws = ws;
         s.bps = bps;
         s.n = n;
         s.x = x;
         s.best_bits = best;
         for (i = 0; i < n; i++) ws->xd[i] = x[i];
         fl_try_lpc_float(&s, lpc[bo-1], bo, 0);
         best = s.best_bits;
      }
   }
   return best;
}

/* ---- Encoder sink ---- */

/* Per-thread state. */
typedef struct {
   fl_ws ws;
   fl_wcache wc;
   fl_flac_dec dec;   /* verification */
   fl_bw bw;          /* frames encoded by this thread in the current segment */
} fl_worker;

/* A frame of the current segment. */
typedef struct {
   int off, len, worker, err;
   size_t pos, bytes;  /* location in the worker's bit writer */
} fl_frame_job;

typedef struct {
   fl_sink base;
   FILE *f;
   fl_flac_cfg cfg;
   fl_flac_cfg dcfg;    /* search used for block costs when cfg.dp_search > 0 */
   int level, lax, nthreads;
   int C, bits, rate, variable, stereo;
   fl_i32 *buf[8];      /* deinterleaved input not yet encoded */
   int have;            /* samples per channel in buf */
   fl_i32 *side, *mid;
   fl_u64 sample_pos, frame_no;
   fl_u32 min_frame, max_frame;
   int min_block, max_block, last_block;
   fl_md5 md5;
   long info_pos;
   fl_worker *wk;
   fl_u64 *cost;        /* block costs: cost[i*K+k-1] for k units starting at unit i */
   fl_u64 *dp_cost;
   int *dp_from;
   fl_prefix pf[8];
   fl_frame_job *jobs;
   int njobs, seg_len;
} fl_flac_sink;

static void fl_flac_streaminfo(fl_flac_sink *s, unsigned char *p, fl_u64 total, const unsigned char *md5) {
   int minb = s->min_block ? s->min_block : s->max_block;
   p[0] = (unsigned char)(minb>>8);
   p[1] = (unsigned char)minb;
   p[2] = (unsigned char)(s->max_block>>8);
   p[3] = (unsigned char)s->max_block;
   p[4] = (unsigned char)(s->min_frame>>16);
   p[5] = (unsigned char)(s->min_frame>>8);
   p[6] = (unsigned char)s->min_frame;
   p[7] = (unsigned char)(s->max_frame>>16);
   p[8] = (unsigned char)(s->max_frame>>8);
   p[9] = (unsigned char)s->max_frame;
   p[10] = (unsigned char)(s->rate>>12);
   p[11] = (unsigned char)(s->rate>>4);
   p[12] = (unsigned char)((s->rate&15)<<4|(s->C-1)<<1|((s->bits-1)>>4));
   p[13] = (unsigned char)(((s->bits-1)&15)<<4|(int)((total>>32)&15));
   p[14] = (unsigned char)(total>>24);
   p[15] = (unsigned char)(total>>16);
   p[16] = (unsigned char)(total>>8);
   p[17] = (unsigned char)total;
   memcpy(p+18, md5, 16);
}

static void fl_put_be24(unsigned char *p, fl_u32 v) {
   p[0] = (unsigned char)(v>>16);
   p[1] = (unsigned char)(v>>8);
   p[2] = (unsigned char)v;
}

/* What happens to a source tag in the FLAC file: 0 dropped, 1 comment, 2 PICTURE block.
   Tags describing an Opus encoding (encoder, R128 gains already applied) are dropped. */
static int fl_flac_tag_kind(const fl_format *f, const char *t) {
   if (strncmp(t, "METADATA_BLOCK_PICTURE=", 23) == 0) return 2;
   if (f->opus_family >= 0 && (fl_tag_is(t, "ENCODER") || fl_tag_is(t, "ENCODER_OPTIONS") ||
                               fl_tag_is(t, "R128_TRACK_GAIN") || fl_tag_is(t, "R128_ALBUM_GAIN"))) return 0;
   return 1;
}

/* Writes the metadata blocks: STREAMINFO (patched at the end), VORBIS_COMMENT (only if
   there are tags), PICTURE and other kept blocks. */
static int fl_flac_metadata_out(fl_flac_sink *s, const fl_format *f) {
   unsigned char hdr[4+4+34], zero[16];
   int i, npic = 0, ntag = 0, nblk = f->nblocks, last;
   char mask_tag[64];
   mask_tag[0] = 0;
   if (f->channel_mask) sprintf(mask_tag, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x%04X", (unsigned)f->channel_mask);
   for (i = 0; i < f->ntags; i++) {
      int k = fl_flac_tag_kind(f, f->tags[i]);
      npic += k == 2;
      ntag += k == 1;
   }
   if (mask_tag[0]) ntag++;
   memcpy(hdr, "fLaC", 4);
   last = !ntag && !npic && !nblk;
   hdr[4] = (unsigned char)(last ? 0x80 : 0);
   fl_put_be24(hdr+5, 34);
   memset(zero, 0, 16);
   fl_flac_streaminfo(s, hdr+8, 0, zero);
   if (fwrite(hdr, 1, sizeof(hdr), s->f) != sizeof(hdr)) return FL_ERR_IO;
   s->info_pos = 8;
   if (ntag) {
      const char *vendor = "flapus";
      unsigned char n4[4];
      fl_u32 len = 4+(fl_u32)strlen(vendor)+4;
      for (i = 0; i < f->ntags; i++)
         if (fl_flac_tag_kind(f, f->tags[i]) == 1) len += 4+(fl_u32)strlen(f->tags[i]);
      if (mask_tag[0]) len += 4+(fl_u32)strlen(mask_tag);
      last = !npic && !nblk;
      n4[0] = (unsigned char)(4|(last ? 0x80 : 0));
      fl_put_be24(n4+1, len);
      fwrite(n4, 1, 4, s->f);
      fl_put_le32(n4, (fl_u32)strlen(vendor));
      fwrite(n4, 1, 4, s->f);
      fwrite(vendor, 1, strlen(vendor), s->f);
      fl_put_le32(n4, ntag);
      fwrite(n4, 1, 4, s->f);
      for (i = 0; i < f->ntags; i++)
         if (fl_flac_tag_kind(f, f->tags[i]) == 1) {
            fl_put_le32(n4, (fl_u32)strlen(f->tags[i]));
            fwrite(n4, 1, 4, s->f);
            fwrite(f->tags[i], 1, strlen(f->tags[i]), s->f);
         }
      if (mask_tag[0]) {
         fl_put_le32(n4, (fl_u32)strlen(mask_tag));
         fwrite(n4, 1, 4, s->f);
         fwrite(mask_tag, 1, strlen(mask_tag), s->f);
      }
   }
   for (i = 0; i < f->ntags; i++)
      if (strncmp(f->tags[i], "METADATA_BLOCK_PICTURE=", 23) == 0) {
         const char *b64 = f->tags[i]+23;
         int l = (int)strlen(b64), got;
         unsigned char *pic = (unsigned char*)malloc(l/4*3+3), h4[4];
         if (!pic) return FL_ERR_ALLOC;
         got = fl_base64_decode(b64, l, pic);
         npic--;
         if (got > 0 && got < (1<<24)) {
            h4[0] = (unsigned char)(6|(!npic && !nblk ? 0x80 : 0));
            fl_put_be24(h4+1, (fl_u32)got);
            fwrite(h4, 1, 4, s->f);
            fwrite(pic, 1, got, s->f);
         }
         free(pic);
      }
   for (i = 0; i < nblk; i++) {
      unsigned char h4[4];
      h4[0] = (unsigned char)(f->blocks[i].type|(i == nblk-1 ? 0x80 : 0));
      fl_put_be24(h4+1, (fl_u32)f->blocks[i].len);
      fwrite(h4, 1, 4, s->f);
      fwrite(f->blocks[i].data, 1, f->blocks[i].len, s->f);
   }
   return ferror(s->f) ? FL_ERR_IO : FL_OK;
}

static int fl_flacenc_start(fl_sink *sk, const fl_format *f) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int c, t, slen;
   if (f->is_float) return FL_ERR_UNSUPPORTED;  /* FLAC is integer PCM only */
   if (f->channels < 1 || f->channels > 8 || f->bits < 4 || f->bits > 32 || f->rate < 1 || f->rate > 1048575) return FL_ERR_UNSUPPORTED;
   s->C = f->channels;
   s->bits = f->bits;
   s->rate = f->rate;
   s->stereo = s->C == 2 && s->bits < 32;  /* the side channel needs bits+1 <= 32 */
   fl_flac_level(&s->cfg, s->level, s->lax, s->rate);
   if (s->cfg.dp_search) {
      fl_flac_level(&s->dcfg, 0, s->lax, s->rate);
      s->dcfg.max_porder = s->cfg.max_porder;
      s->dcfg.max_order = s->cfg.max_order;
      s->dcfg.estimate = 1;
   }
   s->variable = s->cfg.unit > 0;
   if (s->cfg.unit) {
      int M = FL_FLAC_SEGMENT/s->cfg.unit+2, K = s->cfg.max_block/s->cfg.unit;
      s->dp_cost = (fl_u64*)malloc(sizeof(fl_u64)*(M+1));
      s->dp_from = (int*)malloc(sizeof(int)*(M+1));
      s->cost = (fl_u64*)malloc(sizeof(fl_u64)*M*K);
      if (!s->dp_cost || !s->dp_from || !s->cost) return FL_ERR_ALLOC;
   }
   s->jobs = (fl_frame_job*)malloc(sizeof(fl_frame_job)*(FL_FLAC_SEGMENT/16+2));
   s->wk = (fl_worker*)calloc(s->nthreads, sizeof(fl_worker));
   if (!s->jobs || !s->wk) return FL_ERR_ALLOC;
   fl_flac_crc_init();
   fl_md5_init(&s->md5);
   slen = FL_FLAC_SEGMENT+s->cfg.max_block+FL_SLACK;
   for (c = 0; c < s->C; c++)
      if (!(s->buf[c] = (fl_i32*)calloc(slen, sizeof(fl_i32)))) return FL_ERR_ALLOC;
   s->side = (fl_i32*)calloc(slen, sizeof(fl_i32));
   s->mid = (fl_i32*)calloc(slen, sizeof(fl_i32));
   if (!s->side || !s->mid) return FL_ERR_ALLOC;
   for (t = 0; t < s->nthreads; t++) {
      fl_worker *w = &s->wk[t];
      w->dec.si.channels = s->C;
      w->dec.si.bits = s->bits;
      w->dec.si.rate = s->rate;
      if (fl_ws_alloc(&w->ws, s->cfg.max_block, s->cfg.max_porder) < 0 || fl_flac_dec_alloc(&w->dec, s->cfg.max_block) < 0)
         return FL_ERR_ALLOC;
   }
   s->min_frame = 0xFFFFFF;
   return fl_flac_metadata_out(s, f);
}

/* Frame header for frame/sample number num; returns its length in bytes. */
static int fl_frame_header(fl_flac_sink *s, unsigned char *h, int bs, int assignment, fl_u64 num) {
   static const int rate_codes[12] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
   int pos = 4, bcode, rcode = 0, scode, i, extra_b = 0, extra_r = 0;
   if (bs == 192) bcode = 1;
   else if (bs == 576 || bs == 1152 || bs == 2304 || bs == 4608) bcode = 2+fl_ilog(bs/576)-1;
   else if (bs >= 256 && bs <= 32768 && (bs&(bs-1)) == 0) bcode = 8+fl_ilog(bs/256)-1;
   else if (bs <= 256) { bcode = 6; extra_b = 1; }
   else { bcode = 7; extra_b = 2; }
   for (i = 1; i < 12; i++)
      if (rate_codes[i] == s->rate) rcode = i;
   if (!rcode) {
      if (s->rate%1000 == 0 && s->rate/1000 <= 255) { rcode = 12; extra_r = 1; }
      else if (s->rate <= 65535) { rcode = 13; extra_r = 2; }
      else if (s->rate%10 == 0 && s->rate/10 <= 65535) { rcode = 14; extra_r = 2; }
   }
   switch (s->bits) {
   case 8: scode = 1; break;
   case 12: scode = 2; break;
   case 16: scode = 4; break;
   case 20: scode = 5; break;
   case 24: scode = 6; break;
   case 32: scode = 7; break;
   default: scode = 0; break;
   }
   h[0] = 0xFF;
   h[1] = (unsigned char)(0xF8|s->variable);
   h[2] = (unsigned char)(bcode<<4|rcode);
   h[3] = (unsigned char)(assignment<<4|scode<<1);
   if (num < 0x80) h[pos++] = (unsigned char)num;
   else {
      int len = 1;
      while (len < 6 && num >= ((fl_u64)1<<(5*len+6))) len++;
      h[pos++] = (unsigned char)(((0xFF<<(7-len))&0xFF)|(num>>(6*len)));
      for (i = len-1; i >= 0; i--) h[pos++] = (unsigned char)(0x80|((num>>(6*i))&0x3F));
   }
   if (extra_b == 1) h[pos++] = (unsigned char)(bs-1);
   else if (extra_b == 2) {
      h[pos++] = (unsigned char)((bs-1)>>8);
      h[pos++] = (unsigned char)(bs-1);
   }
   if (extra_r == 1) h[pos++] = (unsigned char)(s->rate/1000);
   else if (extra_r == 2) {
      int v = rcode == 13 ? s->rate : s->rate/10;
      h[pos++] = (unsigned char)(v>>8);
      h[pos++] = (unsigned char)v;
   }
   h[pos] = (unsigned char)fl_crc8(h, pos);
   return pos+1;
}

/* Header bits for the block size search (sample number assumed 4 bytes). */
static int fl_frame_header_bits(int bs) {
   int extra = 0;
   if (!(bs == 192 || bs == 576 || bs == 1152 || bs == 2304 || bs == 4608 || (bs >= 256 && bs <= 32768 && (bs&(bs-1)) == 0)))
      extra = bs <= 256 ? 8 : 16;
   return (4+4+1)*8+extra+16+4;
}

/* Encodes one frame into the worker's bit writer and verifies it by decoding. */
static int fl_encode_frame(fl_flac_sink *s, fl_worker *wk, fl_frame_job *job, fl_u64 num) {
   fl_sf sf[8], sfx[4];
   fl_bw *w = &wk->bw;
   const fl_i32 *ch[8];
   int c, i, assignment, hl, n, off = job->off, bs = job->len;
   size_t start, flen;
   unsigned char h[16];
   for (c = 0; c < s->C; c++) ch[c] = s->buf[c]+off;
   assignment = s->C-1;
   if (s->stereo) {
      /* compare left/right, left/side, side/right and mid/side */
      const fl_i32 *sd = s->side+off, *md = s->mid+off;
      fl_u64 costs[4];
      static const int asg[4] = {1, 8, 9, 10};
      int m = 0;
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[0], bs, s->bits, &sfx[0]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[1], bs, s->bits, &sfx[1]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, sd, bs, s->bits+1, &sfx[2]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, md, bs, s->bits, &sfx[3]);
      costs[0] = sfx[0].bits+sfx[1].bits;
      costs[1] = sfx[0].bits+sfx[2].bits;
      costs[2] = sfx[2].bits+sfx[1].bits;
      costs[3] = sfx[3].bits+sfx[2].bits;
      for (i = 1; i < 4; i++)
         if (costs[i] < costs[m]) m = i;
      assignment = asg[m];
      sf[0] = m == 2 ? sfx[2] : m == 3 ? sfx[3] : sfx[0];
      sf[1] = m == 0 || m == 2 ? sfx[1] : sfx[2];
      if (m == 2) ch[0] = sd;
      if (m == 3) ch[0] = md;
      if (m == 1 || m == 3) ch[1] = sd;
   } else {
      for (c = 0; c < s->C; c++) fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[c], bs, s->bits, &sf[c]);
   }
   hl = fl_frame_header(s, h, bs, assignment, num);
   {
      size_t bound = 16+2;
      for (c = 0; c < s->C; c++) bound += (size_t)bs*33/8+64;
      if (fl_bw_reserve(w, bound) < 0) return FL_ERR_ALLOC;
   }
   start = w->len;
   memcpy(w->buf+w->len, h, hl);
   w->len += hl;
   for (c = 0; c < s->C; c++) {
      int side = (assignment == 8 && c == 1) || (assignment == 9 && c == 0) || (assignment == 10 && c == 1);
      fl_write_subframe(w, &s->cfg, &wk->ws, ch[c], bs, s->bits+side, &sf[c]);
   }
   fl_bw_align(w);
   {
      unsigned crc = fl_crc16(w->buf+start, w->len-start);
      w->buf[w->len++] = (unsigned char)(crc>>8);
      w->buf[w->len++] = (unsigned char)crc;
   }
   flen = w->len-start;
   /* decode the frame again and compare with the input */
   if (fl_flac_decode_frame(&wk->dec, w->buf+start, flen, &n) != flen || n != bs) return FL_ERR_INTERNAL;
   for (c = 0; c < s->C; c++) {
      const fl_i32 *x = s->buf[c]+off;
      for (i = 0; i < bs; i++)
         if (wk->dec.out[i*s->C+c] != x[i]) return FL_ERR_INTERNAL;
   }
   job->pos = start;
   job->bytes = flen;
   return FL_OK;
}

/* Estimated cost of a block for the block size search. */
static fl_u64 fl_block_cost(fl_flac_sink *s, fl_worker *wk, int off, int bs) {
   fl_u64 cost = fl_frame_header_bits(bs);
   int c;
   if (s->cfg.dp_search) {
      fl_sf sf;
      fl_u64 v[4];
      if (s->stereo) {
         const fl_i32 *src[4];
         src[0] = s->buf[0];
         src[1] = s->buf[1];
         src[2] = s->side;
         src[3] = s->mid;
         for (c = 0; c < 4; c++) {
            fl_search_subframe(&s->dcfg, &wk->ws, &wk->wc, src[c]+off, bs, s->bits+(c == 2), &sf);
            v[c] = sf.bits;
         }
         return cost+FL_MIN(FL_MIN(v[0]+v[1], v[0]+v[2]), FL_MIN(v[2]+v[1], v[3]+v[2]));
      }
      for (c = 0; c < s->C; c++) {
         fl_search_subframe(&s->dcfg, &wk->ws, &wk->wc, s->buf[c]+off, bs, s->bits, &sf);
         cost += sf.bits;
      }
      return cost;
   }
   if (s->stereo) {
      fl_u64 a = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[0], s->buf[0], off, bs, s->bits);
      fl_u64 b = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[1], s->buf[1], off, bs, s->bits);
      fl_u64 sc = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[2], s->side, off, bs, s->bits+1);
      fl_u64 m = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[3], s->mid, off, bs, s->bits);
      cost += FL_MIN(FL_MIN(a+b, a+sc), FL_MIN(sc+b, m+sc));
   } else {
      for (c = 0; c < s->C; c++) cost += fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[c], s->buf[c], off, bs, s->bits);
   }
   return cost;
}

/* Block lengths (in units) tried by the search. */
static int fl_dp_len_ok(const fl_flac_cfg *c, int k) {
   int len = k*c->unit;
   return !c->coarse || len <= c->coarse || len%c->coarse_step == 0;
}

/* Thread body: block costs for units t, t+nt, ... */
static void fl_cost_job(void *ctx, int t, int nt) {
   fl_flac_sink *s = (fl_flac_sink*)ctx;
   int u = s->cfg.unit, K = s->cfg.max_block/u, len = s->seg_len, M = (len+u-1)/u, i, k;
   for (i = t; i < M; i += nt)
      for (k = 1; k <= K && i+k <= M; k++) {
         int off = i*u, bs = FL_MIN(k*u, len-off);
         if (fl_dp_len_ok(&s->cfg, k) || i+k == M) s->cost[(size_t)i*K+k-1] = fl_block_cost(s, &s->wk[t], off, bs);
      }
}

/* Thread body: frames t, t+nt, ... */
static void fl_frame_job_run(void *ctx, int t, int nt) {
   fl_flac_sink *s = (fl_flac_sink*)ctx;
   int j;
   for (j = t; j < s->njobs; j += nt) {
      fl_frame_job *job = &s->jobs[j];
      fl_u64 num = s->variable ? s->sample_pos+job->off : s->frame_no+j;
      job->worker = t;
      job->err = fl_encode_frame(s, &s->wk[t], job, num);
   }
}

/* Encodes the first 'len' buffered samples and shifts the rest down. */
static int fl_flac_process(fl_flac_sink *s, int len) {
   int ret = FL_OK, c, i, j, pos = 0;
   if (len <= 0) return FL_OK;
   s->seg_len = len;
   s->njobs = 0;
   if (s->stereo) {
      const fl_i32 *l = s->buf[0], *r = s->buf[1];
      for (i = 0; i < len; i++) {
         s->side[i] = l[i]-r[i];
         s->mid[i] = (fl_i32)(((fl_i64)l[i]+r[i])>>1);
      }
   }
   if (!s->cfg.unit) {
      for (pos = 0; pos < len; pos += s->cfg.fixed_block) {
         s->jobs[s->njobs].off = pos;
         s->jobs[s->njobs++].len = FL_MIN(s->cfg.fixed_block, len-pos);
      }
   } else {
      /* block sizes by dynamic programming over a grid of 'unit' samples */
      int u = s->cfg.unit, K = s->cfg.max_block/u, M = (len+u-1)/u, k, ncut = 0;
      fl_u64 *best = s->dp_cost;
      int *from = s->dp_from, *cuts = (int*)malloc(sizeof(int)*(M+1));
      if (!cuts) return FL_ERR_ALLOC;
      if (!s->cfg.dp_search) {
         int fail = 0;
         if (s->stereo) {
            fail |= fl_prefix_build(&s->pf[0], s->buf[0], len, s->bits);
            fail |= fl_prefix_build(&s->pf[1], s->buf[1], len, s->bits);
            fail |= fl_prefix_build(&s->pf[2], s->side, len, s->bits+1);
            fail |= fl_prefix_build(&s->pf[3], s->mid, len, s->bits);
         } else
            for (c = 0; c < s->C; c++) fail |= fl_prefix_build(&s->pf[c], s->buf[c], len, s->bits);
         if (fail) {
            free(cuts);
            return FL_ERR_ALLOC;
         }
      }
      fl_parallel(fl_cost_job, s, FL_MIN(s->nthreads, M));
      best[0] = 0;
      for (i = 1; i <= M; i++) best[i] = ~(fl_u64)0;
      for (i = 0; i < M; i++)
         for (k = 1; k <= K && i+k <= M; k++) {
            fl_u64 cst;
            if (!fl_dp_len_ok(&s->cfg, k) && i+k != M) continue;
            cst = best[i]+s->cost[(size_t)i*K+k-1];
            if (cst < best[i+k]) {
               best[i+k] = cst;
               from[i+k] = i;
            }
         }
      for (i = M; i > 0; i = from[i]) cuts[ncut++] = i;
      for (k = ncut-1; k >= 0; k--) {
         int end = FL_MIN(cuts[k]*u, len);
         s->jobs[s->njobs].off = pos;
         s->jobs[s->njobs++].len = end-pos;
         pos = end;
      }
      free(cuts);
   }
   fl_parallel(fl_frame_job_run, s, FL_MIN(s->nthreads, s->njobs));
   /* write the frames in order */
   for (j = 0; j < s->njobs && ret == FL_OK; j++) {
      fl_frame_job *job = &s->jobs[j];
      fl_u32 flen = (fl_u32)job->bytes;
      if ((ret = job->err) != FL_OK) break;
      if (fwrite(s->wk[job->worker].bw.buf+job->pos, 1, flen, s->f) != flen) ret = FL_ERR_IO;
      if (flen < s->min_frame) s->min_frame = flen;
      if (flen > s->max_frame) s->max_frame = flen;
      if (job->len > s->max_block) s->max_block = job->len;
      if (s->last_block && (s->min_block == 0 || s->last_block < s->min_block)) s->min_block = s->last_block;
      s->last_block = job->len;
   }
   for (i = 0; i < s->nthreads; i++) s->wk[i].bw.len = 0;
   s->sample_pos += len;
   s->frame_no += s->njobs;
   s->base.out_bytes = ftell(s->f);
   s->base.out_frames = (fl_i64)s->sample_pos;
   for (c = 0; c < s->C; c++) memmove(s->buf[c], s->buf[c]+len, sizeof(fl_i32)*(s->have-len));
   s->have -= len;
   return ret;
}

static int fl_flacenc_write(fl_sink *sk, const fl_i32 *pcm, int n) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int C = s->C, i, c, shift = 32-s->bits;
   for (i = 0; i < n*C; i++) {
      /* samples must fit in the declared bits */
      if (shift && (fl_i32)((fl_u32)pcm[i]<<shift)>>shift != pcm[i]) return FL_ERR_BAD_FILE;
   }
   fl_md5_samples(&s->md5, pcm, (size_t)n*C, s->bits);
   while (n > 0) {
      int k = FL_MIN(n, FL_FLAC_SEGMENT-s->have), ret;
      for (c = 0; c < C; c++) {
         fl_i32 *d = s->buf[c]+s->have;
         for (i = 0; i < k; i++) d[i] = pcm[i*C+c];
      }
      s->have += k;
      pcm += (size_t)k*C;
      n -= k;
      if (s->have == FL_FLAC_SEGMENT &&
          (ret = fl_flac_process(s, s->cfg.unit ? FL_FLAC_SEGMENT : FL_FLAC_SEGMENT/s->cfg.fixed_block*s->cfg.fixed_block)) != FL_OK)
         return ret;
   }
   return FL_OK;
}

static int fl_flacenc_finish(fl_sink *sk) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   unsigned char info[34], md5[16];
   int ret = fl_flac_process(s, s->have);
   if (ret != FL_OK) return ret;
   fl_md5_final(&s->md5, md5);
   if (s->frame_no == 0) s->min_frame = 0;
   if (!s->cfg.unit) {
      s->min_block = s->max_block = s->cfg.fixed_block;
   } else {
      if (s->min_block == 0) s->min_block = s->last_block;
      if (s->max_block < 16) s->max_block = 16;
      if (s->min_block < 16) s->min_block = 16;
   }
   fl_flac_streaminfo(s, info, s->sample_pos, md5);
   if (fseek(s->f, s->info_pos, SEEK_SET) != 0 || fwrite(info, 1, 34, s->f) != 34) return FL_ERR_IO;
   ret = fclose(s->f) != 0 ? FL_ERR_IO : FL_OK;
   s->f = NULL;
   return ret;
}

static void fl_flacenc_destroy(fl_sink *sk) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int c;
   if (s->f) fclose(s->f);
   for (c = 0; c < 8; c++) {
      free(s->buf[c]);
      fl_prefix_free(&s->pf[c]);
   }
   free(s->side);
   free(s->mid);
   free(s->cost);
   free(s->dp_cost);
   free(s->dp_from);
   free(s->jobs);
   if (s->wk)
      for (c = 0; c < s->nthreads; c++) {
         fl_ws_free(&s->wk[c].ws);
         fl_wcache_free(&s->wk[c].wc);
         fl_flac_dec_free(&s->wk[c].dec);
         free(s->wk[c].bw.buf);
      }
   free(s->wk);
   free(s);
}

static fl_sink *fl_flac_sink_create(const char *path, const fl_options *opt, int *err) {
   fl_flac_sink *s = (fl_flac_sink*)calloc(1, sizeof(*s));
   if (!s) {
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   /* the configuration is completed in start(), when the rate is known */
   s->level = opt->flac_level;
   s->lax = opt->flac_lax;
   s->nthreads = opt->threads > 0 ? opt->threads : opt->threads == 0 ? fl_cpu_count() : 1;
   if (s->nthreads > FL_MAX_THREADS) s->nthreads = FL_MAX_THREADS;
   if (s->nthreads < 1) s->nthreads = 1;
   s->base.start = fl_flacenc_start;
   s->base.write = fl_flacenc_write;
   s->base.finish = fl_flacenc_finish;
   s->base.destroy = fl_flacenc_destroy;
   if (!(s->f = fl_fopen(path, "wb"))) {
      free(s);
      *err = FL_ERR_IO;
      return NULL;
   }
   return &s->base;
}

#ifdef FLAPUS_HAVE_ENCODER

/* ======== enc_x87.c ======== */
/* ---- x87 extended-precision helpers for the encoder ----
   The reference encoder is a 32-bit x87 build: float/double values stay in
   80-bit registers until they are stored. We model register values with
   long double (xf) and round explicitly where the reference stores to memory.
   The math functions below reproduce the MinGW (i686) libm code exactly. */
typedef long double xf;
#define XF(v) ((xf)(v))

#if !(defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
#error "The bit-exact encoder needs GCC/Clang on x86 or x86-64 (80-bit long double and x87 inline asm)."
#endif

/* Sets the x87 control word to 64-bit precision, round to nearest (MinGW default). */
static unsigned short fl_x87_enter(void) {
   unsigned short old, cw = 0x037F;
   __asm__ volatile("fnstcw %0" : "=m"(old));
   __asm__ volatile("fldcw %0" : : "m"(cw));
   return old;
}
static void fl_x87_leave(unsigned short old) { __asm__ volatile("fldcw %0" : : "m"(old)); }

#ifdef FLAPUS_PORTABLE_MATH
/* ---- Portable math (FLAPUS_PORTABLE_MATH) ----
   The x87 transcendental instructions fsin, fcos, fyl2x, fyl2xp1, f2xm1 (and
   fpatan inside MinGW's acosl) are not exactly specified: their last bit is
   up to the CPU. Measured over 20000 fixed inputs, an Intel Core i7-13700K
   and an AMD EPYC 7763 disagreed for all five. (On a 3179-file speech corpus
   that changed no packet; rcpps/rsqrtps did, see enc_bands.c.)
   With FLAPUS_PORTABLE_MATH they are replaced by the functions below, which
   only use the operations IEEE 754 defines exactly (+, -, *, /) on long
   double, plus exact exponent manipulation. The output is then the same on
   every x86 CPU. It is no longer bit-identical to opusenc: where opusenc's
   x87 result and ours round differently, a frame can take another decision.
   Everything else (fsqrt, frndint, fscale, the stores) stays as it is. */

/* x = m * 2^e with m in [0.5, 1), exactly, through the 80-bit layout. x > 0. */
static xf fl_p_frexp(xf x, int *e) {
   unsigned char b[16];
   int ex, adj = 0;
   memcpy(b, &x, sizeof(xf));
   ex = (b[8] | (b[9] << 8)) & 0x7fff;
   if (ex == 0) {  /* subnormal: scale up by 2^64 first (exact) */
      x *= 18446744073709551616.0L;
      adj = 64;
      memcpy(b, &x, sizeof(xf));
      ex = (b[8] | (b[9] << 8)) & 0x7fff;
   }
   *e = ex-16382-adj;
   b[8] = 0xfe;
   b[9] = (unsigned char)((b[9] & 0x80) | 0x3f);
   memcpy(&x, b, sizeof(xf));
   return x;
}

/* 2^n exactly, |n| < 16382. */
static xf fl_p_pow2(int n) {
   unsigned char b[16];
   xf x = 1.0L;
   int ex = 16383+n;
   memcpy(b, &x, sizeof(xf));
   b[8] = (unsigned char)(ex & 0xff);
   b[9] = (unsigned char)((ex >> 8) & 0x7f);
   memcpy(&x, b, sizeof(xf));
   return x;
}

/* Nearest integer, halves away from zero; exact for |t| < 2^62. */
static long long fl_p_nint(xf t) { return (long long)(t < 0 ? t-0.5L : t+0.5L); }

#define FL_P_LN2     0.693147180559945309417232121458176568L
#define FL_P_LN2_HI  0x1.62e42fee00000p-1L       /* 32 bits: e*hi is exact */
#define FL_P_LN2_LO  0x1.a39ef35793c76p-33L
#define FL_P_LOG2E   1.442695040888963407359924681001892137L
#define FL_P_LOG10E  0.434294481903251827651128918916605082L
#define FL_P_PI      3.141592653589793238462643383279502884L
#define FL_P_2_PI    0.636619772367581343075535053490057448L
#define FL_P_PIO2_1  0x1.921fb544p0L             /* pi/2 in three parts, the first */
#define FL_P_PIO2_2  0x1.0b4611a6p-34L           /* two with 33 bits so k*part is */
#define FL_P_PIO2_3  2.02226624879595063154e-21L /* exact for the k used here */
#define FL_P_SQRT3   1.732050807568877293527446341505872367L

/* ln((1+s)/(1-s)) = 2*atanh(s) for |s| <= 0.172: odd series up to s^25. */
static xf fl_p_lnser(xf s) {
   xf z = s*s, p = 1.0L/25;
   int k;
   for (k = 23; k >= 3; k -= 2) p = p*z+1.0L/k;
   return 2*(s+s*z*p);
}
/* ln(x), x > 0: x = m*2^e with m in [sqrt(1/2), sqrt(2)), ln(m) from the series. */
static xf fl_p_ln_parts(xf x, int *e) {
   xf m = fl_p_frexp(x, e);
   if (m < 0.707106781186547524400844362104849039L) {
      m *= 2;
      (*e)--;
   }
   return fl_p_lnser((m-1)/(m+1));
}
static xf fl_p_ln(xf x) {
   int e;
   xf l = fl_p_ln_parts(x, &e);
   return XF(e)*FL_P_LN2_HI+(XF(e)*FL_P_LN2_LO+l);
}
static xf fl_p_log2(xf x) {
   int e;
   xf l = fl_p_ln_parts(x, &e);
   return XF(e)+l*FL_P_LOG2E;
}
/* ln(1+x) for |x| <= 0.29, as fyl2xp1 is used: (1+s)/(1-s) = 1+x. */
static xf fl_p_ln1p(xf x) { return fl_p_lnser(x/(2+x)); }

/* e^t-1 for |t| <= 0.7: t*(1+t/2*(1+t/3*(...(1+t/22)))). */
static xf fl_p_expm1(xf t) {
   xf p = 1;
   int k;
   for (k = 22; k >= 2; k--) p = 1+t*p/k;
   return t*p;
}
/* 2^x-1 for |x| <= 1, as f2xm1. */
static xf fl_p_f2xm1(xf x) { return fl_p_expm1(x*FL_P_LN2); }

/* sin and cos for |r| <= pi/4, series up to r^23 and r^22. */
static xf fl_p_sin_k(xf r) {
   xf z = r*r, p = 1;
   int k;
   for (k = 22; k >= 2; k -= 2) p = 1-z*p/(XF(k)*(k+1));
   return r*p;
}
static xf fl_p_cos_k(xf r) {
   xf z = r*r, p = 1;
   int k;
   for (k = 21; k >= 1; k -= 2) p = 1-z*p/(XF(k)*(k+1));
   return p;
}
/* Reduces x by the nearest multiple k of pi/2 (Cody-Waite); *q = k mod 4. */
static xf fl_p_reduce(xf x, int *q) {
   long long k = fl_p_nint(x*FL_P_2_PI);
   xf kf = XF(k);
   *q = (int)(k & 3);
   return ((x-kf*FL_P_PIO2_1)-kf*FL_P_PIO2_2)-kf*FL_P_PIO2_3;
}
static xf fl_p_sin(xf x) {
   int q;
   xf r = fl_p_reduce(x, &q);
   return q == 0 ? fl_p_sin_k(r) : q == 1 ? fl_p_cos_k(r) : q == 2 ? -fl_p_sin_k(r) : -fl_p_cos_k(r);
}
static xf fl_p_cos(xf x) {
   int q;
   xf r = fl_p_reduce(x, &q);
   return q == 0 ? fl_p_cos_k(r) : q == 1 ? -fl_p_sin_k(r) : q == 2 ? -fl_p_cos_k(r) : fl_p_sin_k(r);
}

/* atan: a > 1 is folded with pi/2-atan(1/a), a > tan(pi/12) with
   pi/6+atan((a*sqrt3-1)/(a+sqrt3)); then the series up to a^37. */
static xf fl_p_atan(xf a) {
   int neg = a < 0, inv = 0, k;
   xf off = 0, z, s = 0;
   if (neg) a = -a;
   if (a > 1) {
      a = 1/a;
      inv = 1;
   }
   if (a > 0.267949192431122706472553658494127633L) {
      a = (a*FL_P_SQRT3-1)/(a+FL_P_SQRT3);
      off = FL_P_PI/6;
   }
   z = a*a;
   for (k = 37; k >= 1; k -= 2) s = 1.0L/k-z*s;
   s = a*s+off;
   if (inv) s = FL_P_PI/2-s;
   return neg ? -s : s;
}
/* acos(x) = 2*atan(sqrt((1-x)/(1+x))); fsqrt is exact in IEEE terms. */
static xf fl_p_acos(xf x) {
   xf v;
   if (x <= -1) return FL_P_PI;
   if (x >= 1) return 0;
   v = (1-x)/(1+x);
   __asm__("fsqrt" : "+t"(v));
   return 2*fl_p_atan(v);
}
#endif /* FLAPUS_PORTABLE_MATH */

static xf fl_x87_sqrt(xf x) {
   __asm__("fsqrt" : "+t"(x));
   return x;
}
/* MinGW sqrt(double): fsqrt of the double argument, unrounded result. */
static xf fl_m_sqrt(double d) {
   if (d == 1.0) return 1.0L;
   return fl_x87_sqrt(XF(d));
}
/* MinGW sqrtf(float): fsqrt of the float argument, unrounded result. */
static xf fl_m_sqrtf(float f) {
   if (f == 1.0f) return 1.0L;
   return fl_x87_sqrt(XF(f));
}
static xf fl_m_floor(double d) { return floorl(XF(d)); }
static xf fl_m_floorf(float f) { return floorl(XF(f)); }

#ifdef FLAPUS_PORTABLE_MATH
static double fl_m_sin(double d) { return (double)fl_p_sin(XF(d)); }
static double fl_m_cos(double d) { return (double)fl_p_cos(XF(d)); }
#else
static xf fl_x87_fsin(xf x) {
   __asm__("fsin" : "+t"(x));
   return x;
}
static xf fl_x87_fcos(xf x) {
   __asm__("fcos" : "+t"(x));
   return x;
}
/* MinGW sin/cos(double): fsin/fcos rounded to double (arguments are small here). */
static double fl_m_sin(double d) { return (double)fl_x87_fsin(XF(d)); }
static double fl_m_cos(double d) { return (double)fl_x87_fcos(XF(d)); }
#endif

/* MinGW log(double) (__logl_internal): fyl2xp1 near 1, fyl2x otherwise; rounded to double. */
static double fl_m_log(double d) {
   xf x = XF(d), r;
   xf xm1 = x-1.0L;
#ifdef FLAPUS_PORTABLE_MATH
   r = fabsl(xm1) > XF(0.29) ? fl_p_ln(x) : fl_p_ln1p(xm1);
#else
   if (fabsl(xm1) > XF(0.29)) {
      __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   } else {
      __asm__("fldln2\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"(xm1) : "st(1)");
   }
#endif
   return (double)r;
}

#ifdef FLAPUS_PORTABLE_MATH
/* exp(d) = 2^n * e^r with n = nint(d*log2(e)), r = d-n*ln2 (Cody-Waite). */
static double fl_m_exp(double d) {
   xf x = XF(d), r;
   long long n;
   if (d > 709.782712893384 || d < -745.1332191019411) return d > 0 ? HUGE_VAL : 0.0;
   if (d == 0) return 1.0;
   n = fl_p_nint(x*FL_P_LOG2E);
   r = (x-XF(n)*FL_P_LN2_HI)-XF(n)*FL_P_LN2_LO;
   return (double)((1+fl_p_expm1(r))*fl_p_pow2((int)n));
}
#else
/* MinGW exp(double): transcription of the i686 code (normal range only). */
static double fl_m_exp(double d) {
   static const unsigned char c1b[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xb8, 0xff, 0x3f};
   static const unsigned char c2b[10] = {0x20, 0xfa, 0xee, 0xc2, 0x5f, 0x70, 0xa5, 0xec, 0xed, 0x3f};
   xf x = XF(d), r, c1, c2;
   unsigned short cw0, cw1;
   if (d > 709.782712893384 || d < -745.1332191019411) return d > 0 ? HUGE_VAL : 0.0;
   if (d == 0) return 1.0;
   memcpy(&c1, c1b, 10);
   memcpy(&c2, c2b, 10);
   __asm__(
      "fldl2e\n\t"
      "fmul %%st(1),%%st\n\t"
      "fnstcw %2\n\t"
      "movzwl %2,%%eax\n\t"
      "or $0xc,%%ah\n\t"
      "mov %%ax,%3\n\t"
      "fldcw %3\n\t"
      "frndint\n\t"
      "fld %%st(1)\n\t"
      "frndint\n\t"
      "fldcw %2\n\t"
      "fld %%st(1)\n\t"
      "fldt %4\n\t"
      "fld %%st(2)\n\t"
      "fmul %%st(1),%%st\n\t"
      "fsubp %%st,%%st(2)\n\t"
      "fld %%st(4)\n\t"
      "fsub %%st(3),%%st\n\t"
      "fmulp %%st,%%st(1)\n\t"
      "faddp %%st,%%st(1)\n\t"
      "fldt %5\n\t"
      "fmul %%st(4),%%st\n\t"
      "faddp %%st,%%st(1)\n\t"
      "f2xm1\n\t"
      "fld1\n\t"
      "faddp %%st,%%st(1)\n\t"
      "fstp %%st(1)\n\t"
      "fscale\n\t"
      "fstp %%st(1)\n\t"
      "fstp %%st(1)\n\t"
      : "=t"(r) : "0"(x), "m"(cw0), "m"(cw1), "m"(c1), "m"(c2)
      : "eax", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)");
   return (double)r;
}
#endif

/* msvcrt log10/acos take the SSE2 path (near correctly rounded doubles). */
#ifdef FLAPUS_PORTABLE_MATH
static double fl_m_log10(double d) { return (double)(fl_p_ln(XF(d))*FL_P_LOG10E); }
static double fl_m_acos(double d) { return (double)fl_p_acos(XF(d)); }
#else
static double fl_m_log10(double d) {
   xf r, x = XF(d);
   __asm__("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   return (double)r;
}
static double fl_m_acos(double d) {
   return (double)acosl(XF(d));
}
#endif

/* SSE float->int conversions used by the reference (_mm_cvt_ss2si / cvttss2si). */
static int fl_cvt_ss2si(float f) { return (int)lrintf(f); }
static int fl_cvtt_ss2si(float f) { return (int)f; }

/* ======== enc_celt_dsp.c ======== */
/* ---- CELT encoder DSP with x87 rounding behaviour ---- */

/* kiss FFT butterflies: intermediates stay extended except where the reference spills. */
static void fl_e_bfly2(fl_cpx *Fout, int N) {
   const xf tw = 0.7071067812f;
   int i;
   for (i = 0; i < N; i++) {
      fl_cpx *F2 = Fout+4;
      xf tr, ti;
      float f2r = F2[0].r, f2i = F2[0].i;
      F2[0].r = (float)(XF(Fout[0].r)-f2r);
      F2[0].i = (float)(XF(Fout[0].i)-f2i);
      Fout[0].r = (float)(XF(Fout[0].r)+f2r);
      Fout[0].i = (float)(XF(Fout[0].i)+f2i);
      tr = (XF(F2[1].r)+F2[1].i)*tw;
      ti = (XF(F2[1].i)-F2[1].r)*tw;
      F2[1].r = (float)(XF(Fout[1].r)-tr);
      F2[1].i = (float)(XF(Fout[1].i)-ti);
      Fout[1].r = (float)(XF(Fout[1].r)+tr);
      Fout[1].i = (float)(XF(Fout[1].i)+ti);
      tr = F2[2].i;
      ti = -XF(F2[2].r);
      F2[2].r = (float)(XF(Fout[2].r)-tr);
      F2[2].i = (float)(XF(Fout[2].i)-ti);
      Fout[2].r = (float)(XF(Fout[2].r)+tr);
      Fout[2].i = (float)(XF(Fout[2].i)+ti);
      tr = (XF(F2[3].i)-F2[3].r)*tw;
      ti = -(XF(F2[3].i)+F2[3].r)*tw;
      F2[3].r = (float)(XF(Fout[3].r)-tr);
      F2[3].i = (float)(XF(Fout[3].i)-ti);
      Fout[3].r = (float)(XF(Fout[3].r)+tr);
      Fout[3].i = (float)(XF(Fout[3].i)+ti);
      Fout += 8;
   }
}
static void fl_e_bfly4(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i;
   if (m == 1) {
      for (i = 0; i < N; i++) {
         xf s0r = XF(Fout[0].r)-Fout[2].r, s0i = XF(Fout[0].i)-Fout[2].i;
         xf f0r = XF(Fout[0].r)+Fout[2].r, f0i = XF(Fout[0].i)+Fout[2].i;
         xf s1r = XF(Fout[1].r)+Fout[3].r, s1i = XF(Fout[1].i)+Fout[3].i;
         Fout[2].r = (float)(f0r-s1r);
         Fout[2].i = (float)(f0i-s1i);
         Fout[0].r = (float)(f0r+s1r);
         Fout[0].i = (float)(f0i+s1i);
         s1r = XF(Fout[1].r)-Fout[3].r;
         s1i = XF(Fout[1].i)-Fout[3].i;
         Fout[1].r = (float)(s0r+s1i);
         Fout[1].i = (float)(s0i-s1r);
         Fout[3].r = (float)(s0r-s1i);
         Fout[3].i = (float)(s0i+s1r);
         Fout += 4;
      }
   } else {
      int j, m2 = 2*m, m3 = 3*m;
      fl_cpx *Fout_beg = Fout;
      for (i = 0; i < N; i++) {
         const fl_cpx *tw1, *tw2, *tw3;
         Fout = Fout_beg+i*mm;
         tw3 = tw2 = tw1 = fl_fft_twiddles48000_960;
         for (j = 0; j < m; j++) {
            float s0r, s0i;
            xf s1r, s1i, s2r, s2i, s3r, s3i, s4r, s4i, s5r, s5i, f0r, f0i;
            s0r = (float)(XF(Fout[m].r)*tw1->r-XF(Fout[m].i)*tw1->i);
            s0i = (float)(XF(Fout[m].r)*tw1->i+XF(Fout[m].i)*tw1->r);
            s1r = XF(Fout[m2].r)*tw2->r-XF(Fout[m2].i)*tw2->i;
            s1i = XF(Fout[m2].r)*tw2->i+XF(Fout[m2].i)*tw2->r;
            s2r = XF(Fout[m3].r)*tw3->r-XF(Fout[m3].i)*tw3->i;
            s2i = XF(Fout[m3].r)*tw3->i+XF(Fout[m3].i)*tw3->r;
            s5r = XF(Fout->r)-s1r;
            s5i = XF(Fout->i)-s1i;
            f0r = XF(Fout->r)+s1r;
            f0i = XF(Fout->i)+s1i;
            Fout->r = (float)f0r;
            Fout->i = (float)f0i;
            s3r = s0r+s2r;
            s3i = s0i+s2i;
            s4r = s0r-s2r;
            s4i = s0i-s2i;
            Fout[m2].r = (float)(f0r-s3r);
            Fout[m2].i = (float)(f0i-s3i);
            tw1 += fstride;
            tw2 += fstride*2;
            tw3 += fstride*3;
            Fout->r = (float)(s3r+Fout->r);
            Fout->i = (float)(s3i+Fout->i);
            Fout[m].r = (float)(s5r+s4i);
            Fout[m].i = (float)(s5i-s4r);
            Fout[m3].r = (float)(s5r-s4i);
            Fout[m3].i = (float)(s5i+s4r);
            ++Fout;
         }
      }
   }
}
static void fl_e_bfly3(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i, k, m2 = 2*m;
   const fl_cpx *tw1, *tw2;
   fl_cpx *Fout_beg = Fout;
   float epi3i = fl_fft_twiddles48000_960[fstride*m].i;
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      tw1 = tw2 = fl_fft_twiddles48000_960;
      k = m;
      do {
         xf s1r, s1i, s2r, s2i, s3r, s3i, s0r, s0i;
         float fmr, fmi;
         s1r = XF(Fout[m].r)*tw1->r-XF(Fout[m].i)*tw1->i;
         s1i = XF(Fout[m].r)*tw1->i+XF(Fout[m].i)*tw1->r;
         s2r = XF(Fout[m2].r)*tw2->r-XF(Fout[m2].i)*tw2->i;
         s2i = XF(Fout[m2].r)*tw2->i+XF(Fout[m2].i)*tw2->r;
         s3r = s1r+s2r;
         s3i = s1i+s2i;
         s0r = s1r-s2r;
         s0i = s1i-s2i;
         tw1 += fstride;
         tw2 += fstride*2;
         fmr = (float)(XF(Fout->r)-XF(.5f)*s3r);
         fmi = (float)(XF(Fout->i)-XF(.5f)*s3i);
         Fout[m].r = fmr;
         Fout[m].i = fmi;
         s0r *= epi3i;
         s0i *= epi3i;
         Fout->r = (float)(s3r+Fout->r);
         Fout->i = (float)(s3i+Fout->i);
         Fout[m2].r = (float)(XF(fmr)+s0i);
         Fout[m2].i = (float)(XF(fmi)-s0r);
         Fout[m].r = (float)(XF(fmr)-s0i);
         Fout[m].i = (float)(XF(fmi)+s0r);
         ++Fout;
      } while (--k);
   }
}
static void fl_e_bfly5(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   fl_cpx *F0, *F1, *F2, *F3, *F4, *Fout_beg = Fout;
   const fl_cpx *tw = fl_fft_twiddles48000_960;
   float yar = tw[fstride*m].r, yai = tw[fstride*m].i, ybr = tw[fstride*2*m].r, ybi = tw[fstride*2*m].i;
   int i, u;
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      F0 = Fout; F1 = F0+m; F2 = F0+2*m; F3 = F0+3*m; F4 = F0+4*m;
      for (u = 0; u < m; ++u) {
         float s0r = F0->r, s0i = F0->i, s1r, s1i, s7i_f, s9r, s9i, s10r, s10i;
         xf s2r, s2i, s3r, s3i, s4r, s4i, s7r, s7i, s8r, s8i, s5r, s5i, s6r, s6i, s11r, s11i, s12r, s12i;
         const fl_cpx *t1 = &tw[u*fstride], *t2 = &tw[2*u*fstride], *t3 = &tw[3*u*fstride], *t4 = &tw[4*u*fstride];
         s1r = (float)(XF(F1->r)*t1->r-XF(F1->i)*t1->i);
         s1i = (float)(XF(F1->r)*t1->i+XF(F1->i)*t1->r);
         s2r = XF(F2->r)*t2->r-XF(F2->i)*t2->i;
         s2i = XF(F2->r)*t2->i+XF(F2->i)*t2->r;
         s3r = XF(F3->r)*t3->r-XF(F3->i)*t3->i;
         s3i = XF(F3->r)*t3->i+XF(F3->i)*t3->r;
         s4r = XF(F4->r)*t4->r-XF(F4->i)*t4->i;
         s4i = XF(F4->r)*t4->i+XF(F4->i)*t4->r;
         s7r = s1r+s4r;
         s7i = s1i+s4i;
         s7i_f = (float)s7i;
         s10r = (float)(s1r-s4r);
         s10i = (float)(s1i-s4i);
         s8r = s2r+s3r;
         s8i = s2i+s3i;
         s9r = (float)(s2r-s3r);
         s9i = (float)(s2i-s3i);
         F0->r = (float)((s7r+s8r)+s0r);
         F0->i = (float)((s7i+s8i)+s0i);
         s5r = (yar*s7r+ybr*s8r)+s0r;
         s5i = (s7i*yar+ybr*s8i)+s0i;
         s6r = XF(yai)*s10i+XF(ybi)*s9i;
         s6i = -(XF(yai)*s10r+XF(ybi)*s9r);
         F1->r = (float)(s5r-s6r);
         F1->i = (float)(s5i-s6i);
         F4->r = (float)(s6r+s5r);
         F4->i = (float)(s5i+s6i);
         s11r = (s7r*ybr+s8r*yar)+s0r;
         s11i = (XF(s7i_f)*ybr+s8i*yar)+s0i;
         s12r = XF(s9i)*yai-XF(s10i)*ybi;
         s12i = XF(s10r)*ybi-XF(s9r)*yai;
         F2->r = (float)(s11r+s12r);
         F2->i = (float)(s11i+s12i);
         F3->r = (float)(s11r-s12r);
         F3->i = (float)(s11i-s12i);
         ++F0; ++F1; ++F2; ++F3; ++F4;
      }
   }
}
static void fl_e_fft_impl(const fl_fft_state *st, fl_cpx *fout) {
   int m2, m, p, L, i, shift, fstride[8];
   shift = st->shift > 0 ? st->shift : 0;
   fstride[0] = 1;
   L = 0;
   do {
      p = st->factors[2*L];
      m = st->factors[2*L+1];
      fstride[L+1] = fstride[L]*p;
      L++;
   } while (m != 1);
   m = st->factors[2*L-1];
   for (i = L-1; i >= 0; i--) {
      m2 = i != 0 ? st->factors[2*i-1] : 1;
      switch (st->factors[2*i]) {
      case 2: fl_e_bfly2(fout, fstride[i]); break;
      case 4: fl_e_bfly4(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 3: fl_e_bfly3(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 5: fl_e_bfly5(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      }
      m = m2;
   }
}
/* opus_fft_c: scaled bit-reversed copy then butterflies. */
static void fl_e_fft(const fl_fft_state *st, const fl_cpx *fin, fl_cpx *fout) {
   int i;
   for (i = 0; i < st->nfft; i++) {
      fout[st->bitrev[i]].r = (float)(XF(fin[i].r)*st->scale);
      fout[st->bitrev[i]].i = (float)(XF(fin[i].i)*st->scale);
   }
   fl_e_fft_impl(st, fout);
}

/* ======== enc_celt.c ======== */
/* ---- CELT encoder (float, x87 rounding as in the reference build) ---- */
static xf fl_e_inner_prod(const float *x, const float *y, int N);

/* FLOAT_APPROX log2: result stays in an x87 register (unrounded). */
static xf fl_e_log2(float x) {
   fl_i32 integer, range_idx;
   union { float f; fl_u32 i; } in;
   xf f;
   in.f = x;
   integer = (fl_i32)(in.i>>23)-127;
   in.i = (fl_i32)in.i-(fl_i32)((fl_u32)integer<<23);
   range_idx = (in.i>>20)&0x7;
   f = XF(in.f)*fl_log2_x_norm_coeff[range_idx]-1.0625f;
   f = 8.74628424644470214843750000e-02f+f*(1.357829570770263671875000000000f
         +f*(-6.3897705078125000000000000e-01f
         +f*(4.01971250772476196289062500e-01f
         +f*(-2.8415444493293762207031250e-01f))));
   return f+integer+fl_log2_y_norm_coeff[range_idx];
}
/* FLOAT_APPROX exp2: polynomial in extended precision, stored as float. */
static float fl_e_exp2(float x) {
   fl_i32 integer;
   xf frac;
   union { float f; fl_u32 i; } res;
   integer = (int)floorf(x);
   if (integer < -50) return 0;
   frac = XF(x)-integer;
   res.f = (float)(9.999999403953552246093750000000e-01f+frac*(6.931530833244323730468750000000e-01f
         +frac*(2.401536107063293457031250000000e-01f
         +frac*(5.582631751894950866699218750000e-02f
         +frac*(8.989339694380760192871093750000e-03f
         +frac*(1.877576694823801517486572265625e-03f))))));
   res.i = (fl_u32)((fl_i32)res.i+(fl_i32)((fl_u32)integer<<23))&0x7fffffff;
   return res.f;
}

static void fl_e_mdct_forward(const float *in, float *out, int overlap, int shift, int stride) {
   const float *trig = fl_mdct_twiddles960, *window = fl_window120;
   const fl_fft_state *st = &fl_fft_states[shift];
   int i, N = 1920, N2, N4;
   float f[960];
   fl_cpx f2[480];
   xf scale = st->scale;
   for (i = 0; i < shift; i++) {
      N >>= 1;
      trig += N;
   }
   N2 = N>>1;
   N4 = N>>2;
   {
      const float *xp1 = in+(overlap>>1), *xp2 = in+N2-1+(overlap>>1);
      const float *wp1 = window+(overlap>>1), *wp2 = window+(overlap>>1)-1;
      float *yp = f;
      for (i = 0; i < ((overlap+3)>>2); i++) {
         *yp++ = (float)(XF(xp1[N2])**wp2+XF(*xp2)**wp1);
         *yp++ = (float)(XF(*xp1)**wp1-XF(xp2[-N2])**wp2);
         xp1 += 2; xp2 -= 2; wp1 += 2; wp2 -= 2;
      }
      wp1 = window;
      wp2 = window+overlap-1;
      for (; i < N4-((overlap+3)>>2); i++) {
         *yp++ = *xp2;
         *yp++ = *xp1;
         xp1 += 2; xp2 -= 2;
      }
      for (; i < N4; i++) {
         *yp++ = (float)(-(XF(xp1[-N2])**wp1)+XF(*xp2)**wp2);
         *yp++ = (float)(XF(*xp1)**wp2+XF(xp2[N2])**wp1);
         xp1 += 2; xp2 -= 2; wp1 += 2; wp2 -= 2;
      }
   }
   for (i = 0; i < N4; i++) {
      xf re = f[2*i], im = f[2*i+1], t0 = trig[i], t1 = trig[N4+i];
      f2[st->bitrev[i]].r = (float)((re*t0-im*t1)*scale);
      f2[st->bitrev[i]].i = (float)((im*t0+re*t1)*scale);
   }
   fl_e_fft_impl(st, f2);
   {
      float *yp1 = out, *yp2 = out+stride*(N2-1);
      for (i = 0; i < N4; i++) {
         xf t0 = trig[i], t1 = trig[N4+i];
         *yp1 = (float)(XF(f2[i].i)*t1-XF(f2[i].r)*t0);
         *yp2 = (float)(XF(f2[i].r)*t1+XF(f2[i].i)*t0);
         yp1 += 2*stride;
         yp2 -= 2*stride;
      }
   }
}

static void fl_e_compute_mdcts(int shortBlocks, const float *in, float *out, int C, int CC, int LM) {
   int N, B, shift, b, c, i;
   if (shortBlocks) {
      B = shortBlocks;
      N = 120;
      shift = 3;
   } else {
      B = 1;
      N = 120<<LM;
      shift = 3-LM;
   }
   for (c = 0; c < CC; c++)
      for (b = 0; b < B; b++)
         fl_e_mdct_forward(in+c*(B*N+120)+b*N, &out[b+c*N*B], 120, shift, B);
   if (CC == 2 && C == 1)
      for (i = 0; i < B*N; i++) out[i] = (float)(XF(.5f)*out[i]+XF(.5f)*out[B*N+i]);
}

static void fl_e_band_energies(const float *X, float *bandE, int end, int C, int LM) {
   int i, c, N = 120<<LM;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         int lo = c*N+(fl_eband5ms[i]<<LM);
         float sum = (float)(fl_e_inner_prod(&X[lo], &X[lo], (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM)+1e-27f);
         bandE[i+c*CELT_NB_EBANDS] = sqrtf(sum);
      }
}

static void fl_e_normalise_bands(const float *freq, float *X, const float *bandE, int end, int C, int M) {
   int i, j, c, N = M*120;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         xf g = 1.0L/(XF(1e-27f)+bandE[i+c*CELT_NB_EBANDS]);
         for (j = M*fl_eband5ms[i]; j < M*fl_eband5ms[i+1]; j++) X[j+c*N] = (float)(freq[j+c*N]*g);
      }
}

static void fl_e_amp2log2(int effEnd, int end, const float *bandE, float *bandLogE, int C) {
   int c, i;
   for (c = 0; c < C; c++) {
      for (i = 0; i < effEnd; i++)
         bandLogE[i+c*CELT_NB_EBANDS] = (float)(fl_e_log2(bandE[i+c*CELT_NB_EBANDS])-fl_eMeans[i]);
      for (i = effEnd; i < end; i++) bandLogE[c*CELT_NB_EBANDS+i] = -14.f;
   }
}

static int fl_e_quant_coarse_impl(int start, int end, const float *eBands, float *oldEBands, fl_i32 budget,
      fl_i32 tell, const unsigned char *prob_model, float *error, ec_ctx *enc, int C, int LM, int intra,
      float max_decay, int lfe) {
   int i, c, badness = 0;
   float prev[2] = {0, 0}, coef, beta;
   if (tell+3 <= budget) ec_enc_bit_logp(enc, intra, 3);
   if (intra) {
      coef = 0;
      beta = fl_beta_intra;
   } else {
      beta = fl_beta_coef[LM];
      coef = fl_pred_coef[LM];
   }
   for (i = start; i < end; i++) {
      c = 0;
      do {
         int bits_left, qi, qi0;
         float x = eBands[i+c*CELT_NB_EBANDS], old = oldEBands[i+c*CELT_NB_EBANDS];
         float oldE = FL_MAX(-9.f, old), coE, f_f;
         xf f, decay_bound;
         coE = (float)(XF(coef)*oldE);
         f = (XF(x)-XF(coef)*oldE)-prev[c];
         f_f = (float)f;
         qi = (int)floorf((float)(f+.5f));
         decay_bound = XF(FL_MAX(-28.f, old))-max_decay;
         if (qi < 0 && x < decay_bound) {
            qi += (int)(float)(decay_bound-x);
            if (qi > 0) qi = 0;
         }
         qi0 = qi;
         tell = ec_tell(enc);
         bits_left = budget-tell-3*C*(end-i);
         if (i != start && bits_left < 30) {
            if (bits_left < 24) qi = IMIN(1, qi);
            if (bits_left < 16) qi = IMAX(-1, qi);
         }
         if (lfe && i >= 2) qi = IMIN(qi, 0);
         if (budget-tell >= 15) {
            int pi = 2*IMIN(i, 20);
            fl_laplace_encode(enc, &qi, prob_model[pi]<<7, prob_model[pi+1]<<6);
         } else if (budget-tell >= 2) {
            qi = IMAX(-1, IMIN(qi, 1));
            ec_enc_icdf(enc, 2*qi^-(qi < 0), fl_small_energy_icdf, 2);
         } else if (budget-tell >= 1) {
            qi = IMIN(0, qi);
            ec_enc_bit_logp(enc, -qi, 1);
         } else qi = -1;
         error[i+c*CELT_NB_EBANDS] = (float)(XF(f_f)-qi);
         badness += abs(qi0-qi);
         oldEBands[i+c*CELT_NB_EBANDS] = (float)((XF(coE)+prev[c])+qi);
         prev[c] = (float)((XF(prev[c])+qi)-XF(beta)*qi);
      } while (++c < C);
   }
   return lfe ? 0 : badness;
}

static void fl_e_quant_coarse_energy(int start, int end, int effEnd, const float *eBands, float *oldEBands,
      fl_u32 budget, float *error, ec_ctx *enc, int C, int LM, int nbAvailableBytes, int force_intra,
      float *delayedIntra, int two_pass, int loss_rate, int lfe) {
   int intra, badness1 = 0, i, c;
   float max_decay, new_distortion;
   float oldEBands_intra[2*CELT_NB_EBANDS], error_intra[2*CELT_NB_EBANDS];
   ec_ctx enc_start_state;
   fl_u32 tell;
   fl_i32 intra_bias;
   intra = force_intra || (!two_pass && *delayedIntra > 2*C*(end-start) && nbAvailableBytes > (end-start)*C);
   intra_bias = (fl_i32)(float)((XF((fl_i64)budget)**delayedIntra*loss_rate)/(C*512));
   {
      xf dist = 0;
      for (c = 0; c < C; c++)
         for (i = start; i < effEnd; i++) {
            xf d = XF(eBands[i+c*CELT_NB_EBANDS])-oldEBands[i+c*CELT_NB_EBANDS];
            dist += d*d;
         }
      new_distortion = (float)(dist < 200 ? dist : 200);
   }
   tell = ec_tell(enc);
   if (tell+3 > budget) two_pass = intra = 0;
   max_decay = 16.f;
   if (end-start > 10) {
      xf md = XF(.125f)*nbAvailableBytes;
      if (md < max_decay) max_decay = (float)md;
   }
   if (lfe) max_decay = 3.f;
   enc_start_state = *enc;
   memcpy(oldEBands_intra, oldEBands, C*CELT_NB_EBANDS*sizeof(float));
   if (two_pass || intra)
      badness1 = fl_e_quant_coarse_impl(start, end, eBands, oldEBands_intra, budget, tell,
            fl_e_prob_model[LM][1], error_intra, enc, C, LM, 1, max_decay, lfe);
   if (!intra) {
      unsigned char *intra_buf, intra_bits[1275];
      ec_ctx enc_intra_state;
      fl_i32 tell_intra;
      fl_u32 nstart_bytes, nintra_bytes;
      int badness2;
      tell_intra = ec_tell_frac(enc);
      enc_intra_state = *enc;
      nstart_bytes = enc_start_state.offs;
      nintra_bytes = enc_intra_state.offs;
      intra_buf = enc_intra_state.buf+nstart_bytes;
      memcpy(intra_bits, intra_buf, nintra_bytes-nstart_bytes);
      *enc = enc_start_state;
      badness2 = fl_e_quant_coarse_impl(start, end, eBands, oldEBands, budget, tell,
            fl_e_prob_model[LM][intra], error, enc, C, LM, 0, max_decay, lfe);
      if (two_pass && (badness1 < badness2 || (badness1 == badness2 && ((fl_i32)ec_tell_frac(enc))+intra_bias > tell_intra))) {
         *enc = enc_intra_state;
         memcpy(intra_buf, intra_bits, nintra_bytes-nstart_bytes);
         memcpy(oldEBands, oldEBands_intra, C*CELT_NB_EBANDS*sizeof(float));
         memcpy(error, error_intra, C*CELT_NB_EBANDS*sizeof(float));
         intra = 1;
      }
   } else {
      memcpy(oldEBands, oldEBands_intra, C*CELT_NB_EBANDS*sizeof(float));
      memcpy(error, error_intra, C*CELT_NB_EBANDS*sizeof(float));
   }
   if (intra) *delayedIntra = new_distortion;
   else *delayedIntra = (float)((XF(fl_pred_coef[LM])*fl_pred_coef[LM])**delayedIntra+new_distortion);
}

static void fl_e_quant_fine_energy(int start, int end, float *oldEBands, float *error, const int *extra_quant,
      ec_ctx *enc, int C) {
   int i, c;
   for (i = start; i < end; i++) {
      int extra = 1<<extra_quant[i];
      if (extra_quant[i] <= 0) continue;
      if (ec_tell(enc)+C*extra_quant[i] > (fl_i32)enc->storage*8) continue;
      c = 0;
      do {
         int q2;
         xf offset;
         q2 = (int)floorf((float)((XF(error[i+c*CELT_NB_EBANDS])+.5f)*(float)(fl_i16)extra));
         if (q2 > extra-1) q2 = extra-1;
         if (q2 < 0) q2 = 0;
         ec_enc_bits(enc, q2, extra_quant[i]);
         offset = (XF(.5f)+q2)*(1<<(14-extra_quant[i]))*XF(1.f/16384)-.5f;
         offset *= XF(1.f/16384)*(1<<14);
         oldEBands[i+c*CELT_NB_EBANDS] = (float)(oldEBands[i+c*CELT_NB_EBANDS]+offset);
         error[i+c*CELT_NB_EBANDS] = (float)(error[i+c*CELT_NB_EBANDS]-offset);
      } while (++c < C);
   }
}

static void fl_e_quant_energy_finalise(int start, int end, float *oldEBands, float *error, const int *fine_quant,
      const int *fine_priority, int bits_left, ec_ctx *enc, int C) {
   int i, prio, c;
   for (prio = 0; prio < 2; prio++)
      for (i = start; i < end && bits_left >= C; i++) {
         if (fine_quant[i] >= 8 || fine_priority[i] != prio) continue;
         c = 0;
         do {
            int q2 = error[i+c*CELT_NB_EBANDS] < 0 ? 0 : 1;
            xf offset;
            ec_enc_bits(enc, q2, 1);
            offset = (q2-XF(.5f))*(1<<(14-fine_quant[i]-1))*XF(1.f/16384);
            oldEBands[i+c*CELT_NB_EBANDS] = (float)(oldEBands[i+c*CELT_NB_EBANDS]+offset);
            error[i+c*CELT_NB_EBANDS] = (float)(error[i+c*CELT_NB_EBANDS]-offset);
            bits_left--;
         } while (++c < C);
      }
}

/* ======== enc_rcp_tables.c ======== */
/* ---- Intel rcpps/rsqrtps as tables (FLAPUS_PORTABLE_MATH), generated by tools/gen_rcp.c ----
   Entry: bit 12 = result exponent - 126 for an input in [1,2) (rsqrt: [1,4)),
   bits 0-11 = the 12 result mantissa bits below the 11 that are always zero. Checked
   against an Intel Core i7-13700K for all 2^32 inputs of both instructions. */
#ifdef FLAPUS_PORTABLE_MATH
static const unsigned short fl_rcp_tab[2048] = {
   0x0ffe, 0x0ffa, 0x0ff6, 0x0ff2, 0x0fee, 0x0fea, 0x0fe6, 0x0fe2, 0x0fde, 0x0fda, 0x0fd6, 0x0fd2,
   0x0fce, 0x0fca, 0x0fc6, 0x0fc2, 0x0fbf, 0x0fbb, 0x0fb7, 0x0fb3, 0x0faf, 0x0fab, 0x0fa7, 0x0fa3,
   0x0f9f, 0x0f9b, 0x0f97, 0x0f93, 0x0f90, 0x0f8c, 0x0f88, 0x0f84, 0x0f80, 0x0f7c, 0x0f78, 0x0f74,
   0x0f71, 0x0f6d, 0x0f69, 0x0f65, 0x0f61, 0x0f5d, 0x0f59, 0x0f56, 0x0f52, 0x0f4e, 0x0f4a, 0x0f46,
   0x0f42, 0x0f3f, 0x0f3b, 0x0f37, 0x0f33, 0x0f2f, 0x0f2c, 0x0f28, 0x0f24, 0x0f20, 0x0f1c, 0x0f19,
   0x0f15, 0x0f11, 0x0f0d, 0x0f0a, 0x0f06, 0x0f02, 0x0efe, 0x0efb, 0x0ef7, 0x0ef3, 0x0eef, 0x0eec,
   0x0ee8, 0x0ee4, 0x0ee0, 0x0edd, 0x0ed9, 0x0ed5, 0x0ed2, 0x0ece, 0x0eca, 0x0ec6, 0x0ec3, 0x0ebf,
   0x0ebb, 0x0eb8, 0x0eb4, 0x0eb0, 0x0ead, 0x0ea9, 0x0ea5, 0x0ea2, 0x0e9e, 0x0e9a, 0x0e97, 0x0e93,
   0x0e8f, 0x0e8c, 0x0e88, 0x0e84, 0x0e81, 0x0e7d, 0x0e7a, 0x0e76, 0x0e72, 0x0e6f, 0x0e6b, 0x0e67,
   0x0e64, 0x0e60, 0x0e5d, 0x0e59, 0x0e55, 0x0e52, 0x0e4e, 0x0e4b, 0x0e47, 0x0e44, 0x0e40, 0x0e3c,
   0x0e39, 0x0e35, 0x0e32, 0x0e2e, 0x0e2b, 0x0e27, 0x0e23, 0x0e20, 0x0e1c, 0x0e19, 0x0e15, 0x0e12,
   0x0e0e, 0x0e0b, 0x0e07, 0x0e04, 0x0e00, 0x0dfd, 0x0df9, 0x0df6, 0x0df2, 0x0def, 0x0deb, 0x0de8,
   0x0de4, 0x0de1, 0x0ddd, 0x0dda, 0x0dd6, 0x0dd3, 0x0dcf, 0x0dcc, 0x0dc8, 0x0dc5, 0x0dc1, 0x0dbe,
   0x0dba, 0x0db7, 0x0db4, 0x0db0, 0x0dad, 0x0da9, 0x0da6, 0x0da2, 0x0d9f, 0x0d9b, 0x0d98, 0x0d95,
   0x0d91, 0x0d8e, 0x0d8a, 0x0d87, 0x0d84, 0x0d80, 0x0d7d, 0x0d79, 0x0d76, 0x0d73, 0x0d6f, 0x0d6c,
   0x0d68, 0x0d65, 0x0d62, 0x0d5e, 0x0d5b, 0x0d58, 0x0d54, 0x0d51, 0x0d4e, 0x0d4a, 0x0d47, 0x0d44,
   0x0d40, 0x0d3d, 0x0d39, 0x0d36, 0x0d33, 0x0d2f, 0x0d2c, 0x0d29, 0x0d26, 0x0d22, 0x0d1f, 0x0d1c,
   0x0d18, 0x0d15, 0x0d12, 0x0d0e, 0x0d0b, 0x0d08, 0x0d04, 0x0d01, 0x0cfe, 0x0cfb, 0x0cf7, 0x0cf4,
   0x0cf1, 0x0cee, 0x0cea, 0x0ce7, 0x0ce4, 0x0ce0, 0x0cdd, 0x0cda, 0x0cd7, 0x0cd3, 0x0cd0, 0x0ccd,
   0x0cca, 0x0cc7, 0x0cc3, 0x0cc0, 0x0cbd, 0x0cba, 0x0cb6, 0x0cb3, 0x0cb0, 0x0cad, 0x0caa, 0x0ca6,
   0x0ca3, 0x0ca0, 0x0c9d, 0x0c99, 0x0c96, 0x0c93, 0x0c90, 0x0c8d, 0x0c8a, 0x0c86, 0x0c83, 0x0c80,
   0x0c7d, 0x0c7a, 0x0c77, 0x0c73, 0x0c70, 0x0c6d, 0x0c6a, 0x0c67, 0x0c64, 0x0c60, 0x0c5d, 0x0c5a,
   0x0c57, 0x0c54, 0x0c51, 0x0c4e, 0x0c4a, 0x0c47, 0x0c44, 0x0c41, 0x0c3e, 0x0c3b, 0x0c38, 0x0c35,
   0x0c32, 0x0c2e, 0x0c2b, 0x0c28, 0x0c25, 0x0c22, 0x0c1f, 0x0c1c, 0x0c19, 0x0c16, 0x0c13, 0x0c10,
   0x0c0c, 0x0c09, 0x0c06, 0x0c03, 0x0c00, 0x0bfd, 0x0bfa, 0x0bf7, 0x0bf4, 0x0bf1, 0x0bee, 0x0beb,
   0x0be8, 0x0be5, 0x0be2, 0x0bdf, 0x0bdc, 0x0bd9, 0x0bd6, 0x0bd3, 0x0bd0, 0x0bcd, 0x0bca, 0x0bc6,
   0x0bc3, 0x0bc0, 0x0bbd, 0x0bba, 0x0bb7, 0x0bb4, 0x0bb1, 0x0bae, 0x0bab, 0x0ba8, 0x0ba6, 0x0ba3,
   0x0ba0, 0x0b9d, 0x0b9a, 0x0b97, 0x0b94, 0x0b91, 0x0b8e, 0x0b8b, 0x0b88, 0x0b85, 0x0b82, 0x0b7f,
   0x0b7c, 0x0b79, 0x0b76, 0x0b73, 0x0b70, 0x0b6d, 0x0b6a, 0x0b67, 0x0b64, 0x0b61, 0x0b5f, 0x0b5c,
   0x0b59, 0x0b56, 0x0b53, 0x0b50, 0x0b4d, 0x0b4a, 0x0b47, 0x0b44, 0x0b41, 0x0b3f, 0x0b3c, 0x0b39,
   0x0b36, 0x0b33, 0x0b30, 0x0b2d, 0x0b2a, 0x0b27, 0x0b25, 0x0b22, 0x0b1f, 0x0b1c, 0x0b19, 0x0b16,
   0x0b13, 0x0b10, 0x0b0e, 0x0b0b, 0x0b08, 0x0b05, 0x0b02, 0x0aff, 0x0afc, 0x0afa, 0x0af7, 0x0af4,
   0x0af1, 0x0aee, 0x0aeb, 0x0ae9, 0x0ae6, 0x0ae3, 0x0ae0, 0x0add, 0x0ada, 0x0ad8, 0x0ad5, 0x0ad2,
   0x0acf, 0x0acc, 0x0aca, 0x0ac7, 0x0ac4, 0x0ac1, 0x0abe, 0x0abc, 0x0ab9, 0x0ab6, 0x0ab3, 0x0ab1,
   0x0aae, 0x0aab, 0x0aa8, 0x0aa5, 0x0aa3, 0x0aa0, 0x0a9d, 0x0a9a, 0x0a98, 0x0a95, 0x0a92, 0x0a8f,
   0x0a8d, 0x0a8a, 0x0a87, 0x0a84, 0x0a82, 0x0a7f, 0x0a7c, 0x0a79, 0x0a77, 0x0a74, 0x0a71, 0x0a6e,
   0x0a6c, 0x0a69, 0x0a66, 0x0a63, 0x0a61, 0x0a5e, 0x0a5b, 0x0a59, 0x0a56, 0x0a53, 0x0a50, 0x0a4e,
   0x0a4b, 0x0a48, 0x0a46, 0x0a43, 0x0a40, 0x0a3e, 0x0a3b, 0x0a38, 0x0a36, 0x0a33, 0x0a30, 0x0a2e,
   0x0a2b, 0x0a28, 0x0a25, 0x0a23, 0x0a20, 0x0a1d, 0x0a1b, 0x0a18, 0x0a15, 0x0a13, 0x0a10, 0x0a0e,
   0x0a0b, 0x0a08, 0x0a06, 0x0a03, 0x0a00, 0x09fe, 0x09fb, 0x09f8, 0x09f6, 0x09f3, 0x09f0, 0x09ee,
   0x09eb, 0x09e9, 0x09e6, 0x09e3, 0x09e1, 0x09de, 0x09dc, 0x09d9, 0x09d6, 0x09d4, 0x09d1, 0x09cf,
   0x09cc, 0x09c9, 0x09c7, 0x09c4, 0x09c2, 0x09bf, 0x09bc, 0x09ba, 0x09b7, 0x09b5, 0x09b2, 0x09af,
   0x09ad, 0x09aa, 0x09a8, 0x09a5, 0x09a3, 0x09a0, 0x099d, 0x099b, 0x0998, 0x0996, 0x0993, 0x0991,
   0x098e, 0x098c, 0x0989, 0x0986, 0x0984, 0x0981, 0x097f, 0x097c, 0x097a, 0x0977, 0x0975, 0x0972,
   0x0970, 0x096d, 0x096b, 0x0968, 0x0966, 0x0963, 0x0961, 0x095e, 0x095b, 0x0959, 0x0956, 0x0954,
   0x0951, 0x094f, 0x094c, 0x094a, 0x0947, 0x0945, 0x0942, 0x0940, 0x093d, 0x093b, 0x0939, 0x0936,
   0x0934, 0x0931, 0x092f, 0x092c, 0x092a, 0x0927, 0x0925, 0x0922, 0x0920, 0x091d, 0x091b, 0x0918,
   0x0916, 0x0913, 0x0911, 0x090f, 0x090c, 0x090a, 0x0907, 0x0905, 0x0902, 0x0900, 0x08fd, 0x08fb,
   0x08f9, 0x08f6, 0x08f4, 0x08f1, 0x08ef, 0x08ec, 0x08ea, 0x08e8, 0x08e5, 0x08e3, 0x08e0, 0x08de,
   0x08db, 0x08d9, 0x08d7, 0x08d4, 0x08d2, 0x08cf, 0x08cd, 0x08cb, 0x08c8, 0x08c6, 0x08c3, 0x08c1,
   0x08bf, 0x08bc, 0x08ba, 0x08b7, 0x08b5, 0x08b3, 0x08b0, 0x08ae, 0x08ac, 0x08a9, 0x08a7, 0x08a4,
   0x08a2, 0x08a0, 0x089d, 0x089b, 0x0899, 0x0896, 0x0894, 0x0891, 0x088f, 0x088d, 0x088a, 0x0888,
   0x0886, 0x0883, 0x0881, 0x087f, 0x087c, 0x087a, 0x0878, 0x0875, 0x0873, 0x0871, 0x086e, 0x086c,
   0x086a, 0x0867, 0x0865, 0x0863, 0x0860, 0x085e, 0x085c, 0x0859, 0x0857, 0x0855, 0x0852, 0x0850,
   0x084e, 0x084c, 0x0849, 0x0847, 0x0845, 0x0842, 0x0840, 0x083e, 0x083b, 0x0839, 0x0837, 0x0835,
   0x0832, 0x0830, 0x082e, 0x082b, 0x0829, 0x0827, 0x0825, 0x0822, 0x0820, 0x081e, 0x081b, 0x0819,
   0x0817, 0x0815, 0x0812, 0x0810, 0x080e, 0x080c, 0x0809, 0x0807, 0x0805, 0x0803, 0x0800, 0x07fe,
   0x07fc, 0x07fa, 0x07f7, 0x07f5, 0x07f3, 0x07f1, 0x07ee, 0x07ec, 0x07ea, 0x07e8, 0x07e5, 0x07e3,
   0x07e1, 0x07df, 0x07dd, 0x07da, 0x07d8, 0x07d6, 0x07d4, 0x07d1, 0x07cf, 0x07cd, 0x07cb, 0x07c9,
   0x07c6, 0x07c4, 0x07c2, 0x07c0, 0x07be, 0x07bb, 0x07b9, 0x07b7, 0x07b5, 0x07b3, 0x07b0, 0x07ae,
   0x07ac, 0x07aa, 0x07a8, 0x07a5, 0x07a3, 0x07a1, 0x079f, 0x079d, 0x079b, 0x0798, 0x0796, 0x0794,
   0x0792, 0x0790, 0x078e, 0x078b, 0x0789, 0x0787, 0x0785, 0x0783, 0x0781, 0x077e, 0x077c, 0x077a,
   0x0778, 0x0776, 0x0774, 0x0772, 0x076f, 0x076d, 0x076b, 0x0769, 0x0767, 0x0765, 0x0763, 0x0760,
   0x075e, 0x075c, 0x075a, 0x0758, 0x0756, 0x0754, 0x0751, 0x074f, 0x074d, 0x074b, 0x0749, 0x0747,
   0x0745, 0x0743, 0x0741, 0x073e, 0x073c, 0x073a, 0x0738, 0x0736, 0x0734, 0x0732, 0x0730, 0x072e,
   0x072b, 0x0729, 0x0727, 0x0725, 0x0723, 0x0721, 0x071f, 0x071d, 0x071b, 0x0719, 0x0717, 0x0715,
   0x0712, 0x0710, 0x070e, 0x070c, 0x070a, 0x0708, 0x0706, 0x0704, 0x0702, 0x0700, 0x06fe, 0x06fc,
   0x06fa, 0x06f8, 0x06f5, 0x06f3, 0x06f1, 0x06ef, 0x06ed, 0x06eb, 0x06e9, 0x06e7, 0x06e5, 0x06e3,
   0x06e1, 0x06df, 0x06dd, 0x06db, 0x06d9, 0x06d7, 0x06d5, 0x06d3, 0x06d1, 0x06cf, 0x06cd, 0x06cb,
   0x06c9, 0x06c6, 0x06c4, 0x06c2, 0x06c0, 0x06be, 0x06bc, 0x06ba, 0x06b8, 0x06b6, 0x06b4, 0x06b2,
   0x06b0, 0x06ae, 0x06ac, 0x06aa, 0x06a8, 0x06a6, 0x06a4, 0x06a2, 0x06a0, 0x069e, 0x069c, 0x069a,
   0x0698, 0x0696, 0x0694, 0x0692, 0x0690, 0x068e, 0x068c, 0x068a, 0x0688, 0x0686, 0x0684, 0x0682,
   0x0680, 0x067e, 0x067c, 0x067a, 0x0679, 0x0677, 0x0675, 0x0673, 0x0671, 0x066f, 0x066d, 0x066b,
   0x0669, 0x0667, 0x0665, 0x0663, 0x0661, 0x065f, 0x065d, 0x065b, 0x0659, 0x0657, 0x0655, 0x0653,
   0x0651, 0x064f, 0x064d, 0x064c, 0x064a, 0x0648, 0x0646, 0x0644, 0x0642, 0x0640, 0x063e, 0x063c,
   0x063a, 0x0638, 0x0636, 0x0634, 0x0632, 0x0630, 0x062f, 0x062d, 0x062b, 0x0629, 0x0627, 0x0625,
   0x0623, 0x0621, 0x061f, 0x061d, 0x061b, 0x0619, 0x0618, 0x0616, 0x0614, 0x0612, 0x0610, 0x060e,
   0x060c, 0x060a, 0x0608, 0x0606, 0x0605, 0x0603, 0x0601, 0x05ff, 0x05fd, 0x05fb, 0x05f9, 0x05f7,
   0x05f5, 0x05f4, 0x05f2, 0x05f0, 0x05ee, 0x05ec, 0x05ea, 0x05e8, 0x05e6, 0x05e5, 0x05e3, 0x05e1,
   0x05df, 0x05dd, 0x05db, 0x05d9, 0x05d7, 0x05d6, 0x05d4, 0x05d2, 0x05d0, 0x05ce, 0x05cc, 0x05ca,
   0x05c9, 0x05c7, 0x05c5, 0x05c3, 0x05c1, 0x05bf, 0x05be, 0x05bc, 0x05ba, 0x05b8, 0x05b6, 0x05b4,
   0x05b2, 0x05b1, 0x05af, 0x05ad, 0x05ab, 0x05a9, 0x05a7, 0x05a6, 0x05a4, 0x05a2, 0x05a0, 0x059e,
   0x059c, 0x059b, 0x0599, 0x0597, 0x0595, 0x0593, 0x0592, 0x0590, 0x058e, 0x058c, 0x058a, 0x0588,
   0x0587, 0x0585, 0x0583, 0x0581, 0x057f, 0x057e, 0x057c, 0x057a, 0x0578, 0x0576, 0x0575, 0x0573,
   0x0571, 0x056f, 0x056d, 0x056c, 0x056a, 0x0568, 0x0566, 0x0564, 0x0563, 0x0561, 0x055f, 0x055d,
   0x055c, 0x055a, 0x0558, 0x0556, 0x0554, 0x0553, 0x0551, 0x054f, 0x054d, 0x054c, 0x054a, 0x0548,
   0x0546, 0x0544, 0x0543, 0x0541, 0x053f, 0x053d, 0x053c, 0x053a, 0x0538, 0x0536, 0x0535, 0x0533,
   0x0531, 0x052f, 0x052e, 0x052c, 0x052a, 0x0528, 0x0527, 0x0525, 0x0523, 0x0521, 0x0520, 0x051e,
   0x051c, 0x051a, 0x0519, 0x0517, 0x0515, 0x0513, 0x0512, 0x0510, 0x050e, 0x050d, 0x050b, 0x0509,
   0x0507, 0x0506, 0x0504, 0x0502, 0x0500, 0x04ff, 0x04fd, 0x04fb, 0x04fa, 0x04f8, 0x04f6, 0x04f4,
   0x04f3, 0x04f1, 0x04ef, 0x04ee, 0x04ec, 0x04ea, 0x04e8, 0x04e7, 0x04e5, 0x04e3, 0x04e2, 0x04e0,
   0x04de, 0x04dd, 0x04db, 0x04d9, 0x04d7, 0x04d6, 0x04d4, 0x04d2, 0x04d1, 0x04cf, 0x04cd, 0x04cc,
   0x04ca, 0x04c8, 0x04c7, 0x04c5, 0x04c3, 0x04c1, 0x04c0, 0x04be, 0x04bc, 0x04bb, 0x04b9, 0x04b7,
   0x04b6, 0x04b4, 0x04b2, 0x04b1, 0x04af, 0x04ad, 0x04ac, 0x04aa, 0x04a8, 0x04a7, 0x04a5, 0x04a3,
   0x04a2, 0x04a0, 0x049e, 0x049d, 0x049b, 0x0499, 0x0498, 0x0496, 0x0494, 0x0493, 0x0491, 0x048f,
   0x048e, 0x048c, 0x048a, 0x0489, 0x0487, 0x0486, 0x0484, 0x0482, 0x0481, 0x047f, 0x047d, 0x047c,
   0x047a, 0x0478, 0x0477, 0x0475, 0x0474, 0x0472, 0x0470, 0x046f, 0x046d, 0x046b, 0x046a, 0x0468,
   0x0466, 0x0465, 0x0463, 0x0462, 0x0460, 0x045e, 0x045d, 0x045b, 0x045a, 0x0458, 0x0456, 0x0455,
   0x0453, 0x0451, 0x0450, 0x044e, 0x044d, 0x044b, 0x0449, 0x0448, 0x0446, 0x0445, 0x0443, 0x0441,
   0x0440, 0x043e, 0x043d, 0x043b, 0x0439, 0x0438, 0x0436, 0x0435, 0x0433, 0x0431, 0x0430, 0x042e,
   0x042d, 0x042b, 0x0429, 0x0428, 0x0426, 0x0425, 0x0423, 0x0421, 0x0420, 0x041e, 0x041d, 0x041b,
   0x041a, 0x0418, 0x0416, 0x0415, 0x0413, 0x0412, 0x0410, 0x040f, 0x040d, 0x040b, 0x040a, 0x0408,
   0x0407, 0x0405, 0x0404, 0x0402, 0x0400, 0x03ff, 0x03fd, 0x03fc, 0x03fa, 0x03f9, 0x03f7, 0x03f6,
   0x03f4, 0x03f2, 0x03f1, 0x03ef, 0x03ee, 0x03ec, 0x03eb, 0x03e9, 0x03e8, 0x03e6, 0x03e4, 0x03e3,
   0x03e1, 0x03e0, 0x03de, 0x03dd, 0x03db, 0x03da, 0x03d8, 0x03d7, 0x03d5, 0x03d4, 0x03d2, 0x03d0,
   0x03cf, 0x03cd, 0x03cc, 0x03ca, 0x03c9, 0x03c7, 0x03c6, 0x03c4, 0x03c3, 0x03c1, 0x03c0, 0x03be,
   0x03bd, 0x03bb, 0x03ba, 0x03b8, 0x03b7, 0x03b5, 0x03b4, 0x03b2, 0x03b0, 0x03af, 0x03ad, 0x03ac,
   0x03aa, 0x03a9, 0x03a7, 0x03a6, 0x03a4, 0x03a3, 0x03a1, 0x03a0, 0x039e, 0x039d, 0x039b, 0x039a,
   0x0398, 0x0397, 0x0395, 0x0394, 0x0392, 0x0391, 0x038f, 0x038e, 0x038c, 0x038b, 0x0389, 0x0388,
   0x0386, 0x0385, 0x0383, 0x0382, 0x0380, 0x037f, 0x037e, 0x037c, 0x037b, 0x0379, 0x0378, 0x0376,
   0x0375, 0x0373, 0x0372, 0x0370, 0x036f, 0x036d, 0x036c, 0x036a, 0x0369, 0x0367, 0x0366, 0x0364,
   0x0363, 0x0361, 0x0360, 0x035f, 0x035d, 0x035c, 0x035a, 0x0359, 0x0357, 0x0356, 0x0354, 0x0353,
   0x0351, 0x0350, 0x034e, 0x034d, 0x034c, 0x034a, 0x0349, 0x0347, 0x0346, 0x0344, 0x0343, 0x0341,
   0x0340, 0x033f, 0x033d, 0x033c, 0x033a, 0x0339, 0x0337, 0x0336, 0x0334, 0x0333, 0x0332, 0x0330,
   0x032f, 0x032d, 0x032c, 0x032a, 0x0329, 0x0327, 0x0326, 0x0325, 0x0323, 0x0322, 0x0320, 0x031f,
   0x031d, 0x031c, 0x031b, 0x0319, 0x0318, 0x0316, 0x0315, 0x0313, 0x0312, 0x0311, 0x030f, 0x030e,
   0x030c, 0x030b, 0x030a, 0x0308, 0x0307, 0x0305, 0x0304, 0x0302, 0x0301, 0x0300, 0x02fe, 0x02fd,
   0x02fb, 0x02fa, 0x02f9, 0x02f7, 0x02f6, 0x02f4, 0x02f3, 0x02f2, 0x02f0, 0x02ef, 0x02ed, 0x02ec,
   0x02eb, 0x02e9, 0x02e8, 0x02e6, 0x02e5, 0x02e4, 0x02e2, 0x02e1, 0x02df, 0x02de, 0x02dd, 0x02db,
   0x02da, 0x02d9, 0x02d7, 0x02d6, 0x02d4, 0x02d3, 0x02d2, 0x02d0, 0x02cf, 0x02cd, 0x02cc, 0x02cb,
   0x02c9, 0x02c8, 0x02c7, 0x02c5, 0x02c4, 0x02c2, 0x02c1, 0x02c0, 0x02be, 0x02bd, 0x02bc, 0x02ba,
   0x02b9, 0x02b7, 0x02b6, 0x02b5, 0x02b3, 0x02b2, 0x02b1, 0x02af, 0x02ae, 0x02ad, 0x02ab, 0x02aa,
   0x02a8, 0x02a7, 0x02a6, 0x02a4, 0x02a3, 0x02a2, 0x02a0, 0x029f, 0x029e, 0x029c, 0x029b, 0x029a,
   0x0298, 0x0297, 0x0295, 0x0294, 0x0293, 0x0291, 0x0290, 0x028f, 0x028d, 0x028c, 0x028b, 0x0289,
   0x0288, 0x0287, 0x0285, 0x0284, 0x0283, 0x0281, 0x0280, 0x027f, 0x027d, 0x027c, 0x027b, 0x0279,
   0x0278, 0x0277, 0x0275, 0x0274, 0x0273, 0x0271, 0x0270, 0x026f, 0x026d, 0x026c, 0x026b, 0x0269,
   0x0268, 0x0267, 0x0265, 0x0264, 0x0263, 0x0261, 0x0260, 0x025f, 0x025d, 0x025c, 0x025b, 0x025a,
   0x0258, 0x0257, 0x0256, 0x0254, 0x0253, 0x0252, 0x0250, 0x024f, 0x024e, 0x024c, 0x024b, 0x024a,
   0x0248, 0x0247, 0x0246, 0x0245, 0x0243, 0x0242, 0x0241, 0x023f, 0x023e, 0x023d, 0x023b, 0x023a,
   0x0239, 0x0238, 0x0236, 0x0235, 0x0234, 0x0232, 0x0231, 0x0230, 0x022f, 0x022d, 0x022c, 0x022b,
   0x0229, 0x0228, 0x0227, 0x0225, 0x0224, 0x0223, 0x0222, 0x0220, 0x021f, 0x021e, 0x021d, 0x021b,
   0x021a, 0x0219, 0x0217, 0x0216, 0x0215, 0x0214, 0x0212, 0x0211, 0x0210, 0x020e, 0x020d, 0x020c,
   0x020b, 0x0209, 0x0208, 0x0207, 0x0206, 0x0204, 0x0203, 0x0202, 0x0200, 0x01ff, 0x01fe, 0x01fd,
   0x01fb, 0x01fa, 0x01f9, 0x01f8, 0x01f6, 0x01f5, 0x01f4, 0x01f3, 0x01f1, 0x01f0, 0x01ef, 0x01ee,
   0x01ec, 0x01eb, 0x01ea, 0x01e9, 0x01e7, 0x01e6, 0x01e5, 0x01e4, 0x01e2, 0x01e1, 0x01e0, 0x01df,
   0x01dd, 0x01dc, 0x01db, 0x01da, 0x01d8, 0x01d7, 0x01d6, 0x01d5, 0x01d3, 0x01d2, 0x01d1, 0x01d0,
   0x01ce, 0x01cd, 0x01cc, 0x01cb, 0x01c9, 0x01c8, 0x01c7, 0x01c6, 0x01c5, 0x01c3, 0x01c2, 0x01c1,
   0x01c0, 0x01be, 0x01bd, 0x01bc, 0x01bb, 0x01b9, 0x01b8, 0x01b7, 0x01b6, 0x01b5, 0x01b3, 0x01b2,
   0x01b1, 0x01b0, 0x01ae, 0x01ad, 0x01ac, 0x01ab, 0x01aa, 0x01a8, 0x01a7, 0x01a6, 0x01a5, 0x01a3,
   0x01a2, 0x01a1, 0x01a0, 0x019f, 0x019d, 0x019c, 0x019b, 0x019a, 0x0199, 0x0197, 0x0196, 0x0195,
   0x0194, 0x0193, 0x0191, 0x0190, 0x018f, 0x018e, 0x018c, 0x018b, 0x018a, 0x0189, 0x0188, 0x0186,
   0x0185, 0x0184, 0x0183, 0x0182, 0x0180, 0x017f, 0x017e, 0x017d, 0x017c, 0x017b, 0x0179, 0x0178,
   0x0177, 0x0176, 0x0175, 0x0173, 0x0172, 0x0171, 0x0170, 0x016f, 0x016d, 0x016c, 0x016b, 0x016a,
   0x0169, 0x0168, 0x0166, 0x0165, 0x0164, 0x0163, 0x0162, 0x0160, 0x015f, 0x015e, 0x015d, 0x015c,
   0x015b, 0x0159, 0x0158, 0x0157, 0x0156, 0x0155, 0x0153, 0x0152, 0x0151, 0x0150, 0x014f, 0x014e,
   0x014c, 0x014b, 0x014a, 0x0149, 0x0148, 0x0147, 0x0145, 0x0144, 0x0143, 0x0142, 0x0141, 0x0140,
   0x013e, 0x013d, 0x013c, 0x013b, 0x013a, 0x0139, 0x0138, 0x0136, 0x0135, 0x0134, 0x0133, 0x0132,
   0x0131, 0x012f, 0x012e, 0x012d, 0x012c, 0x012b, 0x012a, 0x0129, 0x0127, 0x0126, 0x0125, 0x0124,
   0x0123, 0x0122, 0x0120, 0x011f, 0x011e, 0x011d, 0x011c, 0x011b, 0x011a, 0x0118, 0x0117, 0x0116,
   0x0115, 0x0114, 0x0113, 0x0112, 0x0110, 0x010f, 0x010e, 0x010d, 0x010c, 0x010b, 0x010a, 0x0109,
   0x0107, 0x0106, 0x0105, 0x0104, 0x0103, 0x0102, 0x0101, 0x0100, 0x00fe, 0x00fd, 0x00fc, 0x00fb,
   0x00fa, 0x00f9, 0x00f8, 0x00f6, 0x00f5, 0x00f4, 0x00f3, 0x00f2, 0x00f1, 0x00f0, 0x00ef, 0x00ee,
   0x00ec, 0x00eb, 0x00ea, 0x00e9, 0x00e8, 0x00e7, 0x00e6, 0x00e5, 0x00e3, 0x00e2, 0x00e1, 0x00e0,
   0x00df, 0x00de, 0x00dd, 0x00dc, 0x00db, 0x00d9, 0x00d8, 0x00d7, 0x00d6, 0x00d5, 0x00d4, 0x00d3,
   0x00d2, 0x00d1, 0x00d0, 0x00ce, 0x00cd, 0x00cc, 0x00cb, 0x00ca, 0x00c9, 0x00c8, 0x00c7, 0x00c6,
   0x00c4, 0x00c3, 0x00c2, 0x00c1, 0x00c0, 0x00bf, 0x00be, 0x00bd, 0x00bc, 0x00bb, 0x00ba, 0x00b8,
   0x00b7, 0x00b6, 0x00b5, 0x00b4, 0x00b3, 0x00b2, 0x00b1, 0x00b0, 0x00af, 0x00ae, 0x00ac, 0x00ab,
   0x00aa, 0x00a9, 0x00a8, 0x00a7, 0x00a6, 0x00a5, 0x00a4, 0x00a3, 0x00a2, 0x00a1, 0x009f, 0x009e,
   0x009d, 0x009c, 0x009b, 0x009a, 0x0099, 0x0098, 0x0097, 0x0096, 0x0095, 0x0094, 0x0093, 0x0091,
   0x0090, 0x008f, 0x008e, 0x008d, 0x008c, 0x008b, 0x008a, 0x0089, 0x0088, 0x0087, 0x0086, 0x0085,
   0x0084, 0x0083, 0x0081, 0x0080, 0x007f, 0x007e, 0x007d, 0x007c, 0x007b, 0x007a, 0x0079, 0x0078,
   0x0077, 0x0076, 0x0075, 0x0074, 0x0073, 0x0072, 0x0071, 0x006f, 0x006e, 0x006d, 0x006c, 0x006b,
   0x006a, 0x0069, 0x0068, 0x0067, 0x0066, 0x0065, 0x0064, 0x0063, 0x0062, 0x0061, 0x0060, 0x005f,
   0x005e, 0x005d, 0x005b, 0x005a, 0x0059, 0x0058, 0x0057, 0x0056, 0x0055, 0x0054, 0x0053, 0x0052,
   0x0051, 0x0050, 0x004f, 0x004e, 0x004d, 0x004c, 0x004b, 0x004a, 0x0049, 0x0048, 0x0047, 0x0046,
   0x0045, 0x0044, 0x0043, 0x0042, 0x0040, 0x003f, 0x003e, 0x003d, 0x003c, 0x003b, 0x003a, 0x0039,
   0x0038, 0x0037, 0x0036, 0x0035, 0x0034, 0x0033, 0x0032, 0x0031, 0x0030, 0x002f, 0x002e, 0x002d,
   0x002c, 0x002b, 0x002a, 0x0029, 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
   0x0020, 0x001f, 0x001e, 0x001d, 0x001c, 0x001b, 0x001a, 0x0019, 0x0018, 0x0017, 0x0016, 0x0015,
   0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000f, 0x000e, 0x000d, 0x000c, 0x000b, 0x000a, 0x0009,
   0x0008, 0x0007, 0x0006, 0x0005, 0x0004, 0x0003, 0x0002, 0x0001,
};
static const unsigned short fl_rsqrt_tab[2048] = {
   0x0ffe, 0x0ffa, 0x0ff6, 0x0ff2, 0x0fee, 0x0fea, 0x0fe6, 0x0fe2, 0x0fde, 0x0fda, 0x0fd6, 0x0fd2,
   0x0fce, 0x0fcb, 0x0fc7, 0x0fc3, 0x0fbf, 0x0fbb, 0x0fb7, 0x0fb3, 0x0faf, 0x0fab, 0x0fa7, 0x0fa4,
   0x0fa0, 0x0f9c, 0x0f98, 0x0f94, 0x0f90, 0x0f8c, 0x0f89, 0x0f85, 0x0f81, 0x0f7d, 0x0f79, 0x0f76,
   0x0f72, 0x0f6e, 0x0f6a, 0x0f66, 0x0f63, 0x0f5f, 0x0f5b, 0x0f57, 0x0f54, 0x0f50, 0x0f4c, 0x0f48,
   0x0f45, 0x0f41, 0x0f3d, 0x0f39, 0x0f36, 0x0f32, 0x0f2e, 0x0f2b, 0x0f27, 0x0f23, 0x0f20, 0x0f1c,
   0x0f18, 0x0f15, 0x0f11, 0x0f0d, 0x0f0a, 0x0f06, 0x0f02, 0x0eff, 0x0efb, 0x0ef7, 0x0ef4, 0x0ef0,
   0x0eed, 0x0ee9, 0x0ee5, 0x0ee2, 0x0ede, 0x0edb, 0x0ed7, 0x0ed3, 0x0ed0, 0x0ecc, 0x0ec9, 0x0ec5,
   0x0ec2, 0x0ebe, 0x0eba, 0x0eb7, 0x0eb3, 0x0eb0, 0x0eac, 0x0ea9, 0x0ea5, 0x0ea2, 0x0e9e, 0x0e9b,
   0x0e97, 0x0e94, 0x0e90, 0x0e8d, 0x0e89, 0x0e86, 0x0e82, 0x0e7f, 0x0e7b, 0x0e78, 0x0e75, 0x0e71,
   0x0e6e, 0x0e6a, 0x0e67, 0x0e63, 0x0e60, 0x0e5d, 0x0e59, 0x0e56, 0x0e52, 0x0e4f, 0x0e4c, 0x0e48,
   0x0e45, 0x0e41, 0x0e3e, 0x0e3b, 0x0e37, 0x0e34, 0x0e31, 0x0e2d, 0x0e2a, 0x0e26, 0x0e23, 0x0e20,
   0x0e1c, 0x0e19, 0x0e16, 0x0e12, 0x0e0f, 0x0e0c, 0x0e09, 0x0e05, 0x0e02, 0x0dff, 0x0dfb, 0x0df8,
   0x0df5, 0x0df1, 0x0dee, 0x0deb, 0x0de8, 0x0de4, 0x0de1, 0x0dde, 0x0ddb, 0x0dd7, 0x0dd4, 0x0dd1,
   0x0dce, 0x0dca, 0x0dc7, 0x0dc4, 0x0dc1, 0x0dbe, 0x0dba, 0x0db7, 0x0db4, 0x0db1, 0x0dae, 0x0daa,
   0x0da7, 0x0da4, 0x0da1, 0x0d9e, 0x0d9b, 0x0d97, 0x0d94, 0x0d91, 0x0d8e, 0x0d8b, 0x0d88, 0x0d84,
   0x0d81, 0x0d7e, 0x0d7b, 0x0d78, 0x0d75, 0x0d72, 0x0d6f, 0x0d6b, 0x0d68, 0x0d65, 0x0d62, 0x0d5f,
   0x0d5c, 0x0d59, 0x0d56, 0x0d53, 0x0d50, 0x0d4d, 0x0d49, 0x0d46, 0x0d43, 0x0d40, 0x0d3d, 0x0d3a,
   0x0d37, 0x0d34, 0x0d31, 0x0d2e, 0x0d2b, 0x0d28, 0x0d25, 0x0d22, 0x0d1f, 0x0d1c, 0x0d19, 0x0d16,
   0x0d13, 0x0d10, 0x0d0d, 0x0d0a, 0x0d07, 0x0d04, 0x0d01, 0x0cfe, 0x0cfb, 0x0cf8, 0x0cf5, 0x0cf2,
   0x0cef, 0x0cec, 0x0ce9, 0x0ce6, 0x0ce3, 0x0ce0, 0x0cdd, 0x0cdb, 0x0cd8, 0x0cd5, 0x0cd2, 0x0ccf,
   0x0ccc, 0x0cc9, 0x0cc6, 0x0cc3, 0x0cc0, 0x0cbd, 0x0cba, 0x0cb8, 0x0cb5, 0x0cb2, 0x0caf, 0x0cac,
   0x0ca9, 0x0ca6, 0x0ca3, 0x0ca1, 0x0c9e, 0x0c9b, 0x0c98, 0x0c95, 0x0c92, 0x0c8f, 0x0c8d, 0x0c8a,
   0x0c87, 0x0c84, 0x0c81, 0x0c7e, 0x0c7c, 0x0c79, 0x0c76, 0x0c73, 0x0c70, 0x0c6e, 0x0c6b, 0x0c68,
   0x0c65, 0x0c62, 0x0c60, 0x0c5d, 0x0c5a, 0x0c57, 0x0c54, 0x0c52, 0x0c4f, 0x0c4c, 0x0c49, 0x0c47,
   0x0c44, 0x0c41, 0x0c3e, 0x0c3c, 0x0c39, 0x0c36, 0x0c33, 0x0c31, 0x0c2e, 0x0c2b, 0x0c28, 0x0c26,
   0x0c23, 0x0c20, 0x0c1e, 0x0c1b, 0x0c18, 0x0c15, 0x0c13, 0x0c10, 0x0c0d, 0x0c0b, 0x0c08, 0x0c05,
   0x0c03, 0x0c00, 0x0bfd, 0x0bfb, 0x0bf8, 0x0bf5, 0x0bf3, 0x0bf0, 0x0bed, 0x0beb, 0x0be8, 0x0be5,
   0x0be3, 0x0be0, 0x0bdd, 0x0bdb, 0x0bd8, 0x0bd5, 0x0bd3, 0x0bd0, 0x0bce, 0x0bcb, 0x0bc8, 0x0bc6,
   0x0bc3, 0x0bc0, 0x0bbe, 0x0bbb, 0x0bb9, 0x0bb6, 0x0bb3, 0x0bb1, 0x0bae, 0x0bac, 0x0ba9, 0x0ba7,
   0x0ba4, 0x0ba1, 0x0b9f, 0x0b9c, 0x0b9a, 0x0b97, 0x0b95, 0x0b92, 0x0b8f, 0x0b8d, 0x0b8a, 0x0b88,
   0x0b85, 0x0b83, 0x0b80, 0x0b7e, 0x0b7b, 0x0b79, 0x0b76, 0x0b73, 0x0b71, 0x0b6e, 0x0b6c, 0x0b69,
   0x0b67, 0x0b64, 0x0b62, 0x0b5f, 0x0b5d, 0x0b5a, 0x0b58, 0x0b55, 0x0b53, 0x0b50, 0x0b4e, 0x0b4b,
   0x0b49, 0x0b46, 0x0b44, 0x0b41, 0x0b3f, 0x0b3d, 0x0b3a, 0x0b38, 0x0b35, 0x0b33, 0x0b30, 0x0b2e,
   0x0b2b, 0x0b29, 0x0b26, 0x0b24, 0x0b22, 0x0b1f, 0x0b1d, 0x0b1a, 0x0b18, 0x0b15, 0x0b13, 0x0b11,
   0x0b0e, 0x0b0c, 0x0b09, 0x0b07, 0x0b05, 0x0b02, 0x0b00, 0x0afd, 0x0afb, 0x0af9, 0x0af6, 0x0af4,
   0x0af1, 0x0aef, 0x0aed, 0x0aea, 0x0ae8, 0x0ae5, 0x0ae3, 0x0ae1, 0x0ade, 0x0adc, 0x0ada, 0x0ad7,
   0x0ad5, 0x0ad3, 0x0ad0, 0x0ace, 0x0acb, 0x0ac9, 0x0ac7, 0x0ac4, 0x0ac2, 0x0ac0, 0x0abd, 0x0abb,
   0x0ab9, 0x0ab6, 0x0ab4, 0x0ab2, 0x0aaf, 0x0aad, 0x0aab, 0x0aa8, 0x0aa6, 0x0aa4, 0x0aa2, 0x0a9f,
   0x0a9d, 0x0a9b, 0x0a98, 0x0a96, 0x0a94, 0x0a91, 0x0a8f, 0x0a8d, 0x0a8b, 0x0a88, 0x0a86, 0x0a84,
   0x0a82, 0x0a7f, 0x0a7d, 0x0a7b, 0x0a78, 0x0a76, 0x0a74, 0x0a72, 0x0a6f, 0x0a6d, 0x0a6b, 0x0a69,
   0x0a66, 0x0a64, 0x0a62, 0x0a60, 0x0a5d, 0x0a5b, 0x0a59, 0x0a57, 0x0a55, 0x0a52, 0x0a50, 0x0a4e,
   0x0a4c, 0x0a49, 0x0a47, 0x0a45, 0x0a43, 0x0a41, 0x0a3e, 0x0a3c, 0x0a3a, 0x0a38, 0x0a36, 0x0a33,
   0x0a31, 0x0a2f, 0x0a2d, 0x0a2b, 0x0a28, 0x0a26, 0x0a24, 0x0a22, 0x0a20, 0x0a1d, 0x0a1b, 0x0a19,
   0x0a17, 0x0a15, 0x0a13, 0x0a10, 0x0a0e, 0x0a0c, 0x0a0a, 0x0a08, 0x0a06, 0x0a04, 0x0a01, 0x09ff,
   0x09fd, 0x09fb, 0x09f9, 0x09f7, 0x09f5, 0x09f2, 0x09f0, 0x09ee, 0x09ec, 0x09ea, 0x09e8, 0x09e6,
   0x09e4, 0x09e1, 0x09df, 0x09dd, 0x09db, 0x09d9, 0x09d7, 0x09d5, 0x09d3, 0x09d1, 0x09ce, 0x09cc,
   0x09ca, 0x09c8, 0x09c6, 0x09c4, 0x09c2, 0x09c0, 0x09be, 0x09bc, 0x09ba, 0x09b7, 0x09b5, 0x09b3,
   0x09b1, 0x09af, 0x09ad, 0x09ab, 0x09a9, 0x09a7, 0x09a5, 0x09a3, 0x09a1, 0x099f, 0x099d, 0x099b,
   0x0999, 0x0997, 0x0994, 0x0992, 0x0990, 0x098e, 0x098c, 0x098a, 0x0988, 0x0986, 0x0984, 0x0982,
   0x0980, 0x097e, 0x097c, 0x097a, 0x0978, 0x0976, 0x0974, 0x0972, 0x0970, 0x096e, 0x096c, 0x096a,
   0x0968, 0x0966, 0x0964, 0x0962, 0x0960, 0x095e, 0x095c, 0x095a, 0x0958, 0x0956, 0x0954, 0x0952,
   0x0950, 0x094e, 0x094c, 0x094a, 0x0948, 0x0946, 0x0944, 0x0942, 0x0940, 0x093e, 0x093c, 0x093a,
   0x0938, 0x0937, 0x0935, 0x0933, 0x0931, 0x092f, 0x092d, 0x092b, 0x0929, 0x0927, 0x0925, 0x0923,
   0x0921, 0x091f, 0x091d, 0x091b, 0x0919, 0x0917, 0x0916, 0x0914, 0x0912, 0x0910, 0x090e, 0x090c,
   0x090a, 0x0908, 0x0906, 0x0904, 0x0902, 0x0900, 0x08ff, 0x08fd, 0x08fb, 0x08f9, 0x08f7, 0x08f5,
   0x08f3, 0x08f1, 0x08ef, 0x08ed, 0x08ec, 0x08ea, 0x08e8, 0x08e6, 0x08e4, 0x08e2, 0x08e0, 0x08de,
   0x08dc, 0x08db, 0x08d9, 0x08d7, 0x08d5, 0x08d3, 0x08d1, 0x08cf, 0x08ce, 0x08cc, 0x08ca, 0x08c8,
   0x08c6, 0x08c4, 0x08c2, 0x08c1, 0x08bf, 0x08bd, 0x08bb, 0x08b9, 0x08b7, 0x08b5, 0x08b4, 0x08b2,
   0x08b0, 0x08ae, 0x08ac, 0x08aa, 0x08a9, 0x08a7, 0x08a5, 0x08a3, 0x08a1, 0x089f, 0x089e, 0x089c,
   0x089a, 0x0898, 0x0896, 0x0895, 0x0893, 0x0891, 0x088f, 0x088d, 0x088c, 0x088a, 0x0888, 0x0886,
   0x0884, 0x0883, 0x0881, 0x087f, 0x087d, 0x087b, 0x087a, 0x0878, 0x0876, 0x0874, 0x0872, 0x0871,
   0x086f, 0x086d, 0x086b, 0x086a, 0x0868, 0x0866, 0x0864, 0x0862, 0x0861, 0x085f, 0x085d, 0x085b,
   0x085a, 0x0858, 0x0856, 0x0854, 0x0853, 0x0851, 0x084f, 0x084d, 0x084c, 0x084a, 0x0848, 0x0846,
   0x0845, 0x0843, 0x0841, 0x083f, 0x083e, 0x083c, 0x083a, 0x0838, 0x0837, 0x0835, 0x0833, 0x0831,
   0x0830, 0x082e, 0x082c, 0x082b, 0x0829, 0x0827, 0x0825, 0x0824, 0x0822, 0x0820, 0x081f, 0x081d,
   0x081b, 0x0819, 0x0818, 0x0816, 0x0814, 0x0813, 0x0811, 0x080f, 0x080d, 0x080c, 0x080a, 0x0808,
   0x0807, 0x0805, 0x0803, 0x0802, 0x0800, 0x07fe, 0x07fd, 0x07fb, 0x07f9, 0x07f7, 0x07f6, 0x07f4,
   0x07f2, 0x07f1, 0x07ef, 0x07ed, 0x07ec, 0x07ea, 0x07e8, 0x07e7, 0x07e5, 0x07e3, 0x07e2, 0x07e0,
   0x07de, 0x07dd, 0x07db, 0x07d9, 0x07d8, 0x07d6, 0x07d4, 0x07d3, 0x07d1, 0x07d0, 0x07ce, 0x07cc,
   0x07cb, 0x07c9, 0x07c7, 0x07c6, 0x07c4, 0x07c2, 0x07c1, 0x07bf, 0x07be, 0x07bc, 0x07ba, 0x07b9,
   0x07b7, 0x07b5, 0x07b4, 0x07b2, 0x07b0, 0x07af, 0x07ad, 0x07ac, 0x07aa, 0x07a8, 0x07a7, 0x07a5,
   0x07a4, 0x07a2, 0x07a0, 0x079f, 0x079d, 0x079b, 0x079a, 0x0798, 0x0797, 0x0795, 0x0793, 0x0792,
   0x0790, 0x078f, 0x078d, 0x078b, 0x078a, 0x0788, 0x0787, 0x0785, 0x0784, 0x0782, 0x0780, 0x077f,
   0x077d, 0x077c, 0x077a, 0x0778, 0x0777, 0x0775, 0x0774, 0x0772, 0x0771, 0x076f, 0x076d, 0x076c,
   0x076a, 0x0769, 0x0767, 0x0766, 0x0764, 0x0762, 0x0761, 0x075f, 0x075e, 0x075c, 0x075b, 0x0759,
   0x0758, 0x0756, 0x0754, 0x0753, 0x0751, 0x0750, 0x074e, 0x074d, 0x074b, 0x074a, 0x0748, 0x0747,
   0x0745, 0x0744, 0x0742, 0x0740, 0x073f, 0x073d, 0x073c, 0x073a, 0x0739, 0x0737, 0x0736, 0x0734,
   0x0733, 0x0731, 0x0730, 0x072e, 0x072d, 0x072b, 0x072a, 0x0728, 0x0727, 0x0725, 0x0723, 0x0722,
   0x0720, 0x071f, 0x071d, 0x071c, 0x071a, 0x0719, 0x0717, 0x0716, 0x0714, 0x0713, 0x0711, 0x0710,
   0x070e, 0x070d, 0x070b, 0x070a, 0x0708, 0x0707, 0x0705, 0x0704, 0x0703, 0x0701, 0x0700, 0x06fe,
   0x06fd, 0x06fb, 0x06fa, 0x06f8, 0x06f7, 0x06f5, 0x06f4, 0x06f2, 0x06f1, 0x06ef, 0x06ee, 0x06ec,
   0x06eb, 0x06e9, 0x06e8, 0x06e6, 0x06e5, 0x06e4, 0x06e2, 0x06e1, 0x06df, 0x06de, 0x06dc, 0x06db,
   0x06d9, 0x06d8, 0x06d6, 0x06d5, 0x06d3, 0x06d2, 0x06d1, 0x06cf, 0x06ce, 0x06cc, 0x06cb, 0x06c9,
   0x06c8, 0x06c6, 0x06c5, 0x06c4, 0x06c2, 0x06c1, 0x06bf, 0x06be, 0x06bc, 0x06bb, 0x06ba, 0x06b8,
   0x06b7, 0x06b5, 0x06b4, 0x06b2, 0x06b1, 0x06b0, 0x06ae, 0x06ad, 0x06ab, 0x06aa, 0x06a8, 0x06a7,
   0x06a6, 0x06a4, 0x06a3, 0x06a1, 0x069f, 0x069c, 0x069a, 0x0697, 0x0694, 0x0691, 0x068e, 0x068c,
   0x0689, 0x0686, 0x0683, 0x0680, 0x067e, 0x067b, 0x0678, 0x0675, 0x0673, 0x0670, 0x066d, 0x066a,
   0x0667, 0x0665, 0x0662, 0x065f, 0x065d, 0x065a, 0x0657, 0x0654, 0x0652, 0x064f, 0x064c, 0x064a,
   0x0647, 0x0644, 0x0641, 0x063f, 0x063c, 0x0639, 0x0637, 0x0634, 0x0631, 0x062f, 0x062c, 0x0629,
   0x0627, 0x0624, 0x0621, 0x061f, 0x061c, 0x0619, 0x0617, 0x0614, 0x0612, 0x060f, 0x060c, 0x060a,
   0x0607, 0x0605, 0x0602, 0x05ff, 0x05fd, 0x05fa, 0x05f8, 0x05f5, 0x05f2, 0x05f0, 0x05ed, 0x05eb,
   0x05e8, 0x05e6, 0x05e3, 0x05e0, 0x05de, 0x05db, 0x05d9, 0x05d6, 0x05d4, 0x05d1, 0x05cf, 0x05cc,
   0x05ca, 0x05c7, 0x05c4, 0x05c2, 0x05bf, 0x05bd, 0x05ba, 0x05b8, 0x05b5, 0x05b3, 0x05b0, 0x05ae,
   0x05ab, 0x05a9, 0x05a7, 0x05a4, 0x05a2, 0x059f, 0x059d, 0x059a, 0x0598, 0x0595, 0x0593, 0x0590,
   0x058e, 0x058b, 0x0589, 0x0587, 0x0584, 0x0582, 0x057f, 0x057d, 0x057a, 0x0578, 0x0576, 0x0573,
   0x0571, 0x056e, 0x056c, 0x056a, 0x0567, 0x0565, 0x0562, 0x0560, 0x055e, 0x055b, 0x0559, 0x0557,
   0x0554, 0x0552, 0x054f, 0x054d, 0x054b, 0x0548, 0x0546, 0x0544, 0x0541, 0x053f, 0x053d, 0x053a,
   0x0538, 0x0536, 0x0533, 0x0531, 0x052f, 0x052c, 0x052a, 0x0528, 0x0525, 0x0523, 0x0521, 0x051e,
   0x051c, 0x051a, 0x0518, 0x0515, 0x0513, 0x0511, 0x050e, 0x050c, 0x050a, 0x0508, 0x0505, 0x0503,
   0x0501, 0x04ff, 0x04fc, 0x04fa, 0x04f8, 0x04f6, 0x04f3, 0x04f1, 0x04ef, 0x04ed, 0x04ea, 0x04e8,
   0x04e6, 0x04e4, 0x04e1, 0x04df, 0x04dd, 0x04db, 0x04d9, 0x04d6, 0x04d4, 0x04d2, 0x04d0, 0x04ce,
   0x04cb, 0x04c9, 0x04c7, 0x04c5, 0x04c3, 0x04c0, 0x04be, 0x04bc, 0x04ba, 0x04b8, 0x04b6, 0x04b3,
   0x04b1, 0x04af, 0x04ad, 0x04ab, 0x04a9, 0x04a6, 0x04a4, 0x04a2, 0x04a0, 0x049e, 0x049c, 0x049a,
   0x0497, 0x0495, 0x0493, 0x0491, 0x048f, 0x048d, 0x048b, 0x0489, 0x0486, 0x0484, 0x0482, 0x0480,
   0x047e, 0x047c, 0x047a, 0x0478, 0x0476, 0x0474, 0x0471, 0x046f, 0x046d, 0x046b, 0x0469, 0x0467,
   0x0465, 0x0463, 0x0461, 0x045f, 0x045d, 0x045b, 0x0459, 0x0457, 0x0455, 0x0452, 0x0450, 0x044e,
   0x044c, 0x044a, 0x0448, 0x0446, 0x0444, 0x0442, 0x0440, 0x043e, 0x043c, 0x043a, 0x0438, 0x0436,
   0x0434, 0x0432, 0x0430, 0x042e, 0x042c, 0x042a, 0x0428, 0x0426, 0x0424, 0x0422, 0x0420, 0x041e,
   0x041c, 0x041a, 0x0418, 0x0416, 0x0414, 0x0412, 0x0410, 0x040e, 0x040c, 0x040a, 0x0408, 0x0406,
   0x0404, 0x0402, 0x0400, 0x03fe, 0x03fd, 0x03fb, 0x03f9, 0x03f7, 0x03f5, 0x03f3, 0x03f1, 0x03ef,
   0x03ed, 0x03eb, 0x03e9, 0x03e7, 0x03e5, 0x03e3, 0x03e1, 0x03e0, 0x03de, 0x03dc, 0x03da, 0x03d8,
   0x03d6, 0x03d4, 0x03d2, 0x03d0, 0x03ce, 0x03cc, 0x03cb, 0x03c9, 0x03c7, 0x03c5, 0x03c3, 0x03c1,
   0x03bf, 0x03bd, 0x03bc, 0x03ba, 0x03b8, 0x03b6, 0x03b4, 0x03b2, 0x03b0, 0x03ae, 0x03ad, 0x03ab,
   0x03a9, 0x03a7, 0x03a5, 0x03a3, 0x03a1, 0x03a0, 0x039e, 0x039c, 0x039a, 0x0398, 0x0396, 0x0395,
   0x0393, 0x0391, 0x038f, 0x038d, 0x038b, 0x038a, 0x0388, 0x0386, 0x0384, 0x0382, 0x0381, 0x037f,
   0x037d, 0x037b, 0x0379, 0x0378, 0x0376, 0x0374, 0x0372, 0x0370, 0x036f, 0x036d, 0x036b, 0x0369,
   0x0367, 0x0366, 0x0364, 0x0362, 0x0360, 0x035e, 0x035d, 0x035b, 0x0359, 0x0357, 0x0356, 0x0354,
   0x0352, 0x0350, 0x034f, 0x034d, 0x034b, 0x0349, 0x0348, 0x0346, 0x0344, 0x0342, 0x0341, 0x033f,
   0x033d, 0x033b, 0x033a, 0x0338, 0x0336, 0x0334, 0x0333, 0x0331, 0x032f, 0x032e, 0x032c, 0x032a,
   0x0328, 0x0327, 0x0325, 0x0323, 0x0322, 0x0320, 0x031e, 0x031c, 0x031b, 0x0319, 0x0317, 0x0316,
   0x0314, 0x0312, 0x0311, 0x030f, 0x030d, 0x030b, 0x030a, 0x0308, 0x0306, 0x0305, 0x0303, 0x0301,
   0x0300, 0x02fe, 0x02fc, 0x02fb, 0x02f9, 0x02f7, 0x02f6, 0x02f4, 0x02f2, 0x02f1, 0x02ef, 0x02ed,
   0x02ec, 0x02ea, 0x02e8, 0x02e7, 0x02e5, 0x02e3, 0x02e2, 0x02e0, 0x02df, 0x02dd, 0x02db, 0x02da,
   0x02d8, 0x02d6, 0x02d5, 0x02d3, 0x02d1, 0x02d0, 0x02ce, 0x02cd, 0x02cb, 0x02c9, 0x02c8, 0x02c6,
   0x02c5, 0x02c3, 0x02c1, 0x02c0, 0x02be, 0x02bc, 0x02bb, 0x02b9, 0x02b8, 0x02b6, 0x02b4, 0x02b3,
   0x02b1, 0x02b0, 0x02ae, 0x02ac, 0x02ab, 0x02a9, 0x02a8, 0x02a6, 0x02a5, 0x02a3, 0x02a1, 0x02a0,
   0x029e, 0x029d, 0x029b, 0x029a, 0x0298, 0x0296, 0x0295, 0x0293, 0x0292, 0x0290, 0x028f, 0x028d,
   0x028b, 0x028a, 0x0288, 0x0287, 0x0285, 0x0284, 0x0282, 0x0281, 0x027f, 0x027e, 0x027c, 0x027a,
   0x0279, 0x0277, 0x0276, 0x0274, 0x0273, 0x0271, 0x0270, 0x026e, 0x026d, 0x026b, 0x026a, 0x0268,
   0x0267, 0x0265, 0x0263, 0x0262, 0x0260, 0x025f, 0x025d, 0x025c, 0x025a, 0x0259, 0x0257, 0x0256,
   0x0254, 0x0253, 0x0251, 0x0250, 0x024e, 0x024d, 0x024b, 0x024a, 0x0248, 0x0247, 0x0245, 0x0244,
   0x0242, 0x0241, 0x023f, 0x023e, 0x023d, 0x023b, 0x023a, 0x0238, 0x0237, 0x0235, 0x0234, 0x0232,
   0x0231, 0x022f, 0x022e, 0x022c, 0x022b, 0x0229, 0x0228, 0x0226, 0x0225, 0x0224, 0x0222, 0x0221,
   0x021f, 0x021e, 0x021c, 0x021b, 0x0219, 0x0218, 0x0216, 0x0215, 0x0214, 0x0212, 0x0211, 0x020f,
   0x020e, 0x020c, 0x020b, 0x020a, 0x0208, 0x0207, 0x0205, 0x0204, 0x0202, 0x0201, 0x0200, 0x01fe,
   0x01fd, 0x01fb, 0x01fa, 0x01f8, 0x01f7, 0x01f6, 0x01f4, 0x01f3, 0x01f1, 0x01f0, 0x01ef, 0x01ed,
   0x01ec, 0x01ea, 0x01e9, 0x01e8, 0x01e6, 0x01e5, 0x01e3, 0x01e2, 0x01e1, 0x01df, 0x01de, 0x01dc,
   0x01db, 0x01da, 0x01d8, 0x01d7, 0x01d5, 0x01d4, 0x01d3, 0x01d1, 0x01d0, 0x01cf, 0x01cd, 0x01cc,
   0x01ca, 0x01c9, 0x01c8, 0x01c6, 0x01c5, 0x01c4, 0x01c2, 0x01c1, 0x01bf, 0x01be, 0x01bd, 0x01bb,
   0x01ba, 0x01b9, 0x01b7, 0x01b6, 0x01b5, 0x01b3, 0x01b2, 0x01b0, 0x01af, 0x01ae, 0x01ac, 0x01ab,
   0x01aa, 0x01a8, 0x01a7, 0x01a6, 0x01a4, 0x01a3, 0x01a2, 0x01a0, 0x019f, 0x019e, 0x019c, 0x019b,
   0x019a, 0x0198, 0x0197, 0x0196, 0x0194, 0x0193, 0x0192, 0x0190, 0x018f, 0x018e, 0x018c, 0x018b,
   0x018a, 0x0188, 0x0187, 0x0186, 0x0185, 0x0183, 0x0182, 0x0181, 0x017f, 0x017e, 0x017d, 0x017b,
   0x017a, 0x0179, 0x0177, 0x0176, 0x0175, 0x0174, 0x0172, 0x0171, 0x0170, 0x016e, 0x016d, 0x016c,
   0x016b, 0x0169, 0x0168, 0x0167, 0x0165, 0x0164, 0x0163, 0x0162, 0x0160, 0x015f, 0x015e, 0x015c,
   0x015b, 0x015a, 0x0159, 0x0157, 0x0156, 0x0155, 0x0153, 0x0152, 0x0151, 0x0150, 0x014e, 0x014d,
   0x014c, 0x014b, 0x0149, 0x0148, 0x0147, 0x0146, 0x0144, 0x0143, 0x0142, 0x0141, 0x013f, 0x013e,
   0x013d, 0x013c, 0x013a, 0x0139, 0x0138, 0x0137, 0x0135, 0x0134, 0x0133, 0x0132, 0x0130, 0x012f,
   0x012e, 0x012d, 0x012b, 0x012a, 0x0129, 0x0128, 0x0126, 0x0125, 0x0124, 0x0123, 0x0122, 0x0120,
   0x011f, 0x011e, 0x011d, 0x011b, 0x011a, 0x0119, 0x0118, 0x0117, 0x0115, 0x0114, 0x0113, 0x0112,
   0x0110, 0x010f, 0x010e, 0x010d, 0x010c, 0x010a, 0x0109, 0x0108, 0x0107, 0x0106, 0x0104, 0x0103,
   0x0102, 0x0101, 0x0100, 0x00fe, 0x00fd, 0x00fc, 0x00fb, 0x00fa, 0x00f8, 0x00f7, 0x00f6, 0x00f5,
   0x00f4, 0x00f2, 0x00f1, 0x00f0, 0x00ef, 0x00ee, 0x00ed, 0x00eb, 0x00ea, 0x00e9, 0x00e8, 0x00e7,
   0x00e5, 0x00e4, 0x00e3, 0x00e2, 0x00e1, 0x00e0, 0x00de, 0x00dd, 0x00dc, 0x00db, 0x00da, 0x00d9,
   0x00d7, 0x00d6, 0x00d5, 0x00d4, 0x00d3, 0x00d2, 0x00d0, 0x00cf, 0x00ce, 0x00cd, 0x00cc, 0x00cb,
   0x00c9, 0x00c8, 0x00c7, 0x00c6, 0x00c5, 0x00c4, 0x00c3, 0x00c1, 0x00c0, 0x00bf, 0x00be, 0x00bd,
   0x00bc, 0x00bb, 0x00b9, 0x00b8, 0x00b7, 0x00b6, 0x00b5, 0x00b4, 0x00b3, 0x00b1, 0x00b0, 0x00af,
   0x00ae, 0x00ad, 0x00ac, 0x00ab, 0x00a9, 0x00a8, 0x00a7, 0x00a6, 0x00a5, 0x00a4, 0x00a3, 0x00a2,
   0x00a0, 0x009f, 0x009e, 0x009d, 0x009c, 0x009b, 0x009a, 0x0099, 0x0098, 0x0096, 0x0095, 0x0094,
   0x0093, 0x0092, 0x0091, 0x0090, 0x008f, 0x008e, 0x008c, 0x008b, 0x008a, 0x0089, 0x0088, 0x0087,
   0x0086, 0x0085, 0x0084, 0x0082, 0x0081, 0x0080, 0x007f, 0x007e, 0x007d, 0x007c, 0x007b, 0x007a,
   0x0079, 0x0078, 0x0076, 0x0075, 0x0074, 0x0073, 0x0072, 0x0071, 0x0070, 0x006f, 0x006e, 0x006d,
   0x006c, 0x006b, 0x0069, 0x0068, 0x0067, 0x0066, 0x0065, 0x0064, 0x0063, 0x0062, 0x0061, 0x0060,
   0x005f, 0x005e, 0x005d, 0x005b, 0x005a, 0x0059, 0x0058, 0x0057, 0x0056, 0x0055, 0x0054, 0x0053,
   0x0052, 0x0051, 0x0050, 0x004f, 0x004e, 0x004d, 0x004c, 0x004a, 0x0049, 0x0048, 0x0047, 0x0046,
   0x0045, 0x0044, 0x0043, 0x0042, 0x0041, 0x0040, 0x003f, 0x003e, 0x003d, 0x003c, 0x003b, 0x003a,
   0x0039, 0x0038, 0x0037, 0x0036, 0x0034, 0x0033, 0x0032, 0x0031, 0x0030, 0x002f, 0x002e, 0x002d,
   0x002c, 0x002b, 0x002a, 0x0029, 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
   0x0020, 0x001f, 0x001e, 0x001d, 0x001c, 0x001b, 0x001a, 0x0019, 0x0018, 0x0017, 0x0016, 0x0015,
   0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000f, 0x000e, 0x000d, 0x000c, 0x000b, 0x000a, 0x0009,
   0x0008, 0x0007, 0x0006, 0x0005, 0x0004, 0x0003, 0x0002, 0x0001,
};
#endif /* FLAPUS_PORTABLE_MATH */

/* ======== enc_bands.c ======== */
/* ---- CELT encoder: band quantization, PVQ, pitch and prefilter (x87 rounding) ---- */
#include <emmintrin.h>
#include <stddef.h>

/* SSE inner product; the scalar tail runs on the x87 stack. */
static xf fl_e_inner_prod(const float *x, const float *y, int N) {
   float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
   xf xy;
   int i;
   for (i = 0; i < N-3; i += 4) {
      s0 = s0+x[i]*y[i];
      s1 = s1+x[i+1]*y[i+1];
      s2 = s2+x[i+2]*y[i+2];
      s3 = s3+x[i+3]*y[i+3];
   }
   xy = (s0+s2)+(s1+s3);
   for (; i < N; i++) xy += XF(x[i])*y[i];
   return xy;
}
static void fl_e_dual_inner_prod(const float *x, const float *y1, const float *y2, int N, float *xy1, float *xy2) {
   float a0 = 0, a1 = 0, a2 = 0, a3 = 0, b0 = 0, b1 = 0, b2 = 0, b3 = 0;
   int i;
   for (i = 0; i < N-3; i += 4) {
      a0 = a0+x[i]*y1[i]; a1 = a1+x[i+1]*y1[i+1]; a2 = a2+x[i+2]*y1[i+2]; a3 = a3+x[i+3]*y1[i+3];
      b0 = b0+x[i]*y2[i]; b1 = b1+x[i+1]*y2[i+1]; b2 = b2+x[i+2]*y2[i+2]; b3 = b3+x[i+3]*y2[i+3];
   }
   *xy1 = (a0+a2)+(a1+a3);
   *xy2 = (b0+b2)+(b1+b3);
   for (; i < N; i++) {
      *xy1 = (float)(*xy1+XF(x[i])*y1[i]);
      *xy2 = (float)(*xy2+XF(x[i])*y2[i]);
   }
}

static void fl_e_haar1(float *X, int N0, int stride) {
   int i, j;
   N0 >>= 1;
   for (i = 0; i < stride; i++)
      for (j = 0; j < N0; j++) {
         xf t1 = XF(.70710678f)*X[stride*2*j+i], t2 = XF(.70710678f)*X[stride*(2*j+1)+i];
         X[stride*2*j+i] = (float)(t1+t2);
         X[stride*(2*j+1)+i] = (float)(t1-t2);
      }
}

static void fl_e_exp_rotation1(float *X, int len, int stride, float c, float s) {
   int i;
   float ms = -s, *Xptr = X;
   for (i = 0; i < len-stride; i++) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = (float)(XF(c)*x2+XF(s)*x1);
      *Xptr++ = (float)(XF(c)*x1+XF(ms)*x2);
   }
   Xptr = &X[len-2*stride-1];
   for (i = len-2*stride-1; i >= 0; i--) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = (float)(XF(c)*x2+XF(s)*x1);
      *Xptr-- = (float)(XF(c)*x1+XF(ms)*x2);
   }
}
static void fl_e_exp_rotation(float *X, int len, int dir, int stride, int K, int spread) {
   static const int factors[3] = {15, 10, 5};
   int i, stride2 = 0;
   xf gain, theta;
   float theta_f, c, s;
   if (2*K >= len || spread == SPREAD_NONE) return;
   gain = XF(len)/(len+factors[spread-1]*K);
   theta = gain*gain*.5f;
   theta_f = (float)theta;
   c = (float)fl_m_cos((double)(XF(.5f*3.1415926535897931)*theta));
   s = (float)fl_m_cos((double)((1.0L-theta_f)*XF(.5f*3.1415926535897931)));
   if (len >= 8*stride) {
      stride2 = 1;
      while ((stride2*stride2+stride2)*stride+(stride>>2) < len) stride2++;
   }
   len /= stride;
   for (i = 0; i < stride; i++) {
      if (dir < 0) {
         if (stride2) fl_e_exp_rotation1(X+i*len, len, stride2, s, c);
         fl_e_exp_rotation1(X+i*len, len, 1, c, s);
      } else {
         fl_e_exp_rotation1(X+i*len, len, 1, c, -s);
         if (stride2) fl_e_exp_rotation1(X+i*len, len, stride2, s, -c);
      }
   }
}

static void fl_e_renormalise_vector(float *X, int N, float gain) {
   int i;
   float E = (float)(XF(1e-15f)+fl_e_inner_prod(X, X, N));
   xf g = (1.0L/fl_m_sqrtf(E))*gain;
   for (i = 0; i < N; i++) X[i] = (float)(g*X[i]);
}

#ifdef FLAPUS_PORTABLE_MATH
/* rcpps and rsqrtps are approximations whose exact bits are up to the CPU, and the PVQ
   search below uses them, as opusenc does: on an AMD EPYC 7763 the default build wrote
   other packets than on an Intel Core i7-13700K. With FLAPUS_PORTABLE_MATH they come
   from Intel's values in enc_rcp_tables.c instead, so the result is Intel's on every
   CPU. These two functions are the ones tools/gen_rcp.c checks against the hardware
   for all 2^32 inputs; keep them identical. */
static unsigned fl_p_rcp_bits(unsigned x) {
   unsigned s = x & 0x80000000u, e = (x >> 23) & 0xff, t;
   int re;
   if (e == 0) return s | 0x7f800000u;                         /* 0, subnormal: inf */
   if (e == 0xff) return (x & 0x7fffff) ? x | 0x400000u : s;   /* NaN quiet, inf: 0 */
   t = fl_rcp_tab[(x >> 12) & 0x7ff];
   re = (int)(t >> 12)+126+127-(int)e;
   if (re <= 0) return s;                                      /* subnormal result: 0 */
   return s | (unsigned)re << 23 | (t & 0xfff) << 11;
}
static unsigned fl_p_rsqrt_bits(unsigned x) {
   unsigned e = (x >> 23) & 0xff, t;
   int k, re;
   if (e == 0) return (x & 0x80000000u) | 0x7f800000u;         /* +-0, subnormal: +-inf */
   if (e == 0xff) {
      if (x & 0x7fffff) return x | 0x400000u;                  /* NaN quiet */
      return (x & 0x80000000u) ? 0xffc00000u : 0;              /* -inf: NaN, +inf: 0 */
   }
   if (x & 0x80000000u) return 0xffc00000u;                    /* negative: NaN */
   k = ((int)e-127) >> 1;                                      /* x = 4^k * [1,4) */
   t = fl_rsqrt_tab[((e-127) & 1) << 10 | ((x >> 13) & 0x3ff)];
   re = (int)(t >> 12)+126-k;
   return (unsigned)re << 23 | (t & 0xfff) << 11;
}
static __m128 fl_p_lanes(__m128 v, unsigned (*f)(unsigned)) {
   float a[4];
   int i;
   _mm_storeu_ps(a, v);
   for (i = 0; i < 4; i++) {
      unsigned u;
      memcpy(&u, &a[i], 4);
      u = f(u);
      memcpy(&a[i], &u, 4);
   }
   return _mm_loadu_ps(a);
}
#define FL_RCP_PS(v) fl_p_lanes(v, fl_p_rcp_bits)
#define FL_RSQRT_PS(v) fl_p_lanes(v, fl_p_rsqrt_bits)
#else
#define FL_RCP_PS(v) _mm_rcp_ps(v)
#define FL_RSQRT_PS(v) _mm_rsqrt_ps(v)
#endif

/* op_pvq_search_sse2: SIMD search; xy/yy accumulate on the x87 stack. */
static float fl_e_pvq_search(float *_X, int *iy, int K, int N) {
   int i, j, pulsesLeft;
   float y[176+3], X[176+3], signy[176+3], xyf, yyf;
   xf xy, yy;
   __m128 signmask = _mm_set_ps1(-0.f), sums = _mm_setzero_ps();
   __m128i fours = _mm_set_epi32(4, 4, 4, 4);
   memcpy(X, _X, N*sizeof(float));
   X[N] = X[N+1] = X[N+2] = 0;
   for (j = 0; j < N; j += 4) {
      __m128 x4 = _mm_loadu_ps(&X[j]), s4 = _mm_cmplt_ps(x4, _mm_setzero_ps());
      x4 = _mm_andnot_ps(signmask, x4);
      sums = _mm_add_ps(sums, x4);
      _mm_storeu_ps(&y[j], _mm_setzero_ps());
      _mm_storeu_si128((__m128i*)(void*)&iy[j], _mm_setzero_si128());
      _mm_storeu_ps(&X[j], x4);
      _mm_storeu_ps(&signy[j], s4);
   }
   sums = _mm_add_ps(sums, _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(1, 0, 3, 2)));
   sums = _mm_add_ps(sums, _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(2, 3, 0, 1)));
   xyf = yyf = 0;
   pulsesLeft = K;
   if (K > (N>>1)) {
      __m128i pulses_sum = _mm_setzero_si128();
      __m128 yy4 = _mm_setzero_ps(), xy4 = _mm_setzero_ps(), rcp4;
      float sum = _mm_cvtss_f32(sums);
      if (!(sum > 1e-15f && sum < 64)) {
         X[0] = 1.f;
         j = 1;
         do X[j] = 0;
         while (++j < N);
         sums = _mm_set_ps1(1.f);
      }
      rcp4 = _mm_mul_ps(_mm_set_ps1((float)(XF(.8)+K)), FL_RCP_PS(sums));
      for (j = 0; j < N; j += 4) {
         __m128 x4 = _mm_loadu_ps(&X[j]), rx4 = _mm_mul_ps(x4, rcp4), y4;
         __m128i iy4 = _mm_cvttps_epi32(rx4);
         pulses_sum = _mm_add_epi32(pulses_sum, iy4);
         _mm_storeu_si128((__m128i*)(void*)&iy[j], iy4);
         y4 = _mm_cvtepi32_ps(iy4);
         xy4 = _mm_add_ps(xy4, _mm_mul_ps(x4, y4));
         yy4 = _mm_add_ps(yy4, _mm_mul_ps(y4, y4));
         _mm_storeu_ps(&y[j], _mm_add_ps(y4, y4));
      }
      pulses_sum = _mm_add_epi32(pulses_sum, _mm_shuffle_epi32(pulses_sum, _MM_SHUFFLE(1, 0, 3, 2)));
      pulses_sum = _mm_add_epi32(pulses_sum, _mm_shuffle_epi32(pulses_sum, _MM_SHUFFLE(2, 3, 0, 1)));
      pulsesLeft -= _mm_cvtsi128_si32(pulses_sum);
      xy4 = _mm_add_ps(xy4, _mm_shuffle_ps(xy4, xy4, _MM_SHUFFLE(1, 0, 3, 2)));
      xy4 = _mm_add_ps(xy4, _mm_shuffle_ps(xy4, xy4, _MM_SHUFFLE(2, 3, 0, 1)));
      xyf = _mm_cvtss_f32(xy4);
      yy4 = _mm_add_ps(yy4, _mm_shuffle_ps(yy4, yy4, _MM_SHUFFLE(1, 0, 3, 2)));
      yy4 = _mm_add_ps(yy4, _mm_shuffle_ps(yy4, yy4, _MM_SHUFFLE(2, 3, 0, 1)));
      yyf = _mm_cvtss_f32(yy4);
   }
   X[N] = X[N+1] = X[N+2] = -100;
   y[N] = y[N+1] = y[N+2] = 100;
   xy = xyf;
   yy = yyf;
   if (pulsesLeft > N+3) {
      xf tmp = pulsesLeft;
      yy = yy+tmp*tmp;
      yy = yy+tmp*y[0];
      iy[0] += pulsesLeft;
      pulsesLeft = 0;
   }
   for (i = 0; i < pulsesLeft; i++) {
      int best_id;
      __m128 xy4, yy4, max, max2;
      __m128i count, pos;
      yy += 1;
      xyf = (float)xy;
      yyf = (float)yy;
      xy4 = _mm_load1_ps(&xyf);
      yy4 = _mm_load1_ps(&yyf);
      max = _mm_setzero_ps();
      pos = _mm_setzero_si128();
      count = _mm_set_epi32(3, 2, 1, 0);
      for (j = 0; j < N; j += 4) {
         __m128 x4 = _mm_loadu_ps(&X[j]), y4 = _mm_loadu_ps(&y[j]), r4;
         x4 = _mm_add_ps(x4, xy4);
         y4 = _mm_add_ps(y4, yy4);
         y4 = FL_RSQRT_PS(y4);
         r4 = _mm_mul_ps(x4, y4);
         pos = _mm_max_epi16(pos, _mm_and_si128(count, _mm_castps_si128(_mm_cmpgt_ps(r4, max))));
         max = _mm_max_ps(max, r4);
         count = _mm_add_epi32(count, fours);
      }
      max2 = _mm_max_ps(max, _mm_shuffle_ps(max, max, _MM_SHUFFLE(1, 0, 3, 2)));
      max2 = _mm_max_ps(max2, _mm_shuffle_ps(max2, max2, _MM_SHUFFLE(2, 3, 0, 1)));
      pos = _mm_and_si128(pos, _mm_castps_si128(_mm_cmpeq_ps(max, max2)));
      pos = _mm_max_epi16(pos, _mm_unpackhi_epi64(pos, pos));
      pos = _mm_max_epi16(pos, _mm_shufflelo_epi16(pos, _MM_SHUFFLE(1, 0, 3, 2)));
      best_id = _mm_cvtsi128_si32(pos);
      xy += X[best_id];
      yy += y[best_id];
      y[best_id] = (float)(XF(y[best_id])+2);
      iy[best_id]++;
   }
   for (j = 0; j < N; j += 4) {
      __m128i y4 = _mm_loadu_si128((__m128i*)(void*)&iy[j]);
      __m128i s4 = _mm_castps_si128(_mm_loadu_ps(&signy[j]));
      y4 = _mm_xor_si128(_mm_add_epi32(y4, s4), s4);
      _mm_storeu_si128((__m128i*)(void*)&iy[j], y4);
   }
   return (float)yy;
}

static unsigned fl_e_alg_quant(float *X, int N, int K, int spread, int B, ec_ctx *enc, float gain, int resynth) {
   int iy[176+3], i;
   float yy;
   unsigned collapse_mask;
   fl_e_exp_rotation(X, N, 1, B, K, spread);
   yy = fl_e_pvq_search(X, iy, K, N);
   collapse_mask = fl_extract_collapse_mask(iy, N, B);
   fl_encode_pulses(iy, N, K, enc);
   if (resynth) {
      xf g = (1.0L/fl_m_sqrtf(yy))*gain;
      for (i = 0; i < N; i++) X[i] = (float)(g*iy[i]);
      fl_e_exp_rotation(X, N, -1, B, K, spread);
   }
   return collapse_mask;
}

static xf fl_e_atan_norm(float x) {
   xf xs = XF(x)*x;
   xf p = -3.3331659436225891113281250000e-01f+xs*(1.99627041816711425781250000000e-01f
         +xs*(-1.3976582884788513183593750000e-01f+xs*(9.79423448443412780761718750000e-02f
         +xs*(-5.7773590087890625000000000000e-02f+xs*(2.30401363223791122436523437500e-02f
         +xs*(-4.3554059229791164398193359375e-03f))))));
   return (XF(x)+(xs*x)*p)*0.636619772367581f;
}

static int fl_e_stereo_itheta(const float *X, const float *Y, int stereo, int N) {
   int i;
   float Emid, Eside, mid;
   xf side, r;
   if (stereo) {
      xf em = 0, es = 0;
      for (i = 0; i < N; i++) {
         xf m = XF(X[i])+Y[i], s = XF(X[i])-Y[i];
         em += m*m;
         es += s*s;
      }
      Emid = (float)em;
      Eside = (float)es;
   } else {
      Emid = (float)fl_e_inner_prod(X, X, N);
      Eside = (float)fl_e_inner_prod(Y, Y, N);
   }
   mid = (float)fl_m_sqrtf(Emid);
   side = fl_m_sqrtf(Eside);
   if (XF(mid)*mid+side*side < 1e-18f) r = 0;
   else if (side < mid) r = fl_e_atan_norm((float)(side/mid));
   else r = 1.0L-fl_e_atan_norm((float)(mid/side));
   return (int)floorf((float)(r*1073741824.f+.5f));
}

static void fl_e_intensity_stereo(float *X, const float *Y, const float *bandE, int i, int N) {
   int j;
   float left = bandE[i], right = bandE[i+CELT_NB_EBANDS];
   xf norm = XF(1e-15f)+fl_m_sqrtf((float)((XF(left)*left+1e-15f)+XF(right)*right));
   xf a1 = left/norm, a2 = right/norm;
   for (j = 0; j < N; j++) X[j] = (float)(a1*X[j]+a2*Y[j]);
}
static void fl_e_stereo_split(float *X, float *Y, int N) {
   int j;
   for (j = 0; j < N; j++) {
      xf l = XF(.70710678f)*X[j], r = XF(.70710678f)*Y[j];
      X[j] = (float)(l+r);
      Y[j] = (float)(r-l);
   }
}
static void fl_e_stereo_merge(float *X, float *Y, float mid, int N) {
   int j;
   float xp = (float)fl_e_inner_prod(Y, X, N), lgain;
   xf side = fl_e_inner_prod(Y, Y, N), base, El, Er, rgain;
   base = XF(mid)*mid+side;
   El = base-(XF(xp)*mid)*2;
   Er = base+(XF(xp)*mid)*2;
   if (Er < 6e-4f || El < 6e-4f) {
      memcpy(Y, X, N*sizeof(float));
      return;
   }
   lgain = (float)(1.0L/fl_m_sqrtf((float)El));
   rgain = 1.0L/fl_m_sqrtf((float)Er);
   for (j = 0; j < N; j++) {
      xf l = XF(mid)*X[j], r = Y[j];
      X[j] = (float)((l-r)*lgain);
      Y[j] = (float)((l+r)*rgain);
   }
}

typedef struct {
   int resynth, i, intensity, spread, tf_change, theta_round, disable_inv, avoid_split_noise;
   ec_ctx *ec;
   fl_i32 remaining_bits;
   const float *bandE;
   fl_u32 seed;
} fl_band_ectx;
typedef struct { int inv, imid, iside, delta, itheta, qalloc; } fl_split_ectx;

static void fl_e_compute_theta(fl_band_ectx *ctx, fl_split_ectx *sctx, float *X, float *Y, int N, int *b, int B,
      int B0, int LM, int stereo, int *fill) {
   int qn, itheta, delta, imid, iside, qalloc, pulse_cap, offset, inv = 0, i = ctx->i;
   fl_i32 tell;
   ec_ctx *ec = ctx->ec;
   pulse_cap = fl_logN400[i]+LM*(1<<BITRES);
   offset = (pulse_cap>>1)-(stereo && N == 2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
   qn = fl_compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i >= ctx->intensity) qn = 1;
   itheta = fl_e_stereo_itheta(X, Y, stereo, N)>>16;
   tell = ec_tell_frac(ec);
   if (qn != 1) {
      if (!stereo || ctx->theta_round == 0) {
         itheta = (itheta*(fl_i32)qn+8192)>>14;
         if (!stereo && ctx->avoid_split_noise && itheta > 0 && itheta < qn) {
            int unquantized = (fl_i32)itheta*16384/qn;
            imid = fl_bitexact_cos((fl_i16)unquantized);
            iside = fl_bitexact_cos((fl_i16)(16384-unquantized));
            delta = FL_FRAC_MUL16((N-1)<<7, fl_bitexact_log2tan(iside, imid));
            if (delta > *b) itheta = qn;
            else if (delta < -*b) itheta = 0;
         }
      } else {
         int bias = itheta > 8192 ? 32767/qn : -32767/qn;
         int down = IMIN(qn-1, IMAX(0, (itheta*(fl_i32)qn+bias)>>14));
         itheta = ctx->theta_round < 0 ? down : down+1;
      }
      if (stereo && N > 2) {
         int p0 = 3, x = itheta, x0 = qn/2, ft = p0*(x0+1)+x0;
         ec_encode(ec, x <= x0 ? p0*x : (x-1-x0)+(x0+1)*p0, x <= x0 ? p0*(x+1) : (x-x0)+(x0+1)*p0, ft);
      } else if (B0 > 1 || stereo) {
         ec_enc_uint(ec, itheta, qn+1);
      } else {
         int fs, fl, ft = ((qn>>1)+1)*((qn>>1)+1);
         fs = itheta <= (qn>>1) ? itheta+1 : qn+1-itheta;
         fl = itheta <= (qn>>1) ? itheta*(itheta+1)>>1 : ft-((qn+1-itheta)*(qn+2-itheta)>>1);
         ec_encode(ec, fl, fl+fs, ft);
      }
      itheta = (fl_i32)itheta*16384/qn;
      if (stereo) {
         if (itheta == 0) fl_e_intensity_stereo(X, Y, ctx->bandE, i, N);
         else fl_e_stereo_split(X, Y, N);
      }
   } else if (stereo) {
      inv = itheta > 8192 && !ctx->disable_inv;
      if (inv) {
         int j;
         for (j = 0; j < N; j++) Y[j] = -Y[j];
      }
      fl_e_intensity_stereo(X, Y, ctx->bandE, i, N);
      if (*b > 2<<BITRES && ctx->remaining_bits > 2<<BITRES) ec_enc_bit_logp(ec, inv, 2);
      else inv = 0;
      if (ctx->disable_inv) inv = 0;
      itheta = 0;
   }
   qalloc = ec_tell_frac(ec)-tell;
   *b -= qalloc;
   if (itheta == 0) {
      imid = 32767;
      iside = 0;
      *fill &= (1<<B)-1;
      delta = -16384;
   } else if (itheta == 16384) {
      imid = 0;
      iside = 32767;
      *fill &= ((1<<B)-1)<<B;
      delta = 16384;
   } else {
      imid = fl_bitexact_cos((fl_i16)itheta);
      iside = fl_bitexact_cos((fl_i16)(16384-itheta));
      delta = FL_FRAC_MUL16((N-1)<<7, fl_bitexact_log2tan(iside, imid));
   }
   sctx->inv = inv;
   sctx->imid = imid;
   sctx->iside = iside;
   sctx->delta = delta;
   sctx->itheta = itheta;
   sctx->qalloc = qalloc;
}

static unsigned fl_e_quant_band_n1(fl_band_ectx *ctx, float *X, float *Y, float *lowband_out) {
   int c, stereo = Y != NULL;
   float *x = X;
   c = 0;
   do {
      int sign = 0;
      if (ctx->remaining_bits >= 1<<BITRES) {
         sign = x[0] < 0;
         ec_enc_bits(ctx->ec, sign, 1);
         ctx->remaining_bits -= 1<<BITRES;
      }
      if (ctx->resynth) x[0] = sign ? -1.f : 1.f;
      x = Y;
   } while (++c < 1+stereo);
   if (lowband_out) lowband_out[0] = X[0];
   return 1;
}

static unsigned fl_e_quant_partition(fl_band_ectx *ctx, float *X, int N, int b, int B, float *lowband, int LM,
      float gain, int fill) {
   const unsigned char *cache;
   int q, curr_bits, B0 = B, i = ctx->i;
   unsigned cm = 0;
   cache = fl_cache_bits50+fl_cache_index50[(LM+1)*CELT_NB_EBANDS+i];
   if (LM != -1 && b > cache[cache[0]]+12 && N > 2) {
      int mbits, sbits, delta, itheta, qalloc;
      fl_split_ectx sctx;
      float *Y, *next_lowband2 = NULL, mid, side;
      fl_i32 rebalance;
      N >>= 1;
      Y = X+N;
      LM -= 1;
      if (B == 1) fill = (fill&1)|(fill<<1);
      B = (B+1)>>1;
      fl_e_compute_theta(ctx, &sctx, X, Y, N, &b, B, B0, LM, 0, &fill);
      delta = sctx.delta;
      itheta = sctx.itheta;
      qalloc = sctx.qalloc;
      mid = (1.f/32768)*sctx.imid;
      side = (1.f/32768)*sctx.iside;
      if (B0 > 1 && (itheta&0x3fff)) {
         if (itheta > 8192) delta -= delta>>(4-LM);
         else delta = IMIN(0, delta+(N<<BITRES>>(5-LM)));
      }
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;
      if (lowband) next_lowband2 = lowband+N;
      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = fl_e_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
      } else {
         cm = fl_e_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
      }
   } else {
      q = fl_bits2pulses(i, LM, b);
      curr_bits = fl_pulses2bits(i, LM, q);
      ctx->remaining_bits -= curr_bits;
      while (ctx->remaining_bits < 0 && q > 0) {
         ctx->remaining_bits += curr_bits;
         q--;
         curr_bits = fl_pulses2bits(i, LM, q);
         ctx->remaining_bits -= curr_bits;
      }
      if (q != 0) {
         cm = fl_e_alg_quant(X, N, fl_get_pulses(q), ctx->spread, B, ctx->ec, gain, ctx->resynth);
      } else if (ctx->resynth) {
         int j;
         unsigned cm_mask = (unsigned)(1UL<<B)-1;
         fill &= cm_mask;
         if (!fill) {
            memset(X, 0, N*sizeof(float));
         } else {
            if (lowband == NULL) {
               for (j = 0; j < N; j++) {
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  X[j] = (float)((fl_i32)ctx->seed>>20);
               }
               cm = cm_mask;
            } else {
               for (j = 0; j < N; j++) {
                  float tmp = 1.0f/256;
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  tmp = (ctx->seed)&0x8000 ? tmp : -tmp;
                  X[j] = lowband[j]+tmp;
               }
               cm = fill;
            }
            fl_e_renormalise_vector(X, N, gain);
         }
      }
   }
   return cm;
}

static unsigned fl_e_quant_band(fl_band_ectx *ctx, float *X, int N, int b, int B, float *lowband, int LM,
      float *lowband_out, float gain, float *lowband_scratch, int fill) {
   static const unsigned char bit_interleave_table[16] = {0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3};
   static const unsigned char bit_deinterleave_table[16] = {
      0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF};
   int N0 = N, N_B, N_B0, B0 = B, time_divide = 0, recombine = 0, longBlocks, k, tf_change = ctx->tf_change;
   unsigned cm = 0;
   longBlocks = B0 == 1;
   N_B = N/B;
   if (N == 1) return fl_e_quant_band_n1(ctx, X, NULL, lowband_out);
   if (tf_change > 0) recombine = tf_change;
   if (lowband_scratch && lowband && (recombine || ((N_B&1) == 0 && tf_change < 0) || B0 > 1)) {
      memcpy(lowband_scratch, lowband, N*sizeof(float));
      lowband = lowband_scratch;
   }
   for (k = 0; k < recombine; k++) {
      fl_e_haar1(X, N>>k, 1<<k);
      if (lowband) fl_e_haar1(lowband, N>>k, 1<<k);
      fill = bit_interleave_table[fill&0xF]|bit_interleave_table[fill>>4]<<2;
   }
   B >>= recombine;
   N_B <<= recombine;
   while ((N_B&1) == 0 && tf_change < 0) {
      fl_e_haar1(X, N_B, B);
      if (lowband) fl_e_haar1(lowband, N_B, B);
      fill |= fill<<B;
      B <<= 1;
      N_B >>= 1;
      time_divide++;
      tf_change++;
   }
   B0 = B;
   N_B0 = N_B;
   if (B0 > 1) {
      fl_deinterleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
      if (lowband) fl_deinterleave_hadamard(lowband, N_B>>recombine, B0<<recombine, longBlocks);
   }
   cm = fl_e_quant_partition(ctx, X, N, b, B, lowband, LM, gain, fill);
   if (ctx->resynth) {
      if (B0 > 1) fl_interleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
      N_B = N_B0;
      B = B0;
      for (k = 0; k < time_divide; k++) {
         B >>= 1;
         N_B <<= 1;
         cm |= cm>>B;
         fl_e_haar1(X, N_B, B);
      }
      for (k = 0; k < recombine; k++) {
         cm = bit_deinterleave_table[cm];
         fl_e_haar1(X, N0>>k, 1<<k);
      }
      B <<= recombine;
      if (lowband_out) {
         int j;
         float n = (float)fl_m_sqrt((double)N0);
         for (j = 0; j < N0; j++) lowband_out[j] = n*X[j];
      }
      cm &= (1<<B)-1;
   }
   return cm;
}

static unsigned fl_e_quant_band_stereo(fl_band_ectx *ctx, float *X, float *Y, int N, int b, int B, float *lowband,
      int LM, float *lowband_out, float *lowband_scratch, int fill) {
   int inv, mbits, sbits, delta, itheta, qalloc, orig_fill;
   float mid, side;
   unsigned cm = 0;
   fl_split_ectx sctx;
   if (N == 1) return fl_e_quant_band_n1(ctx, X, Y, lowband_out);
   orig_fill = fill;
   if (ctx->bandE[ctx->i] < 1e-10f || ctx->bandE[CELT_NB_EBANDS+ctx->i] < 1e-10f) {
      if (ctx->bandE[ctx->i] > ctx->bandE[CELT_NB_EBANDS+ctx->i]) memcpy(Y, X, N*sizeof(float));
      else memcpy(X, Y, N*sizeof(float));
   }
   fl_e_compute_theta(ctx, &sctx, X, Y, N, &b, B, B, LM, 1, &fill);
   inv = sctx.inv;
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;
   mid = (1.f/32768)*sctx.imid;
   side = (1.f/32768)*sctx.iside;
   if (N == 2) {
      int c, sign = 0;
      float *x2, *y2;
      mbits = b;
      sbits = 0;
      if (itheta != 0 && itheta != 16384) sbits = 1<<BITRES;
      mbits -= sbits;
      c = itheta > 8192;
      ctx->remaining_bits -= qalloc+sbits;
      x2 = c ? Y : X;
      y2 = c ? X : Y;
      if (sbits) {
         sign = XF(x2[0])*y2[1]-XF(x2[1])*y2[0] < 0;
         ec_enc_bits(ctx->ec, sign, 1);
      }
      sign = 1-2*sign;
      cm = fl_e_quant_band(ctx, x2, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, orig_fill);
      y2[0] = (float)-sign*x2[1];
      y2[1] = (float)sign*x2[0];
      if (ctx->resynth) {
         xf y0;
         float tmp;
         X[0] = mid*X[0];
         X[1] = mid*X[1];
         y0 = XF(side)*Y[0];
         Y[0] = (float)y0;
         Y[1] = side*Y[1];
         tmp = X[0];
         X[0] = (float)(tmp-y0);
         Y[0] = tmp+Y[0];
         tmp = X[1];
         X[1] = tmp-Y[1];
         Y[1] = tmp+Y[1];
      }
   } else {
      fl_i32 rebalance;
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;
      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = fl_e_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
      } else {
         cm = fl_e_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, fill);
      }
   }
   if (ctx->resynth) {
      if (N != 2) fl_e_stereo_merge(X, Y, mid, N);
      if (inv) {
         int j;
         for (j = 0; j < N; j++) Y[j] = -Y[j];
      }
   }
   return cm;
}

static void fl_e_quant_all_bands(int start, int end, float *X_, float *Y_, unsigned char *collapse_masks,
      const float *bandE, const int *pulses, int shortBlocks, int spread, int dual_stereo, int intensity,
      const int *tf_res, fl_i32 total_bits, fl_i32 balance, ec_ctx *ec, int LM, int codedBands, fl_u32 *seed,
      int complexity, int disable_inv) {
   const fl_i16 *eb = fl_eband5ms;
   int i, M = 1<<LM, B, C = Y_ != NULL ? 2 : 1, norm_offset, lowband_offset = 0, update_lowband = 1;
   int theta_rdo = Y_ != NULL && !dual_stereo && complexity >= 8;
   int resynth = theta_rdo;
   float _norm[2*8*100], *norm, *norm2, *lowband_scratch, _lowband_scratch[8*24];
   float X_save[8*24], Y_save[8*24], X_save2[8*24], Y_save2[8*24], norm_save2[8*24];
   unsigned char bytes_save[1275];
   fl_band_ectx ctx;
   B = shortBlocks ? M : 1;
   norm_offset = M*eb[start];
   norm = _norm;
   norm2 = norm+M*eb[CELT_NB_EBANDS-1]-norm_offset;
   lowband_scratch = resynth ? _lowband_scratch : X_+M*eb[CELT_NB_EBANDS-1];
   ctx.bandE = bandE;
   ctx.ec = ec;
   ctx.intensity = intensity;
   ctx.seed = *seed;
   ctx.spread = spread;
   ctx.disable_inv = disable_inv;
   ctx.resynth = resynth;
   ctx.theta_round = 0;
   ctx.avoid_split_noise = B > 1;
   for (i = start; i < end; i++) {
      fl_i32 tell, remaining_bits, curr_balance;
      int b, N, effective_lowband = -1, last = i == end-1;
      float *X, *Y;
      unsigned x_cm, y_cm;
      ctx.i = i;
      X = X_+M*eb[i];
      Y = Y_ != NULL ? Y_+M*eb[i] : NULL;
      N = M*eb[i+1]-M*eb[i];
      tell = ec_tell_frac(ec);
      if (i != start) balance -= tell;
      remaining_bits = total_bits-tell-1;
      ctx.remaining_bits = remaining_bits;
      if (i <= codedBands-1) {
         curr_balance = balance/IMIN(3, codedBands-i);
         b = IMAX(0, IMIN(16383, IMIN(remaining_bits+1, pulses[i]+curr_balance)));
      } else b = 0;
      if (resynth && (M*eb[i]-N >= M*eb[start] || i == start+1) && (update_lowband || lowband_offset == 0))
         lowband_offset = i;
      if (i == start+1) {
         int n1 = M*(eb[start+1]-eb[start]), n2 = M*(eb[start+2]-eb[start+1]);
         memcpy(&norm[n1], &norm[2*n1-n2], (n2-n1)*sizeof(float));
         if (dual_stereo) memcpy(&norm2[n1], &norm2[2*n1-n2], (n2-n1)*sizeof(float));
      }
      ctx.tf_change = tf_res[i];
      if (last && !theta_rdo) lowband_scratch = NULL;
      if (lowband_offset != 0 && (spread != SPREAD_AGGRESSIVE || B > 1 || tf_res[i] < 0)) {
         int fold_start, fold_end, fold_i;
         effective_lowband = IMAX(0, M*eb[lowband_offset]-norm_offset-N);
         fold_start = lowband_offset;
         while (M*eb[--fold_start] > effective_lowband+norm_offset);
         fold_end = lowband_offset-1;
         while (++fold_end < i && M*eb[fold_end] < effective_lowband+norm_offset+N);
         x_cm = y_cm = 0;
         fold_i = fold_start;
         do {
            x_cm |= collapse_masks[fold_i*C+0];
            y_cm |= collapse_masks[fold_i*C+C-1];
         } while (++fold_i < fold_end);
      } else x_cm = y_cm = (1<<B)-1;
      if (dual_stereo && i == intensity) {
         int j;
         dual_stereo = 0;
         if (resynth)
            for (j = 0; j < M*eb[i]-norm_offset; j++) norm[j] = .5f*(norm[j]+norm2[j]);
      }
      if (dual_stereo) {
         x_cm = fl_e_quant_band(&ctx, X, N, b/2, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
               last ? NULL : norm+M*eb[i]-norm_offset, 1.f, lowband_scratch, x_cm);
         y_cm = fl_e_quant_band(&ctx, Y, N, b/2, B, effective_lowband != -1 ? norm2+effective_lowband : NULL, LM,
               last ? NULL : norm2+M*eb[i]-norm_offset, 1.f, lowband_scratch, y_cm);
      } else {
         if (Y != NULL) {
            if (theta_rdo && i < intensity) {
               ec_ctx ec_save, ec_save2;
               fl_band_ectx ctx_save, ctx_save2;
               float dist0, w0, w1;
               xf dist1;
               unsigned cm, cm2;
               int nstart_bytes, save_bytes;
               unsigned char *bytes_buf;
               {
                  float Ex = bandE[i], Ey = bandE[i+CELT_NB_EBANDS], minE = FL_MIN(Ex, Ey);
                  xf m3 = XF(minE)/3.f;
                  w0 = (float)(Ex+m3);
                  w1 = (float)(Ey+m3);
               }
               cm = x_cm|y_cm;
               ec_save = *ec;
               ctx_save = ctx;
               memcpy(X_save, X, N*sizeof(float));
               memcpy(Y_save, Y, N*sizeof(float));
               ctx.theta_round = -1;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, cm);
               {
                  float d0 = (float)(w0*fl_e_inner_prod(X_save, X, N));
                  dist0 = (float)(w1*fl_e_inner_prod(Y_save, Y, N)+d0);
               }
               cm2 = x_cm;
               ec_save2 = *ec;
               ctx_save2 = ctx;
               memcpy(X_save2, X, N*sizeof(float));
               memcpy(Y_save2, Y, N*sizeof(float));
               if (!last) memcpy(norm_save2, norm+M*eb[i]-norm_offset, N*sizeof(float));
               nstart_bytes = ec_save.offs;
               bytes_buf = ec_save.buf+nstart_bytes;
               save_bytes = ec_save.storage-nstart_bytes;
               memcpy(bytes_save, bytes_buf, save_bytes);
               *ec = ec_save;
               ctx = ctx_save;
               memcpy(X, X_save, N*sizeof(float));
               memcpy(Y, Y_save, N*sizeof(float));
               if (i == start+1) {
                  int n1 = M*(eb[start+1]-eb[start]), n2 = M*(eb[start+2]-eb[start+1]);
                  memcpy(&norm[n1], &norm[2*n1-n2], (n2-n1)*sizeof(float));
               }
               ctx.theta_round = 1;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, cm);
               {
                  float d0 = (float)(w0*fl_e_inner_prod(X_save, X, N));
                  dist1 = w1*fl_e_inner_prod(Y_save, Y, N)+d0;
               }
               if (dist0 >= dist1) {
                  x_cm = cm2;
                  *ec = ec_save2;
                  ctx = ctx_save2;
                  memcpy(X, X_save2, N*sizeof(float));
                  memcpy(Y, Y_save2, N*sizeof(float));
                  if (!last) memcpy(norm+M*eb[i]-norm_offset, norm_save2, N*sizeof(float));
                  memcpy(bytes_buf, bytes_save, save_bytes);
               }
            } else {
               ctx.theta_round = 0;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, x_cm|y_cm);
            }
         } else {
            x_cm = fl_e_quant_band(&ctx, X, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
                  last ? NULL : norm+M*eb[i]-norm_offset, 1.f, lowband_scratch, x_cm|y_cm);
         }
         y_cm = x_cm;
      }
      collapse_masks[i*C+0] = (unsigned char)x_cm;
      collapse_masks[i*C+C-1] = (unsigned char)y_cm;
      balance += pulses[i]+tell;
      update_lowband = b > (N<<BITRES);
      ctx.avoid_split_noise = 0;
   }
   *seed = ctx.seed;
}

static int fl_e_spreading_decision(const float *X, int *average, int last_decision, int *hf_average,
      int *tapset_decision, int update_hf, int end, int C, int M, const int *spread_weight) {
   const fl_i16 *eb = fl_eband5ms;
   int i, c, N0 = M*120, sum = 0, nbBands = 0, decision, hf_sum = 0;
   if (M*(eb[end]-eb[end-1]) <= 8) return SPREAD_NONE;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         int j, N, tmp, tcount[3] = {0, 0, 0};
         const float *x = X+M*eb[i]+c*N0;
         N = M*(eb[i+1]-eb[i]);
         if (N <= 8) continue;
         for (j = 0; j < N; j++) {
            xf x2N = (XF(x[j])*x[j])*N;
            if (x2N < .25f) tcount[0]++;
            if (x2N < .0625f) tcount[1]++;
            if (x2N < .015625f) tcount[2]++;
         }
         if (i > CELT_NB_EBANDS-4) hf_sum += (fl_u32)(32*(tcount[1]+tcount[0]))/N;
         tmp = (2*tcount[2] >= N)+(2*tcount[1] >= N)+(2*tcount[0] >= N);
         sum += tmp*spread_weight[i];
         nbBands += spread_weight[i];
      }
   if (update_hf) {
      if (hf_sum) hf_sum = (fl_u32)hf_sum/(fl_u32)(C*(4-CELT_NB_EBANDS+end));
      *hf_average = (*hf_average+hf_sum)>>1;
      hf_sum = *hf_average;
      if (*tapset_decision == 2) hf_sum += 4;
      else if (*tapset_decision == 0) hf_sum -= 4;
      if (hf_sum > 22) *tapset_decision = 2;
      else if (hf_sum > 18) *tapset_decision = 1;
      else *tapset_decision = 0;
   }
   sum = (fl_u32)((fl_i32)sum<<8)/(fl_u32)nbBands;
   sum = (sum+*average)>>1;
   *average = sum;
   sum = (3*sum+(((3-last_decision)<<7)+64)+2)>>2;
   if (sum < 80) decision = SPREAD_AGGRESSIVE;
   else if (sum < 256) decision = SPREAD_NORMAL;
   else if (sum < 384) decision = SPREAD_LIGHT;
   else decision = SPREAD_NONE;
   return decision;
}

/* ---- pitch analysis for the prefilter ---- */
static void fl_e_celt_lpc(float *lpc, const float *ac, int p) {
   int i, j;
   xf error = ac[0];
   memset(lpc, 0, p*sizeof(float));
   if (ac[0] > 1e-10f) {
      for (i = 0; i < p; i++) {
         xf rr = 0, r;
         for (j = 0; j < i; j++) rr += XF(lpc[j])*ac[i-j];
         rr += ac[i+1];
         r = -(rr/error);
         lpc[i] = (float)r;
         for (j = 0; j < (i+1)>>1; j++) {
            float tmp1 = lpc[j], tmp2 = lpc[i-1-j];
            lpc[j] = (float)(tmp1+r*tmp2);
            lpc[i-1-j] = (float)(tmp2+r*tmp1);
         }
         error = error-(r*r)*error;
         if (error <= XF(.001f)*ac[0]) break;
      }
   }
}
static void fl_e_autocorr(const float *x, float *ac, int lag, int n) {
   int i, k, fastN = n-lag;
   fl_pitch_xcorr(x, x, ac, fastN, lag+1);
   for (k = 0; k <= lag; k++) {
      xf d = 0;
      for (i = k+fastN; i < n; i++) d += XF(x[i])*x[i-k];
      ac[k] = (float)(d+ac[k]);
   }
}
static void fl_e_pitch_downsample(float *x[], float *x_lp, int len, int C) {
   int i;
   float ac[5], lpc[4], lpc2[5];
   xf tmp = 1;
   for (i = 1; i < len; i++)
      x_lp[i] = (float)((XF(.25f)*x[0][2*i-1]+XF(.25f)*x[0][2*i+1])+XF(.5f)*x[0][2*i]);
   x_lp[0] = (float)(XF(.25f)*x[0][1]+XF(.5f)*x[0][0]);
   if (C == 2) {
      for (i = 1; i < len; i++)
         x_lp[i] = (float)(((XF(.25f)*x[1][2*i-1]+XF(.25f)*x[1][2*i+1])+XF(.5f)*x[1][2*i])+x_lp[i]);
      x_lp[0] = (float)((XF(.25f)*x[1][1]+XF(.5f)*x[1][0])+x_lp[0]);
   }
   fl_e_autocorr(x_lp, ac, 4, len);
   ac[0] *= 1.0001f;
   for (i = 1; i <= 4; i++) {
      xf k = XF(.008f)*i;
      ac[i] = (float)(ac[i]-(ac[i]*k)*k);
   }
   fl_e_celt_lpc(lpc, ac, 4);
   for (i = 0; i < 4; i++) {
      tmp = XF(.9f)*tmp;
      lpc[i] = (float)(lpc[i]*tmp);
   }
   lpc2[0] = (float)(XF(lpc[0])+.8f);
   lpc2[1] = (float)(lpc[1]+XF(.8f)*lpc[0]);
   lpc2[2] = (float)(lpc[2]+XF(.8f)*lpc[1]);
   lpc2[3] = (float)(lpc[3]+XF(.8f)*lpc[2]);
   lpc2[4] = (float)(XF(.8f)*lpc[3]);
   {
      float m0 = 0, m1 = 0, m2 = 0, m3 = 0, m4 = 0;
      for (i = 0; i < len; i++) {
         xf sum = XF(x_lp[i])+XF(lpc2[0])*m0;
         sum += XF(lpc2[1])*m1;
         sum += XF(lpc2[2])*m2;
         sum += XF(lpc2[3])*m3;
         sum += XF(lpc2[4])*m4;
         m4 = m3; m3 = m2; m2 = m1; m1 = m0;
         m0 = x_lp[i];
         x_lp[i] = (float)sum;
      }
   }
}
static void fl_e_find_best_pitch(const float *xcorr, const float *y, int len, int max_pitch, int *best_pitch) {
   int i, j;
   xf Syy = 1, best_num[2] = {-1, -1}, best_den[2] = {0, 0};
   best_pitch[0] = 0;
   best_pitch[1] = 1;
   for (j = 0; j < len; j++) Syy += XF(y[j])*y[j];
   for (i = 0; i < max_pitch; i++) {
      if (xcorr[i] > 0) {
         xf x16 = XF(xcorr[i])*1e-12f, num = x16*x16;
         if (num*best_den[1] > best_num[1]*Syy) {
            if (num*best_den[0] > best_num[0]*Syy) {
               best_num[1] = best_num[0];
               best_den[1] = best_den[0];
               best_pitch[1] = best_pitch[0];
               best_num[0] = num;
               best_den[0] = Syy;
               best_pitch[0] = i;
            } else {
               best_num[1] = num;
               best_den[1] = Syy;
               best_pitch[1] = i;
            }
         }
      }
      Syy += XF(y[i+len])*y[i+len]-XF(y[i])*y[i];
      if (Syy < 1) Syy = 1;
   }
}
static void fl_e_pitch_search(const float *x_lp, const float *y, int len, int max_pitch, int *pitch) {
   int i, j, lag = len+max_pitch, best_pitch[2] = {0, 0}, offset;
   float x_lp4[512], y_lp4[1024], xcorr[1024];
   for (j = 0; j < len>>2; j++) x_lp4[j] = x_lp[2*j];
   for (j = 0; j < lag>>2; j++) y_lp4[j] = y[2*j];
   fl_pitch_xcorr(x_lp4, y_lp4, xcorr, len>>2, max_pitch>>2);
   fl_e_find_best_pitch(xcorr, y_lp4, len>>2, max_pitch>>2, best_pitch);
   for (i = 0; i < max_pitch>>1; i++) {
      xf sum;
      xcorr[i] = 0;
      if (abs(i-2*best_pitch[0]) > 2 && abs(i-2*best_pitch[1]) > 2) continue;
      sum = fl_e_inner_prod(x_lp, y+i, len>>1);
      xcorr[i] = (float)(sum > -1 ? sum : -1);
   }
   fl_e_find_best_pitch(xcorr, y, len>>1, max_pitch>>1, best_pitch);
   if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch>>1)-1) {
      float a = xcorr[best_pitch[0]-1], b = xcorr[best_pitch[0]], c = xcorr[best_pitch[0]+1];
      if (XF(c)-a > XF(.7f)*(XF(b)-a)) offset = 1;
      else if (XF(a)-c > XF(.7f)*(XF(b)-c)) offset = -1;
      else offset = 0;
   } else offset = 0;
   *pitch = 2*best_pitch[0]-offset;
}
static float fl_e_remove_doubling(float *x, int maxperiod, int minperiod, int N, int *T0_, int prev_period,
      float prev_gain) {
   static const int second_check[16] = {0, 0, 3, 2, 3, 2, 5, 2, 3, 2, 3, 2, 5, 2, 3, 2};
   int k, i, T, T0, offset, minperiod0 = minperiod;
   float xx, xy, xy2, g, g0, pg_f, best_xy, best_yy, yy_f, xcorr[3], yy_lookup[1024/2+1];
   xf yy, pg;
   maxperiod /= 2;
   minperiod /= 2;
   *T0_ /= 2;
   prev_period /= 2;
   N /= 2;
   x += maxperiod;
   if (*T0_ >= maxperiod) *T0_ = maxperiod-1;
   T = T0 = *T0_;
   fl_e_dual_inner_prod(x, x, x-T0, N, &xx, &xy);
   yy_lookup[0] = xx;
   yy = xx;
   for (i = 1; i <= maxperiod; i++) {
      yy = (yy+XF(x[-i])*x[-i])-XF(x[N-i])*x[N-i];
      yy_lookup[i] = (float)(yy > 0 ? yy : 0);
   }
   yy_f = yy_lookup[T0];
   best_xy = xy;
   best_yy = yy_f;
   g = g0 = (float)(xy/fl_m_sqrtf((float)(XF(xx)*yy_f+1)));
   for (k = 2; k <= 15; k++) {
      int T1, T1b;
      xf g1, thresh, cont, yyx;
      T1 = (2*T0+k)/(2*k);
      if (T1 < minperiod) break;
      if (k == 2) T1b = T1+T0 > maxperiod ? T0 : T0+T1;
      else T1b = (2*second_check[k]*T0+k)/(2*k);
      fl_e_dual_inner_prod(x, &x[-T1], &x[-T1b], N, &xy, &xy2);
      xy = (float)((XF(xy)+xy2)*.5f);
      yyx = (XF(yy_lookup[T1])+yy_lookup[T1b])*.5f;
      yy_f = (float)yyx;
      g1 = xy/fl_m_sqrtf((float)(yyx*xx+1));
      if (abs(T1-prev_period) <= 1) cont = prev_gain;
      else if (abs(T1-prev_period) <= 2 && 5*k*k < T0) cont = XF(.5f)*prev_gain;
      else cont = 0;
      thresh = XF(.7f)*g0-cont;
      if (thresh < .3f) thresh = .3f;
      if (T1 < 3*minperiod) {
         thresh = XF(.85f)*g0-cont;
         if (thresh < .4f) thresh = .4f;
      } else if (T1 < 2*minperiod) {
         thresh = XF(.9f)*g0-cont;
         if (thresh < .5f) thresh = .5f;
      }
      if (g1 > thresh) {
         best_xy = xy;
         best_yy = yy_f;
         T = T1;
         g = (float)g1;
      }
   }
   if (best_xy < 0) best_xy = 0;
   if (best_yy <= best_xy) pg = 1;
   else pg = best_xy/(XF(best_yy)+1);
   pg_f = (float)pg;
   for (k = 0; k < 3; k++) xcorr[k] = (float)fl_e_inner_prod(x, x-(T+k-1), N);
   if (XF(xcorr[2])-xcorr[0] > XF(.7f)*(XF(xcorr[1])-xcorr[0])) offset = 1;
   else if (XF(xcorr[0])-xcorr[2] > XF(.7f)*(XF(xcorr[1])-xcorr[2])) offset = -1;
   else offset = 0;
   if (pg_f > g) pg_f = g;
   *T0_ = 2*T+offset;
   if (*T0_ < minperiod0) *T0_ = minperiod0;
   return pg_f;
}

/* comb_filter: overlap part on the x87 stack, constant part SSE. */
static void fl_e_comb_filter(float *y, float *x, int T0, int T1, int N, float g0, float g1, int tapset0,
      int tapset1, const float *window, int overlap) {
   int i;
   float g00, g01, g02, g10, g11, g12, x0, x1, x2, x3, x4;
   if (g0 == 0 && g1 == 0) {
      if (x != y) memmove(y, x, N*sizeof(float));
      return;
   }
   T0 = IMAX(T0, COMBFILTER_MINPERIOD);
   T1 = IMAX(T1, COMBFILTER_MINPERIOD);
   g00 = g0*fl_comb_gains[tapset0][0];
   g01 = g0*fl_comb_gains[tapset0][1];
   g02 = g0*fl_comb_gains[tapset0][2];
   g10 = g1*fl_comb_gains[tapset1][0];
   g11 = g1*fl_comb_gains[tapset1][1];
   g12 = g1*fl_comb_gains[tapset1][2];
   x1 = x[-T1+1];
   x2 = x[-T1];
   x3 = x[-T1-1];
   x4 = x[-T1-2];
   if (g0 == g1 && T0 == T1 && tapset0 == tapset1) overlap = 0;
   for (i = 0; i < overlap; i++) {
      xf f, nf;
      x0 = x[i-T1+2];
      f = XF(window[i])*window[i];
      nf = 1.0L-f;
      y[i] = (float)(((((((nf*g00)*x[i-T0]+x[i])
            +(nf*g01)*(XF(x[i-T0+1])+x[i-T0-1]))
            +(nf*g02)*(XF(x[i-T0+2])+x[i-T0-2]))
            +(f*g10)*x2)
            +(f*g11)*(XF(x1)+x3))
            +(f*g12)*(XF(x4)+x0));
      x4 = x3;
      x3 = x2;
      x2 = x1;
      x1 = x0;
   }
   if (g1 == 0) {
      if (x != y) memmove(y+overlap, x+overlap, (N-overlap)*sizeof(float));
      return;
   }
   fl_comb_filter_const(y+i, x+i, T1, N-i, g10, g11, g12);
}

/* ======== enc_celt_main.c ======== */
/* ---- CELT encoder: state and frame encoding (celt_encode_with_ec) ---- */
#define LEAK_BANDS 19
#define COMBFILTER_MAXPERIOD 1024

typedef struct {
   int valid;
   float tonality, tonality_slope, noisiness, activity, music_prob, music_prob_min, music_prob_max;
   int bandwidth;
   float activity_probability, max_pitch_ratio;
   unsigned char leak_boost[LEAK_BANDS];
} fl_analysis_info;

typedef struct {
   int channels, stream_channels, force_intra, clip, disable_pf, complexity, start, end;
   fl_i32 bitrate;
   int vbr, constrained_vbr, loss_rate, lsb_depth, disable_inv;
   /* reset from here */
   fl_u32 rng;
   int spread_decision;
   float delayedIntra;
   int tonal_average, lastCodedBands, hf_average, tapset_decision, prefilter_period;
   float prefilter_gain;
   int prefilter_tapset, consec_transient;
   fl_analysis_info analysis;
   int silk_signal_type, silk_offset;
   int lfe;                    /* low-frequency effects channel (surround) */
   const float *energy_mask;   /* surround masking per band and channel, NULL if none */
   float preemph_memE[2];
   fl_i32 vbr_reservoir, vbr_drift, vbr_offset, vbr_count;
   float overlap_max, stereo_saving;
   int intensity;
   float spec_avg;
   float in_mem[2*CELT_OVERLAP], prefilter_mem[2*COMBFILTER_MAXPERIOD];
   float oldBandE[2*CELT_NB_EBANDS], oldLogE[2*CELT_NB_EBANDS], oldLogE2[2*CELT_NB_EBANDS];
   float energyError[2*CELT_NB_EBANDS];
} fl_celt_enc;

static void fl_celt_enc_reset(fl_celt_enc *st) {
   int i;
   memset(&st->rng, 0, sizeof(*st)-offsetof(fl_celt_enc, rng));
   for (i = 0; i < 2*CELT_NB_EBANDS; i++) st->oldLogE[i] = st->oldLogE2[i] = -28.f;
   st->vbr_offset = 0;
   st->delayedIntra = 1;
   st->spread_decision = SPREAD_NORMAL;
   st->tonal_average = 256;
   st->hf_average = 0;
   st->tapset_decision = 0;
}
static void fl_celt_enc_init(fl_celt_enc *st, int channels) {
   memset(st, 0, sizeof(*st));
   st->stream_channels = st->channels = channels;
   st->start = 0;
   st->end = CELT_NB_EBANDS;
   st->constrained_vbr = 1;
   st->clip = 1;
   st->bitrate = -1;
   st->vbr = 0;
   st->force_intra = 0;
   st->complexity = 5;
   st->lsb_depth = 24;
   fl_celt_enc_reset(st);
}

static float fl_e_maxabs(const float *x, int len) {
   int i;
   float maxval = 0, minval = 0;
   for (i = 0; i < len; i++) {
      maxval = FL_MAX(maxval, x[i]);
      minval = FL_MIN(minval, x[i]);
   }
   return FL_MAX(maxval, -minval);
}

static void fl_e_preemphasis(const float *pcm, float *inp, int N, int CC, float *mem, int clip) {
   int i;
   xf m = *mem;
   const float coef0 = fl_preemph[0];
   if (!clip) {
      for (i = 0; i < N; i++) {
         float x = pcm[CC*i]*32768.f;
         inp[i] = (float)(x-m);
         m = XF(coef0)*x;
      }
   } else {
      for (i = 0; i < N; i++) {
         float x = pcm[CC*i]*32768.f;
         inp[i] = FL_MAX(-65536.f, FL_MIN(65536.f, x));
      }
      for (i = 0; i < N; i++) {
         float x = inp[i];
         inp[i] = (float)(x-m);
         m = XF(coef0)*x;
      }
   }
   *mem = (float)m;
}

static int fl_e_tone_lpc(const float *x, int len, int delay, float *lpc) {
   int i;
   xf r00 = 0, r01 = 0, r02 = 0, r11, r12, r22, edges, num0, num1, den;
   for (i = 0; i < len-2*delay; i++) {
      r00 += XF(x[i])*x[i];
      r01 += XF(x[i])*x[i+delay];
      r02 += XF(x[i])*x[i+2*delay];
   }
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-2*delay])*x[len+i-2*delay]-XF(x[i])*x[i];
   r11 = r00+edges;
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-delay])*x[len+i-delay]-XF(x[i+delay])*x[i+delay];
   r22 = r11+edges;
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-2*delay])*x[len+i-delay]-XF(x[i])*x[i+delay];
   r12 = r01+edges;
   {
      xf R00 = r00+r22, R01 = r01+r12, R11 = 2*r11, R02 = 2*r02, R12 = r12+r01;
      r00 = R00; r01 = R01; r11 = R11; r02 = R02; r12 = R12;
   }
   den = r00*r11-r01*r01;
   if (den < XF(.001f)*(r00*r11)) return 1;
   num1 = r02*r11-r01*r12;
   if (num1 >= den) lpc[1] = 1.f;
   else if (num1 <= -den) lpc[1] = -1.f;
   else lpc[1] = (float)(num1/den);
   num0 = r00*r12-r02*r01;
   if (XF(.5f)*num0 >= den) lpc[0] = 1.999999f;
   else if (XF(.5f)*num0 <= -den) lpc[0] = -1.999999f;
   else lpc[0] = (float)(num0/den);
   return 0;
}
static float fl_e_tone_detect(const float *in, int CC, int N, float *toneishness, int Fs) {
   int i, delay = 1, fail;
   float lpc[2] = {0, 0}, x[1080];
   if (CC == 2) for (i = 0; i < N; i++) x[i] = in[i]+in[i+N];
   else for (i = 0; i < N; i++) x[i] = in[i];
   fail = fl_e_tone_lpc(x, N, delay, lpc);
   while (delay <= Fs/3000 && (fail || (lpc[0] > 1.f && lpc[1] < 0))) {
      delay *= 2;
      fail = fl_e_tone_lpc(x, N, delay, lpc);
   }
   if (!fail && XF(lpc[0])*lpc[0]+XF(3.999999)*lpc[1] < 0) {
      *toneishness = -lpc[1];
      return (float)(XF(fl_m_acos((double)(.5f*lpc[0])))/delay);
   }
   *toneishness = 0;
   return -1;
}

static int fl_e_transient_analysis(const float *in, int len, int C, float *tf_estimate, int *tf_chan,
      int allow_weak_transients, int *weak_transient, float tone_freq, float toneishness) {
   static const unsigned char inv_table[128] = {
      255, 255, 156, 110, 86, 70, 59, 51, 45, 40, 37, 33, 31, 28, 26, 25,
      23, 22, 21, 20, 19, 18, 17, 16, 16, 15, 15, 14, 13, 13, 12, 12,
      12, 12, 11, 11, 11, 10, 10, 10, 9, 9, 9, 9, 9, 9, 8, 8,
      8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3,
      3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2};
   int i, c, is_transient, mask_metric = 0, len2 = len/2;
   float tmp[1080], forward_decay = allow_weak_transients ? .03125f : .0625f;
   *weak_transient = 0;
   for (c = 0; c < C; c++) {
      xf mem0 = 0, mem1 = 0, mean = 0, maxE = 0, norm;
      float mean_f;
      int unmask = 0;
      for (i = 0; i < len; i++) {
         float x = in[i+c*len];
         xf y = mem0+x, mem00 = mem0;
         mem0 = (mem0-x)+XF(.5f)*mem1;
         mem1 = x-mem00;
         tmp[i] = (float)y;
      }
      memset(tmp, 0, 12*sizeof(float));
      mem0 = 0;
      for (i = 0; i < len2; i++) {
         xf x2 = XF(tmp[2*i])*tmp[2*i]+XF(tmp[2*i+1])*tmp[2*i+1];
         mean += x2;
         mem0 = x2+(1.0L-forward_decay)*mem0;
         tmp[i] = (float)(XF(forward_decay)*mem0);
      }
      mem0 = 0;
      for (i = len2-1; i >= 0; i--) {
         xf v;
         mem0 = XF(.875f)*mem0+tmp[i];
         v = XF(.125f)*mem0;
         tmp[i] = (float)v;
         maxE = maxE > v ? maxE : v;
      }
      mean_f = (float)fl_m_sqrt((double)(((mean*maxE)*.5f)*len2));
      norm = XF(len2)/(XF(mean_f)+1e-15f);
      for (i = 12; i < len2-5; i += 4) {
         xf n = i == 12 ? norm : XF((float)norm);
         float fl = floorf((float)((XF(64.f)*n)*(XF(tmp[i])+1e-15f)));
         int id = fl > 127 ? 127 : (0 > fl ? 0 : (int)fl);
         unmask += inv_table[id];
      }
      unmask = 64*unmask*4/(6*(len2-17));
      if (unmask > mask_metric) {
         *tf_chan = c;
         mask_metric = unmask;
      }
   }
   is_transient = mask_metric > 200;
   if (toneishness > .98f && tone_freq < .026f) {
      is_transient = 0;
      mask_metric = 0;
   }
   if (allow_weak_transients && is_transient && mask_metric < 600) {
      is_transient = 0;
      *weak_transient = 1;
   }
   {
      xf tf_max = XF((float)fl_m_sqrt((double)(27*mask_metric)))-42, v;
      if (0 > tf_max) tf_max = 0;
      v = (163 < tf_max ? XF(163) : tf_max)*XF(.0069f)-XF(0.139);
      if (0 > v) v = 0;
      *tf_estimate = (float)fl_m_sqrt((double)v);
   }
   return is_transient;
}

static int fl_e_patch_transient_decision(const float *newE, const float *oldE, int start, int end, int C) {
   int i, c;
   xf mean_diff = 0;
   float spread_old[26];
   if (C == 1) {
      spread_old[start] = oldE[start];
      for (i = start+1; i < end; i++) {
         xf a = XF(spread_old[i-1])-1;
         spread_old[i] = (float)(a > oldE[i] ? a : XF(oldE[i]));
      }
   } else {
      spread_old[start] = FL_MAX(oldE[start], oldE[start+CELT_NB_EBANDS]);
      for (i = start+1; i < end; i++) {
         xf a = XF(spread_old[i-1])-1;
         float b = FL_MAX(oldE[i], oldE[i+CELT_NB_EBANDS]);
         spread_old[i] = (float)(a > b ? a : XF(b));
      }
   }
   for (i = end-2; i >= start; i--) {
      xf a = XF(spread_old[i+1])-1;
      spread_old[i] = (float)(spread_old[i] > a ? XF(spread_old[i]) : a);
   }
   for (c = 0; c < C; c++)
      for (i = IMAX(2, start); i < end-1; i++) {
         float x1 = FL_MAX(0, newE[i+c*CELT_NB_EBANDS]), x2 = FL_MAX(0, spread_old[i]);
         xf d = XF(x1)-x2;
         mean_diff += 0 > d ? 0 : d;
      }
   mean_diff = mean_diff/(C*(end-1-IMAX(2, start)));
   return mean_diff > 1;
}

static xf fl_e_l1_metric(const float *tmp, int N, int LM, float bias) {
   int i;
   xf L1 = 0;
   for (i = 0; i < N; i++) L1 += fabsf(tmp[i]);
   return L1+(XF(bias)*LM)*L1;
}
static int fl_e_tf_analysis(int len, int isTransient, int *tf_res, int lambda, float *X, int N0, int LM,
      float tf_estimate, int tf_chan, const int *importance) {
   int i, sel, cost0, cost1, tf_select = 0, selcost[2], metric[CELT_NB_EBANDS], path0[CELT_NB_EBANDS], path1[CELT_NB_EBANDS];
   float tmp[176], tmp_1[176], bias;
   {
      xf d = XF(.5f)-tf_estimate;
      bias = (float)((-.25f > d ? XF(-.25f) : d)*.04f);
   }
   for (i = 0; i < len; i++) {
      int k, N, narrow, best_level = 0;
      xf L1;
      float best_L1;
      N = (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM;
      narrow = (fl_eband5ms[i+1]-fl_eband5ms[i]) == 1;
      memcpy(tmp, &X[tf_chan*N0+(fl_eband5ms[i]<<LM)], N*sizeof(float));
      L1 = fl_e_l1_metric(tmp, N, isTransient ? LM : 0, bias);
      best_L1 = (float)L1;
      if (isTransient && !narrow) {
         memcpy(tmp_1, tmp, N*sizeof(float));
         fl_e_haar1(tmp_1, N>>LM, 1<<LM);
         L1 = fl_e_l1_metric(tmp_1, N, LM+1, bias);
         if (L1 < best_L1) {
            best_L1 = (float)L1;
            best_level = -1;
         }
      }
      for (k = 0; k < LM+!(isTransient || narrow); k++) {
         int B = isTransient ? LM-k-1 : k+1;
         fl_e_haar1(tmp, N>>k, 1<<k);
         L1 = fl_e_l1_metric(tmp, N, B, bias);
         if (L1 < best_L1) {
            best_L1 = (float)L1;
            best_level = k+1;
         }
      }
      metric[i] = isTransient ? 2*best_level : -2*best_level;
      if (narrow && (metric[i] == 0 || metric[i] == -2*LM)) metric[i] -= 1;
   }
   for (sel = 0; sel < 2; sel++) {
      cost0 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*sel+0]);
      cost1 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*sel+1])+(isTransient ? 0 : lambda);
      for (i = 1; i < len; i++) {
         int curr0 = IMIN(cost0, cost1+lambda), curr1 = IMIN(cost0+lambda, cost1);
         cost0 = curr0+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*sel+0]);
         cost1 = curr1+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*sel+1]);
      }
      selcost[sel] = IMIN(cost0, cost1);
   }
   if (selcost[1] < selcost[0] && isTransient) tf_select = 1;
   cost0 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+0]);
   cost1 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+1])+(isTransient ? 0 : lambda);
   for (i = 1; i < len; i++) {
      int curr0, curr1, from0, from1;
      from0 = cost0;
      from1 = cost1+lambda;
      if (from0 < from1) {
         curr0 = from0;
         path0[i] = 0;
      } else {
         curr0 = from1;
         path0[i] = 1;
      }
      from0 = cost0+lambda;
      from1 = cost1;
      if (from0 < from1) {
         curr1 = from0;
         path1[i] = 0;
      } else {
         curr1 = from1;
         path1[i] = 1;
      }
      cost0 = curr0+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+0]);
      cost1 = curr1+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+1]);
   }
   tf_res[len-1] = cost0 < cost1 ? 0 : 1;
   for (i = len-2; i >= 0; i--) tf_res[i] = tf_res[i+1] == 1 ? path1[i+1] : path0[i+1];
   return tf_select;
}
static void fl_e_tf_encode(int start, int end, int isTransient, int *tf_res, int LM, int tf_select, ec_ctx *enc) {
   int curr, i, tf_select_rsv, tf_changed, logp;
   fl_u32 budget = enc->storage*8, tell = ec_tell(enc);
   logp = isTransient ? 2 : 4;
   tf_select_rsv = LM > 0 && tell+logp+1 <= budget;
   budget -= tf_select_rsv;
   curr = tf_changed = 0;
   for (i = start; i < end; i++) {
      if (tell+logp <= budget) {
         ec_enc_bit_logp(enc, tf_res[i]^curr, logp);
         tell = ec_tell(enc);
         curr = tf_res[i];
         tf_changed |= curr;
      } else tf_res[i] = curr;
      logp = isTransient ? 4 : 5;
   }
   if (tf_select_rsv && fl_tf_select_table[LM][4*isTransient+0+tf_changed] != fl_tf_select_table[LM][4*isTransient+2+tf_changed])
      ec_enc_bit_logp(enc, tf_select, 1);
   else tf_select = 0;
   for (i = start; i < end; i++) tf_res[i] = fl_tf_select_table[LM][4*isTransient+2*tf_select+tf_res[i]];
}

static int fl_e_hysteresis_decision(float val, const float *thresholds, const float *hysteresis, int N, int prev) {
   int i;
   for (i = 0; i < N; i++)
      if (val < thresholds[i]) break;
   if (i > prev && val < XF(thresholds[prev])+hysteresis[prev]) i = prev;
   if (i < prev && val > XF(thresholds[prev-1])-hysteresis[prev-1]) i = prev;
   return i;
}

static float fl_e_median_of_5(const float *x) {
   float t0, t1, t2, t3, t4, tmp;
   t2 = x[2];
   if (x[0] > x[1]) { t0 = x[1]; t1 = x[0]; } else { t0 = x[0]; t1 = x[1]; }
   if (x[3] > x[4]) { t3 = x[4]; t4 = x[3]; } else { t3 = x[3]; t4 = x[4]; }
   if (t0 > t3) {
      tmp = t0; t0 = t3; t3 = tmp;
      tmp = t1; t1 = t4; t4 = tmp;
   }
   if (t2 > t1) {
      if (t1 < t3) return FL_MIN(t2, t3);
      return FL_MIN(t4, t1);
   }
   if (t2 < t3) return FL_MIN(t1, t3);
   return FL_MIN(t2, t4);
}
static float fl_e_median_of_3(const float *x) {
   float t0, t1, t2;
   if (x[0] > x[1]) { t0 = x[1]; t1 = x[0]; } else { t0 = x[0]; t1 = x[1]; }
   t2 = x[2];
   if (t1 < t2) return t1;
   if (t0 < t2) return t2;
   return t0;
}

static float fl_e_dynalloc_analysis(const float *bandLogE, const float *bandLogE2, const float *oldBandE, int start,
      int end, int C, int *offsets, int lsb_depth, int isTransient, int vbr, int constrained_vbr, int LM,
      int effectiveBytes, fl_i32 *tot_boost_, const fl_analysis_info *analysis, int *importance,
      int *spread_weight, float tone_freq, float toneishness, int lfe, const float *surround_dynalloc) {
   const fl_i16 *eBands = fl_eband5ms;
   int i, c;
   fl_i32 tot_boost = 0;
   float maxDepth, follower[2*CELT_NB_EBANDS], noise_floor[CELT_NB_EBANDS], bandLogE3[CELT_NB_EBANDS];
   memset(offsets, 0, CELT_NB_EBANDS*sizeof(int));
   maxDepth = -31.9f;
   for (i = 0; i < end; i++)
      noise_floor[i] = (float)((((XF(.0625f)*fl_logN400[i]+.5f)+(9-lsb_depth))-fl_eMeans[i])+(XF(.0062f)*(i+5))*(i+5));
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         xf d = XF(bandLogE[c*CELT_NB_EBANDS+i])-noise_floor[i];
         if (!(maxDepth > d)) maxDepth = (float)d;
      }
   {
      float mask[CELT_NB_EBANDS], sig[CELT_NB_EBANDS];
      for (i = 0; i < end; i++) mask[i] = bandLogE[i]-noise_floor[i];
      if (C == 2)
         for (i = 0; i < end; i++) {
            xf d = XF(bandLogE[CELT_NB_EBANDS+i])-noise_floor[i];
            mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
         }
      memcpy(sig, mask, end*sizeof(float));
      for (i = 1; i < end; i++) {
         xf d = XF(mask[i-1])-2;
         mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
      }
      for (i = end-2; i >= 0; i--) {
         xf d = XF(mask[i+1])-3;
         mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
      }
      for (i = 0; i < end; i++) {
         xf md = XF(maxDepth)-12, t, smr;
         int shift;
         md = 0 > md ? 0 : md;
         t = md > mask[i] ? md : XF(mask[i]);
         smr = sig[i]-t;
         shift = IMIN(5, IMAX(0, -(int)floorf((float)(smr+.5f))));
         spread_weight[i] = 32>>shift;
      }
   }
   if (effectiveBytes >= (30+5*LM) && !lfe) {
      int last = 0;
      for (c = 0; c < C; c++) {
         float *f = &follower[c*CELT_NB_EBANDS], tmp;
         memcpy(bandLogE3, &bandLogE2[c*CELT_NB_EBANDS], end*sizeof(float));
         if (LM == 0)
            for (i = 0; i < IMIN(8, end); i++)
               bandLogE3[i] = FL_MAX(bandLogE2[c*CELT_NB_EBANDS+i], oldBandE[c*CELT_NB_EBANDS+i]);
         f[0] = bandLogE3[0];
         for (i = 1; i < end; i++) {
            xf a;
            if (bandLogE3[i] > XF(bandLogE3[i-1])+.5f) last = i;
            a = XF(f[i-1])+1.5f;
            f[i] = (float)(a < bandLogE3[i] ? a : XF(bandLogE3[i]));
         }
         for (i = last-1; i >= 0; i--) {
            xf a = XF(f[i+1])+2, m = a < bandLogE3[i] ? a : XF(bandLogE3[i]);
            f[i] = (float)(f[i] < m ? XF(f[i]) : m);
         }
         for (i = 2; i < end-2; i++) {
            xf m = XF(fl_e_median_of_5(&bandLogE3[i-2]))-1;
            f[i] = (float)(f[i] > m ? XF(f[i]) : m);
         }
         {
            xf m = XF(fl_e_median_of_3(&bandLogE3[0]))-1;
            f[0] = (float)(f[0] > m ? XF(f[0]) : m);
            f[1] = (float)(f[1] > m ? XF(f[1]) : m);
            m = XF(fl_e_median_of_3(&bandLogE3[end-3]))-1;
            f[end-2] = (float)(f[end-2] > m ? XF(f[end-2]) : m);
            f[end-1] = (float)(f[end-1] > m ? XF(f[end-1]) : m);
         }
         for (i = 0; i < end; i++) f[i] = FL_MAX(f[i], noise_floor[i]);
         (void)tmp;
      }
      if (C == 2) {
         for (i = start; i < end; i++) {
            float a = follower[CELT_NB_EBANDS+i], b = follower[i];
            xf M = XF(b)-4, F0, d0, d1;
            M = a > M ? XF(a) : M;
            follower[CELT_NB_EBANDS+i] = (float)M;
            F0 = M-4;
            F0 = b > F0 ? XF(b) : F0;
            follower[i] = (float)F0;
            d0 = XF(bandLogE[i])-F0;
            d1 = XF(bandLogE[CELT_NB_EBANDS+i])-follower[CELT_NB_EBANDS+i];
            follower[i] = (float)(((0 > d0 ? 0 : d0)+(0 > d1 ? 0 : d1))*.5f);
         }
      } else {
         for (i = start; i < end; i++) follower[i] = FL_MAX(0, bandLogE[i]-follower[i]);
      }
      for (i = start; i < end; i++) follower[i] = FL_MAX(follower[i], surround_dynalloc[i]);
      for (i = start; i < end; i++)
         importance[i] = (int)floorf((float)(XF(fl_e_exp2(FL_MIN(follower[i], 4.f)))*13+.5f));
      if ((!vbr || constrained_vbr) && !isTransient)
         for (i = start; i < end; i++) follower[i] = .5f*follower[i];
      for (i = start; i < end; i++) {
         if (i < 8) follower[i] *= 2;
         if (i >= 12) follower[i] = .5f*follower[i];
      }
      if (toneishness > .98f) {
         int freq_bin = (int)floor((double)(XF(tone_freq)*120.f/XF(3.141592653589793)+.5f));
         for (i = start; i < end; i++) {
            if (freq_bin >= eBands[i] && freq_bin <= eBands[i+1]) follower[i] += 2.f;
            if (freq_bin >= eBands[i]-1 && freq_bin <= eBands[i+1]+1) follower[i] += 1.f;
            if (freq_bin >= eBands[i]-2 && freq_bin <= eBands[i+1]+2) follower[i] += 1.f;
            if (freq_bin >= eBands[i]-3 && freq_bin <= eBands[i+1]+3) follower[i] += .5f;
         }
         if (freq_bin >= eBands[end]) {
            follower[end-1] += 2.f;
            follower[end-2] += 1.f;
         }
      }
      if (analysis->valid)
         for (i = start; i < IMIN(LEAK_BANDS, end); i++)
            follower[i] = (float)(XF(1.f/64.f)*analysis->leak_boost[i]+follower[i]);
      for (i = start; i < end; i++) {
         int width, boost, boost_bits;
         follower[i] = FL_MIN(follower[i], 4.f);
         width = C*(eBands[i+1]-eBands[i])<<LM;
         if (width < 6) {
            boost = (int)follower[i];
            boost_bits = boost*width<<BITRES;
         } else if (width > 48) {
            boost = (int)(follower[i]*8);
            boost_bits = (boost*width<<BITRES)/8;
         } else {
            boost = (int)(float)((XF(follower[i])*width)/6.f);
            boost_bits = boost*6<<BITRES;
         }
         if ((!vbr || (constrained_vbr && !isTransient)) && (tot_boost+boost_bits)>>BITRES>>3 > 2*effectiveBytes/3) {
            fl_i32 cap = ((2*effectiveBytes/3)<<BITRES<<3);
            offsets[i] = cap-tot_boost;
            tot_boost = cap;
            break;
         } else {
            offsets[i] = boost;
            tot_boost += boost_bits;
         }
      }
   } else {
      for (i = start; i < end; i++) importance[i] = 13;
   }
   *tot_boost_ = tot_boost;
   return maxDepth;
}

static int fl_e_run_prefilter(fl_celt_enc *st, float *in, int CC, int N, int prefilter_tapset, int *pitch,
      float *gain, int *qgain, int enabled, int complexity, float tf_estimate, int nbAvailableBytes,
      const fl_analysis_info *analysis, float tone_freq, float toneishness) {
   const int max_period = COMBFILTER_MAXPERIOD, min_period = COMBFILTER_MINPERIOD, overlap = CELT_OVERLAP;
   float _pre[2*(960+COMBFILTER_MAXPERIOD)], *pre[2], before[2] = {0, 0}, after[2] = {0, 0}, gain_f;
   int c, pitch_index, pf_on, qg, cancel_pitch = 0;
   xf gain1, pf_threshold;
   pre[0] = _pre;
   pre[1] = _pre+(N+max_period);
   for (c = 0; c < CC; c++) {
      memcpy(pre[c], st->prefilter_mem+c*max_period, max_period*sizeof(float));
      memcpy(pre[c]+max_period, in+c*(N+overlap)+overlap, N*sizeof(float));
   }
   if (enabled && toneishness > .99f) {
      int multiple = 1;
      if (tone_freq >= 3.1416f) tone_freq = 3.141593f-tone_freq;
      while (tone_freq >= XF(.39f)*multiple) multiple++;
      if (tone_freq > .006148f)
         pitch_index = IMIN((int)(double)((XF(6.283185307179586)*multiple)/tone_freq+.5f), COMBFILTER_MAXPERIOD-2);
      else pitch_index = COMBFILTER_MINPERIOD;
      gain1 = .75f;
   } else if (enabled && complexity >= 5) {
      float pitch_buf[(COMBFILTER_MAXPERIOD+960)>>1];
      fl_e_pitch_downsample(pre, pitch_buf, (max_period+N)>>1, CC);
      fl_e_pitch_search(pitch_buf+(max_period>>1), pitch_buf, N, max_period-3*min_period, &pitch_index);
      pitch_index = max_period-pitch_index;
      gain1 = fl_e_remove_doubling(pitch_buf, max_period, min_period, N, &pitch_index, st->prefilter_period,
            st->prefilter_gain);
      if (pitch_index > max_period-2) pitch_index = max_period-2;
      gain1 = XF(.7f)*gain1;
      if (st->loss_rate > 2) gain1 = XF(.5f)*gain1;
      if (st->loss_rate > 4) gain1 = XF(.5f)*gain1;
      if (st->loss_rate > 8) gain1 = 0;
   } else {
      gain1 = 0;
      pitch_index = COMBFILTER_MINPERIOD;
   }
   if (analysis->valid) gain1 = gain1*analysis->max_pitch_ratio;
   pf_threshold = .2f;
   if (abs(pitch_index-st->prefilter_period)*10 > pitch_index) {
      pf_threshold += .2f;
      if (tf_estimate > .98f) gain1 = 0;
   }
   if (nbAvailableBytes < 25) pf_threshold += .1f;
   if (nbAvailableBytes < 35) pf_threshold += .1f;
   if (st->prefilter_gain > .4f) pf_threshold -= .1f;
   if (st->prefilter_gain > .55f) pf_threshold -= .1f;
   if (!(pf_threshold > .2f)) pf_threshold = .2f;
   if (gain1 < pf_threshold) {
      gain_f = 0;
      pf_on = 0;
      qg = 0;
   } else {
      if (fabsl(gain1-st->prefilter_gain) < .1f) gain1 = st->prefilter_gain;
      qg = (int)floorf((float)((gain1*32.f)/3.f+.5f))-1;
      qg = IMAX(0, IMIN(7, qg));
      gain_f = (float)(XF(.09375f)*(qg+1));
      pf_on = 1;
   }
   for (c = 0; c < CC; c++) {
      int i;
      float *y = in+c*(N+overlap)+overlap;
      st->prefilter_period = IMAX(st->prefilter_period, COMBFILTER_MINPERIOD);
      memcpy(in+c*(N+overlap), st->in_mem+c*overlap, overlap*sizeof(float));
      for (i = 0; i < N; i++) before[c] = before[c]+fabsf(y[i]);
      fl_e_comb_filter(y, pre[c]+max_period, st->prefilter_period, pitch_index, N, -st->prefilter_gain, -gain_f,
            st->prefilter_tapset, prefilter_tapset, fl_window120, overlap);
      for (i = 0; i < N; i++) after[c] = after[c]+fabsf(y[i]);
   }
   if (CC == 2) {
      xf g = XF(.25f)*gain_f;
      xf thresh0 = g*before[0]+XF(.01f)*before[1], thresh1 = g*before[1]+XF(.01f)*before[0];
      if (XF(after[0])-before[0] > thresh0 || XF(after[1])-before[1] > thresh1) cancel_pitch = 1;
      if (XF(before[0])-after[0] < thresh0 && XF(before[1])-after[1] < thresh1) cancel_pitch = 1;
   } else {
      if (after[0] > before[0]) cancel_pitch = 1;
   }
   if (cancel_pitch) {
      for (c = 0; c < CC; c++) {
         memcpy(in+c*(N+overlap)+overlap, pre[c]+max_period, N*sizeof(float));
         fl_e_comb_filter(in+c*(N+overlap)+overlap, pre[c]+max_period, st->prefilter_period, pitch_index, overlap,
               -st->prefilter_gain, -0.f, st->prefilter_tapset, prefilter_tapset, fl_window120, overlap);
      }
      gain_f = 0;
      pf_on = 0;
      qg = 0;
   }
   for (c = 0; c < CC; c++) {
      memcpy(st->in_mem+c*overlap, in+c*(N+overlap)+N, overlap*sizeof(float));
      memmove(st->prefilter_mem+c*max_period, st->prefilter_mem+c*max_period+N, (max_period-N)*sizeof(float));
      memcpy(st->prefilter_mem+c*max_period+max_period-N, pre[c]+max_period, N*sizeof(float));
   }
   *gain = gain_f;
   *pitch = pitch_index;
   *qgain = qg;
   return pf_on;
}

static int fl_celt_encode(fl_celt_enc *st, const float *pcm, int frame_size, unsigned char *compressed,
      int nbCompressedBytes, ec_ctx *enc) {
   static const float intensity_thresholds[21] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 24, 36, 44, 50, 56, 62, 67, 72, 79, 88, 106, 134};
   static const float intensity_histeresis[21] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 6, 8, 8};
   const fl_i16 *eBands = fl_eband5ms;
   const int CC = st->channels, C = st->stream_channels, overlap = CELT_OVERLAP, nbEBands = CELT_NB_EBANDS;
   int i, c, N, LM, M, tf_select, nbFilledBytes, nbAvailableBytes, start = st->start, end = st->end, effEnd;
   int codedBands, alloc_trim, pitch_index = COMBFILTER_MINPERIOD, dual_stereo = 0, effectiveBytes;
   int dynalloc_logp, prefilter_tapset = 0, pf_on, anti_collapse_rsv, anti_collapse_on = 0, silence = 0;
   int tf_chan = 0, pitch_change = 0, shortBlocks = 0, isTransient = 0, secondMdct, signalBandwidth;
   int transient_got_disabled = 0, hybrid = start != 0, weak_transient = 0, enable_tf_analysis;
   fl_i32 bits, vbr_rate, total_bits, total_boost, balance, tell, tell0_frac, tot_boost, equiv_rate, min_allowed;
   float gain1 = 0, tf_estimate = 0, sample_max, maxDepth, temporal_vbr = 0, tone_freq, toneishness = 0;
   float surround_masking = 0, surround_trim = 0, surround_dynalloc[2*CELT_NB_EBANDS];
   float in[2*(960+CELT_OVERLAP)], freq[2*960], X[2*960];
   float bandE[2*CELT_NB_EBANDS], bandLogE[2*CELT_NB_EBANDS], bandLogE2[2*CELT_NB_EBANDS], error[2*CELT_NB_EBANDS];
   int fine_quant[CELT_NB_EBANDS], pulses[CELT_NB_EBANDS], cap[CELT_NB_EBANDS], offsets[CELT_NB_EBANDS];
   int importance[CELT_NB_EBANDS], spread_weight[CELT_NB_EBANDS], fine_priority[CELT_NB_EBANDS], tf_res[CELT_NB_EBANDS];
   unsigned char collapse_masks[2*CELT_NB_EBANDS];
   ec_ctx _enc;
   float *oldBandE = st->oldBandE, *oldLogE = st->oldLogE, *oldLogE2 = st->oldLogE2, *energyError = st->energyError;
   if (nbCompressedBytes < 2 || pcm == NULL) return FL_ERR_BAD_ARG;
   for (LM = 0; LM <= CELT_MAX_LM; LM++)
      if (CELT_SHORT_MDCT<<LM == frame_size) break;
   if (LM > CELT_MAX_LM) return FL_ERR_BAD_ARG;
   M = 1<<LM;
   N = M*CELT_SHORT_MDCT;
   if (enc == NULL) {
      tell0_frac = tell = 1;
      nbFilledBytes = 0;
   } else {
      tell0_frac = ec_tell_frac(enc);
      tell = ec_tell(enc);
      nbFilledBytes = (tell+4)>>3;
   }
   nbCompressedBytes = IMIN(nbCompressedBytes, 1275);
   if (st->vbr && st->bitrate != -1) {
      vbr_rate = (st->bitrate*6/(6*48000/frame_size))<<BITRES;
      effectiveBytes = vbr_rate>>(3+BITRES);
   } else {
      fl_i32 tmp;
      vbr_rate = 0;
      tmp = st->bitrate*frame_size;
      if (tell > 1) tmp += tell*48000;
      if (st->bitrate != -1) {
         nbCompressedBytes = IMAX(2, IMIN(nbCompressedBytes, (tmp+4*48000)/(8*48000)));
         if (enc != NULL) ec_enc_shrink(enc, nbCompressedBytes);
      }
      effectiveBytes = nbCompressedBytes-nbFilledBytes;
   }
   nbAvailableBytes = nbCompressedBytes-nbFilledBytes;
   equiv_rate = ((fl_i32)nbCompressedBytes*8*50<<(3-LM))-(40*C+20)*((400>>LM)-50);
   if (st->bitrate != -1) equiv_rate = IMIN(equiv_rate, st->bitrate-(40*C+20)*((400>>LM)-50));
   if (enc == NULL) {
      ec_enc_init(&_enc, compressed, nbCompressedBytes);
      enc = &_enc;
   }
   if (vbr_rate > 0 && st->constrained_vbr) {
      fl_i32 max_allowed = IMIN(IMAX(tell == 1 ? 2 : 0, (vbr_rate+vbr_rate-st->vbr_reservoir)>>(BITRES+3)), nbAvailableBytes);
      if (max_allowed < nbAvailableBytes) {
         nbCompressedBytes = nbFilledBytes+max_allowed;
         nbAvailableBytes = max_allowed;
         ec_enc_shrink(enc, nbCompressedBytes);
      }
   }
   total_bits = nbCompressedBytes*8;
   effEnd = end;
   if (effEnd > CELT_NB_EBANDS) effEnd = CELT_NB_EBANDS;
   sample_max = FL_MAX(st->overlap_max, fl_e_maxabs(pcm, C*(N-overlap)));
   st->overlap_max = fl_e_maxabs(pcm+C*(N-overlap), C*overlap);
   sample_max = FL_MAX(sample_max, st->overlap_max);
   silence = sample_max <= XF(1)/(1<<st->lsb_depth);
   if (tell == 1) ec_enc_bit_logp(enc, silence, 15);
   else silence = 0;
   if (silence) {
      if (vbr_rate > 0) {
         effectiveBytes = nbCompressedBytes = IMIN(nbCompressedBytes, nbFilledBytes+2);
         total_bits = nbCompressedBytes*8;
         nbAvailableBytes = 2;
         ec_enc_shrink(enc, nbCompressedBytes);
      }
      tell = nbCompressedBytes*8;
      enc->nbits_total += tell-ec_tell(enc);
   }
   for (c = 0; c < CC; c++) {
      int need_clip = st->clip && sample_max > 65536.f;
      fl_e_preemphasis(pcm+c, in+c*(N+overlap)+overlap, N, CC, st->preemph_memE+c, need_clip);
      memcpy(in+c*(N+overlap), &st->prefilter_mem[(1+c)*COMBFILTER_MAXPERIOD-overlap], overlap*sizeof(float));
   }
   tone_freq = fl_e_tone_detect(in, CC, N+overlap, &toneishness, 48000);
   if (st->complexity >= 1 && !st->lfe) {
      int allow_weak_transients = hybrid && effectiveBytes < 15 && st->silk_signal_type != 2;
      isTransient = fl_e_transient_analysis(in, N+overlap, CC, &tf_estimate, &tf_chan, allow_weak_transients,
            &weak_transient, tone_freq, toneishness);
   }
   {
      xf t = 1.0L-tf_estimate;
      toneishness = (float)(t < toneishness ? t : XF(toneishness));
   }
   {
      int enabled, qg;
      enabled = ((st->lfe && nbAvailableBytes > 3) || nbAvailableBytes > 12*C) && !hybrid && !silence && tell+16 <= total_bits && !st->disable_pf;
      prefilter_tapset = st->tapset_decision;
      pf_on = fl_e_run_prefilter(st, in, CC, N, prefilter_tapset, &pitch_index, &gain1, &qg, enabled, st->complexity,
            tf_estimate, nbAvailableBytes, &st->analysis, tone_freq, toneishness);
      if ((gain1 > .4f || st->prefilter_gain > .4f) && (!st->analysis.valid || st->analysis.tonality > .3)
            && (pitch_index > XF(1.26)*st->prefilter_period || pitch_index < XF(.79)*st->prefilter_period))
         pitch_change = 1;
      if (pf_on == 0) {
         if (!hybrid && tell+16 <= total_bits) ec_enc_bit_logp(enc, 0, 1);
      } else {
         int octave;
         ec_enc_bit_logp(enc, 1, 1);
         pitch_index += 1;
         octave = EC_ILOG(pitch_index)-5;
         ec_enc_uint(enc, octave, 6);
         ec_enc_bits(enc, pitch_index-(16<<octave), 4+octave);
         pitch_index -= 1;
         ec_enc_bits(enc, qg, 3);
         ec_enc_icdf(enc, prefilter_tapset, fl_tapset_icdf, 2);
      }
   }
   if (LM > 0 && ec_tell(enc)+3 <= total_bits) {
      if (isTransient) shortBlocks = M;
   } else {
      isTransient = 0;
      transient_got_disabled = 1;
   }
   secondMdct = shortBlocks && st->complexity >= 8;
   if (secondMdct) {
      fl_e_compute_mdcts(0, in, freq, C, CC, LM);
      fl_e_band_energies(freq, bandE, effEnd, C, LM);
      fl_e_amp2log2(effEnd, end, bandE, bandLogE2, C);
      for (c = 0; c < C; c++)
         for (i = 0; i < end; i++) bandLogE2[nbEBands*c+i] = (float)(XF(.5f)*LM+bandLogE2[nbEBands*c+i]);
   }
   fl_e_compute_mdcts(shortBlocks, in, freq, C, CC, LM);
   if (CC == 2 && C == 1) tf_chan = 0;
   fl_e_band_energies(freq, bandE, effEnd, C, LM);
   if (st->lfe) {
      for (i = 2; i < end; i++) {
         xf lim = XF(1e-4f)*bandE[0];
         xf v = lim < bandE[i] ? lim : XF(bandE[i]);
         bandE[i] = (float)(v < XF(1e-15f) ? XF(1e-15f) : v);
      }
   }
   fl_e_amp2log2(effEnd, end, bandE, bandLogE, C);
   memset(surround_dynalloc, 0, end*sizeof(float));
   if (!hybrid && st->energy_mask && !st->lfe) {
      /* masking between surround channels; all sums stay in x87 registers */
      int mask_end = IMAX(2, st->lastCodedBands), midband, count_dynalloc = 0, count = 0;
      xf mask_avg = 0, diff = 0;
      for (c = 0; c < C; c++)
         for (i = 0; i < mask_end; i++) {
            float m = st->energy_mask[nbEBands*c+i];
            xf mask;
            m = m < .25f ? m : .25f;
            m = m > -2.f ? m : -2.f;
            mask = m;
            if (mask > 0) mask = XF(.5f)*mask;
            mask_avg += mask*(eBands[i+1]-eBands[i]);
            count += eBands[i+1]-eBands[i];
            diff += mask*(1+2*i-mask_end);
         }
      mask_avg = mask_avg/count+XF(.2f);
      diff = (diff*XF(6.f))/(C*(mask_end-1)*(mask_end+1)*mask_end);
      diff = XF(.5f)*diff;
      diff = diff < XF(.031f) ? diff : XF(.031f);
      diff = diff > XF(-.031f) ? diff : XF(-.031f);
      for (midband = 0; eBands[midband+1] < eBands[mask_end]/2; midband++);
      for (i = 0; i < mask_end; i++) {
         xf lin = diff*(i-midband)+mask_avg, unmask;
         float u = st->energy_mask[i];
         if (C == 2 && st->energy_mask[nbEBands+i] > u) u = st->energy_mask[nbEBands+i];
         u = u < 0.f ? u : 0.f;
         unmask = XF(u)-lin;
         if (unmask > XF(.25f)) {
            surround_dynalloc[i] = (float)(unmask-XF(.25f));
            count_dynalloc++;
         }
      }
      if (count_dynalloc >= 3) {
         mask_avg += XF(.25f);
         if (mask_avg > 0) {
            mask_avg = 0;
            diff = 0;
            memset(surround_dynalloc, 0, mask_end*sizeof(float));
         } else {
            for (i = 0; i < mask_end; i++) {
               xf d = XF(surround_dynalloc[i])-XF(.25f);
               surround_dynalloc[i] = (float)(d > 0 ? d : XF(0));
            }
         }
      }
      surround_masking = (float)(mask_avg+XF(.2f));
      surround_trim = (float)(XF(64.f)*diff);
   }
   if (!st->lfe) {
      xf follow = -10.f, frame_avg = 0, offset = shortBlocks ? XF(.5f)*LM : 0, tv;
      for (i = start; i < end; i++) {
         xf a = follow-1, b = XF(bandLogE[i])-offset;
         follow = a > b ? a : b;
         if (C == 2) {
            b = XF(bandLogE[i+nbEBands])-offset;
            follow = follow > b ? follow : b;
         }
         frame_avg += follow;
      }
      frame_avg /= (end-start);
      tv = frame_avg-st->spec_avg;
      tv = -1.5f > tv ? XF(-1.5f) : tv;
      temporal_vbr = (float)(3.f < tv ? XF(3.f) : tv);
      st->spec_avg = (float)(XF(temporal_vbr)*.02f+st->spec_avg);
   }
   if (!secondMdct) memcpy(bandLogE2, bandLogE, C*nbEBands*sizeof(float));
   if (LM > 0 && ec_tell(enc)+3 <= total_bits && !isTransient && st->complexity >= 5 && !st->lfe && !hybrid) {
      if (fl_e_patch_transient_decision(bandLogE, oldBandE, start, end, C)) {
         isTransient = 1;
         shortBlocks = M;
         fl_e_compute_mdcts(shortBlocks, in, freq, C, CC, LM);
         fl_e_band_energies(freq, bandE, effEnd, C, LM);
         fl_e_amp2log2(effEnd, end, bandE, bandLogE, C);
         for (c = 0; c < C; c++)
            for (i = 0; i < end; i++) bandLogE2[nbEBands*c+i] = (float)(XF(.5f)*LM+bandLogE2[nbEBands*c+i]);
         tf_estimate = .2f;
      }
   }
   if (LM > 0 && ec_tell(enc)+3 <= total_bits) ec_enc_bit_logp(enc, isTransient, 3);
   fl_e_normalise_bands(freq, X, bandE, effEnd, C, M);
   enable_tf_analysis = effectiveBytes >= 15*C && !hybrid && st->complexity >= 2 && !st->lfe && toneishness < .98f;
   maxDepth = fl_e_dynalloc_analysis(bandLogE, bandLogE2, oldBandE, start, end, C, offsets, st->lsb_depth,
         isTransient, st->vbr, st->constrained_vbr, LM, effectiveBytes, &tot_boost, &st->analysis, importance,
         spread_weight, tone_freq, toneishness, st->lfe, surround_dynalloc);
   if (enable_tf_analysis) {
      int lambda = IMAX(80, 20480/effectiveBytes+2);
      tf_select = fl_e_tf_analysis(effEnd, isTransient, tf_res, lambda, X, N, LM, tf_estimate, tf_chan, importance);
      for (i = effEnd; i < end; i++) tf_res[i] = tf_res[effEnd-1];
   } else if (hybrid && weak_transient) {
      for (i = 0; i < end; i++) tf_res[i] = 1;
      tf_select = 0;
   } else if (hybrid && effectiveBytes < 15 && st->silk_signal_type != 2) {
      for (i = 0; i < end; i++) tf_res[i] = 0;
      tf_select = isTransient;
   } else {
      for (i = 0; i < end; i++) tf_res[i] = isTransient;
      tf_select = 0;
   }
   for (c = 0; c < C; c++)
      for (i = start; i < end; i++) {
         float *e = &bandLogE[i+c*nbEBands];
         if (fabsl(XF(*e)-oldBandE[i+c*nbEBands]) < 2.f)
            *e = (float)(*e-XF(.25f)*energyError[i+c*nbEBands]);
      }
   fl_e_quant_coarse_energy(start, end, effEnd, bandLogE, oldBandE, total_bits, error, enc, C, LM,
         nbAvailableBytes, st->force_intra, &st->delayedIntra, st->complexity >= 4, st->loss_rate, st->lfe);
   fl_e_tf_encode(start, end, isTransient, tf_res, LM, tf_select, enc);
   if (ec_tell(enc)+4 <= total_bits) {
      if (st->lfe) {
         st->tapset_decision = 0;
         st->spread_decision = SPREAD_NORMAL;
      } else if (hybrid) {
         if (st->complexity == 0) st->spread_decision = SPREAD_NONE;
         else if (isTransient) st->spread_decision = SPREAD_NORMAL;
         else st->spread_decision = SPREAD_AGGRESSIVE;
      } else if (shortBlocks || st->complexity < 3 || nbAvailableBytes < 10*C) {
         st->spread_decision = st->complexity == 0 ? SPREAD_NONE : SPREAD_NORMAL;
      } else {
         st->spread_decision = fl_e_spreading_decision(X, &st->tonal_average, st->spread_decision, &st->hf_average,
               &st->tapset_decision, pf_on && !shortBlocks, effEnd, C, M, spread_weight);
      }
      ec_enc_icdf(enc, st->spread_decision, fl_spread_icdf, 5);
   } else st->spread_decision = SPREAD_NORMAL;
   if (st->lfe) offsets[0] = IMIN(8, effectiveBytes/3);
   fl_init_caps(cap, LM, C);
   dynalloc_logp = 6;
   total_bits <<= BITRES;
   total_boost = 0;
   tell = ec_tell_frac(enc);
   for (i = start; i < end; i++) {
      int width, quanta, dynalloc_loop_logp, boost, j;
      width = C*(eBands[i+1]-eBands[i])<<LM;
      quanta = IMIN(width<<BITRES, IMAX(6<<BITRES, width));
      dynalloc_loop_logp = dynalloc_logp;
      boost = 0;
      for (j = 0; tell+(dynalloc_loop_logp<<BITRES) < total_bits-total_boost && boost < cap[i]; j++) {
         int flag = j < offsets[i];
         ec_enc_bit_logp(enc, flag, dynalloc_loop_logp);
         tell = ec_tell_frac(enc);
         if (!flag) break;
         boost += quanta;
         total_boost += quanta;
         dynalloc_loop_logp = 1;
      }
      if (j) dynalloc_logp = IMAX(2, dynalloc_logp-1);
      offsets[i] = boost;
   }
   if (C == 2) {
      if (LM != 0) {
         /* stereo_analysis */
         xf sumLR = 1e-15f, sumMS = 1e-15f;
         int thetas;
         for (i = 0; i < 13; i++) {
            int j;
            for (j = eBands[i]<<LM; j < eBands[i+1]<<LM; j++) {
               float L = X[j], R = X[N+j];
               xf Mx = XF(L)+R, S = XF(L)-R;
               sumLR += fabsl(XF(L))+fabsl(XF(R));
               sumMS += fabsl(Mx)+fabsl(S);
            }
         }
         sumMS = XF(.707107f)*sumMS;
         thetas = 13;
         if (LM <= 1) thetas -= 8;
         dual_stereo = ((eBands[13]<<(LM+1))+thetas)*sumMS > (eBands[13]<<(LM+1))*sumLR;
      }
      st->intensity = fl_e_hysteresis_decision((float)(equiv_rate/1000), intensity_thresholds,
            intensity_histeresis, 21, st->intensity);
      st->intensity = IMIN(end, IMAX(start, st->intensity));
   }
   alloc_trim = 5;
   if (tell+(6<<BITRES) <= total_bits-total_boost) {
      if (start > 0 || st->lfe) {
         st->stereo_saving = 0;
         alloc_trim = 5;
      } else {
         /* alloc_trim_analysis */
         float trim = 5.f;
         xf diff = 0, t;
         if (equiv_rate < 64000) trim = 4.f;
         else if (equiv_rate < 80000) trim = (float)(XF(1.f/16.f)*((equiv_rate-64000)>>10)+4.f);
         if (C == 2) {
            float sum = 0, minXC, logXC, half;
            xf logXC2, l2, ss;
            for (i = 0; i < 8; i++)
               sum = (float)(sum+fl_e_inner_prod(&X[eBands[i]<<LM], &X[N+(eBands[i]<<LM)], (eBands[i+1]-eBands[i])<<LM));
            sum = .125f*sum;
            sum = FL_MIN(1.f, fabsf(sum));
            minXC = sum;
            for (i = 8; i < st->intensity; i++) {
               float p = (float)fabsl(fl_e_inner_prod(&X[eBands[i]<<LM], &X[N+(eBands[i]<<LM)], (eBands[i+1]-eBands[i])<<LM));
               minXC = FL_MIN(minXC, p);
            }
            minXC = FL_MIN(1.f, fabsf(minXC));
            {
               xf lx = fl_e_log2((float)(XF(1.001f)-XF(sum)*sum));
               half = (float)(XF(.5f)*lx);
               logXC = (float)lx;
            }
            l2 = fl_e_log2((float)(XF(1.001f)-XF(minXC)*minXC));
            logXC2 = half > l2 ? XF(half) : l2;
            {
               xf a = XF(.75f)*logXC;
               a = -4.f > a ? XF(-4.f) : a;
               trim = (float)(a+trim);
            }
            ss = XF(.25f)+st->stereo_saving;
            {
               xf b = -(XF(.5f)*logXC2);
               st->stereo_saving = (float)(ss < b ? ss : b);
            }
         }
         for (c = 0; c < C; c++)
            for (i = 0; i < end-1; i++) diff += XF(bandLogE[i+c*nbEBands])*(2+2*i-end);
         diff /= C*(end-1);
         t = (diff+1)/6.f;
         t = 2.f < t ? XF(2.f) : t;
         t = -2.f > t ? XF(-2.f) : t;
         t = XF(trim)-t;
         t = t-XF(surround_trim);
         t = t-XF(tf_estimate)*2;
         if (st->analysis.valid) {
            xf a = (XF(st->analysis.tonality_slope)+.05f)*2;
            a = 2.f < a ? XF(2.f) : a;
            a = -2.f > a ? XF(-2.f) : a;
            t = t-a;
         }
         alloc_trim = (int)floorf((float)(t+.5f));
         alloc_trim = IMAX(0, IMIN(10, alloc_trim));
      }
      ec_enc_icdf(enc, alloc_trim, fl_trim_icdf, 7);
      tell = ec_tell_frac(enc);
   }
   min_allowed = ((tell+total_boost+(1<<(BITRES+3))-1)>>(BITRES+3))+2;
   if (hybrid) min_allowed = IMAX(min_allowed, (tell0_frac+(37<<BITRES)+total_boost+(1<<(BITRES+3))-1)>>(BITRES+3));
   if (vbr_rate > 0) {
      xf alpha;
      fl_i32 delta, target, base_target;
      int lm_diff = CELT_MAX_LM-LM;
      nbCompressedBytes = IMIN(nbCompressedBytes, 1275>>(3-LM));
      if (!hybrid) base_target = vbr_rate-((40*C+20)<<BITRES);
      else base_target = IMAX(0, vbr_rate-((9*C+4)<<BITRES));
      if (st->constrained_vbr) base_target += (st->vbr_offset>>lm_diff);
      if (!hybrid) {
         /* compute_vbr */
         int coded_bands = st->lastCodedBands ? st->lastCodedBands : nbEBands, coded_bins;
         coded_bins = eBands[coded_bands]<<LM;
         if (C == 2) coded_bins += eBands[IMIN(st->intensity, coded_bands)]<<LM;
         target = base_target;
         if (st->analysis.valid && st->analysis.activity < .4)
            target -= (fl_i32)(float)((XF(.4f)-st->analysis.activity)*(coded_bins<<BITRES));
         if (C == 2) {
            int coded_stereo_bands = IMIN(st->intensity, coded_bands), coded_stereo_dof;
            xf max_frac, a, b;
            float ss = FL_MIN(st->stereo_saving, 1.f);
            coded_stereo_dof = (eBands[coded_stereo_bands]<<LM)-coded_stereo_bands;
            max_frac = (XF(.8f)*coded_stereo_dof)/coded_bins;
            a = max_frac*target;
            b = (XF(ss)-.1f)*(coded_stereo_dof<<BITRES);
            target -= (fl_i32)(float)(a < b ? a : b);
         }
         target += tot_boost-(19<<LM);
         target += (fl_i32)(float)((XF(tf_estimate)-.044f)*target);
         if (st->analysis.valid && !st->lfe) {
            xf tonal = XF(st->analysis.tonality)-.15f;
            fl_i32 tonal_target;
            tonal = (0 > tonal ? XF(0) : tonal)-.12f;
            tonal_target = target+(fl_i32)(float)((XF(coded_bins<<BITRES)*1.2f)*tonal);
            if (pitch_change) tonal_target += (fl_i32)(float)(XF(coded_bins<<BITRES)*.8f);
            target = tonal_target;
         }
         if (st->energy_mask && !st->lfe) {
            fl_i32 surround_target = target+(fl_i32)(float)(XF(surround_masking)*(coded_bins<<BITRES));
            target = IMAX(target/4, surround_target);
         }
         {
            fl_i32 floor_depth;
            int bins = eBands[nbEBands-2]<<LM;
            floor_depth = (fl_i32)(float)(XF(C*bins<<BITRES)*maxDepth);
            floor_depth = IMAX(floor_depth, target>>2);
            target = IMIN(target, floor_depth);
         }
         if ((!st->energy_mask || st->lfe) && st->constrained_vbr)
            target = base_target+(fl_i32)(float)(XF(.67f)*(target-base_target));
         if (!st->energy_mask && tf_estimate < .2f) {
            xf amount = XF(IMAX(0, IMIN(32000, 96000-equiv_rate)))*3.1e-6f;
            target += (fl_i32)(float)(((amount)*temporal_vbr)*target);
         }
         target = IMIN(2*base_target, target);
      } else {
         target = base_target;
         if (st->silk_offset < 100) target += 12<<BITRES>>(3-LM);
         if (st->silk_offset > 100) target -= 18<<BITRES>>(3-LM);
         target += (fl_i32)(float)((XF(tf_estimate)-.25f)*400.f);
         if (tf_estimate > .7f) target = IMAX(target, 50<<BITRES);
      }
      target = target+tell;
      nbAvailableBytes = (target+(1<<(BITRES+2)))>>(BITRES+3);
      nbAvailableBytes = IMAX(min_allowed, nbAvailableBytes);
      nbAvailableBytes = IMIN(nbCompressedBytes, nbAvailableBytes);
      delta = target-vbr_rate;
      target = nbAvailableBytes<<(BITRES+3);
      if (silence) {
         nbAvailableBytes = 2;
         target = 2*8<<BITRES;
         delta = 0;
      }
      if (st->vbr_count < 970) {
         st->vbr_count++;
         alpha = 1.0L/(st->vbr_count+20);
      } else alpha = .001f;
      if (st->constrained_vbr) st->vbr_reservoir += target-vbr_rate;
      if (st->constrained_vbr) {
         st->vbr_drift += (fl_i32)(float)(alpha*((delta*(1<<lm_diff))-st->vbr_offset-st->vbr_drift));
         st->vbr_offset = -st->vbr_drift;
      }
      if (st->constrained_vbr && st->vbr_reservoir < 0) {
         int adjust = (-st->vbr_reservoir)/(8<<BITRES);
         nbAvailableBytes += silence ? 0 : adjust;
         st->vbr_reservoir = 0;
      }
      nbCompressedBytes = IMIN(nbCompressedBytes, nbAvailableBytes);
      ec_enc_shrink(enc, nbCompressedBytes);
   }
   bits = (((fl_i32)nbCompressedBytes*8)<<BITRES)-(fl_i32)ec_tell_frac(enc)-1;
   anti_collapse_rsv = isTransient && LM >= 2 && bits >= ((LM+2)<<BITRES) ? (1<<BITRES) : 0;
   bits -= anti_collapse_rsv;
   signalBandwidth = end-1;
   if (st->analysis.valid) {
      int min_bandwidth;
      if (equiv_rate < (fl_i32)32000*C) min_bandwidth = 13;
      else if (equiv_rate < (fl_i32)48000*C) min_bandwidth = 16;
      else if (equiv_rate < (fl_i32)60000*C) min_bandwidth = 18;
      else if (equiv_rate < (fl_i32)80000*C) min_bandwidth = 19;
      else min_bandwidth = 20;
      signalBandwidth = IMAX(st->analysis.bandwidth, min_bandwidth);
   }
   if (st->lfe) signalBandwidth = 1;
   codedBands = fl_compute_allocation(start, end, offsets, cap, alloc_trim, &st->intensity, &dual_stereo, bits,
         &balance, pulses, fine_quant, fine_priority, C, LM, enc, 1, st->lastCodedBands, signalBandwidth);
   if (st->lastCodedBands) st->lastCodedBands = IMIN(st->lastCodedBands+1, IMAX(st->lastCodedBands-1, codedBands));
   else st->lastCodedBands = codedBands;
   fl_e_quant_fine_energy(start, end, oldBandE, error, fine_quant, enc, C);
   memset(energyError, 0, nbEBands*CC*sizeof(float));
   fl_e_quant_all_bands(start, end, X, C == 2 ? X+N : NULL, collapse_masks, bandE, pulses, shortBlocks,
         st->spread_decision, dual_stereo, st->intensity, tf_res, nbCompressedBytes*(8<<BITRES)-anti_collapse_rsv,
         balance, enc, LM, codedBands, &st->rng, st->complexity, st->disable_inv);
   if (anti_collapse_rsv > 0) {
      anti_collapse_on = st->consec_transient < 2;
      ec_enc_bits(enc, anti_collapse_on, 1);
   }
   fl_e_quant_energy_finalise(start, end, oldBandE, error, fine_quant, fine_priority, nbCompressedBytes*8-ec_tell(enc), enc, C);
   for (c = 0; c < C; c++)
      for (i = start; i < end; i++)
         energyError[i+c*nbEBands] = FL_MAX(-.5f, FL_MIN(.5f, error[i+c*nbEBands]));
   if (silence)
      for (i = 0; i < C*nbEBands; i++) oldBandE[i] = -28.f;
   st->prefilter_period = pitch_index;
   st->prefilter_gain = gain1;
   st->prefilter_tapset = prefilter_tapset;
   if (CC == 2 && C == 1) memcpy(&oldBandE[nbEBands], oldBandE, nbEBands*sizeof(float));
   if (!isTransient) {
      memcpy(oldLogE2, oldLogE, CC*nbEBands*sizeof(float));
      memcpy(oldLogE, oldBandE, CC*nbEBands*sizeof(float));
   } else {
      for (i = 0; i < CC*nbEBands; i++) oldLogE[i] = FL_MIN(oldLogE[i], oldBandE[i]);
   }
   for (c = 0; c < CC; c++) {
      for (i = 0; i < start; i++) {
         oldBandE[c*nbEBands+i] = 0;
         oldLogE[c*nbEBands+i] = oldLogE2[c*nbEBands+i] = -28.f;
      }
      for (i = end; i < nbEBands; i++) {
         oldBandE[c*nbEBands+i] = 0;
         oldLogE[c*nbEBands+i] = oldLogE2[c*nbEBands+i] = -28.f;
      }
   }
   if (isTransient || transient_got_disabled) st->consec_transient++;
   else st->consec_transient = 0;
   st->rng = enc->rng;
   ec_enc_done(enc);
   if (enc->error) return -3;
   return nbCompressedBytes;
}

/* ======== enc_analysis.c ======== */
/* ---- Tonality/music analysis (analysis.c + mlp.c, x87 rounding) ---- */
/* Tonality analysis MLP weights (from libopus mlp_data.c). */
static const fl_i8 fl_mlp_layer0_weights[800] = {
   -30, -9, 2, -12, 5, -1, 8, 9, 9, 8, -13, 18, -17, -34, -5, 17, -11, 0, -4, 10,
   2, 10, 15, -8, 2, -1, 0, 5, 13, -3, -16, 1, -5, 3, 7, -28, -13, 6, 36, -3,
   19, -60, -17, -28, 7, -11, -30, -7, 2, -42, -21, -3, 6, -22, 33, -9, 7, -30, 21, -14,
   24, -11, -20, -18, -5, -12, 12, -49, -50, -49, 16, 9, -37, -1, 9, 34, -13, -31, -31, 12,
   16, 44, -42, 2, -9, 8, -18, -6, 9, 36, 19, 11, 13, 12, -21, 3, -28, -12, 3, 33,
   25, -14, 11, 1, -94, -39, 18, -12, -11, -15, -7, 49, 52, 10, -43, 9, 57, 8, 21, -6,
   14, -15, 44, -8, 7, -30, -13, -2, -9, 25, -2, -127, 18, -11, -52, 26, -27, 27, 10, -10,
   7, 43, 6, -24, 41, 10, -18, -27, 10, 17, 9, 10, -17, -10, 20, -6, 22, 55, 35, -80,
   36, 25, -24, -36, 15, 9, -19, 88, 19, 64, -51, -35, 17, 0, -7, 41, -16, 27, 4, 15,
   -1, 18, -16, 47, -39, -54, -8, 13, -25, -20, 102, -18, -5, 44, 11, -28, 71, 2, -51, -5,
   5, 2, -83, -9, -29, 8, 21, -53, 58, -37, -7, 13, 38, 9, 34, -1, -41, 21, 4, -24,
   -36, -33, -21, 32, 75, -2, 1, -68, -1, 47, -29, 32, 20, 12, -65, -87, 5, 16, -12, 24,
   40, 15, 7, 19, -26, -17, 17, 6, -2, -37, -30, -9, 32, -127, -39, 0, -31, -27, 4, -22,
   23, -6, -77, 35, -61, 32, -37, -24, 13, -11, -1, -40, -3, 17, -7, 13, 11, 59, -19, 10,
   6, -18, 0, 13, 3, -6, -23, 19, 11, -17, 13, -1, -80, 40, -53, 69, -29, -54, 0, -4,
   33, -25, -2, 38, 35, 36, -15, 46, 2, -13, -16, -8, -8, 12, -24, -9, -55, -5, -9, 32,
   11, 7, 12, -18, -10, -86, -38, 54, 37, -25, 18, -43, 7, -27, -27, -54, 13, 9, 22, 70,
   6, 35, -7, 23, -15, -44, -6, 7, -66, -85, 32, 40, -19, -9, -7, 12, -15, 7, 2, 6,
   -35, 11, 28, 0, 26, 14, 1, 1, 4, 12, 18, 35, 22, -18, -3, 14, -1, 7, 14, -8,
   -14, -3, 4, -3, -19, -7, -1, -25, -27, 25, -26, -2, 33, -22, -27, -25, 4, -9, 7, 21,
   26, -30, 10, -9, -20, 11, 27, 10, 5, -18, 14, -4, 2, -17, -5, -7, -9, -13, 15, 29,
   1, -10, -16, -10, 35, 36, -7, -22, -44, 17, 30, 22, 21, -1, 22, -11, 32, -8, -7, 5,
   -10, 5, 30, -20, 29, -20, -34, 12, -4, -6, 6, -13, 10, -5, -68, -1, 24, 9, 19, -24,
   -64, 31, 19, 27, -26, 75, -45, 41, 39, -42, 8, 6, 23, -30, 16, -25, 30, 34, 8, -38,
   -3, 18, 16, -31, 22, -4, -9, 1, 20, 9, 38, -32, 0, -45, 0, -6, -13, 11, -25, -32,
   -22, 31, -24, -11, -11, -4, -4, 20, -34, 22, 20, 9, -25, 27, -5, 28, -29, 29, 6, 21,
   -6, -18, 54, 4, -46, 23, 21, -14, -31, 36, -41, -24, 4, 22, 10, 11, 7, 36, -32, -13,
   -52, -17, 24, 28, -37, -36, -1, 24, 9, -38, 35, 48, 18, 2, -1, 45, 10, 39, 24, -38,
   13, 8, -16, 8, 25, 11, 7, -29, -11, 7, 20, -30, -38, -45, 14, -18, -28, -9, 65, 61,
   22, -53, -38, -16, 36, 46, 20, -39, 32, -61, -6, -6, -36, -33, -18, -28, 56, 101, 45, 11,
   -28, -23, -29, -61, 20, -47, 2, 48, 27, -17, 1, 40, 1, 3, -51, 15, 35, 28, 22, 35,
   53, -61, -29, 12, -6, -21, 10, 3, -20, 2, -25, 1, -6, 31, 11, -3, 1, -10, -52, 6,
   126, -105, 122, 127, -128, 127, 127, -128, 127, 108, 12, 127, 48, -128, -36, -128, 127, 127, -128, -128,
   127, 89, -128, 127, -128, -128, -128, 127, 127, -128, -128, -93, -82, 20, 125, 65, -82, 127, 38, -74,
   81, 88, -88, 79, 51, -47, -111, -26, 14, 83, -88, -112, 24, 35, -101, 98, -99, -48, -45, 46,
   83, -60, -79, 45, -20, -41, 9, 4, 52, 54, 93, -10, 4, 13, 3, 123, 6, 94, -111, -69,
   -14, -31, 10, 12, 53, -79, -11, -21, -2, -44, -72, 92, 65, -57, 56, -38, 127, -56, -128, 127,
   127, -128, 86, 117, -75, -128, 127, -19, -99, -112, 127, -128, 127, -48, 114, 118, -128, -128, 117, -17,
   -6, 121, -128, 127, -128, 82, 54, -106, 127, 127, -33, 100, -39, -23, 18, -78, -34, -29, -1, -30,
   127, -26, 127, -128, 126, -128, 27, -23, -79, -120, -127, 127, 72, 66, 29, 7, -66, -56, -117, -128,
};
static const fl_i8 fl_mlp_layer0_bias[32] = {
   51, -16, 1, 13, -5, -6, -16, -7, 11, -6, 106, 26, 28, -14, 21, -29, 7, 18, -18, -17,
   21, -17, -9, 20, -25, -3, -34, 48, 11, -13, -31, -20,
};
static const fl_i8 fl_mlp_layer1_weights[2304] = {
   22, -1, -7, 7, 29, -27, -31, -17, -13, 33, 44, -8, 11, 33, 24, 78, 15, 19, 30, -2,
   -24, 5, 49, 5, 36, 29, -14, -11, -48, -33, 21, -42, -38, -12, 55, -37, 54, -8, 1, 36,
   17, 0, 51, 31, 59, 7, -12, 53, 4, 32, -14, 48, 5, -10, -16, -8, 1, -16, -56, -24,
   -6, 18, -2, 23, 6, 46, -6, -10, 20, 35, -44, -15, -49, 36, 16, 5, -7, -79, -67, 12,
   70, -3, -79, -54, -85, -24, 47, -22, 33, 21, 69, -1, 11, 22, 14, -16, -16, -22, -28, -11,
   11, -41, 31, -26, -33, -19, -4, 27, 32, -50, 5, -10, -38, -22, -8, 35, -31, 1, -41, -15,
   -11, 44, 28, -17, -41, -23, 17, 2, -23, -26, -13, -13, -17, 6, 14, -31, -25, 9, -19, 39,
   -8, 4, 31, -1, -45, -11, -28, -92, -46, -15, 21, 118, -22, 45, -51, 11, -20, -20, -15, 13,
   -21, -97, -29, -32, -23, -42, 94, 1, 23, -8, 63, -3, -46, 19, -26, 32, -40, -74, -26, 26,
   -4, -13, 30, -20, -30, -25, -14, -31, -45, -43, 4, -60, -48, -12, -34, 2, 2, 3, 13, 15,
   11, 16, 5, 46, -9, -55, -16, -57, 29, 14, 38, -50, -2, -44, -11, -8, 52, -27, -38, -7,
   20, 47, 17, -59, 0, 47, 46, -63, 35, -17, 19, 33, 68, -19, 2, 15, -16, 28, -16, -103,
   26, -35, 47, -39, -60, 30, 31, -23, -52, -13, 116, 47, -25, 30, 40, 30, -22, 2, 12, -27,
   -18, 31, -10, 27, -8, -66, 12, 14, 4, -26, -28, -13, 3, 13, -26, -51, 37, 5, 2, -21,
   47, 3, 13, 25, -41, -27, -8, -4, 5, -76, -33, 28, 10, 9, -46, -74, 19, 28, 25, 31,
   54, -55, 68, 38, -24, -32, 2, 4, 68, 11, -1, 99, 5, 16, -2, -74, 40, 26, -26, 33,
   31, -1, -68, 14, -6, 25, 9, 29, 60, 61, 7, -7, 0, -24, 7, 77, 4, -1, 16, -7,
   13, -15, -19, 28, -31, -24, -16, 37, 24, 13, 30, 10, -30, 11, 11, -10, 22, 60, 28, 45,
   -3, -40, -62, -5, -102, 9, -32, -27, -54, 21, 15, -5, 37, -43, -11, 37, -19, 47, -64, -128,
   -27, -114, 21, -66, 59, 46, -3, -12, -87, -9, 4, 19, -113, -36, 78, 57, -26, -38, -77, -10,
   6, 6, -75, 25, -97, -11, 33, -46, 1, 13, -21, -33, -20, 16, -6, -3, -11, -4, -27, 38,
   8, -41, -2, -33, 18, 19, -26, 1, -29, -22, -4, -14, -55, -11, -80, -3, 11, 34, 90, 51,
   11, 17, 43, 36, 127, -32, 29, 103, 9, 27, 13, 64, 56, 70, -14, 3, -12, 10, 37, 3,
   12, -22, -10, 46, 28, 10, 20, 26, -24, 18, 9, 7, 14, 34, -5, -7, 31, -14, -56, 11,
   -18, -8, -17, -7, -10, -40, 10, -33, -32, -43, 5, 9, 11, -4, 10, 50, -12, -5, 46, 9,
   7, 1, 11, 15, 91, -17, 7, -50, 23, 6, -30, -99, 0, -17, 14, 8, -10, -25, -30, -69,
   -62, 31, 127, 114, -23, 101, -5, -54, -6, -22, 7, -56, 39, 18, -29, 0, 46, 8, -79, 4,
   -21, 18, -32, 62, -12, -8, -12, -58, 31, -32, 17, 6, -24, 25, 24, 9, -4, -19, 45, 6,
   17, -14, 5, -27, 16, -4, -41, 25, -36, 5, 15, 12, 50, 27, 25, 23, -44, -69, -9, -19,
   -48, -8, 4, 12, -6, 13, -19, -30, -36, 26, 37, -1, -3, -30, -42, -14, -10, -20, 26, -54,
   -27, -44, 4, 73, -26, 90, 32, -69, -29, -16, 3, 103, 15, -17, 37, 24, -23, -31, 33, -37,
   -64, 25, 13, -81, -28, -32, 27, 5, -35, -23, 15, -22, 19, -7, 9, 30, 19, -23, 27, -13,
   43, 29, -29, -6, 9, -40, -33, -33, -32, 9, 11, -48, -8, -23, -52, 46, 17, -22, -42, 35,
   -15, -41, 16, 34, 31, -42, -19, -11, 55, 7, -39, 89, -11, -33, 20, -14, 22, 32, 3, -17,
   -6, 14, 34, 1, 55, -21, -90, -8, 18, 27, 13, -29, 21, 15, -33, -51, -9, -11, 4, -16,
   -18, 23, -4, -4, 48, 1, 7, 29, -14, -12, -16, 17, 35, 8, 0, -7, -2, 9, 8, 17,
   -6, 53, -32, -21, -50, 5, 99, -60, -5, -53, 10, -31, 12, -5, 7, 80, 36, 18, -31, 9,
   98, 36, -63, -35, 4, -13, -28, -24, 28, -13, 18, 16, -1, -18, -34, 10, 20, 7, 4, 29,
   11, 25, -7, 36, 14, 45, 24, 1, -16, 30, 6, 35, -6, -11, -24, 13, -1, 27, 39, 20,
   48, -11, -4, -13, 28, 11, -31, -18, 31, -29, 22, -2, -20, -16, 5, 30, -12, -28, -3, 93,
   -16, 23, 18, -29, 6, -54, -37, 28, -3, -3, -47, -3, -36, -55, -3, 41, -10, 47, -2, 23,
   42, -7, -71, -27, 83, -64, 7, -24, 8, 26, -17, 15, 12, 31, -30, -38, -13, -33, -56, 4,
   -17, 20, 18, 1, -30, -5, -6, -31, -14, -37, 0, 22, 10, -30, 37, -17, 18, 6, 5, 23,
   -36, -32, 14, 18, -13, -61, -52, -69, 44, -30, 16, 18, -4, -25, 14, 81, 26, -8, -23, -59,
   52, -104, 17, 119, -32, 26, 17, 1, 23, 45, 29, -64, -57, -14, 73, 21, -13, -13, 9, -68,
   -7, -52, 3, 24, -39, 44, -15, 27, 14, 19, -9, -28, -11, 5, 3, -34, -2, 2, 22, -6,
   -23, 4, 3, 13, -22, -13, -10, -18, 29, 6, 44, -13, -24, -8, 2, 30, 14, 43, 6, 17,
   -73, -6, -7, 20, -80, -7, -7, -28, 15, -69, -38, -5, -100, -35, 15, -79, 23, 29, -18, -27,
   21, -66, -37, 8, -22, -39, 48, 4, -13, 1, -9, 11, -29, 22, 6, -49, 32, -14, 47, -18,
   -4, 44, -52, -74, 43, 30, 23, -14, 5, 0, -27, 4, -7, 10, -4, 10, 1, -16, 11, -18,
   -2, -5, 2, -11, 0, -20, -4, 38, 74, 59, 39, 64, -10, 26, -3, -40, -68, 3, -30, -51,
   8, -19, -27, -46, 51, 52, 54, 36, 90, 92, 14, 13, -5, 0, 16, -62, 16, 11, -47, -37,
   -6, -5, 21, 54, -57, 32, 42, -6, 62, -9, 16, 21, 24, 9, -10, -4, 33, 50, 13, -15,
   1, -35, -48, 18, -11, -17, -67, -13, 21, 38, -44, 36, -16, 29, 17, 5, -10, 18, 17, -32,
   2, 8, 22, -56, -15, -32, 40, 43, 19, 46, -7, -100, -96, 19, 53, 24, 21, -26, -48, -101,
   -82, 61, 38, -85, -28, -34, -1, 63, -5, -5, 39, 39, -38, 32, -12, -28, 20, 40, -8, 2,
   31, 12, -35, -13, 20, -25, 30, 8, 3, -13, -9, -20, 2, -13, 24, 37, -10, 33, 6, 20,
   -16, -24, -6, -6, -19, -5, 22, 21, 10, 11, -4, -39, -1, 6, 49, 41, -15, -57, 21, -62,
   77, -69, -13, 0, -74, 1, -7, -38, -8, 6, 63, 28, 4, 26, -52, 82, 63, 13, 45, -33,
   44, -52, -65, -21, -46, -49, 64, -17, 32, 24, 68, -39, -16, -5, -26, 28, 5, -61, -28, 2,
   24, 11, -12, -33, 9, -37, -3, -28, 22, -37, -12, 19, 0, -18, -2, 14, 1, 4, 8, -9,
   -2, 43, -17, -2, -66, -31, 56, -40, -87, -36, -2, -4, -42, -45, -1, 31, -43, -15, 27, 63,
   -11, 32, -10, -33, 27, -19, 4, 15, -26, -34, 29, -4, -39, -65, 14, -20, -21, -17, -36, 13,
   59, 47, -38, -33, 13, -37, -8, -37, -7, -6, -76, -31, -12, -46, 7, 24, -21, -30, -14, 9,
   15, -12, -13, 47, -27, -25, -1, -39, 0, 20, -9, 6, 7, 4, 3, 7, 39, 50, 22, -7,
   14, -20, 1, 70, -28, 29, -41, 10, -16, -5, -28, -2, -37, 32, -18, 17, 62, -11, -20, -50,
   36, 21, -62, -12, -56, 52, 50, 17, 3, 48, 44, -41, -25, 3, 16, -3, 0, 33, -6, 15,
   27, 34, -25, 22, 9, 17, -11, 36, 16, -2, 12, 21, -52, 45, -2, -10, 46, 21, -18, 67,
   -28, -13, 30, 37, 42, 16, -9, 11, 75, 7, -64, -40, -10, 29, 57, -23, 5, 53, -77, 3,
   -17, -5, 47, -55, -35, -36, -13, 52, -53, -71, 52, -111, -23, -26, -28, 29, -43, 55, -19, 43,
   -19, 54, -12, -33, -44, -39, -19, -10, -31, -10, 21, 38, -57, -20, 2, -25, 8, -6, 50, 12,
   15, 25, -25, 15, -30, -6, 9, 25, 37, 19, -4, 31, -22, 2, 4, 2, 36, 7, 3, -34,
   -80, 36, -10, -2, -5, 31, -36, 49, -70, 20, -36, 21, 24, 25, -46, -51, 36, -58, -48, -40,
   -10, 55, 71, 47, 10, -1, 1, 2, -46, -68, 16, 13, 0, -74, -29, 73, -52, -18, -11, 7,
   -44, -82, -32, -70, -28, -1, -39, -68, -6, -41, 12, -22, -16, 40, -11, -25, 51, -9, 21, 4,
   4, -34, 7, -78, 16, 6, -38, -30, -2, -44, 32, 0, 22, 64, 5, -72, -2, -14, -10, -16,
   -8, -25, 12, 102, -58, 37, -10, -23, 15, 49, 7, -7, 2, -20, -32, 45, -6, 48, 28, 30,
   33, -1, 22, -6, 30, 65, -17, 29, 74, 37, -26, -10, 15, -24, 19, -66, 22, -10, -31, -1,
   -18, -9, 11, 37, -4, 45, 5, 41, 17, 1, 1, 24, -58, 41, 5, -51, 14, 8, 43, 16,
   -10, -1, 45, 32, -64, 3, -33, -25, -3, -27, -68, 12, 23, -11, -13, -37, -40, 4, -21, -12,
   32, -23, -19, 76, 41, -23, -24, -44, -65, -1, -15, 1, 71, 63, 5, 20, -3, 21, -23, 31,
   -32, 18, -2, 27, 31, 46, -5, -39, -5, -35, 18, -18, -40, -10, 3, 12, 2, -2, -22, 40,
   5, -6, 60, 36, 3, 29, -27, 10, 25, -54, 5, 26, 39, 35, -24, -37, 30, -91, 28, -4,
   -21, -27, -39, -6, 5, 12, -128, 38, -16, 29, -95, -29, 82, -2, 35, 2, 12, 8, -22, 10,
   80, -47, 2, -25, -73, -79, 16, -30, -32, -66, 48, 21, -45, -11, -47, 14, -27, -17, -7, 15,
   -44, -14, -44, -26, -32, 26, -23, 17, -7, -28, 26, -6, 28, 6, -26, 2, 13, -14, -23, -14,
   19, 46, 16, 2, -33, -21, 28, -17, -42, 44, -37, 1, -39, 28, 84, -46, 15, 10, 13, -44,
   72, -26, 26, 32, -28, -12, -83, 2, 10, -30, -44, -10, -28, 53, 45, 65, 0, -25, 57, 36,
   -33, 6, 29, 44, -53, 11, 19, -2, -27, 35, 32, 49, 4, 23, 38, 36, 24, 10, 51, -39,
   4, -7, 26, 37, -35, 11, -47, -18, 28, 16, -35, 42, 17, -21, -41, 28, 14, -12, 11, -45,
   7, -43, -15, 18, -5, 38, -40, -50, -30, -21, 9, -98, 13, 12, 23, 75, -56, -7, -3, -4,
   -1, -34, 12, -49, 11, 26, -18, -28, -17, 33, 13, -14, 40, 24, -72, -37, 10, 17, -6, 22,
   16, 16, -6, -12, -30, -14, 10, 40, -23, 12, 15, -3, -15, 13, -56, -4, -30, 1, -3, -17,
   27, 50, -5, 64, -36, -19, 7, 29, 22, 25, 9, -16, -58, -69, -40, -61, -71, -14, 42, 93,
   26, 11, -6, -58, -11, 70, -52, 19, 9, -30, -33, 11, -37, -47, -21, -22, -40, 10, 47, 4,
   -23, 17, 48, 41, -48, 14, 10, 15, 34, -23, -2, -47, 23, -32, -13, -10, -26, -26, -4, 16,
   38, -14, 0, -12, -7, -7, 20, 44, -1, -32, -27, -16, 4, -6, -18, 14, 5, 4, -29, 28,
   7, -7, 15, -11, -20, -45, -36, 16, 84, 34, -59, -30, 22, 126, 8, 68, 79, -17, 21, -68,
   37, 5, 15, 63, 49, 127, -90, 85, 43, 7, 16, 9, 6, -45, -57, -43, 57, 11, -23, -11,
   -29, 60, -26, 0, 7, 42, -24, 10, 23, -25, 8, -7, -40, 19, -17, 35, 4, 27, -39, -91,
   27, -36, 34, 2, 16, -24, 25, 7, -21, 5, 17, 10, -22, -30, 9, -17, -61, -26, 33, 21,
   58, -51, -14, 69, -38, 20, 7, 80, -4, -65, -6, -27, 53, -12, 47, -1, -15, 1, 60, 102,
   -79, -4, 12, 9, 22, 37, -8, -4, 37, 2, -3, -15, -16, -11, -5, 19, -6, -43, 20, -25,
   -18, 10, -27, 0, -28, -27, -11, 10, -18, -2, -4, -16, 26, 14, -6, 7, -6, 1, 53, -2,
   -29, 23, 9, -30, -6, -4, -6, 56, 70, 0, -33, -20, -17, -9, -24, 46, -5, -105, 47, -46,
   -51, 20, 20, -53, -81, -1, -7, 75, -5, -21, -65, 12, -52, 22, -50, -12, 49, 54, 76, -81,
   10, 45, -41, -59, 18, -19, 25, 14, -31, -53, -5, 12, 31, 84, -23, 2, 7, 2, 10, -32,
   39, -2, -12, 1, -9, 0, -10, -11, 9, 15, -8, -2, 2, -1, 10, 14, -5, -40, 19, -7,
   -7, 26, -4, 2, 1, -27, 35, 32, 21, -31, 26, 43, -9, 4, -32, 40, -62, -52, 36, 22,
   38, 22, 36, -96, 6, -10, -23, -49, 15, -33, -18, -3, 0, 41, 21, -19, 21, 23, -39, -23,
   -6, 6, 47, 56, 4, 74, 0, -98, 29, -47, -14, -36, 21, -22, 22, 16, 13, 12, 16, -5,
   13, 17, -13, -15, 1, -34, -26, 26, 12, 32, 27, 13, -67, 27, 2, 8, 10, 18, 16, 20,
   -17, -17, 57, -64, 5, 14, 19, 31, -18, -44, -46, -16, 4, -25, 17, -126, -24, 39, 4, 8,
   55, -25, -34, 39, -16, 3, 9, 71, 72, -31, -55, 6, 10, -25, 32, -85, -21, 18, -8, 15,
   12, -27, -7, 1, -21, -2, -5, 48, -16, 18, 1, -22, -26, 16, 14, -31, 27, -6, -15, -21,
   4, -14, 18, -36,
};
static const fl_i8 fl_mlp_layer1_recur_weights[1728] = {
   20, 67, -99, 12, 41, -25, 49, -44, 35, 81, 110, 47, 34, -66, -14, 14, -60, 34, 29, -73,
   10, 41, 35, 89, 7, -35, 22, 7, 27, -20, -6, 56, 26, 66, 6, 33, -55, 53, 1, -21,
   14, 17, 68, 55, 59, 0, 18, -9, 5, -41, 6, -5, -114, -12, 29, 42, -23, 10, 81, -27,
   20, -53, -30, -62, 40, 95, 25, -4, 3, 18, -8, -15, -29, -82, 2, -57, -3, -61, -29, -29,
   49, 2, -55, 5, -69, -99, -49, -51, 6, -25, 12, 89, 44, -33, 5, 41, 1, 23, -37, -37,
   -28, -48, 3, 4, -41, -30, -57, -35, -39, -1, -13, -56, -5, 50, 49, 41, -4, -4, 33, -22,
   -1, 33, 34, 18, 40, -42, 12, 1, -6, -2, 18, 17, 39, 44, 11, 65, -60, -45, 10, 91,
   21, 9, -62, -11, 8, 69, 37, 24, -30, 21, 26, -27, 1, -28, 24, 66, -8, 6, -71, 34,
   24, 44, 58, -78, -19, 57, 17, -60, 1, 12, -3, -1, -40, 22, 11, -5, 25, 12, 1, 72,
   79, 7, -50, 23, 18, 13, 21, -11, -20, 5, 77, -94, 24, 15, 57, -51, 3, 36, 53, -1,
   4, 14, 30, -31, 22, 40, 32, -11, -34, -36, -59, 58, 25, 21, -54, -23, 40, 46, 18, 0,
   12, 54, -96, -99, -59, 5, 119, -38, 50, 55, 12, -16, 67, 0, 34, 35, 39, 35, -1, 69,
   24, 27, -30, -35, -4, -70, 2, -44, -7, -6, 19, -9, 60, 44, -21, -10, 37, 43, -16, -3,
   30, -15, -65, 31, -55, 18, -98, 76, 64, 25, 24, -18, -7, -68, -10, 38, 27, -60, 36, 33,
   16, 30, 34, -39, -37, 31, 12, 53, -54, 14, -26, -49, -128, -13, -5, -22, -11, -85, 55, -8,
   -51, -11, -33, -10, -31, -76, -41, 23, 44, -40, -54, -127, -101, 19, -23, -15, 15, 27, 58, -60,
   8, 14, -33, 1, 48, -9, -11, -123, 3, 53, 23, 4, -28, 22, 2, -29, -67, 36, 12, 7,
   55, -21, 88, 20, -1, -21, -17, 3, 41, 32, -10, -14, -5, -57, 67, 57, 21, 23, -2, -27,
   -73, -24, 120, 21, 18, -35, 42, -7, 3, -45, -25, 76, -34, 50, 11, -54, -91, 3, -113, -20,
   -5, 47, 15, -47, 17, 27, -3, -26, -7, 10, 7, 74, -40, 64, -7, -5, -24, -49, -24, -3,
   -10, 27, -17, -8, -3, 14, -27, 33, 13, 39, 28, -7, -38, 29, 16, 44, 19, 55, -3, 9,
   -13, -57, 43, 43, 31, 0, -93, -17, 19, -56, 4, -12, -25, 37, -85, -13, -118, 33, -17, 56,
   71, -80, -4, 6, -11, -18, 47, -52, 25, 9, 48, -107, 1, 21, 20, -3, 10, -16, -4, 24,
   17, 31, -61, -18, -50, 24, -10, 12, 71, 26, 11, -3, 4, 1, 0, -7, -40, 18, 38, -34,
   38, 17, 8, -34, 2, 21, 123, -32, -26, 43, 14, -34, -1, -9, 37, -16, 6, -17, -62, 68,
   22, 17, 11, -75, 33, -80, 62, -9, -75, 76, 36, -41, -8, -40, -11, -71, 40, -39, 62, -49,
   -81, 16, -9, -52, 52, 61, 17, -103, -27, -10, -8, -54, -57, 21, 23, -16, -52, 36, 18, 10,
   -5, 8, 15, -29, 5, -19, -37, 8, -53, 6, 19, -37, 38, -17, 48, 10, 0, 81, 46, 70,
   -29, 101, 11, 44, -44, -3, 24, 11, 3, 14, -9, 11, 14, -45, 13, 46, -3, -57, 68, 44,
   63, 98, 25, -28, -23, 15, 32, -10, 53, -6, -2, -9, -6, 16, -107, -11, -11, -28, 59, 57,
   -22, 38, 42, 83, 27, 5, 29, -30, 12, -21, -13, 31, 38, -21, 58, -10, -10, -15, -2, -5,
   11, 12, -73, -28, -38, 22, 2, -25, 73, -52, -12, -55, 32, -63, 21, 51, 33, 52, -26, 55,
   -26, -26, 57, -32, -4, -52, -61, 21, -33, -91, -51, 69, -90, -53, -38, -44, 12, -76, -20, 77,
   -45, -7, 86, 43, -109, -33, -105, -40, -121, -10, 0, -72, 45, -51, -75, -49, -38, -1, -62, 18,
   -1, 30, -44, -14, -10, -67, 40, -10, -34, 46, -64, -32, 29, -13, 33, 3, -32, -5, 28, -27,
   -25, 93, 24, 68, -40, 57, 23, -3, -21, -58, 17, -39, -17, -22, -89, 11, 18, -46, 27, 24,
   46, 127, 61, 87, 31, 127, -36, 47, -23, 47, 127, -24, 110, 122, 30, 100, 0, 96, -12, 6,
   50, 44, -13, 73, 4, 55, -11, -15, 49, 42, -6, 20, -35, 58, 18, 38, 42, 72, 19, -21,
   11, 9, -37, 7, 29, 31, 16, -17, 13, -50, 19, 5, -23, 51, -16, -5, 4, -24, 76, 10,
   -53, -28, -7, -65, 74, 40, -16, -29, 32, -16, -49, -35, -3, 59, -96, -50, -43, -43, -61, -15,
   -8, -36, -34, -33, -14, 11, -3, -39, 4, -114, -123, -11, -49, -21, 14, -56, 1, 43, -63, 26,
   40, 18, -10, -26, -14, -15, -35, -35, -11, 32, -44, -67, 2, 22, 7, 3, -9, -30, -51, -28,
   28, 6, -22, 16, 34, -25, -52, -54, -8, -6, 5, 8, 20, -16, -17, -44, 27, 3, 31, -5,
   -48, -1, -3, 116, 11, 71, -31, -47, 109, 50, -22, -12, -57, 32, 66, 8, -25, -93, -54, -10,
   19, -76, -34, 97, 48, -36, -18, -30, -39, -26, -12, 28, 14, 12, -12, -31, 38, 2, 10, 4,
   -40, 20, 16, -61, 2, 64, 39, 5, 15, 33, 40, -61, -49, 93, -10, 33, 28, -11, -27, -18,
   39, -62, -6, -6, 62, 11, -8, 38, -67, 12, 27, 39, -27, 123, -18, -6, -65, 83, -64, 20,
   19, -11, 33, 24, 17, 56, 78, 7, -15, 54, -101, -9, 115, -96, 50, 51, 35, 34, 27, 37,
   -40, -11, 8, -36, 42, -45, 2, -23, 0, 67, -8, -9, -13, 50, -14, -27, 4, 0, -8, -14,
   30, -9, 29, 15, 9, -38, 37, -8, 50, -46, 54, 41, -11, -8, -11, -26, 39, 45, 14, -26,
   -17, -27, 69, 38, 39, 98, 66, 0, 42, 123, -101, -19, -83, 117, -32, 56, 10, 12, -88, 79,
   -53, 56, 63, 95, -62, 9, 36, -13, -79, -16, 37, -46, 35, -34, 14, 17, -54, 5, 21, -7,
   7, 63, 56, 15, 27, -76, -25, 4, -26, -63, 28, -67, -52, 43, -47, -70, 40, -12, 40, -66,
   -37, 0, 35, 37, -53, 4, -17, -51, 11, 21, 14, -34, -4, 24, -42, 29, 22, 7, 28, 12,
   37, 39, -39, -19, 65, -60, -50, -2, 1, 82, 39, 19, -23, -43, -22, -67, -35, -34, 32, 102,
   81, 127, 36, 67, -45, 1, -67, -52, -4, 35, 20, 28, 71, 86, -35, -9, -83, -34, 12, 9,
   -23, 2, 14, 28, -23, 7, -25, 45, 7, 17, -37, 0, -19, 31, 26, 40, -27, -16, 17, 5,
   -21, 23, 24, 96, -55, 52, -19, -14, -6, 1, 50, -34, 86, -53, 38, 2, -52, -36, -13, 60,
   -85, -120, 32, 7, -12, 22, 70, -7, -94, 38, -76, -31, -20, 15, -28, 7, 6, 40, 53, 88,
   3, 38, 18, -8, -22, -23, 51, 37, -9, 13, -32, 25, -21, 27, 31, 20, 18, -9, -13, 1,
   21, -24, -13, 39, 15, -11, -29, -36, 18, 15, 8, 27, 21, -94, -1, -22, 49, 66, -1, 6,
   -3, -40, -18, 6, 28, 12, 33, -59, 62, 60, -48, 90, -1, 108, 9, 18, -2, 27, 77, -65,
   82, -48, -38, -19, -11, 127, 50, 66, 18, -13, -22, 60, -38, 40, -14, -26, -13, 38, 67, 57,
   30, 33, 26, 36, 38, -17, 27, -28, 20, 12, -64, 18, 5, -33, -27, 13, -26, 32, 35, -5,
   -48, -14, 92, 43, -47, -14, 40, 11, 51, 66, 22, -63, -16, -61, 4, -28, 27, 20, -33, -30,
   -21, -29, -53, 31, -40, 24, 43, -4, -19, 21, 67, 20, 100, -16, -93, 78, -6, -18, -52, -37,
   -9, 66, -31, -8, 26, 18, 4, 24, -22, 17, -2, -13, 27, 0, 8, -18, -25, 5, -21, -24,
   -7, 18, -93, 21, 7, 2, -75, 69, 50, -5, -15, -17, 60, -42, 55, 1, -4, 3, 10, 46,
   16, -13, 45, -7, -10, -44, -108, 49, 2, -15, -64, -12, -72, 32, -38, -45, 10, -54, 13, -13,
   -27, -36, -64, 58, -62, -101, 88, -86, -71, -39, -9, -128, 32, 15, -4, 54, -16, -39, -26, -36,
   46, 48, -64, -10, 19, 30, -13, 34, -8, 50, 60, -22, -6, -11, -30, 5, 50, 32, 56, 0,
   25, 6, 68, 11, -29, 45, -9, -12, 4, 1, 18, -49, 0, -38, -19, 90, 29, 35, 51, 8,
   -48, 96, -1, -12, -9, -32, -63, -65, -7, 38, 89, 28, -85, -28, -23, -25, -128, 56, 79, -36,
   99, -6, -37, 7, -13, -69, -46, -29, 25, 64, -21, 17, 1, 42, -66, 1, 80, 26, -32, 21,
   15, 15, 6, 6, -10, 15, 127, 5, 38, 27, 87, -57, -25, 11, 72, -21, -5, 11, -13, -66,
   78, 36, -3, 41, -21, 8, -33, 23, 73, 28, 57, -25, -5, 4, -22, -47, 15, 4, -57, -72,
   33, 1, 18, 2, 53, -71, -99, -21, -3, -111, 108, 71, -14, 82, 25, 61, -48, 5, 9, -51,
   -20, -25, -3, 14, -33, 14, -3, -34, 22, 12, -19, -38, -16, 2, 21, 16, 26, -31, 75, 44,
   -31, 16, 26, 66, 17, -9, -22, -22, 22, -44, 22, 27, 2, 58, -14, 10, -73, -42, 55, -25,
   -61, 72, -1, 30, -58, -25, 63, 26, -48, -40, 26, -30, 60, 8, -17, -1, -18, -20, 43, -20,
   -4, -28, 127, -106, 29, 70, 64, -27, 39, -33, -5, -88, -40, -52, 26, 44, -17, 23, 2, -49,
   22, -9, -8, 86, 49, -43, -60, 1, 10, 45, 36, -53, -4, 33, 38, 48, -72, 1, 19, 21,
   -65, 4, -5, -62, 27, -25, 17, -6, 6, -45, -39, -46, 4, 26, 127, -9, 18, -33, -18, -3,
   33, 2, -5, 15, -26, -22, -117, -63, -17, -59, 61, -74, 7, -47, -58, -128, -67, 15, -16, -128,
   12, 2, 20, 9, -48, -40, 43, 3, -40, -16, -38, -6, -22, -28, -16, -59, -22, 6, -5, 11,
   -12, -66, -40, 27, -62, -44, -19, 38, -3, 39, -8, 40, -24, 13, 21, 50, -60, -22, 53, -29,
   -6, 1, 22, -59, 0, 17, -39, 115,
};
static const fl_i8 fl_mlp_layer1_bias[72] = {
   -42, 20, 16, 0, 105, 60, 1, -97, 24, 60, 18, 13, 62, 25, 127, 34, 79, 55, 118, 127,
   95, 31, -4, 87, 21, 12, 2, -14, 18, 23, 8, 17, -1, -8, 5, 4, 24, 37, 21, 13,
   36, 13, 17, 18, 37, 30, 33, 1, 8, -16, -11, -5, -31, -3, -5, 0, 6, 3, 58, -7,
   -1, -16, 5, -13, 16, 10, -2, -14, 11, -4, 3, -11,
};
static const fl_i8 fl_mlp_layer2_weights[48] = {
   -113, -88, 31, -128, -126, -61, 85, -35, 118, -128, -61, 127, -128, -17, -128, 127, 104, -9, -128, 33,
   45, 127, 5, 83, 84, -128, -85, -128, -45, 48, -53, -128, 46, 127, -17, 125, 117, -41, -117, -91,
   -127, -68, -1, -89, -80, 32, 106, 7,
};
static const fl_i8 fl_mlp_layer2_bias[2] = {
   14, 117,
};

#define NB_FRAMES 8
#define NB_TBANDS 18
#define ANALYSIS_BUF_SIZE 720
#define ANALYSIS_COUNT_MAX 10000
#define DETECT_SIZE 100
#define MAX_NEURONS 32
#define NB_TONAL_SKIP_BANDS 9

typedef struct {
   int Fs;
   /* reset from here */
   float angle[240], d_angle[240], d2_angle[240];
   float inmem[ANALYSIS_BUF_SIZE];
   int mem_fill;
   float prev_band_tonality[NB_TBANDS], prev_tonality;
   int prev_bandwidth;
   float E[NB_FRAMES][NB_TBANDS], logE[NB_FRAMES][NB_TBANDS];
   float lowE[NB_TBANDS], highE[NB_TBANDS], meanE[NB_TBANDS+1];
   float mem[32], cmean[8], std[9], Etracker, lowECount;
   int E_count, count, analysis_offset, write_pos, read_pos, read_subframe;
   float hp_ener_accum;
   int initialized;
   float rnn_state[MAX_NEURONS];
   float downmix_state[3];
   fl_analysis_info info[DETECT_SIZE];
} fl_tonality_state;

static const float fl_dct_table[128] = {
   0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f,
   0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f,
   0.351851f, 0.338330f, 0.311806f, 0.273300f, 0.224292f, 0.166664f, 0.102631f, 0.034654f,
   -0.034654f, -0.102631f, -0.166664f, -0.224292f, -0.273300f, -0.311806f, -0.338330f, -0.351851f,
   0.346760f, 0.293969f, 0.196424f, 0.068975f, -0.068975f, -0.196424f, -0.293969f, -0.346760f,
   -0.346760f, -0.293969f, -0.196424f, -0.068975f, 0.068975f, 0.196424f, 0.293969f, 0.346760f,
   0.338330f, 0.224292f, 0.034654f, -0.166664f, -0.311806f, -0.351851f, -0.273300f, -0.102631f,
   0.102631f, 0.273300f, 0.351851f, 0.311806f, 0.166664f, -0.034654f, -0.224292f, -0.338330f,
   0.326641f, 0.135299f, -0.135299f, -0.326641f, -0.326641f, -0.135299f, 0.135299f, 0.326641f,
   0.326641f, 0.135299f, -0.135299f, -0.326641f, -0.326641f, -0.135299f, 0.135299f, 0.326641f,
   0.311806f, 0.034654f, -0.273300f, -0.338330f, -0.102631f, 0.224292f, 0.351851f, 0.166664f,
   -0.166664f, -0.351851f, -0.224292f, 0.102631f, 0.338330f, 0.273300f, -0.034654f, -0.311806f,
   0.293969f, -0.068975f, -0.346760f, -0.196424f, 0.196424f, 0.346760f, 0.068975f, -0.293969f,
   -0.293969f, 0.068975f, 0.346760f, 0.196424f, -0.196424f, -0.346760f, -0.068975f, 0.293969f,
   0.273300f, -0.166664f, -0.338330f, 0.034654f, 0.351851f, 0.102631f, -0.311806f, -0.224292f,
   0.224292f, 0.311806f, -0.102631f, -0.351851f, -0.034654f, 0.338330f, 0.166664f, -0.273300f};
static const float fl_analysis_window[240] = {
   0.000043f, 0.000171f, 0.000385f, 0.000685f, 0.001071f, 0.001541f, 0.002098f, 0.002739f,
   0.003466f, 0.004278f, 0.005174f, 0.006156f, 0.007222f, 0.008373f, 0.009607f, 0.010926f,
   0.012329f, 0.013815f, 0.015385f, 0.017037f, 0.018772f, 0.020590f, 0.022490f, 0.024472f,
   0.026535f, 0.028679f, 0.030904f, 0.033210f, 0.035595f, 0.038060f, 0.040604f, 0.043227f,
   0.045928f, 0.048707f, 0.051564f, 0.054497f, 0.057506f, 0.060591f, 0.063752f, 0.066987f,
   0.070297f, 0.073680f, 0.077136f, 0.080665f, 0.084265f, 0.087937f, 0.091679f, 0.095492f,
   0.099373f, 0.103323f, 0.107342f, 0.111427f, 0.115579f, 0.119797f, 0.124080f, 0.128428f,
   0.132839f, 0.137313f, 0.141849f, 0.146447f, 0.151105f, 0.155823f, 0.160600f, 0.165435f,
   0.170327f, 0.175276f, 0.180280f, 0.185340f, 0.190453f, 0.195619f, 0.200838f, 0.206107f,
   0.211427f, 0.216797f, 0.222215f, 0.227680f, 0.233193f, 0.238751f, 0.244353f, 0.250000f,
   0.255689f, 0.261421f, 0.267193f, 0.273005f, 0.278856f, 0.284744f, 0.290670f, 0.296632f,
   0.302628f, 0.308658f, 0.314721f, 0.320816f, 0.326941f, 0.333097f, 0.339280f, 0.345492f,
   0.351729f, 0.357992f, 0.364280f, 0.370590f, 0.376923f, 0.383277f, 0.389651f, 0.396044f,
   0.402455f, 0.408882f, 0.415325f, 0.421783f, 0.428254f, 0.434737f, 0.441231f, 0.447736f,
   0.454249f, 0.460770f, 0.467298f, 0.473832f, 0.480370f, 0.486912f, 0.493455f, 0.500000f,
   0.506545f, 0.513088f, 0.519630f, 0.526168f, 0.532702f, 0.539230f, 0.545751f, 0.552264f,
   0.558769f, 0.565263f, 0.571746f, 0.578217f, 0.584675f, 0.591118f, 0.597545f, 0.603956f,
   0.610349f, 0.616723f, 0.623077f, 0.629410f, 0.635720f, 0.642008f, 0.648271f, 0.654508f,
   0.660720f, 0.666903f, 0.673059f, 0.679184f, 0.685279f, 0.691342f, 0.697372f, 0.703368f,
   0.709330f, 0.715256f, 0.721144f, 0.726995f, 0.732807f, 0.738579f, 0.744311f, 0.750000f,
   0.755647f, 0.761249f, 0.766807f, 0.772320f, 0.777785f, 0.783203f, 0.788573f, 0.793893f,
   0.799162f, 0.804381f, 0.809547f, 0.814660f, 0.819720f, 0.824724f, 0.829673f, 0.834565f,
   0.839400f, 0.844177f, 0.848895f, 0.853553f, 0.858151f, 0.862687f, 0.867161f, 0.871572f,
   0.875920f, 0.880203f, 0.884421f, 0.888573f, 0.892658f, 0.896677f, 0.900627f, 0.904508f,
   0.908321f, 0.912063f, 0.915735f, 0.919335f, 0.922864f, 0.926320f, 0.929703f, 0.933013f,
   0.936248f, 0.939409f, 0.942494f, 0.945503f, 0.948436f, 0.951293f, 0.954072f, 0.956773f,
   0.959396f, 0.961940f, 0.964405f, 0.966790f, 0.969096f, 0.971321f, 0.973465f, 0.975528f,
   0.977510f, 0.979410f, 0.981228f, 0.982963f, 0.984615f, 0.986185f, 0.987671f, 0.989074f,
   0.990393f, 0.991627f, 0.992778f, 0.993844f, 0.994826f, 0.995722f, 0.996534f, 0.997261f,
   0.997902f, 0.998459f, 0.998929f, 0.999315f, 0.999615f, 0.999829f, 0.999957f, 1.000000f};
static const int fl_tbands[NB_TBANDS+1] = {4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 136, 160, 192, 240};
static const float fl_std_feature_bias[9] = {5.684947f, 3.475288f, 1.770634f, 1.599784f, 3.773215f, 2.163313f,
   1.260756f, 1.116868f, 1.918795f};

/* MLP (int8 weights, scale 1/128). */
static xf fl_tansig_approx(float x) {
   xf X2 = XF(x)*x, num, den;
   num = (XF(0.60863042f)*X2+96.39235687f)*X2+952.52801514f;
   den = (XF(11.88600922f)*X2+413.36801147f)*X2+952.72399902f;
   num = (num*x)/den;
   return 1.f < num ? XF(1.f) : (-1.f > num ? XF(-1.f) : num);
}
static void fl_gemm_accum(float *out, const fl_i8 *w, int rows, int cols, int col_stride, const float *x) {
   int i, j;
   for (i = 0; i < rows; i++)
      for (j = 0; j < cols; j++) out[i] = (float)(XF(w[j*col_stride+i])*x[j]+out[i]);
}
static void fl_mlp_dense(const fl_i8 *bias, const fl_i8 *weights, int M, int N, int sigmoid, float *output,
      const float *input) {
   int i;
   for (i = 0; i < N; i++) output[i] = bias[i];
   fl_gemm_accum(output, weights, N, M, N, input);
   for (i = 0; i < N; i++) output[i] *= 1.f/128;
   for (i = 0; i < N; i++)
      output[i] = sigmoid ? (float)(fl_tansig_approx(.5f*output[i])*.5f+.5f) : (float)fl_tansig_approx(output[i]);
}
static void fl_mlp_gru(float *state, const float *input) {
   const int M = 32, N = 24, stride = 3*24;
   const fl_i8 *bias = fl_mlp_layer1_bias, *iw = fl_mlp_layer1_weights, *rw = fl_mlp_layer1_recur_weights;
   int i;
   float tmp[MAX_NEURONS], z[MAX_NEURONS], r[MAX_NEURONS], h[MAX_NEURONS];
   for (i = 0; i < N; i++) z[i] = bias[i];
   fl_gemm_accum(z, iw, N, M, stride, input);
   fl_gemm_accum(z, rw, N, N, stride, state);
   for (i = 0; i < N; i++) z[i] = (float)(fl_tansig_approx(.5f*((1.f/128)*z[i]))*.5f+.5f);
   for (i = 0; i < N; i++) r[i] = bias[N+i];
   fl_gemm_accum(r, &iw[N], N, M, stride, input);
   fl_gemm_accum(r, &rw[N], N, N, stride, state);
   for (i = 0; i < N; i++) r[i] = (float)(fl_tansig_approx(.5f*((1.f/128)*r[i]))*.5f+.5f);
   for (i = 0; i < N; i++) h[i] = bias[2*N+i];
   for (i = 0; i < N; i++) tmp[i] = state[i]*r[i];
   fl_gemm_accum(h, &iw[2*N], N, M, stride, input);
   fl_gemm_accum(h, &rw[2*N], N, N, stride, tmp);
   for (i = 0; i < N; i++) {
      float zs = z[i]*state[i], omz = 1-z[i];
      h[i] = (float)(fl_tansig_approx((1.f/128)*h[i])*omz+zs);
   }
   for (i = 0; i < N; i++) state[i] = h[i];
}

static xf fl_fast_atan2f(float y, float x) {
   const float cA = 0.43157974f, cB = 0.67848403f, cC = 0.08595542f, cE = (float)3.141592653589793/2;
   xf x2 = XF(x)*x, y2 = XF(y)*y;
   if (x2+y2 < 1e-18f) return 0;
   if (x2 < y2) {
      xf den = (y2+cB*x2)*(y2+cC*x2);
      return ((XF(-x)*y)*(y2+cA*x2))/den+(y < 0 ? -cE : cE);
   } else {
      xf den = (x2+cB*y2)*(x2+cC*y2), xy = XF(x)*y;
      return ((xy*(x2+cA*y2))/den+(y < 0 ? -cE : cE))-(xy < 0 ? -cE : cE);
   }
}

static xf fl_resampler_down2_hp(float *S, float *out, const float *in, int inLen) {
   int k, len2 = inLen/2;
   xf hp_ener = 0;
   for (k = 0; k < len2; k++) {
      float in32 = in[2*k], in32b;
      xf Y, X, out32, out32_hp;
      Y = XF(in32)-S[0];
      X = XF(0.6074371f)*Y;
      out32 = S[0]+X;
      S[0] = (float)(in32+X);
      out32_hp = out32;
      in32b = in[2*k+1];
      Y = XF(in32b)-S[1];
      X = XF(0.15063f)*Y;
      out32 = out32+S[1];
      out32 = out32+X;
      S[1] = (float)(in32b+X);
      Y = XF(-in32b)-S[2];
      X = XF(0.15063f)*Y;
      out32_hp = out32_hp+S[2];
      out32_hp = out32_hp+X;
      S[2] = (float)(X-in32b);
      hp_ener += out32_hp*out32_hp;
      out[k] = (float)(out32*.5f);
   }
   return hp_ener;
}

/* downmix_float + down2_hp for 48 kHz input. */
static xf fl_downmix_and_resample(const float *x, float *y, float S[3], int subframe, int offset, int c1, int c2, int C) {
   float tmp[2*ANALYSIS_BUF_SIZE];
   int j;
   if (subframe == 0) return 0;
   subframe *= 2;
   offset *= 2;
   for (j = 0; j < subframe; j++) tmp[j] = x[(j+offset)*C+c1]*32768.f;
   if (c2 > -1)
      for (j = 0; j < subframe; j++) tmp[j] = tmp[j]+x[(j+offset)*C+c2]*32768.f;
   for (j = 0; j < subframe; j++) {
      if (tmp[j] < -65536.f) tmp[j] = -65536.f;
      if (tmp[j] > 65536.f) tmp[j] = 65536.f;
      if (tmp[j] != tmp[j]) tmp[j] = 0;
   }
   if (c2 > -1)
      for (j = 0; j < subframe; j++) tmp[j] = .5f*tmp[j];
   return fl_resampler_down2_hp(S, y, tmp, subframe)*XF(1.f/32768/32768);
}

static void fl_tonality_reset(fl_tonality_state *t) {
   memset(&t->angle, 0, sizeof(*t)-offsetof(fl_tonality_state, angle));
}

static void fl_tonality_analysis(fl_tonality_state *tonal, const float *x, int len, int offset, int c1, int c2,
      int C, int lsb_depth) {
   int i, b, N = 480, N2 = 240, remaining, bandwidth = 0, is_masked[NB_TBANDS+1];
   float *A = tonal->angle, *dA = tonal->d_angle, *d2A = tonal->d2_angle;
   float tonality[240], noisiness[240], tonality2[240], band_tonality[NB_TBANDS], logE[NB_TBANDS], BFCC[8];
   float features[25], midE[8], band_log2[NB_TBANDS+1], leakage_from[NB_TBANDS+1], leakage_to[NB_TBANDS+1];
   float layer_out[MAX_NEURONS], frame_probs[2], alpha, alphaE, alphaE2, hp_ener, spec_variability;
   float frame_noisiness_f = 0, frame_loudness = 0, frame_stationarity = 0, relativeE = 0, slope = 0;
   float frame_tonality = 0, max_frame_tonality = 0, noise_floor, one_m_alphaE2;
   fl_cpx in[480], out[480];
   fl_analysis_info *info;
   if (!tonal->initialized) {
      tonal->mem_fill = 240;
      tonal->initialized = 1;
   }
   alpha = (float)(1.0L/IMIN(10, 1+tonal->count));
   alphaE = (float)(1.0L/IMIN(25, 1+tonal->count));
   alphaE2 = (float)(1.0L/IMIN(100, 1+tonal->count));
   if (tonal->count <= 1) alphaE2 = 1;
   len /= 2;
   offset /= 2;
   hp_ener = (float)(tonal->hp_ener_accum+fl_downmix_and_resample(x, &tonal->inmem[tonal->mem_fill],
         tonal->downmix_state, IMIN(len, ANALYSIS_BUF_SIZE-tonal->mem_fill), offset, c1, c2, C));
   tonal->hp_ener_accum = hp_ener;
   if (tonal->mem_fill+len < ANALYSIS_BUF_SIZE) {
      tonal->mem_fill += len;
      return;
   }
   info = &tonal->info[tonal->write_pos++];
   if (tonal->write_pos >= DETECT_SIZE) tonal->write_pos -= DETECT_SIZE;
   {
      int is_silence = fl_e_maxabs(tonal->inmem, ANALYSIS_BUF_SIZE) <= XF(1)/(1<<lsb_depth);
      for (i = 0; i < N2; i++) {
         float w = fl_analysis_window[i];
         in[i].r = w*tonal->inmem[i];
         in[i].i = w*tonal->inmem[N2+i];
         in[N-i-1].r = w*tonal->inmem[N-i-1];
         in[N-i-1].i = w*tonal->inmem[N+N2-i-1];
      }
      memmove(tonal->inmem, tonal->inmem+ANALYSIS_BUF_SIZE-240, 240*sizeof(float));
      remaining = len-(ANALYSIS_BUF_SIZE-tonal->mem_fill);
      tonal->hp_ener_accum = (float)fl_downmix_and_resample(x, &tonal->inmem[240], tonal->downmix_state, remaining,
            offset+ANALYSIS_BUF_SIZE-tonal->mem_fill, c1, c2, C);
      tonal->mem_fill = 240+remaining;
      if (is_silence) {
         int prev_pos = tonal->write_pos-2;
         if (prev_pos < 0) prev_pos += DETECT_SIZE;
         memcpy(info, &tonal->info[prev_pos], sizeof(*info));
         return;
      }
   }
   fl_e_fft(&fl_fft_states[0], in, out);
   if (out[0].r != out[0].r) {
      info->valid = 0;
      return;
   }
   for (i = 1; i < N2; i++) {
      float X1r, X2r, X1i, X2i, angle_f, d_angle_f, d2_angle_f;
      xf angle, d_angle, angle2, d_angle2, d2_angle2, mod1, mod2, avg_mod, K = 62341.81640625f;
      X1r = out[i].r+out[N-i].r;
      X1i = out[i].i-out[N-i].i;
      X2r = out[i].i+out[N-i].i;
      X2i = out[N-i].r-out[i].r;
      angle = XF((float)(.5f/3.141592653589793))*fl_fast_atan2f(X1i, X1r);
      angle_f = (float)angle;
      d_angle = angle-A[i];
      d_angle_f = (float)d_angle;
      d2_angle_f = (float)(d_angle-dA[i]);
      angle2 = XF((float)(.5f/3.141592653589793))*fl_fast_atan2f(X2i, X2r);
      d_angle2 = angle2-angle_f;
      d2_angle2 = d_angle2-d_angle_f;
      mod1 = XF(d2_angle_f)-(int)lrintf(d2_angle_f);
      mod2 = d2_angle2-(int)lrintf((float)d2_angle2);
      noisiness[i] = (float)(fabsl(mod1)+fabsl(mod2));
      mod1 = mod1*mod1;
      mod1 = mod1*mod1;
      mod2 = mod2*mod2;
      mod2 = mod2*mod2;
      avg_mod = ((d2A[i]+mod1)+2*mod2)*.25f;
      tonality[i] = (float)(1.0L/(1+K*avg_mod)-.015f);
      tonality2[i] = (float)(1.0L/(1+K*mod2)-.015f);
      A[i] = (float)angle2;
      dA[i] = (float)d_angle2;
      d2A[i] = (float)mod2;
   }
   for (i = 2; i < N2-1; i++) {
      float tt = FL_MIN(tonality2[i], FL_MAX(tonality2[i-1], tonality2[i+1]));
      xf a = XF(tt)-.1f;
      tonality[i] = (float)((tonality[i] > a ? XF(tonality[i]) : a)*.9f);
   }
   info->activity = 0;
   if (!tonal->count)
      for (b = 0; b < NB_TBANDS; b++) {
         tonal->lowE[b] = 1e10;
         tonal->highE[b] = -1e10;
      }
   {
      xf E, X1r = XF(out[0].r)*2, X2r = XF(out[0].i)*2;
      E = X1r*X1r+X2r*X2r;
      for (i = 1; i < 4; i++)
         E += ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
      E = E*XF(1.f/32768/32768);
      band_log2[0] = (float)(XF((float)fl_m_log((double)(E+1e-10f)))*(.5f*1.442695f));
   }
   for (b = 0; b < NB_TBANDS; b++) {
      float E = 0, tE = 0, Eeps, L1 = 0, L2 = 0, arg;
      xf nE = 0, stationarity, bt, s, ft;
      for (i = fl_tbands[b]; i < fl_tbands[b+1]; i++) {
         xf binE = ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
         xf t = tonality[i];
         binE = binE*XF(1.f/32768/32768);
         E = (float)(E+binE);
         t = 0 > t ? 0 : t;
         tE = (float)(binE*t+tE);
         nE += (binE*2)*(XF(.5f)-noisiness[i]);
      }
      if (!(E < 1e9f) || E != E) {
         info->valid = 0;
         return;
      }
      tonal->E[tonal->E_count][b] = E;
      {
         xf e = XF(E)+1e-15f;
         Eeps = (float)e;
         frame_noisiness_f = (float)(frame_noisiness_f+nE/e);
      }
      arg = (float)(XF(E)+1e-10f);
      frame_loudness = (float)(frame_loudness+fl_m_sqrtf(arg));
      logE[b] = (float)fl_m_log((double)arg);
      band_log2[b+1] = (float)(XF(logE[b])*(.5f*1.442695f));
      tonal->logE[tonal->E_count][b] = logE[b];
      if (tonal->count == 0) tonal->highE[b] = tonal->lowE[b] = logE[b];
      {
         /* highE may stay in a register (unrounded) after the -0.01 update */
         xf Hx = tonal->highE[b], L = logE[b];
         if (tonal->highE[b] > XF(tonal->lowE[b])+7.5) {
            if (XF(tonal->highE[b])-L > L-tonal->lowE[b]) {
               Hx = XF(tonal->highE[b])-.01f;
               tonal->highE[b] = (float)Hx;
            } else tonal->lowE[b] = (float)(XF(tonal->lowE[b])+.01f);
         }
         if (L > Hx) {
            xf a = L-15;
            tonal->highE[b] = logE[b];
            tonal->lowE[b] = (float)(a > tonal->lowE[b] ? a : XF(tonal->lowE[b]));
            Hx = L;
         } else if (L < tonal->lowE[b]) {
            xf a = L+15;
            tonal->lowE[b] = logE[b];
            Hx = Hx > a ? a : Hx;
            tonal->highE[b] = (float)Hx;
         }
         relativeE = (float)((L-tonal->lowE[b])/((Hx-tonal->lowE[b])+1e-5f)+relativeE);
      }
      for (i = 0; i < NB_FRAMES-1; i++) {
         L1 = (float)(fl_m_sqrtf(tonal->E[i][b])+L1);
         L2 = L2+tonal->E[i][b];
      }
      L1 = (float)(fl_m_sqrtf(tonal->E[i][b])+L1);
      /* the last L2 term stays in a register */
      s = (float)fl_m_sqrt((double)((XF(L2)+tonal->E[i][b])*8.f+1e-15));
      stationarity = XF(L1)/s;
      stationarity = .99f < stationarity ? XF(.99f) : stationarity;
      stationarity = stationarity*stationarity;
      stationarity = stationarity*stationarity;
      frame_stationarity = (float)(frame_stationarity+stationarity);
      {
         xf a = XF(tE)/Eeps, c = stationarity*tonal->prev_band_tonality[b];
         bt = a > c ? a : c;
      }
      band_tonality[b] = (float)bt;
      ft = frame_tonality+bt;
      if (b >= NB_TBANDS-NB_TONAL_SKIP_BANDS) ft = ft-band_tonality[b-NB_TBANDS+NB_TONAL_SKIP_BANDS];
      frame_tonality = (float)ft;
      {
         xf m = (XF(.03f)*(b-NB_TBANDS)+1)*frame_tonality;
         max_frame_tonality = (float)(max_frame_tonality > m ? XF(max_frame_tonality) : m);
      }
      slope = (float)(bt*(b-8)+slope);
      tonal->prev_band_tonality[b] = (float)bt;
   }
   leakage_from[0] = band_log2[0];
   leakage_to[0] = band_log2[0]-2.5f;
   for (b = 1; b < NB_TBANDS+1; b++) {
      xf leak_slope = XF(fl_tbands[b]-fl_tbands[b-1])*2*.25f, a, c;
      a = leakage_from[b-1]+leak_slope;
      leakage_from[b] = (float)(a < band_log2[b] ? a : XF(band_log2[b]));
      a = leakage_to[b-1]-leak_slope;
      c = XF(band_log2[b])-2.5f;
      leakage_to[b] = (float)(a > c ? a : c);
   }
   for (b = NB_TBANDS-2; b >= 0; b--) {
      xf leak_slope = XF(fl_tbands[b+1]-fl_tbands[b])*2*.25f, a;
      a = leakage_from[b+1]+leak_slope;
      leakage_from[b] = (float)(a < leakage_from[b] ? a : XF(leakage_from[b]));
      a = leakage_to[b+1]-leak_slope;
      leakage_to[b] = (float)(a > leakage_to[b] ? a : XF(leakage_to[b]));
   }
   for (b = 0; b < NB_TBANDS+1; b++) {
      xf a = XF(leakage_to[b])-band_log2[b], c = XF(band_log2[b])-(XF(2.5f)+leakage_from[b]);
      xf boost = (0 > a ? 0 : a)+(0 > c ? 0 : c);
      info->leak_boost[b] = (unsigned char)IMIN(255, (int)floor((double)(boost*64.f+.5f)));
   }
   for (; b < LEAK_BANDS; b++) info->leak_boost[b] = 0;
   {
      xf sv = 0;
      for (i = 0; i < NB_FRAMES; i++) {
         int j;
         xf mindist = 1e15f;
         for (j = 0; j < NB_FRAMES; j++) {
            int k;
            xf dist = 0;
            for (k = 0; k < NB_TBANDS; k++) {
               xf t = XF(tonal->logE[i][k])-tonal->logE[j][k];
               dist += t*t;
            }
            if (j != i) mindist = mindist < dist ? mindist : dist;
         }
         sv += mindist;
      }
      spec_variability = (float)fl_m_sqrtf((float)((sv*.125f)/18.f));
   }
   {
      xf nf = XF(5.7e-4f)/(1<<IMAX(0, lsb_depth-8)), maxE = 0, below = 0, above = 0, bmask = 0;
      noise_floor = (float)(nf*nf);
      one_m_alphaE2 = 1-alphaE2;
      for (b = 0; b < NB_TBANDS; b++) {
         int band_start = fl_tbands[b], band_end = fl_tbands[b+1];
         xf E = 0, M, Em;
         for (i = band_start; i < band_end; i++)
            E += ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
         E = E*XF(1.f/32768/32768);
         maxE = maxE > E ? maxE : E;
         if (band_start < 64) below += E;
         else above += E;
         M = XF(one_m_alphaE2)*tonal->meanE[b];
         M = M > E ? M : E;
         tonal->meanE[b] = (float)M;
         Em = E > M ? E : M;
         if (E*1e9f > maxE && (Em > (XF(noise_floor)*3)*(band_end-band_start) || E > XF(noise_floor)*(band_end-band_start)))
            bandwidth = b+1;
         is_masked[b] = E < (tonal->prev_bandwidth >= b+1 ? XF(.01f) : XF(.05f))*bmask;
         bmask = XF(.05f)*bmask;
         bmask = bmask > E ? bmask : E;
      }
      {
         xf E = XF(hp_ener)*(1.f/(60*60)), M, Em, nr = tonal->prev_bandwidth == 20 ? 10.f : 30.f;
         above += E;
         M = XF(one_m_alphaE2)*tonal->meanE[b];
         M = M > E ? M : E;
         tonal->meanE[b] = (float)M;
         Em = E > M ? E : M;
         if (Em > ((XF(3)*nr)*noise_floor)*160 || E > (nr*noise_floor)*160) bandwidth = 20;
         is_masked[b] = E < (tonal->prev_bandwidth == 20 ? XF(.01f) : XF(.05f))*bmask;
      }
      if (above > below) info->max_pitch_ratio = (float)(below/above);
      else info->max_pitch_ratio = 1;
   }
   if (bandwidth == 20 && is_masked[NB_TBANDS]) bandwidth -= 2;
   else if (bandwidth > 0 && bandwidth <= NB_TBANDS && is_masked[bandwidth-1]) bandwidth--;
   if (tonal->count <= 2) bandwidth = 20;
   {
      xf fl = XF((float)fl_m_log10((double)frame_loudness))*20, et = XF(tonal->Etracker)-.003f, lc;
      et = et > fl ? et : fl;
      tonal->Etracker = (float)et;
      lc = (1-XF(alphaE))*tonal->lowECount;
      if (fl < et-30) lc = lc+alphaE;
      tonal->lowECount = (float)lc;
   }
   for (i = 0; i < 8; i++) {
      xf sum = 0;
      for (b = 0; b < 16; b++) sum += XF(fl_dct_table[i*16+b])*logE[b];
      BFCC[i] = (float)sum;
   }
   for (i = 0; i < 8; i++) {
      xf sum = 0;
      for (b = 0; b < 16; b++) sum += (XF(fl_dct_table[i*16+b])*.5f)*(XF(tonal->highE[b])+tonal->lowE[b]);
      midE[i] = (float)sum;
   }
   {
      float fs_f, act_f, fn_f, ft_f, sl_f;
      xf fs = XF(frame_stationarity)/NB_TBANDS, re = XF(relativeE)/NB_TBANDS, fn, ft;
      if (tonal->count < 10) re = .5f;
      fn = XF(frame_noisiness_f)/NB_TBANDS;
      fn_f = (float)fn;
      act_f = (float)(fn+(1-fn)*re);
      info->activity = act_f;
      ft = XF(max_frame_tonality)/(NB_TBANDS-NB_TONAL_SKIP_BANDS);
      {
         xf p = XF(.8f)*tonal->prev_tonality;
         ft = ft > p ? ft : p;
      }
      ft_f = (float)ft;
      tonal->prev_tonality = ft_f;
      sl_f = slope*(1.f/64);
      info->tonality_slope = sl_f;
      tonal->E_count = (tonal->E_count+1)%NB_FRAMES;
      tonal->count = IMIN(tonal->count+1, ANALYSIS_COUNT_MAX);
      info->tonality = ft_f;
      fs_f = (float)fs;
      for (i = 0; i < 4; i++)
         features[i] = (float)((((XF(-0.12299f)*(XF(BFCC[i])+tonal->mem[i+24]))+XF(0.49195f)*(XF(tonal->mem[i])+tonal->mem[i+16]))
               +XF(0.69693f)*tonal->mem[i+8])-XF(1.4349f)*tonal->cmean[i]);
      for (i = 0; i < 4; i++) tonal->cmean[i] = (float)((1-XF(alpha))*tonal->cmean[i]+XF(alpha)*BFCC[i]);
      for (i = 0; i < 4; i++)
         features[4+i] = (float)(XF(0.63246f)*(XF(BFCC[i])-tonal->mem[i+24])+XF(0.31623f)*(XF(tonal->mem[i])-tonal->mem[i+16]));
      for (i = 0; i < 3; i++)
         features[8+i] = (float)((XF(0.53452f)*(XF(BFCC[i])+tonal->mem[i+24])-XF(0.26726f)*(XF(tonal->mem[i])+tonal->mem[i+16]))
               -XF(0.53452f)*tonal->mem[i+8]);
      if (tonal->count > 5)
         for (i = 0; i < 9; i++)
            tonal->std[i] = (float)((1-XF(alpha))*tonal->std[i]+(XF(alpha)*features[i])*features[i]);
      for (i = 0; i < 4; i++) features[i] = BFCC[i]-midE[i];
      for (i = 0; i < 8; i++) {
         tonal->mem[i+24] = tonal->mem[i+16];
         tonal->mem[i+16] = tonal->mem[i+8];
         tonal->mem[i+8] = tonal->mem[i];
         tonal->mem[i] = BFCC[i];
      }
      for (i = 0; i < 9; i++) features[11+i] = (float)(fl_m_sqrtf(tonal->std[i])-fl_std_feature_bias[i]);
      features[18] = spec_variability-0.78f;
      features[20] = ft_f-0.154723f;
      features[21] = act_f-0.724643f;
      features[22] = fs_f-0.743717f;
      features[23] = sl_f+0.069216f;
      features[24] = tonal->lowECount-0.067930f;
      fl_mlp_dense(fl_mlp_layer0_bias, fl_mlp_layer0_weights, 25, 32, 0, layer_out, features);
      fl_mlp_gru(tonal->rnn_state, layer_out);
      fl_mlp_dense(fl_mlp_layer2_bias, fl_mlp_layer2_weights, 24, 2, 1, frame_probs, tonal->rnn_state);
      info->activity_probability = frame_probs[1];
      info->music_prob = frame_probs[0];
      info->bandwidth = bandwidth;
      tonal->prev_bandwidth = bandwidth;
      info->noisiness = fn_f;
      info->valid = 1;
   }
}

static void fl_tonality_get_info(fl_tonality_state *tonal, fl_analysis_info *info_out, int len) {
   int pos, curr_lookahead, tonality_count, i, pos0, mpos, vpos, bandwidth_span;
   xf tonality_max, tonality_avg, prob_avg, prob_count, prob_min, prob_max, mp;
   float vad_prob;
   pos = tonal->read_pos;
   curr_lookahead = tonal->write_pos-tonal->read_pos;
   if (curr_lookahead < 0) curr_lookahead += DETECT_SIZE;
   tonal->read_subframe += len/(tonal->Fs/400);
   while (tonal->read_subframe >= 8) {
      tonal->read_subframe -= 8;
      tonal->read_pos++;
   }
   if (tonal->read_pos >= DETECT_SIZE) tonal->read_pos -= DETECT_SIZE;
   if (len > tonal->Fs/50 && pos != tonal->write_pos) {
      pos++;
      if (pos == DETECT_SIZE) pos = 0;
   }
   if (pos == tonal->write_pos) pos--;
   if (pos < 0) pos = DETECT_SIZE-1;
   pos0 = pos;
   *info_out = tonal->info[pos];
   if (!info_out->valid) return;
   tonality_max = tonality_avg = info_out->tonality;
   tonality_count = 1;
   bandwidth_span = 6;
   for (i = 0; i < 3; i++) {
      pos++;
      if (pos == DETECT_SIZE) pos = 0;
      if (pos == tonal->write_pos) break;
      tonality_max = tonality_max > tonal->info[pos].tonality ? tonality_max : XF(tonal->info[pos].tonality);
      tonality_avg += tonal->info[pos].tonality;
      tonality_count++;
      info_out->bandwidth = IMAX(info_out->bandwidth, tonal->info[pos].bandwidth);
      bandwidth_span--;
   }
   pos = pos0;
   for (i = 0; i < bandwidth_span; i++) {
      pos--;
      if (pos < 0) pos = DETECT_SIZE-1;
      if (pos == tonal->write_pos) break;
      info_out->bandwidth = IMAX(info_out->bandwidth, tonal->info[pos].bandwidth);
   }
   {
      xf a = tonality_avg/tonality_count, c = tonality_max-.2f;
      info_out->tonality = (float)(a > c ? a : c);
   }
   mpos = vpos = pos0;
   if (curr_lookahead > 15) {
      mpos += 5;
      if (mpos >= DETECT_SIZE) mpos -= DETECT_SIZE;
      vpos += 1;
      if (vpos >= DETECT_SIZE) vpos -= DETECT_SIZE;
   }
   prob_min = 1;
   prob_max = 0;
   vad_prob = tonal->info[vpos].activity_probability;
   prob_count = FL_MAX(.1f, vad_prob);
   prob_avg = prob_count*tonal->info[mpos].music_prob;
   while (1) {
      float pos_vad;
      xf a;
      mpos++;
      if (mpos == DETECT_SIZE) mpos = 0;
      if (mpos == tonal->write_pos) break;
      vpos++;
      if (vpos == DETECT_SIZE) vpos = 0;
      if (vpos == tonal->write_pos) break;
      pos_vad = tonal->info[vpos].activity_probability;
      a = (prob_avg-(XF(vad_prob)-pos_vad)*10.f)/prob_count;
      prob_min = a < prob_min ? a : prob_min;
      a = (prob_avg+(XF(vad_prob)-pos_vad)*10.f)/prob_count;
      prob_max = a > prob_max ? a : prob_max;
      prob_count += FL_MAX(.1f, pos_vad);
      prob_avg += XF(FL_MAX(.1f, pos_vad))*tonal->info[mpos].music_prob;
   }
   mp = prob_avg/prob_count;
   info_out->music_prob = (float)mp;
   prob_min = mp < prob_min ? mp : prob_min;
   prob_max = mp > prob_max ? mp : prob_max;
   prob_min = prob_min > 0 ? prob_min : 0;
   prob_max = prob_max < 1 ? prob_max : 1;
   if (curr_lookahead < 10) {
      xf pmin = prob_min, pmax = prob_max, k;
      pos = pos0;
      for (i = 0; i < IMIN(tonal->count-1, 15); i++) {
         pos--;
         if (pos < 0) pos = DETECT_SIZE-1;
         pmin = pmin < tonal->info[pos].music_prob ? pmin : XF(tonal->info[pos].music_prob);
         pmax = pmax > tonal->info[pos].music_prob ? pmax : XF(tonal->info[pos].music_prob);
      }
      pmin = pmin-XF(.1f)*vad_prob;
      pmin = 0 > pmin ? 0 : pmin;
      pmax = pmax+XF(.1f)*vad_prob;
      pmax = 1 < pmax ? 1 : pmax;
      k = 1-XF(.1f)*curr_lookahead;
      prob_min += k*(pmin-prob_min);
      prob_max += k*(pmax-prob_max);
   }
   info_out->music_prob_min = (float)prob_min;
   info_out->music_prob_max = (float)prob_max;
}

static void fl_run_analysis(fl_tonality_state *analysis, const float *pcm, int analysis_frame_size, int frame_size,
      int c1, int c2, int C, int lsb_depth, fl_analysis_info *analysis_info) {
   int offset, pcm_len;
   const int Fs = 48000;
   analysis_frame_size -= analysis_frame_size&1;
   if (pcm != NULL) {
      analysis_frame_size = IMIN((DETECT_SIZE-5)*Fs/50, analysis_frame_size);
      pcm_len = analysis_frame_size-analysis->analysis_offset;
      offset = analysis->analysis_offset;
      while (pcm_len > 0) {
         fl_tonality_analysis(analysis, pcm, IMIN(Fs/50, pcm_len), offset, c1, c2, C, lsb_depth);
         offset += Fs/50;
         pcm_len -= Fs/50;
      }
      analysis->analysis_offset = analysis_frame_size;
      analysis->analysis_offset -= frame_size;
   }
   fl_tonality_get_info(analysis, analysis_info, frame_size);
}

/* ======== enc_front.c ======== */
/* ---- Opus encoder front end: libopusenc buffering/resampling, Ogg output, opusenc main loop ---- */

/* ---- libopusenc resampler: speex quality 5 with the sinc table computed on the x87 ---- */
typedef struct {
   fl_u32 num_rate, den_rate, filt_len, mem_alloc_size;
   int int_advance, frac_advance, channels;
   float *sinc_table, *mem;
   fl_i32 last_sample[255];
   fl_u32 samp_frac_num[255];
} fl_ope_rs;

/* sinc() with compute_func() as compiled in libopusenc (window KAISER10). */
static float fl_ope_sinc(float cutoff, int i, fl_u32 den, int jterm, int N) {
   xf x = XF(jterm)-XF(i)/XF(den), y, frac, f2, f3, i0, i1, i2, i3, cf;
   double arg, s, Nd = N;
   float x_f, cf_arg, y_f;
   int ind;
   if (fabsl(x) < XF(1e-6)) return cutoff;
   if (fabsl(x) > XF(.5f)*N) return 0;
   x_f = (float)x;
   arg = (double)(XF(3.14159265358979323846)*(x*cutoff));
   s = fl_m_sin(arg);
   cf_arg = (float)((XF(x_f)+XF(x_f))/XF(Nd));
   cf_arg = fabsf(cf_arg);
   y = XF(cf_arg)*32;
   y_f = (float)y;
   ind = (int)(float)fl_m_floorf(y_f);
   frac = XF(y_f)-ind;
   f2 = frac*frac;
   f3 = frac*f2;
   i3 = XF(-0.1666666667)*frac+XF(0.1666666667)*f3;
   i2 = frac+XF(0.5)*f2-XF(0.5)*f3;
   i0 = XF(-0.3333333333)*frac+XF(0.5)*f2-XF(0.1666666667)*f3;
   i1 = XF(1.f)-i3-i2-i0;
   cf = i0*XF(fl_kaiser10_table[ind])+i1*XF(fl_kaiser10_table[ind+1])+i2*XF(fl_kaiser10_table[ind+2])+i3*XF(fl_kaiser10_table[ind+3]);
   return (float)(((XF(s)*cutoff)/XF(arg))*cf);
}

static int fl_ope_rs_init(fl_ope_rs *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   fl_u32 fact = fl_gcd(in_rate, out_rate), i;
   float cutoff;
   int j;
   memset(r, 0, sizeof(*r));
   r->channels = channels;
   r->num_rate = in_rate/fact;
   r->den_rate = out_rate/fact;
   r->int_advance = r->num_rate/r->den_rate;
   r->frac_advance = r->num_rate%r->den_rate;
   r->filt_len = 80;
   if (r->num_rate > r->den_rate) {
      fl_u32 major = r->filt_len/r->den_rate, remain = r->filt_len%r->den_rate;
      cutoff = (float)(XF(0.922f)*XF((double)r->den_rate)/XF((double)r->num_rate));
      r->filt_len = remain*r->num_rate/r->den_rate+major*r->num_rate;
      r->filt_len = ((r->filt_len-1)&(~0x7))+8;
   } else cutoff = 0.940f;
   r->sinc_table = (float*)malloc(sizeof(float)*r->filt_len*r->den_rate);
   r->mem_alloc_size = r->filt_len-1+160;
   r->mem = (float*)calloc((size_t)channels*r->mem_alloc_size, sizeof(float));
   if (!r->sinc_table || !r->mem) return -1;
   for (i = 0; i < r->den_rate; i++)
      for (j = 0; j < (int)r->filt_len; j++)
         r->sinc_table[i*r->filt_len+j] = fl_ope_sinc(cutoff, (int)i, r->den_rate, j-(fl_i32)r->filt_len/2+1, r->filt_len);
   for (j = 0; j < channels; j++) r->last_sample[j] = r->filt_len/2;
   return 0;
}
static void fl_ope_rs_free(fl_ope_rs *r) {
   free(r->sinc_table);
   free(r->mem);
}
static void fl_ope_rs_native(fl_ope_rs *r, int ch, fl_u32 *in_len, float *out, fl_u32 *out_len) {
   const int N = r->filt_len;
   int out_sample = 0, j;
   float *mem = r->mem+ch*r->mem_alloc_size;
   fl_i32 last_sample = r->last_sample[ch];
   fl_u32 samp_frac_num = r->samp_frac_num[ch], ilen;
   while (!(last_sample >= (fl_i32)*in_len || out_sample >= (fl_i32)*out_len)) {
      out[r->channels*out_sample++] = fl_rs_inner(&r->sinc_table[samp_frac_num*N], &mem[last_sample], N);
      last_sample += r->int_advance;
      samp_frac_num += r->frac_advance;
      if (samp_frac_num >= r->den_rate) {
         samp_frac_num -= r->den_rate;
         last_sample++;
      }
   }
   r->last_sample[ch] = last_sample;
   r->samp_frac_num[ch] = samp_frac_num;
   if (r->last_sample[ch] < (fl_i32)*in_len) *in_len = r->last_sample[ch];
   *out_len = out_sample;
   r->last_sample[ch] -= *in_len;
   ilen = *in_len;
   for (j = 0; j < N-1; ++j) mem[j] = mem[j+ilen];
}
static void fl_ope_rs_process(fl_ope_rs *r, const float *in, fl_u32 *in_len, float *out, fl_u32 *out_len) {
   fl_u32 bak_in = *in_len, bak_out = *out_len;
   int ch, C = r->channels;
   for (ch = 0; ch < C; ch++) {
      fl_u32 ilen = bak_in, olen = bak_out, xlen = r->mem_alloc_size-(r->filt_len-1);
      float *x = r->mem+ch*r->mem_alloc_size, *o = out+ch;
      const float *ip = in ? in+ch : NULL;
      const int filt_offs = r->filt_len-1;
      while (ilen && olen) {
         fl_u32 j, ichunk = (ilen > xlen) ? xlen : ilen, ochunk = olen;
         for (j = 0; j < ichunk; ++j) x[j+filt_offs] = ip ? ip[j*C] : 0;
         fl_ope_rs_native(r, ch, &ichunk, o, &ochunk);
         ilen -= ichunk;
         olen -= ochunk;
         o += ochunk*C;
         if (ip) ip += ichunk*C;
      }
      *in_len = bak_in-ilen;
      *out_len = bak_out-olen;
   }
}

/* ---- Ogg page packer (libopusenc ogg_packer.c) ---- */
typedef struct {
   fl_u64 granulepos;
   size_t buf_pos, buf_size, lacing_pos, lacing_size;
   int flags;
   size_t pageno;
} fl_oggp_page;

typedef struct {
   fl_i32 serialno;
   unsigned char *buf;
   size_t buf_fill, buf_begin, buf_cap;
   unsigned char *lacing;
   size_t lacing_fill, lacing_begin, lacing_cap;
   fl_oggp_page *pages;
   size_t pages_fill, pages_cap;
   fl_u64 muxing_delay, curr_granule, last_granule;
   int is_eos;
   size_t pageno;
   FILE *out;
   int error;
} fl_oggp;

static fl_u32 fl_ogg_crc_table[256];
static void fl_ogg_crc_init(void) {
   fl_u32 i, j;
   if (fl_ogg_crc_table[1]) return;
   for (i = 0; i < 256; i++) {
      fl_u32 r = i<<24;
      for (j = 0; j < 8; j++) r = (r&0x80000000U) ? (r<<1)^0x04c11db7U : r<<1;
      fl_ogg_crc_table[i] = r;
   }
}
static void fl_oggp_reserve(unsigned char **p, size_t *cap, size_t need) {
   if (need > *cap) {
      size_t n = need*3/2+1024;
      *p = (unsigned char*)realloc(*p, n);
      *cap = n;
   }
}
static void fl_oggp_flush(fl_oggp *o) {
   size_t nb_lacing;
   int cont = 0;
   if (o->lacing_fill == o->lacing_begin) return;
   nb_lacing = o->lacing_fill-o->lacing_begin;
   do {
      fl_oggp_page *p;
      if (o->pages_fill >= o->pages_cap) {
         o->pages_cap = 1+o->pages_cap*3/2;
         o->pages = (fl_oggp_page*)realloc(o->pages, o->pages_cap*sizeof(*o->pages));
      }
      p = &o->pages[o->pages_fill++];
      p->granulepos = o->curr_granule;
      p->lacing_pos = o->lacing_begin;
      p->lacing_size = nb_lacing;
      p->flags = cont;
      p->buf_pos = o->buf_begin;
      if (p->lacing_size > 255) {
         size_t bytes = 0;
         int i;
         for (i = 0; i < 255; i++) bytes += o->lacing[o->lacing_begin+1];
         p->buf_size = bytes;
         p->lacing_size = 255;
         p->granulepos = (fl_u64)-1;
         cont = 1;
      } else {
         p->buf_size = o->buf_fill-o->buf_begin;
         if (o->is_eos) p->flags |= 0x04;
      }
      nb_lacing -= p->lacing_size;
      o->lacing_begin += p->lacing_size;
      o->buf_begin += p->buf_size;
      p->pageno = o->pageno++;
      if (p->pageno == 0) p->flags |= 0x02;
   } while (nb_lacing > 0);
   o->last_granule = o->curr_granule;
}
static void fl_oggp_write_pages(fl_oggp *o) {
   size_t k;
   for (k = 0; k < o->pages_fill; k++) {
      fl_oggp_page *p = &o->pages[k];
      unsigned char h[27+255];
      fl_u32 crc = 0;
      fl_u64 g = p->granulepos;
      size_t i, hs = 27+p->lacing_size;
      memcpy(h, "OggS", 4);
      h[4] = 0;
      h[5] = (unsigned char)p->flags;
      for (i = 6; i < 14; i++) {
         h[i] = (unsigned char)(g&0xff);
         g >>= 8;
      }
      fl_put_le32(h+14, (fl_u32)o->serialno);
      fl_put_le32(h+18, (fl_u32)p->pageno);
      memset(h+22, 0, 4);
      h[26] = (unsigned char)p->lacing_size;
      memcpy(h+27, &o->lacing[p->lacing_pos], p->lacing_size);
      for (i = 0; i < hs; i++) crc = (crc<<8)^fl_ogg_crc_table[((crc>>24)&0xff)^h[i]];
      for (i = 0; i < p->buf_size; i++) crc = (crc<<8)^fl_ogg_crc_table[((crc>>24)&0xff)^o->buf[p->buf_pos+i]];
      fl_put_le32(h+22, crc);
      if (fwrite(h, 1, hs, o->out) != hs || fwrite(o->buf+p->buf_pos, 1, p->buf_size, o->out) != p->buf_size) o->error = 1;
   }
   o->pages_fill = 0;
}
static void fl_oggp_commit(fl_oggp *o, const unsigned char *data, int bytes, fl_u64 granulepos, int eos) {
   size_t i, nb_255s = bytes/255;
   if (o->lacing_fill-o->lacing_begin+nb_255s+1 > 255 || (o->muxing_delay && granulepos-o->last_granule > o->muxing_delay))
      fl_oggp_flush(o);
   fl_oggp_reserve(&o->buf, &o->buf_cap, o->buf_fill+bytes);
   memcpy(o->buf+o->buf_fill, data, bytes);
   o->buf_fill += bytes;
   fl_oggp_reserve(&o->lacing, &o->lacing_cap, o->lacing_fill+nb_255s+1);
   for (i = 0; i < nb_255s; i++) o->lacing[o->lacing_fill+i] = 255;
   o->lacing[o->lacing_fill+nb_255s] = (unsigned char)(bytes-255*nb_255s);
   o->lacing_fill += nb_255s+1;
   o->curr_granule = granulepos;
   o->is_eos = eos;
   if (o->muxing_delay && granulepos-o->last_granule >= o->muxing_delay) fl_oggp_flush(o);
}
/* Drops consumed data once all pages are written. */
static void fl_oggp_compact(fl_oggp *o) {
   if (o->pages_fill == 0 && o->buf_begin > 0) {
      memmove(o->buf, o->buf+o->buf_begin, o->buf_fill-o->buf_begin);
      o->buf_fill -= o->buf_begin;
      o->buf_begin = 0;
      memmove(o->lacing, o->lacing+o->lacing_begin, o->lacing_fill-o->lacing_begin);
      o->lacing_fill -= o->lacing_begin;
      o->lacing_begin = 0;
   }
}

/* ---- Ogg Opus encoder (libopusenc 0.3 logic) ---- */
#define OPE_LPC_PADDING 120
#define OPE_LPC_ORDER 24
#define OPE_LPC_INPUT 480
#define OPE_LPC_GOERTZEL_CONST 1.99931465f
#define OPE_BUFFER_SAMPLES (96000+24000)

typedef struct fl_msenc fl_msenc;
static fl_msenc *fl_msenc_create(int channels, int family, int *err);
static int fl_msenc_layout(int channels, int family, int *streams, int *coupled, unsigned char *mapping);
static void fl_msenc_destroy(fl_msenc *st);
static int fl_msenc_encode(fl_msenc *st, const float *pcm, int frame_size, unsigned char *data, fl_i32 max_bytes);
static int fl_msenc_lookahead(fl_msenc *st);
static void fl_msenc_setup(fl_msenc *st, const fl_options *opt, int lsb_depth);
static int fl_msenc_get_duration(fl_msenc *st);
static void fl_msenc_set_duration(fl_msenc *st, int request);

typedef struct {
   int channels, streams;
   fl_i32 rate;
   fl_msenc *st;
   fl_ope_rs re;
   int have_re;
   float *buffer, *lpc_buffer;
   int buffer_start, buffer_end, frame_size, frame_size_request, decision_delay, draining;
   fl_i64 curr_granule, write_granule, end_granule, global_granule_offset;
   fl_oggp ogg;
   unsigned char *packet;
} fl_ope;

/* LPC extrapolation of the input tail (extend_signal / vorbis_lpc_from_data). */
static void fl_ope_extend_signal(float *x, int before, int after, int channels) {
   int c, i, j;
   float window[OPE_LPC_PADDING];
   if (after == 0) return;
   before = FL_MIN(before, OPE_LPC_INPUT);
   if (before < 4*OPE_LPC_ORDER) {
      for (i = 0; i < after*channels; i++) x[i] = 0;
      return;
   }
   {
      xf m0 = 1, m1 = (float)(.5*OPE_LPC_GOERTZEL_CONST), a1 = OPE_LPC_GOERTZEL_CONST, w;
      window[0] = 1;
      for (i = 1; i < OPE_LPC_PADDING; i++) {
         w = a1*m0-m1;
         window[i] = (float)w;
         m1 = m0;
         m0 = w;
      }
      for (i = 0; i < OPE_LPC_PADDING; i++) window[i] = (float)(XF(.5f)+XF(.5f)*window[i]);
   }
   for (c = 0; c < channels; c++) {
      float lpci[OPE_LPC_ORDER], *data = x-channels*before+c;
      double aut[OPE_LPC_ORDER+1], lpc[OPE_LPC_ORDER];
      xf error, epsilon;
      int n = before;
      j = OPE_LPC_ORDER+1;
      while (j--) {
         xf d = 0;
         for (i = j; i < n; i++) d += XF(data[i*channels])*data[(i-j)*channels];
         aut[j] = (double)d;
      }
      for (i = 1; i <= OPE_LPC_ORDER; i++) aut[i] = (double)(XF(aut[i])-XF(aut[i])*(0.008f*0.008f)*i*i);
      error = XF(aut[0])*XF(1.+1e-7);
      epsilon = XF(1e-6)*XF(aut[0])+XF(1e-7);
      for (i = 0; i < OPE_LPC_ORDER; i++) {
         xf r = -XF(aut[i+1]);
         if (error < epsilon) {
            memset(lpc+i, 0, (OPE_LPC_ORDER-i)*sizeof(*lpc));
            break;
         }
         for (j = 0; j < i; j++) r -= XF(lpc[j])*XF(aut[i-j]);
         r /= error;
         lpc[i] = (double)r;
         for (j = 0; j < i/2; j++) {
            double tmp = lpc[j];
            lpc[j] = (double)(XF(lpc[j])+r*XF(lpc[i-1-j]));
            lpc[i-1-j] = (double)(XF(lpc[i-1-j])+r*XF(tmp));
         }
         if (i&1) lpc[j] = (double)(XF(lpc[j])+XF(lpc[j])*r);
         error *= XF(1.)-r*r;
      }
      {
         xf g = XF(.999), damp = g;
         for (j = 0; j < OPE_LPC_ORDER; j++) {
            lpc[j] = (double)(XF(lpc[j])*damp);
            damp *= g;
         }
      }
      for (j = 0; j < OPE_LPC_ORDER; j++) lpci[j] = (float)lpc[j];
      for (i = 0; i < after; i++) {
         xf sum = 0;
         for (j = 0; j < OPE_LPC_ORDER; j++) sum -= XF(x[(i-j-1)*channels+c])*lpci[j];
         x[i*channels+c] = (float)sum;
      }
      for (i = 0; i < after; i++) x[i*channels+c] = (float)(XF(x[i*channels+c])*window[i]);
   }
}

static int fl_frame_samples(int request) {
   return request <= 5005 ? 120<<(request-5001) : (request-5001-2)*960;
}

static void fl_ope_encode_buffer(fl_ope *e) {
   fl_i32 max_packet_size = (1277*6+2)*e->streams;
   fl_i64 end_granule48k = (e->end_granule*48000+e->rate-1)/e->rate+e->global_granule_offset;
   while (e->buffer_end-e->buffer_start > e->frame_size+e->decision_delay) {
      int nbBytes, e_o_s;
      fl_i64 granulepos;
      /* last packet: avoid encoding too much padding */
      if (e->curr_granule+e->frame_size >= end_granule48k && e->draining && e->frame_size_request > 5004) {
         int min_samples = (int)(end_granule48k-e->curr_granule), req = 5004;
         while (fl_frame_samples(req) < min_samples) req++;
         e->frame_size_request = req;
         e->frame_size = fl_frame_samples(req);
         fl_msenc_set_duration(e->st, req);
      }
      nbBytes = fl_msenc_encode(e->st, &e->buffer[e->channels*e->buffer_start], e->buffer_end-e->buffer_start,
            e->packet, max_packet_size);
      if (nbBytes < 0) {
         e->ogg.error = 1;
         return;
      }
      e->curr_granule += e->frame_size;
      granulepos = e->curr_granule;
      e_o_s = e->curr_granule >= end_granule48k;
      if (e_o_s) granulepos = end_granule48k;
      fl_oggp_commit(&e->ogg, e->packet, nbBytes, (fl_u64)granulepos, e_o_s);
      if (e_o_s) fl_oggp_flush(&e->ogg);
      fl_oggp_write_pages(&e->ogg);
      fl_oggp_compact(&e->ogg);
      e->buffer_start += e->frame_size;
      if (e_o_s) {
         e->buffer_start = e->buffer_end;
         return;
      }
   }
   if (e->buffer_end == OPE_BUFFER_SAMPLES && e->buffer_start > OPE_LPC_INPUT) {
      memmove(&e->buffer[0], &e->buffer[e->channels*(e->buffer_start-OPE_LPC_INPUT)],
            e->channels*(e->buffer_end-e->buffer_start+OPE_LPC_INPUT)*sizeof(*e->buffer));
      e->buffer_end -= e->buffer_start-OPE_LPC_INPUT;
      e->buffer_start = OPE_LPC_INPUT;
   }
}

static void fl_ope_write_float(fl_ope *e, const float *pcm, int n) {
   int C = e->channels, i;
   e->write_granule += n;
   e->end_granule = e->write_granule;
   if (e->lpc_buffer) {
      if (n < OPE_LPC_INPUT) {
         for (i = 0; i < (OPE_LPC_INPUT-n)*C; i++) e->lpc_buffer[i] = e->lpc_buffer[n*C+i];
         for (i = 0; i < n*C; i++) e->lpc_buffer[(OPE_LPC_INPUT-n)*C+i] = pcm[i];
      } else {
         for (i = 0; i < OPE_LPC_INPUT*C; i++) e->lpc_buffer[i] = pcm[(n-OPE_LPC_INPUT)*C+i];
      }
   }
   do {
      fl_u32 in_samples, out_samples = OPE_BUFFER_SAMPLES-e->buffer_end;
      if (e->have_re) {
         in_samples = n;
         fl_ope_rs_process(&e->re, pcm, &in_samples, &e->buffer[C*e->buffer_end], &out_samples);
      } else {
         int curr = (int)FL_MIN((fl_u32)n, out_samples);
         for (i = 0; i < C*curr; i++) e->buffer[C*e->buffer_end+i] = pcm[i];
         in_samples = out_samples = curr;
      }
      e->buffer_end += out_samples;
      pcm += in_samples*C;
      n -= in_samples;
      fl_ope_encode_buffer(e);
   } while (n > 0);
}

static void fl_ope_drain(fl_ope *e) {
   int pad_samples, resampler_drain = 0, C = e->channels;
   if (e->have_re) resampler_drain = (int)(((e->re.filt_len/2)*e->re.den_rate+(e->re.num_rate>>1))/e->re.num_rate);
   pad_samples = FL_MAX(OPE_LPC_PADDING, (int)e->global_granule_offset+e->frame_size+resampler_drain+1);
   if (e->buffer_start > OPE_LPC_INPUT) {
      memmove(&e->buffer[0], &e->buffer[C*(e->buffer_start-OPE_LPC_INPUT)], C*(e->buffer_end-e->buffer_start+OPE_LPC_INPUT)*sizeof(*e->buffer));
      e->buffer_end -= e->buffer_start-OPE_LPC_INPUT;
      e->buffer_start = OPE_LPC_INPUT;
   }
   memset(&e->buffer[C*e->buffer_end], 0, pad_samples*C*sizeof(e->buffer[0]));
   if (e->have_re) {
      fl_ope_extend_signal(&e->lpc_buffer[OPE_LPC_INPUT*C], OPE_LPC_INPUT, OPE_LPC_PADDING, C);
      do {
         fl_u32 in_samples = OPE_LPC_PADDING, out_samples = pad_samples;
         fl_ope_rs_process(&e->re, &e->lpc_buffer[OPE_LPC_INPUT*C], &in_samples, &e->buffer[C*e->buffer_end], &out_samples);
         e->buffer_end += out_samples;
         pad_samples -= out_samples;
         memset(&e->lpc_buffer[OPE_LPC_INPUT*C], 0, OPE_LPC_PADDING*C*sizeof(e->lpc_buffer[0]));
      } while (pad_samples > 0);
   } else {
      fl_ope_extend_signal(&e->buffer[C*e->buffer_end], e->buffer_end, OPE_LPC_PADDING, C);
      e->buffer_end += pad_samples;
   }
   e->decision_delay = 0;
   e->draining = 1;
   fl_ope_encode_buffer(e);
}

static void fl_put_bytes(unsigned char **p, size_t *len, const void *src, size_t n) {
   *p = (unsigned char*)realloc(*p, *len+n);
   memcpy(*p+*len, src, n);
   *len += n;
}

/* Encodes a WAV file to Ogg Opus like opusenc (opus-tools 0.2 + libopusenc 0.3). */
/* opusenc automatic bitrate for a stream layout and input rate, scaled by --quality. */
static int fl_default_bitrate(int streams, int coupled, fl_i32 rate, double quality) {
   static const double ratio[11] = {1/4., 1/3., 5/12., 1/2., 2/3., 5/6., 1., 7/6., 4/3., 5/3., 2.};
   int r = rate < 44100 ? (int)rate : 48000;
   int bitrate = ((64000*streams+32000*coupled)*(IMIN(48, IMAX(8, (r+1000)/1000))+16)+32)>>6;
   if (quality >= 0) {
      /* quality_scale() in x87, rounded to double before truncation */
      unsigned short cw = fl_x87_enter();
      int l = (int)quality;
      xf sc = quality >= 10 ? XF(2) : (xf)ratio[l]+((xf)ratio[l+1]-ratio[l])*((xf)quality-l);
      bitrate = (int)(double)((xf)bitrate*sc+XF(.5f));
      fl_x87_leave(cw);
   }
   return bitrate;
}

/* opusenc setup_downmix(): builds the in->out matrix, returns 0 if nothing to do. */
static int fl_setup_downmix(float *m, int in, int out) {
   static const float tab[7][8][2] = {
      {{1, 0}, {0, 1}},
      {{1, 0}, {0.7071f, 0.7071f}, {0, 1}},
      {{1, 0}, {0, 1}, {0.866f, 0.5f}, {0.5f, 0.866f}},
      {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {0.866f, 0.5f}, {0.5f, 0.866f}},
      {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {0.866f, 0.5f}, {0.5f, 0.866f}, {0.7071f, 0.7071f}},
      {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {0.866f, 0.5f}, {0.5f, 0.866f}, {0.6123f, 0.6123f}, {0.7071f, 0.7071f}},
      {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {0.866f, 0.5f}, {0.5f, 0.866f}, {0.866f, 0.5f}, {0.5f, 0.866f}, {0.7071f, 0.7071f}}};
   int i, j;
   if (in > 8) out = 1;
   if (in <= out || in <= 1) return 0;
   if (in <= 8) {
      unsigned short cw = fl_x87_enter();
      xf sum = 0, scale;
      if (out == 2) {
         for (j = 0; j < 2; j++)
            for (i = 0; i < in; i++) m[in*j+i] = tab[in-2][i][j];
      } else {
         for (i = 0; i < in; i++) m[i] = (float)((xf)tab[in-2][i][0]+tab[in-2][i][1]);
      }
      /* the sum and scale stay in x87 registers */
      for (i = 0; i < in*out; i++) sum += m[i];
      scale = (xf)out/sum;
      for (i = 0; i < in*out; i++) m[i] = (float)(m[i]*scale);
      fl_x87_leave(cw);
   } else {
      unsigned short cw = fl_x87_enter();
      for (i = 0; i < in; i++) m[i] = (float)(XF(1)/in);
      fl_x87_leave(cw);
   }
   return out;
}

/* opusenc read_downmix(): extended precision accumulation, one float store. */
static void fl_apply_downmix(const float *m, const float *in, float *out, int n, int ic, int oc) {
   int i, j, k;
   for (i = 0; i < n; i++)
      for (j = 0; j < oc; j++) {
         xf acc = 0;
         for (k = 0; k < ic; k++) acc += (xf)in[i*ic+k]*m[ic*j+k];
         out[i*oc+j] = (float)acc;
      }
}

/* opusenc ReplayGain handling for FLAC input: album gain -> OpusHead gain (R128 level),
   track gain -> R128_TRACK_GAIN. Other ReplayGain tags are dropped. */
static void fl_opus_gain_from_tags(const fl_format *f, char *r128, int *gain_q8) {
   float album = 0, track = 0, ref = 89.0f, g;
   int saw_album = 0, saw_track = 0, i;
   *gain_q8 = 0;
   r128[0] = 0;
   for (i = 0; i < f->ntags; i++) {
      const char *t = f->tags[i];
      char *end;
      double v;
      if (fl_tag_is(t, "REPLAYGAIN_REFERENCE_LOUDNESS")) {
         v = strtod(t+30, &end);
         if (end > t+30) ref = (float)v;
      } else if (fl_tag_is(t, "REPLAYGAIN_ALBUM_GAIN") || fl_tag_is(t, "REPLAYGAIN_TRACK_GAIN")) {
         v = strtod(t+22, &end);
         if (end > t+22) {
            if (t[11] == 'A' || t[11] == 'a') {
               album = (float)v;
               saw_album = 1;
            } else {
               track = (float)v;
               saw_track = 1;
            }
         }
      }
   }
   if (saw_album) {
      g = (float)(256*(album+(84-ref))+(xf)0.5);
      *gain_q8 = g < -32768 ? -32768 : g < 32767 ? (int)floor(g) : 32767;
   }
   if (saw_track) {
      g = (float)(256*(track-album)+(xf)0.5);
      sprintf(r128, "R128_TRACK_GAIN=%i", g < -32768 ? -32768 : g < 32767 ? (int)floor(g) : 32767);
   }
}

/* Source tags that are not copied to the Opus file. */
static int fl_opus_skip_tag(const char *t) {
   return fl_tag_is(t, "ENCODER") || fl_tag_is(t, "ENCODER_OPTIONS") || fl_tag_is(t, "R128_TRACK_GAIN") ||
          fl_tag_is(t, "R128_ALBUM_GAIN") || fl_tag_is(t, "REPLAYGAIN_ALBUM_GAIN") ||
          fl_tag_is(t, "REPLAYGAIN_TRACK_GAIN") || fl_tag_is(t, "REPLAYGAIN_ALBUM_PEAK") ||
          fl_tag_is(t, "REPLAYGAIN_TRACK_PEAK") || fl_tag_is(t, "REPLAYGAIN_REFERENCE_LOUDNESS");
}

static void fl_put_comment(unsigned char **tags, size_t *tlen, const char *c) {
   unsigned char n4[4];
   fl_put_le32(n4, (fl_u32)strlen(c));
   fl_put_bytes(tags, tlen, n4, 4);
   fl_put_bytes(tags, tlen, c, strlen(c));
}

/* Opus output: the opusenc main loop fed by any source (960-sample reads, WAV to Vorbis
   channel order, optional downmix, int to float scaling like opusenc's readers). */
typedef struct {
   fl_sink base;
   char *path;
   fl_options opt;
   fl_ope *e;
   float *input, *raw, dmx[2*255], scale;
   int C, IC, n, is_float;
} fl_opus_sink;

static int fl_opus_start(fl_sink *s, const fl_format *f) {
   fl_opus_sink *o = (fl_opus_sink*)s;
   fl_options *opt = &o->opt;
   fl_ope *e;
   int ret, err, C, IC, bitrate, family, streams, coupled, gain;
   unsigned char mapping[255];
   char r128[40];
   unsigned short cw;
   IC = C = f->channels;
   if (C < 1 || C > 255 || f->rate <= 0) return FL_ERR_UNSUPPORTED;
   if (opt->bitrate > 0 && opt->quality >= 0) return FL_ERR_BAD_ARG;
   o->is_float = f->is_float;
   o->scale = f->is_float ? 1.0f : (float)ldexp(1.0, 1-f->bits);
   /* opusenc downmix rule: below 16 kbps per channel surround is downmixed */
   if (opt->downmix == 0 && C > 2 && opt->bitrate > 0 && opt->bitrate < 16000*C) opt->downmix = C > 8 ? 1 : 2;
   if (opt->downmix > 0) C = fl_setup_downmix(o->dmx, IC, opt->downmix);
   if (C == 0) C = IC;
   /* opusenc: family 1 for 3..8 channels, 255 above */
   family = C > 8 ? 255 : C > 2;
   fl_msenc_layout(C, family, &streams, &coupled, mapping);
   /* opusenc default bitrate and limits */
   bitrate = opt->bitrate > 0 ? opt->bitrate : fl_default_bitrate(streams, coupled, f->rate, opt->quality);
   if (bitrate > 1024000*C || bitrate < 500) return FL_ERR_BAD_ARG;
   opt->bitrate = bitrate;
   o->C = C;
   o->IC = IC;
   o->e = e = (fl_ope*)calloc(1, sizeof(*e));
   o->input = (float*)malloc(sizeof(float)*960*C);
   o->raw = (float*)malloc(sizeof(float)*960*IC);
   if (!e || !o->input || !o->raw) return FL_ERR_ALLOC;
   cw = fl_x87_enter();
   fl_ogg_crc_init();
   fl_opus_gain_from_tags(f, r128, &gain);
   e->channels = C;
   e->rate = f->rate;
   e->frame_size = 960;
   e->decision_delay = 96000;
   e->global_granule_offset = -1;
   e->buffer = (float*)malloc(sizeof(float)*OPE_BUFFER_SAMPLES*C);
   e->streams = streams;
   e->packet = (unsigned char*)malloc((1277*6+2)*streams);
   e->st = fl_msenc_create(C, family, &err);
   if (f->rate != 48000) {
      e->have_re = 1;
      fl_ope_rs_init(&e->re, C, f->rate, 48000);
      e->lpc_buffer = (float*)calloc((OPE_LPC_INPUT+OPE_LPC_PADDING)*C, sizeof(float));
   }
   fl_msenc_setup(e->st, opt, IMAX(8, IMIN(24, f->container_bits)));
   e->frame_size_request = fl_msenc_get_duration(e->st);
   e->frame_size = fl_frame_samples(e->frame_size_request);
   e->ogg.out = fl_fopen(o->path, "wb");
   e->ogg.serialno = opt->serialno >= 0 ? (fl_i32)(fl_u32)opt->serialno : rand();
   e->ogg.muxing_delay = 48000;
   if (!e->ogg.out || !e->buffer || !e->packet || !e->st) {
      ret = e->ogg.out ? FL_ERR_ALLOC : FL_ERR_IO;
      goto done;
   }
   {
      /* OpusHead and OpusTags pages */
      unsigned char head[21+255];
      unsigned char *tags = NULL;
      size_t tlen = 0;
      const char *vendor = "flapus (libopus 1.6.1 compatible)";
      unsigned char n4[4];
      int ncom = 1, padded, i;
      e->global_granule_offset = fl_msenc_lookahead(e->st);
      memcpy(head, "OpusHead", 8);
      head[8] = 1;
      head[9] = (unsigned char)C;
      fl_put_le16(head+10, (int)e->global_granule_offset);
      fl_put_le32(head+12, (fl_u32)f->rate);
      fl_put_le16(head+16, gain);
      head[18] = (unsigned char)family;
      if (family != 0) {
         head[19] = (unsigned char)streams;
         head[20] = (unsigned char)coupled;
         memcpy(head+21, mapping, C);
      }
      fl_oggp_commit(&e->ogg, head, family != 0 ? 21+C : 19, 0, 0);
      fl_oggp_flush(&e->ogg);
      fl_put_bytes(&tags, &tlen, "OpusTags", 8);
      fl_put_le32(n4, (fl_u32)strlen(vendor));
      fl_put_bytes(&tags, &tlen, n4, 4);
      fl_put_bytes(&tags, &tlen, vendor, strlen(vendor));
      if (opt->comment_options) ncom++;
      if (r128[0]) ncom++;
      for (i = 0; i < f->ntags; i++) ncom += !fl_opus_skip_tag(f->tags[i]);
      fl_put_le32(n4, ncom);
      fl_put_bytes(&tags, &tlen, n4, 4);
      fl_put_comment(&tags, &tlen, "ENCODER=flapus");
      if (opt->comment_options) {
         char c2[300];
         snprintf(c2, sizeof(c2), "ENCODER_OPTIONS=%s", opt->comment_options);
         fl_put_comment(&tags, &tlen, c2);
      }
      for (i = 0; i < f->ntags; i++)
         if (!fl_opus_skip_tag(f->tags[i])) fl_put_comment(&tags, &tlen, f->tags[i]);
      if (r128[0]) fl_put_comment(&tags, &tlen, r128);
      /* opusenc --padding: reserve at least that many bytes and fill up the last
         Ogg segment; with 0 the tags are written without any padding */
      padded = opt->padding > 0 ? (int)((tlen+(size_t)opt->padding+255)/255*255-1) : (int)tlen;
      tags = (unsigned char*)realloc(tags, padded);
      memset(tags+tlen, 0, padded-tlen);
      fl_oggp_commit(&e->ogg, tags, padded, 0, 0);
      fl_oggp_flush(&e->ogg);
      fl_oggp_write_pages(&e->ogg);
      fl_oggp_compact(&e->ogg);
      free(tags);
   }
   ret = e->ogg.error ? FL_ERR_IO : FL_OK;
done:
   fl_x87_leave(cw);
   return ret;
}

/* Converts n frames into o->raw at frame offset o->n (Vorbis channel order). */
static void fl_opus_convert(fl_opus_sink *o, const fl_i32 *pcm, int n) {
   static const int perm[8][8] = {
      {0}, {0, 1}, {0, 2, 1}, {0, 1, 2, 3}, {0, 2, 1, 3, 4}, {0, 2, 1, 4, 5, 3},
      {0, 2, 1, 5, 6, 4, 3}, {0, 2, 1, 6, 7, 4, 5, 3}};
   int C = o->IC, i, j;
   int permute = C == 3 || (C > 4 && C <= 8);
   float *out = o->raw+(size_t)o->n*C;
   for (i = 0; i < n; i++)
      for (j = 0; j < C; j++) {
         fl_i32 v = pcm[i*C+(permute ? perm[C-1][j] : j)];
         if (o->is_float) memcpy(&out[i*C+j], &v, 4);
         else out[i*C+j] = (float)v*o->scale;
      }
}

/* Encodes the buffered read of o->n frames (one opusenc read call). */
static void fl_opus_flush_read(fl_opus_sink *o) {
   if (o->C != o->IC) {
      fl_apply_downmix(o->dmx, o->raw, o->input, o->n, o->IC, o->C);
      fl_ope_write_float(o->e, o->input, o->n);
   } else fl_ope_write_float(o->e, o->raw, o->n);
   o->n = 0;
}

static int fl_opus_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_opus_sink *o = (fl_opus_sink*)s;
   int frames_in = n;
   unsigned short cw = fl_x87_enter();
   while (n > 0) {
      int k = FL_MIN(n, 960-o->n);
      fl_opus_convert(o, pcm, k);
      o->n += k;
      pcm += (size_t)k*o->IC;
      n -= k;
      if (o->n == 960) fl_opus_flush_read(o);
   }
   fl_x87_leave(cw);
   o->base.out_bytes = ftell(o->e->ogg.out);
   o->base.out_frames += frames_in;
   return o->e->ogg.error ? FL_ERR_IO : FL_OK;
}

static int fl_opus_finish(fl_sink *s) {
   fl_opus_sink *o = (fl_opus_sink*)s;
   unsigned short cw = fl_x87_enter();
   int ret;
   fl_opus_flush_read(o);  /* the last, short read (possibly empty) */
   fl_ope_drain(o->e);
   fl_x87_leave(cw);
   ret = o->e->ogg.error ? FL_ERR_IO : FL_OK;
   if (fclose(o->e->ogg.out) != 0) ret = FL_ERR_IO;
   o->e->ogg.out = NULL;
   return ret;
}

static void fl_opus_destroy(fl_sink *s) {
   fl_opus_sink *o = (fl_opus_sink*)s;
   fl_ope *e = o->e;
   if (e) {
      if (e->ogg.out) fclose(e->ogg.out);
      if (e->st) fl_msenc_destroy(e->st);
      if (e->have_re) fl_ope_rs_free(&e->re);
      free(e->buffer);
      free(e->lpc_buffer);
      free(e->packet);
      free(e->ogg.buf);
      free(e->ogg.lacing);
      free(e->ogg.pages);
      free(e);
   }
   free(o->input);
   free(o->raw);
   free(o->path);
   free(o);
}

static fl_sink *fl_opus_sink_create(const char *path, const fl_options *opt, int *err) {
   fl_opus_sink *o = (fl_opus_sink*)calloc(1, sizeof(*o));
   if (!o || !(o->path = (char*)malloc(strlen(path)+1))) {
      free(o);
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   strcpy(o->path, path);
   o->opt = *opt;
   o->base.start = fl_opus_start;
   o->base.write = fl_opus_write;
   o->base.finish = fl_opus_finish;
   o->base.destroy = fl_opus_destroy;
   return &o->base;
}

/* ---- Public packet encoder API (48 kHz float input) ---- */
struct fl_opus_encoder {
   fl_msenc *ms;
   int channels, family, streams, coupled, frame_size;
   unsigned char mapping[255];
};

FLDEF fl_opus_encoder *fl_opus_encoder_create(int channels, const fl_options *opt_in, int *error) {
   fl_options opt;
   fl_opus_encoder *enc;
   int err;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if (error) *error = FL_ERR_BAD_ARG;
   if (channels < 1 || channels > 255 || (opt.bitrate > 0 && opt.quality >= 0)) return NULL;
   enc = (fl_opus_encoder*)calloc(1, sizeof(*enc));
   if (!enc) {
      if (error) *error = FL_ERR_ALLOC;
      return NULL;
   }
   enc->channels = channels;
   enc->family = channels > 8 ? 255 : channels > 2;
   fl_msenc_layout(channels, enc->family, &enc->streams, &enc->coupled, enc->mapping);
   if (opt.bitrate <= 0) opt.bitrate = fl_default_bitrate(enc->streams, enc->coupled, 48000, opt.quality);
   if (opt.bitrate > 1024000*channels || opt.bitrate < 500) {
      free(enc);
      return NULL;
   }
   enc->ms = fl_msenc_create(channels, enc->family, &err);
   if (!enc->ms) {
      free(enc);
      if (error) *error = err;
      return NULL;
   }
   fl_msenc_setup(enc->ms, &opt, 24);
   enc->frame_size = fl_frame_samples(fl_msenc_get_duration(enc->ms));
   if (error) *error = FL_OK;
   return enc;
}

FLDEF void fl_opus_encoder_destroy(fl_opus_encoder *enc) {
   if (!enc) return;
   fl_msenc_destroy(enc->ms);
   free(enc);
}

FLDEF int fl_opus_encoder_frame_size(const fl_opus_encoder *enc) { return enc->frame_size; }
FLDEF int fl_opus_encoder_lookahead(const fl_opus_encoder *enc) { return fl_msenc_lookahead(enc->ms); }

FLDEF int fl_opus_encoder_layout(const fl_opus_encoder *enc, int *family, int *streams, int *coupled, unsigned char *mapping) {
   if (family) *family = enc->family;
   if (streams) *streams = enc->streams;
   if (coupled) *coupled = enc->coupled;
   if (mapping) memcpy(mapping, enc->mapping, enc->channels);
   return enc->channels;
}

FLDEF int fl_opus_encode_float(fl_opus_encoder *enc, const float *pcm, unsigned char *packet, int max_bytes) {
   unsigned short cw = fl_x87_enter();
   int ret = fl_msenc_encode(enc->ms, pcm, enc->frame_size, packet, max_bytes);
   fl_x87_leave(cw);
   return ret < 0 ? FL_ERR_BAD_ARG : ret;
}

/* ======== enc_opus.c ======== */
/* ---- Opus encoder (opus_encoder.c) and single-stream multistream wrapper ---- */
#define FL_MODE_SILK_ONLY 1000
#define FL_MODE_HYBRID 1001
#define FL_MODE_CELT_ONLY 1002
#define FL_BW_NB 1101
#define FL_BW_MB 1102
#define FL_BW_WB 1103
#define FL_BW_SWB 1104
#define FL_BW_FB 1105
#define FL_AUTO -1000
#define FL_BITRATE_MAX -1
#define FL_SIGNAL_VOICE 3001
#define FL_SIGNAL_MUSIC 3002
#define FL_MAX_ENCODER_BUFFER 480
#define FL_VAD_NO_DECISION -1

/* SILK encoder control (silk_EncControlStruct subset used by the Opus layer). */
typedef struct {
   int nChannelsAPI, nChannelsInternal, API_sampleRate, maxInternalSampleRate, minInternalSampleRate;
   int desiredInternalSampleRate, payloadSize_ms, bitRate, packetLossPercentage, complexity, useInBandFEC;
   int LBRR_coded, useDTX, useCBR, maxBits, toMono, opusCanSwitch, reducedDependency, internalSampleRate;
   int allowBandwidthSwitch, inWBmodeWithoutVariableLP, stereoWidth_Q14, switchReady, signalType, offset;
} fl_silk_ctrl;

typedef struct fl_silk_enc fl_silk_enc;
static int fl_silk_init(fl_silk_enc *st, int channels, fl_silk_ctrl *status);
static int fl_silk_encode(fl_silk_enc *st, fl_silk_ctrl *ctrl, const float *pcm, int nSamples, ec_ctx *enc,
      fl_i32 *nBytes, int prefillFlag, int activity);
static int fl_silk_hp_smth1(fl_silk_enc *st);
static fl_silk_enc *fl_silk_create(void);

typedef struct { float XX, XY, YY, smoothed_width, max_follower; } fl_width_state;

typedef struct {
   fl_silk_enc *silk;
   fl_celt_enc celt;
   fl_silk_ctrl silk_mode;
   int channels, delay_compensation, force_channels, signal_type, user_bandwidth, max_bandwidth;
   int user_forced_mode, voice_ratio, use_vbr, vbr_constraint, variable_duration;
   fl_i32 bitrate_bps, user_bitrate_bps;
   int lsb_depth, encoder_buffer, use_dtx;
   int lfe;                      /* LFE stream of a surround encoder */
   const float *energy_masking;  /* surround masking (21 bands per channel), NULL if none */
   fl_tonality_state analysis;
   /* reset from here */
   int stream_channels;
   fl_i16 hybrid_stereo_width_Q14;
   fl_i32 variable_HP_smth2_Q15;
   float prev_HB_gain, hp_mem[4];
   int mode, prev_mode, prev_channels, prev_framesize, bandwidth, auto_bandwidth, silk_bw_switch, first;
   fl_width_state width_mem;
   int detected_bandwidth, nb_no_activity_ms_Q1;
   float peak_signal_energy;
   int nonfinal_frame;
   fl_u32 rangeFinal;
   float delay_buffer[FL_MAX_ENCODER_BUFFER*2];
} fl_opus_enc;

static void fl_opus_enc_init(fl_opus_enc *st, int channels) {
   fl_silk_enc *silk = st->silk;
   memset(st, 0, sizeof(*st));
   st->silk = silk;
   st->stream_channels = st->channels = channels;
   fl_silk_init(st->silk, channels, &st->silk_mode);
   st->silk_mode.nChannelsAPI = channels;
   st->silk_mode.nChannelsInternal = channels;
   st->silk_mode.API_sampleRate = 48000;
   st->silk_mode.maxInternalSampleRate = 16000;
   st->silk_mode.minInternalSampleRate = 8000;
   st->silk_mode.desiredInternalSampleRate = 16000;
   st->silk_mode.payloadSize_ms = 20;
   st->silk_mode.bitRate = 25000;
   st->silk_mode.packetLossPercentage = 0;
   st->silk_mode.complexity = 9;
   st->silk_mode.useInBandFEC = 0;
   st->silk_mode.useDTX = 0;
   st->silk_mode.useCBR = 0;
   st->silk_mode.reducedDependency = 0;
   fl_celt_enc_init(&st->celt, channels);
   st->celt.complexity = 9;
   st->use_vbr = 1;
   st->vbr_constraint = 1;
   st->user_bitrate_bps = FL_AUTO;
   st->bitrate_bps = 3000+48000*channels;
   st->signal_type = FL_AUTO;
   st->user_bandwidth = FL_AUTO;
   st->max_bandwidth = FL_BW_FB;
   st->force_channels = FL_AUTO;
   st->user_forced_mode = FL_AUTO;
   st->voice_ratio = -1;
   st->encoder_buffer = 480;
   st->lsb_depth = 24;
   st->variable_duration = 5000;
   st->delay_compensation = 192;
   st->hybrid_stereo_width_Q14 = 1<<14;
   st->prev_HB_gain = 1;
   st->variable_HP_smth2_Q15 = silk_LSHIFT(silk_lin2log(60), 8);
   st->first = 1;
   st->mode = FL_MODE_HYBRID;
   st->bandwidth = FL_BW_FB;
   st->analysis.Fs = 48000;
   fl_tonality_reset(&st->analysis);
}

static unsigned char fl_gen_toc(int mode, int framerate, int bandwidth, int channels) {
   int period = 0;
   unsigned char toc;
   while (framerate < 400) {
      framerate <<= 1;
      period++;
   }
   if (mode == FL_MODE_SILK_ONLY) {
      toc = (bandwidth-FL_BW_NB)<<5;
      toc |= (period-2)<<3;
   } else if (mode == FL_MODE_CELT_ONLY) {
      int tmp = bandwidth-FL_BW_MB;
      if (tmp < 0) tmp = 0;
      toc = 0x80;
      toc |= tmp<<5;
      toc |= period<<3;
   } else {
      toc = 0x60;
      toc |= (bandwidth-FL_BW_SWB)<<4;
      toc |= (period-2)<<3;
   }
   toc |= (channels == 2)<<2;
   return toc;
}

static void fl_dc_reject(const float *in, float *out, float *hp_mem, int len, int channels) {
   int i;
   xf coef = XF(18.900001525878906f)/48000, coef2 = 1-coef; /* 6.3f*3 folded in float */
   if (channels == 2) {
      xf m0 = hp_mem[0], m2 = hp_mem[2];
      for (i = 0; i < len; i++) {
         float x0 = in[2*i], x1 = in[2*i+1];
         out[2*i] = (float)(x0-m0);
         out[2*i+1] = (float)(x1-m2);
         m0 = (coef*x0+1e-30f)+coef2*m0;
         m2 = (coef*x1+1e-30f)+coef2*m2;
      }
      hp_mem[0] = (float)m0;
      hp_mem[2] = (float)m2;
   } else {
      xf m0 = hp_mem[0];
      for (i = 0; i < len; i++) {
         float x = in[i];
         out[i] = (float)(x-m0);
         m0 = (coef*x+1e-30f)+coef2*m0;
      }
      hp_mem[0] = (float)m0;
   }
}

static void fl_stereo_fade(float *buf, int g1_q14, int g2_q14, int frame_size, int channels) {
   int i;
   xf g1 = 1-XF(1.f/16384)*g1_q14, g2 = 1-XF(1.f/16384)*g2_q14;
   for (i = 0; i < CELT_OVERLAP; i++) {
      xf w = XF(fl_window120[i])*fl_window120[i], g, diff;
      g = w*g2+(1-w)*g1;
      diff = (XF(buf[i*channels])-buf[i*channels+1])*.5f;
      diff = g*diff;
      buf[i*channels] = (float)(buf[i*channels]-diff);
      buf[i*channels+1] = (float)(buf[i*channels+1]+diff);
   }
   for (; i < frame_size; i++) {
      xf diff = (XF(buf[i*channels])-buf[i*channels+1])*.5f;
      diff = diff*g2;
      buf[i*channels] = (float)(buf[i*channels]-diff);
      buf[i*channels+1] = (float)(buf[i*channels+1]+diff);
   }
}

static void fl_gain_fade(const float *in, float *out, float g1, float g2, int overlap, int frame_size, int channels) {
   int i, c;
   for (i = 0; i < overlap; i++) {
      xf w = XF(fl_window120[i])*fl_window120[i], g = w*g2+(1-w)*g1;
      for (c = 0; c < channels; c++) out[i*channels+c] = (float)(g*in[i*channels+c]);
   }
   for (c = 0; c < channels; c++)
      for (i = overlap; i < frame_size; i++) out[i*channels+c] = g2*in[i*channels+c];
}

static float fl_compute_stereo_width(const float *pcm, int frame_size, fl_width_state *mem) {
   int i, frame_rate = 48000/frame_size;
   float short_alpha, xx_f, yy_f, xy = 0;
   xf xx = 0, yy = 0, XX, XY, YY;
   short_alpha = (float)(XF(25.f)/IMAX(50, frame_rate));
   for (i = 0; i < frame_size-3; i += 4) {
      xf pxx, pxy, pyy;
      float x = pcm[2*i], y = pcm[2*i+1];
      pxx = XF(x)*x;
      pxy = XF(x)*y;
      pyy = XF(y)*y;
      x = pcm[2*i+2]; y = pcm[2*i+3];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      x = pcm[2*i+4]; y = pcm[2*i+5];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      x = pcm[2*i+6]; y = pcm[2*i+7];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      xx += pxx;
      xy = (float)(xy+pxy);
      yy += pyy;
   }
   xx_f = (float)xx;
   yy_f = (float)yy;
   if (!(xx < 1e9f) || xx_f != xx_f || !(yy < 1e9f) || yy_f != yy_f) xy = xx_f = yy_f = 0;
   XX = (XF(xx_f)-mem->XX)*short_alpha+mem->XX;
   XY = (1-XF(short_alpha))*mem->XY+XF(xy)*short_alpha;
   YY = (XF(yy_f)-mem->YY)*short_alpha+mem->YY;
   XX = 0 > XX ? 0 : XX;
   XY = 0 > XY ? 0 : XY;
   YY = 0 > YY ? 0 : YY;
   mem->XX = (float)XX;
   mem->XY = (float)XY;
   mem->YY = (float)YY;
   if ((XX > YY ? XX : YY) > 8e-4f) {
      float sqrt_xx, sqrt_yy, qrrt_xx, ldiff;
      xf qrrt_yy, xym, corr, s, width, sw, mf;
      sqrt_xx = (float)fl_m_sqrtf(mem->XX);
      sqrt_yy = (float)fl_m_sqrtf(mem->YY);
      qrrt_xx = (float)fl_m_sqrtf(sqrt_xx);
      qrrt_yy = fl_m_sqrtf(sqrt_yy);
      xym = XF(sqrt_xx)*sqrt_yy;
      xym = mem->XY < xym ? XF(mem->XY) : xym;
      mem->XY = (float)xym;
      corr = xym/(XF(1e-15f)+XF(sqrt_xx)*sqrt_yy);
      ldiff = (float)(fabsl(XF(qrrt_xx)-qrrt_yy)/((XF(1e-15f)+qrrt_xx)+qrrt_yy));
      s = fl_m_sqrtf((float)(1-corr*corr));
      s = 1 < s ? 1 : s;
      width = s*ldiff;
      sw = mem->smoothed_width+(width-mem->smoothed_width)/frame_rate;
      mem->smoothed_width = (float)sw;
      mf = mem->max_follower-XF(.02f)/frame_rate;
      mem->max_follower = (float)(mf > sw ? mf : sw);
   }
   {
      xf r = XF(20.f)*mem->max_follower;
      return (float)(1 < r ? 1 : r);
   }
}

static int fl_compute_silk_rate_for_hybrid(int rate, int bandwidth, int frame20ms, int vbr, int fec, int channels) {
   static const int rate_table[][5] = {
      {0, 0, 0, 0, 0}, {12000, 10000, 10000, 11000, 11000}, {16000, 13500, 13500, 15000, 15000},
      {20000, 16000, 16000, 18000, 18000}, {24000, 18000, 18000, 21000, 21000},
      {32000, 22000, 22000, 28000, 28000}, {64000, 38000, 38000, 50000, 50000}};
   int entry, i, N = 7, silk_rate;
   rate /= channels;
   entry = 1+frame20ms+2*fec;
   for (i = 1; i < N; i++)
      if (rate_table[i][0] > rate) break;
   if (i == N) {
      silk_rate = rate_table[i-1][entry];
      silk_rate += (rate-rate_table[i-1][0])/2;
   } else {
      fl_i32 lo = rate_table[i-1][entry], hi = rate_table[i][entry], x0 = rate_table[i-1][0], x1 = rate_table[i][0];
      silk_rate = (lo*(x1-rate)+hi*(rate-x0))/(x1-x0);
   }
   if (!vbr) silk_rate += 100;
   if (bandwidth == FL_BW_SWB) silk_rate += 300;
   silk_rate *= channels;
   if (channels == 2 && rate >= 12000) silk_rate -= 1000;
   return silk_rate;
}

static fl_i32 fl_compute_equiv_rate(fl_i32 bitrate, int channels, int frame_rate, int vbr, int mode, int complexity, int loss) {
   fl_i32 equiv = bitrate;
   if (frame_rate > 50) equiv -= (40*channels+20)*(frame_rate-50);
   if (!vbr) equiv -= equiv/12;
   equiv = equiv*(90+complexity)/100;
   if (mode == FL_MODE_SILK_ONLY || mode == FL_MODE_HYBRID) {
      if (complexity < 2) equiv = equiv*4/5;
      equiv -= equiv*loss/(6*loss+10);
   } else if (mode == FL_MODE_CELT_ONLY) {
      if (complexity < 5) equiv = equiv*9/10;
   } else {
      equiv -= equiv*loss/(12*loss+20);
   }
   return equiv;
}

static xf fl_frame_energy(const float *pcm, int len) { return XF(fl_inner_prod(pcm, pcm, len))/len; }

static int fl_compute_redundancy_bytes(fl_i32 max_data_bytes, fl_i32 bitrate_bps, int frame_rate, int channels) {
   int redundancy_bytes_cap, redundancy_bytes, base_bits = 40*channels+20;
   fl_i32 redundancy_rate, available_bits;
   redundancy_rate = bitrate_bps+base_bits*(200-frame_rate);
   redundancy_rate = 3*redundancy_rate/2;
   redundancy_bytes = redundancy_rate/1600;
   available_bits = max_data_bytes*8-2*base_bits;
   redundancy_bytes_cap = (available_bits*240/(240+48000/frame_rate)+base_bits)/8;
   redundancy_bytes = IMIN(redundancy_bytes, redundancy_bytes_cap);
   if (redundancy_bytes > 4+8*channels) redundancy_bytes = IMIN(257, redundancy_bytes);
   else redundancy_bytes = 0;
   return redundancy_bytes;
}

/* Pads a single-frame packet to new_len bytes (opus_packet_pad for one frame). */
static int fl_packet_pad(unsigned char *data, int len, int new_len) {
   int pad, i;
   unsigned char frame[1276];
   if (len == new_len) return 0;
   if (len > new_len || (data[0]&3) != 0) return -1;
   memcpy(frame, data+1, len-1);
   pad = new_len-len-1;
   data[0] |= 3;
   data[1] = (unsigned char)(1|(pad > 0 ? 0x40 : 0));
   i = 2;
   if (pad > 0) {
      int nb255 = (pad-1)/255;
      memset(data+i, 255, nb255);
      i += nb255;
      data[i++] = (unsigned char)(pad-1-255*nb255);
   }
   memcpy(data+i, frame, len-1);
   memset(data+i+len-1, 0, new_len-(i+len-1));
   return 0;
}

static int fl_encode_size(int size, unsigned char *data) {
   if (size < 252) {
      data[0] = (unsigned char)size;
      return 1;
   }
   data[0] = (unsigned char)(252+(size&3));
   data[1] = (unsigned char)((size-data[0])>>2);
   return 2;
}

/* Writes frames as one packet; self_delimited adds the last frame size (multistream),
   pad fills maxlen (CBR). Returns the length or <0. */
static fl_i32 fl_repacketize(unsigned char toc, unsigned char *const *frames, const int *lens, int count,
      unsigned char *data, fl_i32 maxlen, int self_delimited, int pad) {
   fl_i32 total = self_delimited ? 1+(lens[count-1] >= 252) : 0;
   unsigned char *ptr = data;
   int k;
   toc &= 0xFC;
   if (count == 1) {
      total += lens[0]+1;
      if (total > maxlen) return -2;
      *ptr++ = toc;
   } else if (count == 2) {
      if (lens[1] == lens[0]) {
         total += 2*lens[0]+1;
         if (total > maxlen) return -2;
         *ptr++ = toc|1;
      } else {
         total += lens[0]+lens[1]+2+(lens[0] >= 252);
         if (total > maxlen) return -2;
         *ptr++ = toc|2;
         ptr += fl_encode_size(lens[0], ptr);
      }
   }
   if (count > 2 || (pad && total < maxlen)) {
      int vbr = 0;
      fl_i32 pad_amount;
      ptr = data;
      total = self_delimited ? 1+(lens[count-1] >= 252) : 0;
      for (k = 1; k < count; k++)
         if (lens[k] != lens[0]) vbr = 1;
      if (vbr) {
         total += 2;
         for (k = 0; k < count-1; k++) total += 1+(lens[k] >= 252)+lens[k];
         total += lens[count-1];
         if (total > maxlen) return -2;
         *ptr++ = toc|3;
         *ptr++ = (unsigned char)(count|0x80);
      } else {
         total += count*lens[0]+2;
         if (total > maxlen) return -2;
         *ptr++ = toc|3;
         *ptr++ = (unsigned char)count;
      }
      pad_amount = pad ? maxlen-total : 0;
      if (pad_amount != 0) {
         fl_i32 nb_255s = (pad_amount-1)/255;
         data[1] |= 0x40;
         if (total+nb_255s+1 > maxlen) return -2;
         for (k = 0; k < nb_255s; k++) *ptr++ = 255;
         *ptr++ = (unsigned char)(pad_amount-255*nb_255s-1);
         total += pad_amount;
      }
      if (vbr)
         for (k = 0; k < count-1; k++) ptr += fl_encode_size(lens[k], ptr);
   }
   if (self_delimited) ptr += fl_encode_size(lens[count-1], ptr);
   for (k = 0; k < count; k++) {
      memmove(ptr, frames[k], lens[k]);
      ptr += lens[k];
   }
   if (pad)
      while (ptr < data+maxlen) *ptr++ = 0;
   return total;
}

/* OPUS_SET_BITRATE on the CELT encoder: values <= 500 are rejected and leave it unchanged. */
static void fl_celt_set_bitrate(fl_celt_enc *celt, fl_i32 v) {
   if (v <= 500 && v != FL_BITRATE_MAX) return;
   celt->bitrate = IMIN(v, 750000*celt->channels);
}

static void fl_celt_set_analysis(fl_celt_enc *celt, const fl_analysis_info *info) {
   if (info) celt->analysis = *info;
}

static fl_i32 fl_opus_encode_frame(fl_opus_enc *st, const float *pcm, int frame_size, unsigned char *data,
      fl_i32 orig_max_data_bytes, fl_analysis_info *analysis_info, int is_silence, int redundancy, int celt_to_silk,
      int prefill, fl_i32 equiv_rate, int to_celt) {
   fl_celt_enc *celt = &st->celt;
   int i, ret = 0, max_data_bytes, bits_target, start_band = 0, redundancy_bytes = 0, nb_compr_bytes;
   int frame_rate, curr_bandwidth, total_buffer, activity = FL_VAD_NO_DECISION, hp_freq_smth1;
   fl_i32 nBytes;
   fl_u32 redundant_rng = 0;
   ec_ctx enc;
   float HB_gain;
   float pcm_buf[(FL_MAX_ENCODER_BUFFER+2880)*2], tmp_prefill[2*120];
   max_data_bytes = IMIN(orig_max_data_bytes, 1276);
   st->rangeFinal = 0;
   curr_bandwidth = st->bandwidth;
   total_buffer = st->delay_compensation;
   frame_rate = 48000/frame_size;
   if (is_silence) activity = !is_silence;
   else if (analysis_info->valid) {
      activity = analysis_info->activity_probability >= .1f;
      if (!activity) activity = st->peak_signal_energy < XF(316.23f)*fl_frame_energy(pcm, frame_size*st->channels);
   } else if (st->mode == FL_MODE_CELT_ONLY) {
      activity = st->peak_signal_energy < (fl_frame_energy(pcm, frame_size*st->channels)*.5f)*316.23f;
   }
   if (st->silk_bw_switch) {
      redundancy = 1;
      celt_to_silk = 1;
      st->silk_bw_switch = 0;
      prefill = 2;
   }
   if (st->mode == FL_MODE_CELT_ONLY) redundancy = 0;
   if (redundancy) {
      redundancy_bytes = fl_compute_redundancy_bytes(max_data_bytes, st->bitrate_bps, frame_rate, st->stream_channels);
      if (redundancy_bytes == 0) redundancy = 0;
   }
   bits_target = IMIN(8*(max_data_bytes-redundancy_bytes), st->bitrate_bps*6/(6*48000/frame_size))-8;
   data += 1;
   ec_enc_init(&enc, data, orig_max_data_bytes-1);
   memcpy(pcm_buf, &st->delay_buffer[(st->encoder_buffer-total_buffer)*st->channels], total_buffer*st->channels*sizeof(float));
   if (st->mode == FL_MODE_CELT_ONLY) hp_freq_smth1 = silk_LSHIFT(silk_lin2log(60), 8);
   else hp_freq_smth1 = fl_silk_hp_smth1(st->silk);
   st->variable_HP_smth2_Q15 = silk_SMLAWB(st->variable_HP_smth2_Q15, hp_freq_smth1-st->variable_HP_smth2_Q15,
         SILK_FIX_CONST(0.015, 16));
   fl_dc_reject(pcm, &pcm_buf[total_buffer*st->channels], st->hp_mem, frame_size, st->channels);
   {
      float sum = fl_inner_prod(&pcm_buf[total_buffer*st->channels], &pcm_buf[total_buffer*st->channels], frame_size*st->channels);
      if (!(sum < 1e9f) || sum != sum) {
         memset(&pcm_buf[total_buffer*st->channels], 0, frame_size*st->channels*sizeof(float));
         st->hp_mem[0] = st->hp_mem[1] = st->hp_mem[2] = st->hp_mem[3] = 0;
      }
   }
   HB_gain = 1;
   if (st->mode != FL_MODE_CELT_ONLY) {
      fl_i32 total_bitRate, celt_rate;
      const float *pcm_silk;
      total_bitRate = bits_target*(6*48000/frame_size)/6;
      if (st->mode == FL_MODE_HYBRID) {
         st->silk_mode.bitRate = fl_compute_silk_rate_for_hybrid(total_bitRate, curr_bandwidth, 48000 == 50*frame_size,
               st->use_vbr, st->silk_mode.LBRR_coded, st->stream_channels);
         if (!st->energy_masking) {
            celt_rate = total_bitRate-st->silk_mode.bitRate;
            HB_gain = (float)(1-XF(fl_e_exp2((float)(XF(-celt_rate)*(1.f/1024))))); /* SHR32 is a no-op in float */
         }
      } else {
         st->silk_mode.bitRate = total_bitRate;
      }
      if (st->energy_masking && st->use_vbr && !st->lfe) {
         /* surround masking for SILK; the sum stays in an x87 register */
         xf mask_sum = 0, masking_depth;
         fl_i32 rate_offset;
         int c, end = 17, srate = 16000;
         if (st->bandwidth == FL_BW_NB) {
            end = 13;
            srate = 8000;
         } else if (st->bandwidth == FL_BW_MB) {
            end = 15;
            srate = 12000;
         }
         for (c = 0; c < st->channels; c++)
            for (i = 0; i < end; i++) {
               float m = st->energy_masking[21*c+i];
               xf mask;
               m = m < .5f ? m : .5f;
               m = m > -2.f ? m : -2.f;
               mask = m;
               if (mask > 0) mask = XF(.5f)*mask;
               mask_sum += mask;
            }
         masking_depth = mask_sum/end*st->channels+XF(.2f);
         rate_offset = (fl_i32)(float)(masking_depth*srate);
         rate_offset = FL_MAX(rate_offset, -2*st->silk_mode.bitRate/3);
         if (st->bandwidth == FL_BW_SWB || st->bandwidth == FL_BW_FB) st->silk_mode.bitRate += 3*rate_offset/5;
         else st->silk_mode.bitRate += rate_offset;
      }
      st->silk_mode.payloadSize_ms = 1000*frame_size/48000;
      st->silk_mode.nChannelsAPI = st->channels;
      st->silk_mode.nChannelsInternal = st->stream_channels;
      if (curr_bandwidth == FL_BW_NB) st->silk_mode.desiredInternalSampleRate = 8000;
      else if (curr_bandwidth == FL_BW_MB) st->silk_mode.desiredInternalSampleRate = 12000;
      else st->silk_mode.desiredInternalSampleRate = 16000;
      st->silk_mode.minInternalSampleRate = st->mode == FL_MODE_HYBRID ? 16000 : 8000;
      st->silk_mode.maxInternalSampleRate = 16000;
      if (st->mode == FL_MODE_SILK_ONLY) {
         fl_i32 effective_max_rate = max_data_bytes*8*(6*48000/frame_size)/6;
         if (frame_rate > 50) effective_max_rate = effective_max_rate*2/3;
         if (effective_max_rate < 8000) {
            st->silk_mode.maxInternalSampleRate = 12000;
            st->silk_mode.desiredInternalSampleRate = IMIN(12000, st->silk_mode.desiredInternalSampleRate);
         }
         if (effective_max_rate < 7000) {
            st->silk_mode.maxInternalSampleRate = 8000;
            st->silk_mode.desiredInternalSampleRate = IMIN(8000, st->silk_mode.desiredInternalSampleRate);
         }
      }
      st->silk_mode.useCBR = !st->use_vbr;
      st->silk_mode.maxBits = (max_data_bytes-1)*8;
      if (redundancy && redundancy_bytes >= 2) {
         st->silk_mode.maxBits -= redundancy_bytes*8+1;
         if (st->mode == FL_MODE_HYBRID) st->silk_mode.maxBits -= 20;
      }
      if (st->silk_mode.useCBR) {
         if (st->mode == FL_MODE_HYBRID) {
            fl_i16 other_bits = (fl_i16)IMAX(0, st->silk_mode.maxBits-st->silk_mode.bitRate*frame_size/48000);
            st->silk_mode.maxBits = IMAX(0, st->silk_mode.maxBits-other_bits*3/4);
            st->silk_mode.useCBR = 0;
         }
      } else if (st->mode == FL_MODE_HYBRID) {
         fl_i32 maxBitRate = fl_compute_silk_rate_for_hybrid(st->silk_mode.maxBits*48000/frame_size, curr_bandwidth,
               48000 == 50*frame_size, st->use_vbr, st->silk_mode.LBRR_coded, st->stream_channels);
         st->silk_mode.maxBits = maxBitRate*6/(6*48000/frame_size);
      }
      if (prefill) {
         fl_i32 zero = 0;
         int prefill_offset = st->channels*(st->encoder_buffer-st->delay_compensation-48000/400);
         fl_gain_fade(st->delay_buffer+prefill_offset, st->delay_buffer+prefill_offset, 0, 1, CELT_OVERLAP, 48000/400, st->channels);
         memset(st->delay_buffer, 0, prefill_offset*sizeof(float));
         fl_silk_encode(st->silk, &st->silk_mode, st->delay_buffer, st->encoder_buffer, NULL, &zero, prefill, activity);
         st->silk_mode.opusCanSwitch = 0;
      }
      pcm_silk = pcm_buf+total_buffer*st->channels;
      ret = fl_silk_encode(st->silk, &st->silk_mode, pcm_silk, frame_size, &enc, &nBytes, 0, activity);
      if (ret) return -3;
      if (st->mode == FL_MODE_SILK_ONLY) {
         if (st->silk_mode.internalSampleRate == 8000) curr_bandwidth = FL_BW_NB;
         else if (st->silk_mode.internalSampleRate == 12000) curr_bandwidth = FL_BW_MB;
         else if (st->silk_mode.internalSampleRate == 16000) curr_bandwidth = FL_BW_WB;
      }
      st->silk_mode.opusCanSwitch = st->silk_mode.switchReady && !st->nonfinal_frame;
      if (activity == FL_VAD_NO_DECISION) activity = st->silk_mode.signalType != 0;
      if (nBytes == 0) {
         st->rangeFinal = 0;
         data[-1] = fl_gen_toc(st->mode, 48000/frame_size, curr_bandwidth, st->stream_channels);
         return 1;
      }
      if (st->silk_mode.opusCanSwitch) {
         redundancy_bytes = fl_compute_redundancy_bytes(max_data_bytes, st->bitrate_bps, frame_rate, st->stream_channels);
         redundancy = redundancy_bytes != 0;
         celt_to_silk = 0;
         st->silk_bw_switch = 1;
      }
   }
   {
      int endband = 21;
      switch (curr_bandwidth) {
      case FL_BW_NB: endband = 13; break;
      case FL_BW_MB: case FL_BW_WB: endband = 17; break;
      case FL_BW_SWB: endband = 19; break;
      case FL_BW_FB: endband = 21; break;
      }
      celt->end = endband;
      celt->stream_channels = st->stream_channels;
      celt->bitrate = FL_BITRATE_MAX;
   }
   if (st->mode != FL_MODE_SILK_ONLY) {
      int celt_pred = st->silk_mode.reducedDependency ? 0 : 2;
      celt->disable_pf = celt_pred <= 1;
      celt->force_intra = celt_pred == 0;
   }
   if (st->mode != FL_MODE_SILK_ONLY && st->mode != st->prev_mode && st->prev_mode > 0)
      memcpy(tmp_prefill, &st->delay_buffer[(st->encoder_buffer-total_buffer-48000/400)*st->channels],
            st->channels*48000/400*sizeof(float));
   if (st->channels*(st->encoder_buffer-(frame_size+total_buffer)) > 0) {
      memmove(st->delay_buffer, &st->delay_buffer[st->channels*frame_size],
            st->channels*(st->encoder_buffer-frame_size-total_buffer)*sizeof(float));
      memcpy(&st->delay_buffer[st->channels*(st->encoder_buffer-frame_size-total_buffer)], &pcm_buf[0],
            (frame_size+total_buffer)*st->channels*sizeof(float));
   } else {
      memcpy(st->delay_buffer, &pcm_buf[(frame_size+total_buffer-st->encoder_buffer)*st->channels],
            st->encoder_buffer*st->channels*sizeof(float));
   }
   if (st->prev_HB_gain < 1 || HB_gain < 1)
      fl_gain_fade(pcm_buf, pcm_buf, st->prev_HB_gain, HB_gain, CELT_OVERLAP, frame_size, st->channels);
   st->prev_HB_gain = HB_gain;
   if (st->mode != FL_MODE_HYBRID || st->stream_channels == 1) {
      if (equiv_rate > 32000) st->silk_mode.stereoWidth_Q14 = 16384;
      else if (equiv_rate < 16000) st->silk_mode.stereoWidth_Q14 = 0;
      else st->silk_mode.stereoWidth_Q14 = 16384-2048*(fl_i32)(32000-equiv_rate)/(equiv_rate-14000);
   }
   if (!st->energy_masking && st->channels == 2) {
      if (st->hybrid_stereo_width_Q14 < (1<<14) || st->silk_mode.stereoWidth_Q14 < (1<<14)) {
         fl_stereo_fade(pcm_buf, st->hybrid_stereo_width_Q14, (fl_i16)st->silk_mode.stereoWidth_Q14, frame_size, st->channels);
         st->hybrid_stereo_width_Q14 = (fl_i16)st->silk_mode.stereoWidth_Q14;
      }
   }
   if (st->mode != FL_MODE_CELT_ONLY && ec_tell(&enc)+17+20*(st->mode == FL_MODE_HYBRID) <= 8*(max_data_bytes-1)) {
      if (st->mode == FL_MODE_HYBRID) ec_enc_bit_logp(&enc, redundancy, 12);
      if (redundancy) {
         int max_redundancy;
         ec_enc_bit_logp(&enc, celt_to_silk, 1);
         if (st->mode == FL_MODE_HYBRID) max_redundancy = (max_data_bytes-1)-((ec_tell(&enc)+8+3+7)>>3);
         else max_redundancy = (max_data_bytes-1)-((ec_tell(&enc)+7)>>3);
         redundancy_bytes = IMIN(max_redundancy, redundancy_bytes);
         redundancy_bytes = IMIN(257, IMAX(2, redundancy_bytes));
         if (st->mode == FL_MODE_HYBRID) ec_enc_uint(&enc, redundancy_bytes-2, 256);
      }
   } else redundancy = 0;
   if (!redundancy) {
      st->silk_bw_switch = 0;
      redundancy_bytes = 0;
   }
   if (st->mode != FL_MODE_CELT_ONLY) start_band = 17;
   if (st->mode == FL_MODE_SILK_ONLY) {
      ret = (ec_tell(&enc)+7)>>3;
      ec_enc_done(&enc);
      nb_compr_bytes = ret;
   } else {
      nb_compr_bytes = (max_data_bytes-1)-redundancy_bytes;
      ec_enc_shrink(&enc, nb_compr_bytes);
   }
   if (redundancy || st->mode != FL_MODE_SILK_ONLY) fl_celt_set_analysis(celt, analysis_info);
   if (st->mode == FL_MODE_HYBRID) {
      celt->silk_signal_type = st->silk_mode.signalType;
      celt->silk_offset = st->silk_mode.offset;
   }
   if (redundancy && celt_to_silk) {
      int err;
      celt->start = 0;
      celt->vbr = 0;
      celt->bitrate = FL_BITRATE_MAX;
      err = fl_celt_encode(celt, pcm_buf, 48000/200, data+nb_compr_bytes, redundancy_bytes, NULL);
      if (err < 0) return -3;
      redundant_rng = celt->rng;
      fl_celt_enc_reset(celt);
   }
   celt->start = start_band;
   data[-1] = 0;
   if (st->mode != FL_MODE_SILK_ONLY) {
      celt->vbr = st->use_vbr;
      if (st->mode == FL_MODE_HYBRID) {
         if (st->use_vbr) {
            fl_celt_set_bitrate(celt, st->bitrate_bps-st->silk_mode.bitRate);
            celt->constrained_vbr = 0;
         }
      } else if (st->use_vbr) {
         celt->vbr = 1;
         celt->constrained_vbr = st->vbr_constraint;
         fl_celt_set_bitrate(celt, st->bitrate_bps);
      }
      if (st->mode != st->prev_mode && st->prev_mode > 0) {
         unsigned char dummy[2];
         fl_celt_enc_reset(celt);
         fl_celt_encode(celt, tmp_prefill, 48000/400, dummy, 2, NULL);
         celt->disable_pf = 1;
         celt->force_intra = 1;
      }
      if (ec_tell(&enc) <= 8*nb_compr_bytes) {
         ret = fl_celt_encode(celt, pcm_buf, frame_size, NULL, nb_compr_bytes, &enc);
         if (ret < 0) return -3;
         if (redundancy && celt_to_silk && st->mode == FL_MODE_HYBRID && nb_compr_bytes != ret) {
            memmove(data+ret, data+nb_compr_bytes, redundancy_bytes);
            nb_compr_bytes = ret+redundancy_bytes;
         }
      }
      st->rangeFinal = celt->rng;
   } else {
      st->rangeFinal = enc.rng;
   }
   if (redundancy && !celt_to_silk) {
      unsigned char dummy[2];
      int err, N2 = 48000/200, N4 = 48000/400;
      fl_celt_enc_reset(celt);
      celt->start = 0;
      celt->disable_pf = 1;
      celt->force_intra = 1;
      celt->vbr = 0;
      celt->bitrate = FL_BITRATE_MAX;
      if (st->mode == FL_MODE_HYBRID) {
         nb_compr_bytes = ret;
         ec_enc_shrink(&enc, nb_compr_bytes);
      }
      fl_celt_encode(celt, pcm_buf+st->channels*(frame_size-N2-N4), N4, dummy, 2, NULL);
      err = fl_celt_encode(celt, pcm_buf+st->channels*(frame_size-N2), N2, data+nb_compr_bytes, redundancy_bytes, NULL);
      if (err < 0) return -3;
      redundant_rng = celt->rng;
   }
   data--;
   data[0] |= fl_gen_toc(st->mode, 48000/frame_size, curr_bandwidth, st->stream_channels);
   st->rangeFinal ^= redundant_rng;
   st->prev_mode = to_celt ? FL_MODE_CELT_ONLY : st->mode;
   st->prev_channels = st->stream_channels;
   st->prev_framesize = frame_size;
   st->first = 0;
   st->nb_no_activity_ms_Q1 = 0;
   if (ec_tell(&enc) > (max_data_bytes-1)*8) {
      if (max_data_bytes < 2) return -2;
      data[1] = 0;
      ret = 1;
      st->rangeFinal = 0;
   } else if (st->mode == FL_MODE_SILK_ONLY && !redundancy) {
      while (ret > 2 && data[ret] == 0) ret--;
   }
   ret += 1+redundancy_bytes;
   if (!st->use_vbr) {
      if (fl_packet_pad(data, ret, orig_max_data_bytes) != 0) return -3;
      ret = orig_max_data_bytes;
   }
   (void)i;
   return ret;
}

static fl_i32 fl_opus_encode_native(fl_opus_enc *st, const float *pcm, int frame_size, unsigned char *data,
      fl_i32 out_data_bytes, int lsb_depth, const float *analysis_pcm, fl_i32 analysis_size, int c1, int c2,
      int analysis_channels) {
   int i, redundancy = 0, celt_to_silk = 0, to_celt = 0, voice_est, frame_rate, prefill = 0, is_silence;
   fl_i32 equiv_rate, max_rate, max_data_bytes, cbr_bytes = -1;
   float stereo_width;
   fl_analysis_info analysis_info;
   int analysis_read_pos_bak = -1, analysis_read_subframe_bak = -1;
   max_data_bytes = IMIN(1276*6, out_data_bytes);
   st->rangeFinal = 0;
   if (frame_size <= 0 || max_data_bytes <= 0) return FL_ERR_BAD_ARG;
   lsb_depth = IMIN(lsb_depth, st->lsb_depth);
   is_silence = fl_e_maxabs(pcm, frame_size*st->channels) <= XF(1)/(1<<lsb_depth);
   analysis_info.valid = 0;
   if (st->silk_mode.complexity >= 7) {
      analysis_read_pos_bak = st->analysis.read_pos;
      analysis_read_subframe_bak = st->analysis.read_subframe;
      fl_run_analysis(&st->analysis, analysis_pcm, analysis_size, frame_size, c1, c2, analysis_channels, lsb_depth,
            &analysis_info);
   } else if (st->analysis.initialized) {
      fl_tonality_reset(&st->analysis);
   }
   if (!is_silence) st->voice_ratio = -1;
   st->detected_bandwidth = 0;
   if (analysis_info.valid) {
      int analysis_bandwidth;
      if (st->signal_type == FL_AUTO) {
         float prob;
         if (st->prev_mode == 0) prob = analysis_info.music_prob;
         else if (st->prev_mode == FL_MODE_CELT_ONLY) prob = analysis_info.music_prob_max;
         else prob = analysis_info.music_prob_min;
         st->voice_ratio = (int)floor((double)((1-XF(prob))*100.f+.5f));
      }
      analysis_bandwidth = analysis_info.bandwidth;
      if (analysis_bandwidth <= 12) st->detected_bandwidth = FL_BW_NB;
      else if (analysis_bandwidth <= 14) st->detected_bandwidth = FL_BW_MB;
      else if (analysis_bandwidth <= 16) st->detected_bandwidth = FL_BW_WB;
      else if (analysis_bandwidth <= 18) st->detected_bandwidth = FL_BW_SWB;
      else st->detected_bandwidth = FL_BW_FB;
   }
   if (!analysis_info.valid || analysis_info.activity_probability > .1f) {
      if (!is_silence) {
         float a = (float)(XF(.999f)*st->peak_signal_energy);
         xf e = fl_frame_energy(pcm, frame_size*st->channels);
         st->peak_signal_energy = a > e ? a : (float)e;
      }
   }
   if (st->channels == 2 && st->force_channels != 1) stereo_width = fl_compute_stereo_width(pcm, frame_size, &st->width_mem);
   else stereo_width = 0;
   {
      fl_i32 max_bitrate = max_data_bytes*8*(6*48000/frame_size)/6, user_bitrate;
      if (st->user_bitrate_bps == FL_AUTO) user_bitrate = 60*48000/frame_size+48000*st->channels;
      else if (st->user_bitrate_bps == FL_BITRATE_MAX) user_bitrate = 1500000;
      else user_bitrate = st->user_bitrate_bps;
      st->bitrate_bps = IMIN(user_bitrate, max_bitrate);
   }
   frame_rate = 48000/frame_size;
   if (!st->use_vbr) {
      cbr_bytes = IMIN((st->bitrate_bps*6/(6*48000/frame_size)+4)/8, max_data_bytes);
      st->bitrate_bps = cbr_bytes*8*(6*48000/frame_size)/6;
      max_data_bytes = IMAX(1, cbr_bytes);
   }
   if (max_data_bytes < 3 || st->bitrate_bps < 3*frame_rate*8 || (frame_rate < 50 && (max_data_bytes*frame_rate < 300 || st->bitrate_bps < 2400))) {
      int tocmode = st->mode, bw = st->bandwidth == 0 ? FL_BW_NB : st->bandwidth, ret;
      if (tocmode == 0) tocmode = FL_MODE_SILK_ONLY;
      if (frame_rate > 100) tocmode = FL_MODE_CELT_ONLY;
      if (tocmode == FL_MODE_SILK_ONLY && bw > FL_BW_WB) bw = FL_BW_WB;
      else if (tocmode == FL_MODE_CELT_ONLY && bw == FL_BW_MB) bw = FL_BW_NB;
      else if (tocmode == FL_MODE_HYBRID && bw <= FL_BW_SWB) bw = FL_BW_SWB;
      data[0] = fl_gen_toc(tocmode, frame_rate, bw, st->stream_channels);
      ret = 1;
      if (!st->use_vbr) {
         if (fl_packet_pad(data, ret, max_data_bytes) != 0) return -3;
         ret = max_data_bytes;
      }
      return ret;
   }
   max_rate = max_data_bytes*8*(6*48000/frame_size)/6;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->channels, frame_rate, st->use_vbr, 0,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   if (st->signal_type == FL_SIGNAL_VOICE) voice_est = 127;
   else if (st->signal_type == FL_SIGNAL_MUSIC) voice_est = 0;
   else if (st->voice_ratio >= 0) voice_est = IMIN(st->voice_ratio*327>>8, 115);
   else voice_est = 48;
   if (st->force_channels != FL_AUTO && st->channels == 2) {
      st->stream_channels = st->force_channels;
   } else if (st->channels == 2) {
      fl_i32 stereo_threshold = 17000+((voice_est*voice_est*(19000-17000))>>14);
      if (st->stream_channels == 2) stereo_threshold -= 1000;
      else stereo_threshold += 1000;
      st->stream_channels = equiv_rate > stereo_threshold ? 2 : 1;
   } else st->stream_channels = st->channels;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->stream_channels, frame_rate, st->use_vbr, 0,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   st->silk_mode.useDTX = st->use_dtx && !(analysis_info.valid || is_silence);
   if (st->user_forced_mode == FL_AUTO) {
      fl_i32 mode_voice, mode_music, threshold;
      xf a = 1-XF(stereo_width);
      mode_voice = (fl_i32)(float)(a*64000+XF(stereo_width)*44000);
      mode_music = (fl_i32)(float)(a*10000+XF(stereo_width)*10000);
      threshold = mode_music+((voice_est*voice_est*(mode_voice-mode_music))>>14);
      if (st->prev_mode == FL_MODE_CELT_ONLY) threshold -= 4000;
      else if (st->prev_mode > 0) threshold += 4000;
      st->mode = equiv_rate >= threshold ? FL_MODE_CELT_ONLY : FL_MODE_SILK_ONLY;
      if (st->silk_mode.useInBandFEC && st->silk_mode.packetLossPercentage > (128-voice_est)>>4) st->mode = FL_MODE_SILK_ONLY;
      if (st->silk_mode.useDTX && voice_est > 100) st->mode = FL_MODE_SILK_ONLY;
      if (max_data_bytes < (frame_rate > 50 ? 9000 : 6000)*6/(6*48000/frame_size)/8) st->mode = FL_MODE_CELT_ONLY;
   } else st->mode = st->user_forced_mode;
   if (st->mode != FL_MODE_CELT_ONLY && frame_size < 48000/100) st->mode = FL_MODE_CELT_ONLY;
   if (st->lfe) st->mode = FL_MODE_CELT_ONLY;
   if (st->prev_mode > 0 && ((st->mode != FL_MODE_CELT_ONLY && st->prev_mode == FL_MODE_CELT_ONLY) ||
         (st->mode == FL_MODE_CELT_ONLY && st->prev_mode != FL_MODE_CELT_ONLY))) {
      redundancy = 1;
      celt_to_silk = st->mode != FL_MODE_CELT_ONLY;
      if (!celt_to_silk) {
         if (frame_size >= 48000/100) {
            st->mode = st->prev_mode;
            to_celt = 1;
         } else redundancy = 0;
      }
   }
   if (st->stream_channels == 1 && st->prev_channels == 2 && st->silk_mode.toMono == 0 &&
         st->mode != FL_MODE_CELT_ONLY && st->prev_mode != FL_MODE_CELT_ONLY) {
      st->silk_mode.toMono = 1;
      st->stream_channels = 2;
   } else st->silk_mode.toMono = 0;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->stream_channels, frame_rate, st->use_vbr, st->mode,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   if (st->mode != FL_MODE_CELT_ONLY && st->prev_mode == FL_MODE_CELT_ONLY) {
      fl_silk_ctrl dummy;
      fl_silk_init(st->silk, st->channels, &dummy);
      prefill = 1;
   }
   if (st->mode == FL_MODE_CELT_ONLY || st->first || st->silk_mode.allowBandwidthSwitch) {
      static const fl_i32 voice_thr[8] = {9000, 700, 9000, 700, 13500, 1000, 14000, 2000};
      static const fl_i32 music_thr[8] = {9000, 700, 9000, 700, 11000, 1000, 12000, 2000};
      fl_i32 bandwidth_thresholds[8];
      int bandwidth = FL_BW_FB;
      for (i = 0; i < 8; i++)
         bandwidth_thresholds[i] = music_thr[i]+((voice_est*voice_est*(voice_thr[i]-music_thr[i]))>>14);
      do {
         int threshold = bandwidth_thresholds[2*(bandwidth-FL_BW_MB)], hysteresis = bandwidth_thresholds[2*(bandwidth-FL_BW_MB)+1];
         if (!st->first) {
            if (st->auto_bandwidth >= bandwidth) threshold -= hysteresis;
            else threshold += hysteresis;
         }
         if (equiv_rate >= threshold) break;
      } while (--bandwidth > FL_BW_NB);
      if (bandwidth == FL_BW_MB) bandwidth = FL_BW_WB;
      st->bandwidth = st->auto_bandwidth = bandwidth;
      if (!st->first && st->mode != FL_MODE_CELT_ONLY && !st->silk_mode.inWBmodeWithoutVariableLP && st->bandwidth > FL_BW_WB)
         st->bandwidth = FL_BW_WB;
   }
   if (st->bandwidth > st->max_bandwidth) st->bandwidth = st->max_bandwidth;
   if (st->user_bandwidth != FL_AUTO) st->bandwidth = st->user_bandwidth;
   if (st->mode != FL_MODE_CELT_ONLY && max_rate < 15000) st->bandwidth = IMIN(st->bandwidth, FL_BW_WB);
   if (st->detected_bandwidth && st->user_bandwidth == FL_AUTO) {
      int min_detected_bandwidth;
      if (equiv_rate <= 18000*st->stream_channels && st->mode == FL_MODE_CELT_ONLY) min_detected_bandwidth = FL_BW_NB;
      else if (equiv_rate <= 24000*st->stream_channels && st->mode == FL_MODE_CELT_ONLY) min_detected_bandwidth = FL_BW_MB;
      else if (equiv_rate <= 30000*st->stream_channels) min_detected_bandwidth = FL_BW_WB;
      else if (equiv_rate <= 44000*st->stream_channels) min_detected_bandwidth = FL_BW_SWB;
      else min_detected_bandwidth = FL_BW_FB;
      st->detected_bandwidth = IMAX(st->detected_bandwidth, min_detected_bandwidth);
      st->bandwidth = IMIN(st->bandwidth, st->detected_bandwidth);
   }
   st->silk_mode.LBRR_coded = 0;
   st->celt.lsb_depth = lsb_depth;
   if (st->mode == FL_MODE_CELT_ONLY && st->bandwidth == FL_BW_MB) st->bandwidth = FL_BW_WB;
   if (st->lfe) st->bandwidth = FL_BW_NB;
   {
      int curr_bandwidth = st->bandwidth;
      if (st->mode == FL_MODE_SILK_ONLY && curr_bandwidth > FL_BW_WB) st->mode = FL_MODE_HYBRID;
      if (st->mode == FL_MODE_HYBRID && curr_bandwidth <= FL_BW_WB) st->mode = FL_MODE_SILK_ONLY;
   }
   if ((frame_size > 48000/50 && st->mode != FL_MODE_SILK_ONLY) || frame_size > 3*48000/50) {
      /* Several frames repacketized into one packet. */
      int enc_frame_size, nb_frames, max_header_bytes, dtx_count = 0, bak_to_mono, nb = 0;
      fl_i32 repacketize_len, max_len_sum, tot_size = 0;
      unsigned char tmp_data[7680], *curr_data = tmp_data, *frames[6];
      int lens[6];
      if (st->mode == FL_MODE_SILK_ONLY) {
         if (frame_size == 2*48000/25) enc_frame_size = 48000/25;
         else if (frame_size == 3*48000/25) enc_frame_size = 3*48000/50;
         else enc_frame_size = 48000/50;
      } else enc_frame_size = 48000/50;
      nb_frames = frame_size/enc_frame_size;
      if (analysis_read_pos_bak != -1) {
         st->analysis.read_pos = analysis_read_pos_bak;
         st->analysis.read_subframe = analysis_read_subframe_bak;
      }
      max_header_bytes = nb_frames == 2 ? 3 : (2+(nb_frames-1)*2);
      if (st->use_vbr || st->user_bitrate_bps == FL_BITRATE_MAX) repacketize_len = out_data_bytes;
      else repacketize_len = IMIN(cbr_bytes, out_data_bytes);
      max_len_sum = nb_frames+repacketize_len-max_header_bytes;
      bak_to_mono = st->silk_mode.toMono;
      if (bak_to_mono) st->force_channels = 1;
      else st->prev_channels = st->stream_channels;
      for (i = 0; i < nb_frames; i++) {
         int first_frame, frame_to_celt, frame_redundancy, tmp_len;
         fl_i32 curr_max;
         first_frame = i == 0 || i == dtx_count;
         (void)first_frame;
         st->silk_mode.toMono = 0;
         st->nonfinal_frame = i < nb_frames-1;
         frame_to_celt = to_celt && i == nb_frames-1;
         frame_redundancy = redundancy && (frame_to_celt || (!to_celt && i == 0));
         curr_max = IMIN(st->bitrate_bps*6/(6*48000/enc_frame_size)/8, max_len_sum/nb_frames);
         curr_max = IMIN(max_len_sum-tot_size, curr_max);
         if (analysis_read_pos_bak != -1) fl_tonality_get_info(&st->analysis, &analysis_info, enc_frame_size);
         is_silence = fl_e_maxabs(pcm+i*(st->channels*enc_frame_size), enc_frame_size*st->channels) <= XF(1)/(1<<lsb_depth);
         tmp_len = fl_opus_encode_frame(st, pcm+i*(st->channels*enc_frame_size), enc_frame_size, curr_data, curr_max,
               &analysis_info, is_silence, frame_redundancy, celt_to_silk, prefill, equiv_rate, frame_to_celt);
         if (tmp_len < 0) return -3;
         if (tmp_len == 1) dtx_count++;
         /* opus_repacketizer_cat: strip the TOC of single-frame packets */
         if ((curr_data[0]&3) != 0) {
            /* padded (CBR) frame: code 3 with one frame and padding */
            int p = 2, pad = 0;
            if (curr_data[1]&0x40) {
               int b;
               do {
                  b = curr_data[p++];
                  pad += b == 255 ? 254 : b;
               } while (b == 255);
            }
            frames[nb] = curr_data+p;
            lens[nb] = tmp_len-p-pad;
         } else {
            frames[nb] = curr_data+1;
            lens[nb] = tmp_len-1;
         }
         if (nb == 0) data[0] = curr_data[0];
         nb++;
         tot_size += tmp_len;
         curr_data += tmp_len;
      }
      st->silk_mode.toMono = bak_to_mono;
      {
         int pad = !st->use_vbr && dtx_count != nb_frames;
         return fl_repacketize(data[0], frames, lens, nb, data, repacketize_len, 0, pad);
      }
   }
   (void)cbr_bytes;
   return fl_opus_encode_frame(st, pcm, frame_size, data, max_data_bytes, &analysis_info, is_silence, redundancy,
         celt_to_silk, prefill, equiv_rate, to_celt);
}

/* ---- Multistream encoder (libopus opus_multistream_encoder.c, float API) ---- */
struct fl_msenc {
   int channels, family, streams, coupled, lfe_stream, surround, variable_duration;
   fl_i32 bitrate_bps;
   unsigned char mapping[255];
   fl_opus_enc *enc;                 /* one encoder per stream */
   float *window_mem, *preemph_mem;  /* surround analysis state */
   float *bandSMR, *buf, bandLogE[42];
   unsigned char tmp[6*1275+12];
};

/* Stream layout of opus_multistream_surround_encoder_init() for families 0, 1 and 255. */
static int fl_msenc_layout(int channels, int family, int *streams, int *coupled, unsigned char *mapping) {
   static const unsigned char vorbis[8][10] = {
      {1, 0, 0}, {1, 1, 0, 1}, {2, 1, 0, 2, 1}, {2, 2, 0, 1, 2, 3}, {3, 2, 0, 4, 1, 2, 3},
      {4, 2, 0, 4, 1, 2, 3, 5}, {4, 3, 0, 4, 1, 2, 3, 5, 6}, {5, 3, 0, 6, 1, 2, 3, 4, 5, 7}};
   int i;
   if (channels < 1 || channels > 255) return FL_ERR_BAD_ARG;
   if (family == 0 && channels <= 2) {
      *streams = 1;
      *coupled = channels-1;
      for (i = 0; i < channels; i++) mapping[i] = (unsigned char)i;
   } else if (family == 1 && channels <= 8) {
      *streams = vorbis[channels-1][0];
      *coupled = vorbis[channels-1][1];
      for (i = 0; i < channels; i++) mapping[i] = vorbis[channels-1][2+i];
   } else if (family == 255) {
      *streams = channels;
      *coupled = 0;
      for (i = 0; i < channels; i++) mapping[i] = (unsigned char)i;
   } else return FL_ERR_UNSUPPORTED;
   return FL_OK;
}

static void fl_msenc_destroy(fl_msenc *st) {
   int s;
   if (!st) return;
   if (st->enc)
      for (s = 0; s < st->streams; s++) free(st->enc[s].silk);
   free(st->enc);
   free(st->window_mem);
   free(st->preemph_mem);
   free(st->bandSMR);
   free(st->buf);
   free(st);
}

static fl_msenc *fl_msenc_create(int channels, int family, int *err) {
   fl_msenc *st = (fl_msenc*)calloc(1, sizeof(*st));
   int s;
   *err = FL_ERR_ALLOC;
   if (!st) return NULL;
   *err = fl_msenc_layout(channels, family, &st->streams, &st->coupled, st->mapping);
   if (*err != FL_OK) {
      free(st);
      return NULL;
   }
   *err = FL_ERR_ALLOC;
   st->channels = channels;
   st->family = family;
   st->lfe_stream = family == 1 && channels >= 6 ? st->streams-1 : -1;
   st->surround = family == 1 && channels > 2;
   st->bitrate_bps = FL_AUTO;
   st->variable_duration = 5000;
   st->enc = (fl_opus_enc*)calloc(st->streams, sizeof(fl_opus_enc));
   st->bandSMR = (float*)calloc(21*channels, sizeof(float));
   st->buf = (float*)malloc(sizeof(float)*2*5760);
   if (st->surround) {
      st->window_mem = (float*)calloc(channels*CELT_OVERLAP, sizeof(float));
      st->preemph_mem = (float*)calloc(channels, sizeof(float));
   }
   if (!st->enc || !st->bandSMR || !st->buf || (st->surround && (!st->window_mem || !st->preemph_mem))) {
      fl_msenc_destroy(st);
      return NULL;
   }
   for (s = 0; s < st->streams; s++) {
      st->enc[s].silk = fl_silk_create();
      if (!st->enc[s].silk) {
         fl_msenc_destroy(st);
         return NULL;
      }
      fl_opus_enc_init(&st->enc[s], s < st->coupled ? 2 : 1);
      if (s == st->lfe_stream) st->enc[s].lfe = st->enc[s].celt.lfe = 1;
   }
   *err = FL_OK;
   return st;
}

static int fl_msenc_lookahead(fl_msenc *st) { (void)st; return 48000/400+192; }

/* The opusenc ctl sequence, forwarded to every stream encoder. */
static void fl_msenc_setup(fl_msenc *st, const fl_options *opt, int lsb_depth) {
   fl_i32 v = opt->bitrate;
   int s, duration;
   if (v != FL_AUTO && v != FL_BITRATE_MAX) v = IMIN(750000*st->channels, IMAX(500*st->channels, v));
   st->bitrate_bps = v;
   duration = opt->frame_ms_x10 == 25 ? 5001 : opt->frame_ms_x10 == 50 ? 5002 : opt->frame_ms_x10 == 100 ? 5003 :
         opt->frame_ms_x10 == 400 ? 5005 : opt->frame_ms_x10 == 600 ? 5006 : opt->frame_ms_x10 == 800 ? 5007 :
         opt->frame_ms_x10 == 1000 ? 5008 : opt->frame_ms_x10 == 1200 ? 5009 : 5004;
   st->variable_duration = duration;
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      e->use_vbr = opt->vbr;
      e->silk_mode.useCBR = 1-opt->vbr;
      if (opt->vbr) e->vbr_constraint = opt->cvbr;
      e->signal_type = opt->signal == 1 ? FL_SIGNAL_MUSIC : opt->signal == 2 ? FL_SIGNAL_VOICE : FL_AUTO;
      e->silk_mode.complexity = opt->complexity;
      e->celt.complexity = opt->complexity;
      e->silk_mode.packetLossPercentage = opt->expect_loss;
      e->celt.loss_rate = opt->expect_loss;
      e->celt.disable_inv = opt->no_phase_inv;
      e->lsb_depth = lsb_depth;
      e->variable_duration = duration;
   }
}

static int fl_msenc_get_duration(fl_msenc *st) { return st->variable_duration; }
static void fl_msenc_set_duration(fl_msenc *st, int request) {
   int s;
   st->variable_duration = request;
   for (s = 0; s < st->streams; s++) st->enc[s].variable_duration = request;
}

/* OPUS_SET_BITRATE on one stream encoder. */
static void fl_enc_set_bitrate(fl_opus_enc *e, fl_i32 v) {
   if (v != FL_AUTO && v != FL_BITRATE_MAX) {
      if (v <= 0) return;  /* rejected by the ctl */
      if (v <= 500) v = 500;
      else if (v > 750000*e->channels) v = 750000*e->channels;
   }
   e->user_bitrate_bps = v;
}

/* log2(2^a+2^b) approximation; 2*diff is rounded to float before floorf. */
static float fl_log_sum(float a, float b) {
   static const float diff_table[17] = {0.5000000f, 0.2924813f, 0.1609640f, 0.0849625f, 0.0437314f,
      0.0221971f, 0.0111839f, 0.0056136f, 0.0028123f};
   float mx, d2;
   xf diff;
   int low;
   if (a > b) {
      mx = a;
      diff = XF(a)-b;
   } else {
      mx = b;
      diff = XF(b)-a;
   }
   if (!(diff < 8.f)) return mx;
   d2 = (float)(diff+diff);
   low = (int)floorf(d2);
   return (float)((XF(diff_table[low])+mx)+(XF(d2)-low)*(XF(diff_table[low+1])-diff_table[low]));
}

/* surround_analysis(): per-channel band energies masked by the other channels. */
static void fl_surround_analysis(fl_msenc *st, const float *pcm, int frame_size) {
   int C = st->channels, c, i, LM, pos[8] = {0}, freq_size, nb_frames, frame;
   float *in, freq[960], bandE[21], tmpE[21], maskLogE[3][21], *bandLogE = st->bandSMR;
   xf channel_offset;
   for (LM = 0; LM < CELT_MAX_LM; LM++)
      if (CELT_SHORT_MDCT<<LM == frame_size) break;
   freq_size = CELT_SHORT_MDCT<<LM;
   nb_frames = frame_size/freq_size;
   in = (float*)malloc(sizeof(float)*(frame_size+CELT_OVERLAP));
   if (!in) return;
   /* channel_pos(): 0 don't mix, 1 left, 2 center, 3 right */
   if (C == 4) {
      pos[0] = 1; pos[1] = 3; pos[2] = 1; pos[3] = 3;
   } else if (C == 3 || C == 5 || C == 6) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 0;
   } else if (C == 7) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 2; pos[6] = 0;
   } else if (C == 8) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 1; pos[6] = 3; pos[7] = 0;
   }
   for (c = 0; c < 3; c++)
      for (i = 0; i < 21; i++) maskLogE[c][i] = -28.f;
   for (c = 0; c < C; c++) {
      memcpy(in, st->window_mem+c*CELT_OVERLAP, CELT_OVERLAP*sizeof(float));
      fl_e_preemphasis(pcm+c, in+CELT_OVERLAP, frame_size, C, &st->preemph_mem[c], 0);
      {
         float sum = (float)fl_e_inner_prod(in, in, frame_size+CELT_OVERLAP);
         if (!(sum < 1e18f) || sum != sum) {
            memset(in, 0, (frame_size+CELT_OVERLAP)*sizeof(float));
            st->preemph_mem[c] = 0;
         }
      }
      memset(bandE, 0, sizeof(bandE));
      for (frame = 0; frame < nb_frames; frame++) {
         fl_e_mdct_forward(in+freq_size*frame, freq, CELT_OVERLAP, CELT_MAX_LM-LM, 1);
         fl_e_band_energies(freq, tmpE, 21, 1, LM);
         for (i = 0; i < 21; i++) bandE[i] = FL_MAX(bandE[i], tmpE[i]);
      }
      fl_e_amp2log2(21, 21, bandE, bandLogE+21*c, 1);
      /* spreading: -6 dB/band going up, -12 dB/band going down */
      for (i = 1; i < 21; i++) {
         xf v = XF(bandLogE[21*c+i-1])-1;
         if (XF(bandLogE[21*c+i]) < v) bandLogE[21*c+i] = (float)v;
      }
      for (i = 19; i >= 0; i--) {
         xf v = XF(bandLogE[21*c+i+1])-2;
         if (XF(bandLogE[21*c+i]) < v) bandLogE[21*c+i] = (float)v;
      }
      if (pos[c] == 1) {
         for (i = 0; i < 21; i++) maskLogE[0][i] = fl_log_sum(maskLogE[0][i], bandLogE[21*c+i]);
      } else if (pos[c] == 3) {
         for (i = 0; i < 21; i++) maskLogE[2][i] = fl_log_sum(maskLogE[2][i], bandLogE[21*c+i]);
      } else if (pos[c] == 2) {
         for (i = 0; i < 21; i++) {
            float b = (float)(XF(bandLogE[21*c+i])-XF(.5f));
            maskLogE[0][i] = fl_log_sum(maskLogE[0][i], b);
            maskLogE[2][i] = fl_log_sum(maskLogE[2][i], b);
         }
      }
      memcpy(st->window_mem+c*CELT_OVERLAP, in+frame_size, CELT_OVERLAP*sizeof(float));
   }
   free(in);
   for (i = 0; i < 21; i++) maskLogE[1][i] = FL_MIN(maskLogE[0][i], maskLogE[2][i]);
   channel_offset = XF(.5f)*fl_e_log2((float)(XF(2.f)/(C-1)));
   for (c = 0; c < 3; c++)
      for (i = 0; i < 21; i++) maskLogE[c][i] = (float)(maskLogE[c][i]+channel_offset);
   for (c = 0; c < C; c++) {
      if (pos[c] != 0) {
         const float *mask = maskLogE[pos[c]-1];
         for (i = 0; i < 21; i++) bandLogE[21*c+i] = bandLogE[21*c+i]-mask[i];
      } else {
         for (i = 0; i < 21; i++) bandLogE[21*c+i] = 0;
      }
   }
}

/* surround_rate_allocation(); returns the rate sum. */
static fl_i32 fl_msenc_rate_allocation(fl_msenc *st, fl_i32 *rate, int frame_size) {
   int i, nb_lfe = st->lfe_stream != -1, nb_coupled = st->coupled, nb_uncoupled = st->streams-nb_coupled-nb_lfe;
   int nb_normal = 2*nb_coupled+nb_uncoupled, total;
   fl_i32 channel_offset = 40*IMAX(50, 48000/frame_size), bitrate, lfe_offset, stream_offset, channel_rate, sum = 0;
   if (st->bitrate_bps == FL_AUTO) bitrate = nb_normal*(channel_offset+48000+10000)+8000*nb_lfe;
   else if (st->bitrate_bps == FL_BITRATE_MAX) bitrate = nb_normal*750000+nb_lfe*128000;
   else bitrate = st->bitrate_bps;
   lfe_offset = IMIN(bitrate/20, 3000)+15*IMAX(50, 48000/frame_size);
   stream_offset = (bitrate-channel_offset*nb_normal-lfe_offset*nb_lfe)/nb_normal/2;
   stream_offset = IMAX(0, IMIN(20000, stream_offset));
   total = (nb_uncoupled<<8)+512*nb_coupled+nb_lfe*32;
   channel_rate = (fl_i32)(256*(fl_i64)(bitrate-lfe_offset*nb_lfe-stream_offset*(nb_coupled+nb_uncoupled)-channel_offset*nb_normal)/total);
   for (i = 0; i < st->streams; i++) {
      if (i < st->coupled) rate[i] = 2*channel_offset+IMAX(0, stream_offset+(channel_rate*512>>8));
      else if (i != st->lfe_stream) rate[i] = channel_offset+IMAX(0, stream_offset+channel_rate);
      else rate[i] = IMAX(0, lfe_offset+(channel_rate*32>>8));
      rate[i] = IMAX(rate[i], 500);
      sum += rate[i];
   }
   return sum;
}

/* First channel mapped to stream index m (get_left/right/mono_channel). */
static int fl_msenc_channel(const fl_msenc *st, int m) {
   int c;
   for (c = 0; c < st->channels; c++)
      if (st->mapping[c] == m) return c;
   return -1;
}

static int fl_msenc_encode(fl_msenc *st, const float *pcm, int analysis_frame_size, unsigned char *data, fl_i32 max_bytes) {
   static const int sizes[9] = {120, 240, 480, 960, 1920, 2880, 3840, 4800, 5760};
   int s, i, frame_size, vbr = st->enc[0].use_vbr, C = st->channels;
   fl_i32 bitrates[256], rate_sum, smallest_packet, tot_size = 0;
   frame_size = sizes[st->variable_duration-5001];
   if (frame_size > analysis_frame_size) return FL_ERR_BAD_ARG;
   smallest_packet = st->streams*2-1;
   if (48000/frame_size == 10) smallest_packet += st->streams;
   if (max_bytes < smallest_packet) return FL_ERR_BAD_ARG;
   if (st->surround) fl_surround_analysis(st, pcm, frame_size);
   rate_sum = fl_msenc_rate_allocation(st, bitrates, frame_size);
   if (!vbr) {
      if (st->bitrate_bps == FL_AUTO) max_bytes = IMIN(max_bytes, (rate_sum*6/(6*48000/frame_size)+4)/8);
      else if (st->bitrate_bps != FL_BITRATE_MAX)
         max_bytes = IMIN(max_bytes, IMAX(smallest_packet, (st->bitrate_bps*6/(6*48000/frame_size)+4)/8));
   }
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      fl_enc_set_bitrate(e, bitrates[s]);
      if (st->surround) {
         fl_i32 equiv_rate = st->bitrate_bps;
         if (frame_size*50 < 48000) equiv_rate -= 60*(48000/frame_size-50)*C;
         if (equiv_rate > 10000*C) e->user_bandwidth = FL_BW_FB;
         else if (equiv_rate > 7000*C) e->user_bandwidth = FL_BW_SWB;
         else if (equiv_rate > 5000*C) e->user_bandwidth = FL_BW_WB;
         else e->user_bandwidth = FL_BW_NB;
         e->silk_mode.maxInternalSampleRate = e->user_bandwidth == FL_BW_NB ? 8000 : 16000;
         if (s < st->coupled) {
            /* keep the spatial image: stereo CELT on coupled streams */
            e->user_forced_mode = FL_MODE_CELT_ONLY;
            e->force_channels = 2;
         }
      }
   }
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      unsigned char *frames[48];
      int lens[48], c1, c2, len, count, offset, k;
      fl_i16 size[48];
      fl_i32 curr_max;
      if (s < st->coupled) {
         c1 = fl_msenc_channel(st, 2*s);
         c2 = fl_msenc_channel(st, 2*s+1);
         for (i = 0; i < frame_size; i++) {
            st->buf[2*i] = pcm[i*C+c1];
            st->buf[2*i+1] = pcm[i*C+c2];
         }
         if (st->surround)
            for (i = 0; i < 21; i++) {
               st->bandLogE[i] = st->bandSMR[21*c1+i];
               st->bandLogE[21+i] = st->bandSMR[21*c2+i];
            }
      } else {
         c1 = fl_msenc_channel(st, s+st->coupled);
         c2 = -1;
         for (i = 0; i < frame_size; i++) st->buf[i] = pcm[i*C+c1];
         if (st->surround)
            for (i = 0; i < 21; i++) st->bandLogE[i] = st->bandSMR[21*c1+i];
      }
      if (st->surround) e->energy_masking = e->celt.energy_mask = st->bandLogE;
      curr_max = max_bytes-tot_size;
      curr_max -= IMAX(0, 2*(st->streams-s-1)-1);
      if (48000/frame_size == 10) curr_max -= st->streams-s-1;
      curr_max = IMIN(curr_max, 6*1275+12);
      if (s != st->streams-1) curr_max -= curr_max > 253 ? 2 : 1;
      if (!vbr && s == st->streams-1) fl_enc_set_bitrate(e, curr_max*8*(6*48000/frame_size)/6);
      len = fl_opus_encode_native(e, st->buf, frame_size, st->tmp, curr_max, 24, pcm, analysis_frame_size, c1, c2, C);
      if (len < 0) return len;
      /* opus_repacketizer_cat + out_range_impl: self-delimited framing for all but the last stream */
      count = fl_packet_parse(st->tmp, len, 0, size, &offset, NULL);
      if (count < 0) return -3;
      frames[0] = st->tmp+offset;
      for (k = 0; k < count; k++) {
         lens[k] = size[k];
         if (k > 0) frames[k] = frames[k-1]+lens[k-1];
      }
      len = fl_repacketize(st->tmp[0], frames, lens, count, data, max_bytes-tot_size, s != st->streams-1,
            !vbr && s == st->streams-1);
      if (len < 0) return -3;
      data += len;
      tot_size += len;
   }
   return tot_size;
}

/* ======== enc_silk_fix.c ======== */
/* ---- SILK encoder: fixed-point parts (ported from libopus, see LICENSE notes) ---- */
#define silk_encoder_state_Fxx silk_encoder_state_FLP
#define silk_encode_frame_Fxx silk_encode_frame_FLP
#define silk_encode_do_VAD_Fxx silk_encode_do_VAD_FLP
#define silk_VAD_GetSA_Q8(a, b, arch) silk_VAD_GetSA_Q8_c(a, b)
#define silk_VQ_WMat_EC(a, b, c, d, e, f, g, h, i, j, k, l, arch) silk_VQ_WMat_EC_c(a, b, c, d, e, f, g, h, i, j, k, l)
#define silk_biquad_alt_stride2(a, b, c, d, e, f, arch) silk_biquad_alt_stride2_c(a, b, c, d, e, f)
#define RES2INT16(a) ((fl_i16)lrintf(FL_MAX(-32768.f, FL_MIN(32767.f, (a)*32768.f))))
#define silk_memcpy memcpy
#define silk_memset memset
#define silk_memmove memmove
#define celt_assert(x)
#define silk_assert(x)
#define celt_assert2(x, y)
#define SAVE_STACK
#define RESTORE_STACK
#define VARDECL(type, var) type *var
#define ALLOC(var, size, type) type var##_buf_[(size) > 0 ? (size) : 1]; var = var##_buf_
#define SILK_NO_ERROR 0
#define SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES -101
#define SILK_ENC_FS_NOT_SUPPORTED -102
#define SILK_ENC_PACKET_SIZE_NOT_SUPPORTED -103
#define SILK_ENC_INVALID_LOSS_RATE -105
#define SILK_ENC_INVALID_COMPLEXITY_SETTING -106
#define SILK_ENC_INVALID_INBAND_FEC_SETTING -107
#define SILK_ENC_INVALID_DTX_SETTING -108
#define SILK_ENC_INVALID_CBR_SETTING -109
#define SILK_ENC_INTERNAL_ERROR -110
#define SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR -111
#define opus_likely(x) (x)
#define opus_unlikely(x) (x)
#define silk_NLSF2A(a, b, c, arch) (silk_NLSF2A)(a, b, c)
#define opus_res float
#define silk_LPC_analysis_filter(a, b, c, d, e, arch) (silk_LPC_analysis_filter)(a, b, c, d, e)
#define silk_resampler_down2_0 9872
#define silk_resampler_down2_1 (39809-65536)
static void silk_float2short_array(fl_i16 *out, const float *in, fl_i32 length) {
  fl_i32 k;
  for (k = length-1; k >= 0; k--) out[k] = (fl_i16)silk_SAT16(lrintf(in[k]));
}
static void silk_short2float_array(float *out, const fl_i16 *in, fl_i32 length) {
  fl_i32 k;
  for (k = length-1; k >= 0; k--) out[k] = (float)in[k];
}
#define silk_short_prediction_create_arch_coef(a, b, c) ((void)(a), (void)(b), (void)(c))

#define ENCODER_NUM_CHANNELS 2
#define DECODER_NUM_CHANNELS 2
#define MIN_TARGET_RATE_BPS 5000
#define MAX_TARGET_RATE_BPS 80000
#define LBRR_NB_MIN_RATE_BPS 12000
#define LBRR_MB_MIN_RATE_BPS 14000
#define LBRR_WB_MIN_RATE_BPS 16000
#define NB_SPEECH_FRAMES_BEFORE_DTX 10
#define MAX_CONSECUTIVE_DTX 20
#define DTX_ACTIVITY_THRESHOLD 0.1f
#define VAD_NO_DECISION -1
#define VAD_NO_ACTIVITY 0
#define VAD_ACTIVITY 1
#define STEREO_RATIO_SMOOTH_COEF 0.01
#define LA_PITCH_MS 2
#define LA_PITCH_MAX ( LA_PITCH_MS * MAX_FS_KHZ )
#define MAX_FIND_PITCH_LPC_ORDER 16
#define FIND_PITCH_LPC_WIN_MS ( 20 + (LA_PITCH_MS << 1) )
#define FIND_PITCH_LPC_WIN_MS_2_SF ( 10 + (LA_PITCH_MS << 1) )
#define FIND_PITCH_LPC_WIN_MAX ( FIND_PITCH_LPC_WIN_MS * MAX_FS_KHZ )
#define LA_SHAPE_MS 5
#define LA_SHAPE_MAX ( LA_SHAPE_MS * MAX_FS_KHZ )
#define SHAPE_LPC_WIN_MAX ( 15 * MAX_FS_KHZ )
#define MAX_PREDICTION_POWER_GAIN_AFTER_RESET 1e2f
#define USE_HARM_SHAPING 1
#define MAX_SHAPE_LPC_ORDER 24
#define HARM_SHAPE_FIR_TAPS 3
#define MAX_DEL_DEC_STATES 4
#define LTP_BUF_LENGTH 512
#define LTP_MASK ( LTP_BUF_LENGTH - 1 )
#define DECISION_DELAY 40
#define MAX_MATRIX_SIZE MAX_LPC_ORDER
#define NSQ_LPC_BUF_LENGTH MAX_LPC_ORDER
#define VAD_N_BANDS 4
#define VAD_INTERNAL_SUBFRAMES_LOG2 2
#define VAD_INTERNAL_SUBFRAMES ( 1 << VAD_INTERNAL_SUBFRAMES_LOG2 )
#define VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 1024
#define VAD_NOISE_LEVELS_BIAS 50
#define VAD_NEGATIVE_OFFSET_Q5 128
#define VAD_SNR_FACTOR_Q16 45000
#define VAD_SNR_SMOOTH_COEF_Q18 4096
#define NLSF_W_Q 2
#define NLSF_VQ_MAX_VECTORS 32
#define NLSF_QUANT_MAX_AMPLITUDE_EXT 10
#define NLSF_QUANT_DEL_DEC_STATES_LOG2 2
#define NLSF_QUANT_DEL_DEC_STATES ( 1 << NLSF_QUANT_DEL_DEC_STATES_LOG2 )
#define TRANSITION_TIME_MS 5120
#define TRANSITION_FRAMES ( TRANSITION_TIME_MS / MAX_FRAME_LENGTH_MS )
#define TRANSITION_INT_STEPS ( TRANSITION_FRAMES / ( TRANSITION_INT_NUM - 1 ) )
#define BITRESERVOIR_DECAY_TIME_MS 500
#define FIND_PITCH_WHITE_NOISE_FRACTION 1e-3f
#define FIND_PITCH_BANDWIDTH_EXPANSION 0.99f
#define FIND_LPC_COND_FAC 1e-5f
#define MAX_SUM_LOG_GAIN_DB 250.0f
#define LTP_CORR_INV_MAX 0.03f
#define VARIABLE_HP_SMTH_COEF1 0.1f
#define VARIABLE_HP_SMTH_COEF2 0.015f
#define VARIABLE_HP_MAX_DELTA_FREQ 0.4f
#define VARIABLE_HP_MIN_CUTOFF_HZ 60
#define VARIABLE_HP_MAX_CUTOFF_HZ 100
#define SPEECH_ACTIVITY_DTX_THRES 0.05f
#define LBRR_SPEECH_ACTIVITY_THRES 0.3f
#define BG_SNR_DECR_dB 2.0f
#define HARM_SNR_INCR_dB 2.0f
#define SPARSE_SNR_INCR_dB 2.0f
#define ENERGY_VARIATION_THRESHOLD_QNT_OFFSET 0.6f
#define WARPING_MULTIPLIER 0.015f
#define SHAPE_WHITE_NOISE_FRACTION 3e-5f
#define BANDWIDTH_EXPANSION 0.94f
#define HARMONIC_SHAPING 0.3f
#define HIGH_RATE_OR_LOW_QUALITY_HARMONIC_SHAPING 0.2f
#define HP_NOISE_COEF 0.25f
#define HARM_HP_NOISE_COEF 0.35f
#define INPUT_TILT 0.05f
#define HIGH_RATE_INPUT_TILT 0.1f
#define LOW_FREQ_SHAPING 4.0f
#define LOW_QUALITY_LOW_FREQ_SHAPING_DECR 0.5f
#define SUBFR_SMTH_COEF 0.4f
#define LAMBDA_OFFSET 1.2f
#define LAMBDA_SPEECH_ACT -0.2f
#define LAMBDA_DELAYED_DECISIONS -0.05f
#define LAMBDA_INPUT_QUALITY -0.1f
#define LAMBDA_CODING_QUALITY -0.2f
#define LAMBDA_QUANT_OFFSET 0.8f
#define REDUCE_BITRATE_10_MS_BPS 2200
#define MAX_BANDWIDTH_SWITCH_DELAY_MS 5000
#define PE_MAX_FS_KHZ 16
#define PE_SUBFR_LENGTH_MS 5
#define PE_LTP_MEM_LENGTH_MS ( 4 * PE_SUBFR_LENGTH_MS )
#define PE_MAX_FRAME_LENGTH_MS ( PE_LTP_MEM_LENGTH_MS + PE_MAX_NB_SUBFR * PE_SUBFR_LENGTH_MS )
#define PE_MAX_FRAME_LENGTH ( PE_MAX_FRAME_LENGTH_MS * PE_MAX_FS_KHZ )
#define PE_MAX_FRAME_LENGTH_ST_1 ( PE_MAX_FRAME_LENGTH >> 2 )
#define PE_MAX_FRAME_LENGTH_ST_2 ( PE_MAX_FRAME_LENGTH >> 1 )
#define PE_MAX_LAG ( PE_MAX_LAG_MS * PE_MAX_FS_KHZ )
#define PE_MIN_LAG ( PE_MIN_LAG_MS * PE_MAX_FS_KHZ )
#define PE_D_SRCH_LENGTH 24
#define PE_NB_STAGE3_LAGS 5
#define PE_SHORTLAG_BIAS 0.2f
#define PE_PREVLAG_BIAS 0.2f
#define PE_FLATCONTOUR_BIAS 0.05f
#define SILK_PE_MIN_COMPLEX 0
#define SILK_PE_MID_COMPLEX 1
#define silk_DWORD_ALIGN __attribute__((aligned(4)))
#define silk_MUL_uint(a32,b32) silk_MUL(a32, b32)
#define silk_MLA_uint(a32,b32,c32) silk_MLA(a32, b32, c32)
#define silk_ADD16(a,b) ((a) + (b))
#define silk_ADD64(a,b) ((a) + (b))
#define silk_SUB16(a,b) ((a) - (b))
#define silk_SUB64(a,b) ((a) - (b))
#define silk_SAT8(a) ((a) > silk_int8_MAX ? silk_int8_MAX : \
                                 ((a) < silk_int8_MIN ? silk_int8_MIN : (a)))
#define silk_CHECK_FIT8(a) (a)
#define silk_CHECK_FIT16(a) (a)
#define silk_CHECK_FIT32(a) (a)
#define silk_ADD_SAT64(a,b) ((((a) + (b)) & 0x8000000000000000LL) == 0 ? \
                                 ((((a) & (b)) & 0x8000000000000000LL) != 0 ? silk_int64_MIN : (a)+(b)) : \
                                 ((((a) | (b)) & 0x8000000000000000LL) == 0 ? silk_int64_MAX : (a)+(b)) )
#define silk_SUB_SAT64(a,b) ((((a)-(b)) & 0x8000000000000000LL) == 0 ? \
                                 (( (a) & ((b)^0x8000000000000000LL) & 0x8000000000000000LL) ? silk_int64_MIN : (a)-(b)) : \
                                 ((((a)^0x8000000000000000LL) & (b) & 0x8000000000000000LL) ? silk_int64_MAX : (a)-(b)) )
#define silk_ADD_POS_SAT8(a,b) ((((a)+(b)) & 0x80) ? silk_int8_MAX : ((a)+(b)))
#define silk_ADD_POS_SAT16(a,b) ((((a)+(b)) & 0x8000) ? silk_int16_MAX : ((a)+(b)))
#define silk_LSHIFT8(a,shift) ((fl_i8)((fl_u8)(a)<<(shift)))
#define silk_LSHIFT16(a,shift) ((fl_i16)((fl_u16)(a)<<(shift)))
#define silk_RSHIFT8(a,shift) ((a)>>(shift))
#define silk_RSHIFT16(a,shift) ((a)>>(shift))
#define silk_LSHIFT_uint(a,shift) ((a) << (shift))
#define silk_RSHIFT_uint(a,shift) ((a) >> (shift))
#define silk_ADD_LSHIFT(a,b,shift) ((a) + silk_LSHIFT((b), (shift)))
#define silk_ADD_LSHIFT_uint(a,b,shift) ((a) + silk_LSHIFT_uint((b), (shift)))
#define silk_ADD_RSHIFT(a,b,shift) ((a) + silk_RSHIFT((b), (shift)))
#define silk_NSHIFT_MUL_32_32(a,b) ( -(31- (32-silk_CLZ32(silk_abs(a)) + (32-silk_CLZ32(silk_abs(b))))) )
#define silk_NSHIFT_MUL_16_16(a,b) ( -(15- (16-silk_CLZ16(silk_abs(a)) + (16-silk_CLZ16(silk_abs(b))))) )
#define silk_abs_int(a) (((a) ^ ((a) >> (8 * sizeof(a) - 1))) - ((a) >> (8 * sizeof(a) - 1)))
#define silk_abs_int64(a) (((a) > 0) ? (a) : -(a))
#define silk_sign(a) ((a) > 0 ? 1 : ( (a) < 0 ? -1 : 0 ))
#define RAND_MULTIPLIER 196314165
#define RAND_INCREMENT 907633515
#define silk_burg_modified(res_nrg,res_nrg_Q,A_Q16,x,minInvGain_Q30,subfr_length,nb_subfr,D,arch) \
   ((void)(arch), silk_burg_modified_c(res_nrg, res_nrg_Q, A_Q16, x, minInvGain_Q30, subfr_length, nb_subfr, D, arch))
#define silk_inner_prod16(inVec1,inVec2,len,arch) \
   ((void)(arch),silk_inner_prod16_c(inVec1, inVec2, len))
#define matrix_ptr(Matrix_base_adr,row,column,N) \
   (*((Matrix_base_adr) + ((row)*(N)+(column))))
#define matrix_adr(Matrix_base_adr,row,column,N) \
    ((Matrix_base_adr) + ((row)*(N)+(column)))
#define matrix_c_ptr(Matrix_base_adr,row,column,M) \
   (*((Matrix_base_adr) + ((row)+(M)*(column))))
#define RESAMPLER_MAX_FS_KHZ 48
#define RESAMPLER_MAX_BATCH_SIZE_IN ( RESAMPLER_MAX_BATCH_SIZE_MS * RESAMPLER_MAX_FS_KHZ )

typedef struct {
   fl_i16 xq[ 2 * MAX_FRAME_LENGTH ];
   fl_i32 sLTP_shp_Q14[ 2 * MAX_FRAME_LENGTH ];
   fl_i32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
   fl_i32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
   fl_i32 sLF_AR_shp_Q14;
   fl_i32 sDiff_shp_Q14;
   int lagPrev;
   int sLTP_buf_idx;
   int sLTP_shp_buf_idx;
   fl_i32 rand_seed;
   fl_i32 prev_gain_Q16;
   int rewhite_flag;
} silk_nsq_state;

typedef struct {
   fl_i32 AnaState[ 2 ];
   fl_i32 AnaState1[ 2 ];
   fl_i32 AnaState2[ 2 ];
   fl_i32 XnrgSubfr[ VAD_N_BANDS ];
   fl_i32 NrgRatioSmth_Q8[ VAD_N_BANDS ];
   fl_i16 HPstate;
   fl_i32 NL[ VAD_N_BANDS ];
   fl_i32 inv_NL[ VAD_N_BANDS ];
   fl_i32 NoiseLevelBias[ VAD_N_BANDS ];
   fl_i32 counter;
} silk_VAD_state;

typedef struct {
   fl_i32 In_LP_State[ 2 ];
   fl_i32 transition_frame_no;
   int mode;
   fl_i32 saved_fs_kHz;
} silk_LP_state;

typedef struct {
   fl_i16 pred_prev_Q13[ 2 ];
   fl_i16 sMid[ 2 ];
   fl_i16 sSide[ 2 ];
   fl_i32 mid_side_amp_Q0[ 4 ];
   fl_i16 smth_width_Q14;
   fl_i16 width_prev_Q14;
   fl_i16 silent_side_len;
   fl_i8 predIx[ MAX_FRAMES_PER_PACKET ][ 2 ][ 3 ];
   fl_i8 mid_only_flags[ MAX_FRAMES_PER_PACKET ];
} stereo_enc_state;

typedef struct {
   fl_i8 GainsIndices[ MAX_NB_SUBFR ];
   fl_i8 LTPIndex[ MAX_NB_SUBFR ];
   fl_i8 NLSFIndices[ MAX_LPC_ORDER + 1 ];
   fl_i16 lagIndex;
   fl_i8 contourIndex;
   fl_i8 signalType;
   fl_i8 quantOffsetType;
   fl_i8 NLSFInterpCoef_Q2;
   fl_i8 PERIndex;
   fl_i8 LTP_scaleIndex;
   fl_i8 Seed;
} SideInfoIndices;

typedef struct {
   fl_i32 In_HP_State[ 2 ];
   fl_i32 variable_HP_smth1_Q15;
   fl_i32 variable_HP_smth2_Q15;
   silk_LP_state sLP;
   silk_VAD_state sVAD;
   silk_nsq_state sNSQ;
   fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ];
   int speech_activity_Q8;
   int allow_bandwidth_switch;
   fl_i8 LBRRprevLastGainIndex;
   fl_i8 prevSignalType;
   int prevLag;
   int pitch_LPC_win_length;
   int max_pitch_lag;
   fl_i32 API_fs_Hz;
   fl_i32 prev_API_fs_Hz;
   int maxInternal_fs_Hz;
   int minInternal_fs_Hz;
   int desiredInternal_fs_Hz;
   int fs_kHz;
   int nb_subfr;
   int frame_length;
   int subfr_length;
   int ltp_mem_length;
   int la_pitch;
   int la_shape;
   int shapeWinLength;
   fl_i32 TargetRate_bps;
   int PacketSize_ms;
   int PacketLoss_perc;
   fl_i32 frameCounter;
   int Complexity;
   int nStatesDelayedDecision;
   int useInterpolatedNLSFs;
   int shapingLPCOrder;
   int predictLPCOrder;
   int pitchEstimationComplexity;
   int pitchEstimationLPCOrder;
   fl_i32 pitchEstimationThreshold_Q16;
   fl_i32 sum_log_gain_Q7;
   int NLSF_MSVQ_Survivors;
   int first_frame_after_reset;
   int controlled_since_last_payload;
   int warping_Q16;
   int useCBR;
   int prefillFlag;
   const fl_u8 *pitch_lag_low_bits_iCDF;
   const fl_u8 *pitch_contour_iCDF;
   const fl_nlsf_cb *psNLSF_CB;
   int input_quality_bands_Q15[ VAD_N_BANDS ];
   int input_tilt_Q15;
   int SNR_dB_Q7;
   fl_i8 VAD_flags[ MAX_FRAMES_PER_PACKET ];
   fl_i8 LBRR_flag;
   int LBRR_flags[ MAX_FRAMES_PER_PACKET ];
   SideInfoIndices indices;
   fl_i8 pulses[ MAX_FRAME_LENGTH ];
   int arch;
   fl_i16 inputBuf[ MAX_FRAME_LENGTH + 2 ];
   int inputBufIx;
   int nFramesPerPacket;
   int nFramesEncoded;
   int nChannelsAPI;
   int nChannelsInternal;
   int channelNb;
   int frames_since_onset;
   int ec_prevSignalType;
   fl_i16 ec_prevLagIndex;
   silk_resampler_state resampler_state;
   int useDTX;
   int inDTX;
   int noSpeechCounter;
   int useInBandFEC;
   int LBRR_enabled;
   int LBRR_GainIncreases;
   SideInfoIndices indices_LBRR[ MAX_FRAMES_PER_PACKET ];
   fl_i8 pulses_LBRR[ MAX_FRAMES_PER_PACKET ][ MAX_FRAME_LENGTH ];
} silk_encoder_state;

typedef struct {
   fl_i8 LastGainIndex;
   float HarmShapeGain_smth;
   float Tilt_smth;
} silk_shape_state_FLP;

typedef struct {
   silk_encoder_state sCmn;
   silk_shape_state_FLP sShape;
   float x_buf[ 2 * MAX_FRAME_LENGTH + LA_SHAPE_MAX ];
   float LTPCorr;
} silk_encoder_state_FLP;

typedef struct {
   float Gains[ MAX_NB_SUBFR ];
   float PredCoef[ 2 ][ MAX_LPC_ORDER ];
   float LTPCoef[LTP_ORDER * MAX_NB_SUBFR];
   float LTP_scale;
   int pitchL[ MAX_NB_SUBFR ];
   float AR[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ];
   float LF_MA_shp[ MAX_NB_SUBFR ];
   float LF_AR_shp[ MAX_NB_SUBFR ];
   float Tilt[ MAX_NB_SUBFR ];
   float HarmShapeGain[ MAX_NB_SUBFR ];
   float Lambda;
   float input_quality;
   float coding_quality;
   float predGain;
   float LTPredCodGain;
   float ResNrg[ MAX_NB_SUBFR ];
   fl_i32 GainsUnq_Q16[ MAX_NB_SUBFR ];
   fl_i8 lastGainIndexPrev;
} silk_encoder_control_FLP;

typedef struct {
   stereo_enc_state sStereo;
   fl_i32 nBitsUsedLBRR;
   fl_i32 nBitsExceeded;
   int nChannelsAPI;
   int nChannelsInternal;
   int nPrevChannelsInternal;
   int timeSinceSwitchAllowed_ms;
   int allowBandwidthSwitch;
   int prev_decode_only_middle;
   silk_encoder_state_FLP state_Fxx[ ENCODER_NUM_CHANNELS ];
} fl_silk_encoder;

static int silk_encode_frame_FLP(silk_encoder_state_FLP *psEnc, fl_i32 *pnBytesOut, ec_ctx *psRangeEnc,
    int condCoding, int maxBits, int useCBR);
static void silk_encode_do_VAD_FLP(silk_encoder_state_FLP *psEnc, int activity);


static const unsigned char silk_TargetRate_NB_21[117 - 10] = {
                                  0, 15, 39, 52, 61, 68,
   74, 79, 84, 88, 92, 95, 99,102,105,108,111,114,117,119,122,124,
   126,129,131,133,135,137,139,142,143,145,147,149,151,153,155,157,
   158,160,162,163,165,167,168,170,171,173,174,176,177,179,180,182,
   183,185,186,187,189,190,192,193,194,196,197,199,200,201,203,204,
   205,207,208,209,211,212,213,215,216,217,219,220,221,223,224,225,
   227,228,230,231,232,234,235,236,238,239,241,242,243,245,246,248,
   249,250,252,253,255
};
static const unsigned char silk_TargetRate_MB_21[165 - 10] = {
                                  0, 0, 28, 43, 52, 59,
   65, 70, 74, 78, 81, 85, 87, 90, 93, 95, 98,100,102,105,107,109,
   111,113,115,116,118,120,122,123,125,127,128,130,131,133,134,136,
   137,138,140,141,143,144,145,147,148,149,151,152,153,154,156,157,
   158,159,160,162,163,164,165,166,167,168,169,171,172,173,174,175,
   176,177,178,179,180,181,182,183,184,185,186,187,188,188,189,190,
   191,192,193,194,195,196,197,198,199,200,201,202,203,203,204,205,
   206,207,208,209,210,211,212,213,214,214,215,216,217,218,219,220,
   221,222,223,224,224,225,226,227,228,229,230,231,232,233,234,235,
   236,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,
   251,252,253,254,255
};
static const unsigned char silk_TargetRate_WB_21[201 - 10] = {
                                  0, 0, 0, 8, 29, 41,
   49, 56, 62, 66, 70, 74, 77, 80, 83, 86, 88, 91, 93, 95, 97, 99,
   101,103,105,107,108,110,112,113,115,116,118,119,121,122,123,125,
   126,127,129,130,131,132,134,135,136,137,138,140,141,142,143,144,
   145,146,147,148,149,150,151,152,153,154,156,157,158,159,159,160,
   161,162,163,164,165,166,167,168,169,170,171,171,172,173,174,175,
   176,177,177,178,179,180,181,181,182,183,184,185,185,186,187,188,
   189,189,190,191,192,192,193,194,195,195,196,197,198,198,199,200,
   200,201,202,203,203,204,205,206,206,207,208,209,209,210,211,211,
   212,213,214,214,215,216,216,217,218,219,219,220,221,221,222,223,
   224,224,225,226,226,227,228,229,229,230,231,232,232,233,234,234,
   235,236,237,237,238,239,240,240,241,242,243,243,244,245,246,246,
   247,248,249,249,250,251,252,253,255
};

#define silk_encoder_state_Fxx silk_encoder_state_FLP
static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
);
static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
);
static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
);
static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
);

static const fl_i32 sigm_LUT_slope_Q10[ 6 ] = {
   237, 153, 73, 30, 12, 7
};
static const fl_i32 sigm_LUT_pos_Q15[ 6 ] = {
   16384, 23955, 28861, 31213, 32178, 32548
};
static const fl_i32 sigm_LUT_neg_Q15[ 6 ] = {
   16384, 8812, 3906, 1554, 589, 219
};

static const fl_i16 A_fb1_20 = 5394 << 1;
static const fl_i16 A_fb1_21 = -24290;

static const fl_i32 tiltWeights[ VAD_N_BANDS ] = { 30000, 6000, -12000, -12000 };

#define BIN_DIV_STEPS_A2NLSF_FIX 3
#define MAX_ITERATIONS_A2NLSF_FIX 16

#define SILK_NSQ_H

#define silk_noise_shape_quantizer_short_prediction(in,coef,coefRev,order,arch) ((void)arch,silk_noise_shape_quantizer_short_prediction_c(in, coef, order))

#define silk_NSQ_noise_shape_feedback_loop(data0,data1,coef,order,arch) ((void)arch,silk_NSQ_noise_shape_feedback_loop_c(data0, data1, coef, order))

static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
);

typedef struct {
   fl_i32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
   fl_i32 RandState[ DECISION_DELAY ];
   fl_i32 Q_Q10[ DECISION_DELAY ];
   fl_i32 Xq_Q14[ DECISION_DELAY ];
   fl_i32 Pred_Q15[ DECISION_DELAY ];
   fl_i32 Shape_Q14[ DECISION_DELAY ];
   fl_i32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
   fl_i32 LF_AR_Q14;
   fl_i32 Diff_Q14;
   fl_i32 Seed;
   fl_i32 SeedInit;
   fl_i32 RD_Q10;
} NSQ_del_dec_struct;
typedef struct {
   fl_i32 Q_Q10;
   fl_i32 RD_Q10;
   fl_i32 xq_Q14;
   fl_i32 LF_AR_Q14;
   fl_i32 Diff_Q14;
   fl_i32 sLTP_shp_Q14;
   fl_i32 LPC_exc_Q14;
} NSQ_sample_struct;
typedef NSQ_sample_struct NSQ_sample_pair[ 2 ];
static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
);
static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
);

#define ORDER_FIR 4

static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
);

static int silk_init_encoder(
   silk_encoder_state_Fxx *psEnc,
   int arch
);
static int check_control_input(
   fl_silk_ctrl *encControl
);
static int silk_control_audio_bandwidth(
   silk_encoder_state *psEncC,
   fl_silk_ctrl *encControl
);
static int silk_control_SNR(
   silk_encoder_state *psEncC,
   fl_i32 TargetRate_bps
);
static int silk_control_encoder(
   silk_encoder_state_Fxx *psEnc,
   fl_silk_ctrl *encControl,
   const int allow_bw_switch,
   const int channelNb,
   const int force_fs_kHz
);
static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
);
static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
);
static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
);
static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
);
static void silk_HP_variable_cutoff(
   silk_encoder_state_Fxx state_Fxx[]
);
static inline void silk_LP_interpolate_filter_taps(
   fl_i32 B_Q28[ TRANSITION_NB ],
   fl_i32 A_Q28[ TRANSITION_NA ],
   const int ind,
   const fl_i32 fac_Q16
);
static void silk_LP_variable_cutoff(
   silk_LP_state *psLP,
   fl_i16 *frame,
   const int frame_length
);
static void silk_biquad_alt_stride1(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
);
static void silk_biquad_alt_stride2_c(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
);
static int silk_sigm_Q15(
   int in_Q5
);
static void silk_ana_filt_bank_1(
   const fl_i16 *in,
   fl_i32 *S,
   fl_i16 *outL,
   fl_i16 *outH,
   const fl_i32 N
);
static int silk_VAD_Init(
   silk_VAD_state *psSilk_VAD
);
static int silk_VAD_GetSA_Q8_c(
   silk_encoder_state *psEncC,
   const fl_i16 pIn[]
);
static void silk_VAD_GetNoiseLevels(
   const fl_i32 pX[ VAD_N_BANDS ],
   silk_VAD_state *psSilk_VAD
);
static fl_i32 silk_inner_prod_aligned_scale(
   const fl_i16 *const inVec1,
   const fl_i16 *const inVec2,
   const int scale,
   const int len
);
static fl_i32 silk_stereo_find_predictor(
   fl_i32 *ratio_Q14,
   const fl_i16 x[],
   const fl_i16 y[],
   fl_i32 mid_res_amp_Q0[],
   int length,
   int smooth_coef_Q16
);
static void silk_stereo_quant_pred(
   fl_i32 pred_Q13[],
   fl_i8 ix[ 2 ][ 3 ]
);
static void silk_stereo_encode_pred(
   ec_ctx *psRangeEnc,
   fl_i8 ix[ 2 ][ 3 ]
);
static void silk_stereo_encode_mid_only(
   ec_ctx *psRangeEnc,
   fl_i8 mid_only_flag
);
static void silk_stereo_LR_to_MS(
   stereo_enc_state *state,
   fl_i16 x1[],
   fl_i16 x2[],
   fl_i8 ix[ 2 ][ 3 ],
   fl_i8 *mid_only_flag,
   fl_i32 mid_side_rates_bps[],
   fl_i32 total_rate_bps,
   int prev_speech_act_Q8,
   int toMono,
   int fs_kHz,
   int frame_length
);
static inline void silk_A2NLSF_trans_poly(
   fl_i32 *p,
   const int dd
);
static inline fl_i32 silk_A2NLSF_eval_poly(
   fl_i32 *p,
   const fl_i32 x,
   const int dd
);
static inline void silk_A2NLSF_init(
   const fl_i32 *a_Q16,
   fl_i32 *P,
   fl_i32 *Q,
   const int dd
);
static void silk_A2NLSF(
   fl_i16 *NLSF,
   fl_i32 *a_Q16,
   const int d
);
static void silk_NLSF_VQ_weights_laroia(
   fl_i16 *pNLSFW_Q_OUT,
   const fl_i16 *pNLSF_Q15,
   const int D
);
static void silk_NLSF_VQ(
   fl_i32 err_Q24[],
   const fl_i16 in_Q15[],
   const fl_u8 pCB_Q8[],
   const fl_i16 pWght_Q9[],
   const int K,
   const int LPC_order
);
static fl_i32 silk_NLSF_del_dec_quant(
   fl_i8 indices[],
   const fl_i16 x_Q10[],
   const fl_i16 w_Q5[],
   const fl_u8 pred_coef_Q8[],
   const fl_i16 ec_ix[],
   const fl_u8 ec_rates_Q5[],
   const int quant_step_size_Q16,
   const fl_i16 inv_quant_step_size_Q6,
   const fl_i32 mu_Q20,
   const fl_i16 order
);
static fl_i32 silk_NLSF_encode(
       fl_i8 *NLSFIndices,
       fl_i16 *pNLSF_Q15,
   const fl_nlsf_cb *psNLSF_CB,
   const fl_i16 *pW_Q2,
   const int NLSF_mu_Q20,
   const int nSurvivors,
   const int signalType
);
static void silk_interpolate(
   fl_i16 xi[ MAX_LPC_ORDER ],
   const fl_i16 x0[ MAX_LPC_ORDER ],
   const fl_i16 x1[ MAX_LPC_ORDER ],
   const int ifact_Q2,
   const int d
);
static void silk_process_NLSFs(
   silk_encoder_state *psEncC,
   fl_i16 PredCoef_Q12[ 2 ][ MAX_LPC_ORDER ],
   fl_i16 pNLSF_Q15[ MAX_LPC_ORDER ],
   const fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ]
);
static void silk_VQ_WMat_EC_c(
   fl_i8 *ind,
   fl_i32 *res_nrg_Q15,
   fl_i32 *rate_dist_Q8,
   int *gain_Q7,
   const fl_i32 *XX_Q17,
   const fl_i32 *xX_Q17,
   const fl_i8 *cb_Q7,
   const fl_u8 *cb_gain_Q7,
   const fl_u8 *cl_Q5,
   const int subfr_len,
   const fl_i32 max_gain_Q7,
   const int L
);
static void silk_quant_LTP_gains(
   fl_i16 B_Q14[ MAX_NB_SUBFR * LTP_ORDER ],
   fl_i8 cbk_index[ MAX_NB_SUBFR ],
   fl_i8 *periodicity_index,
   fl_i32 *sum_log_gain_Q7,
   int *pred_gain_dB_Q7,
   const fl_i32 XX_Q17[ MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER ],
   const fl_i32 xX_Q17[ MAX_NB_SUBFR*LTP_ORDER ],
   const int subfr_len,
   const int nb_subfr,
   int arch
);
static void silk_encode_indices(
   silk_encoder_state *psEncC,
   ec_ctx *psRangeEnc,
   int FrameIndex,
   int encode_LBRR,
   int condCoding
);
static inline int combine_and_check(
   int *pulses_comb,
   const int *pulses_in,
   int max_pulses,
   int len
);
static void silk_encode_pulses(
   ec_ctx *psRangeEnc,
   const int signalType,
   const int quantOffsetType,
   fl_i8 pulses[],
   const int frame_length
);
static inline fl_i32 silk_noise_shape_quantizer_short_prediction_c(const fl_i32 *buf32, const fl_i16 *coef16, int order);
static inline fl_i32 silk_NSQ_noise_shape_feedback_loop_c(const fl_i32 *data0, fl_i32 *data1, const fl_i16 *coef, int order);
static void silk_NSQ_c
(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
);
static void silk_noise_shape_quantizer(
   silk_nsq_state *NSQ,
   int signalType,
   const fl_i32 x_sc_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int shapingLPCOrder,
   int predictLPCOrder,
   int arch
);
static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
);
static void silk_NSQ_del_dec_c(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
);
static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
);
static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
);
static void silk_resampler_down2(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
);
static void silk_resampler_down2_3(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
);
static int silk_Get_Encoder_Size(
   int *encSizeBytes,
   int channels
);
static int silk_InitEncoder(
   void *encState,
   int channels,
   int arch,
   fl_silk_ctrl *encStatus
);
static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
);
static int silk_Encode(
   void *encState,
   fl_silk_ctrl *encControl,
   const opus_res *samplesIn,
   int nSamplesIn,
   ec_ctx *psRangeEnc,
   fl_i32 *nBytesOut,
   const int prefillFlag,
   int activity
);

static int silk_init_encoder(
   silk_encoder_state_Fxx *psEnc,
   int arch
)
{
   int ret = 0;
   silk_memset( psEnc, 0, sizeof( silk_encoder_state_Fxx ) );
   psEnc->sCmn.arch = arch;
   psEnc->sCmn.variable_HP_smth1_Q15 = silk_LSHIFT( silk_lin2log( SILK_FIX_CONST( VARIABLE_HP_MIN_CUTOFF_HZ, 16 ) ) - ( 16 << 7 ), 8 );
   psEnc->sCmn.variable_HP_smth2_Q15 = psEnc->sCmn.variable_HP_smth1_Q15;
   psEnc->sCmn.first_frame_after_reset = 1;
   ret += silk_VAD_Init( &psEnc->sCmn.sVAD );
   return ret;
}

static int check_control_input(
   fl_silk_ctrl *encControl
)
{
   celt_assert( encControl != NULL );
   if( ( ( encControl->API_sampleRate != 8000 ) &&
       ( encControl->API_sampleRate != 12000 ) &&
       ( encControl->API_sampleRate != 16000 ) &&
       ( encControl->API_sampleRate != 24000 ) &&
       ( encControl->API_sampleRate != 32000 ) &&
       ( encControl->API_sampleRate != 44100 ) &&
       ( encControl->API_sampleRate != 48000 ) ) ||
      ( ( encControl->desiredInternalSampleRate != 8000 ) &&
       ( encControl->desiredInternalSampleRate != 12000 ) &&
       ( encControl->desiredInternalSampleRate != 16000 ) ) ||
      ( ( encControl->maxInternalSampleRate != 8000 ) &&
       ( encControl->maxInternalSampleRate != 12000 ) &&
       ( encControl->maxInternalSampleRate != 16000 ) ) ||
      ( ( encControl->minInternalSampleRate != 8000 ) &&
       ( encControl->minInternalSampleRate != 12000 ) &&
       ( encControl->minInternalSampleRate != 16000 ) ) ||
       ( encControl->minInternalSampleRate > encControl->desiredInternalSampleRate ) ||
       ( encControl->maxInternalSampleRate < encControl->desiredInternalSampleRate ) ||
       ( encControl->minInternalSampleRate > encControl->maxInternalSampleRate ) ) {
      celt_assert( 0 );
      return SILK_ENC_FS_NOT_SUPPORTED;
   }
   if( encControl->payloadSize_ms != 10 &&
      encControl->payloadSize_ms != 20 &&
      encControl->payloadSize_ms != 40 &&
      encControl->payloadSize_ms != 60 ) {
      celt_assert( 0 );
      return SILK_ENC_PACKET_SIZE_NOT_SUPPORTED;
   }
   if( encControl->packetLossPercentage < 0 || encControl->packetLossPercentage > 100 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_LOSS_RATE;
   }
   if( encControl->useDTX < 0 || encControl->useDTX > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_DTX_SETTING;
   }
   if( encControl->useCBR < 0 || encControl->useCBR > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_CBR_SETTING;
   }
   if( encControl->useInBandFEC < 0 || encControl->useInBandFEC > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_INBAND_FEC_SETTING;
   }
   if( encControl->nChannelsAPI < 1 || encControl->nChannelsAPI > ENCODER_NUM_CHANNELS ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->nChannelsInternal < 1 || encControl->nChannelsInternal > ENCODER_NUM_CHANNELS ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->nChannelsInternal > encControl->nChannelsAPI ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->complexity < 0 || encControl->complexity > 10 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_COMPLEXITY_SETTING;
   }
   return SILK_NO_ERROR;
}

static int silk_control_audio_bandwidth(
   silk_encoder_state *psEncC,
   fl_silk_ctrl *encControl
)
{
   int fs_kHz;
   int orig_kHz;
   fl_i32 fs_Hz;
   orig_kHz = psEncC->fs_kHz;
   if( orig_kHz == 0 ) {
      orig_kHz = psEncC->sLP.saved_fs_kHz;
   }
   fs_kHz = orig_kHz;
   fs_Hz = silk_SMULBB( fs_kHz, 1000 );
   if( fs_Hz == 0 ) {
      fs_Hz = silk_min( psEncC->desiredInternal_fs_Hz, psEncC->API_fs_Hz );
      fs_kHz = silk_DIV32_16( fs_Hz, 1000 );
   } else if( fs_Hz > psEncC->API_fs_Hz || fs_Hz > psEncC->maxInternal_fs_Hz || fs_Hz < psEncC->minInternal_fs_Hz ) {
      fs_Hz = psEncC->API_fs_Hz;
      fs_Hz = silk_min( fs_Hz, psEncC->maxInternal_fs_Hz );
      fs_Hz = silk_max( fs_Hz, psEncC->minInternal_fs_Hz );
      fs_kHz = silk_DIV32_16( fs_Hz, 1000 );
   } else {
      if( psEncC->sLP.transition_frame_no >= TRANSITION_FRAMES ) {
         psEncC->sLP.mode = 0;
      }
      if( psEncC->allow_bandwidth_switch || encControl->opusCanSwitch ) {
         if( silk_SMULBB( orig_kHz, 1000 ) > psEncC->desiredInternal_fs_Hz )
         {
            if( psEncC->sLP.mode == 0 ) {
               psEncC->sLP.transition_frame_no = TRANSITION_FRAMES;
               silk_memset( psEncC->sLP.In_LP_State, 0, sizeof( psEncC->sLP.In_LP_State ) );
            }
            if( encControl->opusCanSwitch ) {
               psEncC->sLP.mode = 0;
               fs_kHz = orig_kHz == 16 ? 12 : 8;
            } else {
              if( psEncC->sLP.transition_frame_no <= 0 ) {
                 encControl->switchReady = 1;
                 encControl->maxBits -= encControl->maxBits * 5 / ( encControl->payloadSize_ms + 5 );
              } else {
                 psEncC->sLP.mode = -2;
              }
            }
         }
         else
         if( silk_SMULBB( orig_kHz, 1000 ) < psEncC->desiredInternal_fs_Hz )
         {
            if( encControl->opusCanSwitch ) {
               fs_kHz = orig_kHz == 8 ? 12 : 16;
               psEncC->sLP.transition_frame_no = 0;
               silk_memset( psEncC->sLP.In_LP_State, 0, sizeof( psEncC->sLP.In_LP_State ) );
               psEncC->sLP.mode = 1;
            } else {
              if( psEncC->sLP.mode == 0 ) {
                 encControl->switchReady = 1;
                 encControl->maxBits -= encControl->maxBits * 5 / ( encControl->payloadSize_ms + 5 );
              } else {
                 psEncC->sLP.mode = 1;
              }
            }
         } else {
           if (psEncC->sLP.mode<0)
             psEncC->sLP.mode = 1;
         }
      }
   }
   return fs_kHz;
}

static int silk_control_SNR(
   silk_encoder_state *psEncC,
   fl_i32 TargetRate_bps
)
{
   int id;
   int bound;
   const unsigned char *snr_table;
   psEncC->TargetRate_bps = TargetRate_bps;
   if( psEncC->nb_subfr == 2 ) {
      TargetRate_bps -= 2000 + psEncC->fs_kHz/16;
   }
   if( psEncC->fs_kHz == 8 ) {
      bound = sizeof(silk_TargetRate_NB_21);
      snr_table = silk_TargetRate_NB_21;
   } else if( psEncC->fs_kHz == 12 ) {
      bound = sizeof(silk_TargetRate_MB_21);
      snr_table = silk_TargetRate_MB_21;
   } else {
      bound = sizeof(silk_TargetRate_WB_21);
      snr_table = silk_TargetRate_WB_21;
   }
   id = (TargetRate_bps+200)/400;
   id = silk_min(id - 10, bound-1);
   if( id <= 0 ) {
      psEncC->SNR_dB_Q7 = 0;
   } else {
      psEncC->SNR_dB_Q7 = snr_table[id]*21;
   }
   return SILK_NO_ERROR;
}

static int silk_control_encoder(
   silk_encoder_state_Fxx *psEnc,
   fl_silk_ctrl *encControl,
   const int allow_bw_switch,
   const int channelNb,
   const int force_fs_kHz
)
{
   int fs_kHz, ret = 0;
   psEnc->sCmn.useDTX = encControl->useDTX;
   psEnc->sCmn.useCBR = encControl->useCBR;
   psEnc->sCmn.API_fs_Hz = encControl->API_sampleRate;
   psEnc->sCmn.maxInternal_fs_Hz = encControl->maxInternalSampleRate;
   psEnc->sCmn.minInternal_fs_Hz = encControl->minInternalSampleRate;
   psEnc->sCmn.desiredInternal_fs_Hz = encControl->desiredInternalSampleRate;
   psEnc->sCmn.useInBandFEC = encControl->useInBandFEC;
   psEnc->sCmn.nChannelsAPI = encControl->nChannelsAPI;
   psEnc->sCmn.nChannelsInternal = encControl->nChannelsInternal;
   psEnc->sCmn.allow_bandwidth_switch = allow_bw_switch;
   psEnc->sCmn.channelNb = channelNb;
   if( psEnc->sCmn.controlled_since_last_payload != 0 && psEnc->sCmn.prefillFlag == 0 ) {
      if( psEnc->sCmn.API_fs_Hz != psEnc->sCmn.prev_API_fs_Hz && psEnc->sCmn.fs_kHz > 0 ) {
         ret += silk_setup_resamplers( psEnc, psEnc->sCmn.fs_kHz );
      }
      return ret;
   }
   fs_kHz = silk_control_audio_bandwidth( &psEnc->sCmn, encControl );
   if( force_fs_kHz ) {
     fs_kHz = force_fs_kHz;
   }
   ret += silk_setup_resamplers( psEnc, fs_kHz );
   ret += silk_setup_fs( psEnc, fs_kHz, encControl->payloadSize_ms );
   ret += silk_setup_complexity( &psEnc->sCmn, encControl->complexity );
   psEnc->sCmn.PacketLoss_perc = encControl->packetLossPercentage;
   ret += silk_setup_LBRR( &psEnc->sCmn, encControl );
   psEnc->sCmn.controlled_since_last_payload = 1;
   return ret;
}

static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
)
{
   int ret = SILK_NO_ERROR;
   SAVE_STACK;
   if( psEnc->sCmn.fs_kHz != fs_kHz || psEnc->sCmn.prev_API_fs_Hz != psEnc->sCmn.API_fs_Hz )
   {
      if( psEnc->sCmn.fs_kHz == 0 ) {
         ret += silk_resampler_init( &psEnc->sCmn.resampler_state, psEnc->sCmn.API_fs_Hz, fs_kHz * 1000, 1 );
      } else {
         VARDECL( fl_i16, x_buf_API_fs_Hz );
         VARDECL( silk_resampler_state, temp_resampler_state );
         VARDECL( fl_i16, x_bufFIX );
         fl_i32 new_buf_samples;
         fl_i32 api_buf_samples;
         fl_i32 old_buf_samples;
         fl_i32 buf_length_ms;
         buf_length_ms = silk_LSHIFT( psEnc->sCmn.nb_subfr * 5, 1 ) + LA_SHAPE_MS;
         old_buf_samples = buf_length_ms * psEnc->sCmn.fs_kHz;
         new_buf_samples = buf_length_ms * fs_kHz;
         ALLOC( x_bufFIX, silk_max( old_buf_samples, new_buf_samples ),
              fl_i16 );
         silk_float2short_array( x_bufFIX, psEnc->x_buf, old_buf_samples );
         ALLOC( temp_resampler_state, 1, silk_resampler_state );
         ret += silk_resampler_init( temp_resampler_state, silk_SMULBB( psEnc->sCmn.fs_kHz, 1000 ), psEnc->sCmn.API_fs_Hz, 0 );
         api_buf_samples = buf_length_ms * silk_DIV32_16( psEnc->sCmn.API_fs_Hz, 1000 );
         ALLOC( x_buf_API_fs_Hz, api_buf_samples, fl_i16 );
         ret += silk_resampler( temp_resampler_state, x_buf_API_fs_Hz, x_bufFIX, old_buf_samples );
         ret += silk_resampler_init( &psEnc->sCmn.resampler_state, psEnc->sCmn.API_fs_Hz, silk_SMULBB( fs_kHz, 1000 ), 1 );
         ret += silk_resampler( &psEnc->sCmn.resampler_state, x_bufFIX, x_buf_API_fs_Hz, api_buf_samples );
         silk_short2float_array( psEnc->x_buf, x_bufFIX, new_buf_samples);
      }
   }
   psEnc->sCmn.prev_API_fs_Hz = psEnc->sCmn.API_fs_Hz;
   RESTORE_STACK;
   return ret;
}

static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
)
{
   int ret = SILK_NO_ERROR;
   if( PacketSize_ms != psEnc->sCmn.PacketSize_ms ) {
      if( ( PacketSize_ms != 10 ) &&
         ( PacketSize_ms != 20 ) &&
         ( PacketSize_ms != 40 ) &&
         ( PacketSize_ms != 60 ) ) {
         ret = SILK_ENC_PACKET_SIZE_NOT_SUPPORTED;
      }
      if( PacketSize_ms <= 10 ) {
         psEnc->sCmn.nFramesPerPacket = 1;
         psEnc->sCmn.nb_subfr = PacketSize_ms == 10 ? 2 : 1;
         psEnc->sCmn.frame_length = silk_SMULBB( PacketSize_ms, fs_kHz );
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS_2_SF, fs_kHz );
         if( psEnc->sCmn.fs_kHz == 8 ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_iCDF;
         }
      } else {
         psEnc->sCmn.nFramesPerPacket = silk_DIV32_16( PacketSize_ms, MAX_FRAME_LENGTH_MS );
         psEnc->sCmn.nb_subfr = MAX_NB_SUBFR;
         psEnc->sCmn.frame_length = silk_SMULBB( 20, fs_kHz );
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS, fs_kHz );
         if( psEnc->sCmn.fs_kHz == 8 ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_iCDF;
         }
      }
      psEnc->sCmn.PacketSize_ms = PacketSize_ms;
      psEnc->sCmn.TargetRate_bps = 0;
   }
   celt_assert( fs_kHz == 8 || fs_kHz == 12 || fs_kHz == 16 );
   celt_assert( psEnc->sCmn.nb_subfr == 2 || psEnc->sCmn.nb_subfr == 4 );
   if( psEnc->sCmn.fs_kHz != fs_kHz ) {
      silk_memset( &psEnc->sShape, 0, sizeof( psEnc->sShape ) );
      silk_memset( &psEnc->sCmn.sNSQ, 0, sizeof( psEnc->sCmn.sNSQ ) );
      silk_memset( psEnc->sCmn.prev_NLSFq_Q15, 0, sizeof( psEnc->sCmn.prev_NLSFq_Q15 ) );
      silk_memset( &psEnc->sCmn.sLP.In_LP_State, 0, sizeof( psEnc->sCmn.sLP.In_LP_State ) );
      psEnc->sCmn.inputBufIx = 0;
      psEnc->sCmn.nFramesEncoded = 0;
      psEnc->sCmn.TargetRate_bps = 0;
      psEnc->sCmn.prevLag = 100;
      psEnc->sCmn.first_frame_after_reset = 1;
      psEnc->sShape.LastGainIndex = 10;
      psEnc->sCmn.sNSQ.lagPrev = 100;
      psEnc->sCmn.sNSQ.prev_gain_Q16 = 65536;
      psEnc->sCmn.prevSignalType = TYPE_NO_VOICE_ACTIVITY;
      psEnc->sCmn.fs_kHz = fs_kHz;
      if( psEnc->sCmn.fs_kHz == 8 ) {
         if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_NB_iCDF;
         }
      } else {
         if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_iCDF;
         }
      }
      if( psEnc->sCmn.fs_kHz == 8 || psEnc->sCmn.fs_kHz == 12 ) {
         psEnc->sCmn.predictLPCOrder = MIN_LPC_ORDER;
         psEnc->sCmn.psNLSF_CB = &fl_NLSF_CB_NB_MB;
      } else {
         psEnc->sCmn.predictLPCOrder = MAX_LPC_ORDER;
         psEnc->sCmn.psNLSF_CB = &fl_NLSF_CB_WB;
      }
      psEnc->sCmn.subfr_length = SUB_FRAME_LENGTH_MS * fs_kHz;
      psEnc->sCmn.frame_length = silk_SMULBB( psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr );
      psEnc->sCmn.ltp_mem_length = silk_SMULBB( LTP_MEM_LENGTH_MS, fs_kHz );
      psEnc->sCmn.la_pitch = silk_SMULBB( LA_PITCH_MS, fs_kHz );
      psEnc->sCmn.max_pitch_lag = silk_SMULBB( 18, fs_kHz );
      if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS, fs_kHz );
      } else {
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS_2_SF, fs_kHz );
      }
      if( psEnc->sCmn.fs_kHz == 16 ) {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform8_iCDF;
      } else if( psEnc->sCmn.fs_kHz == 12 ) {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform6_iCDF;
      } else {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform4_iCDF;
      }
   }
   celt_assert( ( psEnc->sCmn.subfr_length * psEnc->sCmn.nb_subfr ) == psEnc->sCmn.frame_length );
   return ret;
}

static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
)
{
   int ret = 0;
   celt_assert( Complexity >= 0 && Complexity <= 10 );
   if( Complexity < 1 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MIN_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.8, 16 );
      psEncC->pitchEstimationLPCOrder = 6;
      psEncC->shapingLPCOrder = 12;
      psEncC->la_shape = 3 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 1;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 2;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 2 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.76, 16 );
      psEncC->pitchEstimationLPCOrder = 8;
      psEncC->shapingLPCOrder = 14;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 1;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 3;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 3 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MIN_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.8, 16 );
      psEncC->pitchEstimationLPCOrder = 6;
      psEncC->shapingLPCOrder = 12;
      psEncC->la_shape = 3 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 2;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 4 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.76, 16 );
      psEncC->pitchEstimationLPCOrder = 8;
      psEncC->shapingLPCOrder = 14;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 4;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 6 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.74, 16 );
      psEncC->pitchEstimationLPCOrder = 10;
      psEncC->shapingLPCOrder = 16;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 6;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   } else if( Complexity < 8 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.72, 16 );
      psEncC->pitchEstimationLPCOrder = 12;
      psEncC->shapingLPCOrder = 20;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 3;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 8;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   } else {
      psEncC->pitchEstimationComplexity = SILK_PE_MAX_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.7, 16 );
      psEncC->pitchEstimationLPCOrder = 16;
      psEncC->shapingLPCOrder = 24;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = MAX_DEL_DEC_STATES;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 16;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   }
   psEncC->pitchEstimationLPCOrder = silk_min_int( psEncC->pitchEstimationLPCOrder, psEncC->predictLPCOrder );
   psEncC->shapeWinLength = SUB_FRAME_LENGTH_MS * psEncC->fs_kHz + 2 * psEncC->la_shape;
   psEncC->Complexity = Complexity;
   celt_assert( psEncC->pitchEstimationLPCOrder <= MAX_FIND_PITCH_LPC_ORDER );
   celt_assert( psEncC->shapingLPCOrder <= MAX_SHAPE_LPC_ORDER );
   celt_assert( psEncC->nStatesDelayedDecision <= MAX_DEL_DEC_STATES );
   celt_assert( psEncC->warping_Q16 <= 32767 );
   celt_assert( psEncC->la_shape <= LA_SHAPE_MAX );
   celt_assert( psEncC->shapeWinLength <= SHAPE_LPC_WIN_MAX );
   return ret;
}

static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
)
{
   int LBRR_in_previous_packet, ret = SILK_NO_ERROR;
   LBRR_in_previous_packet = psEncC->LBRR_enabled;
   psEncC->LBRR_enabled = encControl->LBRR_coded;
   if( psEncC->LBRR_enabled ) {
      if( LBRR_in_previous_packet == 0 ) {
         psEncC->LBRR_GainIncreases = 7;
      } else {
         psEncC->LBRR_GainIncreases = silk_max_int( 7 - silk_SMULWB( (fl_i32)psEncC->PacketLoss_perc, SILK_FIX_CONST( 0.2, 16 ) ), 3 );
      }
   }
   return ret;
}

static void silk_HP_variable_cutoff(
   silk_encoder_state_Fxx state_Fxx[]
)
{
  int quality_Q15;
  fl_i32 pitch_freq_Hz_Q16, pitch_freq_log_Q7, delta_freq_Q7;
  silk_encoder_state *psEncC1 = &state_Fxx[ 0 ].sCmn;
  if( psEncC1->prevSignalType == TYPE_VOICED ) {
    pitch_freq_Hz_Q16 = silk_DIV32_16( silk_LSHIFT( silk_MUL( psEncC1->fs_kHz, 1000 ), 16 ), psEncC1->prevLag );
    pitch_freq_log_Q7 = silk_lin2log( pitch_freq_Hz_Q16 ) - ( 16 << 7 );
    quality_Q15 = psEncC1->input_quality_bands_Q15[ 0 ];
    pitch_freq_log_Q7 = silk_SMLAWB( pitch_freq_log_Q7, silk_SMULWB( silk_LSHIFT( -quality_Q15, 2 ), quality_Q15 ),
         pitch_freq_log_Q7 - ( silk_lin2log( SILK_FIX_CONST( VARIABLE_HP_MIN_CUTOFF_HZ, 16 ) ) - ( 16 << 7 ) ) );
    delta_freq_Q7 = pitch_freq_log_Q7 - silk_RSHIFT( psEncC1->variable_HP_smth1_Q15, 8 );
    if( delta_freq_Q7 < 0 ) {
      delta_freq_Q7 = silk_MUL( delta_freq_Q7, 3 );
    }
    delta_freq_Q7 = silk_LIMIT_32( delta_freq_Q7, -SILK_FIX_CONST( VARIABLE_HP_MAX_DELTA_FREQ, 7 ), SILK_FIX_CONST( VARIABLE_HP_MAX_DELTA_FREQ, 7 ) );
    psEncC1->variable_HP_smth1_Q15 = silk_SMLAWB( psEncC1->variable_HP_smth1_Q15,
         silk_SMULBB( psEncC1->speech_activity_Q8, delta_freq_Q7 ), SILK_FIX_CONST( VARIABLE_HP_SMTH_COEF1, 16 ) );
    psEncC1->variable_HP_smth1_Q15 = silk_LIMIT_32( psEncC1->variable_HP_smth1_Q15,
         silk_LSHIFT( silk_lin2log( VARIABLE_HP_MIN_CUTOFF_HZ ), 8 ),
         silk_LSHIFT( silk_lin2log( VARIABLE_HP_MAX_CUTOFF_HZ ), 8 ) );
  }
}

static inline void silk_LP_interpolate_filter_taps(
   fl_i32 B_Q28[ TRANSITION_NB ],
   fl_i32 A_Q28[ TRANSITION_NA ],
   const int ind,
   const fl_i32 fac_Q16
)
{
   int nb, na;
   if( ind < TRANSITION_INT_NUM - 1 ) {
      if( fac_Q16 > 0 ) {
         if( fac_Q16 < 32768 ) {
            for( nb = 0; nb < TRANSITION_NB; nb++ ) {
               B_Q28[ nb ] = silk_SMLAWB(
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ] -
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fac_Q16 );
            }
            for( na = 0; na < TRANSITION_NA; na++ ) {
               A_Q28[ na ] = silk_SMLAWB(
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ] -
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fac_Q16 );
            }
         } else {
            silk_assert( fac_Q16 - ( 1 << 16 ) == silk_SAT16( fac_Q16 - ( 1 << 16 ) ) );
            for( nb = 0; nb < TRANSITION_NB; nb++ ) {
               B_Q28[ nb ] = silk_SMLAWB(
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ],
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ] -
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fac_Q16 - ( (fl_i32)1 << 16 ) );
            }
            for( na = 0; na < TRANSITION_NA; na++ ) {
               A_Q28[ na ] = silk_SMLAWB(
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ],
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ] -
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fac_Q16 - ( (fl_i32)1 << 16 ) );
            }
         }
      } else {
         silk_memcpy( B_Q28, fl_Transition_LP_B_Q28[ ind ], TRANSITION_NB * sizeof( fl_i32 ) );
         silk_memcpy( A_Q28, fl_Transition_LP_A_Q28[ ind ], TRANSITION_NA * sizeof( fl_i32 ) );
      }
   } else {
      silk_memcpy( B_Q28, fl_Transition_LP_B_Q28[ TRANSITION_INT_NUM - 1 ], TRANSITION_NB * sizeof( fl_i32 ) );
      silk_memcpy( A_Q28, fl_Transition_LP_A_Q28[ TRANSITION_INT_NUM - 1 ], TRANSITION_NA * sizeof( fl_i32 ) );
   }
}

static void silk_LP_variable_cutoff(
   silk_LP_state *psLP,
   fl_i16 *frame,
   const int frame_length
)
{
   fl_i32 B_Q28[ TRANSITION_NB ], A_Q28[ TRANSITION_NA ], fac_Q16 = 0;
   int ind = 0;
   silk_assert( psLP->transition_frame_no >= 0 && psLP->transition_frame_no <= TRANSITION_FRAMES );
   if( psLP->mode != 0 ) {
      fac_Q16 = silk_LSHIFT( TRANSITION_FRAMES - psLP->transition_frame_no, 16 - 6 );
      fac_Q16 = silk_DIV32_16( silk_LSHIFT( TRANSITION_FRAMES - psLP->transition_frame_no, 16 ), TRANSITION_FRAMES );
      ind = silk_RSHIFT( fac_Q16, 16 );
      fac_Q16 -= silk_LSHIFT( ind, 16 );
      silk_assert( ind >= 0 );
      silk_assert( ind < TRANSITION_INT_NUM );
      silk_LP_interpolate_filter_taps( B_Q28, A_Q28, ind, fac_Q16 );
      psLP->transition_frame_no = silk_LIMIT( psLP->transition_frame_no + psLP->mode, 0, TRANSITION_FRAMES );
      silk_assert( TRANSITION_NB == 3 && TRANSITION_NA == 2 );
      silk_biquad_alt_stride1( frame, B_Q28, A_Q28, psLP->In_LP_State, frame, frame_length);
   }
}

static void silk_biquad_alt_stride1(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
)
{
   int k;
   fl_i32 inval, A0_U_Q28, A0_L_Q28, A1_U_Q28, A1_L_Q28, out32_Q14;
   A0_L_Q28 = ( -A_Q28[ 0 ] ) & 0x00003FFF;
   A0_U_Q28 = silk_RSHIFT( -A_Q28[ 0 ], 14 );
   A1_L_Q28 = ( -A_Q28[ 1 ] ) & 0x00003FFF;
   A1_U_Q28 = silk_RSHIFT( -A_Q28[ 1 ], 14 );
   for( k = 0; k < len; k++ ) {
      inval = in[ k ];
      out32_Q14 = silk_LSHIFT( silk_SMLAWB( S[ 0 ], B_Q28[ 0 ], inval ), 2 );
      S[ 0 ] = S[1] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14, A0_L_Q28 ), 14 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], out32_Q14, A0_U_Q28 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], B_Q28[ 1 ], inval);
      S[ 1 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14, A1_L_Q28 ), 14 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], out32_Q14, A1_U_Q28 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], B_Q28[ 2 ], inval );
      out[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14 + (1<<14) - 1, 14 ) );
   }
}

static void silk_biquad_alt_stride2_c(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
)
{
   int k;
   fl_i32 A0_U_Q28, A0_L_Q28, A1_U_Q28, A1_L_Q28, out32_Q14[ 2 ];
   A0_L_Q28 = ( -A_Q28[ 0 ] ) & 0x00003FFF;
   A0_U_Q28 = silk_RSHIFT( -A_Q28[ 0 ], 14 );
   A1_L_Q28 = ( -A_Q28[ 1 ] ) & 0x00003FFF;
   A1_U_Q28 = silk_RSHIFT( -A_Q28[ 1 ], 14 );
   for( k = 0; k < len; k++ ) {
      out32_Q14[ 0 ] = silk_LSHIFT( silk_SMLAWB( S[ 0 ], B_Q28[ 0 ], in[ 2 * k + 0 ] ), 2 );
      out32_Q14[ 1 ] = silk_LSHIFT( silk_SMLAWB( S[ 2 ], B_Q28[ 0 ], in[ 2 * k + 1 ] ), 2 );
      S[ 0 ] = S[ 1 ] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 0 ], A0_L_Q28 ), 14 );
      S[ 2 ] = S[ 3 ] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 1 ], A0_L_Q28 ), 14 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], out32_Q14[ 0 ], A0_U_Q28 );
      S[ 2 ] = silk_SMLAWB( S[ 2 ], out32_Q14[ 1 ], A0_U_Q28 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], B_Q28[ 1 ], in[ 2 * k + 0 ] );
      S[ 2 ] = silk_SMLAWB( S[ 2 ], B_Q28[ 1 ], in[ 2 * k + 1 ] );
      S[ 1 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 0 ], A1_L_Q28 ), 14 );
      S[ 3 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 1 ], A1_L_Q28 ), 14 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], out32_Q14[ 0 ], A1_U_Q28 );
      S[ 3 ] = silk_SMLAWB( S[ 3 ], out32_Q14[ 1 ], A1_U_Q28 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], B_Q28[ 2 ], in[ 2 * k + 0 ] );
      S[ 3 ] = silk_SMLAWB( S[ 3 ], B_Q28[ 2 ], in[ 2 * k + 1 ] );
      out[ 2 * k + 0 ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14[ 0 ] + (1<<14) - 1, 14 ) );
      out[ 2 * k + 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14[ 1 ] + (1<<14) - 1, 14 ) );
   }
}

static int silk_sigm_Q15(
   int in_Q5
)
{
   int ind;
   if( in_Q5 < 0 ) {
      in_Q5 = -in_Q5;
      if( in_Q5 >= 6 * 32 ) {
         return 0;
      } else {
         ind = silk_RSHIFT( in_Q5, 5 );
         return( sigm_LUT_neg_Q15[ ind ] - silk_SMULBB( sigm_LUT_slope_Q10[ ind ], in_Q5 & 0x1F ) );
      }
   } else {
      if( in_Q5 >= 6 * 32 ) {
         return 32767;
      } else {
         ind = silk_RSHIFT( in_Q5, 5 );
         return( sigm_LUT_pos_Q15[ ind ] + silk_SMULBB( sigm_LUT_slope_Q10[ ind ], in_Q5 & 0x1F ) );
      }
   }
}

static void silk_ana_filt_bank_1(
   const fl_i16 *in,
   fl_i32 *S,
   fl_i16 *outL,
   fl_i16 *outH,
   const fl_i32 N
)
{
   int k, N2 = silk_RSHIFT( N, 1 );
   fl_i32 in32, X, Y, out_1, out_2;
   for( k = 0; k < N2; k++ ) {
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k ], 10 );
      Y = silk_SUB32( in32, S[ 0 ] );
      X = silk_SMLAWB( Y, Y, A_fb1_21 );
      out_1 = silk_ADD32( S[ 0 ], X );
      S[ 0 ] = silk_ADD32( in32, X );
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k + 1 ], 10 );
      Y = silk_SUB32( in32, S[ 1 ] );
      X = silk_SMULWB( Y, A_fb1_20 );
      out_2 = silk_ADD32( S[ 1 ], X );
      S[ 1 ] = silk_ADD32( in32, X );
      outL[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_ADD32( out_2, out_1 ), 11 ) );
      outH[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_SUB32( out_2, out_1 ), 11 ) );
   }
}

static int silk_VAD_Init(
   silk_VAD_state *psSilk_VAD
)
{
   int b, ret = 0;
   silk_memset( psSilk_VAD, 0, sizeof( silk_VAD_state ) );
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NoiseLevelBias[ b ] = silk_max_32( silk_DIV32_16( VAD_NOISE_LEVELS_BIAS, b + 1 ), 1 );
   }
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NL[ b ] = silk_MUL( 100, psSilk_VAD->NoiseLevelBias[ b ] );
      psSilk_VAD->inv_NL[ b ] = silk_DIV32( silk_int32_MAX, psSilk_VAD->NL[ b ] );
   }
   psSilk_VAD->counter = 15;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NrgRatioSmth_Q8[ b ] = 100 * 256;
   }
   return( ret );
}

static int silk_VAD_GetSA_Q8_c(
   silk_encoder_state *psEncC,
   const fl_i16 pIn[]
)
{
   int SA_Q15, pSNR_dB_Q7, input_tilt;
   int decimated_framelength1, decimated_framelength2;
   int decimated_framelength;
   int dec_subframe_length, dec_subframe_offset, SNR_Q7, i, b, s;
   fl_i32 sumSquared, smooth_coef_Q16;
   fl_i16 HPstateTmp;
   VARDECL( fl_i16, X );
   fl_i32 Xnrg[ VAD_N_BANDS ];
   fl_i32 NrgToNoiseRatio_Q8[ VAD_N_BANDS ];
   fl_i32 speech_nrg, x_tmp;
   int X_offset[ VAD_N_BANDS ];
   int ret = 0;
   silk_VAD_state *psSilk_VAD = &psEncC->sVAD;
   SAVE_STACK;
   silk_assert( VAD_N_BANDS == 4 );
   celt_assert( MAX_FRAME_LENGTH >= psEncC->frame_length );
   celt_assert( psEncC->frame_length <= 512 );
   celt_assert( psEncC->frame_length == 8 * silk_RSHIFT( psEncC->frame_length, 3 ) );
   decimated_framelength1 = silk_RSHIFT( psEncC->frame_length, 1 );
   decimated_framelength2 = silk_RSHIFT( psEncC->frame_length, 2 );
   decimated_framelength = silk_RSHIFT( psEncC->frame_length, 3 );
   X_offset[ 0 ] = 0;
   X_offset[ 1 ] = decimated_framelength + decimated_framelength2;
   X_offset[ 2 ] = X_offset[ 1 ] + decimated_framelength;
   X_offset[ 3 ] = X_offset[ 2 ] + decimated_framelength2;
   ALLOC( X, X_offset[ 3 ] + decimated_framelength1, fl_i16 );
   silk_ana_filt_bank_1( pIn, &psSilk_VAD->AnaState[ 0 ],
      X, &X[ X_offset[ 3 ] ], psEncC->frame_length );
   silk_ana_filt_bank_1( X, &psSilk_VAD->AnaState1[ 0 ],
      X, &X[ X_offset[ 2 ] ], decimated_framelength1 );
   silk_ana_filt_bank_1( X, &psSilk_VAD->AnaState2[ 0 ],
      X, &X[ X_offset[ 1 ] ], decimated_framelength2 );
   X[ decimated_framelength - 1 ] = silk_RSHIFT( X[ decimated_framelength - 1 ], 1 );
   HPstateTmp = X[ decimated_framelength - 1 ];
   for( i = decimated_framelength - 1; i > 0; i-- ) {
      X[ i - 1 ] = silk_RSHIFT( X[ i - 1 ], 1 );
      X[ i ] -= X[ i - 1 ];
   }
   X[ 0 ] -= psSilk_VAD->HPstate;
   psSilk_VAD->HPstate = HPstateTmp;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      decimated_framelength = silk_RSHIFT( psEncC->frame_length, silk_min_int( VAD_N_BANDS - b, VAD_N_BANDS - 1 ) );
      dec_subframe_length = silk_RSHIFT( decimated_framelength, VAD_INTERNAL_SUBFRAMES_LOG2 );
      dec_subframe_offset = 0;
      Xnrg[ b ] = psSilk_VAD->XnrgSubfr[ b ];
      for( s = 0; s < VAD_INTERNAL_SUBFRAMES; s++ ) {
         sumSquared = 0;
         for( i = 0; i < dec_subframe_length; i++ ) {
            x_tmp = silk_RSHIFT(
               X[ X_offset[ b ] + i + dec_subframe_offset ], 3 );
            sumSquared = silk_SMLABB( sumSquared, x_tmp, x_tmp );
            silk_assert( sumSquared >= 0 );
         }
         if( s < VAD_INTERNAL_SUBFRAMES - 1 ) {
            Xnrg[ b ] = silk_ADD_POS_SAT32( Xnrg[ b ], sumSquared );
         } else {
            Xnrg[ b ] = silk_ADD_POS_SAT32( Xnrg[ b ], silk_RSHIFT( sumSquared, 1 ) );
         }
         dec_subframe_offset += dec_subframe_length;
      }
      psSilk_VAD->XnrgSubfr[ b ] = sumSquared;
   }
   silk_VAD_GetNoiseLevels( &Xnrg[ 0 ], psSilk_VAD );
   sumSquared = 0;
   input_tilt = 0;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      speech_nrg = Xnrg[ b ] - psSilk_VAD->NL[ b ];
      if( speech_nrg > 0 ) {
         if( ( Xnrg[ b ] & 0xFF800000 ) == 0 ) {
            NrgToNoiseRatio_Q8[ b ] = silk_DIV32( silk_LSHIFT( Xnrg[ b ], 8 ), psSilk_VAD->NL[ b ] + 1 );
         } else {
            NrgToNoiseRatio_Q8[ b ] = silk_DIV32( Xnrg[ b ], silk_RSHIFT( psSilk_VAD->NL[ b ], 8 ) + 1 );
         }
         SNR_Q7 = silk_lin2log( NrgToNoiseRatio_Q8[ b ] ) - 8 * 128;
         sumSquared = silk_SMLABB( sumSquared, SNR_Q7, SNR_Q7 );
         if( speech_nrg < ( (fl_i32)1 << 20 ) ) {
            SNR_Q7 = silk_SMULWB( silk_LSHIFT( silk_SQRT_APPROX( speech_nrg ), 6 ), SNR_Q7 );
         }
         input_tilt = silk_SMLAWB( input_tilt, tiltWeights[ b ], SNR_Q7 );
      } else {
         NrgToNoiseRatio_Q8[ b ] = 256;
      }
   }
   sumSquared = silk_DIV32_16( sumSquared, VAD_N_BANDS );
   pSNR_dB_Q7 = (fl_i16)( 3 * silk_SQRT_APPROX( sumSquared ) );
   SA_Q15 = silk_sigm_Q15( silk_SMULWB( VAD_SNR_FACTOR_Q16, pSNR_dB_Q7 ) - VAD_NEGATIVE_OFFSET_Q5 );
   psEncC->input_tilt_Q15 = silk_LSHIFT( silk_sigm_Q15( input_tilt ) - 16384, 1 );
   speech_nrg = 0;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      speech_nrg += ( b + 1 ) * silk_RSHIFT( Xnrg[ b ] - psSilk_VAD->NL[ b ], 4 );
   }
   if( psEncC->frame_length == 20 * psEncC->fs_kHz ) {
      speech_nrg = silk_RSHIFT32( speech_nrg, 1 );
   }
   if( speech_nrg <= 0 ) {
      SA_Q15 = silk_RSHIFT( SA_Q15, 1 );
   } else if( speech_nrg < 16384 ) {
      speech_nrg = silk_LSHIFT32( speech_nrg, 16 );
      speech_nrg = silk_SQRT_APPROX( speech_nrg );
      SA_Q15 = silk_SMULWB( 32768 + speech_nrg, SA_Q15 );
   }
   psEncC->speech_activity_Q8 = silk_min_int( silk_RSHIFT( SA_Q15, 7 ), silk_uint8_MAX );
   smooth_coef_Q16 = silk_SMULWB( VAD_SNR_SMOOTH_COEF_Q18, silk_SMULWB( (fl_i32)SA_Q15, SA_Q15 ) );
   if( psEncC->frame_length == 10 * psEncC->fs_kHz ) {
      smooth_coef_Q16 >>= 1;
   }
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NrgRatioSmth_Q8[ b ] = silk_SMLAWB( psSilk_VAD->NrgRatioSmth_Q8[ b ],
         NrgToNoiseRatio_Q8[ b ] - psSilk_VAD->NrgRatioSmth_Q8[ b ], smooth_coef_Q16 );
      SNR_Q7 = 3 * ( silk_lin2log( psSilk_VAD->NrgRatioSmth_Q8[b] ) - 8 * 128 );
      psEncC->input_quality_bands_Q15[ b ] = silk_sigm_Q15( silk_RSHIFT( SNR_Q7 - 16 * 128, 4 ) );
   }
   RESTORE_STACK;
   return( ret );
}

static void silk_VAD_GetNoiseLevels(
   const fl_i32 pX[ VAD_N_BANDS ],
   silk_VAD_state *psSilk_VAD
)
{
   int k;
   fl_i32 nl, nrg, inv_nrg;
   int coef, min_coef;
   if( psSilk_VAD->counter < 1000 ) {
      min_coef = silk_DIV32_16( silk_int16_MAX, silk_RSHIFT( psSilk_VAD->counter, 4 ) + 1 );
      psSilk_VAD->counter++;
   } else {
      min_coef = 0;
   }
   for( k = 0; k < VAD_N_BANDS; k++ ) {
      nl = psSilk_VAD->NL[ k ];
      silk_assert( nl >= 0 );
      nrg = silk_ADD_POS_SAT32( pX[ k ], psSilk_VAD->NoiseLevelBias[ k ] );
      silk_assert( nrg > 0 );
      inv_nrg = silk_DIV32( silk_int32_MAX, nrg );
      silk_assert( inv_nrg >= 0 );
      if( nrg > silk_LSHIFT( nl, 3 ) ) {
         coef = VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 >> 3;
      } else if( nrg < nl ) {
         coef = VAD_NOISE_LEVEL_SMOOTH_COEF_Q16;
      } else {
         coef = silk_SMULWB( silk_SMULWW( inv_nrg, nl ), VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 << 1 );
      }
      coef = silk_max_int( coef, min_coef );
      psSilk_VAD->inv_NL[ k ] = silk_SMLAWB( psSilk_VAD->inv_NL[ k ], inv_nrg - psSilk_VAD->inv_NL[ k ], coef );
      silk_assert( psSilk_VAD->inv_NL[ k ] >= 0 );
      nl = silk_DIV32( silk_int32_MAX, psSilk_VAD->inv_NL[ k ] );
      silk_assert( nl >= 0 );
      nl = silk_min( nl, 0x00FFFFFF );
      psSilk_VAD->NL[ k ] = nl;
   }
}

static fl_i32 silk_inner_prod_aligned_scale(
   const fl_i16 *const inVec1,
   const fl_i16 *const inVec2,
   const int scale,
   const int len
)
{
   int i;
   fl_i32 sum = 0;
   for( i = 0; i < len; i++ ) {
      sum = silk_ADD_RSHIFT32( sum, silk_SMULBB( inVec1[ i ], inVec2[ i ] ), scale );
   }
   return sum;
}

static fl_i32 silk_stereo_find_predictor(
   fl_i32 *ratio_Q14,
   const fl_i16 x[],
   const fl_i16 y[],
   fl_i32 mid_res_amp_Q0[],
   int length,
   int smooth_coef_Q16
)
{
   int scale, scale1, scale2;
   fl_i32 nrgx, nrgy, corr, pred_Q13, pred2_Q10;
   silk_sum_sqr_shift( &nrgx, &scale1, x, length );
   silk_sum_sqr_shift( &nrgy, &scale2, y, length );
   scale = silk_max_int( scale1, scale2 );
   scale = scale + ( scale & 1 );
   nrgy = silk_RSHIFT32( nrgy, scale - scale2 );
   nrgx = silk_RSHIFT32( nrgx, scale - scale1 );
   nrgx = silk_max_int( nrgx, 1 );
   corr = silk_inner_prod_aligned_scale( x, y, scale, length );
   pred_Q13 = silk_DIV32_varQ( corr, nrgx, 13 );
   pred_Q13 = silk_LIMIT( pred_Q13, -(1 << 14), 1 << 14 );
   pred2_Q10 = silk_SMULWB( pred_Q13, pred_Q13 );
   smooth_coef_Q16 = (int)silk_max_int( smooth_coef_Q16, silk_abs( pred2_Q10 ) );
   silk_assert( smooth_coef_Q16 < 32768 );
   scale = silk_RSHIFT( scale, 1 );
   mid_res_amp_Q0[ 0 ] = silk_SMLAWB( mid_res_amp_Q0[ 0 ], silk_LSHIFT( silk_SQRT_APPROX( nrgx ), scale ) - mid_res_amp_Q0[ 0 ],
      smooth_coef_Q16 );
   nrgy = silk_SUB_LSHIFT32( nrgy, silk_SMULWB( corr, pred_Q13 ), 3 + 1 );
   nrgy = silk_ADD_LSHIFT32( nrgy, silk_SMULWB( nrgx, pred2_Q10 ), 6 );
   mid_res_amp_Q0[ 1 ] = silk_SMLAWB( mid_res_amp_Q0[ 1 ], silk_LSHIFT( silk_SQRT_APPROX( nrgy ), scale ) - mid_res_amp_Q0[ 1 ],
      smooth_coef_Q16 );
   *ratio_Q14 = silk_DIV32_varQ( mid_res_amp_Q0[ 1 ], silk_max( mid_res_amp_Q0[ 0 ], 1 ), 14 );
   *ratio_Q14 = silk_LIMIT( *ratio_Q14, 0, 32767 );
   return pred_Q13;
}

static void silk_stereo_quant_pred(
   fl_i32 pred_Q13[],
   fl_i8 ix[ 2 ][ 3 ]
)
{
   int i, j, n;
   fl_i32 low_Q13, step_Q13, lvl_Q13, err_min_Q13, err_Q13, quant_pred_Q13 = 0;
   for( n = 0; n < 2; n++ ) {
      err_min_Q13 = silk_int32_MAX;
      for( i = 0; i < STEREO_QUANT_TAB_SIZE - 1; i++ ) {
         low_Q13 = fl_stereo_pred_quant_Q13[ i ];
         step_Q13 = silk_SMULWB( fl_stereo_pred_quant_Q13[ i + 1 ] - low_Q13,
            SILK_FIX_CONST( 0.5 / STEREO_QUANT_SUB_STEPS, 16 ) );
         for( j = 0; j < STEREO_QUANT_SUB_STEPS; j++ ) {
            lvl_Q13 = silk_SMLABB( low_Q13, step_Q13, 2 * j + 1 );
            err_Q13 = silk_abs( pred_Q13[ n ] - lvl_Q13 );
            if( err_Q13 < err_min_Q13 ) {
               err_min_Q13 = err_Q13;
               quant_pred_Q13 = lvl_Q13;
               ix[ n ][ 0 ] = i;
               ix[ n ][ 1 ] = j;
            } else {
               goto done;
            }
         }
      }
      done:
      ix[ n ][ 2 ] = silk_DIV32_16( ix[ n ][ 0 ], 3 );
      ix[ n ][ 0 ] -= ix[ n ][ 2 ] * 3;
      pred_Q13[ n ] = quant_pred_Q13;
   }
   pred_Q13[ 0 ] -= pred_Q13[ 1 ];
}

static void silk_stereo_encode_pred(
   ec_ctx *psRangeEnc,
   fl_i8 ix[ 2 ][ 3 ]
)
{
   int n;
   n = 5 * ix[ 0 ][ 2 ] + ix[ 1 ][ 2 ];
   celt_assert( n < 25 );
   ec_enc_icdf( psRangeEnc, n, fl_stereo_pred_joint_iCDF, 8 );
   for( n = 0; n < 2; n++ ) {
      celt_assert( ix[ n ][ 0 ] < 3 );
      celt_assert( ix[ n ][ 1 ] < STEREO_QUANT_SUB_STEPS );
      ec_enc_icdf( psRangeEnc, ix[ n ][ 0 ], fl_uniform3_iCDF, 8 );
      ec_enc_icdf( psRangeEnc, ix[ n ][ 1 ], fl_uniform5_iCDF, 8 );
   }
}

static void silk_stereo_encode_mid_only(
   ec_ctx *psRangeEnc,
   fl_i8 mid_only_flag
)
{
   ec_enc_icdf( psRangeEnc, mid_only_flag, fl_stereo_only_code_mid_iCDF, 8 );
}

static void silk_stereo_LR_to_MS(
   stereo_enc_state *state,
   fl_i16 x1[],
   fl_i16 x2[],
   fl_i8 ix[ 2 ][ 3 ],
   fl_i8 *mid_only_flag,
   fl_i32 mid_side_rates_bps[],
   fl_i32 total_rate_bps,
   int prev_speech_act_Q8,
   int toMono,
   int fs_kHz,
   int frame_length
)
{
   int n, is10msFrame, denom_Q16, delta0_Q13, delta1_Q13;
   fl_i32 sum, diff, smooth_coef_Q16, pred_Q13[ 2 ], pred0_Q13, pred1_Q13;
   fl_i32 LP_ratio_Q14, HP_ratio_Q14, frac_Q16, frac_3_Q16, min_mid_rate_bps, width_Q14, w_Q24, deltaw_Q24;
   VARDECL( fl_i16, side );
   VARDECL( fl_i16, LP_mid );
   VARDECL( fl_i16, HP_mid );
   VARDECL( fl_i16, LP_side );
   VARDECL( fl_i16, HP_side );
   fl_i16 *mid = &x1[ -2 ];
   SAVE_STACK;
   ALLOC( side, frame_length + 2, fl_i16 );
   for( n = 0; n < frame_length + 2; n++ ) {
      sum = x1[ n - 2 ] + (fl_i32)x2[ n - 2 ];
      diff = x1[ n - 2 ] - (fl_i32)x2[ n - 2 ];
      mid[ n ] = (fl_i16)silk_RSHIFT_ROUND( sum, 1 );
      side[ n ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( diff, 1 ) );
   }
   silk_memcpy( mid, state->sMid, 2 * sizeof( fl_i16 ) );
   silk_memcpy( side, state->sSide, 2 * sizeof( fl_i16 ) );
   silk_memcpy( state->sMid, &mid[ frame_length ], 2 * sizeof( fl_i16 ) );
   silk_memcpy( state->sSide, &side[ frame_length ], 2 * sizeof( fl_i16 ) );
   ALLOC( LP_mid, frame_length, fl_i16 );
   ALLOC( HP_mid, frame_length, fl_i16 );
   for( n = 0; n < frame_length; n++ ) {
      sum = silk_RSHIFT_ROUND( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 2 );
      LP_mid[ n ] = sum;
      HP_mid[ n ] = mid[ n + 1 ] - sum;
   }
   ALLOC( LP_side, frame_length, fl_i16 );
   ALLOC( HP_side, frame_length, fl_i16 );
   for( n = 0; n < frame_length; n++ ) {
      sum = silk_RSHIFT_ROUND( silk_ADD_LSHIFT32( side[ n ] + (fl_i32)side[ n + 2 ], side[ n + 1 ], 1 ), 2 );
      LP_side[ n ] = sum;
      HP_side[ n ] = side[ n + 1 ] - sum;
   }
   is10msFrame = frame_length == 10 * fs_kHz;
   smooth_coef_Q16 = is10msFrame ?
      SILK_FIX_CONST( STEREO_RATIO_SMOOTH_COEF / 2, 16 ) :
      SILK_FIX_CONST( STEREO_RATIO_SMOOTH_COEF, 16 );
   smooth_coef_Q16 = silk_SMULWB( silk_SMULBB( prev_speech_act_Q8, prev_speech_act_Q8 ), smooth_coef_Q16 );
   pred_Q13[ 0 ] = silk_stereo_find_predictor( &LP_ratio_Q14, LP_mid, LP_side, &state->mid_side_amp_Q0[ 0 ], frame_length, smooth_coef_Q16 );
   pred_Q13[ 1 ] = silk_stereo_find_predictor( &HP_ratio_Q14, HP_mid, HP_side, &state->mid_side_amp_Q0[ 2 ], frame_length, smooth_coef_Q16 );
   frac_Q16 = silk_SMLABB( HP_ratio_Q14, LP_ratio_Q14, 3 );
   frac_Q16 = silk_min( frac_Q16, SILK_FIX_CONST( 1, 16 ) );
   total_rate_bps -= is10msFrame ? 1200 : 600;
   if( total_rate_bps < 1 ) {
      total_rate_bps = 1;
   }
   min_mid_rate_bps = silk_SMLABB( 2000, fs_kHz, 600 );
   silk_assert( min_mid_rate_bps < 32767 );
   frac_3_Q16 = silk_MUL( 3, frac_Q16 );
   mid_side_rates_bps[ 0 ] = silk_DIV32_varQ( total_rate_bps, SILK_FIX_CONST( 8 + 5, 16 ) + frac_3_Q16, 16+3 );
   if( mid_side_rates_bps[ 0 ] < min_mid_rate_bps ) {
      mid_side_rates_bps[ 0 ] = min_mid_rate_bps;
      mid_side_rates_bps[ 1 ] = total_rate_bps - mid_side_rates_bps[ 0 ];
      width_Q14 = silk_DIV32_varQ( silk_LSHIFT( mid_side_rates_bps[ 1 ], 1 ) - min_mid_rate_bps,
         silk_SMULWB( SILK_FIX_CONST( 1, 16 ) + frac_3_Q16, min_mid_rate_bps ), 14+2 );
      width_Q14 = silk_LIMIT( width_Q14, 0, SILK_FIX_CONST( 1, 14 ) );
   } else {
      mid_side_rates_bps[ 1 ] = total_rate_bps - mid_side_rates_bps[ 0 ];
      width_Q14 = SILK_FIX_CONST( 1, 14 );
   }
   state->smth_width_Q14 = (fl_i16)silk_SMLAWB( state->smth_width_Q14, width_Q14 - state->smth_width_Q14, smooth_coef_Q16 );
   *mid_only_flag = 0;
   if( toMono ) {
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
      silk_stereo_quant_pred( pred_Q13, ix );
   } else if( state->width_prev_Q14 == 0 &&
      ( 8 * total_rate_bps < 13 * min_mid_rate_bps || silk_SMULWB( frac_Q16, state->smth_width_Q14 ) < SILK_FIX_CONST( 0.05, 14 ) ) )
   {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
      mid_side_rates_bps[ 0 ] = total_rate_bps;
      mid_side_rates_bps[ 1 ] = 0;
      *mid_only_flag = 1;
   } else if( state->width_prev_Q14 != 0 &&
      ( 8 * total_rate_bps < 11 * min_mid_rate_bps || silk_SMULWB( frac_Q16, state->smth_width_Q14 ) < SILK_FIX_CONST( 0.02, 14 ) ) )
   {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
   } else if( state->smth_width_Q14 > SILK_FIX_CONST( 0.95, 14 ) ) {
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = SILK_FIX_CONST( 1, 14 );
   } else {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = state->smth_width_Q14;
   }
   if( *mid_only_flag == 1 ) {
      state->silent_side_len += frame_length - STEREO_INTERP_LEN_MS * fs_kHz;
      if( state->silent_side_len < LA_SHAPE_MS * fs_kHz ) {
         *mid_only_flag = 0;
      } else {
         state->silent_side_len = 10000;
      }
   } else {
      state->silent_side_len = 0;
   }
   if( *mid_only_flag == 0 && mid_side_rates_bps[ 1 ] < 1 ) {
      mid_side_rates_bps[ 1 ] = 1;
      mid_side_rates_bps[ 0 ] = silk_max_int( 1, total_rate_bps - mid_side_rates_bps[ 1 ]);
   }
   pred0_Q13 = -state->pred_prev_Q13[ 0 ];
   pred1_Q13 = -state->pred_prev_Q13[ 1 ];
   w_Q24 = silk_LSHIFT( state->width_prev_Q14, 10 );
   denom_Q16 = silk_DIV32_16( (fl_i32)1 << 16, STEREO_INTERP_LEN_MS * fs_kHz );
   delta0_Q13 = -silk_RSHIFT_ROUND( silk_SMULBB( pred_Q13[ 0 ] - state->pred_prev_Q13[ 0 ], denom_Q16 ), 16 );
   delta1_Q13 = -silk_RSHIFT_ROUND( silk_SMULBB( pred_Q13[ 1 ] - state->pred_prev_Q13[ 1 ], denom_Q16 ), 16 );
   deltaw_Q24 = silk_LSHIFT( silk_SMULWB( width_Q14 - state->width_prev_Q14, denom_Q16 ), 10 );
   for( n = 0; n < STEREO_INTERP_LEN_MS * fs_kHz; n++ ) {
      pred0_Q13 += delta0_Q13;
      pred1_Q13 += delta1_Q13;
      w_Q24 += deltaw_Q24;
      sum = silk_LSHIFT( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 9 );
      sum = silk_SMLAWB( silk_SMULWB( w_Q24, side[ n + 1 ] ), sum, pred0_Q13 );
      sum = silk_SMLAWB( sum, silk_LSHIFT( (fl_i32)mid[ n + 1 ], 11 ), pred1_Q13 );
      x2[ n - 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( sum, 8 ) );
   }
   pred0_Q13 = -pred_Q13[ 0 ];
   pred1_Q13 = -pred_Q13[ 1 ];
   w_Q24 = silk_LSHIFT( width_Q14, 10 );
   for( n = STEREO_INTERP_LEN_MS * fs_kHz; n < frame_length; n++ ) {
      sum = silk_LSHIFT( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 9 );
      sum = silk_SMLAWB( silk_SMULWB( w_Q24, side[ n + 1 ] ), sum, pred0_Q13 );
      sum = silk_SMLAWB( sum, silk_LSHIFT( (fl_i32)mid[ n + 1 ], 11 ), pred1_Q13 );
      x2[ n - 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( sum, 8 ) );
   }
   state->pred_prev_Q13[ 0 ] = (fl_i16)pred_Q13[ 0 ];
   state->pred_prev_Q13[ 1 ] = (fl_i16)pred_Q13[ 1 ];
   state->width_prev_Q14 = (fl_i16)width_Q14;
   RESTORE_STACK;
}

static inline void silk_A2NLSF_trans_poly(
   fl_i32 *p,
   const int dd
)
{
   int k, n;
   for( k = 2; k <= dd; k++ ) {
      for( n = dd; n > k; n-- ) {
         p[ n - 2 ] -= p[ n ];
      }
      p[ k - 2 ] -= silk_LSHIFT( p[ k ], 1 );
   }
}

static inline fl_i32 silk_A2NLSF_eval_poly(
   fl_i32 *p,
   const fl_i32 x,
   const int dd
)
{
   int n;
   fl_i32 x_Q16, y32;
   y32 = p[ dd ];
   x_Q16 = silk_LSHIFT( x, 4 );
   if ( opus_likely( 8 == dd ) )
   {
      y32 = silk_SMLAWW( p[ 7 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 6 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 5 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 4 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 3 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 2 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 1 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 0 ], y32, x_Q16 );
   }
   else
   {
      for( n = dd - 1; n >= 0; n-- ) {
         y32 = silk_SMLAWW( p[ n ], y32, x_Q16 );
      }
   }
   return y32;
}

static inline void silk_A2NLSF_init(
   const fl_i32 *a_Q16,
   fl_i32 *P,
   fl_i32 *Q,
   const int dd
)
{
   int k;
   P[dd] = silk_LSHIFT( 1, 16 );
   Q[dd] = silk_LSHIFT( 1, 16 );
   for( k = 0; k < dd; k++ ) {
      P[ k ] = -a_Q16[ dd - k - 1 ] - a_Q16[ dd + k ];
      Q[ k ] = -a_Q16[ dd - k - 1 ] + a_Q16[ dd + k ];
   }
   for( k = dd; k > 0; k-- ) {
      P[ k - 1 ] -= P[ k ];
      Q[ k - 1 ] += Q[ k ];
   }
   silk_A2NLSF_trans_poly( P, dd );
   silk_A2NLSF_trans_poly( Q, dd );
}

static void silk_A2NLSF(
   fl_i16 *NLSF,
   fl_i32 *a_Q16,
   const int d
)
{
   int i, k, m, dd, root_ix, ffrac;
   fl_i32 xlo, xhi, xmid;
   fl_i32 ylo, yhi, ymid, thr;
   fl_i32 nom, den;
   fl_i32 P[ SILK_MAX_ORDER_LPC / 2 + 1 ];
   fl_i32 Q[ SILK_MAX_ORDER_LPC / 2 + 1 ];
   fl_i32 *PQ[ 2 ];
   fl_i32 *p;
   PQ[ 0 ] = P;
   PQ[ 1 ] = Q;
   dd = silk_RSHIFT( d, 1 );
   silk_A2NLSF_init( a_Q16, P, Q, dd );
   p = P;
   xlo = fl_LSFCosTab_FIX_Q12[ 0 ];
   ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
   if( ylo < 0 ) {
      NLSF[ 0 ] = 0;
      p = Q;
      ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
      root_ix = 1;
   } else {
      root_ix = 0;
   }
   k = 1;
   i = 0;
   thr = 0;
   while( 1 ) {
      xhi = fl_LSFCosTab_FIX_Q12[ k ];
      yhi = silk_A2NLSF_eval_poly( p, xhi, dd );
      if( ( ylo <= 0 && yhi >= thr ) || ( ylo >= 0 && yhi <= -thr ) ) {
         if( yhi == 0 ) {
            thr = 1;
         } else {
            thr = 0;
         }
         ffrac = -256;
         for( m = 0; m < BIN_DIV_STEPS_A2NLSF_FIX; m++ ) {
            xmid = silk_RSHIFT_ROUND( xlo + xhi, 1 );
            ymid = silk_A2NLSF_eval_poly( p, xmid, dd );
            if( ( ylo <= 0 && ymid >= 0 ) || ( ylo >= 0 && ymid <= 0 ) ) {
               xhi = xmid;
               yhi = ymid;
            } else {
               xlo = xmid;
               ylo = ymid;
               ffrac = silk_ADD_RSHIFT( ffrac, 128, m );
            }
         }
         if( silk_abs( ylo ) < 65536 ) {
            den = ylo - yhi;
            nom = silk_LSHIFT( ylo, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) + silk_RSHIFT( den, 1 );
            if( den != 0 ) {
               ffrac += silk_DIV32( nom, den );
            }
         } else {
            ffrac += silk_DIV32( ylo, silk_RSHIFT( ylo - yhi, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) );
         }
         NLSF[ root_ix ] = (fl_i16)silk_min_32( silk_LSHIFT( (fl_i32)k, 8 ) + ffrac, silk_int16_MAX );
         silk_assert( NLSF[ root_ix ] >= 0 );
         root_ix++;
         if( root_ix >= d ) {
            break;
         }
         p = PQ[ root_ix & 1 ];
         xlo = fl_LSFCosTab_FIX_Q12[ k - 1 ];
         ylo = silk_LSHIFT( 1 - ( root_ix & 2 ), 12 );
      } else {
         k++;
         xlo = xhi;
         ylo = yhi;
         thr = 0;
         if( k > LSF_COS_TAB_SZ_FIX ) {
            i++;
            if( i > MAX_ITERATIONS_A2NLSF_FIX ) {
               NLSF[ 0 ] = (fl_i16)silk_DIV32_16( 1 << 15, d + 1 );
               for( k = 1; k < d; k++ ) {
                  NLSF[ k ] = (fl_i16)silk_ADD16( NLSF[ k-1 ], NLSF[ 0 ] );
               }
               return;
            }
            silk_bwexpander_32( a_Q16, d, 65536 - silk_LSHIFT( 1, i ) );
            silk_A2NLSF_init( a_Q16, P, Q, dd );
            p = P;
            xlo = fl_LSFCosTab_FIX_Q12[ 0 ];
            ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
            if( ylo < 0 ) {
               NLSF[ 0 ] = 0;
               p = Q;
               ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
               root_ix = 1;
            } else {
               root_ix = 0;
            }
            k = 1;
         }
      }
   }
}

static void silk_NLSF_VQ_weights_laroia(
   fl_i16 *pNLSFW_Q_OUT,
   const fl_i16 *pNLSF_Q15,
   const int D
)
{
   int k;
   fl_i32 tmp1_int, tmp2_int;
   celt_assert( D > 0 );
   celt_assert( ( D & 1 ) == 0 );
   tmp1_int = silk_max_int( pNLSF_Q15[ 0 ], 1 );
   tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
   tmp2_int = silk_max_int( pNLSF_Q15[ 1 ] - pNLSF_Q15[ 0 ], 1 );
   tmp2_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp2_int );
   pNLSFW_Q_OUT[ 0 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
   silk_assert( pNLSFW_Q_OUT[ 0 ] > 0 );
   for( k = 1; k < D - 1; k += 2 ) {
      tmp1_int = silk_max_int( pNLSF_Q15[ k + 1 ] - pNLSF_Q15[ k ], 1 );
      tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
      pNLSFW_Q_OUT[ k ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
      silk_assert( pNLSFW_Q_OUT[ k ] > 0 );
      tmp2_int = silk_max_int( pNLSF_Q15[ k + 2 ] - pNLSF_Q15[ k + 1 ], 1 );
      tmp2_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp2_int );
      pNLSFW_Q_OUT[ k + 1 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
      silk_assert( pNLSFW_Q_OUT[ k + 1 ] > 0 );
   }
   tmp1_int = silk_max_int( ( 1 << 15 ) - pNLSF_Q15[ D - 1 ], 1 );
   tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
   pNLSFW_Q_OUT[ D - 1 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
   silk_assert( pNLSFW_Q_OUT[ D - 1 ] > 0 );
}

static void silk_NLSF_VQ(
   fl_i32 err_Q24[],
   const fl_i16 in_Q15[],
   const fl_u8 pCB_Q8[],
   const fl_i16 pWght_Q9[],
   const int K,
   const int LPC_order
)
{
   int i, m;
   fl_i32 diff_Q15, diffw_Q24, sum_error_Q24, pred_Q24;
   const fl_i16 *w_Q9_ptr;
   const fl_u8 *cb_Q8_ptr;
   celt_assert( ( LPC_order & 1 ) == 0 );
   cb_Q8_ptr = pCB_Q8;
   w_Q9_ptr = pWght_Q9;
   for( i = 0; i < K; i++ ) {
      sum_error_Q24 = 0;
      pred_Q24 = 0;
      for( m = LPC_order-2; m >= 0; m -= 2 ) {
         diff_Q15 = silk_SUB_LSHIFT32( in_Q15[ m + 1 ], (fl_i32)cb_Q8_ptr[ m + 1 ], 7 );
         diffw_Q24 = silk_SMULBB( diff_Q15, w_Q9_ptr[ m + 1 ] );
         sum_error_Q24 = silk_ADD32( sum_error_Q24, silk_abs( silk_SUB_RSHIFT32( diffw_Q24, pred_Q24, 1 ) ) );
         pred_Q24 = diffw_Q24;
         diff_Q15 = silk_SUB_LSHIFT32( in_Q15[ m ], (fl_i32)cb_Q8_ptr[ m ], 7 );
         diffw_Q24 = silk_SMULBB( diff_Q15, w_Q9_ptr[ m ] );
         sum_error_Q24 = silk_ADD32( sum_error_Q24, silk_abs( silk_SUB_RSHIFT32( diffw_Q24, pred_Q24, 1 ) ) );
         pred_Q24 = diffw_Q24;
         silk_assert( sum_error_Q24 >= 0 );
      }
      err_Q24[ i ] = sum_error_Q24;
      cb_Q8_ptr += LPC_order;
      w_Q9_ptr += LPC_order;
   }
}

static fl_i32 silk_NLSF_del_dec_quant(
   fl_i8 indices[],
   const fl_i16 x_Q10[],
   const fl_i16 w_Q5[],
   const fl_u8 pred_coef_Q8[],
   const fl_i16 ec_ix[],
   const fl_u8 ec_rates_Q5[],
   const int quant_step_size_Q16,
   const fl_i16 inv_quant_step_size_Q6,
   const fl_i32 mu_Q20,
   const fl_i16 order
)
{
   int i, j, nStates, ind_tmp, ind_min_max, ind_max_min, in_Q10, res_Q10;
   int pred_Q10, diff_Q10, rate0_Q5, rate1_Q5;
   fl_i16 out0_Q10, out1_Q10;
   fl_i32 RD_tmp_Q25, min_Q25, min_max_Q25, max_min_Q25;
   int ind_sort[ NLSF_QUANT_DEL_DEC_STATES ];
   fl_i8 ind[ NLSF_QUANT_DEL_DEC_STATES ][ MAX_LPC_ORDER ];
   fl_i16 prev_out_Q10[ 2 * NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_Q25[ 2 * NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_min_Q25[ NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_max_Q25[ NLSF_QUANT_DEL_DEC_STATES ];
   const fl_u8 *rates_Q5;
   int out0_Q10_table[2 * NLSF_QUANT_MAX_AMPLITUDE_EXT];
   int out1_Q10_table[2 * NLSF_QUANT_MAX_AMPLITUDE_EXT];
   for (i = -NLSF_QUANT_MAX_AMPLITUDE_EXT; i <= NLSF_QUANT_MAX_AMPLITUDE_EXT-1; i++)
   {
      out0_Q10 = silk_LSHIFT( i, 10 );
      out1_Q10 = silk_ADD16( out0_Q10, 1024 );
      if( i > 0 ) {
         out0_Q10 = silk_SUB16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
         out1_Q10 = silk_SUB16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else if( i == 0 ) {
         out1_Q10 = silk_SUB16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else if( i == -1 ) {
         out0_Q10 = silk_ADD16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else {
         out0_Q10 = silk_ADD16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
         out1_Q10 = silk_ADD16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      }
      out0_Q10_table[ i + NLSF_QUANT_MAX_AMPLITUDE_EXT ] = silk_RSHIFT( silk_SMULBB( out0_Q10, quant_step_size_Q16 ), 16 );
      out1_Q10_table[ i + NLSF_QUANT_MAX_AMPLITUDE_EXT ] = silk_RSHIFT( silk_SMULBB( out1_Q10, quant_step_size_Q16 ), 16 );
   }
   silk_assert( (NLSF_QUANT_DEL_DEC_STATES & (NLSF_QUANT_DEL_DEC_STATES-1)) == 0 );
   nStates = 1;
   RD_Q25[ 0 ] = 0;
   prev_out_Q10[ 0 ] = 0;
   for( i = order - 1; i >= 0; i-- ) {
      rates_Q5 = &ec_rates_Q5[ ec_ix[ i ] ];
      in_Q10 = x_Q10[ i ];
      for( j = 0; j < nStates; j++ ) {
         pred_Q10 = silk_RSHIFT( silk_SMULBB( (fl_i16)pred_coef_Q8[ i ], prev_out_Q10[ j ] ), 8 );
         res_Q10 = silk_SUB16( in_Q10, pred_Q10 );
         ind_tmp = silk_RSHIFT( silk_SMULBB( inv_quant_step_size_Q6, res_Q10 ), 16 );
         ind_tmp = silk_LIMIT( ind_tmp, -NLSF_QUANT_MAX_AMPLITUDE_EXT, NLSF_QUANT_MAX_AMPLITUDE_EXT-1 );
         ind[ j ][ i ] = (fl_i8)ind_tmp;
         out0_Q10 = out0_Q10_table[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE_EXT ];
         out1_Q10 = out1_Q10_table[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE_EXT ];
         out0_Q10 = silk_ADD16( out0_Q10, pred_Q10 );
         out1_Q10 = silk_ADD16( out1_Q10, pred_Q10 );
         prev_out_Q10[ j ] = out0_Q10;
         prev_out_Q10[ j + nStates ] = out1_Q10;
         if( ind_tmp + 1 >= NLSF_QUANT_MAX_AMPLITUDE ) {
            if( ind_tmp + 1 == NLSF_QUANT_MAX_AMPLITUDE ) {
               rate0_Q5 = rates_Q5[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE ];
               rate1_Q5 = 280;
            } else {
               rate0_Q5 = silk_SMLABB( 280 - 43 * NLSF_QUANT_MAX_AMPLITUDE, 43, ind_tmp );
               rate1_Q5 = silk_ADD16( rate0_Q5, 43 );
            }
         } else if( ind_tmp <= -NLSF_QUANT_MAX_AMPLITUDE ) {
            if( ind_tmp == -NLSF_QUANT_MAX_AMPLITUDE ) {
               rate0_Q5 = 280;
               rate1_Q5 = rates_Q5[ ind_tmp + 1 + NLSF_QUANT_MAX_AMPLITUDE ];
            } else {
               rate0_Q5 = silk_SMLABB( 280 - 43 * NLSF_QUANT_MAX_AMPLITUDE, -43, ind_tmp );
               rate1_Q5 = silk_SUB16( rate0_Q5, 43 );
            }
         } else {
            rate0_Q5 = rates_Q5[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE ];
            rate1_Q5 = rates_Q5[ ind_tmp + 1 + NLSF_QUANT_MAX_AMPLITUDE ];
         }
         RD_tmp_Q25 = RD_Q25[ j ];
         diff_Q10 = silk_SUB16( in_Q10, out0_Q10 );
         RD_Q25[ j ] = silk_SMLABB( silk_MLA( RD_tmp_Q25, silk_SMULBB( diff_Q10, diff_Q10 ), w_Q5[ i ] ), mu_Q20, rate0_Q5 );
         diff_Q10 = silk_SUB16( in_Q10, out1_Q10 );
         RD_Q25[ j + nStates ] = silk_SMLABB( silk_MLA( RD_tmp_Q25, silk_SMULBB( diff_Q10, diff_Q10 ), w_Q5[ i ] ), mu_Q20, rate1_Q5 );
      }
      if( nStates <= NLSF_QUANT_DEL_DEC_STATES/2 ) {
         for( j = 0; j < nStates; j++ ) {
            ind[ j + nStates ][ i ] = ind[ j ][ i ] + 1;
         }
         nStates = silk_LSHIFT( nStates, 1 );
         for( j = nStates; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            ind[ j ][ i ] = ind[ j - nStates ][ i ];
         }
      } else {
         for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            if( RD_Q25[ j ] > RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ] ) {
               RD_max_Q25[ j ] = RD_Q25[ j ];
               RD_min_Q25[ j ] = RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ];
               RD_Q25[ j ] = RD_min_Q25[ j ];
               RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ] = RD_max_Q25[ j ];
               out0_Q10 = prev_out_Q10[ j ];
               prev_out_Q10[ j ] = prev_out_Q10[ j + NLSF_QUANT_DEL_DEC_STATES ];
               prev_out_Q10[ j + NLSF_QUANT_DEL_DEC_STATES ] = out0_Q10;
               ind_sort[ j ] = j + NLSF_QUANT_DEL_DEC_STATES;
            } else {
               RD_min_Q25[ j ] = RD_Q25[ j ];
               RD_max_Q25[ j ] = RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ];
               ind_sort[ j ] = j;
            }
         }
         while( 1 ) {
            min_max_Q25 = silk_int32_MAX;
            max_min_Q25 = 0;
            ind_min_max = 0;
            ind_max_min = 0;
            for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
               if( min_max_Q25 > RD_max_Q25[ j ] ) {
                  min_max_Q25 = RD_max_Q25[ j ];
                  ind_min_max = j;
               }
               if( max_min_Q25 < RD_min_Q25[ j ] ) {
                  max_min_Q25 = RD_min_Q25[ j ];
                  ind_max_min = j;
               }
            }
            if( min_max_Q25 >= max_min_Q25 ) {
               break;
            }
            ind_sort[ ind_max_min ] = ind_sort[ ind_min_max ] ^ NLSF_QUANT_DEL_DEC_STATES;
            RD_Q25[ ind_max_min ] = RD_Q25[ ind_min_max + NLSF_QUANT_DEL_DEC_STATES ];
            prev_out_Q10[ ind_max_min ] = prev_out_Q10[ ind_min_max + NLSF_QUANT_DEL_DEC_STATES ];
            RD_min_Q25[ ind_max_min ] = 0;
            RD_max_Q25[ ind_min_max ] = silk_int32_MAX;
            silk_memcpy( ind[ ind_max_min ], ind[ ind_min_max ], MAX_LPC_ORDER * sizeof( fl_i8 ) );
         }
         for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            ind[ j ][ i ] += silk_RSHIFT( ind_sort[ j ], NLSF_QUANT_DEL_DEC_STATES_LOG2 );
         }
      }
   }
   ind_tmp = 0;
   min_Q25 = silk_int32_MAX;
   for( j = 0; j < 2 * NLSF_QUANT_DEL_DEC_STATES; j++ ) {
      if( min_Q25 > RD_Q25[ j ] ) {
         min_Q25 = RD_Q25[ j ];
         ind_tmp = j;
      }
   }
   for( j = 0; j < order; j++ ) {
      indices[ j ] = ind[ ind_tmp & ( NLSF_QUANT_DEL_DEC_STATES - 1 ) ][ j ];
      silk_assert( indices[ j ] >= -NLSF_QUANT_MAX_AMPLITUDE_EXT );
      silk_assert( indices[ j ] <= NLSF_QUANT_MAX_AMPLITUDE_EXT );
   }
   indices[ 0 ] += silk_RSHIFT( ind_tmp, NLSF_QUANT_DEL_DEC_STATES_LOG2 );
   silk_assert( indices[ 0 ] <= NLSF_QUANT_MAX_AMPLITUDE_EXT );
   silk_assert( min_Q25 >= 0 );
   return min_Q25;
}

static fl_i32 silk_NLSF_encode(
       fl_i8 *NLSFIndices,
       fl_i16 *pNLSF_Q15,
   const fl_nlsf_cb *psNLSF_CB,
   const fl_i16 *pW_Q2,
   const int NLSF_mu_Q20,
   const int nSurvivors,
   const int signalType
)
{
   int i, s, ind1, bestIndex, prob_Q8, bits_q7;
   fl_i32 W_tmp_Q9, ret;
   VARDECL( fl_i32, err_Q24 );
   VARDECL( fl_i32, RD_Q25 );
   VARDECL( int, tempIndices1 );
   VARDECL( fl_i8, tempIndices2 );
   fl_i16 res_Q10[ MAX_LPC_ORDER ];
   fl_i16 NLSF_tmp_Q15[ MAX_LPC_ORDER ];
   fl_i16 W_adj_Q5[ MAX_LPC_ORDER ];
   fl_u8 pred_Q8[ MAX_LPC_ORDER ];
   fl_i16 ec_ix[ MAX_LPC_ORDER ];
   const fl_u8 *pCB_element, *iCDF_ptr;
   const fl_i16 *pCB_Wght_Q9;
   SAVE_STACK;
   celt_assert( signalType >= 0 && signalType <= 2 );
   silk_assert( NLSF_mu_Q20 <= 32767 && NLSF_mu_Q20 >= 0 );
   silk_NLSF_stabilize( pNLSF_Q15, psNLSF_CB->deltaMin_Q15, psNLSF_CB->order );
   ALLOC( err_Q24, psNLSF_CB->nVectors, fl_i32 );
   silk_NLSF_VQ( err_Q24, pNLSF_Q15, psNLSF_CB->CB1_NLSF_Q8, psNLSF_CB->CB1_Wght_Q9, psNLSF_CB->nVectors, psNLSF_CB->order );
   ALLOC( tempIndices1, nSurvivors, int );
   silk_insertion_sort_increasing( err_Q24, tempIndices1, psNLSF_CB->nVectors, nSurvivors );
   ALLOC( RD_Q25, nSurvivors, fl_i32 );
   ALLOC( tempIndices2, nSurvivors * MAX_LPC_ORDER, fl_i8 );
   for( s = 0; s < nSurvivors; s++ ) {
      ind1 = tempIndices1[ s ];
      pCB_element = &psNLSF_CB->CB1_NLSF_Q8[ ind1 * psNLSF_CB->order ];
      pCB_Wght_Q9 = &psNLSF_CB->CB1_Wght_Q9[ ind1 * psNLSF_CB->order ];
      for( i = 0; i < psNLSF_CB->order; i++ ) {
         NLSF_tmp_Q15[ i ] = silk_LSHIFT16( (fl_i16)pCB_element[ i ], 7 );
         W_tmp_Q9 = pCB_Wght_Q9[ i ];
         res_Q10[ i ] = (fl_i16)silk_RSHIFT( silk_SMULBB( pNLSF_Q15[ i ] - NLSF_tmp_Q15[ i ], W_tmp_Q9 ), 14 );
         W_adj_Q5[ i ] = silk_DIV32_varQ( (fl_i32)pW_Q2[ i ], silk_SMULBB( W_tmp_Q9, W_tmp_Q9 ), 21 );
      }
      silk_NLSF_unpack( ec_ix, pred_Q8, psNLSF_CB, ind1 );
      RD_Q25[ s ] = silk_NLSF_del_dec_quant( &tempIndices2[ s * MAX_LPC_ORDER ], res_Q10, W_adj_Q5, pred_Q8, ec_ix,
         psNLSF_CB->ec_Rates_Q5, psNLSF_CB->quantStepSize_Q16, psNLSF_CB->invQuantStepSize_Q6, NLSF_mu_Q20, psNLSF_CB->order );
      iCDF_ptr = &psNLSF_CB->CB1_iCDF[ ( signalType >> 1 ) * psNLSF_CB->nVectors ];
      if( ind1 == 0 ) {
         prob_Q8 = 256 - iCDF_ptr[ ind1 ];
      } else {
         prob_Q8 = iCDF_ptr[ ind1 - 1 ] - iCDF_ptr[ ind1 ];
      }
      bits_q7 = ( 8 << 7 ) - silk_lin2log( prob_Q8 );
      RD_Q25[ s ] = silk_SMLABB( RD_Q25[ s ], bits_q7, silk_RSHIFT( NLSF_mu_Q20, 2 ) );
   }
   silk_insertion_sort_increasing( RD_Q25, &bestIndex, nSurvivors, 1 );
   NLSFIndices[ 0 ] = (fl_i8)tempIndices1[ bestIndex ];
   silk_memcpy( &NLSFIndices[ 1 ], &tempIndices2[ bestIndex * MAX_LPC_ORDER ], psNLSF_CB->order * sizeof( fl_i8 ) );
   silk_NLSF_decode( pNLSF_Q15, NLSFIndices, psNLSF_CB );
   ret = RD_Q25[ 0 ];
   RESTORE_STACK;
   return ret;
}

static void silk_interpolate(
   fl_i16 xi[ MAX_LPC_ORDER ],
   const fl_i16 x0[ MAX_LPC_ORDER ],
   const fl_i16 x1[ MAX_LPC_ORDER ],
   const int ifact_Q2,
   const int d
)
{
   int i;
   celt_assert( ifact_Q2 >= 0 );
   celt_assert( ifact_Q2 <= 4 );
   for( i = 0; i < d; i++ ) {
      xi[ i ] = (fl_i16)silk_ADD_RSHIFT( x0[ i ], silk_SMULBB( x1[ i ] - x0[ i ], ifact_Q2 ), 2 );
   }
}

static void silk_process_NLSFs(
   silk_encoder_state *psEncC,
   fl_i16 PredCoef_Q12[ 2 ][ MAX_LPC_ORDER ],
   fl_i16 pNLSF_Q15[ MAX_LPC_ORDER ],
   const fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ]
)
{
   int i, doInterpolate;
   int NLSF_mu_Q20;
   fl_i16 i_sqr_Q15;
   fl_i16 pNLSF0_temp_Q15[ MAX_LPC_ORDER ];
   fl_i16 pNLSFW_QW[ MAX_LPC_ORDER ];
   fl_i16 pNLSFW0_temp_QW[ MAX_LPC_ORDER ];
   silk_assert( psEncC->speech_activity_Q8 >= 0 );
   silk_assert( psEncC->speech_activity_Q8 <= SILK_FIX_CONST( 1.0, 8 ) );
   celt_assert( psEncC->useInterpolatedNLSFs == 1 || psEncC->indices.NLSFInterpCoef_Q2 == ( 1 << 2 ) );
   NLSF_mu_Q20 = silk_SMLAWB( SILK_FIX_CONST( 0.003, 20 ), SILK_FIX_CONST( -0.001, 28 ), psEncC->speech_activity_Q8 );
   if( psEncC->nb_subfr == 2 ) {
      NLSF_mu_Q20 = silk_ADD_RSHIFT( NLSF_mu_Q20, NLSF_mu_Q20, 1 );
   }
   celt_assert( NLSF_mu_Q20 > 0 );
   silk_assert( NLSF_mu_Q20 <= SILK_FIX_CONST( 0.005, 20 ) );
   silk_NLSF_VQ_weights_laroia( pNLSFW_QW, pNLSF_Q15, psEncC->predictLPCOrder );
   doInterpolate = ( psEncC->useInterpolatedNLSFs == 1 ) && ( psEncC->indices.NLSFInterpCoef_Q2 < 4 );
   if( doInterpolate ) {
      silk_interpolate( pNLSF0_temp_Q15, prev_NLSFq_Q15, pNLSF_Q15,
         psEncC->indices.NLSFInterpCoef_Q2, psEncC->predictLPCOrder );
      silk_NLSF_VQ_weights_laroia( pNLSFW0_temp_QW, pNLSF0_temp_Q15, psEncC->predictLPCOrder );
      i_sqr_Q15 = silk_LSHIFT( silk_SMULBB( psEncC->indices.NLSFInterpCoef_Q2, psEncC->indices.NLSFInterpCoef_Q2 ), 11 );
      for( i = 0; i < psEncC->predictLPCOrder; i++ ) {
         pNLSFW_QW[ i ] = silk_ADD16( silk_RSHIFT( pNLSFW_QW[ i ], 1 ), silk_RSHIFT(
             silk_SMULBB( pNLSFW0_temp_QW[ i ], i_sqr_Q15 ), 16) );
         silk_assert( pNLSFW_QW[ i ] >= 1 );
      }
   }
   silk_NLSF_encode( psEncC->indices.NLSFIndices, pNLSF_Q15, psEncC->psNLSF_CB, pNLSFW_QW,
      NLSF_mu_Q20, psEncC->NLSF_MSVQ_Survivors, psEncC->indices.signalType );
   silk_NLSF2A( PredCoef_Q12[ 1 ], pNLSF_Q15, psEncC->predictLPCOrder, psEncC->arch );
   if( doInterpolate ) {
      silk_interpolate( pNLSF0_temp_Q15, prev_NLSFq_Q15, pNLSF_Q15,
         psEncC->indices.NLSFInterpCoef_Q2, psEncC->predictLPCOrder );
      silk_NLSF2A( PredCoef_Q12[ 0 ], pNLSF0_temp_Q15, psEncC->predictLPCOrder, psEncC->arch );
   } else {
      celt_assert( psEncC->predictLPCOrder <= MAX_LPC_ORDER );
      silk_memcpy( PredCoef_Q12[ 0 ], PredCoef_Q12[ 1 ], psEncC->predictLPCOrder * sizeof( fl_i16 ) );
   }
}

static void silk_VQ_WMat_EC_c(
   fl_i8 *ind,
   fl_i32 *res_nrg_Q15,
   fl_i32 *rate_dist_Q8,
   int *gain_Q7,
   const fl_i32 *XX_Q17,
   const fl_i32 *xX_Q17,
   const fl_i8 *cb_Q7,
   const fl_u8 *cb_gain_Q7,
   const fl_u8 *cl_Q5,
   const int subfr_len,
   const fl_i32 max_gain_Q7,
   const int L
)
{
   int k, gain_tmp_Q7;
   const fl_i8 *cb_row_Q7;
   fl_i32 neg_xX_Q24[ 5 ];
   fl_i32 sum1_Q15, sum2_Q24;
   fl_i32 bits_res_Q8, bits_tot_Q8;
   neg_xX_Q24[ 0 ] = -silk_LSHIFT32( xX_Q17[ 0 ], 7 );
   neg_xX_Q24[ 1 ] = -silk_LSHIFT32( xX_Q17[ 1 ], 7 );
   neg_xX_Q24[ 2 ] = -silk_LSHIFT32( xX_Q17[ 2 ], 7 );
   neg_xX_Q24[ 3 ] = -silk_LSHIFT32( xX_Q17[ 3 ], 7 );
   neg_xX_Q24[ 4 ] = -silk_LSHIFT32( xX_Q17[ 4 ], 7 );
   *rate_dist_Q8 = silk_int32_MAX;
   *res_nrg_Q15 = silk_int32_MAX;
   cb_row_Q7 = cb_Q7;
   *ind = 0;
   for( k = 0; k < L; k++ ) {
      fl_i32 penalty;
      gain_tmp_Q7 = cb_gain_Q7[k];
      sum1_Q15 = SILK_FIX_CONST( 1.001, 15 );
      penalty = silk_LSHIFT32( silk_max( silk_SUB32( gain_tmp_Q7, max_gain_Q7 ), 0 ), 11 );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 0 ], XX_Q17[ 1 ], cb_row_Q7[ 1 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 2 ], cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 3 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 4 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 0 ], cb_row_Q7[ 0 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 0 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 1 ], XX_Q17[ 7 ], cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 8 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 9 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 6 ], cb_row_Q7[ 1 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 1 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 2 ], XX_Q17[ 13 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 14 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 12 ], cb_row_Q7[ 2 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 3 ], XX_Q17[ 19 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 18 ], cb_row_Q7[ 3 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_LSHIFT32( neg_xX_Q24[ 4 ], 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 24 ], cb_row_Q7[ 4 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 4 ] );
      if( sum1_Q15 >= 0 ) {
         bits_res_Q8 = silk_SMULBB( subfr_len, silk_lin2log( sum1_Q15 + penalty) - (15 << 7) );
         bits_tot_Q8 = silk_ADD_LSHIFT32( bits_res_Q8, cl_Q5[ k ], 3-1 );
         if( bits_tot_Q8 <= *rate_dist_Q8 ) {
            *rate_dist_Q8 = bits_tot_Q8;
            *res_nrg_Q15 = sum1_Q15 + penalty;
            *ind = (fl_i8)k;
            *gain_Q7 = gain_tmp_Q7;
         }
      }
      cb_row_Q7 += LTP_ORDER;
   }
}

static void silk_quant_LTP_gains(
   fl_i16 B_Q14[ MAX_NB_SUBFR * LTP_ORDER ],
   fl_i8 cbk_index[ MAX_NB_SUBFR ],
   fl_i8 *periodicity_index,
   fl_i32 *sum_log_gain_Q7,
   int *pred_gain_dB_Q7,
   const fl_i32 XX_Q17[ MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER ],
   const fl_i32 xX_Q17[ MAX_NB_SUBFR*LTP_ORDER ],
   const int subfr_len,
   const int nb_subfr,
   int arch
)
{
   int j, k, cbk_size;
   fl_i8 temp_idx[ MAX_NB_SUBFR ];
   const fl_u8 *cl_ptr_Q5;
   const fl_i8 *cbk_ptr_Q7;
   const fl_u8 *cbk_gain_ptr_Q7;
   const fl_i32 *XX_Q17_ptr, *xX_Q17_ptr;
   fl_i32 res_nrg_Q15_subfr, res_nrg_Q15, rate_dist_Q7_subfr, rate_dist_Q7, min_rate_dist_Q7;
   fl_i32 sum_log_gain_tmp_Q7, best_sum_log_gain_Q7, max_gain_Q7;
   int gain_Q7;
   min_rate_dist_Q7 = silk_int32_MAX;
   best_sum_log_gain_Q7 = 0;
   for( k = 0; k < 3; k++ ) {
      fl_i32 gain_safety = SILK_FIX_CONST( 0.4, 7 );
      cl_ptr_Q5 = fl_LTP_gain_BITS_Q5_ptrs[ k ];
      cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[ k ];
      cbk_gain_ptr_Q7 = fl_LTP_vq_gain_ptrs_Q7[ k ];
      cbk_size = fl_LTP_vq_sizes[ k ];
      XX_Q17_ptr = XX_Q17;
      xX_Q17_ptr = xX_Q17;
      res_nrg_Q15 = 0;
      rate_dist_Q7 = 0;
      sum_log_gain_tmp_Q7 = *sum_log_gain_Q7;
      for( j = 0; j < nb_subfr; j++ ) {
         max_gain_Q7 = silk_log2lin( ( SILK_FIX_CONST( MAX_SUM_LOG_GAIN_DB / 6.0, 7 ) - sum_log_gain_tmp_Q7 )
                              + SILK_FIX_CONST( 7, 7 ) ) - gain_safety;
         silk_VQ_WMat_EC(
            &temp_idx[ j ],
            &res_nrg_Q15_subfr,
            &rate_dist_Q7_subfr,
            &gain_Q7,
            XX_Q17_ptr,
            xX_Q17_ptr,
            cbk_ptr_Q7,
            cbk_gain_ptr_Q7,
            cl_ptr_Q5,
            subfr_len,
            max_gain_Q7,
            cbk_size,
            arch
         );
         res_nrg_Q15 = silk_ADD_POS_SAT32( res_nrg_Q15, res_nrg_Q15_subfr );
         rate_dist_Q7 = silk_ADD_POS_SAT32( rate_dist_Q7, rate_dist_Q7_subfr );
         sum_log_gain_tmp_Q7 = silk_max(0, sum_log_gain_tmp_Q7
                        + silk_lin2log( gain_safety + gain_Q7 ) - SILK_FIX_CONST( 7, 7 ));
         XX_Q17_ptr += LTP_ORDER * LTP_ORDER;
         xX_Q17_ptr += LTP_ORDER;
      }
      if( rate_dist_Q7 <= min_rate_dist_Q7 ) {
         min_rate_dist_Q7 = rate_dist_Q7;
         *periodicity_index = (fl_i8)k;
         silk_memcpy( cbk_index, temp_idx, nb_subfr * sizeof( fl_i8 ) );
         best_sum_log_gain_Q7 = sum_log_gain_tmp_Q7;
      }
   }
   cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[ *periodicity_index ];
   for( j = 0; j < nb_subfr; j++ ) {
      for( k = 0; k < LTP_ORDER; k++ ) {
         B_Q14[ j * LTP_ORDER + k ] = silk_LSHIFT( cbk_ptr_Q7[ cbk_index[ j ] * LTP_ORDER + k ], 7 );
      }
   }
   if( nb_subfr == 2 ) {
      res_nrg_Q15 = silk_RSHIFT32( res_nrg_Q15, 1 );
   } else {
      res_nrg_Q15 = silk_RSHIFT32( res_nrg_Q15, 2 );
   }
   *sum_log_gain_Q7 = best_sum_log_gain_Q7;
   *pred_gain_dB_Q7 = (int)silk_SMULBB( -3, silk_lin2log( res_nrg_Q15 ) - ( 15 << 7 ) );
}

static void silk_encode_indices(
   silk_encoder_state *psEncC,
   ec_ctx *psRangeEnc,
   int FrameIndex,
   int encode_LBRR,
   int condCoding
)
{
   int i, k, typeOffset;
   int encode_absolute_lagIndex, delta_lagIndex;
   fl_i16 ec_ix[ MAX_LPC_ORDER ];
   fl_u8 pred_Q8[ MAX_LPC_ORDER ];
   const SideInfoIndices *psIndices;
   if( encode_LBRR ) {
      psIndices = &psEncC->indices_LBRR[ FrameIndex ];
   } else {
      psIndices = &psEncC->indices;
   }
   typeOffset = 2 * psIndices->signalType + psIndices->quantOffsetType;
   celt_assert( typeOffset >= 0 && typeOffset < 6 );
   celt_assert( encode_LBRR == 0 || typeOffset >= 2 );
   if( encode_LBRR || typeOffset >= 2 ) {
      ec_enc_icdf( psRangeEnc, typeOffset - 2, fl_type_offset_VAD_iCDF, 8 );
   } else {
      ec_enc_icdf( psRangeEnc, typeOffset, fl_type_offset_no_VAD_iCDF, 8 );
   }
   if( condCoding == CODE_CONDITIONALLY ) {
      silk_assert( psIndices->GainsIndices[ 0 ] >= 0 && psIndices->GainsIndices[ 0 ] < MAX_DELTA_GAIN_QUANT - MIN_DELTA_GAIN_QUANT + 1 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ 0 ], fl_delta_gain_iCDF, 8 );
   } else {
      silk_assert( psIndices->GainsIndices[ 0 ] >= 0 && psIndices->GainsIndices[ 0 ] < N_LEVELS_QGAIN );
      ec_enc_icdf( psRangeEnc, silk_RSHIFT( psIndices->GainsIndices[ 0 ], 3 ), fl_gain_iCDF[ psIndices->signalType ], 8 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ 0 ] & 7, fl_uniform8_iCDF, 8 );
   }
   for( i = 1; i < psEncC->nb_subfr; i++ ) {
      silk_assert( psIndices->GainsIndices[ i ] >= 0 && psIndices->GainsIndices[ i ] < MAX_DELTA_GAIN_QUANT - MIN_DELTA_GAIN_QUANT + 1 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ i ], fl_delta_gain_iCDF, 8 );
   }
   ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ 0 ], &psEncC->psNLSF_CB->CB1_iCDF[ ( psIndices->signalType >> 1 ) * psEncC->psNLSF_CB->nVectors ], 8 );
   silk_NLSF_unpack( ec_ix, pred_Q8, psEncC->psNLSF_CB, psIndices->NLSFIndices[ 0 ] );
   celt_assert( psEncC->psNLSF_CB->order == psEncC->predictLPCOrder );
   for( i = 0; i < psEncC->psNLSF_CB->order; i++ ) {
      if( psIndices->NLSFIndices[ i+1 ] >= NLSF_QUANT_MAX_AMPLITUDE ) {
         ec_enc_icdf( psRangeEnc, 2 * NLSF_QUANT_MAX_AMPLITUDE, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
         ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ i+1 ] - NLSF_QUANT_MAX_AMPLITUDE, fl_NLSF_EXT_iCDF, 8 );
      } else if( psIndices->NLSFIndices[ i+1 ] <= -NLSF_QUANT_MAX_AMPLITUDE ) {
         ec_enc_icdf( psRangeEnc, 0, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
         ec_enc_icdf( psRangeEnc, -psIndices->NLSFIndices[ i+1 ] - NLSF_QUANT_MAX_AMPLITUDE, fl_NLSF_EXT_iCDF, 8 );
      } else {
         ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ i+1 ] + NLSF_QUANT_MAX_AMPLITUDE, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
      }
   }
   if( psEncC->nb_subfr == MAX_NB_SUBFR ) {
      silk_assert( psIndices->NLSFInterpCoef_Q2 >= 0 && psIndices->NLSFInterpCoef_Q2 < 5 );
      ec_enc_icdf( psRangeEnc, psIndices->NLSFInterpCoef_Q2, fl_NLSF_interpolation_factor_iCDF, 8 );
   }
   if( psIndices->signalType == TYPE_VOICED )
   {
      encode_absolute_lagIndex = 1;
      if( condCoding == CODE_CONDITIONALLY && psEncC->ec_prevSignalType == TYPE_VOICED ) {
         delta_lagIndex = psIndices->lagIndex - psEncC->ec_prevLagIndex;
         if( delta_lagIndex < -8 || delta_lagIndex > 11 ) {
            delta_lagIndex = 0;
         } else {
            delta_lagIndex = delta_lagIndex + 9;
            encode_absolute_lagIndex = 0;
         }
         silk_assert( delta_lagIndex >= 0 && delta_lagIndex < 21 );
         ec_enc_icdf( psRangeEnc, delta_lagIndex, fl_pitch_delta_iCDF, 8 );
      }
      if( encode_absolute_lagIndex ) {
         fl_i32 pitch_high_bits, pitch_low_bits;
         pitch_high_bits = silk_DIV32_16( psIndices->lagIndex, silk_RSHIFT( psEncC->fs_kHz, 1 ) );
         pitch_low_bits = psIndices->lagIndex - silk_SMULBB( pitch_high_bits, silk_RSHIFT( psEncC->fs_kHz, 1 ) );
         silk_assert( pitch_low_bits < psEncC->fs_kHz / 2 );
         silk_assert( pitch_high_bits < 32 );
         ec_enc_icdf( psRangeEnc, pitch_high_bits, fl_pitch_lag_iCDF, 8 );
         ec_enc_icdf( psRangeEnc, pitch_low_bits, psEncC->pitch_lag_low_bits_iCDF, 8 );
      }
      psEncC->ec_prevLagIndex = psIndices->lagIndex;
      silk_assert( psIndices->contourIndex >= 0 );
      silk_assert( ( psIndices->contourIndex < 34 && psEncC->fs_kHz > 8 && psEncC->nb_subfr == 4 ) ||
               ( psIndices->contourIndex < 11 && psEncC->fs_kHz == 8 && psEncC->nb_subfr == 4 ) ||
               ( psIndices->contourIndex < 12 && psEncC->fs_kHz > 8 && psEncC->nb_subfr == 2 ) ||
               ( psIndices->contourIndex < 3 && psEncC->fs_kHz == 8 && psEncC->nb_subfr == 2 ) );
      ec_enc_icdf( psRangeEnc, psIndices->contourIndex, psEncC->pitch_contour_iCDF, 8 );
      silk_assert( psIndices->PERIndex >= 0 && psIndices->PERIndex < 3 );
      ec_enc_icdf( psRangeEnc, psIndices->PERIndex, fl_LTP_per_index_iCDF, 8 );
      for( k = 0; k < psEncC->nb_subfr; k++ ) {
         silk_assert( psIndices->LTPIndex[ k ] >= 0 && psIndices->LTPIndex[ k ] < ( 8 << psIndices->PERIndex ) );
         ec_enc_icdf( psRangeEnc, psIndices->LTPIndex[ k ], fl_LTP_gain_iCDF_ptrs[ psIndices->PERIndex ], 8 );
      }
      if( condCoding == CODE_INDEPENDENTLY ) {
         silk_assert( psIndices->LTP_scaleIndex >= 0 && psIndices->LTP_scaleIndex < 3 );
         ec_enc_icdf( psRangeEnc, psIndices->LTP_scaleIndex, fl_LTPscale_iCDF, 8 );
      }
      silk_assert( !condCoding || psIndices->LTP_scaleIndex == 0 );
   }
   psEncC->ec_prevSignalType = psIndices->signalType;
   silk_assert( psIndices->Seed >= 0 && psIndices->Seed < 4 );
   ec_enc_icdf( psRangeEnc, psIndices->Seed, fl_uniform4_iCDF, 8 );
}

static inline int combine_and_check(
   int *pulses_comb,
   const int *pulses_in,
   int max_pulses,
   int len
)
{
   int k, sum;
   for( k = 0; k < len; k++ ) {
      sum = pulses_in[ 2 * k ] + pulses_in[ 2 * k + 1 ];
      if( sum > max_pulses ) {
         return 1;
      }
      pulses_comb[ k ] = sum;
   }
   return 0;
}

static void silk_encode_pulses(
   ec_ctx *psRangeEnc,
   const int signalType,
   const int quantOffsetType,
   fl_i8 pulses[],
   const int frame_length
)
{
   int i, k, j, iter, bit, nLS, scale_down, RateLevelIndex = 0;
   fl_i32 abs_q, minSumBits_Q5, sumBits_Q5;
   VARDECL( int, abs_pulses );
   VARDECL( int, sum_pulses );
   VARDECL( int, nRshifts );
   int pulses_comb[ 8 ];
   int *abs_pulses_ptr;
   const fl_i8 *pulses_ptr;
   const fl_u8 *cdf_ptr;
   const fl_u8 *nBits_ptr;
   SAVE_STACK;
   silk_memset( pulses_comb, 0, 8 * sizeof( int ) );
   silk_assert( 1 << LOG2_SHELL_CODEC_FRAME_LENGTH == SHELL_CODEC_FRAME_LENGTH );
   iter = silk_RSHIFT( frame_length, LOG2_SHELL_CODEC_FRAME_LENGTH );
   if( iter * SHELL_CODEC_FRAME_LENGTH < frame_length ) {
      celt_assert( frame_length == 12 * 10 );
      iter++;
      silk_memset( &pulses[ frame_length ], 0, SHELL_CODEC_FRAME_LENGTH * sizeof(fl_i8));
   }
   ALLOC( abs_pulses, iter * SHELL_CODEC_FRAME_LENGTH, int );
   silk_assert( !( SHELL_CODEC_FRAME_LENGTH & 3 ) );
   for( i = 0; i < iter * SHELL_CODEC_FRAME_LENGTH; i+=4 ) {
      abs_pulses[i+0] = ( int )silk_abs( pulses[ i + 0 ] );
      abs_pulses[i+1] = ( int )silk_abs( pulses[ i + 1 ] );
      abs_pulses[i+2] = ( int )silk_abs( pulses[ i + 2 ] );
      abs_pulses[i+3] = ( int )silk_abs( pulses[ i + 3 ] );
   }
   ALLOC( sum_pulses, iter, int );
   ALLOC( nRshifts, iter, int );
   abs_pulses_ptr = abs_pulses;
   for( i = 0; i < iter; i++ ) {
      nRshifts[ i ] = 0;
      while( 1 ) {
         scale_down = combine_and_check( pulses_comb, abs_pulses_ptr, fl_max_pulses_table[ 0 ], 8 );
         scale_down += combine_and_check( pulses_comb, pulses_comb, fl_max_pulses_table[ 1 ], 4 );
         scale_down += combine_and_check( pulses_comb, pulses_comb, fl_max_pulses_table[ 2 ], 2 );
         scale_down += combine_and_check( &sum_pulses[ i ], pulses_comb, fl_max_pulses_table[ 3 ], 1 );
         if( scale_down ) {
            nRshifts[ i ]++;
            for( k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++ ) {
               abs_pulses_ptr[ k ] = silk_RSHIFT( abs_pulses_ptr[ k ], 1 );
            }
         } else {
            break;
         }
      }
      abs_pulses_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
   minSumBits_Q5 = silk_int32_MAX;
   for( k = 0; k < N_RATE_LEVELS - 1; k++ ) {
      nBits_ptr = fl_pulses_per_block_BITS_Q5[ k ];
      sumBits_Q5 = fl_rate_levels_BITS_Q5[ signalType >> 1 ][ k ];
      for( i = 0; i < iter; i++ ) {
         if( nRshifts[ i ] > 0 ) {
            sumBits_Q5 += nBits_ptr[ SILK_MAX_PULSES + 1 ];
         } else {
            sumBits_Q5 += nBits_ptr[ sum_pulses[ i ] ];
         }
      }
      if( sumBits_Q5 < minSumBits_Q5 ) {
         minSumBits_Q5 = sumBits_Q5;
         RateLevelIndex = k;
      }
   }
   ec_enc_icdf( psRangeEnc, RateLevelIndex, fl_rate_levels_iCDF[ signalType >> 1 ], 8 );
   cdf_ptr = fl_pulses_per_block_iCDF[ RateLevelIndex ];
   for( i = 0; i < iter; i++ ) {
      if( nRshifts[ i ] == 0 ) {
         ec_enc_icdf( psRangeEnc, sum_pulses[ i ], cdf_ptr, 8 );
      } else {
         ec_enc_icdf( psRangeEnc, SILK_MAX_PULSES + 1, cdf_ptr, 8 );
         for( k = 0; k < nRshifts[ i ] - 1; k++ ) {
            ec_enc_icdf( psRangeEnc, SILK_MAX_PULSES + 1, fl_pulses_per_block_iCDF[ N_RATE_LEVELS - 1 ], 8 );
         }
         ec_enc_icdf( psRangeEnc, sum_pulses[ i ], fl_pulses_per_block_iCDF[ N_RATE_LEVELS - 1 ], 8 );
      }
   }
   for( i = 0; i < iter; i++ ) {
      if( sum_pulses[ i ] > 0 ) {
         silk_shell_encoder( psRangeEnc, &abs_pulses[ i * SHELL_CODEC_FRAME_LENGTH ] );
      }
   }
   for( i = 0; i < iter; i++ ) {
      if( nRshifts[ i ] > 0 ) {
         pulses_ptr = &pulses[ i * SHELL_CODEC_FRAME_LENGTH ];
         nLS = nRshifts[ i ] - 1;
         for( k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++ ) {
            abs_q = (fl_i8)silk_abs( pulses_ptr[ k ] );
            for( j = nLS; j > 0; j-- ) {
               bit = silk_RSHIFT( abs_q, j ) & 1;
               ec_enc_icdf( psRangeEnc, bit, fl_lsb_iCDF, 8 );
            }
            bit = abs_q & 1;
            ec_enc_icdf( psRangeEnc, bit, fl_lsb_iCDF, 8 );
         }
      }
   }
   silk_encode_signs( psRangeEnc, pulses, frame_length, signalType, quantOffsetType, sum_pulses );
   RESTORE_STACK;
}

static inline fl_i32 silk_noise_shape_quantizer_short_prediction_c(const fl_i32 *buf32, const fl_i16 *coef16, int order)
{
   fl_i32 out;
   silk_assert( order == 10 || order == 16 );
   out = silk_RSHIFT( order, 1 );
   out = silk_SMLAWB( out, buf32[ 0 ], coef16[ 0 ] );
   out = silk_SMLAWB( out, buf32[ -1 ], coef16[ 1 ] );
   out = silk_SMLAWB( out, buf32[ -2 ], coef16[ 2 ] );
   out = silk_SMLAWB( out, buf32[ -3 ], coef16[ 3 ] );
   out = silk_SMLAWB( out, buf32[ -4 ], coef16[ 4 ] );
   out = silk_SMLAWB( out, buf32[ -5 ], coef16[ 5 ] );
   out = silk_SMLAWB( out, buf32[ -6 ], coef16[ 6 ] );
   out = silk_SMLAWB( out, buf32[ -7 ], coef16[ 7 ] );
   out = silk_SMLAWB( out, buf32[ -8 ], coef16[ 8 ] );
   out = silk_SMLAWB( out, buf32[ -9 ], coef16[ 9 ] );
   if( order == 16 )
   {
      out = silk_SMLAWB( out, buf32[ -10 ], coef16[ 10 ] );
      out = silk_SMLAWB( out, buf32[ -11 ], coef16[ 11 ] );
      out = silk_SMLAWB( out, buf32[ -12 ], coef16[ 12 ] );
      out = silk_SMLAWB( out, buf32[ -13 ], coef16[ 13 ] );
      out = silk_SMLAWB( out, buf32[ -14 ], coef16[ 14 ] );
      out = silk_SMLAWB( out, buf32[ -15 ], coef16[ 15 ] );
   }
   return out;
}

static inline fl_i32 silk_NSQ_noise_shape_feedback_loop_c(const fl_i32 *data0, fl_i32 *data1, const fl_i16 *coef, int order)
{
   fl_i32 out;
   fl_i32 tmp1, tmp2;
   int j;
   tmp2 = data0[0];
   tmp1 = data1[0];
   data1[0] = tmp2;
   out = silk_RSHIFT(order, 1);
   out = silk_SMLAWB(out, tmp2, coef[0]);
   for (j = 2; j < order; j += 2) {
      tmp2 = data1[j - 1];
      data1[j - 1] = tmp1;
      out = silk_SMLAWB(out, tmp1, coef[j - 1]);
      tmp1 = data1[j + 0];
      data1[j + 0] = tmp2;
      out = silk_SMLAWB(out, tmp2, coef[j]);
   }
   data1[order - 1] = tmp1;
   out = silk_SMLAWB(out, tmp1, coef[order - 1]);
   out = silk_LSHIFT32( out, 1 );
   return out;
}

static void silk_NSQ_c
(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
)
{
   int k, lag, start_idx, LSF_interpolation_flag;
   const fl_i16 *A_Q12, *B_Q14, *AR_shp_Q13;
   fl_i16 *pxq;
   VARDECL( fl_i32, sLTP_Q15 );
   VARDECL( fl_i16, sLTP );
   fl_i32 HarmShapeFIRPacked_Q14;
   int offset_Q10;
   VARDECL( fl_i32, x_sc_Q10 );
   SAVE_STACK;
   NSQ->rand_seed = psIndices->Seed;
   lag = NSQ->lagPrev;
   silk_assert( NSQ->prev_gain_Q16 != 0 );
   offset_Q10 = fl_Quantization_Offsets_Q10[ psIndices->signalType >> 1 ][ psIndices->quantOffsetType ];
   if( psIndices->NLSFInterpCoef_Q2 == 4 ) {
      LSF_interpolation_flag = 0;
   } else {
      LSF_interpolation_flag = 1;
   }
   ALLOC( sLTP_Q15, psEncC->ltp_mem_length + psEncC->frame_length, fl_i32 );
   ALLOC( sLTP, psEncC->ltp_mem_length + psEncC->frame_length, fl_i16 );
   ALLOC( x_sc_Q10, psEncC->subfr_length, fl_i32 );
   NSQ->sLTP_shp_buf_idx = psEncC->ltp_mem_length;
   NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
   pxq = &NSQ->xq[ psEncC->ltp_mem_length ];
   for( k = 0; k < psEncC->nb_subfr; k++ ) {
      A_Q12 = &PredCoef_Q12[ (( k >> 1 ) | ( 1 - LSF_interpolation_flag )) * MAX_LPC_ORDER ];
      B_Q14 = &LTPCoef_Q14[ k * LTP_ORDER ];
      AR_shp_Q13 = &AR_Q13[ k * MAX_SHAPE_LPC_ORDER ];
      silk_assert( HarmShapeGain_Q14[ k ] >= 0 );
      HarmShapeFIRPacked_Q14 = silk_RSHIFT( HarmShapeGain_Q14[ k ], 2 );
      HarmShapeFIRPacked_Q14 |= silk_LSHIFT( (fl_i32)silk_RSHIFT( HarmShapeGain_Q14[ k ], 1 ), 16 );
      NSQ->rewhite_flag = 0;
      if( psIndices->signalType == TYPE_VOICED ) {
         lag = pitchL[ k ];
         if( ( k & ( 3 - silk_LSHIFT( LSF_interpolation_flag, 1 ) ) ) == 0 ) {
            start_idx = psEncC->ltp_mem_length - lag - psEncC->predictLPCOrder - LTP_ORDER / 2;
            celt_assert( start_idx > 0 );
            silk_LPC_analysis_filter( &sLTP[ start_idx ], &NSQ->xq[ start_idx + k * psEncC->subfr_length ],
               A_Q12, psEncC->ltp_mem_length - start_idx, psEncC->predictLPCOrder, psEncC->arch );
            NSQ->rewhite_flag = 1;
            NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
         }
      }
      silk_nsq_scale_states( psEncC, NSQ, x16, x_sc_Q10, sLTP, sLTP_Q15, k, LTP_scale_Q14, Gains_Q16, pitchL, psIndices->signalType );
      silk_noise_shape_quantizer( NSQ, psIndices->signalType, x_sc_Q10, pulses, pxq, sLTP_Q15, A_Q12, B_Q14,
         AR_shp_Q13, lag, HarmShapeFIRPacked_Q14, Tilt_Q14[ k ], LF_shp_Q14[ k ], Gains_Q16[ k ], Lambda_Q10,
         offset_Q10, psEncC->subfr_length, psEncC->shapingLPCOrder, psEncC->predictLPCOrder, psEncC->arch );
      x16 += psEncC->subfr_length;
      pulses += psEncC->subfr_length;
      pxq += psEncC->subfr_length;
   }
   NSQ->lagPrev = pitchL[ psEncC->nb_subfr - 1 ];
   silk_memmove( NSQ->xq, &NSQ->xq[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i16 ) );
   silk_memmove( NSQ->sLTP_shp_Q14, &NSQ->sLTP_shp_Q14[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static void silk_noise_shape_quantizer(
   silk_nsq_state *NSQ,
   int signalType,
   const fl_i32 x_sc_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int shapingLPCOrder,
   int predictLPCOrder,
   int arch
)
{
   int i;
   fl_i32 LTP_pred_Q13, LPC_pred_Q10, n_AR_Q12, n_LTP_Q13;
   fl_i32 n_LF_Q12, r_Q10, rr_Q10, q1_Q0, q1_Q10, q2_Q10, rd1_Q20, rd2_Q20;
   fl_i32 exc_Q14, LPC_exc_Q14, xq_Q14, Gain_Q10;
   fl_i32 tmp1, tmp2, sLF_AR_shp_Q14;
   fl_i32 *psLPC_Q14, *shp_lag_ptr, *pred_lag_ptr;
   fl_i32 a_Q12_arch[MAX_LPC_ORDER];
   shp_lag_ptr = &NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - lag + HARM_SHAPE_FIR_TAPS / 2 ];
   pred_lag_ptr = &sLTP_Q15[ NSQ->sLTP_buf_idx - lag + LTP_ORDER / 2 ];
   Gain_Q10 = silk_RSHIFT( Gain_Q16, 6 );
   psLPC_Q14 = &NSQ->sLPC_Q14[ NSQ_LPC_BUF_LENGTH - 1 ];
   silk_short_prediction_create_arch_coef(a_Q12_arch, a_Q12, predictLPCOrder);
   for( i = 0; i < length; i++ ) {
      NSQ->rand_seed = silk_RAND( NSQ->rand_seed );
      LPC_pred_Q10 = silk_noise_shape_quantizer_short_prediction(psLPC_Q14, a_Q12, a_Q12_arch, predictLPCOrder, arch);
      if( signalType == TYPE_VOICED ) {
         LTP_pred_Q13 = 2;
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ 0 ], b_Q14[ 0 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -1 ], b_Q14[ 1 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -2 ], b_Q14[ 2 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -3 ], b_Q14[ 3 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -4 ], b_Q14[ 4 ] );
         pred_lag_ptr++;
      } else {
         LTP_pred_Q13 = 0;
      }
      celt_assert( ( shapingLPCOrder & 1 ) == 0 );
      n_AR_Q12 = silk_NSQ_noise_shape_feedback_loop(&NSQ->sDiff_shp_Q14, NSQ->sAR2_Q14, AR_shp_Q13, shapingLPCOrder, arch);
      n_AR_Q12 = silk_SMLAWB( n_AR_Q12, NSQ->sLF_AR_shp_Q14, Tilt_Q14 );
      n_LF_Q12 = silk_SMULWB( NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - 1 ], LF_shp_Q14 );
      n_LF_Q12 = silk_SMLAWT( n_LF_Q12, NSQ->sLF_AR_shp_Q14, LF_shp_Q14 );
      celt_assert( lag > 0 || signalType != TYPE_VOICED );
      tmp1 = silk_SUB32_ovflw( silk_LSHIFT32( LPC_pred_Q10, 2 ), n_AR_Q12 );
      tmp1 = silk_SUB32_ovflw( tmp1, n_LF_Q12 );
      if( lag > 0 ) {
         n_LTP_Q13 = silk_SMULWB( silk_ADD_SAT32( shp_lag_ptr[ 0 ], shp_lag_ptr[ -2 ] ), HarmShapeFIRPacked_Q14 );
         n_LTP_Q13 = silk_SMLAWT( n_LTP_Q13, shp_lag_ptr[ -1 ], HarmShapeFIRPacked_Q14 );
         n_LTP_Q13 = silk_LSHIFT( n_LTP_Q13, 1 );
         shp_lag_ptr++;
         tmp2 = silk_SUB32( LTP_pred_Q13, n_LTP_Q13 );
         tmp1 = silk_ADD32_ovflw( tmp2, silk_LSHIFT32( tmp1, 1 ) );
         tmp1 = silk_RSHIFT_ROUND( tmp1, 3 );
      } else {
         tmp1 = silk_RSHIFT_ROUND( tmp1, 2 );
      }
      r_Q10 = silk_SUB32( x_sc_Q10[ i ], tmp1 );
      if( NSQ->rand_seed < 0 ) {
         r_Q10 = -r_Q10;
      }
      r_Q10 = silk_LIMIT_32( r_Q10, -(31 << 10), 30 << 10 );
      q1_Q10 = silk_SUB32( r_Q10, offset_Q10 );
      q1_Q0 = silk_RSHIFT( q1_Q10, 10 );
      if (Lambda_Q10 > 2048) {
         int rdo_offset = Lambda_Q10/2 - 512;
         if (q1_Q10 > rdo_offset) {
            q1_Q0 = silk_RSHIFT( q1_Q10 - rdo_offset, 10 );
         } else if (q1_Q10 < -rdo_offset) {
            q1_Q0 = silk_RSHIFT( q1_Q10 + rdo_offset, 10 );
         } else if (q1_Q10 < 0) {
            q1_Q0 = -1;
         } else {
            q1_Q0 = 0;
         }
      }
      if( q1_Q0 > 0 ) {
         q1_Q10 = silk_SUB32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
         q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
         q2_Q10 = silk_ADD32( q1_Q10, 1024 );
         rd1_Q20 = silk_SMULBB( q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else if( q1_Q0 == 0 ) {
         q1_Q10 = offset_Q10;
         q2_Q10 = silk_ADD32( q1_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
         rd1_Q20 = silk_SMULBB( q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else if( q1_Q0 == -1 ) {
         q2_Q10 = offset_Q10;
         q1_Q10 = silk_SUB32( q2_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
         rd1_Q20 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else {
         q1_Q10 = silk_ADD32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
         q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
         q2_Q10 = silk_ADD32( q1_Q10, 1024 );
         rd1_Q20 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( -q2_Q10, Lambda_Q10 );
      }
      rr_Q10 = silk_SUB32( r_Q10, q1_Q10 );
      rd1_Q20 = silk_SMLABB( rd1_Q20, rr_Q10, rr_Q10 );
      rr_Q10 = silk_SUB32( r_Q10, q2_Q10 );
      rd2_Q20 = silk_SMLABB( rd2_Q20, rr_Q10, rr_Q10 );
      if( rd2_Q20 < rd1_Q20 ) {
         q1_Q10 = q2_Q10;
      }
      pulses[ i ] = (fl_i8)silk_RSHIFT_ROUND( q1_Q10, 10 );
      exc_Q14 = silk_LSHIFT( q1_Q10, 4 );
      if ( NSQ->rand_seed < 0 ) {
        exc_Q14 = -exc_Q14;
      }
      LPC_exc_Q14 = silk_ADD_LSHIFT32( exc_Q14, LTP_pred_Q13, 1 );
      xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, silk_LSHIFT32( LPC_pred_Q10, 4 ) );
      xq[ i ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_SMULWW( xq_Q14, Gain_Q10 ), 8 ) );
      psLPC_Q14++;
      *psLPC_Q14 = xq_Q14;
      NSQ->sDiff_shp_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_sc_Q10[ i ], 4 ) );
      sLF_AR_shp_Q14 = silk_SUB32_ovflw( NSQ->sDiff_shp_Q14, silk_LSHIFT32( n_AR_Q12, 2 ) );
      NSQ->sLF_AR_shp_Q14 = sLF_AR_shp_Q14;
      NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx ] = silk_SUB32_ovflw(sLF_AR_shp_Q14, silk_LSHIFT32(n_LF_Q12, 2));
      sLTP_Q15[ NSQ->sLTP_buf_idx ] = silk_LSHIFT( LPC_exc_Q14, 1 );
      NSQ->sLTP_shp_buf_idx++;
      NSQ->sLTP_buf_idx++;
      NSQ->rand_seed = silk_ADD32_ovflw( NSQ->rand_seed, pulses[ i ] );
   }
   silk_memcpy( NSQ->sLPC_Q14, &NSQ->sLPC_Q14[ length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
}

static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
)
{
   int i, lag;
   fl_i32 gain_adj_Q16, inv_gain_Q31, inv_gain_Q26;
   lag = pitchL[ subfr ];
   inv_gain_Q31 = silk_INVERSE32_varQ( silk_max( Gains_Q16[ subfr ], 1 ), 47 );
   silk_assert( inv_gain_Q31 != 0 );
   inv_gain_Q26 = silk_RSHIFT_ROUND( inv_gain_Q31, 5 );
   for( i = 0; i < psEncC->subfr_length; i++ ) {
      x_sc_Q10[ i ] = silk_SMULWW( x16[ i ], inv_gain_Q26 );
   }
   if( NSQ->rewhite_flag ) {
      if( subfr == 0 ) {
         inv_gain_Q31 = silk_LSHIFT( silk_SMULWB( inv_gain_Q31, LTP_scale_Q14 ), 2 );
      }
      for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
         silk_assert( i < MAX_FRAME_LENGTH );
         sLTP_Q15[ i ] = silk_SMULWB( inv_gain_Q31, sLTP[ i ] );
      }
   }
   if( Gains_Q16[ subfr ] != NSQ->prev_gain_Q16 ) {
      gain_adj_Q16 = silk_DIV32_varQ( NSQ->prev_gain_Q16, Gains_Q16[ subfr ], 16 );
      for( i = NSQ->sLTP_shp_buf_idx - psEncC->ltp_mem_length; i < NSQ->sLTP_shp_buf_idx; i++ ) {
         NSQ->sLTP_shp_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLTP_shp_Q14[ i ] );
      }
      if( signal_type == TYPE_VOICED && NSQ->rewhite_flag == 0 ) {
         for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
            sLTP_Q15[ i ] = silk_SMULWW( gain_adj_Q16, sLTP_Q15[ i ] );
         }
      }
      NSQ->sLF_AR_shp_Q14 = silk_SMULWW( gain_adj_Q16, NSQ->sLF_AR_shp_Q14 );
      NSQ->sDiff_shp_Q14 = silk_SMULWW( gain_adj_Q16, NSQ->sDiff_shp_Q14 );
      for( i = 0; i < NSQ_LPC_BUF_LENGTH; i++ ) {
         NSQ->sLPC_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLPC_Q14[ i ] );
      }
      for( i = 0; i < MAX_SHAPE_LPC_ORDER; i++ ) {
         NSQ->sAR2_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sAR2_Q14[ i ] );
      }
      NSQ->prev_gain_Q16 = Gains_Q16[ subfr ];
   }
}

static void silk_NSQ_del_dec_c(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
)
{
   int i, k, lag, start_idx, LSF_interpolation_flag, Winner_ind, subfr;
   int last_smple_idx, smpl_buf_idx, decisionDelay;
   const fl_i16 *A_Q12, *B_Q14, *AR_shp_Q13;
   fl_i16 *pxq;
   VARDECL( fl_i32, sLTP_Q15 );
   VARDECL( fl_i16, sLTP );
   fl_i32 HarmShapeFIRPacked_Q14;
   int offset_Q10;
   fl_i32 RDmin_Q10, Gain_Q10;
   VARDECL( fl_i32, x_sc_Q10 );
   VARDECL( fl_i32, delayedGain_Q10 );
   VARDECL( NSQ_del_dec_struct, psDelDec );
   NSQ_del_dec_struct *psDD;
   SAVE_STACK;
   lag = NSQ->lagPrev;
   silk_assert( NSQ->prev_gain_Q16 != 0 );
   ALLOC( psDelDec, psEncC->nStatesDelayedDecision, NSQ_del_dec_struct );
   silk_memset( psDelDec, 0, psEncC->nStatesDelayedDecision * sizeof( NSQ_del_dec_struct ) );
   for( k = 0; k < psEncC->nStatesDelayedDecision; k++ ) {
      psDD = &psDelDec[ k ];
      psDD->Seed = ( k + psIndices->Seed ) & 3;
      psDD->SeedInit = psDD->Seed;
      psDD->RD_Q10 = 0;
      psDD->LF_AR_Q14 = NSQ->sLF_AR_shp_Q14;
      psDD->Diff_Q14 = NSQ->sDiff_shp_Q14;
      psDD->Shape_Q14[ 0 ] = NSQ->sLTP_shp_Q14[ psEncC->ltp_mem_length - 1 ];
      silk_memcpy( psDD->sLPC_Q14, NSQ->sLPC_Q14, NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
      silk_memcpy( psDD->sAR2_Q14, NSQ->sAR2_Q14, sizeof( NSQ->sAR2_Q14 ) );
   }
   offset_Q10 = fl_Quantization_Offsets_Q10[ psIndices->signalType >> 1 ][ psIndices->quantOffsetType ];
   smpl_buf_idx = 0;
   decisionDelay = silk_min_int( DECISION_DELAY, psEncC->subfr_length );
   if( psIndices->signalType == TYPE_VOICED ) {
      for( k = 0; k < psEncC->nb_subfr; k++ ) {
         decisionDelay = silk_min_int( decisionDelay, pitchL[ k ] - LTP_ORDER / 2 - 1 );
      }
   } else {
      if( lag > 0 ) {
         decisionDelay = silk_min_int( decisionDelay, lag - LTP_ORDER / 2 - 1 );
      }
   }
   if( psIndices->NLSFInterpCoef_Q2 == 4 ) {
      LSF_interpolation_flag = 0;
   } else {
      LSF_interpolation_flag = 1;
   }
   ALLOC( sLTP_Q15, psEncC->ltp_mem_length + psEncC->frame_length, fl_i32 );
   ALLOC( sLTP, psEncC->ltp_mem_length + psEncC->frame_length, fl_i16 );
   ALLOC( x_sc_Q10, psEncC->subfr_length, fl_i32 );
   ALLOC( delayedGain_Q10, DECISION_DELAY, fl_i32 );
   pxq = &NSQ->xq[ psEncC->ltp_mem_length ];
   NSQ->sLTP_shp_buf_idx = psEncC->ltp_mem_length;
   NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
   subfr = 0;
   for( k = 0; k < psEncC->nb_subfr; k++ ) {
      A_Q12 = &PredCoef_Q12[ ( ( k >> 1 ) | ( 1 - LSF_interpolation_flag ) ) * MAX_LPC_ORDER ];
      B_Q14 = &LTPCoef_Q14[ k * LTP_ORDER ];
      AR_shp_Q13 = &AR_Q13[ k * MAX_SHAPE_LPC_ORDER ];
      silk_assert( HarmShapeGain_Q14[ k ] >= 0 );
      HarmShapeFIRPacked_Q14 = silk_RSHIFT( HarmShapeGain_Q14[ k ], 2 );
      HarmShapeFIRPacked_Q14 |= silk_LSHIFT( (fl_i32)silk_RSHIFT( HarmShapeGain_Q14[ k ], 1 ), 16 );
      NSQ->rewhite_flag = 0;
      if( psIndices->signalType == TYPE_VOICED ) {
         lag = pitchL[ k ];
         if( ( k & ( 3 - silk_LSHIFT( LSF_interpolation_flag, 1 ) ) ) == 0 ) {
            if( k == 2 ) {
               RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
               Winner_ind = 0;
               for( i = 1; i < psEncC->nStatesDelayedDecision; i++ ) {
                  if( psDelDec[ i ].RD_Q10 < RDmin_Q10 ) {
                     RDmin_Q10 = psDelDec[ i ].RD_Q10;
                     Winner_ind = i;
                  }
               }
               for( i = 0; i < psEncC->nStatesDelayedDecision; i++ ) {
                  if( i != Winner_ind ) {
                     psDelDec[ i ].RD_Q10 += ( silk_int32_MAX >> 4 );
                     silk_assert( psDelDec[ i ].RD_Q10 >= 0 );
                  }
               }
               psDD = &psDelDec[ Winner_ind ];
               last_smple_idx = smpl_buf_idx + decisionDelay;
               for( i = 0; i < decisionDelay; i++ ) {
                  last_smple_idx = ( last_smple_idx - 1 ) % DECISION_DELAY;
                  if( last_smple_idx < 0 ) last_smple_idx += DECISION_DELAY;
                  pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
                  pxq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
                     silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], Gains_Q16[ 1 ] ), 14 ) );
                  NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q14[ last_smple_idx ];
               }
               subfr = 0;
            }
            start_idx = psEncC->ltp_mem_length - lag - psEncC->predictLPCOrder - LTP_ORDER / 2;
            celt_assert( start_idx > 0 );
            silk_LPC_analysis_filter( &sLTP[ start_idx ], &NSQ->xq[ start_idx + k * psEncC->subfr_length ],
               A_Q12, psEncC->ltp_mem_length - start_idx, psEncC->predictLPCOrder, psEncC->arch );
            NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
            NSQ->rewhite_flag = 1;
         }
      }
      silk_nsq_del_dec_scale_states( psEncC, NSQ, psDelDec, x16, x_sc_Q10, sLTP, sLTP_Q15, k,
         psEncC->nStatesDelayedDecision, LTP_scale_Q14, Gains_Q16, pitchL, psIndices->signalType, decisionDelay );
      silk_noise_shape_quantizer_del_dec( NSQ, psDelDec, psIndices->signalType, x_sc_Q10, pulses, pxq, sLTP_Q15,
         delayedGain_Q10, A_Q12, B_Q14, AR_shp_Q13, lag, HarmShapeFIRPacked_Q14, Tilt_Q14[ k ], LF_shp_Q14[ k ],
         Gains_Q16[ k ], Lambda_Q10, offset_Q10, psEncC->subfr_length, subfr++, psEncC->shapingLPCOrder,
         psEncC->predictLPCOrder, psEncC->warping_Q16, psEncC->nStatesDelayedDecision, &smpl_buf_idx, decisionDelay, psEncC->arch );
      x16 += psEncC->subfr_length;
      pulses += psEncC->subfr_length;
      pxq += psEncC->subfr_length;
   }
   RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
   Winner_ind = 0;
   for( k = 1; k < psEncC->nStatesDelayedDecision; k++ ) {
      if( psDelDec[ k ].RD_Q10 < RDmin_Q10 ) {
         RDmin_Q10 = psDelDec[ k ].RD_Q10;
         Winner_ind = k;
      }
   }
   psDD = &psDelDec[ Winner_ind ];
   psIndices->Seed = psDD->SeedInit;
   last_smple_idx = smpl_buf_idx + decisionDelay;
   Gain_Q10 = silk_RSHIFT32( Gains_Q16[ psEncC->nb_subfr - 1 ], 6 );
   for( i = 0; i < decisionDelay; i++ ) {
      last_smple_idx = ( last_smple_idx - 1 ) % DECISION_DELAY;
      if( last_smple_idx < 0 ) last_smple_idx += DECISION_DELAY;
      pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
      pxq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
         silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], Gain_Q10 ), 8 ) );
      NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q14[ last_smple_idx ];
   }
   silk_memcpy( NSQ->sLPC_Q14, &psDD->sLPC_Q14[ psEncC->subfr_length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
   silk_memcpy( NSQ->sAR2_Q14, psDD->sAR2_Q14, sizeof( psDD->sAR2_Q14 ) );
   NSQ->sLF_AR_shp_Q14 = psDD->LF_AR_Q14;
   NSQ->sDiff_shp_Q14 = psDD->Diff_Q14;
   NSQ->lagPrev = pitchL[ psEncC->nb_subfr - 1 ];
   silk_memmove( NSQ->xq, &NSQ->xq[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i16 ) );
   silk_memmove( NSQ->sLTP_shp_Q14, &NSQ->sLTP_shp_Q14[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
)
{
   int i, j, k, Winner_ind, RDmin_ind, RDmax_ind, last_smple_idx;
   fl_i32 Winner_rand_state;
   fl_i32 LTP_pred_Q14, LPC_pred_Q14, n_AR_Q14, n_LTP_Q14;
   fl_i32 n_LF_Q14, r_Q10, rr_Q10, rd1_Q10, rd2_Q10, RDmin_Q10, RDmax_Q10;
   fl_i32 q1_Q0, q1_Q10, q2_Q10, exc_Q14, LPC_exc_Q14, xq_Q14, Gain_Q10;
   fl_i32 tmp1, tmp2, sLF_AR_shp_Q14;
   fl_i32 *pred_lag_ptr, *shp_lag_ptr, *psLPC_Q14;
   fl_i32 a_Q12_arch[MAX_LPC_ORDER];
   VARDECL( NSQ_sample_pair, psSampleState );
   NSQ_del_dec_struct *psDD;
   NSQ_sample_struct *psSS;
   SAVE_STACK;
   celt_assert( nStatesDelayedDecision > 0 );
   ALLOC( psSampleState, nStatesDelayedDecision, NSQ_sample_pair );
   shp_lag_ptr = &NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - lag + HARM_SHAPE_FIR_TAPS / 2 ];
   pred_lag_ptr = &sLTP_Q15[ NSQ->sLTP_buf_idx - lag + LTP_ORDER / 2 ];
   Gain_Q10 = silk_RSHIFT( Gain_Q16, 6 );
   silk_short_prediction_create_arch_coef(a_Q12_arch, a_Q12, predictLPCOrder);
   for( i = 0; i < length; i++ ) {
      if( signalType == TYPE_VOICED ) {
         LTP_pred_Q14 = 2;
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ 0 ], b_Q14[ 0 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -1 ], b_Q14[ 1 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -2 ], b_Q14[ 2 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -3 ], b_Q14[ 3 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -4 ], b_Q14[ 4 ] );
         LTP_pred_Q14 = silk_LSHIFT( LTP_pred_Q14, 1 );
         pred_lag_ptr++;
      } else {
         LTP_pred_Q14 = 0;
      }
      if( lag > 0 ) {
         n_LTP_Q14 = silk_SMULWB( silk_ADD_SAT32( shp_lag_ptr[ 0 ], shp_lag_ptr[ -2 ] ), HarmShapeFIRPacked_Q14 );
         n_LTP_Q14 = silk_SMLAWT( n_LTP_Q14, shp_lag_ptr[ -1 ], HarmShapeFIRPacked_Q14 );
         n_LTP_Q14 = silk_SUB_LSHIFT32( LTP_pred_Q14, n_LTP_Q14, 2 );
         shp_lag_ptr++;
      } else {
         n_LTP_Q14 = 0;
      }
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psSS = psSampleState[ k ];
         psDD->Seed = silk_RAND( psDD->Seed );
         psLPC_Q14 = &psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH - 1 + i ];
         LPC_pred_Q14 = silk_noise_shape_quantizer_short_prediction(psLPC_Q14, a_Q12, a_Q12_arch, predictLPCOrder, arch);
         LPC_pred_Q14 = silk_LSHIFT( LPC_pred_Q14, 4 );
         celt_assert( ( shapingLPCOrder & 1 ) == 0 );
         tmp2 = silk_SMLAWB( psDD->Diff_Q14, psDD->sAR2_Q14[ 0 ], warping_Q16 );
         tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ 0 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ 1 ], tmp2), warping_Q16 );
         psDD->sAR2_Q14[ 0 ] = tmp2;
         n_AR_Q14 = silk_RSHIFT( shapingLPCOrder, 1 );
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp2, AR_shp_Q13[ 0 ] );
         for( j = 2; j < shapingLPCOrder; j += 2 ) {
            tmp2 = silk_SMLAWB( psDD->sAR2_Q14[ j - 1 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ j + 0 ], tmp1), warping_Q16 );
            psDD->sAR2_Q14[ j - 1 ] = tmp1;
            n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp1, AR_shp_Q13[ j - 1 ] );
            tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ j + 0 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ j + 1 ], tmp2), warping_Q16 );
            psDD->sAR2_Q14[ j + 0 ] = tmp2;
            n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp2, AR_shp_Q13[ j ] );
         }
         psDD->sAR2_Q14[ shapingLPCOrder - 1 ] = tmp1;
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp1, AR_shp_Q13[ shapingLPCOrder - 1 ] );
         n_AR_Q14 = silk_LSHIFT( n_AR_Q14, 1 );
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, psDD->LF_AR_Q14, Tilt_Q14 );
         n_AR_Q14 = silk_LSHIFT( n_AR_Q14, 2 );
         n_LF_Q14 = silk_SMULWB( psDD->Shape_Q14[ *smpl_buf_idx ], LF_shp_Q14 );
         n_LF_Q14 = silk_SMLAWT( n_LF_Q14, psDD->LF_AR_Q14, LF_shp_Q14 );
         n_LF_Q14 = silk_LSHIFT( n_LF_Q14, 2 );
         tmp1 = silk_ADD_SAT32( n_AR_Q14, n_LF_Q14 );
         tmp2 = silk_ADD32_ovflw( n_LTP_Q14, LPC_pred_Q14 );
         tmp1 = silk_SUB_SAT32( tmp2, tmp1 );
         tmp1 = silk_RSHIFT_ROUND( tmp1, 4 );
         r_Q10 = silk_SUB32( x_Q10[ i ], tmp1 );
         if ( psDD->Seed < 0 ) {
            r_Q10 = -r_Q10;
         }
         r_Q10 = silk_LIMIT_32( r_Q10, -(31 << 10), 30 << 10 );
         q1_Q10 = silk_SUB32( r_Q10, offset_Q10 );
         q1_Q0 = silk_RSHIFT( q1_Q10, 10 );
         if (Lambda_Q10 > 2048) {
            int rdo_offset = Lambda_Q10/2 - 512;
            if (q1_Q10 > rdo_offset) {
               q1_Q0 = silk_RSHIFT( q1_Q10 - rdo_offset, 10 );
            } else if (q1_Q10 < -rdo_offset) {
               q1_Q0 = silk_RSHIFT( q1_Q10 + rdo_offset, 10 );
            } else if (q1_Q10 < 0) {
               q1_Q0 = -1;
            } else {
               q1_Q0 = 0;
            }
         }
         if( q1_Q0 > 0 ) {
            q1_Q10 = silk_SUB32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
            q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
            q2_Q10 = silk_ADD32( q1_Q10, 1024 );
            rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else if( q1_Q0 == 0 ) {
            q1_Q10 = offset_Q10;
            q2_Q10 = silk_ADD32( q1_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
            rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else if( q1_Q0 == -1 ) {
            q2_Q10 = offset_Q10;
            q1_Q10 = silk_SUB32( q2_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
            rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else {
            q1_Q10 = silk_ADD32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
            q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
            q2_Q10 = silk_ADD32( q1_Q10, 1024 );
            rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( -q2_Q10, Lambda_Q10 );
         }
         rr_Q10 = silk_SUB32( r_Q10, q1_Q10 );
         rd1_Q10 = silk_RSHIFT( silk_SMLABB( rd1_Q10, rr_Q10, rr_Q10 ), 10 );
         rr_Q10 = silk_SUB32( r_Q10, q2_Q10 );
         rd2_Q10 = silk_RSHIFT( silk_SMLABB( rd2_Q10, rr_Q10, rr_Q10 ), 10 );
         if( rd1_Q10 < rd2_Q10 ) {
            psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
            psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
            psSS[ 0 ].Q_Q10 = q1_Q10;
            psSS[ 1 ].Q_Q10 = q2_Q10;
         } else {
            psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
            psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
            psSS[ 0 ].Q_Q10 = q2_Q10;
            psSS[ 1 ].Q_Q10 = q1_Q10;
         }
         exc_Q14 = silk_LSHIFT32( psSS[ 0 ].Q_Q10, 4 );
         if ( psDD->Seed < 0 ) {
            exc_Q14 = -exc_Q14;
         }
         LPC_exc_Q14 = silk_ADD32( exc_Q14, LTP_pred_Q14 );
         xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, LPC_pred_Q14 );
         psSS[ 0 ].Diff_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_Q10[ i ], 4 ) );
         sLF_AR_shp_Q14 = silk_SUB32_ovflw( psSS[ 0 ].Diff_Q14, n_AR_Q14 );
         psSS[ 0 ].sLTP_shp_Q14 = silk_SUB_SAT32( sLF_AR_shp_Q14, n_LF_Q14 );
         psSS[ 0 ].LF_AR_Q14 = sLF_AR_shp_Q14;
         psSS[ 0 ].LPC_exc_Q14 = LPC_exc_Q14;
         psSS[ 0 ].xq_Q14 = xq_Q14;
         exc_Q14 = silk_LSHIFT32( psSS[ 1 ].Q_Q10, 4 );
         if ( psDD->Seed < 0 ) {
            exc_Q14 = -exc_Q14;
         }
         LPC_exc_Q14 = silk_ADD32( exc_Q14, LTP_pred_Q14 );
         xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, LPC_pred_Q14 );
         psSS[ 1 ].Diff_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_Q10[ i ], 4 ) );
         sLF_AR_shp_Q14 = silk_SUB32_ovflw( psSS[ 1 ].Diff_Q14, n_AR_Q14 );
         psSS[ 1 ].sLTP_shp_Q14 = silk_SUB_SAT32( sLF_AR_shp_Q14, n_LF_Q14 );
         psSS[ 1 ].LF_AR_Q14 = sLF_AR_shp_Q14;
         psSS[ 1 ].LPC_exc_Q14 = LPC_exc_Q14;
         psSS[ 1 ].xq_Q14 = xq_Q14;
      }
      *smpl_buf_idx = ( *smpl_buf_idx - 1 ) % DECISION_DELAY;
      if( *smpl_buf_idx < 0 ) *smpl_buf_idx += DECISION_DELAY;
      last_smple_idx = ( *smpl_buf_idx + decisionDelay ) % DECISION_DELAY;
      RDmin_Q10 = psSampleState[ 0 ][ 0 ].RD_Q10;
      Winner_ind = 0;
      for( k = 1; k < nStatesDelayedDecision; k++ ) {
         if( psSampleState[ k ][ 0 ].RD_Q10 < RDmin_Q10 ) {
            RDmin_Q10 = psSampleState[ k ][ 0 ].RD_Q10;
            Winner_ind = k;
         }
      }
      Winner_rand_state = psDelDec[ Winner_ind ].RandState[ last_smple_idx ];
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         if( psDelDec[ k ].RandState[ last_smple_idx ] != Winner_rand_state ) {
            psSampleState[ k ][ 0 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 0 ].RD_Q10, silk_int32_MAX >> 4 );
            psSampleState[ k ][ 1 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 1 ].RD_Q10, silk_int32_MAX >> 4 );
            silk_assert( psSampleState[ k ][ 0 ].RD_Q10 >= 0 );
         }
      }
      RDmax_Q10 = psSampleState[ 0 ][ 0 ].RD_Q10;
      RDmin_Q10 = psSampleState[ 0 ][ 1 ].RD_Q10;
      RDmax_ind = 0;
      RDmin_ind = 0;
      for( k = 1; k < nStatesDelayedDecision; k++ ) {
         if( psSampleState[ k ][ 0 ].RD_Q10 > RDmax_Q10 ) {
            RDmax_Q10 = psSampleState[ k ][ 0 ].RD_Q10;
            RDmax_ind = k;
         }
         if( psSampleState[ k ][ 1 ].RD_Q10 < RDmin_Q10 ) {
            RDmin_Q10 = psSampleState[ k ][ 1 ].RD_Q10;
            RDmin_ind = k;
         }
      }
      if( RDmin_Q10 < RDmax_Q10 ) {
         silk_memcpy( ( (fl_i32 *)&psDelDec[ RDmax_ind ] ) + i,
                  ( (fl_i32 *)&psDelDec[ RDmin_ind ] ) + i, sizeof( NSQ_del_dec_struct ) - i * sizeof( fl_i32) );
         silk_memcpy( &psSampleState[ RDmax_ind ][ 0 ], &psSampleState[ RDmin_ind ][ 1 ], sizeof( NSQ_sample_struct ) );
      }
      psDD = &psDelDec[ Winner_ind ];
      if( subfr > 0 || i >= decisionDelay ) {
         pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
         xq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
            silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], delayedGain_Q10[ last_smple_idx ] ), 8 ) );
         NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay ] = psDD->Shape_Q14[ last_smple_idx ];
         sLTP_Q15[ NSQ->sLTP_buf_idx - decisionDelay ] = psDD->Pred_Q15[ last_smple_idx ];
      }
      NSQ->sLTP_shp_buf_idx++;
      NSQ->sLTP_buf_idx++;
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psSS = &psSampleState[ k ][ 0 ];
         psDD->LF_AR_Q14 = psSS->LF_AR_Q14;
         psDD->Diff_Q14 = psSS->Diff_Q14;
         psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH + i ] = psSS->xq_Q14;
         psDD->Xq_Q14[ *smpl_buf_idx ] = psSS->xq_Q14;
         psDD->Q_Q10[ *smpl_buf_idx ] = psSS->Q_Q10;
         psDD->Pred_Q15[ *smpl_buf_idx ] = silk_LSHIFT32( psSS->LPC_exc_Q14, 1 );
         psDD->Shape_Q14[ *smpl_buf_idx ] = psSS->sLTP_shp_Q14;
         psDD->Seed = silk_ADD32_ovflw( psDD->Seed, silk_RSHIFT_ROUND( psSS->Q_Q10, 10 ) );
         psDD->RandState[ *smpl_buf_idx ] = psDD->Seed;
         psDD->RD_Q10 = psSS->RD_Q10;
      }
      delayedGain_Q10[ *smpl_buf_idx ] = Gain_Q10;
   }
   for( k = 0; k < nStatesDelayedDecision; k++ ) {
      psDD = &psDelDec[ k ];
      silk_memcpy( psDD->sLPC_Q14, &psDD->sLPC_Q14[ length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
   }
   RESTORE_STACK;
}

static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
)
{
   int i, k, lag;
   fl_i32 gain_adj_Q16, inv_gain_Q31, inv_gain_Q26;
   NSQ_del_dec_struct *psDD;
   lag = pitchL[ subfr ];
   inv_gain_Q31 = silk_INVERSE32_varQ( silk_max( Gains_Q16[ subfr ], 1 ), 47 );
   silk_assert( inv_gain_Q31 != 0 );
   inv_gain_Q26 = silk_RSHIFT_ROUND( inv_gain_Q31, 5 );
   for( i = 0; i < psEncC->subfr_length; i++ ) {
      x_sc_Q10[ i ] = silk_SMULWW( x16[ i ], inv_gain_Q26 );
   }
   if( NSQ->rewhite_flag ) {
      if( subfr == 0 ) {
         inv_gain_Q31 = silk_LSHIFT( silk_SMULWB( inv_gain_Q31, LTP_scale_Q14 ), 2 );
      }
      for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
         silk_assert( i < MAX_FRAME_LENGTH );
         sLTP_Q15[ i ] = silk_SMULWB( inv_gain_Q31, sLTP[ i ] );
      }
   }
   if( Gains_Q16[ subfr ] != NSQ->prev_gain_Q16 ) {
      gain_adj_Q16 = silk_DIV32_varQ( NSQ->prev_gain_Q16, Gains_Q16[ subfr ], 16 );
      for( i = NSQ->sLTP_shp_buf_idx - psEncC->ltp_mem_length; i < NSQ->sLTP_shp_buf_idx; i++ ) {
         NSQ->sLTP_shp_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLTP_shp_Q14[ i ] );
      }
      if( signal_type == TYPE_VOICED && NSQ->rewhite_flag == 0 ) {
         for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx - decisionDelay; i++ ) {
            sLTP_Q15[ i ] = silk_SMULWW( gain_adj_Q16, sLTP_Q15[ i ] );
         }
      }
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psDD->LF_AR_Q14 = silk_SMULWW( gain_adj_Q16, psDD->LF_AR_Q14 );
         psDD->Diff_Q14 = silk_SMULWW( gain_adj_Q16, psDD->Diff_Q14 );
         for( i = 0; i < NSQ_LPC_BUF_LENGTH; i++ ) {
            psDD->sLPC_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sLPC_Q14[ i ] );
         }
         for( i = 0; i < MAX_SHAPE_LPC_ORDER; i++ ) {
            psDD->sAR2_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sAR2_Q14[ i ] );
         }
         for( i = 0; i < DECISION_DELAY; i++ ) {
            psDD->Pred_Q15[ i ] = silk_SMULWW( gain_adj_Q16, psDD->Pred_Q15[ i ] );
            psDD->Shape_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->Shape_Q14[ i ] );
         }
      }
      NSQ->prev_gain_Q16 = Gains_Q16[ subfr ];
   }
}

static void silk_resampler_down2(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
)
{
   fl_i32 k, len2 = silk_RSHIFT32( inLen, 1 );
   fl_i32 in32, out32, Y, X;
   celt_assert( silk_resampler_down2_0 > 0 );
   celt_assert( silk_resampler_down2_1 < 0 );
   for( k = 0; k < len2; k++ ) {
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k ], 10 );
      Y = silk_SUB32( in32, S[ 0 ] );
      X = silk_SMLAWB( Y, Y, silk_resampler_down2_1 );
      out32 = silk_ADD32( S[ 0 ], X );
      S[ 0 ] = silk_ADD32( in32, X );
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k + 1 ], 10 );
      Y = silk_SUB32( in32, S[ 1 ] );
      X = silk_SMULWB( Y, silk_resampler_down2_0 );
      out32 = silk_ADD32( out32, S[ 1 ] );
      out32 = silk_ADD32( out32, X );
      S[ 1 ] = silk_ADD32( in32, X );
      out[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( out32, 11 ) );
   }
}

static void silk_resampler_down2_3(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
)
{
   fl_i32 nSamplesIn, counter, res_Q6;
   VARDECL( fl_i32, buf );
   fl_i32 *buf_ptr;
   SAVE_STACK;
   ALLOC( buf, RESAMPLER_MAX_BATCH_SIZE_IN + ORDER_FIR, fl_i32 );
   silk_memcpy( buf, S, ORDER_FIR * sizeof( fl_i32 ) );
   while( 1 ) {
      nSamplesIn = silk_min( inLen, RESAMPLER_MAX_BATCH_SIZE_IN );
      silk_resampler_private_AR2( &S[ ORDER_FIR ], &buf[ ORDER_FIR ], in,
         fl_Resampler_2_3_COEFS_LQ, nSamplesIn );
      buf_ptr = buf;
      counter = nSamplesIn;
      while( counter > 2 ) {
         res_Q6 = silk_SMULWB( buf_ptr[ 0 ], fl_Resampler_2_3_COEFS_LQ[ 2 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 1 ], fl_Resampler_2_3_COEFS_LQ[ 3 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 2 ], fl_Resampler_2_3_COEFS_LQ[ 5 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 3 ], fl_Resampler_2_3_COEFS_LQ[ 4 ] );
         *out++ = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( res_Q6, 6 ) );
         res_Q6 = silk_SMULWB( buf_ptr[ 1 ], fl_Resampler_2_3_COEFS_LQ[ 4 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 2 ], fl_Resampler_2_3_COEFS_LQ[ 5 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 3 ], fl_Resampler_2_3_COEFS_LQ[ 3 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 4 ], fl_Resampler_2_3_COEFS_LQ[ 2 ] );
         *out++ = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( res_Q6, 6 ) );
         buf_ptr += 3;
         counter -= 3;
      }
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if( inLen > 0 ) {
         silk_memcpy( buf, &buf[ nSamplesIn ], ORDER_FIR * sizeof( fl_i32 ) );
      } else {
         break;
      }
   }
   silk_memcpy( S, &buf[ nSamplesIn ], ORDER_FIR * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static int silk_Get_Encoder_Size(
   int *encSizeBytes,
   int channels
)
{
   int ret = SILK_NO_ERROR;
   *encSizeBytes = sizeof( fl_silk_encoder );
   if ( channels == 1 ) {
      *encSizeBytes -= sizeof( silk_encoder_state_Fxx );
   }
   return ret;
}

static int silk_InitEncoder(
   void *encState,
   int channels,
   int arch,
   fl_silk_ctrl *encStatus
)
{
   fl_silk_encoder *psEnc;
   int n, ret = SILK_NO_ERROR;
   psEnc = (fl_silk_encoder *)encState;
   silk_memset( psEnc, 0, sizeof( fl_silk_encoder ) - (channels==1)*sizeof( silk_encoder_state_Fxx ) );
   for( n = 0; n < channels; n++ ) {
      if( ret += silk_init_encoder( &psEnc->state_Fxx[ n ], arch ) ) {
         celt_assert( 0 );
      }
   }
   psEnc->nChannelsAPI = 1;
   psEnc->nChannelsInternal = 1;
   if( ret += silk_QueryEncoder( encState, encStatus ) ) {
      celt_assert( 0 );
   }
   return ret;
}

static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
)
{
   int ret = SILK_NO_ERROR;
   silk_encoder_state_Fxx *state_Fxx;
   fl_silk_encoder *psEnc = (fl_silk_encoder *)encState;
   state_Fxx = psEnc->state_Fxx;
   encStatus->nChannelsAPI = psEnc->nChannelsAPI;
   encStatus->nChannelsInternal = psEnc->nChannelsInternal;
   encStatus->API_sampleRate = state_Fxx[ 0 ].sCmn.API_fs_Hz;
   encStatus->maxInternalSampleRate = state_Fxx[ 0 ].sCmn.maxInternal_fs_Hz;
   encStatus->minInternalSampleRate = state_Fxx[ 0 ].sCmn.minInternal_fs_Hz;
   encStatus->desiredInternalSampleRate = state_Fxx[ 0 ].sCmn.desiredInternal_fs_Hz;
   encStatus->payloadSize_ms = state_Fxx[ 0 ].sCmn.PacketSize_ms;
   encStatus->bitRate = state_Fxx[ 0 ].sCmn.TargetRate_bps;
   encStatus->packetLossPercentage = state_Fxx[ 0 ].sCmn.PacketLoss_perc;
   encStatus->complexity = state_Fxx[ 0 ].sCmn.Complexity;
   encStatus->useInBandFEC = state_Fxx[ 0 ].sCmn.useInBandFEC;
   encStatus->useDTX = state_Fxx[ 0 ].sCmn.useDTX;
   encStatus->useCBR = state_Fxx[ 0 ].sCmn.useCBR;
   encStatus->internalSampleRate = silk_SMULBB( state_Fxx[ 0 ].sCmn.fs_kHz, 1000 );
   encStatus->allowBandwidthSwitch = state_Fxx[ 0 ].sCmn.allow_bandwidth_switch;
   encStatus->inWBmodeWithoutVariableLP = state_Fxx[ 0 ].sCmn.fs_kHz == 16 && state_Fxx[ 0 ].sCmn.sLP.mode == 0;
   return ret;
}

static int silk_Encode(
   void *encState,
   fl_silk_ctrl *encControl,
   const opus_res *samplesIn,
   int nSamplesIn,
   ec_ctx *psRangeEnc,
   fl_i32 *nBytesOut,
   const int prefillFlag,
   int activity
)
{
   int n, i, nBits, flags, tmp_payloadSize_ms = 0, tmp_complexity = 0, ret = 0;
   int nSamplesToBuffer, nSamplesToBufferMax, nBlocksOf10ms;
   int nSamplesFromInput = 0, nSamplesFromInputMax;
   int speech_act_thr_for_switch_Q8;
   fl_i32 TargetRate_bps, MStargetRates_bps[ 2 ], channelRate_bps, LBRR_symbol, sum;
   fl_silk_encoder *psEnc = ( fl_silk_encoder * )encState;
   VARDECL( fl_i16, buf );
   int transition, curr_block, tot_blocks;
   SAVE_STACK;
   celt_assert( encControl->nChannelsAPI >= encControl->nChannelsInternal && encControl->nChannelsAPI >= psEnc->nChannelsInternal );
   if (encControl->reducedDependency)
   {
     for( n = 0; n < encControl->nChannelsAPI; n++ ) {
        psEnc->state_Fxx[ n ].sCmn.first_frame_after_reset = 1;
     }
   }
   for( n = 0; n < encControl->nChannelsAPI; n++ ) {
      psEnc->state_Fxx[ n ].sCmn.nFramesEncoded = 0;
   }
   if( ( ret = check_control_input( encControl ) ) != 0 ) {
      celt_assert( 0 );
      RESTORE_STACK;
      return ret;
   }
   encControl->switchReady = 0;
   if( encControl->nChannelsInternal > psEnc->nChannelsInternal ) {
      ret += silk_init_encoder( &psEnc->state_Fxx[ 1 ], psEnc->state_Fxx[ 0 ].sCmn.arch );
      silk_memset( psEnc->sStereo.pred_prev_Q13, 0, sizeof( psEnc->sStereo.pred_prev_Q13 ) );
      silk_memset( psEnc->sStereo.sSide, 0, sizeof( psEnc->sStereo.sSide ) );
      psEnc->sStereo.mid_side_amp_Q0[ 0 ] = 0;
      psEnc->sStereo.mid_side_amp_Q0[ 1 ] = 1;
      psEnc->sStereo.mid_side_amp_Q0[ 2 ] = 0;
      psEnc->sStereo.mid_side_amp_Q0[ 3 ] = 1;
      psEnc->sStereo.width_prev_Q14 = 0;
      psEnc->sStereo.smth_width_Q14 = SILK_FIX_CONST( 1, 14 );
      if( psEnc->nChannelsAPI == 2 ) {
         silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state, &psEnc->state_Fxx[ 0 ].sCmn.resampler_state, sizeof( silk_resampler_state ) );
         silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.In_HP_State, &psEnc->state_Fxx[ 0 ].sCmn.In_HP_State, sizeof( psEnc->state_Fxx[ 1 ].sCmn.In_HP_State ) );
      }
   }
   transition = (encControl->payloadSize_ms != psEnc->state_Fxx[ 0 ].sCmn.PacketSize_ms) || (psEnc->nChannelsInternal != encControl->nChannelsInternal);
   psEnc->nChannelsAPI = encControl->nChannelsAPI;
   psEnc->nChannelsInternal = encControl->nChannelsInternal;
   nBlocksOf10ms = silk_DIV32( 100 * nSamplesIn, encControl->API_sampleRate );
   tot_blocks = ( nBlocksOf10ms > 1 ) ? nBlocksOf10ms >> 1 : 1;
   curr_block = 0;
   if( prefillFlag ) {
      silk_LP_state save_LP;
      if( nBlocksOf10ms != 1 ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
      if ( prefillFlag == 2 ) {
         save_LP = psEnc->state_Fxx[ 0 ].sCmn.sLP;
         save_LP.saved_fs_kHz = psEnc->state_Fxx[ 0 ].sCmn.fs_kHz;
      }
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         ret = silk_init_encoder( &psEnc->state_Fxx[ n ], psEnc->state_Fxx[ n ].sCmn.arch );
         if ( prefillFlag == 2 ) {
            psEnc->state_Fxx[ n ].sCmn.sLP = save_LP;
         }
         celt_assert( !ret );
      }
      tmp_payloadSize_ms = encControl->payloadSize_ms;
      encControl->payloadSize_ms = 10;
      tmp_complexity = encControl->complexity;
      encControl->complexity = 0;
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
         psEnc->state_Fxx[ n ].sCmn.prefillFlag = 1;
      }
   } else {
      if( nBlocksOf10ms * encControl->API_sampleRate != 100 * nSamplesIn || nSamplesIn < 0 ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
      if( 1000 * (fl_i32)nSamplesIn > encControl->payloadSize_ms * encControl->API_sampleRate ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
   }
   for( n = 0; n < encControl->nChannelsInternal; n++ ) {
      int force_fs_kHz = (n==1) ? psEnc->state_Fxx[0].sCmn.fs_kHz : 0;
      if( ( ret = silk_control_encoder( &psEnc->state_Fxx[ n ], encControl, psEnc->allowBandwidthSwitch, n, force_fs_kHz ) ) != 0 ) {
         silk_assert( 0 );
         RESTORE_STACK;
         return ret;
      }
      if( psEnc->state_Fxx[n].sCmn.first_frame_after_reset || transition ) {
         for( i = 0; i < psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket; i++ ) {
            psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ] = 0;
         }
      }
      psEnc->state_Fxx[ n ].sCmn.inDTX = psEnc->state_Fxx[ n ].sCmn.useDTX;
   }
   celt_assert( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 0 ].sCmn.fs_kHz == psEnc->state_Fxx[ 1 ].sCmn.fs_kHz );
   nSamplesToBufferMax =
      10 * nBlocksOf10ms * psEnc->state_Fxx[ 0 ].sCmn.fs_kHz;
   nSamplesFromInputMax =
      silk_DIV32_16( nSamplesToBufferMax *
                    psEnc->state_Fxx[ 0 ].sCmn.API_fs_Hz,
                 psEnc->state_Fxx[ 0 ].sCmn.fs_kHz * 1000 );
   ALLOC( buf, nSamplesFromInputMax, fl_i16 );
   while( 1 ) {
      int curr_nBitsUsedLBRR = 0;
      nSamplesToBuffer = psEnc->state_Fxx[ 0 ].sCmn.frame_length - psEnc->state_Fxx[ 0 ].sCmn.inputBufIx;
      nSamplesToBuffer = silk_min( nSamplesToBuffer, nSamplesToBufferMax );
      nSamplesFromInput = silk_DIV32_16( nSamplesToBuffer * psEnc->state_Fxx[ 0 ].sCmn.API_fs_Hz, psEnc->state_Fxx[ 0 ].sCmn.fs_kHz * 1000 );
      if( encControl->nChannelsAPI == 2 && encControl->nChannelsInternal == 2 ) {
         int id = psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded;
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[ n ] = RES2INT16(samplesIn[ 2 * n ]);
         }
         if( psEnc->nPrevChannelsInternal == 1 && id==0 ) {
           silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state, &psEnc->state_Fxx[ 0 ].sCmn.resampler_state, sizeof(psEnc->state_Fxx[ 1 ].sCmn.resampler_state));
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
         nSamplesToBuffer = psEnc->state_Fxx[ 1 ].sCmn.frame_length - psEnc->state_Fxx[ 1 ].sCmn.inputBufIx;
         nSamplesToBuffer = silk_min( nSamplesToBuffer, 10 * nBlocksOf10ms * psEnc->state_Fxx[ 1 ].sCmn.fs_kHz );
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[ n ] = RES2INT16(samplesIn[ 2 * n + 1 ]);
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 1 ].sCmn.inputBufIx += nSamplesToBuffer;
      } else if( encControl->nChannelsAPI == 2 && encControl->nChannelsInternal == 1 ) {
         for( n = 0; n < nSamplesFromInput; n++ ) {
            sum = RES2INT16(samplesIn[ 2 * n ] + samplesIn[ 2 * n + 1 ]);
            buf[ n ] = (fl_i16)silk_RSHIFT_ROUND( sum, 1 );
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         if( psEnc->nPrevChannelsInternal == 2 && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == 0 ) {
           ret += silk_resampler( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state,
              &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
           for( n = 0; n < psEnc->state_Fxx[ 0 ].sCmn.frame_length; n++ ) {
             psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx+n+2 ] =
                  silk_RSHIFT(psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx+n+2 ]
                         + psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx+n+2 ], 1);
           }
         }
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
      } else {
         celt_assert( encControl->nChannelsAPI == 1 && encControl->nChannelsInternal == 1 );
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[n] = RES2INT16(samplesIn[n]);
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
      }
      samplesIn += nSamplesFromInput * encControl->nChannelsAPI;
      nSamplesIn -= nSamplesFromInput;
      psEnc->allowBandwidthSwitch = 0;
      if( psEnc->state_Fxx[ 0 ].sCmn.inputBufIx >= psEnc->state_Fxx[ 0 ].sCmn.frame_length ) {
         celt_assert( psEnc->state_Fxx[ 0 ].sCmn.inputBufIx == psEnc->state_Fxx[ 0 ].sCmn.frame_length );
         celt_assert( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 1 ].sCmn.inputBufIx == psEnc->state_Fxx[ 1 ].sCmn.frame_length );
         if( psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == 0 && !prefillFlag ) {
            fl_u8 iCDF[ 2 ] = { 0, 0 };
            iCDF[ 0 ] = 256 - silk_RSHIFT( 256, ( psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket + 1 ) * encControl->nChannelsInternal );
            ec_enc_icdf( psRangeEnc, 0, iCDF, 8 );
            curr_nBitsUsedLBRR = ec_tell( psRangeEnc );
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               LBRR_symbol = 0;
               for( i = 0; i < psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket; i++ ) {
                  LBRR_symbol |= silk_LSHIFT( psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ], i );
               }
               psEnc->state_Fxx[ n ].sCmn.LBRR_flag = LBRR_symbol > 0 ? 1 : 0;
               if( LBRR_symbol && psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket > 1 ) {
                  ec_enc_icdf( psRangeEnc, LBRR_symbol - 1, fl_LBRR_flags_iCDF_ptr[ psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket - 2 ], 8 );
               }
            }
            for( i = 0; i < psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket; i++ ) {
               for( n = 0; n < encControl->nChannelsInternal; n++ ) {
                  if( psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ] ) {
                     int condCoding;
                     if( encControl->nChannelsInternal == 2 && n == 0 ) {
                        silk_stereo_encode_pred( psRangeEnc, psEnc->sStereo.predIx[ i ] );
                        if( psEnc->state_Fxx[ 1 ].sCmn.LBRR_flags[ i ] == 0 ) {
                           silk_stereo_encode_mid_only( psRangeEnc, psEnc->sStereo.mid_only_flags[ i ] );
                        }
                     }
                     if( i > 0 && psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i - 1 ] ) {
                        condCoding = CODE_CONDITIONALLY;
                     } else {
                        condCoding = CODE_INDEPENDENTLY;
                     }
                     silk_encode_indices( &psEnc->state_Fxx[ n ].sCmn, psRangeEnc, i, 1, condCoding );
                     silk_encode_pulses( psRangeEnc, psEnc->state_Fxx[ n ].sCmn.indices_LBRR[i].signalType, psEnc->state_Fxx[ n ].sCmn.indices_LBRR[i].quantOffsetType,
                        psEnc->state_Fxx[ n ].sCmn.pulses_LBRR[ i ], psEnc->state_Fxx[ n ].sCmn.frame_length );
                  }
               }
            }
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               silk_memset( psEnc->state_Fxx[ n ].sCmn.LBRR_flags, 0, sizeof( psEnc->state_Fxx[ n ].sCmn.LBRR_flags ) );
            }
            curr_nBitsUsedLBRR = ec_tell( psRangeEnc ) - curr_nBitsUsedLBRR;
         }
         silk_HP_variable_cutoff( psEnc->state_Fxx );
         nBits = silk_DIV32_16( silk_MUL( encControl->bitRate, encControl->payloadSize_ms ), 1000 );
         if( !prefillFlag ) {
            if ( curr_nBitsUsedLBRR < 10 ) {
               psEnc->nBitsUsedLBRR = 0;
            } else if ( psEnc->nBitsUsedLBRR < 10) {
               psEnc->nBitsUsedLBRR = curr_nBitsUsedLBRR;
            } else {
               psEnc->nBitsUsedLBRR = ( psEnc->nBitsUsedLBRR + curr_nBitsUsedLBRR ) / 2;
            }
            nBits -= psEnc->nBitsUsedLBRR;
         }
         nBits = silk_DIV32_16( nBits, psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket );
         if( encControl->payloadSize_ms == 10 ) {
            TargetRate_bps = silk_SMULBB( nBits, 100 );
         } else {
            TargetRate_bps = silk_SMULBB( nBits, 50 );
         }
         TargetRate_bps -= silk_DIV32_16( silk_MUL( psEnc->nBitsExceeded, 1000 ), BITRESERVOIR_DECAY_TIME_MS );
         if( !prefillFlag && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded > 0 ) {
            fl_i32 bitsBalance = ec_tell( psRangeEnc ) - psEnc->nBitsUsedLBRR - nBits * psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded;
            TargetRate_bps -= silk_DIV32_16( silk_MUL( bitsBalance, 1000 ), BITRESERVOIR_DECAY_TIME_MS );
         }
         TargetRate_bps = silk_LIMIT( TargetRate_bps, encControl->bitRate, 5000 );
         if( encControl->nChannelsInternal == 2 ) {
            silk_stereo_LR_to_MS( &psEnc->sStereo, &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ 2 ], &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ 2 ],
               psEnc->sStereo.predIx[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ], &psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ],
               MStargetRates_bps, TargetRate_bps, psEnc->state_Fxx[ 0 ].sCmn.speech_activity_Q8, encControl->toMono,
               psEnc->state_Fxx[ 0 ].sCmn.fs_kHz, psEnc->state_Fxx[ 0 ].sCmn.frame_length );
            if( psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] == 0 ) {
               if( psEnc->prev_decode_only_middle == 1 ) {
                  silk_memset( &psEnc->state_Fxx[ 1 ].sShape, 0, sizeof( psEnc->state_Fxx[ 1 ].sShape ) );
                  silk_memset( &psEnc->state_Fxx[ 1 ].sCmn.sNSQ, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.sNSQ ) );
                  silk_memset( psEnc->state_Fxx[ 1 ].sCmn.prev_NLSFq_Q15, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.prev_NLSFq_Q15 ) );
                  silk_memset( &psEnc->state_Fxx[ 1 ].sCmn.sLP.In_LP_State, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.sLP.In_LP_State ) );
                  psEnc->state_Fxx[ 1 ].sCmn.prevLag = 100;
                  psEnc->state_Fxx[ 1 ].sCmn.sNSQ.lagPrev = 100;
                  psEnc->state_Fxx[ 1 ].sShape.LastGainIndex = 10;
                  psEnc->state_Fxx[ 1 ].sCmn.prevSignalType = TYPE_NO_VOICE_ACTIVITY;
                  psEnc->state_Fxx[ 1 ].sCmn.sNSQ.prev_gain_Q16 = 65536;
                  psEnc->state_Fxx[ 1 ].sCmn.first_frame_after_reset = 1;
               }
               silk_encode_do_VAD_Fxx( &psEnc->state_Fxx[ 1 ], activity );
            } else {
               psEnc->state_Fxx[ 1 ].sCmn.VAD_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] = 0;
            }
            if( !prefillFlag ) {
               silk_stereo_encode_pred( psRangeEnc, psEnc->sStereo.predIx[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] );
               if( psEnc->state_Fxx[ 1 ].sCmn.VAD_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] == 0 ) {
                  silk_stereo_encode_mid_only( psRangeEnc, psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] );
               }
            }
         } else {
            silk_memcpy( psEnc->state_Fxx[ 0 ].sCmn.inputBuf, psEnc->sStereo.sMid, 2 * sizeof( fl_i16 ) );
            silk_memcpy( psEnc->sStereo.sMid, &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.frame_length ], 2 * sizeof( fl_i16 ) );
         }
         silk_encode_do_VAD_Fxx( &psEnc->state_Fxx[ 0 ], activity );
         for( n = 0; n < encControl->nChannelsInternal; n++ ) {
            int maxBits, useCBR;
            maxBits = encControl->maxBits;
            if( tot_blocks == 2 && curr_block == 0 ) {
               maxBits = maxBits * 3 / 5;
            } else if( tot_blocks == 3 ) {
               if( curr_block == 0 ) {
                  maxBits = maxBits * 2 / 5;
               } else if( curr_block == 1 ) {
                  maxBits = maxBits * 3 / 4;
               }
            }
            useCBR = encControl->useCBR && curr_block == tot_blocks - 1;
            if( encControl->nChannelsInternal == 1 ) {
               channelRate_bps = TargetRate_bps;
            } else {
               channelRate_bps = MStargetRates_bps[ n ];
               if( n == 0 && MStargetRates_bps[ 1 ] > 0 ) {
                  useCBR = 0;
                  maxBits -= encControl->maxBits / ( tot_blocks * 2 );
               }
            }
            if( channelRate_bps > 0 ) {
               int condCoding;
               silk_control_SNR( &psEnc->state_Fxx[ n ].sCmn, channelRate_bps );
               if( psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded - n <= 0 ) {
                  condCoding = CODE_INDEPENDENTLY;
               } else if( n > 0 && psEnc->prev_decode_only_middle ) {
                  condCoding = CODE_INDEPENDENTLY_NO_LTP_SCALING;
               } else {
                  condCoding = CODE_CONDITIONALLY;
               }
               if( ( ret = silk_encode_frame_Fxx( &psEnc->state_Fxx[ n ], nBytesOut, psRangeEnc, condCoding, maxBits, useCBR ) ) != 0 ) {
                  silk_assert( 0 );
               }
            }
            psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
            psEnc->state_Fxx[ n ].sCmn.inputBufIx = 0;
            psEnc->state_Fxx[ n ].sCmn.nFramesEncoded++;
         }
         psEnc->prev_decode_only_middle = psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded - 1 ];
         if( *nBytesOut > 0 && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket) {
            flags = 0;
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               for( i = 0; i < psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket; i++ ) {
                  flags = silk_LSHIFT( flags, 1 );
                  flags |= psEnc->state_Fxx[ n ].sCmn.VAD_flags[ i ];
               }
               flags = silk_LSHIFT( flags, 1 );
               flags |= psEnc->state_Fxx[ n ].sCmn.LBRR_flag;
            }
            if( !prefillFlag ) {
               ec_enc_patch_initial_bits( psRangeEnc, flags, ( psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket + 1 ) * encControl->nChannelsInternal );
            }
            if( psEnc->state_Fxx[ 0 ].sCmn.inDTX && ( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 1 ].sCmn.inDTX ) ) {
               *nBytesOut = 0;
            }
            psEnc->nBitsExceeded += *nBytesOut * 8;
            psEnc->nBitsExceeded -= silk_DIV32_16( silk_MUL( encControl->bitRate, encControl->payloadSize_ms ), 1000 );
            psEnc->nBitsExceeded = silk_LIMIT( psEnc->nBitsExceeded, 0, 10000 );
            speech_act_thr_for_switch_Q8 = silk_SMLAWB( SILK_FIX_CONST( SPEECH_ACTIVITY_DTX_THRES, 8 ),
               SILK_FIX_CONST( ( 1 - SPEECH_ACTIVITY_DTX_THRES ) / MAX_BANDWIDTH_SWITCH_DELAY_MS, 16 + 8 ), psEnc->timeSinceSwitchAllowed_ms );
            if( psEnc->state_Fxx[ 0 ].sCmn.speech_activity_Q8 < speech_act_thr_for_switch_Q8 ) {
               psEnc->allowBandwidthSwitch = 1;
               psEnc->timeSinceSwitchAllowed_ms = 0;
            } else {
               psEnc->allowBandwidthSwitch = 0;
               psEnc->timeSinceSwitchAllowed_ms += encControl->payloadSize_ms;
            }
         }
         if( nSamplesIn == 0 ) {
            break;
         }
      } else {
         break;
      }
      curr_block++;
   }
   psEnc->nPrevChannelsInternal = encControl->nChannelsInternal;
   encControl->allowBandwidthSwitch = psEnc->allowBandwidthSwitch;
   encControl->inWBmodeWithoutVariableLP = psEnc->state_Fxx[ 0 ].sCmn.fs_kHz == 16 && psEnc->state_Fxx[ 0 ].sCmn.sLP.mode == 0;
   encControl->internalSampleRate = silk_SMULBB( psEnc->state_Fxx[ 0 ].sCmn.fs_kHz, 1000 );
   encControl->stereoWidth_Q14 = encControl->toMono ? 0 : psEnc->sStereo.smth_width_Q14;
   if( prefillFlag ) {
      encControl->payloadSize_ms = tmp_payloadSize_ms;
      encControl->complexity = tmp_complexity;
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
         psEnc->state_Fxx[ n ].sCmn.prefillFlag = 0;
      }
   }
   encControl->signalType = psEnc->state_Fxx[0].sCmn.indices.signalType;
   encControl->offset = fl_Quantization_Offsets_Q10
                  [ psEnc->state_Fxx[0].sCmn.indices.signalType >> 1 ]
                  [ psEnc->state_Fxx[0].sCmn.indices.quantOffsetType ];
   RESTORE_STACK;
   return ret;
}

/* ======== enc_silk_flp.c ======== */
/* ---- SILK encoder: float analysis (silk/float, x87 rounding as in the reference) ---- */
#define SILK_MAX_ORDER_LPC 24

/* silk_inner_product_FLP_avx2: double FMA lanes, scalar tail on the x87 stack. */
static xf silk_inner_product_FLP(const float *data1, const float *data2, int dataSize) {
   int i, l;
   double a1[4] = {0, 0, 0, 0}, a2[4] = {0, 0, 0, 0}, s[4];
   xf result;
   for (i = 0; i < dataSize-7; i += 8)
      for (l = 0; l < 4; l++) {
         a1[l] = fma((double)data1[i+l], (double)data2[i+l], a1[l]);
         a2[l] = fma((double)data1[i+4+l], (double)data2[i+4+l], a2[l]);
      }
   for (; i < dataSize-3; i += 4)
      for (l = 0; l < 4; l++) a1[l] = fma((double)data1[i+l], (double)data2[i+l], a1[l]);
   for (l = 0; l < 4; l++) s[l] = a1[l]+a2[l];
   s[0] = s[0]+s[2];
   s[1] = s[1]+s[3];
   result = s[0]+s[1];
   for (; i < dataSize; i++) result += XF(data1[i])*data2[i];
   return result;
}
static xf silk_energy_FLP(const float *data, int dataSize) {
   int i;
   xf result = 0;
   for (i = 0; i < dataSize-3; i += 4)
      result += ((XF(data[i])*data[i]+XF(data[i+1])*data[i+1])+XF(data[i+2])*data[i+2])+XF(data[i+3])*data[i+3];
   for (; i < dataSize; i++) result += XF(data[i])*data[i];
   return result;
}
static void silk_autocorrelation_FLP(float *results, const float *inputData, int inputDataSize, int correlationCount) {
   int i;
   if (correlationCount > inputDataSize) correlationCount = inputDataSize;
   for (i = 0; i < correlationCount; i++)
      results[i] = (float)silk_inner_product_FLP(inputData, inputData+i, inputDataSize-i);
}
static void silk_scale_vector_FLP(float *data1, float gain, int dataSize) {
   int i;
   for (i = 0; i < dataSize; i++) data1[i] *= gain;
}
static void silk_scale_copy_vector_FLP(float *data_out, const float *data_in, float gain, int dataSize) {
   int i;
   for (i = 0; i < dataSize; i++) data_out[i] = gain*data_in[i];
}
static void silk_bwexpander_FLP(float *ar, const int d, const float chirp) {
   int i;
   xf cfac = chirp;
   for (i = 0; i < d-1; i++) {
      ar[i] = (float)(ar[i]*cfac);
      cfac *= chirp;
   }
   ar[d-1] = (float)(ar[d-1]*cfac);
}
static void silk_k2a_FLP(float *A, const float *rc, fl_i32 order) {
   int k, n;
   for (k = 0; k < order; k++) {
      float rck = rc[k];
      for (n = 0; n < (k+1)>>1; n++) {
         float tmp1 = A[n], tmp2 = A[k-n-1];
         A[n] = (float)(tmp1+XF(tmp2)*rck);
         A[k-n-1] = (float)(tmp2+XF(tmp1)*rck);
      }
      A[k] = -rck;
   }
}
static float silk_schur_FLP(float refl_coef[], const float auto_corr[], int order) {
   int k, n;
   double C[SILK_MAX_ORDER_LPC+1][2];
   k = 0;
   do C[k][0] = C[k][1] = auto_corr[k];
   while (++k <= order);
   for (k = 0; k < order; k++) {
      xf rc_tmp = -XF(C[k+1][0])/(C[0][1] > 1e-9f ? XF(C[0][1]) : XF(1e-9f));
      refl_coef[k] = (float)rc_tmp;
      for (n = 0; n < order-k; n++) {
         double Ctmp1 = C[n+k+1][0], Ctmp2 = C[n][1];
         C[n+k+1][0] = (double)(Ctmp1+Ctmp2*rc_tmp);
         C[n][1] = (double)(Ctmp2+Ctmp1*rc_tmp);
      }
   }
   return (float)C[0][1];
}
static void silk_apply_sine_window_FLP(float px_win[], const float px[], const int win_type, const int length) {
   int k;
   xf freq, c, S0, S1;
   freq = XF(3.1415926536f)/(length+1);
   c = 2.0f-freq*freq;
   if (win_type < 2) {
      S0 = 0;
      S1 = freq;
   } else {
      S0 = 1;
      S1 = XF(.5f)*c;
   }
   for (k = 0; k < length; k += 4) {
      px_win[k] = (float)((XF(px[k])*.5f)*(S0+S1));
      px_win[k+1] = (float)(XF(px[k+1])*S1);
      S0 = c*S1-S0;
      px_win[k+2] = (float)((XF(px[k+2])*.5f)*(S1+S0));
      px_win[k+3] = (float)(XF(px[k+3])*S0);
      S1 = c*S0-S1;
   }
}
static void silk_warped_autocorrelation_FLP(float *corr, const float *input, const float warping, const int length,
      const int order) {
   int n, i;
   xf tmp1, tmp2;
   double state[25] = {0}, C[25] = {0};
   for (n = 0; n < length; n++) {
      tmp1 = input[n];
      for (i = 0; i < order; i += 2) {
         tmp2 = (state[i]+XF(warping)*state[i+1])-XF(warping)*tmp1;
         state[i] = (double)tmp1;
         C[i] = (double)(C[i]+state[0]*tmp1);
         tmp1 = (state[i+1]+XF(warping)*state[i+2])-XF(warping)*tmp2;
         state[i+1] = (double)tmp2;
         C[i+1] = (double)(C[i+1]+state[0]*tmp2);
      }
      state[order] = (double)tmp1;
      C[order] = (double)(C[order]+tmp1*state[0]);
   }
   for (i = 0; i < order+1; i++) corr[i] = (float)C[i];
}
static float silk_LPC_inverse_pred_gain_FLP(const float *A, fl_i32 order) {
   int k, n;
   xf invGain = 1, rc, rc_mult1, rc_mult2;
   float Atmp[SILK_MAX_ORDER_LPC];
   memcpy(Atmp, A, order*sizeof(float));
   for (k = order-1; k > 0; k--) {
      rc = -XF(Atmp[k]);
      rc_mult1 = 1-rc*rc;
      invGain *= rc_mult1;
      if (invGain*1e4f < 1) return 0;
      rc_mult2 = 1/rc_mult1;
      for (n = 0; n < (k+1)>>1; n++) {
         float tmp1 = Atmp[n], tmp2 = Atmp[k-n-1];
         Atmp[n] = (float)((tmp1-tmp2*rc)*rc_mult2);
         Atmp[k-n-1] = (float)((tmp2-tmp1*rc)*rc_mult2);
      }
   }
   rc = -XF(Atmp[0]);
   rc_mult1 = 1-rc*rc;
   invGain *= rc_mult1;
   if (invGain*1e4f < 1) return 0;
   return (float)invGain;
}
static void silk_insertion_sort_decreasing_FLP(float *a, int *idx, const int L, const int K) {
   float value;
   int i, j;
   for (i = 0; i < K; i++) idx[i] = i;
   for (i = 1; i < K; i++) {
      value = a[i];
      for (j = i-1; j >= 0 && value > a[j]; j--) {
         a[j+1] = a[j];
         idx[j+1] = idx[j];
      }
      a[j+1] = value;
      idx[j+1] = i;
   }
   for (i = K; i < L; i++) {
      value = a[i];
      if (value > a[K-1]) {
         for (j = K-2; j >= 0 && value > a[j]; j--) {
            a[j+1] = a[j];
            idx[j+1] = idx[j];
         }
         a[j+1] = value;
         idx[j+1] = i;
      }
   }
}
static void silk_regularize_correlations_FLP(float *XX, float *xx, const float noise, const int D) {
   int i;
   for (i = 0; i < D; i++) XX[i*D+i] += noise;
   xx[0] += noise;
}
static void silk_corrVector_FLP(const float *x, const float *t, const int L, const int Order, float *Xt) {
   int lag;
   const float *ptr1 = &x[Order-1];
   for (lag = 0; lag < Order; lag++) {
      Xt[lag] = (float)silk_inner_product_FLP(ptr1, t, L);
      ptr1--;
   }
}
static void silk_corrMatrix_FLP(const float *x, const int L, const int Order, float *XX) {
   int j, lag;
   xf energy;
   const float *ptr1 = &x[Order-1], *ptr2;
   energy = silk_energy_FLP(ptr1, L);
   XX[0] = (float)energy;
   for (j = 1; j < Order; j++) {
      energy += XF(ptr1[-j])*ptr1[-j]-XF(ptr1[L-j])*ptr1[L-j];
      XX[j*Order+j] = (float)energy;
   }
   ptr2 = &x[Order-2];
   for (lag = 1; lag < Order; lag++) {
      energy = silk_inner_product_FLP(ptr1, ptr2, L);
      XX[lag*Order] = XX[lag] = (float)energy;
      for (j = 1; j < Order-lag; j++) {
         energy += XF(ptr1[-j])*ptr2[-j]-XF(ptr1[L-j])*ptr2[L-j];
         XX[(lag+j)*Order+j] = XX[j*Order+lag+j] = (float)energy;
      }
      ptr2--;
   }
}
static xf silk_residual_energy_covar_FLP(const float *c, float *wXX, const float *wXx, const float wxx, const int D) {
   int i, j, k;
   xf tmp, nrg = 0, regularization;
   regularization = (XF(wXX[D*D-1])+wXX[0])*1e-8f;
   for (k = 0; k < 10; k++) {
      nrg = wxx;
      tmp = 0;
      for (i = 0; i < D; i++) tmp += XF(wXx[i])*c[i];
      nrg -= tmp*2;
      for (i = 0; i < D; i++) {
         tmp = 0;
         for (j = i+1; j < D; j++) tmp += XF(wXX[i*D+j])*c[j];
         nrg += c[i]*(tmp*2+XF(wXX[i*D+i])*c[i]);
      }
      if (nrg > 0) break;
      for (i = 0; i < D; i++) wXX[i*D+i] = (float)(wXX[i*D+i]+regularization);
      regularization *= 2;
   }
   if (k == 10) nrg = 1;
   return nrg;
}
static void silk_LTP_analysis_filter_FLP(float *LTP_res, const float *x, const float B[], const int pitchL[],
      const float invGains[], const int subfr_length, const int nb_subfr, const int pre_length) {
   const float *x_ptr = x, *x_lag_ptr;
   float *LTP_res_ptr = LTP_res;
   int k, i, j;
   for (k = 0; k < nb_subfr; k++) {
      float inv_gain = invGains[k];
      x_lag_ptr = x_ptr-pitchL[k];
      for (i = 0; i < subfr_length+pre_length; i++) {
         xf v = x_ptr[i];
         for (j = 0; j < LTP_ORDER; j++) v -= XF(B[k*LTP_ORDER+j])*x_lag_ptr[LTP_ORDER/2-j];
         LTP_res_ptr[i] = (float)(v*inv_gain);
         x_lag_ptr++;
      }
      LTP_res_ptr += subfr_length+pre_length;
      x_ptr += subfr_length;
   }
}
static void silk_find_LTP_FLP(float XX[], float xX[], const float r_ptr[], const int lag[], const int subfr_length,
      const int nb_subfr) {
   int k;
   float *xX_ptr = xX, *XX_ptr = XX, xx, temp;
   for (k = 0; k < nb_subfr; k++) {
      const float *lag_ptr = r_ptr-(lag[k]+LTP_ORDER/2);
      xf m;
      silk_corrMatrix_FLP(lag_ptr, subfr_length, LTP_ORDER, XX_ptr);
      silk_corrVector_FLP(lag_ptr, r_ptr, subfr_length, LTP_ORDER, xX_ptr);
      xx = (float)silk_energy_FLP(r_ptr, subfr_length+LTP_ORDER);
      m = (XF(XX_ptr[0])+XX_ptr[24])*0.015f+1;
      temp = (float)(1/(xx > m ? XF(xx) : m));
      silk_scale_vector_FLP(XX_ptr, temp, LTP_ORDER*LTP_ORDER);
      silk_scale_vector_FLP(xX_ptr, temp, LTP_ORDER);
      r_ptr += subfr_length;
      XX_ptr += LTP_ORDER*LTP_ORDER;
      xX_ptr += LTP_ORDER;
   }
}
static float silk_burg_modified_FLP(float A[], const float x[], const float minInvGain, const int subfr_length,
      const int nb_subfr, const int D) {
   int k, n, s, reached_max_gain;
   double C0, C_first_row[SILK_MAX_ORDER_LPC], C_last_row[SILK_MAX_ORDER_LPC];
   double CAf[SILK_MAX_ORDER_LPC+1], CAb[SILK_MAX_ORDER_LPC+1], Af[SILK_MAX_ORDER_LPC];
   xf invGain, num, nrg_f, nrg_b, rc, tmp1, tmp2;
   const float *x_ptr;
   C0 = (double)silk_energy_FLP(x, nb_subfr*subfr_length);
   memset(C_first_row, 0, sizeof(C_first_row));
   for (s = 0; s < nb_subfr; s++) {
      x_ptr = x+s*subfr_length;
      for (n = 1; n < D+1; n++)
         C_first_row[n-1] = (double)(C_first_row[n-1]+silk_inner_product_FLP(x_ptr, x_ptr+n, subfr_length-n));
   }
   memcpy(C_last_row, C_first_row, sizeof(C_first_row));
   CAb[0] = CAf[0] = (double)((C0+XF(C0)*1e-5f)+1e-9f);
   invGain = 1;
   reached_max_gain = 0;
   for (n = 0; n < D; n++) {
      for (s = 0; s < nb_subfr; s++) {
         x_ptr = x+s*subfr_length;
         tmp1 = x_ptr[n];
         tmp2 = x_ptr[subfr_length-n-1];
         for (k = 0; k < n; k++) {
            C_first_row[k] = (double)(C_first_row[k]-XF(x_ptr[n-k-1])*x_ptr[n]);
            C_last_row[k] = (double)(C_last_row[k]-XF(x_ptr[subfr_length-n+k])*x_ptr[subfr_length-n-1]);
            tmp1 += XF(x_ptr[n-k-1])*Af[k];
            tmp2 += XF(x_ptr[subfr_length-n+k])*Af[k];
         }
         for (k = 0; k <= n; k++) {
            CAf[k] = (double)(CAf[k]-tmp1*x_ptr[n-k]);
            CAb[k] = (double)(CAb[k]-tmp2*x_ptr[subfr_length-n+k-1]);
         }
      }
      tmp1 = C_first_row[n];
      tmp2 = C_last_row[n];
      for (k = 0; k < n; k++) {
         tmp1 += XF(C_last_row[n-k-1])*Af[k];
         tmp2 += XF(C_first_row[n-k-1])*Af[k];
      }
      CAf[n+1] = (double)tmp1;
      CAb[n+1] = (double)tmp2;
      num = tmp2;
      nrg_b = CAb[0];
      nrg_f = CAf[0];
      for (k = 0; k < n; k++) {
         num += XF(CAb[n-k])*Af[k];
         nrg_b += XF(CAb[k+1])*Af[k];
         nrg_f += XF(CAf[k+1])*Af[k];
      }
      rc = (num*-2.0f)/(nrg_f+nrg_b);
      tmp1 = invGain*(1-rc*rc);
      if (tmp1 <= minInvGain) {
         double num_d = (double)num;
         rc = fl_m_sqrt((double)(1-minInvGain/invGain));
         if (num_d > 0) rc = -rc;
         invGain = minInvGain;
         reached_max_gain = 1;
      } else invGain = tmp1;
      for (k = 0; k < (n+1)>>1; k++) {
         double a1 = Af[k], a2 = Af[n-k-1];
         Af[k] = (double)(a1+rc*a2);
         Af[n-k-1] = (double)(a2+rc*a1);
      }
      Af[n] = (double)rc;
      if (reached_max_gain) {
         for (k = n+1; k < D; k++) Af[k] = 0;
         break;
      }
      for (k = 0; k <= n+1; k++) {
         double t = CAf[k];
         CAf[k] = (double)(t+rc*CAb[n-k+1]);
         CAb[n-k+1] = (double)(CAb[n-k+1]+rc*t);
      }
   }
   if (reached_max_gain) {
      double ig = (double)invGain;
      for (k = 0; k < D; k++) A[k] = -(float)Af[k];
      for (s = 0; s < nb_subfr; s++) C0 = (double)(C0-silk_energy_FLP(x+s*subfr_length, D));
      nrg_f = XF(C0)*ig;
   } else {
      nrg_f = CAf[0];
      tmp1 = 1;
      for (k = 0; k < D; k++) {
         double Atmp = Af[k];
         nrg_f += XF(CAf[k+1])*Atmp;
         tmp1 += XF(Atmp)*Atmp;
         A[k] = -(float)Atmp;
      }
      nrg_f -= (XF(C0)*1e-5f)*tmp1;
   }
   return (float)nrg_f;
}

/* ======== enc_silk_flp2.c ======== */
/* ---- SILK encoder: float analysis, part 2 ---- */

/* MinGW log2l/exp2l/pow (x87). */
static xf fl_m_log2l(xf x) {
   xf r, xm1 = x-1.0L;
#ifdef FLAPUS_PORTABLE_MATH
   r = fabsl(xm1) > XF(0.29) ? fl_p_log2(x) : fl_p_ln1p(xm1)*FL_P_LOG2E;
#else
   if (fabsl(xm1) > XF(0.29)) __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   else __asm__("fld1\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"(xm1) : "st(1)");
#endif
   return r;
}
static xf fl_m_exp2l(xf x) {
   xf r, i;
   unsigned short cw0, cw1;
   __asm__ volatile("fnstcw %0" : "=m"(cw0));
   cw1 = cw0|0x0c00;
   __asm__ volatile("fldcw %0" : : "m"(cw1));
   __asm__ volatile("frndint" : "=t"(i) : "0"(x));
   __asm__ volatile("fldcw %0" : : "m"(cw0));
   x -= i;
#ifdef FLAPUS_PORTABLE_MATH
   x = fl_p_f2xm1(x);
#else
   __asm__("f2xm1" : "+t"(x));
#endif
   x += 1;
   __asm__("fscale" : "=t"(r) : "0"(x), "u"(i));
   return r;
}
/* pow(x, y) for x > 0 and finite y (the cases used by the SILK encoder). */
static xf fl_m_pow(double x, double y) {
   double ip;
   if (y == 0 || x == 1) return 1;
   ip = trunc(y);
   if (y == ip && ip <= 2147483647.0 && ip >= -2147483648.0) {
      int n = (int)ip;
      unsigned u;
      xf d = fabs(x), rslt;
      if (n < 0) {
         d = 1/d;
         n = -n;
      }
      u = (unsigned)n;
      rslt = (u&1) ? d : XF(1);
      u >>= 1;
      while (u > 0) {
         d *= d;
         if (u&1) rslt *= d;
         u >>= 1;
      }
      return rslt;
   }
   if (y == 0.5) return fl_x87_sqrt(XF(x));
   return (double)fl_m_exp2l(XF(y)*fl_m_log2l(fabsl(XF(x))));
}
static float silk_log2(double x) { return (float)(XF(3.32192809488736)*fl_m_log10(x)); }
/* 1/(1+exp(negx)) as the reference computes silk_sigmoid(-negx). */
static float silk_sigmoid_neg(xf negx) { return (float)(1.0L/(1.0L+fl_m_exp((double)negx))); }

static void silk_LPC_analysis_filter_FLP(float r_LPC[], const float PredCoef[], const float s[], const int length,
      const int Order) {
   int ix, j;
   for (ix = Order; ix < length; ix++) {
      const float *s_ptr = &s[ix-1];
      xf pred = XF(s_ptr[0])*PredCoef[0];
      for (j = 1; j < Order; j++) pred += XF(s_ptr[-j])*PredCoef[j];
      r_LPC[ix] = (float)(s_ptr[1]-pred);
   }
   memset(r_LPC, 0, Order*sizeof(float));
}

static void silk_A2NLSF_FLP(fl_i16 *NLSF_Q15, const float *pAR, const int LPC_order) {
   int i;
   fl_i32 a_fix_Q16[MAX_LPC_ORDER];
   for (i = 0; i < LPC_order; i++) a_fix_Q16[i] = (fl_i32)lrintf(pAR[i]*65536.0f);
   silk_A2NLSF(NLSF_Q15, a_fix_Q16, LPC_order);
}
static void silk_NLSF2A_FLP(float *pAR, const fl_i16 *NLSF_Q15, const int LPC_order) {
   int i;
   fl_i16 a_fix_Q12[MAX_LPC_ORDER];
   silk_NLSF2A(a_fix_Q12, NLSF_Q15, LPC_order, 0);
   for (i = 0; i < LPC_order; i++) pAR[i] = (float)a_fix_Q12[i]*(1.0f/4096.0f);
}
static void silk_process_NLSFs_FLP(silk_encoder_state *psEncC, float PredCoef[2][MAX_LPC_ORDER], fl_i16 NLSF_Q15[],
      const fl_i16 prev_NLSF_Q15[]) {
   int i, j;
   fl_i16 PredCoef_Q12[2][MAX_LPC_ORDER];
   silk_process_NLSFs(psEncC, PredCoef_Q12, NLSF_Q15, prev_NLSF_Q15);
   for (j = 0; j < 2; j++)
      for (i = 0; i < psEncC->predictLPCOrder; i++) PredCoef[j][i] = (float)PredCoef_Q12[j][i]*(1.0f/4096.0f);
}
static void silk_quant_LTP_gains_FLP(float B[], fl_i8 cbk_index[], fl_i8 *periodicity_index, fl_i32 *sum_log_gain_Q7,
      float *pred_gain_dB, const float XX[], const float xX[], const int subfr_len, const int nb_subfr) {
   int i, pred_gain_dB_Q7;
   fl_i16 B_Q14[MAX_NB_SUBFR*LTP_ORDER];
   fl_i32 XX_Q17[MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER], xX_Q17[MAX_NB_SUBFR*LTP_ORDER];
   for (i = 0; i < nb_subfr*LTP_ORDER*LTP_ORDER; i++) XX_Q17[i] = (fl_i32)lrintf(XX[i]*131072.0f);
   for (i = 0; i < nb_subfr*LTP_ORDER; i++) xX_Q17[i] = (fl_i32)lrintf(xX[i]*131072.0f);
   silk_quant_LTP_gains(B_Q14, cbk_index, periodicity_index, sum_log_gain_Q7, &pred_gain_dB_Q7, XX_Q17, xX_Q17,
         subfr_len, nb_subfr, 0);
   for (i = 0; i < nb_subfr*LTP_ORDER; i++) B[i] = (float)B_Q14[i]*(1.0f/16384.0f);
   *pred_gain_dB = (float)pred_gain_dB_Q7*(1.0f/128.0f);
}
static void silk_NSQ_wrapper_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      SideInfoIndices *psIndices, silk_nsq_state *psNSQ, fl_i8 pulses[], const float x[]) {
   int i, j, LTP_scale_Q14, Lambda_Q10, Tilt_Q14[MAX_NB_SUBFR], HarmShapeGain_Q14[MAX_NB_SUBFR];
   fl_i16 x16[MAX_FRAME_LENGTH], PredCoef_Q12[2][MAX_LPC_ORDER], LTPCoef_Q14[LTP_ORDER*MAX_NB_SUBFR];
   fl_i16 AR_Q13[MAX_NB_SUBFR*MAX_SHAPE_LPC_ORDER];
   fl_i32 Gains_Q16[MAX_NB_SUBFR], LF_shp_Q14[MAX_NB_SUBFR];
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++)
      for (j = 0; j < psEnc->sCmn.shapingLPCOrder; j++)
         AR_Q13[i*MAX_SHAPE_LPC_ORDER+j] = (fl_i16)lrintf(psEncCtrl->AR[i*MAX_SHAPE_LPC_ORDER+j]*8192.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
      LF_shp_Q14[i] = silk_LSHIFT32((fl_i32)lrintf(psEncCtrl->LF_AR_shp[i]*16384.0f), 16)|
            (fl_u16)lrintf(psEncCtrl->LF_MA_shp[i]*16384.0f);
      Tilt_Q14[i] = (int)lrintf(psEncCtrl->Tilt[i]*16384.0f);
      HarmShapeGain_Q14[i] = (int)lrintf(psEncCtrl->HarmShapeGain[i]*16384.0f);
   }
   Lambda_Q10 = (int)lrintf(psEncCtrl->Lambda*1024.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr*LTP_ORDER; i++) LTPCoef_Q14[i] = (fl_i16)lrintf(psEncCtrl->LTPCoef[i]*16384.0f);
   for (j = 0; j < 2; j++)
      for (i = 0; i < psEnc->sCmn.predictLPCOrder; i++)
         PredCoef_Q12[j][i] = (fl_i16)lrintf(psEncCtrl->PredCoef[j][i]*4096.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) Gains_Q16[i] = (fl_i32)lrintf(psEncCtrl->Gains[i]*65536.0f);
   LTP_scale_Q14 = psIndices->signalType == TYPE_VOICED ? fl_LTPScales_table_Q14[psIndices->LTP_scaleIndex] : 0;
   for (i = 0; i < psEnc->sCmn.frame_length; i++) x16[i] = (fl_i16)lrintf(x[i]);
   if (psEnc->sCmn.nStatesDelayedDecision > 1 || psEnc->sCmn.warping_Q16 > 0)
      silk_NSQ_del_dec_c(&psEnc->sCmn, psNSQ, psIndices, x16, pulses, PredCoef_Q12[0], LTPCoef_Q14, AR_Q13,
            HarmShapeGain_Q14, Tilt_Q14, LF_shp_Q14, Gains_Q16, psEncCtrl->pitchL, Lambda_Q10, LTP_scale_Q14);
   else
      silk_NSQ_c(&psEnc->sCmn, psNSQ, psIndices, x16, pulses, PredCoef_Q12[0], LTPCoef_Q14, AR_Q13,
            HarmShapeGain_Q14, Tilt_Q14, LF_shp_Q14, Gains_Q16, psEncCtrl->pitchL, Lambda_Q10, LTP_scale_Q14);
}

static void silk_residual_energy_FLP(float nrgs[], const float x[], float a[2][MAX_LPC_ORDER], const float gains[],
      const int subfr_length, const int nb_subfr, const int LPC_order) {
   int shift = LPC_order+subfr_length;
   float LPC_res[(MAX_FRAME_LENGTH+MAX_NB_SUBFR*MAX_LPC_ORDER)/2], *LPC_res_ptr = LPC_res+LPC_order;
   silk_LPC_analysis_filter_FLP(LPC_res, a[0], x, 2*shift, LPC_order);
   nrgs[0] = (float)(silk_energy_FLP(LPC_res_ptr, subfr_length)*(double)(gains[0]*gains[0]));
   nrgs[1] = (float)(silk_energy_FLP(LPC_res_ptr+shift, subfr_length)*(double)(gains[1]*gains[1]));
   if (nb_subfr == MAX_NB_SUBFR) {
      silk_LPC_analysis_filter_FLP(LPC_res, a[1], x+2*shift, 2*shift, LPC_order);
      nrgs[2] = (float)(silk_energy_FLP(LPC_res_ptr, subfr_length)*(double)(gains[2]*gains[2]));
      nrgs[3] = (float)(silk_energy_FLP(LPC_res_ptr+shift, subfr_length)*(double)(gains[3]*gains[3]));
   }
}

static void silk_LTP_scale_ctrl_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, int condCoding) {
   if (condCoding == CODE_INDEPENDENTLY) {
      int round_loss = psEnc->sCmn.PacketLoss_perc*psEnc->sCmn.nFramesPerPacket;
      if (psEnc->sCmn.LBRR_flag) round_loss = 2+silk_SMULBB(round_loss, round_loss)/100;
      psEnc->sCmn.indices.LTP_scaleIndex = silk_SMULBB((int)psEncCtrl->LTPredCodGain, round_loss) > silk_log2lin(2900-psEnc->sCmn.SNR_dB_Q7);
      psEnc->sCmn.indices.LTP_scaleIndex += silk_SMULBB((int)psEncCtrl->LTPredCodGain, round_loss) > silk_log2lin(3900-psEnc->sCmn.SNR_dB_Q7);
   } else psEnc->sCmn.indices.LTP_scaleIndex = 0;
   psEncCtrl->LTP_scale = (float)fl_LTPScales_table_Q14[psEnc->sCmn.indices.LTP_scaleIndex]*(1.0f/16384.0f);
}

static void silk_find_LPC_FLP(silk_encoder_state *psEncC, fl_i16 NLSF_Q15[], const float x[], const float minInvGain) {
   int k, subfr_length;
   float a[MAX_LPC_ORDER], res_nrg, res_nrg_2nd, res_nrg_interp, a_tmp[MAX_LPC_ORDER];
   float LPC_res[MAX_FRAME_LENGTH+MAX_NB_SUBFR*MAX_LPC_ORDER];
   fl_i16 NLSF0_Q15[MAX_LPC_ORDER];
   subfr_length = psEncC->subfr_length+psEncC->predictLPCOrder;
   psEncC->indices.NLSFInterpCoef_Q2 = 4;
   res_nrg = silk_burg_modified_FLP(a, x, minInvGain, subfr_length, psEncC->nb_subfr, psEncC->predictLPCOrder);
   if (psEncC->useInterpolatedNLSFs && !psEncC->first_frame_after_reset && psEncC->nb_subfr == MAX_NB_SUBFR) {
      res_nrg = (float)(res_nrg-XF(silk_burg_modified_FLP(a_tmp, x+(MAX_NB_SUBFR/2)*subfr_length, minInvGain,
            subfr_length, MAX_NB_SUBFR/2, psEncC->predictLPCOrder)));
      silk_A2NLSF_FLP(NLSF_Q15, a_tmp, psEncC->predictLPCOrder);
      res_nrg_2nd = 3.40282347e+38f;
      for (k = 3; k >= 0; k--) {
         double e1;
         silk_interpolate(NLSF0_Q15, psEncC->prev_NLSFq_Q15, NLSF_Q15, k, psEncC->predictLPCOrder);
         silk_NLSF2A_FLP(a_tmp, NLSF0_Q15, psEncC->predictLPCOrder);
         silk_LPC_analysis_filter_FLP(LPC_res, a_tmp, x, 2*subfr_length, psEncC->predictLPCOrder);
         e1 = (double)silk_energy_FLP(LPC_res+psEncC->predictLPCOrder, subfr_length-psEncC->predictLPCOrder);
         res_nrg_interp = (float)(silk_energy_FLP(LPC_res+psEncC->predictLPCOrder+subfr_length,
               subfr_length-psEncC->predictLPCOrder)+e1);
         if (res_nrg_interp < res_nrg) {
            res_nrg = res_nrg_interp;
            psEncC->indices.NLSFInterpCoef_Q2 = (fl_i8)k;
         } else if (res_nrg_interp > res_nrg_2nd) break;
         res_nrg_2nd = res_nrg_interp;
      }
   }
   if (psEncC->indices.NLSFInterpCoef_Q2 == 4) silk_A2NLSF_FLP(NLSF_Q15, a, psEncC->predictLPCOrder);
}

static void silk_find_pred_coefs_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float res_pitch[], const float x[], int condCoding) {
   int i;
   float XXLTP[MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER], xXLTP[MAX_NB_SUBFR*LTP_ORDER], invGains[MAX_NB_SUBFR], minInvGain;
   float LPC_in_pre[MAX_NB_SUBFR*MAX_LPC_ORDER+MAX_FRAME_LENGTH];
   fl_i16 NLSF_Q15[MAX_LPC_ORDER] = {0};
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) invGains[i] = 1.0f/psEncCtrl->Gains[i];
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      silk_find_LTP_FLP(XXLTP, xXLTP, res_pitch, psEncCtrl->pitchL, psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr);
      silk_quant_LTP_gains_FLP(psEncCtrl->LTPCoef, psEnc->sCmn.indices.LTPIndex, &psEnc->sCmn.indices.PERIndex,
            &psEnc->sCmn.sum_log_gain_Q7, &psEncCtrl->LTPredCodGain, XXLTP, xXLTP, psEnc->sCmn.subfr_length,
            psEnc->sCmn.nb_subfr);
      silk_LTP_scale_ctrl_FLP(psEnc, psEncCtrl, condCoding);
      silk_LTP_analysis_filter_FLP(LPC_in_pre, x-psEnc->sCmn.predictLPCOrder, psEncCtrl->LTPCoef, psEncCtrl->pitchL,
            invGains, psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr, psEnc->sCmn.predictLPCOrder);
   } else {
      const float *x_ptr = x-psEnc->sCmn.predictLPCOrder;
      float *x_pre_ptr = LPC_in_pre;
      for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
         silk_scale_copy_vector_FLP(x_pre_ptr, x_ptr, invGains[i], psEnc->sCmn.subfr_length+psEnc->sCmn.predictLPCOrder);
         x_pre_ptr += psEnc->sCmn.subfr_length+psEnc->sCmn.predictLPCOrder;
         x_ptr += psEnc->sCmn.subfr_length;
      }
      memset(psEncCtrl->LTPCoef, 0, psEnc->sCmn.nb_subfr*LTP_ORDER*sizeof(float));
      psEncCtrl->LTPredCodGain = 0;
      psEnc->sCmn.sum_log_gain_Q7 = 0;
   }
   if (psEnc->sCmn.first_frame_after_reset) minInvGain = 1.0f/1e2f;
   else {
      xf m = XF((float)fl_m_pow(2, (double)(XF(psEncCtrl->LTPredCodGain)/3)))/1e4f;
      minInvGain = (float)(m/(XF(.75f)*psEncCtrl->coding_quality+.25f));
   }
   silk_find_LPC_FLP(&psEnc->sCmn, NLSF_Q15, LPC_in_pre, minInvGain);
   silk_process_NLSFs_FLP(&psEnc->sCmn, psEncCtrl->PredCoef, NLSF_Q15, psEnc->sCmn.prev_NLSFq_Q15);
   silk_residual_energy_FLP(psEncCtrl->ResNrg, LPC_in_pre, psEncCtrl->PredCoef, psEncCtrl->Gains,
         psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr, psEnc->sCmn.predictLPCOrder);
   memcpy(psEnc->sCmn.prev_NLSFq_Q15, NLSF_Q15, sizeof(psEnc->sCmn.prev_NLSFq_Q15));
}

static void silk_process_gains_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, int condCoding) {
   silk_shape_state_FLP *psShapeSt = &psEnc->sShape;
   int k;
   fl_i32 pGains_Q16[MAX_NB_SUBFR];
   float InvMaxSqrVal;
   xf quant_offset, lambda;
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      float sig = silk_sigmoid_neg((XF(psEncCtrl->LTPredCodGain)-12.0f)*-.25f);
      xf s = 1-XF(.5f)*sig;
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(psEncCtrl->Gains[k]*s);
   }
   InvMaxSqrVal = (float)(fl_m_pow(2.0, (double)((21.0f-XF(1/128.0f)*psEnc->sCmn.SNR_dB_Q7)*.33f))/psEnc->sCmn.subfr_length);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      float gain = psEncCtrl->Gains[k];
      xf g = fl_m_sqrtf((float)(XF(gain)*gain+XF(psEncCtrl->ResNrg[k])*InvMaxSqrVal));
      psEncCtrl->Gains[k] = (float)(g < 32767.0f ? g : XF(32767.0f));
   }
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) pGains_Q16[k] = (fl_i32)(psEncCtrl->Gains[k]*65536.0f);
   memcpy(psEncCtrl->GainsUnq_Q16, pGains_Q16, psEnc->sCmn.nb_subfr*sizeof(fl_i32));
   psEncCtrl->lastGainIndexPrev = psShapeSt->LastGainIndex;
   silk_gains_quant(psEnc->sCmn.indices.GainsIndices, pGains_Q16, &psShapeSt->LastGainIndex,
         condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(XF(1.0f/65536.0f)*pGains_Q16[k]);
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      if (XF(1.0f/32768.0f)*psEnc->sCmn.input_tilt_Q15+psEncCtrl->LTPredCodGain > 1.0f) psEnc->sCmn.indices.quantOffsetType = 0;
      else psEnc->sCmn.indices.quantOffsetType = 1;
   }
   quant_offset = XF(1.0f/1024.0f)*fl_Quantization_Offsets_Q10[psEnc->sCmn.indices.signalType>>1][psEnc->sCmn.indices.quantOffsetType];
   lambda = XF(-0.05f)*psEnc->sCmn.nStatesDelayedDecision+1.2f;
   lambda += (XF(-0.2f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
   lambda += XF(-0.1f)*psEncCtrl->input_quality;
   lambda += XF(-0.2f)*psEncCtrl->coding_quality;
   lambda += XF(0.8f)*quant_offset;
   psEncCtrl->Lambda = (float)lambda;
}

static void silk_find_pitch_lags_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, float res[],
      const float x[]);
static void silk_noise_shape_analysis_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float *pitch_res, const float *x);

/* ======== enc_silk_flp3.c ======== */
/* ---- SILK encoder: pitch analysis, noise shaping and frame encoding (float, x87 rounding) ---- */
#define PE_SCRATCH_SIZE 22

static void silk_P_Ana_calc_corr_st3(float cross_corr_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS],
      const float frame[], int start_lag, int sf_length, int nb_subfr, int complexity) {
   const float *target_ptr;
   int i, j, k, lag_counter, lag_low, lag_high, nb_cbk_search, delta, idx, cbk_size;
   float scratch_mem[PE_SCRATCH_SIZE], xcorr[PE_SCRATCH_SIZE];
   const fl_i8 *Lag_range_ptr, *Lag_CB_ptr;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      Lag_range_ptr = &fl_Lag_range_stage3[complexity][0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      nb_cbk_search = fl_nb_cbk_searchs_stage3[complexity];
      cbk_size = PE_NB_CBKS_STAGE3_MAX;
   } else {
      Lag_range_ptr = &fl_Lag_range_stage3_10_ms[0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
      cbk_size = PE_NB_CBKS_STAGE3_10MS;
   }
   target_ptr = &frame[sf_length<<2];
   for (k = 0; k < nb_subfr; k++) {
      lag_counter = 0;
      lag_low = Lag_range_ptr[k*2];
      lag_high = Lag_range_ptr[k*2+1];
      fl_pitch_xcorr(target_ptr, target_ptr-start_lag-lag_high, xcorr, sf_length, lag_high-lag_low+1);
      for (j = lag_low; j <= lag_high; j++) scratch_mem[lag_counter++] = xcorr[lag_high-j];
      delta = Lag_range_ptr[k*2];
      for (i = 0; i < nb_cbk_search; i++) {
         idx = Lag_CB_ptr[k*cbk_size+i]-delta;
         for (j = 0; j < PE_NB_STAGE3_LAGS; j++) cross_corr_st3[k][i][j] = scratch_mem[idx+j];
      }
      target_ptr += sf_length;
   }
}
static void silk_P_Ana_calc_energy_st3(float energies_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS],
      const float frame[], int start_lag, int sf_length, int nb_subfr, int complexity) {
   const float *target_ptr, *basis_ptr;
   xf energy;
   int k, i, j, lag_counter, nb_cbk_search, delta, idx, cbk_size, lag_diff;
   float scratch_mem[PE_SCRATCH_SIZE];
   const fl_i8 *Lag_range_ptr, *Lag_CB_ptr;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      Lag_range_ptr = &fl_Lag_range_stage3[complexity][0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      nb_cbk_search = fl_nb_cbk_searchs_stage3[complexity];
      cbk_size = PE_NB_CBKS_STAGE3_MAX;
   } else {
      Lag_range_ptr = &fl_Lag_range_stage3_10_ms[0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
      cbk_size = PE_NB_CBKS_STAGE3_10MS;
   }
   target_ptr = &frame[sf_length<<2];
   for (k = 0; k < nb_subfr; k++) {
      lag_counter = 0;
      basis_ptr = target_ptr-(start_lag+Lag_range_ptr[k*2]);
      energy = silk_energy_FLP(basis_ptr, sf_length)+1e-3;
      scratch_mem[lag_counter++] = (float)energy;
      lag_diff = Lag_range_ptr[k*2+1]-Lag_range_ptr[k*2]+1;
      for (i = 1; i < lag_diff; i++) {
         energy -= XF(basis_ptr[sf_length-i])*basis_ptr[sf_length-i];
         energy += XF(basis_ptr[-i])*basis_ptr[-i];
         scratch_mem[lag_counter++] = (float)energy;
      }
      delta = Lag_range_ptr[k*2];
      for (i = 0; i < nb_cbk_search; i++) {
         idx = Lag_CB_ptr[k*cbk_size+i]-delta;
         for (j = 0; j < PE_NB_STAGE3_LAGS; j++) energies_st3[k][i][j] = scratch_mem[idx+j];
      }
      target_ptr += sf_length;
   }
}

static int silk_pitch_analysis_core_FLP(const float *frame, int *pitch_out, fl_i16 *lagIndex, fl_i8 *contourIndex,
      float *LTPCorr, int prevLag, const float search_thres1, const float search_thres2, const int Fs_kHz,
      const int complexity, const int nb_subfr) {
   int i, k, d, j;
   float frame_8kHz[PE_MAX_FRAME_LENGTH_MS*8], frame_4kHz[PE_MAX_FRAME_LENGTH_MS*4];
   fl_i16 frame_8_FIX[PE_MAX_FRAME_LENGTH_MS*8], frame_4_FIX[PE_MAX_FRAME_LENGTH_MS*4];
   fl_i32 filt_state[6];
   float C[PE_MAX_NB_SUBFR][(PE_MAX_LAG>>1)+5], xcorr[PE_MAX_LAG_MS*4-PE_MIN_LAG_MS*4+1], CC[PE_NB_CBKS_STAGE2_EXT];
   const float *target_ptr, *basis_ptr;
   int d_srch[PE_D_SRCH_LENGTH], length_d_srch, length_d_comp;
   fl_i16 d_comp[(PE_MAX_LAG>>1)+5];
   float Cmax, CCmax, CCmax_b, CCmax_new, lag_log2, prevLag_log2;
   int CBimax, CBimax_new, lag, start_lag, end_lag, lag_new, cbk_size, nb_cbk_search, lag_counter;
   float energies_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS];
   float cross_corr_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS];
   int frame_length, frame_length_8kHz, frame_length_4kHz, sf_length, sf_length_8kHz, min_lag, min_lag_8kHz;
   int min_lag_4kHz, max_lag, max_lag_8kHz, max_lag_4kHz;
   const fl_i8 *Lag_CB_ptr;
   frame_length = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*Fs_kHz;
   frame_length_4kHz = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*4;
   frame_length_8kHz = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*8;
   sf_length = PE_SUBFR_LENGTH_MS*Fs_kHz;
   sf_length_8kHz = PE_SUBFR_LENGTH_MS*8;
   min_lag = PE_MIN_LAG_MS*Fs_kHz;
   min_lag_4kHz = PE_MIN_LAG_MS*4;
   min_lag_8kHz = PE_MIN_LAG_MS*8;
   max_lag = PE_MAX_LAG_MS*Fs_kHz-1;
   max_lag_4kHz = PE_MAX_LAG_MS*4;
   max_lag_8kHz = PE_MAX_LAG_MS*8-1;
   if (Fs_kHz == 16) {
      fl_i16 frame_16_FIX[16*PE_MAX_FRAME_LENGTH_MS];
      for (k = frame_length-1; k >= 0; k--) frame_16_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
      memset(filt_state, 0, 2*sizeof(fl_i32));
      silk_resampler_down2(filt_state, frame_8_FIX, frame_16_FIX, frame_length);
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8kHz[k] = frame_8_FIX[k];
   } else if (Fs_kHz == 12) {
      fl_i16 frame_12_FIX[12*PE_MAX_FRAME_LENGTH_MS];
      for (k = frame_length-1; k >= 0; k--) frame_12_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
      memset(filt_state, 0, 6*sizeof(fl_i32));
      silk_resampler_down2_3(filt_state, frame_8_FIX, frame_12_FIX, frame_length);
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8kHz[k] = frame_8_FIX[k];
   } else {
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
   }
   memset(filt_state, 0, 2*sizeof(fl_i32));
   silk_resampler_down2(filt_state, frame_4_FIX, frame_8_FIX, frame_length_8kHz);
   for (k = frame_length_4kHz-1; k >= 0; k--) frame_4kHz[k] = frame_4_FIX[k];
   for (i = frame_length_4kHz-1; i > 0; i--) {
      float s = frame_4kHz[i]+frame_4kHz[i-1];
      frame_4kHz[i] = (fl_i16)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
   }
   memset(C, 0, sizeof(float)*nb_subfr*((PE_MAX_LAG>>1)+5));
   target_ptr = &frame_4kHz[sf_length_8kHz/2*4];
   for (k = 0; k < nb_subfr>>1; k++) {
      double cross_corr, e1;
      xf normalizer;
      basis_ptr = target_ptr-min_lag_4kHz;
      fl_pitch_xcorr(target_ptr, target_ptr-max_lag_4kHz, xcorr, sf_length_8kHz, max_lag_4kHz-min_lag_4kHz+1);
      cross_corr = xcorr[max_lag_4kHz-min_lag_4kHz];
      e1 = (double)silk_energy_FLP(target_ptr, sf_length_8kHz);
      normalizer = (silk_energy_FLP(basis_ptr, sf_length_8kHz)+e1)+sf_length_8kHz*4000.0f;
      C[0][min_lag_4kHz] = C[0][min_lag_4kHz]+(float)((XF(cross_corr)*2)/normalizer);
      for (d = min_lag_4kHz+1; d <= max_lag_4kHz; d++) {
         basis_ptr--;
         cross_corr = xcorr[max_lag_4kHz-d];
         normalizer += XF(basis_ptr[0])*basis_ptr[0]-XF(basis_ptr[sf_length_8kHz])*basis_ptr[sf_length_8kHz];
         C[0][d] = C[0][d]+(float)((XF(cross_corr)*2)/normalizer);
      }
      target_ptr += sf_length_8kHz;
   }
   for (i = max_lag_4kHz; i >= min_lag_4kHz; i--) C[0][i] = (float)(C[0][i]-(XF(C[0][i])*i)*(1.0f/4096.0f));
   length_d_srch = 4+2*complexity;
   silk_insertion_sort_decreasing_FLP(&C[0][min_lag_4kHz], d_srch, max_lag_4kHz-min_lag_4kHz+1, length_d_srch);
   Cmax = C[0][min_lag_4kHz];
   if (Cmax < 0.2f) {
      memset(pitch_out, 0, nb_subfr*sizeof(int));
      *LTPCorr = 0;
      *lagIndex = 0;
      *contourIndex = 0;
      return 1;
   }
   {
      xf threshold = XF(Cmax)*search_thres1;
      for (i = 0; i < length_d_srch; i++) {
         if (C[0][min_lag_4kHz+i] > threshold) d_srch[i] = (d_srch[i]+min_lag_4kHz)<<1;
         else {
            length_d_srch = i;
            break;
         }
      }
   }
   for (i = min_lag_8kHz-5; i < max_lag_8kHz+5; i++) d_comp[i] = 0;
   for (i = 0; i < length_d_srch; i++) d_comp[d_srch[i]] = 1;
   for (i = max_lag_8kHz+3; i >= min_lag_8kHz; i--) d_comp[i] += d_comp[i-1]+d_comp[i-2];
   length_d_srch = 0;
   for (i = min_lag_8kHz; i < max_lag_8kHz+1; i++)
      if (d_comp[i+1] > 0) d_srch[length_d_srch++] = i;
   for (i = max_lag_8kHz+3; i >= min_lag_8kHz; i--) d_comp[i] += d_comp[i-1]+d_comp[i-2]+d_comp[i-3];
   length_d_comp = 0;
   for (i = min_lag_8kHz; i < max_lag_8kHz+4; i++)
      if (d_comp[i] > 0) d_comp[length_d_comp++] = (fl_i16)(i-2);
   memset(C, 0, PE_MAX_NB_SUBFR*((PE_MAX_LAG>>1)+5)*sizeof(float));
   target_ptr = Fs_kHz == 8 ? &frame[PE_LTP_MEM_LENGTH_MS*8] : &frame_8kHz[PE_LTP_MEM_LENGTH_MS*8];
   for (k = 0; k < nb_subfr; k++) {
      double energy_tmp = (double)(silk_energy_FLP(target_ptr, sf_length_8kHz)+1.0);
      for (j = 0; j < length_d_comp; j++) {
         xf cc;
         d = d_comp[j];
         basis_ptr = target_ptr-d;
         cc = silk_inner_product_FLP(basis_ptr, target_ptr, sf_length_8kHz);
         if (cc > 0) {
            double cross_corr = (double)cc;
            C[k][d] = (float)((XF(cross_corr)*2)/(silk_energy_FLP(basis_ptr, sf_length_8kHz)+energy_tmp));
         } else C[k][d] = 0;
      }
      target_ptr += sf_length_8kHz;
   }
   CCmax = 0;
   CCmax_b = -1000.0f;
   CBimax = 0;
   lag = -1;
   if (prevLag > 0) {
      if (Fs_kHz == 12) prevLag = (prevLag<<1)/3;
      else if (Fs_kHz == 16) prevLag = prevLag>>1;
      prevLag_log2 = silk_log2((double)(float)prevLag);
   } else prevLag_log2 = 0;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      cbk_size = PE_NB_CBKS_STAGE2_EXT;
      Lag_CB_ptr = &fl_CB_lags_stage2[0][0];
      nb_cbk_search = Fs_kHz == 8 && complexity > SILK_PE_MIN_COMPLEX ? PE_NB_CBKS_STAGE2_EXT : PE_NB_CBKS_STAGE2;
   } else {
      cbk_size = PE_NB_CBKS_STAGE2_10MS;
      Lag_CB_ptr = &fl_CB_lags_stage2_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE2_10MS;
   }
   for (k = 0; k < length_d_srch; k++) {
      xf ccb;
      d = d_srch[k];
      for (j = 0; j < nb_cbk_search; j++) {
         xf s = 0;
         for (i = 0; i < nb_subfr; i++) s += C[i][d+Lag_CB_ptr[i*cbk_size+j]];
         CC[j] = (float)s;
      }
      CCmax_new = -1000.0f;
      CBimax_new = 0;
      for (i = 0; i < nb_cbk_search; i++)
         if (CC[i] > CCmax_new) {
            CCmax_new = CC[i];
            CBimax_new = i;
         }
      lag_log2 = silk_log2((double)(float)d);
      ccb = CCmax_new-(XF(.2f)*nb_subfr)*lag_log2;
      if (prevLag > 0) {
         xf dl = XF(lag_log2)-prevLag_log2;
         dl *= dl;
         ccb -= (((XF(.2f)*nb_subfr)**LTPCorr)*dl)/(dl+.5f);
      }
      if (ccb > CCmax_b && CCmax_new > XF(search_thres2)*nb_subfr) {
         CCmax_b = (float)ccb;
         CCmax = CCmax_new;
         lag = d;
         CBimax = CBimax_new;
      }
   }
   if (lag == -1) {
      memset(pitch_out, 0, PE_MAX_NB_SUBFR*sizeof(int));
      *LTPCorr = 0;
      *lagIndex = 0;
      *contourIndex = 0;
      return 1;
   }
   *LTPCorr = (float)(XF(CCmax)/nb_subfr);
   if (Fs_kHz > 8) {
      xf energy_tmp, ccmax;
      float contour_bias;
      if (Fs_kHz == 12) lag = silk_RSHIFT_ROUND(silk_SMULBB(lag, 3), 1);
      else lag = lag<<1;
      lag = silk_LIMIT_int(lag, min_lag, max_lag);
      start_lag = silk_max_int(lag-2, min_lag);
      end_lag = silk_min_int(lag+2, max_lag);
      lag_new = lag;
      CBimax = 0;
      ccmax = -1000.0f;
      silk_P_Ana_calc_corr_st3(cross_corr_st3, frame, start_lag, sf_length, nb_subfr, complexity);
      silk_P_Ana_calc_energy_st3(energies_st3, frame, start_lag, sf_length, nb_subfr, complexity);
      lag_counter = 0;
      contour_bias = (float)(XF(0.05f)/lag);
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         nb_cbk_search = (int)fl_nb_cbk_searchs_stage3[complexity];
         cbk_size = PE_NB_CBKS_STAGE3_MAX;
         Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      } else {
         nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
         cbk_size = PE_NB_CBKS_STAGE3_10MS;
         Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      }
      target_ptr = &frame[PE_LTP_MEM_LENGTH_MS*Fs_kHz];
      energy_tmp = silk_energy_FLP(target_ptr, nb_subfr*sf_length)+1.0;
      for (d = start_lag; d <= end_lag; d++) {
         for (j = 0; j < nb_cbk_search; j++) {
            xf cross_corr = 0, energy = energy_tmp, cn;
            for (k = 0; k < nb_subfr; k++) {
               cross_corr += cross_corr_st3[k][j][lag_counter];
               energy += energies_st3[k][j][lag_counter];
            }
            if (cross_corr > 0) {
               cn = (float)((cross_corr*2)/energy);
               cn *= 1-XF(contour_bias)*j;
            } else cn = 0;
            if (cn > ccmax && (d+(int)fl_CB_lags_stage3[0][j]) <= max_lag) {
               ccmax = cn;
               lag_new = d;
               CBimax = j;
            }
         }
         lag_counter++;
      }
      for (k = 0; k < nb_subfr; k++) {
         pitch_out[k] = lag_new+Lag_CB_ptr[k*cbk_size+CBimax];
         pitch_out[k] = silk_LIMIT(pitch_out[k], min_lag, PE_MAX_LAG_MS*Fs_kHz);
      }
      *lagIndex = (fl_i16)(lag_new-min_lag);
      *contourIndex = (fl_i8)CBimax;
   } else {
      for (k = 0; k < nb_subfr; k++) {
         pitch_out[k] = lag+Lag_CB_ptr[k*cbk_size+CBimax];
         pitch_out[k] = silk_LIMIT(pitch_out[k], min_lag_8kHz, PE_MAX_LAG_MS*8);
      }
      *lagIndex = (fl_i16)(lag-min_lag_8kHz);
      *contourIndex = (fl_i8)CBimax;
   }
   return 0;
}

static void silk_find_pitch_lags_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, float res[],
      const float x[]) {
   int buf_len;
   float res_nrg, auto_corr[MAX_FIND_PITCH_LPC_ORDER+1], A[MAX_FIND_PITCH_LPC_ORDER];
   float refl_coef[MAX_FIND_PITCH_LPC_ORDER], Wsig[FIND_PITCH_LPC_WIN_MAX], *Wsig_ptr;
   const float *x_buf_ptr, *x_buf;
   buf_len = psEnc->sCmn.la_pitch+psEnc->sCmn.frame_length+psEnc->sCmn.ltp_mem_length;
   x_buf = x-psEnc->sCmn.ltp_mem_length;
   x_buf_ptr = x_buf+buf_len-psEnc->sCmn.pitch_LPC_win_length;
   Wsig_ptr = Wsig;
   silk_apply_sine_window_FLP(Wsig_ptr, x_buf_ptr, 1, psEnc->sCmn.la_pitch);
   Wsig_ptr += psEnc->sCmn.la_pitch;
   x_buf_ptr += psEnc->sCmn.la_pitch;
   memcpy(Wsig_ptr, x_buf_ptr, (psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1))*sizeof(float));
   Wsig_ptr += psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1);
   x_buf_ptr += psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1);
   silk_apply_sine_window_FLP(Wsig_ptr, x_buf_ptr, 2, psEnc->sCmn.la_pitch);
   silk_autocorrelation_FLP(auto_corr, Wsig, psEnc->sCmn.pitch_LPC_win_length, psEnc->sCmn.pitchEstimationLPCOrder+1);
   auto_corr[0] = (float)((XF(auto_corr[0])*1e-3f+1)+auto_corr[0]);
   res_nrg = silk_schur_FLP(refl_coef, auto_corr, psEnc->sCmn.pitchEstimationLPCOrder);
   psEncCtrl->predGain = (float)(XF(auto_corr[0])/(res_nrg > 1.0f ? res_nrg : 1.0f));
   silk_k2a_FLP(A, refl_coef, psEnc->sCmn.pitchEstimationLPCOrder);
   silk_bwexpander_FLP(A, psEnc->sCmn.pitchEstimationLPCOrder, 0.99f);
   silk_LPC_analysis_filter_FLP(res, A, x_buf, buf_len, psEnc->sCmn.pitchEstimationLPCOrder);
   if (psEnc->sCmn.indices.signalType != TYPE_NO_VOICE_ACTIVITY && psEnc->sCmn.first_frame_after_reset == 0) {
      xf thr = 0.6f-XF(0.004f)*psEnc->sCmn.pitchEstimationLPCOrder;
      float thrhld;
      thr -= (XF(0.1f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
      thr -= XF(0.15f)*(psEnc->sCmn.prevSignalType>>1);
      thr -= (XF(0.1f)*psEnc->sCmn.input_tilt_Q15)*(1.0f/32768.0f);
      thrhld = (float)thr;
      if (silk_pitch_analysis_core_FLP(res, psEncCtrl->pitchL, &psEnc->sCmn.indices.lagIndex,
            &psEnc->sCmn.indices.contourIndex, &psEnc->LTPCorr, psEnc->sCmn.prevLag,
            (float)(XF(1/65536.0f)*psEnc->sCmn.pitchEstimationThreshold_Q16), thrhld, psEnc->sCmn.fs_kHz,
            psEnc->sCmn.pitchEstimationComplexity, psEnc->sCmn.nb_subfr) == 0)
         psEnc->sCmn.indices.signalType = TYPE_VOICED;
      else psEnc->sCmn.indices.signalType = TYPE_UNVOICED;
   } else {
      memset(psEncCtrl->pitchL, 0, sizeof(psEncCtrl->pitchL));
      psEnc->sCmn.indices.lagIndex = 0;
      psEnc->sCmn.indices.contourIndex = 0;
      psEnc->LTPCorr = 0;
   }
}

static void silk_limit_coefs_FLP(float *coefs, int order) {
   int i, iter, ind = 0;
   for (iter = 0; iter < 10; iter++) {
      float maxabs = -1.0f, chirp;
      for (i = 0; i < order; i++) {
         float tmp = fabsf(coefs[i]);
         if (tmp > maxabs) {
            maxabs = tmp;
            ind = i;
         }
      }
      if (maxabs <= 3.999f) return;
      chirp = (float)(.99f-((XF(.1f)*iter+.8f)*(XF(maxabs)-3.999f))/(XF(maxabs)*(ind+1)));
      silk_bwexpander_FLP(coefs, order, chirp);
   }
}
static void silk_warped_true2monic_coefs(float *coefs, float lambda, int order) {
   int i, iter, ind = 0;
   xf gain, one_m_l2 = 1-XF(lambda)*lambda;
   for (i = order-1; i > 0; i--) coefs[i-1] = (float)(coefs[i-1]-XF(lambda)*coefs[i]);
   gain = one_m_l2/(XF(lambda)*coefs[0]+1);
   for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
   for (iter = 0; iter < 10; iter++) {
      float maxabs = -1.0f, chirp;
      for (i = 0; i < order; i++) {
         float tmp = fabsf(coefs[i]);
         if (tmp > maxabs) {
            maxabs = tmp;
            ind = i;
         }
      }
      if (maxabs <= 3.999f) return;
      for (i = 1; i < order; i++) coefs[i-1] = (float)(coefs[i-1]+XF(lambda)*coefs[i]);
      gain = 1/gain;
      for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
      chirp = (float)(.99f-((XF(.1f)*iter+.8f)*(XF(maxabs)-3.999f))/(XF(maxabs)*(ind+1)));
      /* the hoisted (1-lambda^2) is spilled as float across this call */
      one_m_l2 = (float)one_m_l2;
      silk_bwexpander_FLP(coefs, order, chirp);
      for (i = order-1; i > 0; i--) coefs[i-1] = (float)(coefs[i-1]-XF(lambda)*coefs[i]);
      gain = one_m_l2/(XF(lambda)*coefs[0]+1);
      for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
   }
}

static void silk_noise_shape_analysis_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float *pitch_res, const float *x) {
   silk_shape_state_FLP *psShapeSt = &psEnc->sShape;
   int k, nSamples, nSegs;
   float SNR_adj_dB, BWExp, warping, gain_mult, x_windowed[SHAPE_LPC_WIN_MAX], auto_corr[MAX_SHAPE_LPC_ORDER+1];
   float rc[MAX_SHAPE_LPC_ORDER+1], Tilt, HarmShapeGain_f;
   xf HarmShapeGain, strength, b;
   const float *x_ptr = x-psEnc->sCmn.la_shape, *pitch_res_ptr;
   SNR_adj_dB = psEnc->sCmn.SNR_dB_Q7*(1/128.0f);
   psEncCtrl->input_quality = (float)((XF(.5f)*(psEnc->sCmn.input_quality_bands_Q15[0]+psEnc->sCmn.input_quality_bands_Q15[1]))*(1.0f/32768.0f));
   psEncCtrl->coding_quality = silk_sigmoid_neg((XF(SNR_adj_dB)-20.0f)*-.25f);
   {
      xf snr = SNR_adj_dB;
      if (psEnc->sCmn.useCBR == 0) {
         xf bb = 1-XF(1.0f/256.0f)*psEnc->sCmn.speech_activity_Q8;
         snr -= (((XF(psEncCtrl->coding_quality)*2)*(XF(.5f)*psEncCtrl->input_quality+.5f))*bb)*bb;
      }
      if (psEnc->sCmn.indices.signalType == TYPE_VOICED) snr += XF(psEnc->LTPCorr)*2;
      else snr += ((XF(psEnc->sCmn.SNR_dB_Q7)*-0.4f)*(1/128.0f)+6.0f)*(1-XF(psEncCtrl->input_quality));
      SNR_adj_dB = (float)snr;
   }
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) psEnc->sCmn.indices.quantOffsetType = 0;
   else {
      xf energy_variation = 0;
      float log_energy, log_energy_prev = 0;
      nSamples = 2*psEnc->sCmn.fs_kHz;
      pitch_res_ptr = pitch_res;
      nSegs = silk_SMULBB(SUB_FRAME_LENGTH_MS, psEnc->sCmn.nb_subfr)/2;
      for (k = 0; k < nSegs; k++) {
         float nrg = (float)silk_energy_FLP(pitch_res_ptr, nSamples);
         energy_variation = (float)energy_variation;
         log_energy = silk_log2((double)(XF(nrg)+nSamples));
         if (k > 0) energy_variation += fabsl(XF(log_energy)-log_energy_prev);
         log_energy_prev = log_energy;
         pitch_res_ptr += nSamples;
      }
      psEnc->sCmn.indices.quantOffsetType = energy_variation > XF(0.6f)*(nSegs-1) ? 0 : 1;
   }
   strength = XF(1e-3f)*psEncCtrl->predGain;
   BWExp = (float)(XF(0.94f)/(strength*strength+1));
   warping = (float)(XF(1.52587890625e-05f)*psEnc->sCmn.warping_Q16+XF(0.01f)*psEncCtrl->coding_quality);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      int shift, slope_part, flat_part;
      float nrg, *ar = &psEncCtrl->AR[k*MAX_SHAPE_LPC_ORDER];
      xf g;
      flat_part = psEnc->sCmn.fs_kHz*3;
      slope_part = (psEnc->sCmn.shapeWinLength-flat_part)/2;
      silk_apply_sine_window_FLP(x_windowed, x_ptr, 1, slope_part);
      shift = slope_part;
      memcpy(x_windowed+shift, x_ptr+shift, flat_part*sizeof(float));
      shift += flat_part;
      silk_apply_sine_window_FLP(x_windowed+shift, x_ptr+shift, 2, slope_part);
      x_ptr += psEnc->sCmn.subfr_length;
      if (psEnc->sCmn.warping_Q16 > 0)
         silk_warped_autocorrelation_FLP(auto_corr, x_windowed, warping, psEnc->sCmn.shapeWinLength, psEnc->sCmn.shapingLPCOrder);
      else silk_autocorrelation_FLP(auto_corr, x_windowed, psEnc->sCmn.shapeWinLength, psEnc->sCmn.shapingLPCOrder+1);
      auto_corr[0] = (float)((XF(auto_corr[0])*3e-5f+1.0f)+auto_corr[0]);
      nrg = silk_schur_FLP(rc, auto_corr, psEnc->sCmn.shapingLPCOrder);
      silk_k2a_FLP(ar, rc, psEnc->sCmn.shapingLPCOrder);
      g = fl_m_sqrtf(nrg);
      psEncCtrl->Gains[k] = (float)g;
      if (psEnc->sCmn.warping_Q16 > 0) {
         int i, order = psEnc->sCmn.shapingLPCOrder;
         xf lambda = -XF(warping), gain = ar[order-1];
         for (i = order-2; i >= 0; i--) gain = lambda*gain+ar[i];
         psEncCtrl->Gains[k] = (float)(g*(1/(1-lambda*gain)));
      }
      silk_bwexpander_FLP(ar, psEnc->sCmn.shapingLPCOrder, BWExp);
      if (psEnc->sCmn.warping_Q16 > 0) silk_warped_true2monic_coefs(ar, warping, psEnc->sCmn.shapingLPCOrder);
      else silk_limit_coefs_FLP(ar, psEnc->sCmn.shapingLPCOrder);
   }
   gain_mult = (float)fl_m_pow(2.0, (double)(XF(SNR_adj_dB)*-0.16f));
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++)
      psEncCtrl->Gains[k] = (float)(XF(psEncCtrl->Gains[k])*gain_mult+1.2483305931091309f);
   strength = ((XF(1.0f/32768.0f)*psEnc->sCmn.input_quality_bands_Q15[0]-1)*.5f+1)*4.0f;
   strength *= XF(psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
         b = XF(0.2f)/psEnc->sCmn.fs_kHz+XF(3.0f)/psEncCtrl->pitchL[k];
         psEncCtrl->LF_MA_shp[k] = (float)(b-1);
         psEncCtrl->LF_AR_shp[k] = (float)((1-b)-b*strength);
      }
      Tilt = (float)(-0.25f-(XF(0.26249998807907104f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f));
   } else {
      b = XF(1.3f)/psEnc->sCmn.fs_kHz;
      psEncCtrl->LF_MA_shp[0] = (float)(b-1);
      psEncCtrl->LF_AR_shp[0] = (float)((1-b)-(b*strength)*0.6f);
      for (k = 1; k < psEnc->sCmn.nb_subfr; k++) {
         psEncCtrl->LF_MA_shp[k] = psEncCtrl->LF_MA_shp[0];
         psEncCtrl->LF_AR_shp[k] = psEncCtrl->LF_AR_shp[0];
      }
      Tilt = -0.25f;
   }
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      HarmShapeGain_f = (float)((1-(1-XF(psEncCtrl->coding_quality))*psEncCtrl->input_quality)*0.2f+0.3f);
      HarmShapeGain = fl_m_sqrtf(psEnc->LTPCorr)*HarmShapeGain_f;
   } else HarmShapeGain = 0;
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      psShapeSt->HarmShapeGain_smth = (float)(psShapeSt->HarmShapeGain_smth+(HarmShapeGain-psShapeSt->HarmShapeGain_smth)*0.4f);
      psEncCtrl->HarmShapeGain[k] = psShapeSt->HarmShapeGain_smth;
      psShapeSt->Tilt_smth = (float)(psShapeSt->Tilt_smth+(XF(Tilt)-psShapeSt->Tilt_smth)*0.4f);
      psEncCtrl->Tilt[k] = psShapeSt->Tilt_smth;
   }
}

static void silk_encode_do_VAD_FLP(silk_encoder_state_FLP *psEnc, int activity) {
   const int activity_threshold = SILK_FIX_CONST(SPEECH_ACTIVITY_DTX_THRES, 8);
   silk_VAD_GetSA_Q8_c(&psEnc->sCmn, psEnc->sCmn.inputBuf+1);
   if (activity == VAD_NO_ACTIVITY && psEnc->sCmn.speech_activity_Q8 >= activity_threshold)
      psEnc->sCmn.speech_activity_Q8 = activity_threshold-1;
   if (psEnc->sCmn.speech_activity_Q8 < activity_threshold) {
      psEnc->sCmn.indices.signalType = TYPE_NO_VOICE_ACTIVITY;
      psEnc->sCmn.noSpeechCounter++;
      if (psEnc->sCmn.noSpeechCounter <= NB_SPEECH_FRAMES_BEFORE_DTX) psEnc->sCmn.inDTX = 0;
      else if (psEnc->sCmn.noSpeechCounter > MAX_CONSECUTIVE_DTX+NB_SPEECH_FRAMES_BEFORE_DTX) {
         psEnc->sCmn.noSpeechCounter = NB_SPEECH_FRAMES_BEFORE_DTX;
         psEnc->sCmn.inDTX = 0;
      }
      psEnc->sCmn.VAD_flags[psEnc->sCmn.nFramesEncoded] = 0;
   } else {
      psEnc->sCmn.noSpeechCounter = 0;
      psEnc->sCmn.inDTX = 0;
      psEnc->sCmn.indices.signalType = TYPE_UNVOICED;
      psEnc->sCmn.VAD_flags[psEnc->sCmn.nFramesEncoded] = 1;
   }
}

static void silk_LBRR_encode_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, const float xfw[],
      int condCoding) {
   int k;
   fl_i32 Gains_Q16[MAX_NB_SUBFR];
   float TempGains[MAX_NB_SUBFR];
   SideInfoIndices *psIndices_LBRR = &psEnc->sCmn.indices_LBRR[psEnc->sCmn.nFramesEncoded];
   silk_nsq_state sNSQ_LBRR;
   if (psEnc->sCmn.LBRR_enabled && psEnc->sCmn.speech_activity_Q8 > SILK_FIX_CONST(LBRR_SPEECH_ACTIVITY_THRES, 8)) {
      psEnc->sCmn.LBRR_flags[psEnc->sCmn.nFramesEncoded] = 1;
      memcpy(&sNSQ_LBRR, &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
      memcpy(psIndices_LBRR, &psEnc->sCmn.indices, sizeof(SideInfoIndices));
      memcpy(TempGains, psEncCtrl->Gains, psEnc->sCmn.nb_subfr*sizeof(float));
      if (psEnc->sCmn.nFramesEncoded == 0 || psEnc->sCmn.LBRR_flags[psEnc->sCmn.nFramesEncoded-1] == 0) {
         psEnc->sCmn.LBRRprevLastGainIndex = psEnc->sShape.LastGainIndex;
         psIndices_LBRR->GainsIndices[0] += psEnc->sCmn.LBRR_GainIncreases;
         psIndices_LBRR->GainsIndices[0] = (fl_i8)silk_min_int(psIndices_LBRR->GainsIndices[0], N_LEVELS_QGAIN-1);
      }
      silk_gains_dequant(Gains_Q16, psIndices_LBRR->GainsIndices, &psEnc->sCmn.LBRRprevLastGainIndex,
            condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(XF(1.0f/65536.0f)*Gains_Q16[k]);
      silk_NSQ_wrapper_FLP(psEnc, psEncCtrl, psIndices_LBRR, &sNSQ_LBRR, psEnc->sCmn.pulses_LBRR[psEnc->sCmn.nFramesEncoded], xfw);
      memcpy(psEncCtrl->Gains, TempGains, psEnc->sCmn.nb_subfr*sizeof(float));
   }
}

static int silk_encode_frame_FLP(silk_encoder_state_FLP *psEnc, fl_i32 *pnBytesOut, ec_ctx *psRangeEnc, int condCoding,
      int maxBits, int useCBR) {
   silk_encoder_control_FLP sEncCtrl;
   int i, iter, maxIter, found_upper, found_lower, ret = 0, bits_margin;
   float *x_frame, *res_pitch_frame, res_pitch[2*MAX_FRAME_LENGTH+LA_PITCH_MAX];
   ec_ctx sRangeEnc_copy, sRangeEnc_copy2;
   silk_nsq_state sNSQ_copy[2];
   fl_i32 seed_copy, nBits, nBits_lower, nBits_upper, gainMult_lower, gainMult_upper, gainsID, gainsID_lower, gainsID_upper;
   fl_i16 gainMult_Q8, ec_prevLagIndex_copy, best_gain_mult[MAX_NB_SUBFR];
   int ec_prevSignalType_copy, gain_lock[MAX_NB_SUBFR] = {0}, best_sum[MAX_NB_SUBFR];
   fl_i8 LastGainIndex_copy2;
   fl_i32 pGains_Q16[MAX_NB_SUBFR];
   unsigned char ec_buf_copy[1275];
   memset(&sEncCtrl, 0, sizeof(sEncCtrl));
   bits_margin = useCBR ? 5 : maxBits/4;
   LastGainIndex_copy2 = 0;
   nBits_lower = nBits_upper = gainMult_lower = gainMult_upper = 0;
   psEnc->sCmn.indices.Seed = psEnc->sCmn.frameCounter++&3;
   x_frame = psEnc->x_buf+psEnc->sCmn.ltp_mem_length;
   res_pitch_frame = res_pitch+psEnc->sCmn.ltp_mem_length;
   silk_LP_variable_cutoff(&psEnc->sCmn.sLP, psEnc->sCmn.inputBuf+1, psEnc->sCmn.frame_length);
   for (i = psEnc->sCmn.frame_length-1; i >= 0; i--)
      x_frame[LA_SHAPE_MS*psEnc->sCmn.fs_kHz+i] = psEnc->sCmn.inputBuf[1+i];
   for (i = 0; i < 8; i++) {
      float *p = &x_frame[LA_SHAPE_MS*psEnc->sCmn.fs_kHz+i*(psEnc->sCmn.frame_length>>3)];
      *p = (float)(XF(1e-6f)*(1-(i&2))+*p);
   }
   if (!psEnc->sCmn.prefillFlag) {
      silk_find_pitch_lags_FLP(psEnc, &sEncCtrl, res_pitch, x_frame);
      silk_noise_shape_analysis_FLP(psEnc, &sEncCtrl, res_pitch_frame, x_frame);
      silk_find_pred_coefs_FLP(psEnc, &sEncCtrl, res_pitch_frame, x_frame, condCoding);
      silk_process_gains_FLP(psEnc, &sEncCtrl, condCoding);
      silk_LBRR_encode_FLP(psEnc, &sEncCtrl, x_frame, condCoding);
      maxIter = 6;
      gainMult_Q8 = SILK_FIX_CONST(1, 8);
      found_lower = 0;
      found_upper = 0;
      gainsID = silk_gains_ID(psEnc->sCmn.indices.GainsIndices, psEnc->sCmn.nb_subfr);
      gainsID_lower = -1;
      gainsID_upper = -1;
      memcpy(&sRangeEnc_copy, psRangeEnc, sizeof(ec_ctx));
      memcpy(&sNSQ_copy[0], &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
      seed_copy = psEnc->sCmn.indices.Seed;
      ec_prevLagIndex_copy = psEnc->sCmn.ec_prevLagIndex;
      ec_prevSignalType_copy = psEnc->sCmn.ec_prevSignalType;
      for (iter = 0;; iter++) {
         if (gainsID == gainsID_lower) nBits = nBits_lower;
         else if (gainsID == gainsID_upper) nBits = nBits_upper;
         else {
            if (iter > 0) {
               memcpy(psRangeEnc, &sRangeEnc_copy, sizeof(ec_ctx));
               memcpy(&psEnc->sCmn.sNSQ, &sNSQ_copy[0], sizeof(silk_nsq_state));
               psEnc->sCmn.indices.Seed = (fl_i8)seed_copy;
               psEnc->sCmn.ec_prevLagIndex = ec_prevLagIndex_copy;
               psEnc->sCmn.ec_prevSignalType = ec_prevSignalType_copy;
            }
            silk_NSQ_wrapper_FLP(psEnc, &sEncCtrl, &psEnc->sCmn.indices, &psEnc->sCmn.sNSQ, psEnc->sCmn.pulses, x_frame);
            if (iter == maxIter && !found_lower) memcpy(&sRangeEnc_copy2, psRangeEnc, sizeof(ec_ctx));
            silk_encode_indices(&psEnc->sCmn, psRangeEnc, psEnc->sCmn.nFramesEncoded, 0, condCoding);
            silk_encode_pulses(psRangeEnc, psEnc->sCmn.indices.signalType, psEnc->sCmn.indices.quantOffsetType,
                  psEnc->sCmn.pulses, psEnc->sCmn.frame_length);
            nBits = ec_tell(psRangeEnc);
            if (iter == maxIter && !found_lower && nBits > maxBits) {
               memcpy(psRangeEnc, &sRangeEnc_copy2, sizeof(ec_ctx));
               psEnc->sShape.LastGainIndex = sEncCtrl.lastGainIndexPrev;
               for (i = 0; i < psEnc->sCmn.nb_subfr; i++) psEnc->sCmn.indices.GainsIndices[i] = 4;
               if (condCoding != CODE_CONDITIONALLY) psEnc->sCmn.indices.GainsIndices[0] = sEncCtrl.lastGainIndexPrev;
               psEnc->sCmn.ec_prevLagIndex = ec_prevLagIndex_copy;
               psEnc->sCmn.ec_prevSignalType = ec_prevSignalType_copy;
               for (i = 0; i < psEnc->sCmn.frame_length; i++) psEnc->sCmn.pulses[i] = 0;
               silk_encode_indices(&psEnc->sCmn, psRangeEnc, psEnc->sCmn.nFramesEncoded, 0, condCoding);
               silk_encode_pulses(psRangeEnc, psEnc->sCmn.indices.signalType, psEnc->sCmn.indices.quantOffsetType,
                     psEnc->sCmn.pulses, psEnc->sCmn.frame_length);
               nBits = ec_tell(psRangeEnc);
            }
            if (useCBR == 0 && iter == 0 && nBits <= maxBits) break;
         }
         if (iter == maxIter) {
            if (found_lower && (gainsID == gainsID_lower || nBits > maxBits)) {
               memcpy(psRangeEnc, &sRangeEnc_copy2, sizeof(ec_ctx));
               memcpy(psRangeEnc->buf, ec_buf_copy, sRangeEnc_copy2.offs);
               memcpy(&psEnc->sCmn.sNSQ, &sNSQ_copy[1], sizeof(silk_nsq_state));
               psEnc->sShape.LastGainIndex = LastGainIndex_copy2;
            }
            break;
         }
         if (nBits > maxBits) {
            if (found_lower == 0 && iter >= 2) {
               float l = sEncCtrl.Lambda*1.5f;
               sEncCtrl.Lambda = l > 1.5f ? l : 1.5f;
               psEnc->sCmn.indices.quantOffsetType = 0;
               found_upper = 0;
               gainsID_upper = -1;
            } else {
               found_upper = 1;
               nBits_upper = nBits;
               gainMult_upper = gainMult_Q8;
               gainsID_upper = gainsID;
            }
         } else if (nBits < maxBits-bits_margin) {
            found_lower = 1;
            nBits_lower = nBits;
            gainMult_lower = gainMult_Q8;
            if (gainsID != gainsID_lower) {
               gainsID_lower = gainsID;
               memcpy(&sRangeEnc_copy2, psRangeEnc, sizeof(ec_ctx));
               memcpy(ec_buf_copy, psRangeEnc->buf, psRangeEnc->offs);
               memcpy(&sNSQ_copy[1], &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
               LastGainIndex_copy2 = psEnc->sShape.LastGainIndex;
            }
         } else break;
         if (!found_lower && nBits > maxBits) {
            int j;
            for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
               int sum = 0;
               for (j = i*psEnc->sCmn.subfr_length; j < (i+1)*psEnc->sCmn.subfr_length; j++) sum += abs(psEnc->sCmn.pulses[j]);
               if (iter == 0 || (sum < best_sum[i] && !gain_lock[i])) {
                  best_sum[i] = sum;
                  best_gain_mult[i] = gainMult_Q8;
               } else gain_lock[i] = 1;
            }
         }
         if ((found_lower&found_upper) == 0) {
            if (nBits > maxBits) gainMult_Q8 = (fl_i16)silk_min_32(1024, gainMult_Q8*3/2);
            else gainMult_Q8 = (fl_i16)silk_max_32(64, gainMult_Q8*4/5);
         } else {
            gainMult_Q8 = (fl_i16)(gainMult_lower+((gainMult_upper-gainMult_lower)*(maxBits-nBits_lower))/(nBits_upper-nBits_lower));
            if (gainMult_Q8 > silk_ADD_RSHIFT32(gainMult_lower, gainMult_upper-gainMult_lower, 2))
               gainMult_Q8 = (fl_i16)silk_ADD_RSHIFT32(gainMult_lower, gainMult_upper-gainMult_lower, 2);
            else if (gainMult_Q8 < silk_SUB_RSHIFT32(gainMult_upper, gainMult_upper-gainMult_lower, 2))
               gainMult_Q8 = (fl_i16)silk_SUB_RSHIFT32(gainMult_upper, gainMult_upper-gainMult_lower, 2);
         }
         for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
            fl_i16 tmp = gain_lock[i] ? best_gain_mult[i] : gainMult_Q8;
            pGains_Q16[i] = silk_LSHIFT_SAT32(silk_SMULWB(sEncCtrl.GainsUnq_Q16[i], tmp), 8);
         }
         psEnc->sShape.LastGainIndex = sEncCtrl.lastGainIndexPrev;
         silk_gains_quant(psEnc->sCmn.indices.GainsIndices, pGains_Q16, &psEnc->sShape.LastGainIndex,
               condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
         gainsID = silk_gains_ID(psEnc->sCmn.indices.GainsIndices, psEnc->sCmn.nb_subfr);
         for (i = 0; i < psEnc->sCmn.nb_subfr; i++) sEncCtrl.Gains[i] = (float)(XF(1.0f/65536.0f)*pGains_Q16[i]);
      }
   }
   memmove(psEnc->x_buf, &psEnc->x_buf[psEnc->sCmn.frame_length],
         (psEnc->sCmn.ltp_mem_length+LA_SHAPE_MS*psEnc->sCmn.fs_kHz)*sizeof(float));
   if (psEnc->sCmn.prefillFlag) {
      *pnBytesOut = 0;
      return ret;
   }
   psEnc->sCmn.prevLag = sEncCtrl.pitchL[psEnc->sCmn.nb_subfr-1];
   psEnc->sCmn.prevSignalType = psEnc->sCmn.indices.signalType;
   psEnc->sCmn.first_frame_after_reset = 0;
   *pnBytesOut = silk_RSHIFT(ec_tell(psRangeEnc)+7, 3);
   return ret;
}

/* ======== enc_silk.c ======== */
/* ---- SILK encoder: interface used by the Opus encoder ---- */
struct fl_silk_enc { fl_silk_encoder s; };
static fl_silk_enc *fl_silk_create(void) { return (fl_silk_enc*)calloc(1, sizeof(fl_silk_enc)); }
static int fl_silk_init(fl_silk_enc *st, int channels, fl_silk_ctrl *status) {
   return silk_InitEncoder(&st->s, channels, 0, status);
}
static int fl_silk_encode(fl_silk_enc *st, fl_silk_ctrl *ctrl, const float *pcm, int nSamples, ec_ctx *enc,
      fl_i32 *nBytes, int prefillFlag, int activity) {
   return silk_Encode(&st->s, ctrl, pcm, nSamples, enc, nBytes, prefillFlag, activity);
}
static int fl_silk_hp_smth1(fl_silk_enc *st) { return st->s.state_Fxx[0].sCmn.variable_HP_smth1_Q15; }

#endif /* FLAPUS_HAVE_ENCODER */

/* ======== convert.c ======== */
/* ---- File conversion and in-memory audio ---- */

FLDEF void fl_options_init(fl_options *opt) {
   memset(opt, 0, sizeof(*opt));
   opt->dither = 1;
   opt->flac_level = 5;
   opt->threads = 1;
   opt->complexity = 10;
   opt->frame_ms_x10 = 200;
   opt->vbr = 1;
   opt->quality = -1;
   opt->padding = 512;
   opt->serialno = -1;
}

/* Detects the input format from the first bytes of the file. */
static int fl_detect_format(const char *path) {
   unsigned char b[4096];
   size_t n, i;
   FILE *f = fl_fopen(path, "rb");
   if (!f) return FL_ERR_IO;
   n = fread(b, 1, sizeof(b), f);
   fclose(f);
   if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b+8, "WAVE", 4) == 0) return FL_FORMAT_WAV;
   if (n >= 4 && (memcmp(b, "fLaC", 4) == 0 || memcmp(b, "ID3", 3) == 0)) return FL_FORMAT_FLAC;
   if (n >= 4 && memcmp(b, "OggS", 4) == 0) {
      /* look at the first packets of the BOS pages */
      for (i = 0; i+8 <= n; i++) {
         if (memcmp(b+i, "OpusHead", 8) == 0) return FL_FORMAT_OPUS;
         if (memcmp(b+i, "\177FLAC", 5) == 0) return FL_FORMAT_FLAC;
      }
   }
   return FL_ERR_UNSUPPORTED;
}

static int fl_ext_is(const char *path, const char *ext) {
   size_t n = strlen(path), e = strlen(ext), i;
   if (n < e) return 0;
   for (i = 0; i < e; i++) {
      int c = path[n-e+i];
      if (c >= 'A' && c <= 'Z') c += 32;
      if (c != ext[i]) return 0;
   }
   return 1;
}

static int fl_format_from_name(const char *path) {
   if (fl_ext_is(path, ".wav")) return FL_FORMAT_WAV;
   if (fl_ext_is(path, ".flac") || fl_ext_is(path, ".fla")) return FL_FORMAT_FLAC;
   if (fl_ext_is(path, ".opus") || fl_ext_is(path, ".ogg")) return FL_FORMAT_OPUS;
   return FL_ERR_BAD_ARG;
}

static fl_sink *fl_create_sink(int format, const char *path, const fl_options *opt, int *err) {
   *err = FL_ERR_BAD_ARG;
   if (format == FL_FORMAT_WAV) return fl_wav_sink_create(path, err);
   if (format == FL_FORMAT_FLAC) return fl_flac_sink_create(path, opt, err);
#ifdef FLAPUS_HAVE_ENCODER
   if (format == FL_FORMAT_OPUS) return fl_opus_sink_create(path, opt, err);
#else
   if (format == FL_FORMAT_OPUS) *err = FL_ERR_UNSUPPORTED;
#endif
   return NULL;
}

/* Sink wrapper that reports progress to opt->progress. */
typedef struct {
   fl_sink base;
   fl_sink *inner;
   const fl_options *opt;
   fl_i64 total, done;
   double pcm_bytes;  /* input PCM bytes per frame */
} fl_progress_sink;

static void fl_progress_report(fl_progress_sink *p, int final) {
   fl_sink *in = p->inner;
   double done = final ? 1.0 : p->total > 0 ? FL_MIN(1.0, (double)p->done/p->total) : -1.0, ratio = -1.0;
   if (in->out_frames > 0) ratio = in->out_bytes/(in->out_frames*p->pcm_bytes);
   p->opt->progress(p->opt->progress_user, done, ratio);
}

static int fl_progress_start(fl_sink *s, const fl_format *f) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->start(p->inner, f);
   p->total = f->frames;
   p->pcm_bytes = (double)f->channels*((f->bits+7)/8);
   if (ret == FL_OK) fl_progress_report(p, 0);
   return ret;
}

static int fl_progress_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->write(p->inner, pcm, n);
   p->done += n;
   if (ret == FL_OK) fl_progress_report(p, 0);
   return ret;
}

static int fl_progress_finish(fl_sink *s) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->finish(p->inner);
   if (ret == FL_OK) fl_progress_report(p, 1);
   return ret;
}

static void fl_progress_destroy(fl_sink *s) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   p->inner->destroy(p->inner);
   free(p);
}

static fl_sink *fl_progress_wrap(fl_sink *inner, const fl_options *opt, int *err) {
   fl_progress_sink *p;
   if (!opt->progress) return inner;
   if (!(p = (fl_progress_sink*)calloc(1, sizeof(*p)))) {
      inner->destroy(inner);
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   p->inner = inner;
   p->opt = opt;
   p->base.start = fl_progress_start;
   p->base.write = fl_progress_write;
   p->base.finish = fl_progress_finish;
   p->base.destroy = fl_progress_destroy;
   return &p->base;
}

static int fl_run_source(int format, const char *path, const fl_options *opt, fl_sink *sink) {
   if (format == FL_FORMAT_WAV) return fl_run_wav(path, sink);
   if (format == FL_FORMAT_FLAC) return fl_run_flac(path, sink);
   return fl_run_opus(path, opt->rate, opt->dither, sink);
}

FLDEF int fl_convert_file(const char *in_path, const char *out_path, const fl_options *opt_in) {
   fl_options opt;
   fl_sink *sink;
   int in_fmt, out_fmt, ret, err;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if (strcmp(in_path, out_path) == 0) return FL_ERR_BAD_ARG;
   in_fmt = fl_detect_format(in_path);
   if (in_fmt < 0) return in_fmt;
   out_fmt = opt.format ? opt.format : fl_format_from_name(out_path);
   if (out_fmt < 0) return out_fmt;
   if (!(sink = fl_create_sink(out_fmt, out_path, &opt, &err))) return err;
   if (!(sink = fl_progress_wrap(sink, &opt, &err))) {
      fl_remove(out_path);
      return err;
   }
   ret = fl_run_source(in_fmt, in_path, &opt, sink);
   sink->destroy(sink);
   /* a damaged FLAC input still gives complete output; anything else is removed */
   if (ret != FL_OK && ret != FL_ERR_CHECKSUM) fl_remove(out_path);
   return ret;
}

FLDEF int fl_read_audio(const char *path, const fl_options *opt_in, fl_audio *audio) {
   fl_options opt;
   fl_mem_sink m;
   int fmt, ret;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   memset(audio, 0, sizeof(*audio));
   fmt = fl_detect_format(path);
   if (fmt < 0) return fmt;
   memset(&m, 0, sizeof(m));
   m.base.start = fl_mem_start;
   m.base.write = fl_mem_write;
   m.base.finish = fl_mem_finish;
   m.base.destroy = fl_mem_destroy;
   m.a = audio;
   ret = fl_run_source(fmt, path, &opt, &m.base);
   if (ret != FL_OK && ret != FL_ERR_CHECKSUM) fl_audio_free(audio);
   return ret;
}

FLDEF int fl_write_audio(const char *path, const fl_audio *a, const fl_options *opt_in) {
   fl_options opt;
   fl_format f;
   fl_sink *sink;
   int out_fmt, ret, err;
   long long pos = 0;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   out_fmt = opt.format ? opt.format : fl_format_from_name(path);
   if (out_fmt < 0) return out_fmt;
   if (a->channels < 1 || a->bits < 1 || a->bits > 32 || a->rate < 1 || a->frames < 0) return FL_ERR_BAD_ARG;
   if (!(sink = fl_create_sink(out_fmt, path, &opt, &err))) return err;
   fl_format_init(&f);
   f.channels = a->channels;
   f.rate = a->rate;
   f.bits = f.container_bits = a->is_float ? 32 : a->bits;
   f.is_float = a->is_float;
   f.frames = a->frames;
   ret = sink->start(sink, &f);
   while (ret == FL_OK && pos < a->frames) {
      int n = a->frames-pos < 4096 ? (int)(a->frames-pos) : 4096;
      ret = sink->write(sink, a->samples+(size_t)pos*a->channels, n);
      pos += n;
   }
   if (ret == FL_OK) ret = sink->finish(sink);
   sink->destroy(sink);
   if (ret != FL_OK) fl_remove(path);
   return ret;
}

FLDEF void fl_audio_free(fl_audio *audio) {
   free(audio->samples);
   memset(audio, 0, sizeof(*audio));
}

#endif /* FLAPUS_IMPLEMENTATION_DONE */
#endif /* FLAPUS_IMPLEMENTATION */
