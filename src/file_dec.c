/* ---- Ogg demuxing, speex-style resampler, dither and WAV output (opusdec behaviour) ---- */

typedef struct {
   unsigned char *data;
   int len;
   fl_i64 granule;  /* granule of the page the packet ends on, -1 if none, -2 if damaged */
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
      {
         /* -1: no packet ends here; -2: damaged (beyond 2^62) */
         fl_u64 g = 0;
         for (i = 7; i >= 0; i--) g = (g<<8)|buf[pos+6+i];
         granule = g == ~(fl_u64)0 ? -1 : g < (fl_u64)1<<62 ? (fl_i64)g : -2;
      }
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
/* speex_resampler_init(channels, in, out, 5) followed by skip_zeros(). */
static int fl_resampler_init(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   fl_u32 i;
   float cutoff;
   int j, ret;
   if ((ret = fl_resampler_alloc(r, channels, in_rate, out_rate)) != FL_OK) return ret;
   if (r->num_rate > r->den_rate) cutoff = 0.922f*r->den_rate/r->num_rate;
   else cutoff = 0.940f;
   for (i = 0; i < r->den_rate; i++)
      for (j = 0; j < (int)r->filt_len; j++)
         r->sinc_table[i*r->filt_len+j] = fl_rs_sinc(cutoff, ((j-(fl_i32)r->filt_len/2+1)-((float)i)/r->den_rate), r->filt_len);
   return FL_OK;
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
static int fl_run_opus(fl_inbuf *in, int rate, int dither, fl_sink *sink) {
   static const int wav_permute[8][8] = {
      {0}, {0, 1}, {0, 2, 1}, {0, 1, 2, 3}, {0, 2, 1, 3, 4}, {0, 2, 1, 4, 5, 3},
      {0, 2, 1, 5, 6, 4, 3}, {0, 2, 1, 6, 7, 4, 5, 3}};
   long size;
   unsigned char *file = fl_inbuf_take(in, &size);
   fl_ogg_stream *links = NULL;
   int nlinks = 0, li, i, ret = FL_ERR_BAD_FILE, force_stereo = 0, C, maxC = 2, permute = 0;
   fl_opus_head *heads = NULL;
   fl_opus_ms_decoder *dec = NULL;
   fl_writer *w = NULL;
   float *pcm = NULL, *tmp = NULL;
   unsigned char channel_map[8];
   fl_format fmt;
   fl_format_init(&fmt);
   if (!file) return FL_ERR_ALLOC;
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
               /* clamped, so that damaged granules cannot overflow */
               total = FL_MIN(total+FL_MAX(0, links[li].pk[i].granule-heads[li].pre_skip), (fl_i64)1<<50);
               break;
            }
      fmt.frames = total/48000*rate+total%48000*rate/48000;
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
         if (page_gp < -1) {
            ret = FL_ERR_BAD_FILE;
            goto done;
         }
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
                  if ((ret = fl_resampler_init(&w->rs, C, 48000, rate)) != FL_OK) goto done;
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
