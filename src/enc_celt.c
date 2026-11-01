/* ---- CELT encoder (float, x87 rounding as in the reference build) ---- */
static xf fl_e_inner_prod(const float *x, const float *y, int N);

/* FLOAT_APPROX log2: result stays in an x87 register (unrounded). */
static xf fl_e_log2(float x) {
   fl_i32 integer, range_idx;
   union { float f; fl_u32 i; } in;
   xf f;
   in.f = x;
   integer = (fl_i32)(in.i>>23)-127;
   in.i = (fl_i32)in.i-(fl_i32)((fl_u32)integer<<23);
   range_idx = (in.i>>20)&0x7;
   f = XF(in.f)*fl_log2_x_norm_coeff[range_idx]-1.0625f;
   f = 8.74628424644470214843750000e-02f+f*(1.357829570770263671875000000000f
         +f*(-6.3897705078125000000000000e-01f
         +f*(4.01971250772476196289062500e-01f
         +f*(-2.8415444493293762207031250e-01f))));
   return f+integer+fl_log2_y_norm_coeff[range_idx];
}
/* FLOAT_APPROX exp2: polynomial in extended precision, stored as float. */
static float fl_e_exp2(float x) {
   fl_i32 integer;
   xf frac;
   union { float f; fl_u32 i; } res;
   integer = (int)floorf(x);
   if (integer < -50) return 0;
   frac = XF(x)-integer;
   res.f = (float)(9.999999403953552246093750000000e-01f+frac*(6.931530833244323730468750000000e-01f
         +frac*(2.401536107063293457031250000000e-01f
         +frac*(5.582631751894950866699218750000e-02f
         +frac*(8.989339694380760192871093750000e-03f
         +frac*(1.877576694823801517486572265625e-03f))))));
   res.i = (fl_u32)((fl_i32)res.i+(fl_i32)((fl_u32)integer<<23))&0x7fffffff;
   return res.f;
}

static void fl_e_mdct_forward(const float *in, float *out, int overlap, int shift, int stride) {
   const float *trig = fl_mdct_twiddles960, *window = fl_window120;
   const fl_fft_state *st = &fl_fft_states[shift];
   int i, N = 1920, N2, N4;
   float f[960];
   fl_cpx f2[480];
   xf scale = st->scale;
   for (i = 0; i < shift; i++) {
      N >>= 1;
      trig += N;
   }
   N2 = N>>1;
   N4 = N>>2;
   {
      const float *xp1 = in+(overlap>>1), *xp2 = in+N2-1+(overlap>>1);
      const float *wp1 = window+(overlap>>1), *wp2 = window+(overlap>>1)-1;
      float *yp = f;
      for (i = 0; i < ((overlap+3)>>2); i++) {
         *yp++ = (float)(XF(xp1[N2])**wp2+XF(*xp2)**wp1);
         *yp++ = (float)(XF(*xp1)**wp1-XF(xp2[-N2])**wp2);
         xp1 += 2; xp2 -= 2; wp1 += 2; wp2 -= 2;
      }
      wp1 = window;
      wp2 = window+overlap-1;
      for (; i < N4-((overlap+3)>>2); i++) {
         *yp++ = *xp2;
         *yp++ = *xp1;
         xp1 += 2; xp2 -= 2;
      }
      for (; i < N4; i++) {
         *yp++ = (float)(-(XF(xp1[-N2])**wp1)+XF(*xp2)**wp2);
         *yp++ = (float)(XF(*xp1)**wp2+XF(xp2[N2])**wp1);
         xp1 += 2; xp2 -= 2; wp1 += 2; wp2 -= 2;
      }
   }
   for (i = 0; i < N4; i++) {
      xf re = f[2*i], im = f[2*i+1], t0 = trig[i], t1 = trig[N4+i];
      f2[st->bitrev[i]].r = (float)((re*t0-im*t1)*scale);
      f2[st->bitrev[i]].i = (float)((im*t0+re*t1)*scale);
   }
   fl_e_fft_impl(st, f2);
   {
      float *yp1 = out, *yp2 = out+stride*(N2-1);
      for (i = 0; i < N4; i++) {
         xf t0 = trig[i], t1 = trig[N4+i];
         *yp1 = (float)(XF(f2[i].i)*t1-XF(f2[i].r)*t0);
         *yp2 = (float)(XF(f2[i].r)*t1+XF(f2[i].i)*t0);
         yp1 += 2*stride;
         yp2 -= 2*stride;
      }
   }
}

static void fl_e_compute_mdcts(int shortBlocks, const float *in, float *out, int C, int CC, int LM) {
   int N, B, shift, b, c, i;
   if (shortBlocks) {
      B = shortBlocks;
      N = 120;
      shift = 3;
   } else {
      B = 1;
      N = 120<<LM;
      shift = 3-LM;
   }
   for (c = 0; c < CC; c++)
      for (b = 0; b < B; b++)
         fl_e_mdct_forward(in+c*(B*N+120)+b*N, &out[b+c*N*B], 120, shift, B);
   if (CC == 2 && C == 1)
      for (i = 0; i < B*N; i++) out[i] = (float)(XF(.5f)*out[i]+XF(.5f)*out[B*N+i]);
}

static void fl_e_band_energies(const float *X, float *bandE, int end, int C, int LM) {
   int i, c, N = 120<<LM;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         int lo = c*N+(fl_eband5ms[i]<<LM);
         float sum = (float)(fl_e_inner_prod(&X[lo], &X[lo], (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM)+1e-27f);
         bandE[i+c*CELT_NB_EBANDS] = sqrtf(sum);
      }
}

static void fl_e_normalise_bands(const float *freq, float *X, const float *bandE, int end, int C, int M) {
   int i, j, c, N = M*120;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         xf g = 1.0L/(XF(1e-27f)+bandE[i+c*CELT_NB_EBANDS]);
         for (j = M*fl_eband5ms[i]; j < M*fl_eband5ms[i+1]; j++) X[j+c*N] = (float)(freq[j+c*N]*g);
      }
}

static void fl_e_amp2log2(int effEnd, int end, const float *bandE, float *bandLogE, int C) {
   int c, i;
   for (c = 0; c < C; c++) {
      for (i = 0; i < effEnd; i++)
         bandLogE[i+c*CELT_NB_EBANDS] = (float)(fl_e_log2(bandE[i+c*CELT_NB_EBANDS])-fl_eMeans[i]);
      for (i = effEnd; i < end; i++) bandLogE[c*CELT_NB_EBANDS+i] = -14.f;
   }
}

static int fl_e_quant_coarse_impl(int start, int end, const float *eBands, float *oldEBands, fl_i32 budget,
      fl_i32 tell, const unsigned char *prob_model, float *error, ec_ctx *enc, int C, int LM, int intra,
      float max_decay, int lfe) {
   int i, c, badness = 0;
   float prev[2] = {0, 0}, coef, beta;
   if (tell+3 <= budget) ec_enc_bit_logp(enc, intra, 3);
   if (intra) {
      coef = 0;
      beta = fl_beta_intra;
   } else {
      beta = fl_beta_coef[LM];
      coef = fl_pred_coef[LM];
   }
   for (i = start; i < end; i++) {
      c = 0;
      do {
         int bits_left, qi, qi0;
         float x = eBands[i+c*CELT_NB_EBANDS], old = oldEBands[i+c*CELT_NB_EBANDS];
         float oldE = FL_MAX(-9.f, old), coE, f_f;
         xf f, decay_bound;
         coE = (float)(XF(coef)*oldE);
         f = (XF(x)-XF(coef)*oldE)-prev[c];
         f_f = (float)f;
         qi = (int)floorf((float)(f+.5f));
         decay_bound = XF(FL_MAX(-28.f, old))-max_decay;
         if (qi < 0 && x < decay_bound) {
            qi += (int)(float)(decay_bound-x);
            if (qi > 0) qi = 0;
         }
         qi0 = qi;
         tell = ec_tell(enc);
         bits_left = budget-tell-3*C*(end-i);
         if (i != start && bits_left < 30) {
            if (bits_left < 24) qi = IMIN(1, qi);
            if (bits_left < 16) qi = IMAX(-1, qi);
         }
         if (lfe && i >= 2) qi = IMIN(qi, 0);
         if (budget-tell >= 15) {
            int pi = 2*IMIN(i, 20);
            fl_laplace_encode(enc, &qi, prob_model[pi]<<7, prob_model[pi+1]<<6);
         } else if (budget-tell >= 2) {
            qi = IMAX(-1, IMIN(qi, 1));
            ec_enc_icdf(enc, 2*qi^-(qi < 0), fl_small_energy_icdf, 2);
         } else if (budget-tell >= 1) {
            qi = IMIN(0, qi);
            ec_enc_bit_logp(enc, -qi, 1);
         } else qi = -1;
         error[i+c*CELT_NB_EBANDS] = (float)(XF(f_f)-qi);
         badness += abs(qi0-qi);
         oldEBands[i+c*CELT_NB_EBANDS] = (float)((XF(coE)+prev[c])+qi);
         prev[c] = (float)((XF(prev[c])+qi)-XF(beta)*qi);
      } while (++c < C);
   }
   return lfe ? 0 : badness;
}

static void fl_e_quant_coarse_energy(int start, int end, int effEnd, const float *eBands, float *oldEBands,
      fl_u32 budget, float *error, ec_ctx *enc, int C, int LM, int nbAvailableBytes, int force_intra,
      float *delayedIntra, int two_pass, int loss_rate, int lfe) {
   int intra, badness1 = 0, i, c;
   float max_decay, new_distortion;
   float oldEBands_intra[2*CELT_NB_EBANDS], error_intra[2*CELT_NB_EBANDS];
   ec_ctx enc_start_state;
   fl_u32 tell;
   fl_i32 intra_bias;
   intra = force_intra || (!two_pass && *delayedIntra > 2*C*(end-start) && nbAvailableBytes > (end-start)*C);
   intra_bias = (fl_i32)(float)((XF((fl_i64)budget)**delayedIntra*loss_rate)/(C*512));
   {
      xf dist = 0;
      for (c = 0; c < C; c++)
         for (i = start; i < effEnd; i++) {
            xf d = XF(eBands[i+c*CELT_NB_EBANDS])-oldEBands[i+c*CELT_NB_EBANDS];
            dist += d*d;
         }
      new_distortion = (float)(dist < 200 ? dist : 200);
   }
   tell = ec_tell(enc);
   if (tell+3 > budget) two_pass = intra = 0;
   max_decay = 16.f;
   if (end-start > 10) {
      xf md = XF(.125f)*nbAvailableBytes;
      if (md < max_decay) max_decay = (float)md;
   }
   if (lfe) max_decay = 3.f;
   enc_start_state = *enc;
   memcpy(oldEBands_intra, oldEBands, C*CELT_NB_EBANDS*sizeof(float));
   if (two_pass || intra)
      badness1 = fl_e_quant_coarse_impl(start, end, eBands, oldEBands_intra, budget, tell,
            fl_e_prob_model[LM][1], error_intra, enc, C, LM, 1, max_decay, lfe);
   if (!intra) {
      unsigned char *intra_buf, intra_bits[1275];
      ec_ctx enc_intra_state;
      fl_i32 tell_intra;
      fl_u32 nstart_bytes, nintra_bytes;
      int badness2;
      tell_intra = ec_tell_frac(enc);
      enc_intra_state = *enc;
      nstart_bytes = enc_start_state.offs;
      nintra_bytes = enc_intra_state.offs;
      intra_buf = enc_intra_state.buf+nstart_bytes;
      memcpy(intra_bits, intra_buf, nintra_bytes-nstart_bytes);
      *enc = enc_start_state;
      badness2 = fl_e_quant_coarse_impl(start, end, eBands, oldEBands, budget, tell,
            fl_e_prob_model[LM][intra], error, enc, C, LM, 0, max_decay, lfe);
      if (two_pass && (badness1 < badness2 || (badness1 == badness2 && ((fl_i32)ec_tell_frac(enc))+intra_bias > tell_intra))) {
         *enc = enc_intra_state;
         memcpy(intra_buf, intra_bits, nintra_bytes-nstart_bytes);
         memcpy(oldEBands, oldEBands_intra, C*CELT_NB_EBANDS*sizeof(float));
         memcpy(error, error_intra, C*CELT_NB_EBANDS*sizeof(float));
         intra = 1;
      }
   } else {
      memcpy(oldEBands, oldEBands_intra, C*CELT_NB_EBANDS*sizeof(float));
      memcpy(error, error_intra, C*CELT_NB_EBANDS*sizeof(float));
   }
   if (intra) *delayedIntra = new_distortion;
   else *delayedIntra = (float)((XF(fl_pred_coef[LM])*fl_pred_coef[LM])**delayedIntra+new_distortion);
}

static void fl_e_quant_fine_energy(int start, int end, float *oldEBands, float *error, const int *extra_quant,
      ec_ctx *enc, int C) {
   int i, c;
   for (i = start; i < end; i++) {
      int extra = 1<<extra_quant[i];
      if (extra_quant[i] <= 0) continue;
      if (ec_tell(enc)+C*extra_quant[i] > (fl_i32)enc->storage*8) continue;
      c = 0;
      do {
         int q2;
         xf offset;
         q2 = (int)floorf((float)((XF(error[i+c*CELT_NB_EBANDS])+.5f)*(float)(fl_i16)extra));
         if (q2 > extra-1) q2 = extra-1;
         if (q2 < 0) q2 = 0;
         ec_enc_bits(enc, q2, extra_quant[i]);
         offset = (XF(.5f)+q2)*(1<<(14-extra_quant[i]))*XF(1.f/16384)-.5f;
         offset *= XF(1.f/16384)*(1<<14);
         oldEBands[i+c*CELT_NB_EBANDS] = (float)(oldEBands[i+c*CELT_NB_EBANDS]+offset);
         error[i+c*CELT_NB_EBANDS] = (float)(error[i+c*CELT_NB_EBANDS]-offset);
      } while (++c < C);
   }
}

static void fl_e_quant_energy_finalise(int start, int end, float *oldEBands, float *error, const int *fine_quant,
      const int *fine_priority, int bits_left, ec_ctx *enc, int C) {
   int i, prio, c;
   for (prio = 0; prio < 2; prio++)
      for (i = start; i < end && bits_left >= C; i++) {
         if (fine_quant[i] >= 8 || fine_priority[i] != prio) continue;
         c = 0;
         do {
            int q2 = error[i+c*CELT_NB_EBANDS] < 0 ? 0 : 1;
            xf offset;
            ec_enc_bits(enc, q2, 1);
            offset = (q2-XF(.5f))*(1<<(14-fine_quant[i]-1))*XF(1.f/16384);
            oldEBands[i+c*CELT_NB_EBANDS] = (float)(oldEBands[i+c*CELT_NB_EBANDS]+offset);
            error[i+c*CELT_NB_EBANDS] = (float)(error[i+c*CELT_NB_EBANDS]-offset);
            bits_left--;
         } while (++c < C);
      }
}
