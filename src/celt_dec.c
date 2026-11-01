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
