/* ---- Common types, helpers and the range coder ---- */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
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
#include <process.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/attr.h>
#endif
#endif
#include <time.h>

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
#define FL_MAX_RATE 1048575  /* highest sample rate (FLAC STREAMINFO limit) */
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

/* A 32-bit number that differs between calls, processes and runs (for Ogg serials). */
static fl_u32 fl_random32(const void *salt) {
   static fl_u32 counter;
   fl_u64 x = (fl_u64)time(NULL);
#ifdef _WIN32
   x ^= (fl_u64)_getpid()<<32;
#elif defined(__unix__) || defined(__APPLE__)
   x ^= (fl_u64)getpid()<<32;
#endif
   x ^= (fl_u64)clock()<<16^(fl_u64)(size_t)salt^(fl_u64)(size_t)&x<<8^(fl_u64)++counter<<48;
   /* splitmix64 finalizer */
   x = (x^(x>>30))*0xbf58476d1ce4e5b9ULL;
   x = (x^(x>>27))*0x94d049bb133111ebULL;
   return (fl_u32)((x^(x>>31))>>32);
}

/* 1 if something exists at path, also a file that cannot be read or a dangling link. */
static int fl_file_exists(const char *path) {
#ifdef _WIN32
   wchar_t *w;
   HANDLE h;
   DWORD e;
#elif defined(__unix__) || defined(__APPLE__)
   struct stat st;
#else
   FILE *f;
#endif
   if (fl_is_std(path)) return 0;
#ifdef _WIN32
   /* no access rights needed: only whether it can be found */
   if ((w = fl_widen(path))) {
      h = CreateFileW(w, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      free(w);
   } else h = CreateFileA(path, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
   if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      return 1;
   }
   e = GetLastError();
   return e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION;
#elif defined(__unix__) || defined(__APPLE__)
   return lstat(path, &st) == 0;
#else
   if ((f = fl_fopen(path, "rb"))) fclose(f);
   return f != NULL;
#endif
}

/* Creates path as a new empty file, never opening an existing one (or a link). Returns 1,
   0 if path exists, or -1 on another error. */
static int fl_create_new(const char *path) {
#ifdef _WIN32
   wchar_t *w;
   HANDLE h;
   DWORD e;
   if ((w = fl_widen(path))) {
      h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      free(w);
   } else h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
   if (h == INVALID_HANDLE_VALUE) {
      e = GetLastError();
      return e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS ? 0 : -1;
   }
   CloseHandle(h);
   return 1;
#elif defined(__unix__) || defined(__APPLE__)
   int fd = open(path, O_WRONLY|O_CREAT|O_EXCL, 0666);
   if (fd < 0) return errno == EEXIST ? 0 : -1;
   close(fd);
   return 1;
#else
   FILE *f;
   if (fl_file_exists(path)) return 0;
   if (!(f = fl_fopen(path, "wb"))) return -1;
   fclose(f);
   return 1;
#endif
}

/* Creates a new empty file next to path ("path.1a2b3c4d.tmp") and returns its name (free
   it), or NULL. */
static char *fl_temp_path(const char *path) {
   size_t n = strlen(path);
   char *t = (char*)malloc(n+14);
   int tries, r;
   if (!t) return NULL;
   for (tries = 0; tries < 100; tries++) {
      sprintf(t, "%s.%08x.tmp", path, (unsigned)fl_random32(t));
      if ((r = fl_create_new(t)) > 0) return t;
      if (r < 0) break;
   }
   free(t);
   return NULL;
}

/* Moves from over to (replacing it). Keeps the permissions of a replaced file. Returns 0. */
static int fl_rename_over(const char *from, const char *to) {
#ifdef _WIN32
   wchar_t *wf = fl_widen(from), *wt = fl_widen(to);
   int ok;
   if (wf && wt) ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING) != 0;
   else ok = MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
   free(wf);
   free(wt);
   return ok ? 0 : -1;
#else
#if defined(__unix__) || defined(__APPLE__)
   struct stat st;
   if (stat(to, &st) == 0) chmod(from, st.st_mode&07777);
#endif
   return rename(from, to);
#endif
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
