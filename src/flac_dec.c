/* ---- FLAC decoder (RFC 9639): native FLAC and Ogg FLAC ---- */

#define FL_FLAC_MAX_BLOCK 65535
#define FL_FLAC_MAX_FRAME (1<<22)  /* worst case frame (65535 samples, 8 x 33-bit verbatim) fits */

typedef struct {
   int min_block, max_block, rate, channels, bits;
   fl_u32 min_frame, max_frame;
   fl_u64 total;
   unsigned char md5[16];
} fl_streaminfo;

/* CRC-8 (polynomial 0x07) and CRC-16 (0x8005) of the FLAC frames; constant, so that
   decoders and encoders in several threads share them safely. */
static const unsigned char fl_crc8_tab[256] = {
   0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
   0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
   0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
   0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
   0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
   0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
   0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
   0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
   0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
   0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
   0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c, 0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
   0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
   0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
   0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
   0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
   0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3};
static const fl_u16 fl_crc16_tab[256] = {
   0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011, 0x8033, 0x0036, 0x003c, 0x8039,
   0x0028, 0x802d, 0x8027, 0x0022, 0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d, 0x8077, 0x0072,
   0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041, 0x80c3, 0x00c6, 0x00cc, 0x80c9,
   0x00d8, 0x80dd, 0x80d7, 0x00d2, 0x00f0, 0x80f5, 0x80ff, 0x00fa, 0x80eb, 0x00ee, 0x00e4, 0x80e1,
   0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4, 0x80b1, 0x8093, 0x0096, 0x009c, 0x8099,
   0x0088, 0x808d, 0x8087, 0x0082, 0x8183, 0x0186, 0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192,
   0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab, 0x01ae, 0x01a4, 0x81a1, 0x01e0, 0x81e5, 0x81ef, 0x01ea,
   0x81fb, 0x01fe, 0x01f4, 0x81f1, 0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2,
   0x0140, 0x8145, 0x814f, 0x014a, 0x815b, 0x015e, 0x0154, 0x8151, 0x8173, 0x0176, 0x017c, 0x8179,
   0x0168, 0x816d, 0x8167, 0x0162, 0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132,
   0x0110, 0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101, 0x8303, 0x0306, 0x030c, 0x8309,
   0x0318, 0x831d, 0x8317, 0x0312, 0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324, 0x8321,
   0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371, 0x8353, 0x0356, 0x035c, 0x8359,
   0x0348, 0x834d, 0x8347, 0x0342, 0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db, 0x03de, 0x03d4, 0x83d1,
   0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2, 0x83a3, 0x03a6, 0x03ac, 0x83a9,
   0x03b8, 0x83bd, 0x83b7, 0x03b2, 0x0390, 0x8395, 0x839f, 0x039a, 0x838b, 0x038e, 0x0384, 0x8381,
   0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e, 0x0294, 0x8291, 0x82b3, 0x02b6, 0x02bc, 0x82b9,
   0x02a8, 0x82ad, 0x82a7, 0x02a2, 0x82e3, 0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2,
   0x02d0, 0x82d5, 0x82df, 0x02da, 0x82cb, 0x02ce, 0x02c4, 0x82c1, 0x8243, 0x0246, 0x024c, 0x8249,
   0x0258, 0x825d, 0x8257, 0x0252, 0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261,
   0x0220, 0x8225, 0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231, 0x8213, 0x0216, 0x021c, 0x8219,
   0x0208, 0x820d, 0x8207, 0x0202};

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

/* Decodes one subframe of n samples with bps bits into x. Returns 0 or -1. Samples
   outside bps bits make the subframe invalid, which also keeps the sums in range. */
#define FL_OUT(v) ((v) < lo || (v) > hi)
static int fl_flac_subframe(fl_br *b, fl_i64 *x, int n, int bps) {
   int type, wasted = 0, i, j;
   fl_i64 lo, hi;
   if (fl_br_bits(b, 1)) return -1;
   type = (int)fl_br_bits(b, 6);
   if (fl_br_bits(b, 1)) {
      fl_i64 w = fl_br_unary(b);
      if (w < 0 || w+1 >= bps) return -1;
      wasted = (int)w+1;
      bps -= wasted;
   }
   hi = ((fl_i64)1<<(bps-1))-1;
   lo = -hi-1;
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
      case 0: for (i = 0; i < n; i++) if (FL_OUT(x[i])) return -1; break;
      case 1: for (i = 1; i < n; i++) { x[i] += x[i-1]; if (FL_OUT(x[i])) return -1; } break;
      case 2: for (i = 2; i < n; i++) { x[i] += 2*x[i-1]-x[i-2]; if (FL_OUT(x[i])) return -1; } break;
      case 3: for (i = 3; i < n; i++) { x[i] += 3*(x[i-1]-x[i-2])+x[i-3]; if (FL_OUT(x[i])) return -1; } break;
      default: for (i = 4; i < n; i++) { x[i] += 4*(x[i-1]+x[i-3])-6*x[i-2]-x[i-4]; if (FL_OUT(x[i])) return -1; } break;
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
         if (FL_OUT(x[i])) return -1;
      }
   } else return -1;
   if (wasted)
      for (i = 0; i < n; i++) x[i] = (fl_i64)((fl_u64)x[i]<<wasted);
   return 0;
}
#undef FL_OUT

typedef struct {
   fl_streaminfo si;
   int cap;  /* largest block the buffers hold */
   fl_i64 *ch[8];
   fl_i32 *out;
   fl_u64 first;  /* first sample of the last decoded frame */
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
   d->first = h.variable ? h.number : h.number*(fl_u64)d->si.max_block;
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

/* Checks STREAMINFO and fills the stream format. */
static int fl_flac_prepare(fl_flac_dec *d, fl_format *f) {
   fl_streaminfo *si = &d->si;
   if (si->channels < 1 || si->bits < 4 || si->rate < 1 || si->max_block < 16) return FL_ERR_BAD_FILE;
   if (fl_flac_dec_alloc(d, FL_FLAC_MAX_BLOCK) < 0) return FL_ERR_ALLOC;
   f->channels = si->channels;
   f->rate = si->rate;
   f->bits = f->container_bits = si->bits;
   f->frames = si->total ? (fl_i64)si->total : -1;
   if (f->channel_mask == fl_default_mask(f->channels)) f->channel_mask = 0;
   return FL_OK;
}

static int fl_md5_check(fl_md5 *m, const fl_streaminfo *si) {
   static const unsigned char zero[16] = {0};
   unsigned char sum[16];
   fl_md5_final(m, sum);
   return memcmp(si->md5, zero, 16) == 0 || memcmp(si->md5, sum, 16) == 0 ? FL_OK : FL_ERR_CHECKSUM;
}

/* FLAC source, native or Ogg (first logical stream, mapping version 1). */
typedef struct {
   fl_source base;
   fl_inbuf *in;
   fl_flac_dec d;
   fl_md5 md5;
   int check;            /* read from the start, so the MD5 is checked */
   fl_i64 frames_off;    /* native: offset of the first frame */
   int ogg, ogg_stop;
   fl_ogg_reader rd;
   fl_i64 ogg_start;
} fl_flac_src;

static int fl_flac_frame_out(fl_flac_src *s, fl_sink *sink, int n) {
   if (s->check) fl_md5_samples(&s->md5, s->d.out, (size_t)n*s->d.si.channels, s->d.si.bits);
   return sink->write(sink, s->d.out, n);
}

static void fl_flac_end(fl_flac_src *s) {
   s->base.end_status = s->check ? fl_md5_check(&s->md5, &s->d.si) : FL_OK;
}

static int fl_flac_step(fl_source *src, fl_sink *sink) {
   fl_flac_src *s = (fl_flac_src*)src;
   fl_inbuf *in = s->in;
   size_t used;
   int n, ret;
   if (fl_inbuf_need(in, FL_FLAC_MAX_FRAME) < 0) return FL_ERR_ALLOC;
   if (in->pos >= in->len) {
      fl_flac_end(s);
      return 0;
   }
   used = fl_flac_decode_frame(&s->d, in->buf+in->pos, in->len-in->pos, &n);
   if (used == 0) {
      /* not a frame: trailing junk (tags) is ignored, a valid frame later means damage */
      size_t k;
      for (k = in->pos+1; k+1 < in->len; k++)
         if (in->buf[k] == 0xFF && (in->buf[k+1]&0xFE) == 0xF8 && fl_flac_decode_frame(&s->d, in->buf+k, in->len-k, &n))
            return FL_ERR_BAD_FILE;
      in->pos = in->len;
      fl_flac_end(s);
      return 0;
   }
   in->pos += used;
   if ((ret = fl_flac_frame_out(s, sink, n)) != FL_OK) return ret;
   return 1;
}

/* The first valid frame within 256 KB from off: its offset and first sample. */
static int fl_flac_find(fl_flac_src *s, fl_i64 off, fl_i64 *at, fl_i64 *first) {
   fl_inbuf *in = s->in;
   size_t r;
   int n;
   if (fl_inbuf_seek(in, off) != 0 || fl_inbuf_need(in, 1<<18) < 0) return 0;
   for (r = 0; r < (1<<18) && in->pos+r+1 < in->len; r++) {
      const unsigned char *p = in->buf+in->pos+r;
      if (p[0] != 0xFF || (p[1]&0xFE) != 0xF8) continue;
      if (!fl_flac_decode_frame(&s->d, p, in->len-in->pos-r, &n)) {
         /* a large frame may need more input */
         if (in->len-in->pos-r >= FL_FLAC_MAX_FRAME || in->eof) continue;
         if (fl_inbuf_need(in, r+FL_FLAC_MAX_FRAME) < 0) return 0;
         if (!fl_flac_decode_frame(&s->d, in->buf+in->pos+r, in->len-in->pos-r, &n)) continue;
      }
      *at = fl_inbuf_tell(in)+(fl_i64)r;
      *first = (fl_i64)s->d.first;
      return 1;
   }
   return 0;
}

/* Bisection over the file for a frame that starts at or before frame, within 64 KB of
   the one that holds it (the rest is decoded and dropped by the caller). */
static int fl_flac_seek(fl_source *src, fl_i64 frame, fl_i64 *at) {
   fl_flac_src *s = (fl_flac_src*)src;
   fl_i64 lo = s->frames_off, lo_first = 0, hi = -1, step = 1<<20, off, first;
   int i;
   if (!s->in->seekable && !s->in->eof) return FL_ERR_UNSUPPORTED;
   for (i = 0; i < 100 && (hi < 0 || hi-lo > 1<<16); i++) {
      fl_i64 probe = hi < 0 ? lo+step : lo+(hi-lo)/2;
      if (fl_flac_find(s, probe, &off, &first) && first <= frame && off >= probe) {
         lo = off;
         lo_first = first;
         step *= 2;
      } else hi = probe;
   }
   if (fl_inbuf_seek(s->in, lo) != 0) return FL_ERR_IO;
   s->check = 0;
   *at = lo_first;
   return FL_OK;
}

/* Ogg FLAC: reads header packets until the first frame, which stays queued. */
static int fl_oggflac_headers(fl_flac_src *s, fl_format *f, int parse) {
   fl_ogg_queue *q = &s->rd.q;
   int more;
   do {
      if ((more = fl_ogg_page(&s->rd)) < 0) return more;
      for (; q->first < q->count; q->first++) {
         const fl_ogg_packet *pk = &q->pk[q->first];
         const unsigned char *p = pk->data;
         int len = pk->len, ret;
         if (pk->link > 0) {
            s->ogg_stop = 1;  /* only the first link */
            return FL_OK;
         }
         if (pk->idx == 0) {
            if (len < 51 || memcmp(p+9, "fLaC", 4) != 0) return FL_ERR_BAD_FILE;
            if (parse) fl_parse_streaminfo(p+17, &s->d.si);
            continue;
         }
         if (len > 0 && p[0] == 0xFF) return FL_OK;  /* the first frame */
         if (len < 4) return FL_ERR_BAD_FILE;
         if (parse && (ret = fl_flac_metadata(f, &s->d.si, p[0]&127, p+4, len-4)) != FL_OK) return ret;
      }
      fl_ogg_compact(q);
   } while (more);
   return s->rd.links == 0 ? FL_ERR_BAD_FILE : FL_OK;
}

static int fl_oggflac_step(fl_source *src, fl_sink *sink) {
   fl_flac_src *s = (fl_flac_src*)src;
   fl_ogg_queue *q = &s->rd.q;
   int more = 1, ret, n;
   if (q->first >= q->count && !s->ogg_stop && (more = fl_ogg_page(&s->rd)) < 0) return more;
   for (; q->first < q->count && !s->ogg_stop; q->first++) {
      const fl_ogg_packet *pk = &q->pk[q->first];
      const unsigned char *p = pk->data;
      int len = pk->len;
      if (pk->link > 0) {
         s->ogg_stop = 1;
         break;
      }
      if (pk->idx == 0) return FL_ERR_BAD_FILE;
      if (len > 0 && p[0] != 0xFF) {
         /* header packet: one metadata block */
         if (len < 4) return FL_ERR_BAD_FILE;
         if ((ret = fl_flac_metadata(&s->base.fmt, &s->d.si, p[0]&127, p+4, len-4)) != FL_OK) return ret;
         continue;
      }
      if (fl_flac_decode_frame(&s->d, p, len, &n) == 0) return FL_ERR_BAD_FILE;
      if ((ret = fl_flac_frame_out(s, sink, n)) != FL_OK) return ret;
   }
   fl_ogg_compact(q);
   if (s->ogg_stop || (!more && q->first >= q->count)) {
      fl_flac_end(s);
      return 0;
   }
   return 1;
}

/* Ogg FLAC seeks by reading again from the start. */
static int fl_oggflac_seek(fl_source *src, fl_i64 frame, fl_i64 *at) {
   fl_flac_src *s = (fl_flac_src*)src;
   int ret;
   (void)frame;
   if (!s->in->seekable && !s->in->eof) return FL_ERR_UNSUPPORTED;
   if (fl_inbuf_seek(s->in, s->ogg_start) != 0) return FL_ERR_IO;
   fl_ogg_free(&s->rd);
   fl_ogg_init(&s->rd, s->in, "\177FLAC");
   s->ogg_stop = 0;
   if ((ret = fl_oggflac_headers(s, NULL, 0)) != FL_OK) return ret;
   s->check = 0;
   *at = 0;
   return FL_OK;
}

static void fl_flac_close(fl_source *src) {
   fl_flac_src *s = (fl_flac_src*)src;
   fl_ogg_free(&s->rd);
   fl_flac_dec_free(&s->d);
   fl_format_free(&s->base.fmt);
   free(s);
}

/* Opens FLAC or Ogg FLAC input: the metadata is read up to the first frame. */
static int fl_flac_open(fl_inbuf *in, fl_source **out) {
   fl_flac_src *s = (fl_flac_src*)calloc(1, sizeof(*s));
   fl_format *f;
   int ret = FL_ERR_BAD_FILE, last = 0;
   *out = NULL;
   if (!s) return FL_ERR_ALLOC;
   f = &s->base.fmt;
   fl_format_init(f);
   fl_md5_init(&s->md5);
   s->in = in;
   s->check = 1;
   s->base.step = fl_flac_step;
   s->base.seek = fl_flac_seek;
   s->base.close = fl_flac_close;
   if (fl_inbuf_need(in, 1<<16) < 0) {
      ret = FL_ERR_ALLOC;
      goto fail;
   }
   if (in->len-in->pos >= 4 && memcmp(in->buf+in->pos, "OggS", 4) == 0) {
      s->ogg = 1;
      s->ogg_start = fl_inbuf_tell(in);
      s->base.step = fl_oggflac_step;
      s->base.seek = fl_oggflac_seek;
      fl_ogg_init(&s->rd, in, "\177FLAC");
      if ((ret = fl_oggflac_headers(s, f, 1)) != FL_OK || (ret = fl_flac_prepare(&s->d, f)) != FL_OK) goto fail;
      *out = &s->base;
      return FL_OK;
   }
   /* skip ID3v2 tags */
   while (in->len-in->pos >= 10 && memcmp(in->buf+in->pos, "ID3", 3) == 0) {
      const unsigned char *p = in->buf+in->pos;
      size_t skip = 10+((size_t)(p[6]&127)<<21|(p[7]&127)<<14|(p[8]&127)<<7|(p[9]&127))+(p[5]&0x10 ? 10 : 0);
      if (fl_inbuf_need(in, skip+4) < 0 || in->len-in->pos < skip) goto fail;
      in->pos += skip;
   }
   if (in->len-in->pos < 8 || memcmp(in->buf+in->pos, "fLaC", 4) != 0) goto fail;
   in->pos += 4;
   while (!last) {
      const unsigned char *p;
      long len;
      int type;
      if (fl_inbuf_need(in, 4) < 0 || in->len-in->pos < 4) goto fail;
      p = in->buf+in->pos;
      last = p[0]>>7;
      type = p[0]&127;
      len = (long)p[1]<<16|p[2]<<8|p[3];
      if (fl_inbuf_need(in, 4+(size_t)len) < 0) {
         ret = FL_ERR_ALLOC;
         goto fail;
      }
      if (in->len-in->pos < 4+(size_t)len) goto fail;
      if ((ret = fl_flac_metadata(f, &s->d.si, type, in->buf+in->pos+4, len)) != FL_OK) goto fail;
      in->pos += 4+len;
      ret = FL_ERR_BAD_FILE;
   }
   if ((ret = fl_flac_prepare(&s->d, f)) != FL_OK) goto fail;
   s->frames_off = fl_inbuf_tell(in);
   *out = &s->base;
   return FL_OK;
fail:
   fl_flac_close(&s->base);
   return ret;
}

/* ---- Public FLAC frame decoder ---- */
struct fl_flac_frame_decoder {
   fl_flac_dec d;
};

FLDEF fl_flac_frame_decoder *fl_flac_frame_decoder_create(const unsigned char info[34], int *error) {
   fl_flac_frame_decoder *dec;
   fl_format f;
   int err;
   if (error) *error = FL_ERR_BAD_ARG;
   if (!info) return NULL;
   if (!(dec = (fl_flac_frame_decoder*)calloc(1, sizeof(*dec)))) {
      if (error) *error = FL_ERR_ALLOC;
      return NULL;
   }
   fl_parse_streaminfo(info, &dec->d.si);
   fl_format_init(&f);
   if ((err = fl_flac_prepare(&dec->d, &f)) != FL_OK) {
      fl_flac_frame_decoder_destroy(dec);
      if (error) *error = err;
      return NULL;
   }
   if (error) *error = FL_OK;
   return dec;
}

FLDEF int fl_flac_frame_decode(fl_flac_frame_decoder *dec, const unsigned char *data, int len, int *pcm, int max_frames, int *used) {
   int n;
   size_t k;
   if (!data || len <= 0 || !pcm) return FL_ERR_BAD_ARG;
   if (!(k = fl_flac_decode_frame(&dec->d, data, (size_t)len, &n))) return FL_ERR_BAD_FILE;
   if (n > max_frames) return FL_ERR_BAD_ARG;
   memcpy(pcm, dec->d.out, sizeof(fl_i32)*n*dec->d.si.channels);
   if (used) *used = (int)k;
   return n;
}

FLDEF void fl_flac_frame_decoder_destroy(fl_flac_frame_decoder *dec) {
   if (!dec) return;
   fl_flac_dec_free(&dec->d);
   free(dec);
}
