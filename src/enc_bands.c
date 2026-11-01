/* ---- CELT encoder: band quantization, PVQ, pitch and prefilter (x87 rounding) ---- */
#include <emmintrin.h>
#include <stddef.h>

/* SSE inner product; the scalar tail runs on the x87 stack. */
static xf fl_e_inner_prod(const float *x, const float *y, int N) {
   float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
   xf xy;
   int i;
   for (i = 0; i < N-3; i += 4) {
      s0 = s0+x[i]*y[i];
      s1 = s1+x[i+1]*y[i+1];
      s2 = s2+x[i+2]*y[i+2];
      s3 = s3+x[i+3]*y[i+3];
   }
   xy = (s0+s2)+(s1+s3);
   for (; i < N; i++) xy += XF(x[i])*y[i];
   return xy;
}
static void fl_e_dual_inner_prod(const float *x, const float *y1, const float *y2, int N, float *xy1, float *xy2) {
   float a0 = 0, a1 = 0, a2 = 0, a3 = 0, b0 = 0, b1 = 0, b2 = 0, b3 = 0;
   int i;
   for (i = 0; i < N-3; i += 4) {
      a0 = a0+x[i]*y1[i]; a1 = a1+x[i+1]*y1[i+1]; a2 = a2+x[i+2]*y1[i+2]; a3 = a3+x[i+3]*y1[i+3];
      b0 = b0+x[i]*y2[i]; b1 = b1+x[i+1]*y2[i+1]; b2 = b2+x[i+2]*y2[i+2]; b3 = b3+x[i+3]*y2[i+3];
   }
   *xy1 = (a0+a2)+(a1+a3);
   *xy2 = (b0+b2)+(b1+b3);
   for (; i < N; i++) {
      *xy1 = (float)(*xy1+XF(x[i])*y1[i]);
      *xy2 = (float)(*xy2+XF(x[i])*y2[i]);
   }
}

static void fl_e_haar1(float *X, int N0, int stride) {
   int i, j;
   N0 >>= 1;
   for (i = 0; i < stride; i++)
      for (j = 0; j < N0; j++) {
         xf t1 = XF(.70710678f)*X[stride*2*j+i], t2 = XF(.70710678f)*X[stride*(2*j+1)+i];
         X[stride*2*j+i] = (float)(t1+t2);
         X[stride*(2*j+1)+i] = (float)(t1-t2);
      }
}

static void fl_e_exp_rotation1(float *X, int len, int stride, float c, float s) {
   int i;
   float ms = -s, *Xptr = X;
   for (i = 0; i < len-stride; i++) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = (float)(XF(c)*x2+XF(s)*x1);
      *Xptr++ = (float)(XF(c)*x1+XF(ms)*x2);
   }
   Xptr = &X[len-2*stride-1];
   for (i = len-2*stride-1; i >= 0; i--) {
      float x1 = Xptr[0], x2 = Xptr[stride];
      Xptr[stride] = (float)(XF(c)*x2+XF(s)*x1);
      *Xptr-- = (float)(XF(c)*x1+XF(ms)*x2);
   }
}
static void fl_e_exp_rotation(float *X, int len, int dir, int stride, int K, int spread) {
   static const int factors[3] = {15, 10, 5};
   int i, stride2 = 0;
   xf gain, theta;
   float theta_f, c, s;
   if (2*K >= len || spread == SPREAD_NONE) return;
   gain = XF(len)/(len+factors[spread-1]*K);
   theta = gain*gain*.5f;
   theta_f = (float)theta;
   c = (float)fl_m_cos((double)(XF(.5f*3.1415926535897931)*theta));
   s = (float)fl_m_cos((double)((1.0L-theta_f)*XF(.5f*3.1415926535897931)));
   if (len >= 8*stride) {
      stride2 = 1;
      while ((stride2*stride2+stride2)*stride+(stride>>2) < len) stride2++;
   }
   len /= stride;
   for (i = 0; i < stride; i++) {
      if (dir < 0) {
         if (stride2) fl_e_exp_rotation1(X+i*len, len, stride2, s, c);
         fl_e_exp_rotation1(X+i*len, len, 1, c, s);
      } else {
         fl_e_exp_rotation1(X+i*len, len, 1, c, -s);
         if (stride2) fl_e_exp_rotation1(X+i*len, len, stride2, s, -c);
      }
   }
}

static void fl_e_renormalise_vector(float *X, int N, float gain) {
   int i;
   float E = (float)(XF(1e-15f)+fl_e_inner_prod(X, X, N));
   xf g = (1.0L/fl_m_sqrtf(E))*gain;
   for (i = 0; i < N; i++) X[i] = (float)(g*X[i]);
}

/* op_pvq_search_sse2: SIMD search; xy/yy accumulate on the x87 stack. */
static float fl_e_pvq_search(float *_X, int *iy, int K, int N) {
   int i, j, pulsesLeft;
   float y[176+3], X[176+3], signy[176+3], xyf, yyf;
   xf xy, yy;
   __m128 signmask = _mm_set_ps1(-0.f), sums = _mm_setzero_ps();
   __m128i fours = _mm_set_epi32(4, 4, 4, 4);
   memcpy(X, _X, N*sizeof(float));
   X[N] = X[N+1] = X[N+2] = 0;
   for (j = 0; j < N; j += 4) {
      __m128 x4 = _mm_loadu_ps(&X[j]), s4 = _mm_cmplt_ps(x4, _mm_setzero_ps());
      x4 = _mm_andnot_ps(signmask, x4);
      sums = _mm_add_ps(sums, x4);
      _mm_storeu_ps(&y[j], _mm_setzero_ps());
      _mm_storeu_si128((__m128i*)(void*)&iy[j], _mm_setzero_si128());
      _mm_storeu_ps(&X[j], x4);
      _mm_storeu_ps(&signy[j], s4);
   }
   sums = _mm_add_ps(sums, _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(1, 0, 3, 2)));
   sums = _mm_add_ps(sums, _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(2, 3, 0, 1)));
   xyf = yyf = 0;
   pulsesLeft = K;
   if (K > (N>>1)) {
      __m128i pulses_sum = _mm_setzero_si128();
      __m128 yy4 = _mm_setzero_ps(), xy4 = _mm_setzero_ps(), rcp4;
      float sum = _mm_cvtss_f32(sums);
      if (!(sum > 1e-15f && sum < 64)) {
         X[0] = 1.f;
         j = 1;
         do X[j] = 0;
         while (++j < N);
         sums = _mm_set_ps1(1.f);
      }
      rcp4 = _mm_mul_ps(_mm_set_ps1((float)(XF(.8)+K)), _mm_rcp_ps(sums));
      for (j = 0; j < N; j += 4) {
         __m128 x4 = _mm_loadu_ps(&X[j]), rx4 = _mm_mul_ps(x4, rcp4), y4;
         __m128i iy4 = _mm_cvttps_epi32(rx4);
         pulses_sum = _mm_add_epi32(pulses_sum, iy4);
         _mm_storeu_si128((__m128i*)(void*)&iy[j], iy4);
         y4 = _mm_cvtepi32_ps(iy4);
         xy4 = _mm_add_ps(xy4, _mm_mul_ps(x4, y4));
         yy4 = _mm_add_ps(yy4, _mm_mul_ps(y4, y4));
         _mm_storeu_ps(&y[j], _mm_add_ps(y4, y4));
      }
      pulses_sum = _mm_add_epi32(pulses_sum, _mm_shuffle_epi32(pulses_sum, _MM_SHUFFLE(1, 0, 3, 2)));
      pulses_sum = _mm_add_epi32(pulses_sum, _mm_shuffle_epi32(pulses_sum, _MM_SHUFFLE(2, 3, 0, 1)));
      pulsesLeft -= _mm_cvtsi128_si32(pulses_sum);
      xy4 = _mm_add_ps(xy4, _mm_shuffle_ps(xy4, xy4, _MM_SHUFFLE(1, 0, 3, 2)));
      xy4 = _mm_add_ps(xy4, _mm_shuffle_ps(xy4, xy4, _MM_SHUFFLE(2, 3, 0, 1)));
      xyf = _mm_cvtss_f32(xy4);
      yy4 = _mm_add_ps(yy4, _mm_shuffle_ps(yy4, yy4, _MM_SHUFFLE(1, 0, 3, 2)));
      yy4 = _mm_add_ps(yy4, _mm_shuffle_ps(yy4, yy4, _MM_SHUFFLE(2, 3, 0, 1)));
      yyf = _mm_cvtss_f32(yy4);
   }
   X[N] = X[N+1] = X[N+2] = -100;
   y[N] = y[N+1] = y[N+2] = 100;
   xy = xyf;
   yy = yyf;
   if (pulsesLeft > N+3) {
      xf tmp = pulsesLeft;
      yy = yy+tmp*tmp;
      yy = yy+tmp*y[0];
      iy[0] += pulsesLeft;
      pulsesLeft = 0;
   }
   for (i = 0; i < pulsesLeft; i++) {
      int best_id;
      __m128 xy4, yy4, max, max2;
      __m128i count, pos;
      yy += 1;
      xyf = (float)xy;
      yyf = (float)yy;
      xy4 = _mm_load1_ps(&xyf);
      yy4 = _mm_load1_ps(&yyf);
      max = _mm_setzero_ps();
      pos = _mm_setzero_si128();
      count = _mm_set_epi32(3, 2, 1, 0);
      for (j = 0; j < N; j += 4) {
         __m128 x4 = _mm_loadu_ps(&X[j]), y4 = _mm_loadu_ps(&y[j]), r4;
         x4 = _mm_add_ps(x4, xy4);
         y4 = _mm_add_ps(y4, yy4);
         y4 = _mm_rsqrt_ps(y4);
         r4 = _mm_mul_ps(x4, y4);
         pos = _mm_max_epi16(pos, _mm_and_si128(count, _mm_castps_si128(_mm_cmpgt_ps(r4, max))));
         max = _mm_max_ps(max, r4);
         count = _mm_add_epi32(count, fours);
      }
      max2 = _mm_max_ps(max, _mm_shuffle_ps(max, max, _MM_SHUFFLE(1, 0, 3, 2)));
      max2 = _mm_max_ps(max2, _mm_shuffle_ps(max2, max2, _MM_SHUFFLE(2, 3, 0, 1)));
      pos = _mm_and_si128(pos, _mm_castps_si128(_mm_cmpeq_ps(max, max2)));
      pos = _mm_max_epi16(pos, _mm_unpackhi_epi64(pos, pos));
      pos = _mm_max_epi16(pos, _mm_shufflelo_epi16(pos, _MM_SHUFFLE(1, 0, 3, 2)));
      best_id = _mm_cvtsi128_si32(pos);
      xy += X[best_id];
      yy += y[best_id];
      y[best_id] = (float)(XF(y[best_id])+2);
      iy[best_id]++;
   }
   for (j = 0; j < N; j += 4) {
      __m128i y4 = _mm_loadu_si128((__m128i*)(void*)&iy[j]);
      __m128i s4 = _mm_castps_si128(_mm_loadu_ps(&signy[j]));
      y4 = _mm_xor_si128(_mm_add_epi32(y4, s4), s4);
      _mm_storeu_si128((__m128i*)(void*)&iy[j], y4);
   }
   return (float)yy;
}

static unsigned fl_e_alg_quant(float *X, int N, int K, int spread, int B, ec_ctx *enc, float gain, int resynth) {
   int iy[176+3], i;
   float yy;
   unsigned collapse_mask;
   fl_e_exp_rotation(X, N, 1, B, K, spread);
   yy = fl_e_pvq_search(X, iy, K, N);
   collapse_mask = fl_extract_collapse_mask(iy, N, B);
   fl_encode_pulses(iy, N, K, enc);
   if (resynth) {
      xf g = (1.0L/fl_m_sqrtf(yy))*gain;
      for (i = 0; i < N; i++) X[i] = (float)(g*iy[i]);
      fl_e_exp_rotation(X, N, -1, B, K, spread);
   }
   return collapse_mask;
}

static xf fl_e_atan_norm(float x) {
   xf xs = XF(x)*x;
   xf p = -3.3331659436225891113281250000e-01f+xs*(1.99627041816711425781250000000e-01f
         +xs*(-1.3976582884788513183593750000e-01f+xs*(9.79423448443412780761718750000e-02f
         +xs*(-5.7773590087890625000000000000e-02f+xs*(2.30401363223791122436523437500e-02f
         +xs*(-4.3554059229791164398193359375e-03f))))));
   return (XF(x)+(xs*x)*p)*0.636619772367581f;
}

static int fl_e_stereo_itheta(const float *X, const float *Y, int stereo, int N) {
   int i;
   float Emid, Eside, mid;
   xf side, r;
   if (stereo) {
      xf em = 0, es = 0;
      for (i = 0; i < N; i++) {
         xf m = XF(X[i])+Y[i], s = XF(X[i])-Y[i];
         em += m*m;
         es += s*s;
      }
      Emid = (float)em;
      Eside = (float)es;
   } else {
      Emid = (float)fl_e_inner_prod(X, X, N);
      Eside = (float)fl_e_inner_prod(Y, Y, N);
   }
   mid = (float)fl_m_sqrtf(Emid);
   side = fl_m_sqrtf(Eside);
   if (XF(mid)*mid+side*side < 1e-18f) r = 0;
   else if (side < mid) r = fl_e_atan_norm((float)(side/mid));
   else r = 1.0L-fl_e_atan_norm((float)(mid/side));
   return (int)floorf((float)(r*1073741824.f+.5f));
}

static void fl_e_intensity_stereo(float *X, const float *Y, const float *bandE, int i, int N) {
   int j;
   float left = bandE[i], right = bandE[i+CELT_NB_EBANDS];
   xf norm = XF(1e-15f)+fl_m_sqrtf((float)((XF(left)*left+1e-15f)+XF(right)*right));
   xf a1 = left/norm, a2 = right/norm;
   for (j = 0; j < N; j++) X[j] = (float)(a1*X[j]+a2*Y[j]);
}
static void fl_e_stereo_split(float *X, float *Y, int N) {
   int j;
   for (j = 0; j < N; j++) {
      xf l = XF(.70710678f)*X[j], r = XF(.70710678f)*Y[j];
      X[j] = (float)(l+r);
      Y[j] = (float)(r-l);
   }
}
static void fl_e_stereo_merge(float *X, float *Y, float mid, int N) {
   int j;
   float xp = (float)fl_e_inner_prod(Y, X, N), lgain;
   xf side = fl_e_inner_prod(Y, Y, N), base, El, Er, rgain;
   base = XF(mid)*mid+side;
   El = base-(XF(xp)*mid)*2;
   Er = base+(XF(xp)*mid)*2;
   if (Er < 6e-4f || El < 6e-4f) {
      memcpy(Y, X, N*sizeof(float));
      return;
   }
   lgain = (float)(1.0L/fl_m_sqrtf((float)El));
   rgain = 1.0L/fl_m_sqrtf((float)Er);
   for (j = 0; j < N; j++) {
      xf l = XF(mid)*X[j], r = Y[j];
      X[j] = (float)((l-r)*lgain);
      Y[j] = (float)((l+r)*rgain);
   }
}

typedef struct {
   int resynth, i, intensity, spread, tf_change, theta_round, disable_inv, avoid_split_noise;
   ec_ctx *ec;
   fl_i32 remaining_bits;
   const float *bandE;
   fl_u32 seed;
} fl_band_ectx;
typedef struct { int inv, imid, iside, delta, itheta, qalloc; } fl_split_ectx;

static void fl_e_compute_theta(fl_band_ectx *ctx, fl_split_ectx *sctx, float *X, float *Y, int N, int *b, int B,
      int B0, int LM, int stereo, int *fill) {
   int qn, itheta, delta, imid, iside, qalloc, pulse_cap, offset, inv = 0, i = ctx->i;
   fl_i32 tell;
   ec_ctx *ec = ctx->ec;
   pulse_cap = fl_logN400[i]+LM*(1<<BITRES);
   offset = (pulse_cap>>1)-(stereo && N == 2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
   qn = fl_compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i >= ctx->intensity) qn = 1;
   itheta = fl_e_stereo_itheta(X, Y, stereo, N)>>16;
   tell = ec_tell_frac(ec);
   if (qn != 1) {
      if (!stereo || ctx->theta_round == 0) {
         itheta = (itheta*(fl_i32)qn+8192)>>14;
         if (!stereo && ctx->avoid_split_noise && itheta > 0 && itheta < qn) {
            int unquantized = (fl_i32)itheta*16384/qn;
            imid = fl_bitexact_cos((fl_i16)unquantized);
            iside = fl_bitexact_cos((fl_i16)(16384-unquantized));
            delta = FL_FRAC_MUL16((N-1)<<7, fl_bitexact_log2tan(iside, imid));
            if (delta > *b) itheta = qn;
            else if (delta < -*b) itheta = 0;
         }
      } else {
         int bias = itheta > 8192 ? 32767/qn : -32767/qn;
         int down = IMIN(qn-1, IMAX(0, (itheta*(fl_i32)qn+bias)>>14));
         itheta = ctx->theta_round < 0 ? down : down+1;
      }
      if (stereo && N > 2) {
         int p0 = 3, x = itheta, x0 = qn/2, ft = p0*(x0+1)+x0;
         ec_encode(ec, x <= x0 ? p0*x : (x-1-x0)+(x0+1)*p0, x <= x0 ? p0*(x+1) : (x-x0)+(x0+1)*p0, ft);
      } else if (B0 > 1 || stereo) {
         ec_enc_uint(ec, itheta, qn+1);
      } else {
         int fs, fl, ft = ((qn>>1)+1)*((qn>>1)+1);
         fs = itheta <= (qn>>1) ? itheta+1 : qn+1-itheta;
         fl = itheta <= (qn>>1) ? itheta*(itheta+1)>>1 : ft-((qn+1-itheta)*(qn+2-itheta)>>1);
         ec_encode(ec, fl, fl+fs, ft);
      }
      itheta = (fl_i32)itheta*16384/qn;
      if (stereo) {
         if (itheta == 0) fl_e_intensity_stereo(X, Y, ctx->bandE, i, N);
         else fl_e_stereo_split(X, Y, N);
      }
   } else if (stereo) {
      inv = itheta > 8192 && !ctx->disable_inv;
      if (inv) {
         int j;
         for (j = 0; j < N; j++) Y[j] = -Y[j];
      }
      fl_e_intensity_stereo(X, Y, ctx->bandE, i, N);
      if (*b > 2<<BITRES && ctx->remaining_bits > 2<<BITRES) ec_enc_bit_logp(ec, inv, 2);
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

static unsigned fl_e_quant_band_n1(fl_band_ectx *ctx, float *X, float *Y, float *lowband_out) {
   int c, stereo = Y != NULL;
   float *x = X;
   c = 0;
   do {
      int sign = 0;
      if (ctx->remaining_bits >= 1<<BITRES) {
         sign = x[0] < 0;
         ec_enc_bits(ctx->ec, sign, 1);
         ctx->remaining_bits -= 1<<BITRES;
      }
      if (ctx->resynth) x[0] = sign ? -1.f : 1.f;
      x = Y;
   } while (++c < 1+stereo);
   if (lowband_out) lowband_out[0] = X[0];
   return 1;
}

static unsigned fl_e_quant_partition(fl_band_ectx *ctx, float *X, int N, int b, int B, float *lowband, int LM,
      float gain, int fill) {
   const unsigned char *cache;
   int q, curr_bits, B0 = B, i = ctx->i;
   unsigned cm = 0;
   cache = fl_cache_bits50+fl_cache_index50[(LM+1)*CELT_NB_EBANDS+i];
   if (LM != -1 && b > cache[cache[0]]+12 && N > 2) {
      int mbits, sbits, delta, itheta, qalloc;
      fl_split_ectx sctx;
      float *Y, *next_lowband2 = NULL, mid, side;
      fl_i32 rebalance;
      N >>= 1;
      Y = X+N;
      LM -= 1;
      if (B == 1) fill = (fill&1)|(fill<<1);
      B = (B+1)>>1;
      fl_e_compute_theta(ctx, &sctx, X, Y, N, &b, B, B0, LM, 0, &fill);
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
         cm = fl_e_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
      } else {
         cm = fl_e_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain*side, fill>>B)<<(B0>>1);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain*mid, fill);
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
         cm = fl_e_alg_quant(X, N, fl_get_pulses(q), ctx->spread, B, ctx->ec, gain, ctx->resynth);
      } else if (ctx->resynth) {
         int j;
         unsigned cm_mask = (unsigned)(1UL<<B)-1;
         fill &= cm_mask;
         if (!fill) {
            memset(X, 0, N*sizeof(float));
         } else {
            if (lowband == NULL) {
               for (j = 0; j < N; j++) {
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  X[j] = (float)((fl_i32)ctx->seed>>20);
               }
               cm = cm_mask;
            } else {
               for (j = 0; j < N; j++) {
                  float tmp = 1.0f/256;
                  ctx->seed = fl_lcg_rand(ctx->seed);
                  tmp = (ctx->seed)&0x8000 ? tmp : -tmp;
                  X[j] = lowband[j]+tmp;
               }
               cm = fill;
            }
            fl_e_renormalise_vector(X, N, gain);
         }
      }
   }
   return cm;
}

static unsigned fl_e_quant_band(fl_band_ectx *ctx, float *X, int N, int b, int B, float *lowband, int LM,
      float *lowband_out, float gain, float *lowband_scratch, int fill) {
   static const unsigned char bit_interleave_table[16] = {0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3};
   static const unsigned char bit_deinterleave_table[16] = {
      0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF};
   int N0 = N, N_B, N_B0, B0 = B, time_divide = 0, recombine = 0, longBlocks, k, tf_change = ctx->tf_change;
   unsigned cm = 0;
   longBlocks = B0 == 1;
   N_B = N/B;
   if (N == 1) return fl_e_quant_band_n1(ctx, X, NULL, lowband_out);
   if (tf_change > 0) recombine = tf_change;
   if (lowband_scratch && lowband && (recombine || ((N_B&1) == 0 && tf_change < 0) || B0 > 1)) {
      memcpy(lowband_scratch, lowband, N*sizeof(float));
      lowband = lowband_scratch;
   }
   for (k = 0; k < recombine; k++) {
      fl_e_haar1(X, N>>k, 1<<k);
      if (lowband) fl_e_haar1(lowband, N>>k, 1<<k);
      fill = bit_interleave_table[fill&0xF]|bit_interleave_table[fill>>4]<<2;
   }
   B >>= recombine;
   N_B <<= recombine;
   while ((N_B&1) == 0 && tf_change < 0) {
      fl_e_haar1(X, N_B, B);
      if (lowband) fl_e_haar1(lowband, N_B, B);
      fill |= fill<<B;
      B <<= 1;
      N_B >>= 1;
      time_divide++;
      tf_change++;
   }
   B0 = B;
   N_B0 = N_B;
   if (B0 > 1) {
      fl_deinterleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
      if (lowband) fl_deinterleave_hadamard(lowband, N_B>>recombine, B0<<recombine, longBlocks);
   }
   cm = fl_e_quant_partition(ctx, X, N, b, B, lowband, LM, gain, fill);
   if (ctx->resynth) {
      if (B0 > 1) fl_interleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
      N_B = N_B0;
      B = B0;
      for (k = 0; k < time_divide; k++) {
         B >>= 1;
         N_B <<= 1;
         cm |= cm>>B;
         fl_e_haar1(X, N_B, B);
      }
      for (k = 0; k < recombine; k++) {
         cm = bit_deinterleave_table[cm];
         fl_e_haar1(X, N0>>k, 1<<k);
      }
      B <<= recombine;
      if (lowband_out) {
         int j;
         float n = (float)fl_m_sqrt((double)N0);
         for (j = 0; j < N0; j++) lowband_out[j] = n*X[j];
      }
      cm &= (1<<B)-1;
   }
   return cm;
}

static unsigned fl_e_quant_band_stereo(fl_band_ectx *ctx, float *X, float *Y, int N, int b, int B, float *lowband,
      int LM, float *lowband_out, float *lowband_scratch, int fill) {
   int inv, mbits, sbits, delta, itheta, qalloc, orig_fill;
   float mid, side;
   unsigned cm = 0;
   fl_split_ectx sctx;
   if (N == 1) return fl_e_quant_band_n1(ctx, X, Y, lowband_out);
   orig_fill = fill;
   if (ctx->bandE[ctx->i] < 1e-10f || ctx->bandE[CELT_NB_EBANDS+ctx->i] < 1e-10f) {
      if (ctx->bandE[ctx->i] > ctx->bandE[CELT_NB_EBANDS+ctx->i]) memcpy(Y, X, N*sizeof(float));
      else memcpy(X, Y, N*sizeof(float));
   }
   fl_e_compute_theta(ctx, &sctx, X, Y, N, &b, B, B, LM, 1, &fill);
   inv = sctx.inv;
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;
   mid = (1.f/32768)*sctx.imid;
   side = (1.f/32768)*sctx.iside;
   if (N == 2) {
      int c, sign = 0;
      float *x2, *y2;
      mbits = b;
      sbits = 0;
      if (itheta != 0 && itheta != 16384) sbits = 1<<BITRES;
      mbits -= sbits;
      c = itheta > 8192;
      ctx->remaining_bits -= qalloc+sbits;
      x2 = c ? Y : X;
      y2 = c ? X : Y;
      if (sbits) {
         sign = XF(x2[0])*y2[1]-XF(x2[1])*y2[0] < 0;
         ec_enc_bits(ctx->ec, sign, 1);
      }
      sign = 1-2*sign;
      cm = fl_e_quant_band(ctx, x2, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, orig_fill);
      y2[0] = (float)-sign*x2[1];
      y2[1] = (float)sign*x2[0];
      if (ctx->resynth) {
         xf y0;
         float tmp;
         X[0] = mid*X[0];
         X[1] = mid*X[1];
         y0 = XF(side)*Y[0];
         Y[0] = (float)y0;
         Y[1] = side*Y[1];
         tmp = X[0];
         X[0] = (float)(tmp-y0);
         Y[0] = tmp+Y[0];
         tmp = X[1];
         X[1] = tmp-Y[1];
         Y[1] = tmp+Y[1];
      }
   } else {
      fl_i32 rebalance;
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;
      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = fl_e_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, fill);
         rebalance = mbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 0) sbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
      } else {
         cm = fl_e_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
         rebalance = sbits-(rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta != 16384) mbits += rebalance-(3<<BITRES);
         cm |= fl_e_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.f, lowband_scratch, fill);
      }
   }
   if (ctx->resynth) {
      if (N != 2) fl_e_stereo_merge(X, Y, mid, N);
      if (inv) {
         int j;
         for (j = 0; j < N; j++) Y[j] = -Y[j];
      }
   }
   return cm;
}

static void fl_e_quant_all_bands(int start, int end, float *X_, float *Y_, unsigned char *collapse_masks,
      const float *bandE, const int *pulses, int shortBlocks, int spread, int dual_stereo, int intensity,
      const int *tf_res, fl_i32 total_bits, fl_i32 balance, ec_ctx *ec, int LM, int codedBands, fl_u32 *seed,
      int complexity, int disable_inv) {
   const fl_i16 *eb = fl_eband5ms;
   int i, M = 1<<LM, B, C = Y_ != NULL ? 2 : 1, norm_offset, lowband_offset = 0, update_lowband = 1;
   int theta_rdo = Y_ != NULL && !dual_stereo && complexity >= 8;
   int resynth = theta_rdo;
   float _norm[2*8*100], *norm, *norm2, *lowband_scratch, _lowband_scratch[8*24];
   float X_save[8*24], Y_save[8*24], X_save2[8*24], Y_save2[8*24], norm_save2[8*24];
   unsigned char bytes_save[1275];
   fl_band_ectx ctx;
   B = shortBlocks ? M : 1;
   norm_offset = M*eb[start];
   norm = _norm;
   norm2 = norm+M*eb[CELT_NB_EBANDS-1]-norm_offset;
   lowband_scratch = resynth ? _lowband_scratch : X_+M*eb[CELT_NB_EBANDS-1];
   ctx.bandE = bandE;
   ctx.ec = ec;
   ctx.intensity = intensity;
   ctx.seed = *seed;
   ctx.spread = spread;
   ctx.disable_inv = disable_inv;
   ctx.resynth = resynth;
   ctx.theta_round = 0;
   ctx.avoid_split_noise = B > 1;
   for (i = start; i < end; i++) {
      fl_i32 tell, remaining_bits, curr_balance;
      int b, N, effective_lowband = -1, last = i == end-1;
      float *X, *Y;
      unsigned x_cm, y_cm;
      ctx.i = i;
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
      if (resynth && (M*eb[i]-N >= M*eb[start] || i == start+1) && (update_lowband || lowband_offset == 0))
         lowband_offset = i;
      if (i == start+1) {
         int n1 = M*(eb[start+1]-eb[start]), n2 = M*(eb[start+2]-eb[start+1]);
         memcpy(&norm[n1], &norm[2*n1-n2], (n2-n1)*sizeof(float));
         if (dual_stereo) memcpy(&norm2[n1], &norm2[2*n1-n2], (n2-n1)*sizeof(float));
      }
      ctx.tf_change = tf_res[i];
      if (last && !theta_rdo) lowband_scratch = NULL;
      if (lowband_offset != 0 && (spread != SPREAD_AGGRESSIVE || B > 1 || tf_res[i] < 0)) {
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
         if (resynth)
            for (j = 0; j < M*eb[i]-norm_offset; j++) norm[j] = .5f*(norm[j]+norm2[j]);
      }
      if (dual_stereo) {
         x_cm = fl_e_quant_band(&ctx, X, N, b/2, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
               last ? NULL : norm+M*eb[i]-norm_offset, 1.f, lowband_scratch, x_cm);
         y_cm = fl_e_quant_band(&ctx, Y, N, b/2, B, effective_lowband != -1 ? norm2+effective_lowband : NULL, LM,
               last ? NULL : norm2+M*eb[i]-norm_offset, 1.f, lowband_scratch, y_cm);
      } else {
         if (Y != NULL) {
            if (theta_rdo && i < intensity) {
               ec_ctx ec_save, ec_save2;
               fl_band_ectx ctx_save, ctx_save2;
               float dist0, w0, w1;
               xf dist1;
               unsigned cm, cm2;
               int nstart_bytes, save_bytes;
               unsigned char *bytes_buf;
               {
                  float Ex = bandE[i], Ey = bandE[i+CELT_NB_EBANDS], minE = FL_MIN(Ex, Ey);
                  xf m3 = XF(minE)/3.f;
                  w0 = (float)(Ex+m3);
                  w1 = (float)(Ey+m3);
               }
               cm = x_cm|y_cm;
               ec_save = *ec;
               ctx_save = ctx;
               memcpy(X_save, X, N*sizeof(float));
               memcpy(Y_save, Y, N*sizeof(float));
               ctx.theta_round = -1;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, cm);
               {
                  float d0 = (float)(w0*fl_e_inner_prod(X_save, X, N));
                  dist0 = (float)(w1*fl_e_inner_prod(Y_save, Y, N)+d0);
               }
               cm2 = x_cm;
               ec_save2 = *ec;
               ctx_save2 = ctx;
               memcpy(X_save2, X, N*sizeof(float));
               memcpy(Y_save2, Y, N*sizeof(float));
               if (!last) memcpy(norm_save2, norm+M*eb[i]-norm_offset, N*sizeof(float));
               nstart_bytes = ec_save.offs;
               bytes_buf = ec_save.buf+nstart_bytes;
               save_bytes = ec_save.storage-nstart_bytes;
               memcpy(bytes_save, bytes_buf, save_bytes);
               *ec = ec_save;
               ctx = ctx_save;
               memcpy(X, X_save, N*sizeof(float));
               memcpy(Y, Y_save, N*sizeof(float));
               if (i == start+1) {
                  int n1 = M*(eb[start+1]-eb[start]), n2 = M*(eb[start+2]-eb[start+1]);
                  memcpy(&norm[n1], &norm[2*n1-n2], (n2-n1)*sizeof(float));
               }
               ctx.theta_round = 1;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, cm);
               {
                  float d0 = (float)(w0*fl_e_inner_prod(X_save, X, N));
                  dist1 = w1*fl_e_inner_prod(Y_save, Y, N)+d0;
               }
               if (dist0 >= dist1) {
                  x_cm = cm2;
                  *ec = ec_save2;
                  ctx = ctx_save2;
                  memcpy(X, X_save2, N*sizeof(float));
                  memcpy(Y, Y_save2, N*sizeof(float));
                  if (!last) memcpy(norm+M*eb[i]-norm_offset, norm_save2, N*sizeof(float));
                  memcpy(bytes_buf, bytes_save, save_bytes);
               }
            } else {
               ctx.theta_round = 0;
               x_cm = fl_e_quant_band_stereo(&ctx, X, Y, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL,
                     LM, last ? NULL : norm+M*eb[i]-norm_offset, lowband_scratch, x_cm|y_cm);
            }
         } else {
            x_cm = fl_e_quant_band(&ctx, X, N, b, B, effective_lowband != -1 ? norm+effective_lowband : NULL, LM,
                  last ? NULL : norm+M*eb[i]-norm_offset, 1.f, lowband_scratch, x_cm|y_cm);
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

static int fl_e_spreading_decision(const float *X, int *average, int last_decision, int *hf_average,
      int *tapset_decision, int update_hf, int end, int C, int M, const int *spread_weight) {
   const fl_i16 *eb = fl_eband5ms;
   int i, c, N0 = M*120, sum = 0, nbBands = 0, decision, hf_sum = 0;
   if (M*(eb[end]-eb[end-1]) <= 8) return SPREAD_NONE;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         int j, N, tmp, tcount[3] = {0, 0, 0};
         const float *x = X+M*eb[i]+c*N0;
         N = M*(eb[i+1]-eb[i]);
         if (N <= 8) continue;
         for (j = 0; j < N; j++) {
            xf x2N = (XF(x[j])*x[j])*N;
            if (x2N < .25f) tcount[0]++;
            if (x2N < .0625f) tcount[1]++;
            if (x2N < .015625f) tcount[2]++;
         }
         if (i > CELT_NB_EBANDS-4) hf_sum += (fl_u32)(32*(tcount[1]+tcount[0]))/N;
         tmp = (2*tcount[2] >= N)+(2*tcount[1] >= N)+(2*tcount[0] >= N);
         sum += tmp*spread_weight[i];
         nbBands += spread_weight[i];
      }
   if (update_hf) {
      if (hf_sum) hf_sum = (fl_u32)hf_sum/(fl_u32)(C*(4-CELT_NB_EBANDS+end));
      *hf_average = (*hf_average+hf_sum)>>1;
      hf_sum = *hf_average;
      if (*tapset_decision == 2) hf_sum += 4;
      else if (*tapset_decision == 0) hf_sum -= 4;
      if (hf_sum > 22) *tapset_decision = 2;
      else if (hf_sum > 18) *tapset_decision = 1;
      else *tapset_decision = 0;
   }
   sum = (fl_u32)((fl_i32)sum<<8)/(fl_u32)nbBands;
   sum = (sum+*average)>>1;
   *average = sum;
   sum = (3*sum+(((3-last_decision)<<7)+64)+2)>>2;
   if (sum < 80) decision = SPREAD_AGGRESSIVE;
   else if (sum < 256) decision = SPREAD_NORMAL;
   else if (sum < 384) decision = SPREAD_LIGHT;
   else decision = SPREAD_NONE;
   return decision;
}

/* ---- pitch analysis for the prefilter ---- */
static void fl_e_celt_lpc(float *lpc, const float *ac, int p) {
   int i, j;
   xf error = ac[0];
   memset(lpc, 0, p*sizeof(float));
   if (ac[0] > 1e-10f) {
      for (i = 0; i < p; i++) {
         xf rr = 0, r;
         for (j = 0; j < i; j++) rr += XF(lpc[j])*ac[i-j];
         rr += ac[i+1];
         r = -(rr/error);
         lpc[i] = (float)r;
         for (j = 0; j < (i+1)>>1; j++) {
            float tmp1 = lpc[j], tmp2 = lpc[i-1-j];
            lpc[j] = (float)(tmp1+r*tmp2);
            lpc[i-1-j] = (float)(tmp2+r*tmp1);
         }
         error = error-(r*r)*error;
         if (error <= XF(.001f)*ac[0]) break;
      }
   }
}
static void fl_e_autocorr(const float *x, float *ac, int lag, int n) {
   int i, k, fastN = n-lag;
   fl_pitch_xcorr(x, x, ac, fastN, lag+1);
   for (k = 0; k <= lag; k++) {
      xf d = 0;
      for (i = k+fastN; i < n; i++) d += XF(x[i])*x[i-k];
      ac[k] = (float)(d+ac[k]);
   }
}
static void fl_e_pitch_downsample(float *x[], float *x_lp, int len, int C) {
   int i;
   float ac[5], lpc[4], lpc2[5];
   xf tmp = 1;
   for (i = 1; i < len; i++)
      x_lp[i] = (float)((XF(.25f)*x[0][2*i-1]+XF(.25f)*x[0][2*i+1])+XF(.5f)*x[0][2*i]);
   x_lp[0] = (float)(XF(.25f)*x[0][1]+XF(.5f)*x[0][0]);
   if (C == 2) {
      for (i = 1; i < len; i++)
         x_lp[i] = (float)(((XF(.25f)*x[1][2*i-1]+XF(.25f)*x[1][2*i+1])+XF(.5f)*x[1][2*i])+x_lp[i]);
      x_lp[0] = (float)((XF(.25f)*x[1][1]+XF(.5f)*x[1][0])+x_lp[0]);
   }
   fl_e_autocorr(x_lp, ac, 4, len);
   ac[0] *= 1.0001f;
   for (i = 1; i <= 4; i++) {
      xf k = XF(.008f)*i;
      ac[i] = (float)(ac[i]-(ac[i]*k)*k);
   }
   fl_e_celt_lpc(lpc, ac, 4);
   for (i = 0; i < 4; i++) {
      tmp = XF(.9f)*tmp;
      lpc[i] = (float)(lpc[i]*tmp);
   }
   lpc2[0] = (float)(XF(lpc[0])+.8f);
   lpc2[1] = (float)(lpc[1]+XF(.8f)*lpc[0]);
   lpc2[2] = (float)(lpc[2]+XF(.8f)*lpc[1]);
   lpc2[3] = (float)(lpc[3]+XF(.8f)*lpc[2]);
   lpc2[4] = (float)(XF(.8f)*lpc[3]);
   {
      float m0 = 0, m1 = 0, m2 = 0, m3 = 0, m4 = 0;
      for (i = 0; i < len; i++) {
         xf sum = XF(x_lp[i])+XF(lpc2[0])*m0;
         sum += XF(lpc2[1])*m1;
         sum += XF(lpc2[2])*m2;
         sum += XF(lpc2[3])*m3;
         sum += XF(lpc2[4])*m4;
         m4 = m3; m3 = m2; m2 = m1; m1 = m0;
         m0 = x_lp[i];
         x_lp[i] = (float)sum;
      }
   }
}
static void fl_e_find_best_pitch(const float *xcorr, const float *y, int len, int max_pitch, int *best_pitch) {
   int i, j;
   xf Syy = 1, best_num[2] = {-1, -1}, best_den[2] = {0, 0};
   best_pitch[0] = 0;
   best_pitch[1] = 1;
   for (j = 0; j < len; j++) Syy += XF(y[j])*y[j];
   for (i = 0; i < max_pitch; i++) {
      if (xcorr[i] > 0) {
         xf x16 = XF(xcorr[i])*1e-12f, num = x16*x16;
         if (num*best_den[1] > best_num[1]*Syy) {
            if (num*best_den[0] > best_num[0]*Syy) {
               best_num[1] = best_num[0];
               best_den[1] = best_den[0];
               best_pitch[1] = best_pitch[0];
               best_num[0] = num;
               best_den[0] = Syy;
               best_pitch[0] = i;
            } else {
               best_num[1] = num;
               best_den[1] = Syy;
               best_pitch[1] = i;
            }
         }
      }
      Syy += XF(y[i+len])*y[i+len]-XF(y[i])*y[i];
      if (Syy < 1) Syy = 1;
   }
}
static void fl_e_pitch_search(const float *x_lp, const float *y, int len, int max_pitch, int *pitch) {
   int i, j, lag = len+max_pitch, best_pitch[2] = {0, 0}, offset;
   float x_lp4[512], y_lp4[1024], xcorr[1024];
   for (j = 0; j < len>>2; j++) x_lp4[j] = x_lp[2*j];
   for (j = 0; j < lag>>2; j++) y_lp4[j] = y[2*j];
   fl_pitch_xcorr(x_lp4, y_lp4, xcorr, len>>2, max_pitch>>2);
   fl_e_find_best_pitch(xcorr, y_lp4, len>>2, max_pitch>>2, best_pitch);
   for (i = 0; i < max_pitch>>1; i++) {
      xf sum;
      xcorr[i] = 0;
      if (abs(i-2*best_pitch[0]) > 2 && abs(i-2*best_pitch[1]) > 2) continue;
      sum = fl_e_inner_prod(x_lp, y+i, len>>1);
      xcorr[i] = (float)(sum > -1 ? sum : -1);
   }
   fl_e_find_best_pitch(xcorr, y, len>>1, max_pitch>>1, best_pitch);
   if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch>>1)-1) {
      float a = xcorr[best_pitch[0]-1], b = xcorr[best_pitch[0]], c = xcorr[best_pitch[0]+1];
      if (XF(c)-a > XF(.7f)*(XF(b)-a)) offset = 1;
      else if (XF(a)-c > XF(.7f)*(XF(b)-c)) offset = -1;
      else offset = 0;
   } else offset = 0;
   *pitch = 2*best_pitch[0]-offset;
}
static float fl_e_remove_doubling(float *x, int maxperiod, int minperiod, int N, int *T0_, int prev_period,
      float prev_gain) {
   static const int second_check[16] = {0, 0, 3, 2, 3, 2, 5, 2, 3, 2, 3, 2, 5, 2, 3, 2};
   int k, i, T, T0, offset, minperiod0 = minperiod;
   float xx, xy, xy2, g, g0, pg_f, best_xy, best_yy, yy_f, xcorr[3], yy_lookup[1024/2+1];
   xf yy, pg;
   maxperiod /= 2;
   minperiod /= 2;
   *T0_ /= 2;
   prev_period /= 2;
   N /= 2;
   x += maxperiod;
   if (*T0_ >= maxperiod) *T0_ = maxperiod-1;
   T = T0 = *T0_;
   fl_e_dual_inner_prod(x, x, x-T0, N, &xx, &xy);
   yy_lookup[0] = xx;
   yy = xx;
   for (i = 1; i <= maxperiod; i++) {
      yy = (yy+XF(x[-i])*x[-i])-XF(x[N-i])*x[N-i];
      yy_lookup[i] = (float)(yy > 0 ? yy : 0);
   }
   yy_f = yy_lookup[T0];
   best_xy = xy;
   best_yy = yy_f;
   g = g0 = (float)(xy/fl_m_sqrtf((float)(XF(xx)*yy_f+1)));
   for (k = 2; k <= 15; k++) {
      int T1, T1b;
      xf g1, thresh, cont, yyx;
      T1 = (2*T0+k)/(2*k);
      if (T1 < minperiod) break;
      if (k == 2) T1b = T1+T0 > maxperiod ? T0 : T0+T1;
      else T1b = (2*second_check[k]*T0+k)/(2*k);
      fl_e_dual_inner_prod(x, &x[-T1], &x[-T1b], N, &xy, &xy2);
      xy = (float)((XF(xy)+xy2)*.5f);
      yyx = (XF(yy_lookup[T1])+yy_lookup[T1b])*.5f;
      yy_f = (float)yyx;
      g1 = xy/fl_m_sqrtf((float)(yyx*xx+1));
      if (abs(T1-prev_period) <= 1) cont = prev_gain;
      else if (abs(T1-prev_period) <= 2 && 5*k*k < T0) cont = XF(.5f)*prev_gain;
      else cont = 0;
      thresh = XF(.7f)*g0-cont;
      if (thresh < .3f) thresh = .3f;
      if (T1 < 3*minperiod) {
         thresh = XF(.85f)*g0-cont;
         if (thresh < .4f) thresh = .4f;
      } else if (T1 < 2*minperiod) {
         thresh = XF(.9f)*g0-cont;
         if (thresh < .5f) thresh = .5f;
      }
      if (g1 > thresh) {
         best_xy = xy;
         best_yy = yy_f;
         T = T1;
         g = (float)g1;
      }
   }
   if (best_xy < 0) best_xy = 0;
   if (best_yy <= best_xy) pg = 1;
   else pg = best_xy/(XF(best_yy)+1);
   pg_f = (float)pg;
   for (k = 0; k < 3; k++) xcorr[k] = (float)fl_e_inner_prod(x, x-(T+k-1), N);
   if (XF(xcorr[2])-xcorr[0] > XF(.7f)*(XF(xcorr[1])-xcorr[0])) offset = 1;
   else if (XF(xcorr[0])-xcorr[2] > XF(.7f)*(XF(xcorr[1])-xcorr[2])) offset = -1;
   else offset = 0;
   if (pg_f > g) pg_f = g;
   *T0_ = 2*T+offset;
   if (*T0_ < minperiod0) *T0_ = minperiod0;
   return pg_f;
}

/* comb_filter: overlap part on the x87 stack, constant part SSE. */
static void fl_e_comb_filter(float *y, float *x, int T0, int T1, int N, float g0, float g1, int tapset0,
      int tapset1, const float *window, int overlap) {
   int i;
   float g00, g01, g02, g10, g11, g12, x0, x1, x2, x3, x4;
   if (g0 == 0 && g1 == 0) {
      if (x != y) memmove(y, x, N*sizeof(float));
      return;
   }
   T0 = IMAX(T0, COMBFILTER_MINPERIOD);
   T1 = IMAX(T1, COMBFILTER_MINPERIOD);
   g00 = g0*fl_comb_gains[tapset0][0];
   g01 = g0*fl_comb_gains[tapset0][1];
   g02 = g0*fl_comb_gains[tapset0][2];
   g10 = g1*fl_comb_gains[tapset1][0];
   g11 = g1*fl_comb_gains[tapset1][1];
   g12 = g1*fl_comb_gains[tapset1][2];
   x1 = x[-T1+1];
   x2 = x[-T1];
   x3 = x[-T1-1];
   x4 = x[-T1-2];
   if (g0 == g1 && T0 == T1 && tapset0 == tapset1) overlap = 0;
   for (i = 0; i < overlap; i++) {
      xf f, nf;
      x0 = x[i-T1+2];
      f = XF(window[i])*window[i];
      nf = 1.0L-f;
      y[i] = (float)(((((((nf*g00)*x[i-T0]+x[i])
            +(nf*g01)*(XF(x[i-T0+1])+x[i-T0-1]))
            +(nf*g02)*(XF(x[i-T0+2])+x[i-T0-2]))
            +(f*g10)*x2)
            +(f*g11)*(XF(x1)+x3))
            +(f*g12)*(XF(x4)+x0));
      x4 = x3;
      x3 = x2;
      x2 = x1;
      x1 = x0;
   }
   if (g1 == 0) {
      if (x != y) memmove(y+overlap, x+overlap, (N-overlap)*sizeof(float));
      return;
   }
   fl_comb_filter_const(y+i, x+i, T1, N-i, g10, g11, g12);
}
