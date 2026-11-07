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
#include <io.h>
#include <fcntl.h>
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

/* The path "-" is stdin for input and stdout for output. */
static int fl_is_std(const char *path) { return path[0] == '-' && path[1] == 0; }

static FILE *fl_fopen(const char *path, const char *mode) {
#ifdef _WIN32
   wchar_t *w;
#endif
   if (fl_is_std(path)) {
      FILE *f = mode[0] == 'r' ? stdin : stdout;
#ifdef _WIN32
      _setmode(_fileno(f), _O_BINARY);
#endif
      return f;
   }
#ifdef _WIN32
   w = fl_widen(path);
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

/* fclose, but stdin and stdout stay open (stdout is only flushed). */
static int fl_fclose(FILE *f) {
   if (f == stdin) return 0;
   return f == stdout ? fflush(f) : fclose(f);
}

static int fl_remove(const char *path) {
#ifdef _WIN32
   wchar_t *w;
#endif
   if (fl_is_std(path)) return 0;  /* nothing to remove */
#ifdef _WIN32
   w = fl_widen(path);
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
