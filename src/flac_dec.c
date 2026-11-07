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

/* Decodes a FLAC or Ogg FLAC file and passes the samples to sink. */
static int fl_run_flac(fl_inbuf *b, fl_sink *sink) {
   fl_inbuf in = *b;  /* handed back at the end */
   fl_flac_dec d;
   fl_format f;
   fl_md5 md5;
   int ret = FL_ERR_BAD_FILE, last = 0;
   memset(&d, 0, sizeof(d));
   fl_format_init(&f);
   fl_flac_crc_init();
   fl_md5_init(&md5);
   if (fl_inbuf_need(&in, 1<<16) < 0) {
      ret = FL_ERR_ALLOC;
      goto done;
   }
   if (in.len-in.pos >= 4 && memcmp(in.buf+in.pos, "OggS", 4) == 0) {
      long size;
      unsigned char *file = fl_inbuf_take(&in, &size);
      ret = file ? fl_run_ogg_flac(file, size, sink) : FL_ERR_ALLOC;
      free(file);
      goto done;
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
   *b = in;  /* the caller closes the input */
   fl_flac_dec_free(&d);
   fl_format_free(&f);
   return ret;
}
