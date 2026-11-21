/* ---- Opus encoder front end: libopusenc buffering/resampling, Ogg output, opusenc main loop ---- */

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

/* ---- libopusenc resampler: fl_resampler with the sinc table computed on the x87 ---- */
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

static int fl_ope_rs_init(fl_resampler *r, int channels, fl_u32 in_rate, fl_u32 out_rate) {
   fl_u32 i;
   float cutoff;
   int j, ret;
   if ((ret = fl_resampler_alloc(r, channels, in_rate, out_rate)) != FL_OK) return ret;
   if (r->num_rate > r->den_rate) cutoff = (float)(XF(0.922f)*XF((double)r->den_rate)/XF((double)r->num_rate));
   else cutoff = 0.940f;
   for (i = 0; i < r->den_rate; i++)
      for (j = 0; j < (int)r->filt_len; j++)
         r->sinc_table[i*r->filt_len+j] = fl_ope_sinc(cutoff, (int)i, r->den_rate, j-(fl_i32)r->filt_len/2+1, r->filt_len);
   return FL_OK;
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
   fl_i64 written;  /* bytes written (ftell does not work on pipes) */
   int error;
} fl_oggp;

/* Ogg page CRC (polynomial 0x04c11db7) bit by bit: no table that threads would share,
   and the pages of an Opus file are small. */
static fl_u32 fl_ogg_crc(fl_u32 crc, const unsigned char *p, size_t n) {
   int k;
   while (n--) {
      crc ^= (fl_u32)*p++<<24;
      for (k = 0; k < 8; k++) crc = (crc&0x80000000U) ? (crc<<1)^0x04c11db7U : crc<<1;
   }
   return crc;
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
      crc = fl_ogg_crc(crc, h, hs);
      crc = fl_ogg_crc(crc, o->buf+p->buf_pos, p->buf_size);
      fl_put_le32(h+22, crc);
      if (fwrite(h, 1, hs, o->out) != hs || fwrite(o->buf+p->buf_pos, 1, p->buf_size, o->out) != p->buf_size) o->error = 1;
      o->written += hs+p->buf_size;
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
   fl_resampler re;
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
         fl_resampler_process(&e->re, pcm, &in_samples, &e->buffer[C*e->buffer_end], &out_samples);
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
         fl_resampler_process(&e->re, &e->lpc_buffer[OPE_LPC_INPUT*C], &in_samples, &e->buffer[C*e->buffer_end], &out_samples);
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
   /* below 1000 Hz the resampler drain would not fit in the buffer */
   if (C < 1 || C > 255 || f->rate < 1000 || f->rate > FL_MAX_RATE) return FL_ERR_UNSUPPORTED;
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
      if ((ret = fl_ope_rs_init(&e->re, C, f->rate, 48000)) != FL_OK) goto done;
      e->lpc_buffer = (float*)calloc((OPE_LPC_INPUT+OPE_LPC_PADDING)*C, sizeof(float));
      if (!e->lpc_buffer) {
         ret = FL_ERR_ALLOC;
         goto done;
      }
   }
   fl_msenc_setup(e->st, opt, IMAX(8, IMIN(24, f->container_bits)));
   e->frame_size_request = fl_msenc_get_duration(e->st);
   e->frame_size = fl_frame_samples(e->frame_size_request);
   e->ogg.out = fl_fopen(o->path, "wb");
   e->ogg.serialno = opt->serialno >= 0 ? (fl_i32)(fl_u32)opt->serialno : (fl_i32)fl_random32(e);
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
   o->base.out_bytes = o->e->ogg.written;
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
   o->base.out_bytes = o->e->ogg.written;  /* the drained pages count for the final ratio */
   ret = o->e->ogg.error ? FL_ERR_IO : FL_OK;
   if (fl_fclose(o->e->ogg.out) != 0) ret = FL_ERR_IO;
   o->e->ogg.out = NULL;
   return ret;
}

static void fl_opus_destroy(fl_sink *s) {
   fl_opus_sink *o = (fl_opus_sink*)s;
   fl_ope *e = o->e;
   if (e) {
      if (e->ogg.out) fl_fclose(e->ogg.out);
      if (e->st) fl_msenc_destroy(e->st);
      if (e->have_re) fl_resampler_free(&e->re);
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
   if (channels < 1 || channels > 255 || fl_check_options(&opt) != FL_OK) return NULL;
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
