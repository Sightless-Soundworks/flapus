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
   if (o->flac_seektable & ~1) return FL_ERR_BAD_ARG;
   if (o->serialno < -1 || o->serialno > 4294967295LL) return FL_ERR_BAD_ARG;
   return FL_OK;
}

/* Buffered input that keeps at least the requested number of bytes in memory. The input
   is opened once, so stdin works as well: format detection looks at the buffer. */
typedef struct {
   FILE *f;
   unsigned char *buf;
   size_t cap, pos, len;
   int eof;
} fl_inbuf;

static int fl_inbuf_open(fl_inbuf *b, const char *path) {
   memset(b, 0, sizeof(*b));
   return (b->f = fl_fopen(path, "rb")) ? FL_OK : FL_ERR_IO;
}

static void fl_inbuf_close(fl_inbuf *b) {
   if (b->f) fl_fclose(b->f);
   free(b->buf);
   memset(b, 0, sizeof(*b));
}

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

/* Reads up to n bytes into dst, returns the number read. */
static size_t fl_inbuf_read(fl_inbuf *b, void *dst, size_t n) {
   size_t k = FL_MIN(n, b->len-b->pos);
   if (k) memcpy(dst, b->buf+b->pos, k);
   b->pos += k;
   if (k < n && !b->eof) {
      size_t r = fread((unsigned char*)dst+k, 1, n-k, b->f);
      if (r < n-k) b->eof = 1;
      k += r;
   }
   return k;
}

/* Skips n bytes: seeks in files, reads through stdin (pipes cannot seek). */
static void fl_inbuf_skip(fl_inbuf *b, fl_u64 n) {
   unsigned char tmp[4096];
   size_t k = (size_t)FL_MIN(n, b->len-b->pos);
   b->pos += k;
   n -= k;
   if (!n || b->eof || (b->f != stdin && n <= LONG_MAX && fseek(b->f, (long)n, SEEK_CUR) == 0)) return;
   while (n > 0 && !b->eof) n -= fl_inbuf_read(b, tmp, (size_t)FL_MIN(n, sizeof(tmp)));
}

/* Reads the rest of the input into one block that the caller frees (like a whole file). */
static unsigned char *fl_inbuf_take(fl_inbuf *b, long *size) {
   unsigned char *p;
   while (!b->eof)
      if (fl_inbuf_need(b, b->cap ? 2*b->cap : 65536) < 0) return NULL;
   if (!b->buf && !(b->buf = (unsigned char*)malloc(1))) return NULL;
   memmove(b->buf, b->buf+b->pos, b->len-b->pos);
   *size = (long)(b->len-b->pos);
   p = b->buf;
   b->buf = NULL;
   b->cap = b->pos = b->len = 0;
   return p;
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
static int fl_run_wav(fl_inbuf *in, fl_sink *sink) {
   unsigned char hdr[12], ck[8], fmt[40], *buf = NULL;
   fl_i32 *pcm = NULL;
   int have_fmt = 0, ret, bytes = 0, C = 0, shift = 0, unsigned8 = 0;
   fl_i64 left = -1;
   fl_format f;
   fl_format_init(&f);
   ret = FL_ERR_BAD_FILE;
   if (fl_inbuf_read(in, hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr+8, "WAVE", 4) != 0) goto done;
   for (;;) {
      fl_u32 len;
      if (fl_inbuf_read(in, ck, 8) != 8) goto done;
      len = fl_get_le32(ck+4);
      if (memcmp(ck, "fmt ", 4) == 0) {
         int format, n = len < 40 ? (int)len : 40;
         if (len < 16) goto done;
         memset(fmt, 0, sizeof(fmt));
         if (fl_inbuf_read(in, fmt, n) != (size_t)n) goto done;
         if (len > 40) fl_inbuf_skip(in, len-40);
         if (len&1) fl_inbuf_skip(in, 1);
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
         fl_inbuf_skip(in, len+(len&1));
      }
   }
   if (C < 1 || C > 255 || f.rate <= 0 || f.rate > FL_MAX_RATE) {
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
      n = want > 0 ? (int)(fl_inbuf_read(in, buf, (size_t)want*bytes*C)/((size_t)bytes*C)) : 0;
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

/* Data size for the first header: 0 for files (rewritten in finish), but stdout cannot
   seek back, so it gets the exact length, or the maximum if that is unknown. */
static fl_u64 fl_wav_start_size(fl_wav_sink *w, const fl_format *f) {
   fl_i64 frames = fl_exact_frames(f);
   if (w->f != stdout) return 0;
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
   if (fwrite(w->buf, 1, need, w->f) != need) return FL_ERR_IO;
   w->written += n;
   return FL_OK;
}

static int fl_wav_finish(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   fl_u64 data = (fl_u64)w->written*w->C*(w->container/8);
   int ret = FL_OK;
   if (data&1) ret = fputc(0, w->f) == EOF ? FL_ERR_IO : FL_OK;
   if (w->f != stdout) {  /* stdout keeps the header from start() */
      fseek(w->f, 0, SEEK_SET);
      if (ret == FL_OK) ret = fl_wav_header(w, data);
   }
   if (fl_fclose(w->f) != 0 && ret == FL_OK) ret = FL_ERR_IO;
   w->f = NULL;
   return ret;
}

static void fl_wav_destroy(fl_sink *s) {
   fl_wav_sink *w = (fl_wav_sink*)s;
   if (w->f) fl_fclose(w->f);
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
