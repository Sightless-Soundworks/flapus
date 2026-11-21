/* ---- Ogg demuxing, speex-style resampler, dither and WAV output (opusdec behaviour) ---- */

/* ---- Streaming Ogg reader ----
   Follows one logical stream per link: in each group of BOS pages the first stream whose
   first packet starts with magic (like opusfile); other streams are skipped. Completed
   packets are appended to a queue. A page's granule belongs to the last packet completed
   on it, if the page ends at a packet boundary; an EOS page marks the last completed
   packet. Only that packet can still change, so the consumer leaves it in the queue until
   a later packet completes or its link has ended. */
typedef struct {
   unsigned char *data;
   int len, idx, link;  /* idx: packet number within its link */
   fl_i64 granule;      /* granule of the page the packet ends on, -1 if none, -2 if damaged */
   fl_i64 page_end;     /* with a granule: offset after that page */
   int eos;
} fl_ogg_packet;

typedef struct {
   fl_ogg_packet *pk;
   int count, cap, first;  /* pk[first..count-1] are not consumed yet */
} fl_ogg_queue;

typedef struct {
   fl_inbuf *in;
   const char *magic;
   fl_u32 serial;
   int have_serial, prev_bos, links, npk, ended;
   unsigned char *cur;
   int cur_len;
   fl_ogg_queue q;
   fl_i64 link_start;   /* offset of the page that started the last link */
} fl_ogg_reader;

static void fl_ogg_init(fl_ogg_reader *r, fl_inbuf *in, const char *magic) {
   memset(r, 0, sizeof(*r));
   r->in = in;
   r->magic = magic;
}

/* Frees the consumed packets and moves the rest to the front. */
static void fl_ogg_compact(fl_ogg_queue *q) {
   int i;
   for (i = 0; i < q->first; i++) free(q->pk[i].data);
   if (q->count > q->first) memmove(q->pk, q->pk+q->first, sizeof(*q->pk)*(q->count-q->first));
   q->count -= q->first;
   q->first = 0;
}

static void fl_ogg_free(fl_ogg_reader *r) {
   r->q.first = r->q.count;
   fl_ogg_compact(&r->q);
   free(r->q.pk);
   free(r->cur);
   memset(&r->q, 0, sizeof(r->q));
   r->cur = NULL;
}

static int fl_ogg_add_packet(fl_ogg_reader *r) {
   fl_ogg_queue *q = &r->q;
   if (q->count == q->cap) {
      int cap = q->cap ? 2*q->cap : 64;
      fl_ogg_packet *t = (fl_ogg_packet*)realloc(q->pk, cap*sizeof(*q->pk));
      if (!t) return -1;
      q->pk = t;
      q->cap = cap;
   }
   q->pk[q->count].data = r->cur;
   q->pk[q->count].len = r->cur_len;
   q->pk[q->count].idx = r->npk++;
   q->pk[q->count].link = r->links-1;
   q->pk[q->count].granule = -1;
   q->pk[q->count].eos = 0;
   q->count++;
   r->cur = NULL;
   r->cur_len = 0;
   return 0;
}

/* Reads the next page of the followed stream into the queue. Returns 1, or 0 at the end of
   the input (or of the readable part of a damaged file), or FL_ERR_ALLOC. */
static int fl_ogg_page(fl_ogg_reader *r) {
   fl_inbuf *b = r->in;
   int mlen = (int)strlen(r->magic);
   for (;;) {
      const unsigned char *h;
      int nseg, i, hdr, bos, body;
      fl_i64 granule;
      fl_u32 ser;
      if (r->ended) return 0;
      if (fl_inbuf_need(b, 27) < 0) return FL_ERR_ALLOC;
      if (b->len-b->pos < 27) break;
      h = b->buf+b->pos;
      if (memcmp(h, "OggS", 4) != 0) {
         b->pos++;
         continue;
      }
      nseg = h[26];
      hdr = 27+nseg;
      if (fl_inbuf_need(b, hdr+255*255) < 0) return FL_ERR_ALLOC;
      h = b->buf+b->pos;
      if (b->len-b->pos < (size_t)hdr) break;
      {
         fl_u64 g = 0;
         for (i = 7; i >= 0; i--) g = (g<<8)|h[6+i];
         granule = g == ~(fl_u64)0 ? -1 : g < (fl_u64)1<<62 ? (fl_i64)g : -2;
      }
      ser = h[14]|(h[15]<<8)|(h[16]<<16)|((fl_u32)h[17]<<24);
      bos = (h[5]&2) != 0;
      body = hdr;
      if (bos) {
         /* a new group of BOS pages starts a new link */
         if (!r->prev_bos) r->have_serial = 0;
         if (!r->have_serial && nseg > 0 && h[27] >= mlen && b->len-b->pos >= (size_t)(hdr+mlen) &&
             memcmp(h+hdr, r->magic, mlen) == 0) {
            r->serial = ser;
            r->have_serial = 1;
            r->link_start = fl_inbuf_tell(b);
            r->links++;
            r->npk = 0;
            free(r->cur);
            r->cur = NULL;
            r->cur_len = 0;
         }
      }
      r->prev_bos = bos;
      if (!r->have_serial || ser != r->serial) {
         fl_i64 skip = 0;
         for (i = 0; i < nseg; i++) skip += h[27+i];
         fl_inbuf_skip(b, (fl_u64)(hdr+skip));
         continue;
      }
      for (i = 0; i < nseg; i++) {
         int l = h[27+i];
         unsigned char *t;
         if (b->len-b->pos < (size_t)(body+l)) {
            /* truncated page: the end of the readable input */
            r->ended = 1;
            return 0;
         }
         if (!(t = (unsigned char*)realloc(r->cur, r->cur_len+l+1))) return FL_ERR_ALLOC;
         r->cur = t;
         memcpy(r->cur+r->cur_len, h+body, l);
         r->cur_len += l;
         body += l;
         if (l < 255 && fl_ogg_add_packet(r) < 0) return FL_ERR_ALLOC;
      }
      if (r->npk > 0 && r->q.count > 0 && r->q.pk[r->q.count-1].link == r->links-1) {
         fl_ogg_packet *last = &r->q.pk[r->q.count-1];
         if (r->cur_len == 0) {
            last->granule = granule;
            last->eos = (h[5]&4) != 0;
            last->page_end = fl_inbuf_tell(b)+body;
         } else if (h[5]&4) last->eos = 1;
      }
      b->pos += body;
      return 1;
   }
   r->ended = 1;
   return 0;
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
   fl_i32 last_sample[255];
   fl_u32 samp_frac_num[255];
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
static float fl_rs_sinc(float cutoff, float x, int N, double (*sin_fn)(double)) {
   float xx = x*cutoff;
   if (fabs(x) < 1e-6) return cutoff;
   else if (fabs(x) > .5*N) return 0;
   return cutoff*sin_fn(3.14159265358979323846*xx)/(3.14159265358979323846*xx)*fl_rs_compute_func(fabs(2.*x/N), fl_kaiser10_table, 32);
}
static fl_u32 fl_gcd(fl_u32 a, fl_u32 b) {
   while (b != 0) {
      fl_u32 t = a%b;
      a = b;
      b = t;
   }
   return a;
}
/* fl_resampler_init() without the sinc table, which the caller fills. Returns FL_OK,
   FL_ERR_ALLOC, or FL_ERR_UNSUPPORTED if the table would exceed 2^24 entries (rates
   with a small common divisor, such as 1048573 to 48000 Hz). */
static int fl_resampler_alloc(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   fl_u32 fact = fl_gcd(in_rate, out_rate);
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
      r->filt_len = remain*r->num_rate/r->den_rate+major*r->num_rate;
      r->filt_len = ((r->filt_len-1)&(~0x7))+8;
   }
   if ((fl_u64)r->filt_len*r->den_rate > (1<<24)) return FL_ERR_UNSUPPORTED;
   r->sinc_table = (float*)malloc(sizeof(float)*r->filt_len*r->den_rate);
   r->mem_alloc_size = r->filt_len-1+160;
   r->mem = (float*)calloc((size_t)channels*r->mem_alloc_size, sizeof(float));
   if (!r->sinc_table || !r->mem) return FL_ERR_ALLOC;
   for (j = 0; j < channels; j++) r->last_sample[j] = r->filt_len/2;
   return FL_OK;
}
/* speex_resampler_init(channels, in, out, 5) followed by skip_zeros(), with sin_fn as sin(). */
static int fl_resampler_init_sin(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate, double (*sin_fn)(double)) {
   fl_u32 i;
   float cutoff;
   int j, ret;
   if ((ret = fl_resampler_alloc(r, channels, in_rate, out_rate)) != FL_OK) return ret;
   if (r->num_rate > r->den_rate) cutoff = 0.922f*r->den_rate/r->num_rate;
   else cutoff = 0.940f;
   for (i = 0; i < r->den_rate; i++)
      for (j = 0; j < (int)r->filt_len; j++)
         r->sinc_table[i*r->filt_len+j] = fl_rs_sinc(cutoff, ((j-(fl_i32)r->filt_len/2+1)-((float)i)/r->den_rate), r->filt_len, sin_fn);
   return FL_OK;
}
static int fl_resampler_init(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   return fl_resampler_init_sin(r, channels, in_rate, out_rate, sin);
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

/* Converts one chunk and passes it on: 16-bit with soft clip and dither as in opusdec,
   24/32-bit with soft clip and rounding, or float as decoded (opusdec --float). */
static int fl_emit_chunk(fl_sink *sink, float *pcm, int n, int C, float *clipmem, fl_shapestate *ss, int dither,
                         int bits, fl_i64 *written) {
   short *out;
   fl_i32 *v;
   int i, ret;
   if (n > 0 && bits) fl_pcm_soft_clip(pcm, n, C, clipmem);
   out = (short*)malloc(sizeof(short)*(n > 0 ? n*C : 1));
   v = (fl_i32*)malloc(sizeof(fl_i32)*(n > 0 ? n*C : 1));
   if (!out || !v) {
      free(out);
      free(v);
      return FL_ERR_ALLOC;
   }
   if (bits == 0) memcpy(v, pcm, sizeof(float)*n*C);
   else if (bits > 16) {
      double scale = bits == 32 ? 2147483648.0 : 8388608.0;
      for (i = 0; i < n*C; i++) v[i] = (fl_i32)lrint(FL_MAX(-scale, FL_MIN(pcm[i]*scale, scale-1)));
   } else {
      if (dither) fl_shape_dither_toshort(ss, out, pcm, n, C);
      else for (i = 0; i < n*C; i++) out[i] = (short)fl_float2int(FL_MAX(-32768, FL_MIN(pcm[i]*32768.f, 32767)));
      for (i = 0; i < n*C; i++) v[i] = out[i];
   }
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
   int C, rate, dither, bits;  /* bits: 16, 24, 32, or 0 for float */
   fl_resampler rs;
   int have_rs;
   float clipmem[8];
   fl_shapestate ss;
   fl_i64 link_read, link_out, written;
   float buf[1024*8];
} fl_opus_out;

static int fl_audio_write(fl_opus_out *w, float *pcm, int frame_size) {
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
         int r = fl_emit_chunk(w->sink, output, out_len, C, w->clipmem, &w->ss, w->dither, w->bits, &w->written);
         if (r != FL_OK) return r;
         w->link_out += w->written-before;
         maxout -= w->written-before;
      } else {
         /* dither state is still updated for empty chunks */
         if (w->dither && w->bits == 16) {
            short dummy[1];
            fl_shape_dither_toshort(&w->ss, dummy, output, 0, C);
         }
      }
   } while (frame_size > 0 && maxout > 0);
   return FL_OK;
}

/* Drains the resampler with filt_len/2 zeros (opusdec drain_resampler). */
static int fl_drain_resampler(fl_opus_out *w) {
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

/* Opus file source (opusdec/opusfile behaviour). The file is read twice: first for the
   link headers, lengths and a seek index, then to decode. */
typedef struct {
   fl_i64 granule, offset;  /* a page that ends a packet group: its granule and the offset after it */
   int link;
} fl_opus_mark;

typedef struct {
   fl_source base;
   fl_inbuf *in;
   fl_ogg_reader rd;
   fl_opus_head *heads;
   fl_i64 *last_gp;       /* last granule of an audio packet per link, -1 if none */
   fl_i64 *link_off;      /* offset of the first page of each link */
   fl_u32 *serial;
   int *npk;              /* packets per link */
   int nlinks, cap;
   fl_opus_mark *marks;
   int nmarks, mcap;
   unsigned char *tags;   /* OpusTags of the first link */
   int tags_len, head_err, more;
   /* decoding */
   fl_opus_ms_decoder *dec;
   fl_opus_out *w;
   float *pcm, *tmp;
   unsigned char channel_map[8];
   int C, rate, force_stereo, permute, link, cur_discard, started, drained;
   fl_i64 prev_gp;
} fl_opus_file;

static int fl_opus_grow(void **p, int *cap, int need, size_t size) {
   if (need > *cap) {
      int cap2 = 2*need+16;
      void *t = realloc(*p, size*cap2);
      if (!t) return -1;
      *p = t;
      *cap = cap2;
   }
   return 0;
}

/* Pass 1: collects headers, lengths and seek marks from one packet. */
static int fl_opus_scan_packet(fl_opus_file *o, const fl_ogg_packet *p) {
   int li = p->link;
   if (li >= o->cap) {
      int cap = 2*li+4;
      fl_opus_head *h = (fl_opus_head*)realloc(o->heads, sizeof(*h)*cap);
      fl_i64 *g, *lo;
      fl_u32 *se;
      int *n;
      if (h) o->heads = h;
      g = (fl_i64*)realloc(o->last_gp, sizeof(*g)*cap);
      if (g) o->last_gp = g;
      lo = (fl_i64*)realloc(o->link_off, sizeof(*lo)*cap);
      if (lo) o->link_off = lo;
      se = (fl_u32*)realloc(o->serial, sizeof(*se)*cap);
      if (se) o->serial = se;
      n = (int*)realloc(o->npk, sizeof(*n)*cap);
      if (n) o->npk = n;
      if (!h || !g || !lo || !se || !n) return FL_ERR_ALLOC;
      o->cap = cap;
   }
   while (o->nlinks <= li) {
      o->last_gp[o->nlinks] = -1;
      o->link_off[o->nlinks] = o->rd.link_start;
      o->serial[o->nlinks] = o->rd.serial;
      o->npk[o->nlinks++] = 0;
   }
   o->npk[li]++;
   if (p->idx == 0) {
      int r = fl_parse_opus_head(p->data, p->len, &o->heads[li]);
      if (r < 0 && !o->head_err) o->head_err = r == FL_ERR_UNSUPPORTED ? r : FL_ERR_BAD_FILE;
   } else if (p->idx == 1) {
      if ((p->len < 8 || memcmp(p->data, "OpusTags", 8) != 0) && !o->head_err) o->head_err = FL_ERR_BAD_FILE;
      if (li == 0) {
         if (!(o->tags = (unsigned char*)malloc(p->len+1))) return FL_ERR_ALLOC;
         memcpy(o->tags, p->data, p->len);
         o->tags_len = p->len;
      }
   } else if (p->granule >= 0) {
      o->last_gp[li] = p->granule;
      if (fl_opus_grow((void**)&o->marks, &o->mcap, o->nmarks+1, sizeof(*o->marks)) < 0) return FL_ERR_ALLOC;
      o->marks[o->nmarks].granule = p->granule;
      o->marks[o->nmarks].offset = p->page_end;
      o->marks[o->nmarks++].link = li;
   }
   return FL_OK;
}

/* Starts decoding link li. */
static int fl_opus_link_start(fl_opus_file *o, int li) {
   fl_opus_head *head = &o->heads[li];
   o->link = li;
   o->cur_discard = head->pre_skip;
   o->started = 0;
   o->prev_gp = -1;
   fl_opus_ms_decoder_destroy(o->dec);
   o->dec = fl_opus_ms_decoder_create(48000, head->channels, head->streams, head->coupled, head->mapping, NULL);
   if (!o->dec) return FL_ERR_BAD_FILE;
   fl_opus_ms_decoder_set_gain(o->dec, head->gain);
   return FL_OK;
}

/* Decodes the packets completed on one page (pk[0..n-1], the last one carries the
   granule) or the packets left at the end of a link. */
static int fl_opus_group(fl_opus_file *o, const fl_ogg_packet *pk, int n) {
   fl_opus_out *w = o->w;
   int HC = o->heads[o->link].channels, C = o->C, k, total = 0, eos_trim = 0, ret;
   fl_i64 page_gp = pk[n-1].granule, gp;
   for (k = 0; k < n; k++) {
      int d = fl_packet_duration(pk[k].data, pk[k].len);
      total += d > 0 ? d : 0;
   }
   if (page_gp < -1) return FL_ERR_BAD_FILE;
   if (o->prev_gp == -1) {
      /* first audio page; if it is also the last one and holds more audio than its
         granule position, the stream starts at 0 and the end is trimmed (opusfile) */
      o->prev_gp = page_gp-total;
      if (o->prev_gp < 0) {
         if (!pk[n-1].eos) return FL_ERR_BAD_FILE;
         o->prev_gp = 0;
      }
   }
   if (pk[n-1].eos && page_gp-o->prev_gp < total) eos_trim = 1;
   else o->prev_gp = page_gp-total;
   gp = o->prev_gp;
   for (k = 0; k < n; k++) {
      int dur = fl_packet_duration(pk[k].data, pk[k].len), trimmed, got, skip;
      float *out = o->pcm, *pcm = o->pcm;
      if (dur <= 0) continue;
      got = fl_opus_ms_decode_float(o->dec, pk[k].data, pk[k].len, pcm, 5760);
      if (got < 0) return FL_ERR_BAD_FILE;
      trimmed = dur;
      if (eos_trim) {
         fl_i64 left = page_gp-gp;
         trimmed = left <= 0 ? 0 : (int)FL_MIN(left, dur);
      }
      gp += dur;
      skip = FL_MIN(trimmed, o->cur_discard);
      o->cur_discard -= skip;
      trimmed -= skip;
      if (trimmed <= 0) continue;
      if (skip > 0) memmove(pcm, pcm+skip*HC, sizeof(float)*trimmed*HC);
      if (o->force_stereo) fl_downmix_stereo(pcm, trimmed, HC);
      if (o->permute) {
         int j, c;
         for (j = 0; j < trimmed; j++)
            for (c = 0; c < C; c++) o->tmp[j*C+c] = pcm[j*C+o->channel_map[c]];
         out = o->tmp;
      }
      if (!o->started) {
         /* new link: drain and drop the resampler, restart the per-link counters */
         if (w->have_rs) {
            if ((ret = fl_drain_resampler(w)) != FL_OK) return ret;
            fl_resampler_free(&w->rs);
            w->have_rs = 0;
         }
         w->link_read = w->link_out = 0;
         if (o->rate != 48000) {
            if ((ret = fl_resampler_init(&w->rs, C, 48000, o->rate)) != FL_OK) return ret;
            w->have_rs = 1;
         }
         o->started = 1;
      }
      w->link_read += trimmed;
      if ((ret = fl_audio_write(w, out, trimmed)) != FL_OK) return ret;
   }
   o->prev_gp = page_gp;
   return FL_OK;
}

/* Pass 2: decodes the complete groups in the queue; with done set (no more pages of this
   link will come) also the rest. A group whose last packet is the latest one read can
   still get a granule or EOS flag, so it waits. */
static int fl_opus_drain_queue(fl_opus_file *o, int more) {
   fl_ogg_reader *rd = &o->rd;
   fl_ogg_queue *q = &rd->q;
   while (q->first < q->count) {
      fl_ogg_packet *p = &q->pk[q->first];
      int L = p->link, k = q->first, ret, done = !more || rd->links-1 > L;
      if (L != o->link && (ret = fl_opus_link_start(o, L)) != FL_OK) return ret;
      if (p->idx < 2) {
         q->first++;  /* OpusHead and OpusTags */
         continue;
      }
      while (k < q->count && q->pk[k].link == L && q->pk[k].granule == -1) k++;
      if (k < q->count && q->pk[k].link == L) {
         if (!done && k == q->count-1) break;
      } else {
         if (!done) break;
         k--;  /* the rest of the link */
      }
      if ((ret = fl_opus_group(o, p, k-q->first+1)) != FL_OK) return ret;
      q->first = k+1;
   }
   fl_ogg_compact(q);
   return FL_OK;
}

static int fl_opus_step(fl_source *s, fl_sink *sink) {
   fl_opus_file *o = (fl_opus_file*)s;
   int ret;
   o->w->sink = sink;
   if (o->drained) return 0;
   if ((o->more = fl_ogg_page(&o->rd)) < 0) return o->more;
   if ((ret = fl_opus_drain_queue(o, o->more)) != FL_OK) return ret;
   if (o->more) return 1;
   if (o->w->have_rs && (ret = fl_drain_resampler(o->w)) != FL_OK) return ret;
   o->drained = 1;
   return 0;
}

/* Output frames of the links before li (approximate, like the length). */
static fl_i64 fl_opus_link_frames(const fl_opus_file *o, int li) {
   fl_i64 t = 0, g;
   int i;
   /* clamped, so that damaged granules cannot overflow */
   for (i = 0; i < li; i++)
      if ((g = o->last_gp[i]) >= 0) t = FL_MIN(t+FL_MAX(0, g-o->heads[i].pre_skip), (fl_i64)1<<50);
   return t/48000*o->rate+t%48000*o->rate/48000;
}

/* Seeks with the marks of pass 1: decoding restarts at a page at least 80 ms (3840
   samples at 48 kHz) before the target, with a new decoder and resampler. */
static int fl_opus_seek(fl_source *s, fl_i64 frame, fl_i64 *at) {
   fl_opus_file *o = (fl_opus_file*)s;
   fl_opus_out *w = o->w;
   fl_ogg_reader *rd = &o->rd;
   int li, m, best = -1, ret;
   fl_i64 g, start;
   for (li = 0; li+1 < o->nlinks && fl_opus_link_frames(o, li+1) <= frame; li++);
   start = fl_opus_link_frames(o, li);
   g = o->heads[li].pre_skip+((frame-start)/o->rate*48000+(frame-start)%o->rate*48000/o->rate);
   for (m = 0; m < o->nmarks; m++)
      if (o->marks[m].link == li && o->marks[m].granule <= g-3840) best = m;
   if (w->have_rs) fl_resampler_free(&w->rs);
   w->have_rs = 0;
   fl_ogg_free(rd);
   fl_ogg_init(rd, o->in, "OpusHead");
   o->drained = 0;
   o->link = -1;
   if (best < 0) {
      /* from the start of the link */
      if (fl_inbuf_seek(o->in, o->link_off[li]) != 0) return FL_ERR_IO;
      rd->links = li;
      *at = start;
      return FL_OK;
   }
   if (fl_inbuf_seek(o->in, o->marks[best].offset) != 0) return FL_ERR_IO;
   /* continue the link as if its earlier pages had been read */
   rd->links = li+1;
   rd->have_serial = 1;
   rd->serial = o->serial[li];
   rd->npk = 2;
   if ((ret = fl_opus_link_start(o, li)) != FL_OK) return ret;
   o->prev_gp = o->marks[best].granule;
   o->cur_discard = (int)FL_MAX(0, o->heads[li].pre_skip-o->prev_gp);
   o->started = 1;
   w->link_read = FL_MAX(0, o->prev_gp-o->heads[li].pre_skip);
   if (o->rate != 48000) {
      /* the new resampler starts where an output sample falls exactly on an input one */
      fl_i64 step = 48000/fl_gcd(48000, (fl_u32)o->rate), extra = (step-w->link_read%step)%step;
      o->cur_discard += (int)extra;
      w->link_read += extra;
   }
   w->link_out = w->link_read/48000*o->rate+w->link_read%48000*o->rate/48000;
   if (o->rate != 48000) {
      if ((ret = fl_resampler_init(&w->rs, o->C, 48000, o->rate)) != FL_OK) return ret;
      w->have_rs = 1;
   }
   *at = start+w->link_out;
   return FL_OK;
}

static void fl_opus_close(fl_source *s) {
   fl_opus_file *o = (fl_opus_file*)s;
   fl_ogg_free(&o->rd);
   fl_format_free(&o->base.fmt);
   if (o->w) {
      if (o->w->have_rs) fl_resampler_free(&o->w->rs);
      free(o->w);
   }
   fl_opus_ms_decoder_destroy(o->dec);
   free(o->pcm);
   free(o->tmp);
   free(o->heads);
   free(o->last_gp);
   free(o->link_off);
   free(o->serial);
   free(o->npk);
   free(o->marks);
   free(o->tags);
   free(o);
}

/* Opens Ogg Opus input: opt->rate (0 = the original rate), opt->dither, opt->bits and
   opt->as_float. Pass 1 runs here. */
static int fl_opus_open(fl_inbuf *in, const fl_options *opt, fl_source **out) {
   static const int wav_permute[8][8] = {
      {0}, {0, 1}, {0, 2, 1}, {0, 1, 2, 3}, {0, 2, 1, 3, 4}, {0, 2, 1, 4, 5, 3},
      {0, 2, 1, 5, 6, 4, 3}, {0, 2, 1, 6, 7, 4, 5, 3}};
   fl_opus_file *o = (fl_opus_file*)calloc(1, sizeof(*o));
   fl_opus_out *w;
   fl_format *fmt;
   int li, i, ret = FL_ERR_BAD_FILE, maxC = 2, more, rate = opt->rate;
   fl_i64 start = fl_inbuf_tell(in);
   *out = NULL;
   if (!o) return FL_ERR_ALLOC;
   fmt = &o->base.fmt;
   fl_format_init(fmt);
   o->in = in;
   o->link = -1;
   o->base.step = fl_opus_step;
   o->base.seek = fl_opus_seek;
   o->base.close = fl_opus_close;
   fl_ogg_init(&o->rd, in, "OpusHead");
   if (fl_inbuf_make_seekable(in) < 0) {
      ret = FL_ERR_ALLOC;
      goto fail;
   }
   /* pass 1 */
   do {
      fl_ogg_queue *q = &o->rd.q;
      int lim;
      if ((more = fl_ogg_page(&o->rd)) < 0) {
         ret = more;
         goto fail;
      }
      lim = more ? q->count-1 : q->count;
      for (i = q->first; i < lim; i++)
         if ((ret = fl_opus_scan_packet(o, &q->pk[i])) != FL_OK) goto fail;
      q->first = FL_MAX(q->first, lim);
      fl_ogg_compact(q);
   } while (more);
   ret = FL_ERR_BAD_FILE;
   if (o->rd.links == 0 || o->nlinks < o->rd.links) goto fail;
   for (li = 0; li < o->nlinks; li++)
      if (o->npk[li] < 2) goto fail;
   if (o->head_err) {
      ret = o->head_err;
      goto fail;
   }
   fl_ogg_free(&o->rd);
   for (li = 0; li < o->nlinks; li++) {
      if (o->heads[li].channels != o->heads[0].channels) o->force_stereo = 1;
      maxC = FL_MAX(maxC, o->heads[li].channels);
   }
   if (rate == 0) {
      rate = o->heads[0].input_sample_rate ? (int)o->heads[0].input_sample_rate : 48000;
      for (li = 1; li < o->nlinks; li++)
         if ((o->heads[li].input_sample_rate ? (int)o->heads[li].input_sample_rate : 48000) != rate) rate = 48000;
   }
   if (rate < 8000 || rate > 192000) rate = 48000;
   o->rate = rate;
   o->C = o->force_stereo ? 2 : o->heads[0].channels;
   if (o->C > 8) {
      ret = FL_ERR_UNSUPPORTED;  /* output state holds at most 8 channels */
      goto fail;
   }
   o->w = w = (fl_opus_out*)calloc(1, sizeof(*w));
   o->pcm = (float*)malloc(sizeof(float)*5760*maxC);
   o->tmp = (float*)malloc(sizeof(float)*5760*o->C);
   if (!w || !o->pcm || !o->tmp) {
      ret = FL_ERR_ALLOC;
      goto fail;
   }
   /* WAV channel order for family 1 (opusdec adjust_wav_mapping) */
   o->permute = o->heads[0].mapping_family == 1 && !o->force_stereo && (o->C == 3 || o->C > 4);
   if (o->permute)
      for (i = 0; i < o->C; i++) o->channel_map[wav_permute[o->C-1][i]] = (unsigned char)i;
   w->C = o->C;
   w->rate = rate;
   w->dither = opt->dither;
   w->bits = opt->as_float ? 0 : opt->bits ? opt->bits : 16;
   w->ss.fs = rate;
   w->ss.mute = 960;
   w->ss.rngseed = 22222;
   fmt->channels = o->C;
   fmt->rate = rate;
   fmt->bits = fmt->container_bits = opt->as_float ? 32 : opt->bits ? opt->bits : 16;
   fmt->is_float = opt->as_float;
   fmt->opus_family = o->force_stereo ? 0 : o->heads[0].mapping_family;
   /* approximate length (for progress): last granule minus pre-skip of every link */
   fmt->frames = fl_opus_link_frames(o, o->nlinks);
   fl_parse_comments(fmt, o->tags+8, o->tags_len-8);
   /* pass 2 starts */
   if (fl_inbuf_seek(in, start) != 0) {
      ret = FL_ERR_IO;
      goto fail;
   }
   fl_ogg_init(&o->rd, in, "OpusHead");
   *out = &o->base;
   return FL_OK;
fail:
   fl_opus_close(&o->base);
   return ret;
}
