/* ---- Audio pipeline: stream format, sinks, tags, WAV input/output, MD5, base64 ---- */

/* FL_OK if every option is in range, else FL_ERR_BAD_ARG. */
static int fl_check_options(const fl_options *o) {
   int fs = o->frame_ms_x10;
   if (o->format < 0 || o->format > FL_FORMAT_OPUS) return FL_ERR_BAD_ARG;
   if (o->rate != 0 && (o->rate < 8000 || o->rate > 192000)) return FL_ERR_BAD_ARG;
   if ((o->bits && o->bits != 16 && o->bits != 24 && o->bits != 32) || (o->as_float & ~1)) return FL_ERR_BAD_ARG;
   if ((o->dither|o->flac_lax|o->vbr|o->cvbr|o->no_phase_inv) & ~1) return FL_ERR_BAD_ARG;
   if (o->flac_level < 0 || o->flac_level > 8 || o->threads < 0) return FL_ERR_BAD_ARG;
   if (o->bitrate < 0 || (o->bitrate > 0 && o->bitrate < 500)) return FL_ERR_BAD_ARG;
   if (o->complexity < 0 || o->complexity > 10) return FL_ERR_BAD_ARG;
   if (fs != 25 && fs != 50 && fs != 100 && fs != 200 && fs != 400 && fs != 600 && fs != 800 && fs != 1000 && fs != 1200)
      return FL_ERR_BAD_ARG;
   if (o->quality != -1 && !(o->quality >= 0 && o->quality <= 10)) return FL_ERR_BAD_ARG;
   if (o->quality >= 0 && o->bitrate > 0) return FL_ERR_BAD_ARG;
   if (o->signal < 0 || o->signal > 2 || o->expect_loss < 0 || o->expect_loss > 100) return FL_ERR_BAD_ARG;
   if (o->downmix < 0 || o->downmix > 2 || o->padding < 0 || o->padding > 0xFFFFFF) return FL_ERR_BAD_ARG;
   if ((o->flac_seektable|o->flac_verify|o->keep_time|o->remove_tags) & ~1) return FL_ERR_BAD_ARG;
   if (o->serialno < -1 || o->serialno > 4294967295LL || o->search < 0 || o->search > 1<<30) return FL_ERR_BAD_ARG;
   return FL_OK;
}

/* Buffered input that keeps at least the requested number of bytes in memory. The input
   is opened once, so stdin works as well: format detection looks at the buffer. base is
   the stream offset of buf[0]. Files can seek; pipes can only skip forward. */
typedef struct {
   FILE *f;
   fl_io io;            /* user input when f is NULL */
   unsigned char *buf;
   size_t cap, pos, len;
   fl_i64 base;
   int eof, seekable;
   int borrowed;        /* buf is the caller's memory (fl_reader_open_memory) */
} fl_inbuf;

/* fseek with 64-bit offsets, SEEK_SET and SEEK_CUR. */
static int fl_fseek64(FILE *f, fl_i64 off, int whence) {
#if defined(_WIN32)
   /* fpos_t is a 64-bit byte offset in every Windows C library, msvcrt.dll of NT 4 included */
   fpos_t pos;
   if (whence == SEEK_CUR) {
      if (fgetpos(f, &pos) != 0) return -1;
      off += pos;
   } else if (whence != SEEK_SET) return -1;
   pos = off;
   return fsetpos(f, &pos);
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__STRICT_ANSI__)
   return fseeko(f, (off_t)off, whence);
#else
   return off > LONG_MAX || off < LONG_MIN ? -1 : fseek(f, (long)off, whence);
#endif
}

static int fl_inbuf_open(fl_inbuf *b, const char *path) {
   memset(b, 0, sizeof(*b));
   if (!(b->f = fl_fopen(path, "rb"))) return FL_ERR_IO;
   b->seekable = b->f != stdin && fl_fseek64(b->f, 0, SEEK_CUR) == 0;
   return FL_OK;
}

static void fl_inbuf_open_io(fl_inbuf *b, const fl_io *io) {
   memset(b, 0, sizeof(*b));
   b->io = *io;
   b->seekable = io->seek != NULL;
}

/* Memory input: the whole stream is the buffer. */
static void fl_inbuf_open_memory(fl_inbuf *b, const void *data, size_t size) {
   memset(b, 0, sizeof(*b));
   b->buf = (unsigned char*)data;
   b->len = b->cap = size;
   b->eof = b->seekable = b->borrowed = 1;
}

static void fl_inbuf_close(fl_inbuf *b) {
   if (b->f) fl_fclose(b->f);
   if (!b->borrowed) free(b->buf);
   memset(b, 0, sizeof(*b));
}

static size_t fl_inbuf_raw(fl_inbuf *b, void *dst, size_t n) {
   if (b->borrowed) return 0;
   return b->f ? fread(dst, 1, n, b->f) : b->io.read ? b->io.read(b->io.user, dst, n) : 0;
}

static fl_i64 fl_inbuf_tell(const fl_inbuf *b) { return b->base+(fl_i64)b->pos; }

/* Makes n bytes available at buf+pos (fewer only at end of file). */
static int fl_inbuf_need(fl_inbuf *b, size_t n) {
   if (b->len-b->pos >= n || b->eof) return 0;
   if (b->pos > 0) {
      memmove(b->buf, b->buf+b->pos, b->len-b->pos);
      b->len -= b->pos;
      b->base += (fl_i64)b->pos;
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
      size_t r = fl_inbuf_raw(b, b->buf+b->len, b->cap-b->len);
      b->len += r;
      if (r == 0) b->eof = 1;
   }
   return 0;
}

/* Reads up to n bytes into dst, returns the number read. */
static size_t fl_inbuf_read(fl_inbuf *b, void *dst, size_t n) {
   size_t k = FL_MIN(n, b->len-b->pos);
   if (k) memcpy(dst, b->buf+b->pos, k);
   b->pos += k;
   if (k < n && !b->eof) {
      size_t r = fl_inbuf_raw(b, (unsigned char*)dst+k, n-k);
      if (r < n-k) b->eof = 1;
      b->base += (fl_i64)(b->len+r);
      b->pos = b->len = 0;
      k += r;
   }
   return k;
}

/* Moves to stream offset off: within the buffer, by seeking, or (forward only) by
   reading through. Returns 0 or -1. */
static int fl_inbuf_seek(fl_inbuf *b, fl_i64 off) {
   unsigned char tmp[4096];
   if (off >= b->base && off <= b->base+(fl_i64)b->len) {
      b->pos = (size_t)(off-b->base);
      return 0;
   }
   if (b->seekable && !b->borrowed) {
      if (b->f ? fl_fseek64(b->f, off, SEEK_SET) != 0 : b->io.seek(b->io.user, off) != 0) return -1;
      b->base = off;
      b->pos = b->len = 0;
      b->eof = 0;
      return 0;
   }
   if (off < b->base || b->borrowed) return -1;
   b->pos = b->len;
   while (fl_inbuf_tell(b) < off && !b->eof) fl_inbuf_read(b, tmp, (size_t)FL_MIN(off-fl_inbuf_tell(b), (fl_i64)sizeof(tmp)));
   return fl_inbuf_tell(b) == off ? 0 : -1;
}

/* Skips n bytes; past the end of the input, to the end (memory input cannot seek there). */
static void fl_inbuf_skip(fl_inbuf *b, fl_u64 n) {
   if (n > (fl_u64)1<<62 || fl_inbuf_seek(b, fl_inbuf_tell(b)+(fl_i64)n) != 0) {
      b->pos = b->len;
      b->eof = 1;
   }
}

/* Input that cannot seek is read into memory, so that it can be scanned twice. */
static int fl_inbuf_make_seekable(fl_inbuf *b) {
   while (!b->seekable && !b->eof)
      if (fl_inbuf_need(b, b->len-b->pos+(b->cap ? b->cap : 65536)) < 0) return -1;
   return 0;
}

/* Output: a file, stdout or user callbacks. Errors are remembered until the end. */
typedef struct {
   FILE *f;
   fl_io io;            /* user output when f is NULL */
   int seekable, err;
} fl_out;

static fl_out *fl_out_file(const char *path, int rw) {
   fl_out *o = (fl_out*)calloc(1, sizeof(*o));
   if (!o) return NULL;
   if (!(o->f = fl_fopen(path, fl_is_std(path) ? "wb" : rw ? "w+b" : "wb"))) {
      free(o);
      return NULL;
   }
   o->seekable = o->f != stdout && fl_fseek64(o->f, 0, SEEK_CUR) == 0;  /* not a pipe */
   return o;
}

static fl_out *fl_out_from_io(const fl_io *io) {
   fl_out *o = (fl_out*)calloc(1, sizeof(*o));
   if (!o) return NULL;
   o->io = *io;
   o->seekable = io->seek != NULL;
   return o;
}

static size_t fl_out_write(fl_out *o, const void *p, size_t n) {
   size_t r;
   if (!n) return 0;
   r = o->f ? fwrite(p, 1, n, o->f) : o->io.write ? o->io.write(o->io.user, p, n) : 0;
   if (r != n) o->err = 1;
   return r;
}

static int fl_out_seek(fl_out *o, fl_i64 off) {
   if (!o->seekable) return -1;
   return o->f ? fl_fseek64(o->f, off, SEEK_SET) : o->io.seek(o->io.user, off);
}

/* Reads back output (WAV above 4 GB); 0 if the output cannot be read. */
static size_t fl_out_read(fl_out *o, void *p, size_t n) {
   return o->f ? fread(p, 1, n, o->f) : o->io.read ? o->io.read(o->io.user, p, n) : 0;
}

/* Closes the output: FL_OK, or FL_ERR_IO if a write or the close failed. */
static int fl_out_close(fl_out *o) {
   int ret;
   if (!o) return FL_OK;
   ret = o->err ? FL_ERR_IO : FL_OK;
   if (o->f && fl_fclose(o->f) != 0) ret = FL_ERR_IO;
   free(o);
   return ret;
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

/* The 64 steps written out (RFC 1321): about 1.5 times as fast as a loop over tables. */
#define FL_MD5_F(x, y, z) ((z)^((x)&((y)^(z))))
#define FL_MD5_G(x, y, z) FL_MD5_F(z, x, y)
#define FL_MD5_H(x, y, z) ((x)^(y)^(z))
#define FL_MD5_I(x, y, z) ((y)^((x)|~(z)))
#define FL_MD5_STEP(f, a, b, c, d, x, k, s) (a += f(b, c, d)+(x)+(k), a = (a<<(s)|a>>(32-(s)))+(b))

static void fl_md5_block(fl_md5 *m, const unsigned char *p) {
   fl_u32 w[16], a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
   int i;
   for (i = 0; i < 16; i++) w[i] = fl_get_le32(p+4*i);
   FL_MD5_STEP(FL_MD5_F, a, b, c, d, w[0], 0xd76aa478, 7);
   FL_MD5_STEP(FL_MD5_F, d, a, b, c, w[1], 0xe8c7b756, 12);
   FL_MD5_STEP(FL_MD5_F, c, d, a, b, w[2], 0x242070db, 17);
   FL_MD5_STEP(FL_MD5_F, b, c, d, a, w[3], 0xc1bdceee, 22);
   FL_MD5_STEP(FL_MD5_F, a, b, c, d, w[4], 0xf57c0faf, 7);
   FL_MD5_STEP(FL_MD5_F, d, a, b, c, w[5], 0x4787c62a, 12);
   FL_MD5_STEP(FL_MD5_F, c, d, a, b, w[6], 0xa8304613, 17);
   FL_MD5_STEP(FL_MD5_F, b, c, d, a, w[7], 0xfd469501, 22);
   FL_MD5_STEP(FL_MD5_F, a, b, c, d, w[8], 0x698098d8, 7);
   FL_MD5_STEP(FL_MD5_F, d, a, b, c, w[9], 0x8b44f7af, 12);
   FL_MD5_STEP(FL_MD5_F, c, d, a, b, w[10], 0xffff5bb1, 17);
   FL_MD5_STEP(FL_MD5_F, b, c, d, a, w[11], 0x895cd7be, 22);
   FL_MD5_STEP(FL_MD5_F, a, b, c, d, w[12], 0x6b901122, 7);
   FL_MD5_STEP(FL_MD5_F, d, a, b, c, w[13], 0xfd987193, 12);
   FL_MD5_STEP(FL_MD5_F, c, d, a, b, w[14], 0xa679438e, 17);
   FL_MD5_STEP(FL_MD5_F, b, c, d, a, w[15], 0x49b40821, 22);
   FL_MD5_STEP(FL_MD5_G, a, b, c, d, w[1], 0xf61e2562, 5);
   FL_MD5_STEP(FL_MD5_G, d, a, b, c, w[6], 0xc040b340, 9);
   FL_MD5_STEP(FL_MD5_G, c, d, a, b, w[11], 0x265e5a51, 14);
   FL_MD5_STEP(FL_MD5_G, b, c, d, a, w[0], 0xe9b6c7aa, 20);
   FL_MD5_STEP(FL_MD5_G, a, b, c, d, w[5], 0xd62f105d, 5);
   FL_MD5_STEP(FL_MD5_G, d, a, b, c, w[10], 0x02441453, 9);
   FL_MD5_STEP(FL_MD5_G, c, d, a, b, w[15], 0xd8a1e681, 14);
   FL_MD5_STEP(FL_MD5_G, b, c, d, a, w[4], 0xe7d3fbc8, 20);
   FL_MD5_STEP(FL_MD5_G, a, b, c, d, w[9], 0x21e1cde6, 5);
   FL_MD5_STEP(FL_MD5_G, d, a, b, c, w[14], 0xc33707d6, 9);
   FL_MD5_STEP(FL_MD5_G, c, d, a, b, w[3], 0xf4d50d87, 14);
   FL_MD5_STEP(FL_MD5_G, b, c, d, a, w[8], 0x455a14ed, 20);
   FL_MD5_STEP(FL_MD5_G, a, b, c, d, w[13], 0xa9e3e905, 5);
   FL_MD5_STEP(FL_MD5_G, d, a, b, c, w[2], 0xfcefa3f8, 9);
   FL_MD5_STEP(FL_MD5_G, c, d, a, b, w[7], 0x676f02d9, 14);
   FL_MD5_STEP(FL_MD5_G, b, c, d, a, w[12], 0x8d2a4c8a, 20);
   FL_MD5_STEP(FL_MD5_H, a, b, c, d, w[5], 0xfffa3942, 4);
   FL_MD5_STEP(FL_MD5_H, d, a, b, c, w[8], 0x8771f681, 11);
   FL_MD5_STEP(FL_MD5_H, c, d, a, b, w[11], 0x6d9d6122, 16);
   FL_MD5_STEP(FL_MD5_H, b, c, d, a, w[14], 0xfde5380c, 23);
   FL_MD5_STEP(FL_MD5_H, a, b, c, d, w[1], 0xa4beea44, 4);
   FL_MD5_STEP(FL_MD5_H, d, a, b, c, w[4], 0x4bdecfa9, 11);
   FL_MD5_STEP(FL_MD5_H, c, d, a, b, w[7], 0xf6bb4b60, 16);
   FL_MD5_STEP(FL_MD5_H, b, c, d, a, w[10], 0xbebfbc70, 23);
   FL_MD5_STEP(FL_MD5_H, a, b, c, d, w[13], 0x289b7ec6, 4);
   FL_MD5_STEP(FL_MD5_H, d, a, b, c, w[0], 0xeaa127fa, 11);
   FL_MD5_STEP(FL_MD5_H, c, d, a, b, w[3], 0xd4ef3085, 16);
   FL_MD5_STEP(FL_MD5_H, b, c, d, a, w[6], 0x04881d05, 23);
   FL_MD5_STEP(FL_MD5_H, a, b, c, d, w[9], 0xd9d4d039, 4);
   FL_MD5_STEP(FL_MD5_H, d, a, b, c, w[12], 0xe6db99e5, 11);
   FL_MD5_STEP(FL_MD5_H, c, d, a, b, w[15], 0x1fa27cf8, 16);
   FL_MD5_STEP(FL_MD5_H, b, c, d, a, w[2], 0xc4ac5665, 23);
   FL_MD5_STEP(FL_MD5_I, a, b, c, d, w[0], 0xf4292244, 6);
   FL_MD5_STEP(FL_MD5_I, d, a, b, c, w[7], 0x432aff97, 10);
   FL_MD5_STEP(FL_MD5_I, c, d, a, b, w[14], 0xab9423a7, 15);
   FL_MD5_STEP(FL_MD5_I, b, c, d, a, w[5], 0xfc93a039, 21);
   FL_MD5_STEP(FL_MD5_I, a, b, c, d, w[12], 0x655b59c3, 6);
   FL_MD5_STEP(FL_MD5_I, d, a, b, c, w[3], 0x8f0ccc92, 10);
   FL_MD5_STEP(FL_MD5_I, c, d, a, b, w[10], 0xffeff47d, 15);
   FL_MD5_STEP(FL_MD5_I, b, c, d, a, w[1], 0x85845dd1, 21);
   FL_MD5_STEP(FL_MD5_I, a, b, c, d, w[8], 0x6fa87e4f, 6);
   FL_MD5_STEP(FL_MD5_I, d, a, b, c, w[15], 0xfe2ce6e0, 10);
   FL_MD5_STEP(FL_MD5_I, c, d, a, b, w[6], 0xa3014314, 15);
   FL_MD5_STEP(FL_MD5_I, b, c, d, a, w[13], 0x4e0811a1, 21);
   FL_MD5_STEP(FL_MD5_I, a, b, c, d, w[4], 0xf7537e82, 6);
   FL_MD5_STEP(FL_MD5_I, d, a, b, c, w[11], 0xbd3af235, 10);
   FL_MD5_STEP(FL_MD5_I, c, d, a, b, w[2], 0x2ad7d2bb, 15);
   FL_MD5_STEP(FL_MD5_I, b, c, d, a, w[9], 0xeb86d391, 21);
   m->h[0] += a;
   m->h[1] += b;
   m->h[2] += c;
   m->h[3] += d;
}
#undef FL_MD5_F
#undef FL_MD5_G
#undef FL_MD5_H
#undef FL_MD5_I
#undef FL_MD5_STEP

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

/* Ogg page CRC (polynomial 0x04c11db7), four bits at a time. */
static fl_u32 fl_ogg_crc(fl_u32 crc, const unsigned char *p, size_t n) {
   static const fl_u32 tab[16] = {0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
                                  0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
                                  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd};
   while (n--) {
      crc = crc<<4^tab[crc>>28^*p>>4];
      crc = crc<<4^tab[crc>>28^(*p++&15)];
   }
   return crc;
}

/* CRC of the Ogg page of len bytes at p, with its CRC field taken as zero. */
static fl_u32 fl_ogg_page_crc(const unsigned char *p, size_t len) {
   unsigned char h[27];
   memcpy(h, p, 27);
   memset(h+22, 0, 4);
   return fl_ogg_crc(fl_ogg_crc(0, h, 27), p+27, len-27);
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

/* Length for a header that cannot be patched at the end (stdout): the source length if
   it is exact, else -1 (Opus lengths are only approximate). */
static fl_i64 fl_exact_frames(const fl_format *f) { return f->opus_family >= 0 ? -1 : f->frames; }

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
   fl_u64 pos = 4, n, i;  /* 64 bits: long has only 32 on Windows */
   if (len < 8) return -1;
   pos += fl_get_le32(p);
   if (pos+4 > (fl_u64)len) return -1;
   n = fl_get_le32(p+pos);
   pos += 4;
   for (i = 0; i < n; i++) {
      fl_u32 l;
      if (pos+4 > (fl_u64)len) return -1;
      l = fl_get_le32(p+pos);
      pos += 4;
      if (l > (fl_u64)len-pos) return -1;
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

/* Source: an opened input that decodes step by step into a sink. */
typedef struct fl_source fl_source;
struct fl_source {
   fl_format fmt;
   /* decodes the next part: 1, 0 at the end, or an FL_ERR_* code */
   int (*step)(fl_source *s, fl_sink *sink);
   /* moves to a frame at or before frame and returns it in *at; NULL if not possible */
   int (*seek)(fl_source *s, fl_i64 frame, fl_i64 *at);
   void (*close)(fl_source *s);
   int end_status;  /* FL_ERR_CHECKSUM after a FLAC file read to the end whose MD5 differs */
};

/* Default WAVE_FORMAT_EXTENSIBLE channel masks (FLAC channel order). */
static fl_u32 fl_default_mask(int channels) {
   static const fl_u32 masks[8] = {4, 3, 7, 0x33, 0x607, 0x60F, 0x70F, 0x63F};
   return channels >= 1 && channels <= 8 ? masks[channels-1] : 0;
}

/* ---- WAV input: RIFF, RF64/BW64 (EBU Tech 3306) and Sony Wave64 ---- */
static const unsigned char fl_w64_riff[16] = {'r', 'i', 'f', 'f', 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0, 0};
/* Wave64 GUIDs of "wave", "fmt " and "data": the FourCC followed by these 12 bytes */
static const unsigned char fl_w64_tail[12] = {0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};

static fl_u64 fl_get_le64(const unsigned char *p) { return (fl_u64)fl_get_le32(p+4)<<32|fl_get_le32(p); }

/* 1 if the buffer starts a WAV, RF64/BW64 or Wave64 file. */
static int fl_is_wav(const unsigned char *b, size_t n) {
   if (n >= 12 && (memcmp(b, "RIFF", 4) == 0 || memcmp(b, "RF64", 4) == 0 || memcmp(b, "BW64", 4) == 0) &&
       memcmp(b+8, "WAVE", 4) == 0) return 1;
   return n >= 40 && memcmp(b, fl_w64_riff, 16) == 0 && memcmp(b+24, "wave", 4) == 0 && memcmp(b+28, fl_w64_tail, 12) == 0;
}

typedef struct {
   fl_source base;
   fl_inbuf *in;
   unsigned char *buf;
   fl_i32 *pcm;
   int bytes, C, shift;
   fl_i64 left, total, data_start;  /* frames left and in total (-1 if unknown) */
} fl_wav_src;

static int fl_wav_step(fl_source *s, fl_sink *sink) {
   fl_wav_src *w = (fl_wav_src*)s;
   int want = 4096, n, i, bytes = w->bytes, C = w->C, ret;
   if (w->left >= 0 && want > w->left) want = (int)w->left;
   n = want > 0 ? (int)(fl_inbuf_read(w->in, w->buf, (size_t)want*bytes*C)/((size_t)bytes*C)) : 0;
   for (i = 0; i < n*C; i++) {
      const unsigned char *p = w->buf+(size_t)i*bytes;
      fl_i32 v;
      if (bytes == 1) v = (int)p[0]-128;
      else if (bytes == 2) v = (fl_i16)(p[0]|(p[1]<<8));
      else if (bytes == 3) v = (fl_i32)(((fl_u32)p[0]<<8)|((fl_u32)p[1]<<16)|((fl_u32)p[2]<<24))>>8;
      else v = (fl_i32)fl_get_le32(p);
      if (w->shift) {
         if (v&((1<<w->shift)-1)) return FL_ERR_BAD_FILE;  /* samples use more bits than the header says */
         v >>= w->shift;
      }
      w->pcm[i] = v;
   }
   if (n > 0 && (ret = sink->write(sink, w->pcm, n)) != FL_OK) return ret;
   if (w->left >= 0) w->left -= n;
   return n < want || want == 0 ? 0 : 1;
}

static int fl_wav_seek(fl_source *s, fl_i64 frame, fl_i64 *at) {
   fl_wav_src *w = (fl_wav_src*)s;
   if (!w->in->seekable && !w->in->eof) return FL_ERR_UNSUPPORTED;
   if (w->total >= 0 && frame > w->total) frame = w->total;
   if (fl_inbuf_seek(w->in, w->data_start+frame*w->bytes*w->C) != 0) return FL_ERR_IO;
   w->left = w->total >= 0 ? w->total-frame : -1;
   *at = frame;
   return FL_OK;
}

static void fl_wav_close(fl_source *s) {
   fl_wav_src *w = (fl_wav_src*)s;
   fl_format_free(&w->base.fmt);
   free(w->buf);
   free(w->pcm);
   free(w);
}

/* Opens WAV input: the header is read up to the samples. */
static int fl_wav_open(fl_inbuf *in, fl_source **out) {
   unsigned char hdr[40], ck[24], fmt[40];
   int have_fmt = 0, ret, bytes = 0, C = 0, shift = 0, w64, rf64;
   fl_i64 data_len = -1, ds64_data = -1;
   fl_wav_src *w = (fl_wav_src*)calloc(1, sizeof(*w));
   fl_format f;
   *out = NULL;
   if (!w) return FL_ERR_ALLOC;
   fl_format_init(&f);
   ret = FL_ERR_BAD_FILE;
   if (fl_inbuf_need(in, 40) < 0) {
      ret = FL_ERR_ALLOC;
      goto fail;
   }
   if (!fl_is_wav(in->buf+in->pos, in->len-in->pos)) goto fail;
   w64 = memcmp(in->buf+in->pos, fl_w64_riff, 16) == 0;
   rf64 = !w64 && memcmp(in->buf+in->pos, "RIFF", 4) != 0;
   fl_inbuf_read(in, hdr, w64 ? 40 : 12);
   for (;;) {
      fl_u64 len;
      int pad;
      if (w64) {
         if (fl_inbuf_read(in, ck, 24) != 24) goto fail;
         len = fl_get_le64(ck+16);
         if (len < 24) goto fail;
         len -= 24;
         pad = (int)((8-len%8)%8);
         if (memcmp(ck+4, fl_w64_tail, 12) != 0) memset(ck, 0, 4);  /* not a known chunk */
      } else {
         if (fl_inbuf_read(in, ck, 8) != 8) goto fail;
         len = fl_get_le32(ck+4);
         pad = (int)(len&1);
      }
      if (memcmp(ck, "fmt ", 4) == 0) {
         int format, n = len < 40 ? (int)len : 40;
         if (len < 16) goto fail;
         memset(fmt, 0, sizeof(fmt));
         if (fl_inbuf_read(in, fmt, n) != (size_t)n) goto fail;
         if (len > 40) fl_inbuf_skip(in, len-40);
         fl_inbuf_skip(in, pad);
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
            goto fail;
         }
         if (f.is_float) f.bits = 32;
         shift = f.container_bits-f.bits;
         bytes = f.container_bits/8;
         have_fmt = 1;
      } else if (memcmp(ck, "data", 4) == 0) {
         if (!have_fmt) goto fail;
         if (rf64 && len == 0xFFFFFFFFu) data_len = ds64_data;
         else if (w64 || (len != 0xFFFFFFFFu && len != 0)) data_len = (fl_i64)len;
         break;
      } else if (rf64 && memcmp(ck, "ds64", 4) == 0 && len >= 28) {
         unsigned char ds[28];
         if (fl_inbuf_read(in, ds, 28) != 28) goto fail;
         ds64_data = (fl_i64)FL_MIN(fl_get_le64(ds+8), (fl_u64)1<<62);
         fl_inbuf_skip(in, len-28+pad);
      } else {
         fl_inbuf_skip(in, len+pad);
      }
   }
   if (C < 1 || C > 255 || f.rate <= 0 || f.rate > FL_MAX_RATE) {
      ret = FL_ERR_UNSUPPORTED;
      goto fail;
   }
   if (f.channel_mask == fl_default_mask(C)) f.channel_mask = 0;
   f.channels = C;
   f.frames = data_len >= 0 ? data_len/((fl_i64)bytes*C) : -1;
   w->buf = (unsigned char*)malloc((size_t)4096*bytes*C);
   w->pcm = (fl_i32*)malloc(sizeof(fl_i32)*4096*C);
   if (!w->buf || !w->pcm) {
      ret = FL_ERR_ALLOC;
      goto fail;
   }
   w->in = in;
   w->bytes = bytes;
   w->C = C;
   w->shift = shift;
   w->left = w->total = f.frames;
   w->data_start = fl_inbuf_tell(in);
   w->base.fmt = f;
   w->base.step = fl_wav_step;
   w->base.seek = fl_wav_seek;
   w->base.close = fl_wav_close;
   *out = &w->base;
   return FL_OK;
fail:
   fl_format_free(&f);
   free(w->buf);
   free(w->pcm);
   free(w);
   return ret;
}

/* ---- WAV output ----
   Opus sources get opusdec's header (16-bit, WAVE_FORMAT_EXTENSIBLE for family 1 with
   3..8 channels); other sources get flac's (extensible above 2 channels or 16 bits).
   Data that does not fit in 4 GB gets an RF64 header (EBU Tech 3306). */
typedef struct {
   fl_sink base;
   fl_out *f;
   int C, rate, bits, container, ext, is_float, hdr_len, rf64;
   fl_u32 mask;
   fl_i64 written;
   unsigned char *buf;
   size_t cap;
} fl_wav_sink;

/* Largest data size of a plain RIFF header. */
static fl_u64 fl_wav_plain_max(const fl_wav_sink *w) { return 0xFFFFFFFFu-28-(w->ext ? 40 : 16); }

static void fl_put_le64(unsigned char *p, fl_u64 v) {
   fl_put_le32(p, (fl_u32)v);
   fl_put_le32(p+4, (fl_u32)(v>>32));
}

static int fl_wav_header(fl_wav_sink *w, fl_u64 data_bytes) {
   static const unsigned char pcm_guid[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
   unsigned char h[68+36], *q = h;
   int bpf = w->container/8*w->C, fmt = w->ext ? 40 : 16, n = 28+fmt+(w->rf64 ? 36 : 0);
   fl_u32 db = data_bytes > 0xFFFFFFFFu-n ? 0xFFFFFFFFu-n : (fl_u32)data_bytes;
   if (w->rf64) {
      /* RF64 with a ds64 chunk; the 32-bit sizes are -1 */
      memcpy(h, "RF64", 4);
      fl_put_le32(h+4, 0xFFFFFFFFu);
      memcpy(h+8, "WAVEds64", 8);
      fl_put_le32(h+16, 28);
      fl_put_le64(h+20, data_bytes+(data_bytes&1)+n-8);
      fl_put_le64(h+28, data_bytes);
      fl_put_le64(h+36, bpf ? data_bytes/bpf : 0);
      fl_put_le32(h+44, 0);
      q = h+36;
      db = 0xFFFFFFFFu;
   } else {
      memcpy(h, "RIFF", 4);
      fl_put_le32(h+4, db+(db&1)+n-8);  /* with the pad byte after odd data, as flac */
      memcpy(h+8, "WAVE", 4);
   }
   memcpy(q+12, "fmt ", 4);
   fl_put_le32(q+16, fmt);
   fl_put_le16(q+20, w->ext ? 0xFFFE : w->is_float ? 3 : 1);
   fl_put_le16(q+22, w->C);
   fl_put_le32(q+24, w->rate);
   fl_put_le32(q+28, (fl_u32)bpf*w->rate);
   fl_put_le16(q+32, bpf);
   fl_put_le16(q+34, w->container);
   if (w->ext) {
      fl_put_le16(q+36, 22);
      fl_put_le16(q+38, w->bits);
      fl_put_le32(q+40, w->mask);
      memcpy(q+44, pcm_guid, 16);
   }
   memcpy(h+n-8, "data", 4);
   fl_put_le32(h+n-4, db);
   w->hdr_len = n;
   return fl_out_write(w->f, h, n) == (size_t)n ? FL_OK : FL_ERR_IO;
}

/* Moves everything after a plain header 36 bytes on, for an RF64 header (data that was
   not known to exceed 4 GB in advance). The file is open for reading as well. */
static int fl_wav_make_room(fl_wav_sink *w, fl_u64 end) {
   size_t chunk = 1<<20;
   unsigned char *b = (unsigned char*)malloc(chunk);
   fl_u64 pos = end;
   if (!b) return FL_ERR_ALLOC;
   while (pos > (fl_u64)w->hdr_len) {
      size_t k = (size_t)FL_MIN((fl_u64)chunk, pos-w->hdr_len);
      pos -= k;
      if (fl_out_seek(w->f, (fl_i64)pos) != 0 || fl_out_read(w->f, b, k) != k ||
          fl_out_seek(w->f, (fl_i64)pos+36) != 0 || fl_out_write(w->f, b, k) != k) {
         free(b);
         return FL_ERR_IO;
      }
   }
   free(b);
   return FL_OK;
}

/* Data size for the first header: 0 for files (rewritten in finish), but stdout cannot
   seek back, so it gets the exact length, or the maximum if that is unknown. */
static fl_u64 fl_wav_start_size(fl_wav_sink *w, const fl_format *f) {
   fl_i64 frames = fl_exact_frames(f);
   if (w->f->seekable) return 0;
   return frames >= 0 ? (fl_u64)frames*w->C*(w->container/8) : ~(fl_u64)0;
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
      if (fl_exact_frames(f) >= 0) w->rf64 = (fl_u64)fl_exact_frames(f)*w->C*4 > fl_wav_plain_max(w);
      return fl_wav_header(w, fl_wav_start_size(w, f));
   }
   w->container = (f->bits+7)/8*8;
   if (f->opus_family >= 0) {
      /* opusdec's header for 16 bits; above 16 bits always extensible */
      int surround = f->opus_family == 1 && w->C >= 3 && w->C <= 8;
      w->ext = surround || w->bits > 16;
      w->mask = surround ? opus_masks[w->C-1] : w->ext ? fl_default_mask(w->C) : 0;
   } else {
      w->ext = w->C > 2 || w->bits > 16 || w->bits != w->container;
      w->mask = f->channel_mask ? f->channel_mask : fl_default_mask(w->C);
   }
   if (fl_exact_frames(f) >= 0) w->rf64 = (fl_u64)fl_exact_frames(f)*w->C*(w->container/8) > fl_wav_plain_max(w);
   return fl_wav_header(w, fl_wav_start_size(w, f));
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
   if (fl_out_write(w->f, w->buf, need) != need) return FL_ERR_IO;
   w->written += n;
   return FL_OK;
}

static int fl_wav_finish(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   fl_u64 data = (fl_u64)w->written*w->C*(w->container/8);
   int ret = FL_OK;
   if (data&1) ret = fl_out_write(w->f, "", 1) == 1 ? FL_OK : FL_ERR_IO;
   if (w->f->seekable) {  /* stdout keeps the header from start() */
      if (ret == FL_OK && !w->rf64 && data > fl_wav_plain_max(w)) {
         ret = fl_wav_make_room(w, w->hdr_len+data+(data&1));
         w->rf64 = 1;
      }
      if (ret == FL_OK) ret = fl_out_seek(w->f, 0) == 0 ? fl_wav_header(w, data) : FL_ERR_IO;
   }
   if (fl_out_close(w->f) != FL_OK && ret == FL_OK) ret = FL_ERR_IO;
   w->f = NULL;
   return ret;
}

static void fl_wav_destroy(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   fl_out_close(w->f);
   free(w->buf);
   free(w);
}

static fl_sink *fl_wav_sink_create(fl_out *out, int *err) {
   fl_wav_sink *w = (fl_wav_sink*)calloc(1, sizeof(*w));
   if (!w) {
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   w->f = out;
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
