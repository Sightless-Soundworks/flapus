/* ---- CELT encoder: state and frame encoding (celt_encode_with_ec) ---- */
#define LEAK_BANDS 19
#define COMBFILTER_MAXPERIOD 1024

typedef struct {
   int valid;
   float tonality, tonality_slope, noisiness, activity, music_prob, music_prob_min, music_prob_max;
   int bandwidth;
   float activity_probability, max_pitch_ratio;
   unsigned char leak_boost[LEAK_BANDS];
} fl_analysis_info;

typedef struct {
   int channels, stream_channels, force_intra, clip, disable_pf, complexity, start, end;
   fl_i32 bitrate;
   int vbr, constrained_vbr, loss_rate, lsb_depth, disable_inv;
   /* reset from here */
   fl_u32 rng;
   int spread_decision;
   float delayedIntra;
   int tonal_average, lastCodedBands, hf_average, tapset_decision, prefilter_period;
   float prefilter_gain;
   int prefilter_tapset, consec_transient;
   fl_analysis_info analysis;
   int silk_signal_type, silk_offset;
   int lfe;                    /* low-frequency effects channel (surround) */
   const float *energy_mask;   /* surround masking per band and channel, NULL if none */
   float preemph_memE[2];
   fl_i32 vbr_reservoir, vbr_drift, vbr_offset, vbr_count;
   float overlap_max, stereo_saving;
   int intensity;
   float spec_avg;
   float in_mem[2*CELT_OVERLAP], prefilter_mem[2*COMBFILTER_MAXPERIOD];
   float oldBandE[2*CELT_NB_EBANDS], oldLogE[2*CELT_NB_EBANDS], oldLogE2[2*CELT_NB_EBANDS];
   float energyError[2*CELT_NB_EBANDS];
} fl_celt_enc;

static void fl_celt_enc_reset(fl_celt_enc *st) {
   int i;
   memset(&st->rng, 0, sizeof(*st)-offsetof(fl_celt_enc, rng));
   for (i = 0; i < 2*CELT_NB_EBANDS; i++) st->oldLogE[i] = st->oldLogE2[i] = -28.f;
   st->vbr_offset = 0;
   st->delayedIntra = 1;
   st->spread_decision = SPREAD_NORMAL;
   st->tonal_average = 256;
   st->hf_average = 0;
   st->tapset_decision = 0;
}
static void fl_celt_enc_init(fl_celt_enc *st, int channels) {
   memset(st, 0, sizeof(*st));
   st->stream_channels = st->channels = channels;
   st->start = 0;
   st->end = CELT_NB_EBANDS;
   st->constrained_vbr = 1;
   st->clip = 1;
   st->bitrate = -1;
   st->vbr = 0;
   st->force_intra = 0;
   st->complexity = 5;
   st->lsb_depth = 24;
   fl_celt_enc_reset(st);
}

static float fl_e_maxabs(const float *x, int len) {
   int i;
   float maxval = 0, minval = 0;
   for (i = 0; i < len; i++) {
      maxval = FL_MAX(maxval, x[i]);
      minval = FL_MIN(minval, x[i]);
   }
   return FL_MAX(maxval, -minval);
}

static void fl_e_preemphasis(const float *pcm, float *inp, int N, int CC, float *mem, int clip) {
   int i;
   xf m = *mem;
   const float coef0 = fl_preemph[0];
   if (!clip) {
      for (i = 0; i < N; i++) {
         float x = pcm[CC*i]*32768.f;
         inp[i] = (float)(x-m);
         m = XF(coef0)*x;
      }
   } else {
      for (i = 0; i < N; i++) {
         float x = pcm[CC*i]*32768.f;
         inp[i] = FL_MAX(-65536.f, FL_MIN(65536.f, x));
      }
      for (i = 0; i < N; i++) {
         float x = inp[i];
         inp[i] = (float)(x-m);
         m = XF(coef0)*x;
      }
   }
   *mem = (float)m;
}

static int fl_e_tone_lpc(const float *x, int len, int delay, float *lpc) {
   int i;
   xf r00 = 0, r01 = 0, r02 = 0, r11, r12, r22, edges, num0, num1, den;
   for (i = 0; i < len-2*delay; i++) {
      r00 += XF(x[i])*x[i];
      r01 += XF(x[i])*x[i+delay];
      r02 += XF(x[i])*x[i+2*delay];
   }
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-2*delay])*x[len+i-2*delay]-XF(x[i])*x[i];
   r11 = r00+edges;
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-delay])*x[len+i-delay]-XF(x[i+delay])*x[i+delay];
   r22 = r11+edges;
   edges = 0;
   for (i = 0; i < delay; i++) edges += XF(x[len+i-2*delay])*x[len+i-delay]-XF(x[i])*x[i+delay];
   r12 = r01+edges;
   {
      xf R00 = r00+r22, R01 = r01+r12, R11 = 2*r11, R02 = 2*r02, R12 = r12+r01;
      r00 = R00; r01 = R01; r11 = R11; r02 = R02; r12 = R12;
   }
   den = r00*r11-r01*r01;
   if (den < XF(.001f)*(r00*r11)) return 1;
   num1 = r02*r11-r01*r12;
   if (num1 >= den) lpc[1] = 1.f;
   else if (num1 <= -den) lpc[1] = -1.f;
   else lpc[1] = (float)(num1/den);
   num0 = r00*r12-r02*r01;
   if (XF(.5f)*num0 >= den) lpc[0] = 1.999999f;
   else if (XF(.5f)*num0 <= -den) lpc[0] = -1.999999f;
   else lpc[0] = (float)(num0/den);
   return 0;
}
static float fl_e_tone_detect(const float *in, int CC, int N, float *toneishness, int Fs) {
   int i, delay = 1, fail;
   float lpc[2] = {0, 0}, x[1080];
   if (CC == 2) for (i = 0; i < N; i++) x[i] = in[i]+in[i+N];
   else for (i = 0; i < N; i++) x[i] = in[i];
   fail = fl_e_tone_lpc(x, N, delay, lpc);
   while (delay <= Fs/3000 && (fail || (lpc[0] > 1.f && lpc[1] < 0))) {
      delay *= 2;
      fail = fl_e_tone_lpc(x, N, delay, lpc);
   }
   if (!fail && XF(lpc[0])*lpc[0]+XF(3.999999)*lpc[1] < 0) {
      *toneishness = -lpc[1];
      return (float)(XF(fl_m_acos((double)(.5f*lpc[0])))/delay);
   }
   *toneishness = 0;
   return -1;
}

static int fl_e_transient_analysis(const float *in, int len, int C, float *tf_estimate, int *tf_chan,
      int allow_weak_transients, int *weak_transient, float tone_freq, float toneishness) {
   static const unsigned char inv_table[128] = {
      255, 255, 156, 110, 86, 70, 59, 51, 45, 40, 37, 33, 31, 28, 26, 25,
      23, 22, 21, 20, 19, 18, 17, 16, 16, 15, 15, 14, 13, 13, 12, 12,
      12, 12, 11, 11, 11, 10, 10, 10, 9, 9, 9, 9, 9, 9, 8, 8,
      8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3,
      3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2};
   int i, c, is_transient, mask_metric = 0, len2 = len/2;
   float tmp[1080], forward_decay = allow_weak_transients ? .03125f : .0625f;
   *weak_transient = 0;
   for (c = 0; c < C; c++) {
      xf mem0 = 0, mem1 = 0, mean = 0, maxE = 0, norm;
      float mean_f;
      int unmask = 0;
      for (i = 0; i < len; i++) {
         float x = in[i+c*len];
         xf y = mem0+x, mem00 = mem0;
         mem0 = (mem0-x)+XF(.5f)*mem1;
         mem1 = x-mem00;
         tmp[i] = (float)y;
      }
      memset(tmp, 0, 12*sizeof(float));
      mem0 = 0;
      for (i = 0; i < len2; i++) {
         xf x2 = XF(tmp[2*i])*tmp[2*i]+XF(tmp[2*i+1])*tmp[2*i+1];
         mean += x2;
         mem0 = x2+(1.0L-forward_decay)*mem0;
         tmp[i] = (float)(XF(forward_decay)*mem0);
      }
      mem0 = 0;
      for (i = len2-1; i >= 0; i--) {
         xf v;
         mem0 = XF(.875f)*mem0+tmp[i];
         v = XF(.125f)*mem0;
         tmp[i] = (float)v;
         maxE = maxE > v ? maxE : v;
      }
      mean_f = (float)fl_m_sqrt((double)(((mean*maxE)*.5f)*len2));
      norm = XF(len2)/(XF(mean_f)+1e-15f);
      for (i = 12; i < len2-5; i += 4) {
         xf n = i == 12 ? norm : XF((float)norm);
         float fl = floorf((float)((XF(64.f)*n)*(XF(tmp[i])+1e-15f)));
         int id = fl > 127 ? 127 : (0 > fl ? 0 : (int)fl);
         unmask += inv_table[id];
      }
      unmask = 64*unmask*4/(6*(len2-17));
      if (unmask > mask_metric) {
         *tf_chan = c;
         mask_metric = unmask;
      }
   }
   is_transient = mask_metric > 200;
   if (toneishness > .98f && tone_freq < .026f) {
      is_transient = 0;
      mask_metric = 0;
   }
   if (allow_weak_transients && is_transient && mask_metric < 600) {
      is_transient = 0;
      *weak_transient = 1;
   }
   {
      xf tf_max = XF((float)fl_m_sqrt((double)(27*mask_metric)))-42, v;
      if (0 > tf_max) tf_max = 0;
      v = (163 < tf_max ? XF(163) : tf_max)*XF(.0069f)-XF(0.139);
      if (0 > v) v = 0;
      *tf_estimate = (float)fl_m_sqrt((double)v);
   }
   return is_transient;
}

static int fl_e_patch_transient_decision(const float *newE, const float *oldE, int start, int end, int C) {
   int i, c;
   xf mean_diff = 0;
   float spread_old[26];
   if (C == 1) {
      spread_old[start] = oldE[start];
      for (i = start+1; i < end; i++) {
         xf a = XF(spread_old[i-1])-1;
         spread_old[i] = (float)(a > oldE[i] ? a : XF(oldE[i]));
      }
   } else {
      spread_old[start] = FL_MAX(oldE[start], oldE[start+CELT_NB_EBANDS]);
      for (i = start+1; i < end; i++) {
         xf a = XF(spread_old[i-1])-1;
         float b = FL_MAX(oldE[i], oldE[i+CELT_NB_EBANDS]);
         spread_old[i] = (float)(a > b ? a : XF(b));
      }
   }
   for (i = end-2; i >= start; i--) {
      xf a = XF(spread_old[i+1])-1;
      spread_old[i] = (float)(spread_old[i] > a ? XF(spread_old[i]) : a);
   }
   for (c = 0; c < C; c++)
      for (i = IMAX(2, start); i < end-1; i++) {
         float x1 = FL_MAX(0, newE[i+c*CELT_NB_EBANDS]), x2 = FL_MAX(0, spread_old[i]);
         xf d = XF(x1)-x2;
         mean_diff += 0 > d ? 0 : d;
      }
   mean_diff = mean_diff/(C*(end-1-IMAX(2, start)));
   return mean_diff > 1;
}

static xf fl_e_l1_metric(const float *tmp, int N, int LM, float bias) {
   int i;
   xf L1 = 0;
   for (i = 0; i < N; i++) L1 += fabsf(tmp[i]);
   return L1+(XF(bias)*LM)*L1;
}
static int fl_e_tf_analysis(int len, int isTransient, int *tf_res, int lambda, float *X, int N0, int LM,
      float tf_estimate, int tf_chan, const int *importance) {
   int i, sel, cost0, cost1, tf_select = 0, selcost[2], metric[CELT_NB_EBANDS], path0[CELT_NB_EBANDS], path1[CELT_NB_EBANDS];
   float tmp[176], tmp_1[176], bias;
   {
      xf d = XF(.5f)-tf_estimate;
      bias = (float)((-.25f > d ? XF(-.25f) : d)*.04f);
   }
   for (i = 0; i < len; i++) {
      int k, N, narrow, best_level = 0;
      xf L1;
      float best_L1;
      N = (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM;
      narrow = (fl_eband5ms[i+1]-fl_eband5ms[i]) == 1;
      memcpy(tmp, &X[tf_chan*N0+(fl_eband5ms[i]<<LM)], N*sizeof(float));
      L1 = fl_e_l1_metric(tmp, N, isTransient ? LM : 0, bias);
      best_L1 = (float)L1;
      if (isTransient && !narrow) {
         memcpy(tmp_1, tmp, N*sizeof(float));
         fl_e_haar1(tmp_1, N>>LM, 1<<LM);
         L1 = fl_e_l1_metric(tmp_1, N, LM+1, bias);
         if (L1 < best_L1) {
            best_L1 = (float)L1;
            best_level = -1;
         }
      }
      for (k = 0; k < LM+!(isTransient || narrow); k++) {
         int B = isTransient ? LM-k-1 : k+1;
         fl_e_haar1(tmp, N>>k, 1<<k);
         L1 = fl_e_l1_metric(tmp, N, B, bias);
         if (L1 < best_L1) {
            best_L1 = (float)L1;
            best_level = k+1;
         }
      }
      metric[i] = isTransient ? 2*best_level : -2*best_level;
      if (narrow && (metric[i] == 0 || metric[i] == -2*LM)) metric[i] -= 1;
   }
   for (sel = 0; sel < 2; sel++) {
      cost0 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*sel+0]);
      cost1 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*sel+1])+(isTransient ? 0 : lambda);
      for (i = 1; i < len; i++) {
         int curr0 = IMIN(cost0, cost1+lambda), curr1 = IMIN(cost0+lambda, cost1);
         cost0 = curr0+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*sel+0]);
         cost1 = curr1+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*sel+1]);
      }
      selcost[sel] = IMIN(cost0, cost1);
   }
   if (selcost[1] < selcost[0] && isTransient) tf_select = 1;
   cost0 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+0]);
   cost1 = importance[0]*abs(metric[0]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+1])+(isTransient ? 0 : lambda);
   for (i = 1; i < len; i++) {
      int curr0, curr1, from0, from1;
      from0 = cost0;
      from1 = cost1+lambda;
      if (from0 < from1) {
         curr0 = from0;
         path0[i] = 0;
      } else {
         curr0 = from1;
         path0[i] = 1;
      }
      from0 = cost0+lambda;
      from1 = cost1;
      if (from0 < from1) {
         curr1 = from0;
         path1[i] = 0;
      } else {
         curr1 = from1;
         path1[i] = 1;
      }
      cost0 = curr0+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+0]);
      cost1 = curr1+importance[i]*abs(metric[i]-2*fl_tf_select_table[LM][4*isTransient+2*tf_select+1]);
   }
   tf_res[len-1] = cost0 < cost1 ? 0 : 1;
   for (i = len-2; i >= 0; i--) tf_res[i] = tf_res[i+1] == 1 ? path1[i+1] : path0[i+1];
   return tf_select;
}
static void fl_e_tf_encode(int start, int end, int isTransient, int *tf_res, int LM, int tf_select, ec_ctx *enc) {
   int curr, i, tf_select_rsv, tf_changed, logp;
   fl_u32 budget = enc->storage*8, tell = ec_tell(enc);
   logp = isTransient ? 2 : 4;
   tf_select_rsv = LM > 0 && tell+logp+1 <= budget;
   budget -= tf_select_rsv;
   curr = tf_changed = 0;
   for (i = start; i < end; i++) {
      if (tell+logp <= budget) {
         ec_enc_bit_logp(enc, tf_res[i]^curr, logp);
         tell = ec_tell(enc);
         curr = tf_res[i];
         tf_changed |= curr;
      } else tf_res[i] = curr;
      logp = isTransient ? 4 : 5;
   }
   if (tf_select_rsv && fl_tf_select_table[LM][4*isTransient+0+tf_changed] != fl_tf_select_table[LM][4*isTransient+2+tf_changed])
      ec_enc_bit_logp(enc, tf_select, 1);
   else tf_select = 0;
   for (i = start; i < end; i++) tf_res[i] = fl_tf_select_table[LM][4*isTransient+2*tf_select+tf_res[i]];
}

static int fl_e_hysteresis_decision(float val, const float *thresholds, const float *hysteresis, int N, int prev) {
   int i;
   for (i = 0; i < N; i++)
      if (val < thresholds[i]) break;
   if (i > prev && val < XF(thresholds[prev])+hysteresis[prev]) i = prev;
   if (i < prev && val > XF(thresholds[prev-1])-hysteresis[prev-1]) i = prev;
   return i;
}

static float fl_e_median_of_5(const float *x) {
   float t0, t1, t2, t3, t4, tmp;
   t2 = x[2];
   if (x[0] > x[1]) { t0 = x[1]; t1 = x[0]; } else { t0 = x[0]; t1 = x[1]; }
   if (x[3] > x[4]) { t3 = x[4]; t4 = x[3]; } else { t3 = x[3]; t4 = x[4]; }
   if (t0 > t3) {
      tmp = t0; t0 = t3; t3 = tmp;
      tmp = t1; t1 = t4; t4 = tmp;
   }
   if (t2 > t1) {
      if (t1 < t3) return FL_MIN(t2, t3);
      return FL_MIN(t4, t1);
   }
   if (t2 < t3) return FL_MIN(t1, t3);
   return FL_MIN(t2, t4);
}
static float fl_e_median_of_3(const float *x) {
   float t0, t1, t2;
   if (x[0] > x[1]) { t0 = x[1]; t1 = x[0]; } else { t0 = x[0]; t1 = x[1]; }
   t2 = x[2];
   if (t1 < t2) return t1;
   if (t0 < t2) return t2;
   return t0;
}

static float fl_e_dynalloc_analysis(const float *bandLogE, const float *bandLogE2, const float *oldBandE, int start,
      int end, int C, int *offsets, int lsb_depth, int isTransient, int vbr, int constrained_vbr, int LM,
      int effectiveBytes, fl_i32 *tot_boost_, const fl_analysis_info *analysis, int *importance,
      int *spread_weight, float tone_freq, float toneishness, int lfe, const float *surround_dynalloc) {
   const fl_i16 *eBands = fl_eband5ms;
   int i, c;
   fl_i32 tot_boost = 0;
   float maxDepth, follower[2*CELT_NB_EBANDS], noise_floor[CELT_NB_EBANDS], bandLogE3[CELT_NB_EBANDS];
   memset(offsets, 0, CELT_NB_EBANDS*sizeof(int));
   maxDepth = -31.9f;
   for (i = 0; i < end; i++)
      noise_floor[i] = (float)((((XF(.0625f)*fl_logN400[i]+.5f)+(9-lsb_depth))-fl_eMeans[i])+(XF(.0062f)*(i+5))*(i+5));
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++) {
         xf d = XF(bandLogE[c*CELT_NB_EBANDS+i])-noise_floor[i];
         if (!(maxDepth > d)) maxDepth = (float)d;
      }
   {
      float mask[CELT_NB_EBANDS], sig[CELT_NB_EBANDS];
      for (i = 0; i < end; i++) mask[i] = bandLogE[i]-noise_floor[i];
      if (C == 2)
         for (i = 0; i < end; i++) {
            xf d = XF(bandLogE[CELT_NB_EBANDS+i])-noise_floor[i];
            mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
         }
      memcpy(sig, mask, end*sizeof(float));
      for (i = 1; i < end; i++) {
         xf d = XF(mask[i-1])-2;
         mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
      }
      for (i = end-2; i >= 0; i--) {
         xf d = XF(mask[i+1])-3;
         mask[i] = (float)(mask[i] > d ? XF(mask[i]) : d);
      }
      for (i = 0; i < end; i++) {
         xf md = XF(maxDepth)-12, t, smr;
         int shift;
         md = 0 > md ? 0 : md;
         t = md > mask[i] ? md : XF(mask[i]);
         smr = sig[i]-t;
         shift = IMIN(5, IMAX(0, -(int)floorf((float)(smr+.5f))));
         spread_weight[i] = 32>>shift;
      }
   }
   if (effectiveBytes >= (30+5*LM) && !lfe) {
      int last = 0;
      for (c = 0; c < C; c++) {
         float *f = &follower[c*CELT_NB_EBANDS], tmp;
         memcpy(bandLogE3, &bandLogE2[c*CELT_NB_EBANDS], end*sizeof(float));
         if (LM == 0)
            for (i = 0; i < IMIN(8, end); i++)
               bandLogE3[i] = FL_MAX(bandLogE2[c*CELT_NB_EBANDS+i], oldBandE[c*CELT_NB_EBANDS+i]);
         f[0] = bandLogE3[0];
         for (i = 1; i < end; i++) {
            xf a;
            if (bandLogE3[i] > XF(bandLogE3[i-1])+.5f) last = i;
            a = XF(f[i-1])+1.5f;
            f[i] = (float)(a < bandLogE3[i] ? a : XF(bandLogE3[i]));
         }
         for (i = last-1; i >= 0; i--) {
            xf a = XF(f[i+1])+2, m = a < bandLogE3[i] ? a : XF(bandLogE3[i]);
            f[i] = (float)(f[i] < m ? XF(f[i]) : m);
         }
         for (i = 2; i < end-2; i++) {
            xf m = XF(fl_e_median_of_5(&bandLogE3[i-2]))-1;
            f[i] = (float)(f[i] > m ? XF(f[i]) : m);
         }
         {
            xf m = XF(fl_e_median_of_3(&bandLogE3[0]))-1;
            f[0] = (float)(f[0] > m ? XF(f[0]) : m);
            f[1] = (float)(f[1] > m ? XF(f[1]) : m);
            m = XF(fl_e_median_of_3(&bandLogE3[end-3]))-1;
            f[end-2] = (float)(f[end-2] > m ? XF(f[end-2]) : m);
            f[end-1] = (float)(f[end-1] > m ? XF(f[end-1]) : m);
         }
         for (i = 0; i < end; i++) f[i] = FL_MAX(f[i], noise_floor[i]);
         (void)tmp;
      }
      if (C == 2) {
         for (i = start; i < end; i++) {
            float a = follower[CELT_NB_EBANDS+i], b = follower[i];
            xf M = XF(b)-4, F0, d0, d1;
            M = a > M ? XF(a) : M;
            follower[CELT_NB_EBANDS+i] = (float)M;
            F0 = M-4;
            F0 = b > F0 ? XF(b) : F0;
            follower[i] = (float)F0;
            d0 = XF(bandLogE[i])-F0;
            d1 = XF(bandLogE[CELT_NB_EBANDS+i])-follower[CELT_NB_EBANDS+i];
            follower[i] = (float)(((0 > d0 ? 0 : d0)+(0 > d1 ? 0 : d1))*.5f);
         }
      } else {
         for (i = start; i < end; i++) follower[i] = FL_MAX(0, bandLogE[i]-follower[i]);
      }
      for (i = start; i < end; i++) follower[i] = FL_MAX(follower[i], surround_dynalloc[i]);
      for (i = start; i < end; i++)
         importance[i] = (int)floorf((float)(XF(fl_e_exp2(FL_MIN(follower[i], 4.f)))*13+.5f));
      if ((!vbr || constrained_vbr) && !isTransient)
         for (i = start; i < end; i++) follower[i] = .5f*follower[i];
      for (i = start; i < end; i++) {
         if (i < 8) follower[i] *= 2;
         if (i >= 12) follower[i] = .5f*follower[i];
      }
      if (toneishness > .98f) {
         int freq_bin = (int)floor((double)(XF(tone_freq)*120.f/XF(3.141592653589793)+.5f));
         for (i = start; i < end; i++) {
            if (freq_bin >= eBands[i] && freq_bin <= eBands[i+1]) follower[i] += 2.f;
            if (freq_bin >= eBands[i]-1 && freq_bin <= eBands[i+1]+1) follower[i] += 1.f;
            if (freq_bin >= eBands[i]-2 && freq_bin <= eBands[i+1]+2) follower[i] += 1.f;
            if (freq_bin >= eBands[i]-3 && freq_bin <= eBands[i+1]+3) follower[i] += .5f;
         }
         if (freq_bin >= eBands[end]) {
            follower[end-1] += 2.f;
            follower[end-2] += 1.f;
         }
      }
      if (analysis->valid)
         for (i = start; i < IMIN(LEAK_BANDS, end); i++)
            follower[i] = (float)(XF(1.f/64.f)*analysis->leak_boost[i]+follower[i]);
      for (i = start; i < end; i++) {
         int width, boost, boost_bits;
         follower[i] = FL_MIN(follower[i], 4.f);
         width = C*(eBands[i+1]-eBands[i])<<LM;
         if (width < 6) {
            boost = (int)follower[i];
            boost_bits = boost*width<<BITRES;
         } else if (width > 48) {
            boost = (int)(follower[i]*8);
            boost_bits = (boost*width<<BITRES)/8;
         } else {
            boost = (int)(float)((XF(follower[i])*width)/6.f);
            boost_bits = boost*6<<BITRES;
         }
         if ((!vbr || (constrained_vbr && !isTransient)) && (tot_boost+boost_bits)>>BITRES>>3 > 2*effectiveBytes/3) {
            fl_i32 cap = ((2*effectiveBytes/3)<<BITRES<<3);
            offsets[i] = cap-tot_boost;
            tot_boost = cap;
            break;
         } else {
            offsets[i] = boost;
            tot_boost += boost_bits;
         }
      }
   } else {
      for (i = start; i < end; i++) importance[i] = 13;
   }
   *tot_boost_ = tot_boost;
   return maxDepth;
}

static int fl_e_run_prefilter(fl_celt_enc *st, float *in, int CC, int N, int prefilter_tapset, int *pitch,
      float *gain, int *qgain, int enabled, int complexity, float tf_estimate, int nbAvailableBytes,
      const fl_analysis_info *analysis, float tone_freq, float toneishness) {
   const int max_period = COMBFILTER_MAXPERIOD, min_period = COMBFILTER_MINPERIOD, overlap = CELT_OVERLAP;
   float _pre[2*(960+COMBFILTER_MAXPERIOD)], *pre[2], before[2] = {0, 0}, after[2] = {0, 0}, gain_f;
   int c, pitch_index, pf_on, qg, cancel_pitch = 0;
   xf gain1, pf_threshold;
   pre[0] = _pre;
   pre[1] = _pre+(N+max_period);
   for (c = 0; c < CC; c++) {
      memcpy(pre[c], st->prefilter_mem+c*max_period, max_period*sizeof(float));
      memcpy(pre[c]+max_period, in+c*(N+overlap)+overlap, N*sizeof(float));
   }
   if (enabled && toneishness > .99f) {
      int multiple = 1;
      if (tone_freq >= 3.1416f) tone_freq = 3.141593f-tone_freq;
      while (tone_freq >= XF(.39f)*multiple) multiple++;
      if (tone_freq > .006148f)
         pitch_index = IMIN((int)(double)((XF(6.283185307179586)*multiple)/tone_freq+.5f), COMBFILTER_MAXPERIOD-2);
      else pitch_index = COMBFILTER_MINPERIOD;
      gain1 = .75f;
   } else if (enabled && complexity >= 5) {
      float pitch_buf[(COMBFILTER_MAXPERIOD+960)>>1];
      fl_e_pitch_downsample(pre, pitch_buf, (max_period+N)>>1, CC);
      fl_e_pitch_search(pitch_buf+(max_period>>1), pitch_buf, N, max_period-3*min_period, &pitch_index);
      pitch_index = max_period-pitch_index;
      gain1 = fl_e_remove_doubling(pitch_buf, max_period, min_period, N, &pitch_index, st->prefilter_period,
            st->prefilter_gain);
      if (pitch_index > max_period-2) pitch_index = max_period-2;
      gain1 = XF(.7f)*gain1;
      if (st->loss_rate > 2) gain1 = XF(.5f)*gain1;
      if (st->loss_rate > 4) gain1 = XF(.5f)*gain1;
      if (st->loss_rate > 8) gain1 = 0;
   } else {
      gain1 = 0;
      pitch_index = COMBFILTER_MINPERIOD;
   }
   if (analysis->valid) gain1 = gain1*analysis->max_pitch_ratio;
   pf_threshold = .2f;
   if (abs(pitch_index-st->prefilter_period)*10 > pitch_index) {
      pf_threshold += .2f;
      if (tf_estimate > .98f) gain1 = 0;
   }
   if (nbAvailableBytes < 25) pf_threshold += .1f;
   if (nbAvailableBytes < 35) pf_threshold += .1f;
   if (st->prefilter_gain > .4f) pf_threshold -= .1f;
   if (st->prefilter_gain > .55f) pf_threshold -= .1f;
   if (!(pf_threshold > .2f)) pf_threshold = .2f;
   if (gain1 < pf_threshold) {
      gain_f = 0;
      pf_on = 0;
      qg = 0;
   } else {
      if (fabsl(gain1-st->prefilter_gain) < .1f) gain1 = st->prefilter_gain;
      qg = (int)floorf((float)((gain1*32.f)/3.f+.5f))-1;
      qg = IMAX(0, IMIN(7, qg));
      gain_f = (float)(XF(.09375f)*(qg+1));
      pf_on = 1;
   }
   for (c = 0; c < CC; c++) {
      int i;
      float *y = in+c*(N+overlap)+overlap;
      st->prefilter_period = IMAX(st->prefilter_period, COMBFILTER_MINPERIOD);
      memcpy(in+c*(N+overlap), st->in_mem+c*overlap, overlap*sizeof(float));
      for (i = 0; i < N; i++) before[c] = before[c]+fabsf(y[i]);
      fl_e_comb_filter(y, pre[c]+max_period, st->prefilter_period, pitch_index, N, -st->prefilter_gain, -gain_f,
            st->prefilter_tapset, prefilter_tapset, fl_window120, overlap);
      for (i = 0; i < N; i++) after[c] = after[c]+fabsf(y[i]);
   }
   if (CC == 2) {
      xf g = XF(.25f)*gain_f;
      xf thresh0 = g*before[0]+XF(.01f)*before[1], thresh1 = g*before[1]+XF(.01f)*before[0];
      if (XF(after[0])-before[0] > thresh0 || XF(after[1])-before[1] > thresh1) cancel_pitch = 1;
      if (XF(before[0])-after[0] < thresh0 && XF(before[1])-after[1] < thresh1) cancel_pitch = 1;
   } else {
      if (after[0] > before[0]) cancel_pitch = 1;
   }
   if (cancel_pitch) {
      for (c = 0; c < CC; c++) {
         memcpy(in+c*(N+overlap)+overlap, pre[c]+max_period, N*sizeof(float));
         fl_e_comb_filter(in+c*(N+overlap)+overlap, pre[c]+max_period, st->prefilter_period, pitch_index, overlap,
               -st->prefilter_gain, -0.f, st->prefilter_tapset, prefilter_tapset, fl_window120, overlap);
      }
      gain_f = 0;
      pf_on = 0;
      qg = 0;
   }
   for (c = 0; c < CC; c++) {
      memcpy(st->in_mem+c*overlap, in+c*(N+overlap)+N, overlap*sizeof(float));
      memmove(st->prefilter_mem+c*max_period, st->prefilter_mem+c*max_period+N, (max_period-N)*sizeof(float));
      memcpy(st->prefilter_mem+c*max_period+max_period-N, pre[c]+max_period, N*sizeof(float));
   }
   *gain = gain_f;
   *pitch = pitch_index;
   *qgain = qg;
   return pf_on;
}

static int fl_celt_encode(fl_celt_enc *st, const float *pcm, int frame_size, unsigned char *compressed,
      int nbCompressedBytes, ec_ctx *enc) {
   static const float intensity_thresholds[21] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 24, 36, 44, 50, 56, 62, 67, 72, 79, 88, 106, 134};
   static const float intensity_histeresis[21] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 6, 8, 8};
   const fl_i16 *eBands = fl_eband5ms;
   const int CC = st->channels, C = st->stream_channels, overlap = CELT_OVERLAP, nbEBands = CELT_NB_EBANDS;
   int i, c, N, LM, M, tf_select, nbFilledBytes, nbAvailableBytes, start = st->start, end = st->end, effEnd;
   int codedBands, alloc_trim, pitch_index = COMBFILTER_MINPERIOD, dual_stereo = 0, effectiveBytes;
   int dynalloc_logp, prefilter_tapset = 0, pf_on, anti_collapse_rsv, anti_collapse_on = 0, silence = 0;
   int tf_chan = 0, pitch_change = 0, shortBlocks = 0, isTransient = 0, secondMdct, signalBandwidth;
   int transient_got_disabled = 0, hybrid = start != 0, weak_transient = 0, enable_tf_analysis;
   fl_i32 bits, vbr_rate, total_bits, total_boost, balance, tell, tell0_frac, tot_boost, equiv_rate, min_allowed;
   float gain1 = 0, tf_estimate = 0, sample_max, maxDepth, temporal_vbr = 0, tone_freq, toneishness = 0;
   float surround_masking = 0, surround_trim = 0, surround_dynalloc[2*CELT_NB_EBANDS];
   float in[2*(960+CELT_OVERLAP)], freq[2*960], X[2*960];
   float bandE[2*CELT_NB_EBANDS], bandLogE[2*CELT_NB_EBANDS], bandLogE2[2*CELT_NB_EBANDS], error[2*CELT_NB_EBANDS];
   int fine_quant[CELT_NB_EBANDS], pulses[CELT_NB_EBANDS], cap[CELT_NB_EBANDS], offsets[CELT_NB_EBANDS];
   int importance[CELT_NB_EBANDS], spread_weight[CELT_NB_EBANDS], fine_priority[CELT_NB_EBANDS], tf_res[CELT_NB_EBANDS];
   unsigned char collapse_masks[2*CELT_NB_EBANDS];
   ec_ctx _enc;
   float *oldBandE = st->oldBandE, *oldLogE = st->oldLogE, *oldLogE2 = st->oldLogE2, *energyError = st->energyError;
   if (nbCompressedBytes < 2 || pcm == NULL) return FL_ERR_BAD_ARG;
   for (LM = 0; LM <= CELT_MAX_LM; LM++)
      if (CELT_SHORT_MDCT<<LM == frame_size) break;
   if (LM > CELT_MAX_LM) return FL_ERR_BAD_ARG;
   M = 1<<LM;
   N = M*CELT_SHORT_MDCT;
   if (enc == NULL) {
      tell0_frac = tell = 1;
      nbFilledBytes = 0;
   } else {
      tell0_frac = ec_tell_frac(enc);
      tell = ec_tell(enc);
      nbFilledBytes = (tell+4)>>3;
   }
   nbCompressedBytes = IMIN(nbCompressedBytes, 1275);
   if (st->vbr && st->bitrate != -1) {
      vbr_rate = (st->bitrate*6/(6*48000/frame_size))<<BITRES;
      effectiveBytes = vbr_rate>>(3+BITRES);
   } else {
      fl_i32 tmp;
      vbr_rate = 0;
      tmp = st->bitrate*frame_size;
      if (tell > 1) tmp += tell*48000;
      if (st->bitrate != -1) {
         nbCompressedBytes = IMAX(2, IMIN(nbCompressedBytes, (tmp+4*48000)/(8*48000)));
         if (enc != NULL) ec_enc_shrink(enc, nbCompressedBytes);
      }
      effectiveBytes = nbCompressedBytes-nbFilledBytes;
   }
   nbAvailableBytes = nbCompressedBytes-nbFilledBytes;
   equiv_rate = ((fl_i32)nbCompressedBytes*8*50<<(3-LM))-(40*C+20)*((400>>LM)-50);
   if (st->bitrate != -1) equiv_rate = IMIN(equiv_rate, st->bitrate-(40*C+20)*((400>>LM)-50));
   if (enc == NULL) {
      ec_enc_init(&_enc, compressed, nbCompressedBytes);
      enc = &_enc;
   }
   if (vbr_rate > 0 && st->constrained_vbr) {
      fl_i32 max_allowed = IMIN(IMAX(tell == 1 ? 2 : 0, (vbr_rate+vbr_rate-st->vbr_reservoir)>>(BITRES+3)), nbAvailableBytes);
      if (max_allowed < nbAvailableBytes) {
         nbCompressedBytes = nbFilledBytes+max_allowed;
         nbAvailableBytes = max_allowed;
         ec_enc_shrink(enc, nbCompressedBytes);
      }
   }
   total_bits = nbCompressedBytes*8;
   effEnd = end;
   if (effEnd > CELT_NB_EBANDS) effEnd = CELT_NB_EBANDS;
   sample_max = FL_MAX(st->overlap_max, fl_e_maxabs(pcm, C*(N-overlap)));
   st->overlap_max = fl_e_maxabs(pcm+C*(N-overlap), C*overlap);
   sample_max = FL_MAX(sample_max, st->overlap_max);
   silence = sample_max <= XF(1)/(1<<st->lsb_depth);
   if (tell == 1) ec_enc_bit_logp(enc, silence, 15);
   else silence = 0;
   if (silence) {
      if (vbr_rate > 0) {
         effectiveBytes = nbCompressedBytes = IMIN(nbCompressedBytes, nbFilledBytes+2);
         total_bits = nbCompressedBytes*8;
         nbAvailableBytes = 2;
         ec_enc_shrink(enc, nbCompressedBytes);
      }
      tell = nbCompressedBytes*8;
      enc->nbits_total += tell-ec_tell(enc);
   }
   for (c = 0; c < CC; c++) {
      int need_clip = st->clip && sample_max > 65536.f;
      fl_e_preemphasis(pcm+c, in+c*(N+overlap)+overlap, N, CC, st->preemph_memE+c, need_clip);
      memcpy(in+c*(N+overlap), &st->prefilter_mem[(1+c)*COMBFILTER_MAXPERIOD-overlap], overlap*sizeof(float));
   }
   tone_freq = fl_e_tone_detect(in, CC, N+overlap, &toneishness, 48000);
   if (st->complexity >= 1 && !st->lfe) {
      int allow_weak_transients = hybrid && effectiveBytes < 15 && st->silk_signal_type != 2;
      isTransient = fl_e_transient_analysis(in, N+overlap, CC, &tf_estimate, &tf_chan, allow_weak_transients,
            &weak_transient, tone_freq, toneishness);
   }
   {
      xf t = 1.0L-tf_estimate;
      toneishness = (float)(t < toneishness ? t : XF(toneishness));
   }
   {
      int enabled, qg;
      enabled = ((st->lfe && nbAvailableBytes > 3) || nbAvailableBytes > 12*C) && !hybrid && !silence && tell+16 <= total_bits && !st->disable_pf;
      prefilter_tapset = st->tapset_decision;
      pf_on = fl_e_run_prefilter(st, in, CC, N, prefilter_tapset, &pitch_index, &gain1, &qg, enabled, st->complexity,
            tf_estimate, nbAvailableBytes, &st->analysis, tone_freq, toneishness);
      if ((gain1 > .4f || st->prefilter_gain > .4f) && (!st->analysis.valid || st->analysis.tonality > .3)
            && (pitch_index > XF(1.26)*st->prefilter_period || pitch_index < XF(.79)*st->prefilter_period))
         pitch_change = 1;
      if (pf_on == 0) {
         if (!hybrid && tell+16 <= total_bits) ec_enc_bit_logp(enc, 0, 1);
      } else {
         int octave;
         ec_enc_bit_logp(enc, 1, 1);
         pitch_index += 1;
         octave = EC_ILOG(pitch_index)-5;
         ec_enc_uint(enc, octave, 6);
         ec_enc_bits(enc, pitch_index-(16<<octave), 4+octave);
         pitch_index -= 1;
         ec_enc_bits(enc, qg, 3);
         ec_enc_icdf(enc, prefilter_tapset, fl_tapset_icdf, 2);
      }
   }
   if (LM > 0 && ec_tell(enc)+3 <= total_bits) {
      if (isTransient) shortBlocks = M;
   } else {
      isTransient = 0;
      transient_got_disabled = 1;
   }
   secondMdct = shortBlocks && st->complexity >= 8;
   if (secondMdct) {
      fl_e_compute_mdcts(0, in, freq, C, CC, LM);
      fl_e_band_energies(freq, bandE, effEnd, C, LM);
      fl_e_amp2log2(effEnd, end, bandE, bandLogE2, C);
      for (c = 0; c < C; c++)
         for (i = 0; i < end; i++) bandLogE2[nbEBands*c+i] = (float)(XF(.5f)*LM+bandLogE2[nbEBands*c+i]);
   }
   fl_e_compute_mdcts(shortBlocks, in, freq, C, CC, LM);
   if (CC == 2 && C == 1) tf_chan = 0;
   fl_e_band_energies(freq, bandE, effEnd, C, LM);
   if (st->lfe) {
      for (i = 2; i < end; i++) {
         xf lim = XF(1e-4f)*bandE[0];
         xf v = lim < bandE[i] ? lim : XF(bandE[i]);
         bandE[i] = (float)(v < XF(1e-15f) ? XF(1e-15f) : v);
      }
   }
   fl_e_amp2log2(effEnd, end, bandE, bandLogE, C);
   memset(surround_dynalloc, 0, end*sizeof(float));
   if (!hybrid && st->energy_mask && !st->lfe) {
      /* masking between surround channels; all sums stay in x87 registers */
      int mask_end = IMAX(2, st->lastCodedBands), midband, count_dynalloc = 0, count = 0;
      xf mask_avg = 0, diff = 0;
      for (c = 0; c < C; c++)
         for (i = 0; i < mask_end; i++) {
            float m = st->energy_mask[nbEBands*c+i];
            xf mask;
            m = m < .25f ? m : .25f;
            m = m > -2.f ? m : -2.f;
            mask = m;
            if (mask > 0) mask = XF(.5f)*mask;
            mask_avg += mask*(eBands[i+1]-eBands[i]);
            count += eBands[i+1]-eBands[i];
            diff += mask*(1+2*i-mask_end);
         }
      mask_avg = mask_avg/count+XF(.2f);
      diff = (diff*XF(6.f))/(C*(mask_end-1)*(mask_end+1)*mask_end);
      diff = XF(.5f)*diff;
      diff = diff < XF(.031f) ? diff : XF(.031f);
      diff = diff > XF(-.031f) ? diff : XF(-.031f);
      for (midband = 0; eBands[midband+1] < eBands[mask_end]/2; midband++);
      for (i = 0; i < mask_end; i++) {
         xf lin = diff*(i-midband)+mask_avg, unmask;
         float u = st->energy_mask[i];
         if (C == 2 && st->energy_mask[nbEBands+i] > u) u = st->energy_mask[nbEBands+i];
         u = u < 0.f ? u : 0.f;
         unmask = XF(u)-lin;
         if (unmask > XF(.25f)) {
            surround_dynalloc[i] = (float)(unmask-XF(.25f));
            count_dynalloc++;
         }
      }
      if (count_dynalloc >= 3) {
         mask_avg += XF(.25f);
         if (mask_avg > 0) {
            mask_avg = 0;
            diff = 0;
            memset(surround_dynalloc, 0, mask_end*sizeof(float));
         } else {
            for (i = 0; i < mask_end; i++) {
               xf d = XF(surround_dynalloc[i])-XF(.25f);
               surround_dynalloc[i] = (float)(d > 0 ? d : XF(0));
            }
         }
      }
      surround_masking = (float)(mask_avg+XF(.2f));
      surround_trim = (float)(XF(64.f)*diff);
   }
   if (!st->lfe) {
      xf follow = -10.f, frame_avg = 0, offset = shortBlocks ? XF(.5f)*LM : 0, tv;
      for (i = start; i < end; i++) {
         xf a = follow-1, b = XF(bandLogE[i])-offset;
         follow = a > b ? a : b;
         if (C == 2) {
            b = XF(bandLogE[i+nbEBands])-offset;
            follow = follow > b ? follow : b;
         }
         frame_avg += follow;
      }
      frame_avg /= (end-start);
      tv = frame_avg-st->spec_avg;
      tv = -1.5f > tv ? XF(-1.5f) : tv;
      temporal_vbr = (float)(3.f < tv ? XF(3.f) : tv);
      st->spec_avg = (float)(XF(temporal_vbr)*.02f+st->spec_avg);
   }
   if (!secondMdct) memcpy(bandLogE2, bandLogE, C*nbEBands*sizeof(float));
   if (LM > 0 && ec_tell(enc)+3 <= total_bits && !isTransient && st->complexity >= 5 && !st->lfe && !hybrid) {
      if (fl_e_patch_transient_decision(bandLogE, oldBandE, start, end, C)) {
         isTransient = 1;
         shortBlocks = M;
         fl_e_compute_mdcts(shortBlocks, in, freq, C, CC, LM);
         fl_e_band_energies(freq, bandE, effEnd, C, LM);
         fl_e_amp2log2(effEnd, end, bandE, bandLogE, C);
         for (c = 0; c < C; c++)
            for (i = 0; i < end; i++) bandLogE2[nbEBands*c+i] = (float)(XF(.5f)*LM+bandLogE2[nbEBands*c+i]);
         tf_estimate = .2f;
      }
   }
   if (LM > 0 && ec_tell(enc)+3 <= total_bits) ec_enc_bit_logp(enc, isTransient, 3);
   fl_e_normalise_bands(freq, X, bandE, effEnd, C, M);
   enable_tf_analysis = effectiveBytes >= 15*C && !hybrid && st->complexity >= 2 && !st->lfe && toneishness < .98f;
   maxDepth = fl_e_dynalloc_analysis(bandLogE, bandLogE2, oldBandE, start, end, C, offsets, st->lsb_depth,
         isTransient, st->vbr, st->constrained_vbr, LM, effectiveBytes, &tot_boost, &st->analysis, importance,
         spread_weight, tone_freq, toneishness, st->lfe, surround_dynalloc);
   if (enable_tf_analysis) {
      int lambda = IMAX(80, 20480/effectiveBytes+2);
      tf_select = fl_e_tf_analysis(effEnd, isTransient, tf_res, lambda, X, N, LM, tf_estimate, tf_chan, importance);
      for (i = effEnd; i < end; i++) tf_res[i] = tf_res[effEnd-1];
   } else if (hybrid && weak_transient) {
      for (i = 0; i < end; i++) tf_res[i] = 1;
      tf_select = 0;
   } else if (hybrid && effectiveBytes < 15 && st->silk_signal_type != 2) {
      for (i = 0; i < end; i++) tf_res[i] = 0;
      tf_select = isTransient;
   } else {
      for (i = 0; i < end; i++) tf_res[i] = isTransient;
      tf_select = 0;
   }
   for (c = 0; c < C; c++)
      for (i = start; i < end; i++) {
         float *e = &bandLogE[i+c*nbEBands];
         if (fabsl(XF(*e)-oldBandE[i+c*nbEBands]) < 2.f)
            *e = (float)(*e-XF(.25f)*energyError[i+c*nbEBands]);
      }
   fl_e_quant_coarse_energy(start, end, effEnd, bandLogE, oldBandE, total_bits, error, enc, C, LM,
         nbAvailableBytes, st->force_intra, &st->delayedIntra, st->complexity >= 4, st->loss_rate, st->lfe);
   fl_e_tf_encode(start, end, isTransient, tf_res, LM, tf_select, enc);
   if (ec_tell(enc)+4 <= total_bits) {
      if (st->lfe) {
         st->tapset_decision = 0;
         st->spread_decision = SPREAD_NORMAL;
      } else if (hybrid) {
         if (st->complexity == 0) st->spread_decision = SPREAD_NONE;
         else if (isTransient) st->spread_decision = SPREAD_NORMAL;
         else st->spread_decision = SPREAD_AGGRESSIVE;
      } else if (shortBlocks || st->complexity < 3 || nbAvailableBytes < 10*C) {
         st->spread_decision = st->complexity == 0 ? SPREAD_NONE : SPREAD_NORMAL;
      } else {
         st->spread_decision = fl_e_spreading_decision(X, &st->tonal_average, st->spread_decision, &st->hf_average,
               &st->tapset_decision, pf_on && !shortBlocks, effEnd, C, M, spread_weight);
      }
      ec_enc_icdf(enc, st->spread_decision, fl_spread_icdf, 5);
   } else st->spread_decision = SPREAD_NORMAL;
   if (st->lfe) offsets[0] = IMIN(8, effectiveBytes/3);
   fl_init_caps(cap, LM, C);
   dynalloc_logp = 6;
   total_bits <<= BITRES;
   total_boost = 0;
   tell = ec_tell_frac(enc);
   for (i = start; i < end; i++) {
      int width, quanta, dynalloc_loop_logp, boost, j;
      width = C*(eBands[i+1]-eBands[i])<<LM;
      quanta = IMIN(width<<BITRES, IMAX(6<<BITRES, width));
      dynalloc_loop_logp = dynalloc_logp;
      boost = 0;
      for (j = 0; tell+(dynalloc_loop_logp<<BITRES) < total_bits-total_boost && boost < cap[i]; j++) {
         int flag = j < offsets[i];
         ec_enc_bit_logp(enc, flag, dynalloc_loop_logp);
         tell = ec_tell_frac(enc);
         if (!flag) break;
         boost += quanta;
         total_boost += quanta;
         dynalloc_loop_logp = 1;
      }
      if (j) dynalloc_logp = IMAX(2, dynalloc_logp-1);
      offsets[i] = boost;
   }
   if (C == 2) {
      if (LM != 0) {
         /* stereo_analysis */
         xf sumLR = 1e-15f, sumMS = 1e-15f;
         int thetas;
         for (i = 0; i < 13; i++) {
            int j;
            for (j = eBands[i]<<LM; j < eBands[i+1]<<LM; j++) {
               float L = X[j], R = X[N+j];
               xf Mx = XF(L)+R, S = XF(L)-R;
               sumLR += fabsl(XF(L))+fabsl(XF(R));
               sumMS += fabsl(Mx)+fabsl(S);
            }
         }
         sumMS = XF(.707107f)*sumMS;
         thetas = 13;
         if (LM <= 1) thetas -= 8;
         dual_stereo = ((eBands[13]<<(LM+1))+thetas)*sumMS > (eBands[13]<<(LM+1))*sumLR;
      }
      st->intensity = fl_e_hysteresis_decision((float)(equiv_rate/1000), intensity_thresholds,
            intensity_histeresis, 21, st->intensity);
      st->intensity = IMIN(end, IMAX(start, st->intensity));
   }
   alloc_trim = 5;
   if (tell+(6<<BITRES) <= total_bits-total_boost) {
      if (start > 0 || st->lfe) {
         st->stereo_saving = 0;
         alloc_trim = 5;
      } else {
         /* alloc_trim_analysis */
         float trim = 5.f;
         xf diff = 0, t;
         if (equiv_rate < 64000) trim = 4.f;
         else if (equiv_rate < 80000) trim = (float)(XF(1.f/16.f)*((equiv_rate-64000)>>10)+4.f);
         if (C == 2) {
            float sum = 0, minXC, logXC, half;
            xf logXC2, l2, ss;
            for (i = 0; i < 8; i++)
               sum = (float)(sum+fl_e_inner_prod(&X[eBands[i]<<LM], &X[N+(eBands[i]<<LM)], (eBands[i+1]-eBands[i])<<LM));
            sum = .125f*sum;
            sum = FL_MIN(1.f, fabsf(sum));
            minXC = sum;
            for (i = 8; i < st->intensity; i++) {
               float p = (float)fabsl(fl_e_inner_prod(&X[eBands[i]<<LM], &X[N+(eBands[i]<<LM)], (eBands[i+1]-eBands[i])<<LM));
               minXC = FL_MIN(minXC, p);
            }
            minXC = FL_MIN(1.f, fabsf(minXC));
            {
               xf lx = fl_e_log2((float)(XF(1.001f)-XF(sum)*sum));
               half = (float)(XF(.5f)*lx);
               logXC = (float)lx;
            }
            l2 = fl_e_log2((float)(XF(1.001f)-XF(minXC)*minXC));
            logXC2 = half > l2 ? XF(half) : l2;
            {
               xf a = XF(.75f)*logXC;
               a = -4.f > a ? XF(-4.f) : a;
               trim = (float)(a+trim);
            }
            ss = XF(.25f)+st->stereo_saving;
            {
               xf b = -(XF(.5f)*logXC2);
               st->stereo_saving = (float)(ss < b ? ss : b);
            }
         }
         for (c = 0; c < C; c++)
            for (i = 0; i < end-1; i++) diff += XF(bandLogE[i+c*nbEBands])*(2+2*i-end);
         diff /= C*(end-1);
         t = (diff+1)/6.f;
         t = 2.f < t ? XF(2.f) : t;
         t = -2.f > t ? XF(-2.f) : t;
         t = XF(trim)-t;
         t = t-XF(surround_trim);
         t = t-XF(tf_estimate)*2;
         if (st->analysis.valid) {
            xf a = (XF(st->analysis.tonality_slope)+.05f)*2;
            a = 2.f < a ? XF(2.f) : a;
            a = -2.f > a ? XF(-2.f) : a;
            t = t-a;
         }
         alloc_trim = (int)floorf((float)(t+.5f));
         alloc_trim = IMAX(0, IMIN(10, alloc_trim));
      }
      ec_enc_icdf(enc, alloc_trim, fl_trim_icdf, 7);
      tell = ec_tell_frac(enc);
   }
   min_allowed = ((tell+total_boost+(1<<(BITRES+3))-1)>>(BITRES+3))+2;
   if (hybrid) min_allowed = IMAX(min_allowed, (tell0_frac+(37<<BITRES)+total_boost+(1<<(BITRES+3))-1)>>(BITRES+3));
   if (vbr_rate > 0) {
      xf alpha;
      fl_i32 delta, target, base_target;
      int lm_diff = CELT_MAX_LM-LM;
      nbCompressedBytes = IMIN(nbCompressedBytes, 1275>>(3-LM));
      if (!hybrid) base_target = vbr_rate-((40*C+20)<<BITRES);
      else base_target = IMAX(0, vbr_rate-((9*C+4)<<BITRES));
      if (st->constrained_vbr) base_target += (st->vbr_offset>>lm_diff);
      if (!hybrid) {
         /* compute_vbr */
         int coded_bands = st->lastCodedBands ? st->lastCodedBands : nbEBands, coded_bins;
         coded_bins = eBands[coded_bands]<<LM;
         if (C == 2) coded_bins += eBands[IMIN(st->intensity, coded_bands)]<<LM;
         target = base_target;
         if (st->analysis.valid && st->analysis.activity < .4)
            target -= (fl_i32)(float)((XF(.4f)-st->analysis.activity)*(coded_bins<<BITRES));
         if (C == 2) {
            int coded_stereo_bands = IMIN(st->intensity, coded_bands), coded_stereo_dof;
            xf max_frac, a, b;
            float ss = FL_MIN(st->stereo_saving, 1.f);
            coded_stereo_dof = (eBands[coded_stereo_bands]<<LM)-coded_stereo_bands;
            max_frac = (XF(.8f)*coded_stereo_dof)/coded_bins;
            a = max_frac*target;
            b = (XF(ss)-.1f)*(coded_stereo_dof<<BITRES);
            target -= (fl_i32)(float)(a < b ? a : b);
         }
         target += tot_boost-(19<<LM);
         target += (fl_i32)(float)((XF(tf_estimate)-.044f)*target);
         if (st->analysis.valid && !st->lfe) {
            xf tonal = XF(st->analysis.tonality)-.15f;
            fl_i32 tonal_target;
            tonal = (0 > tonal ? XF(0) : tonal)-.12f;
            tonal_target = target+(fl_i32)(float)((XF(coded_bins<<BITRES)*1.2f)*tonal);
            if (pitch_change) tonal_target += (fl_i32)(float)(XF(coded_bins<<BITRES)*.8f);
            target = tonal_target;
         }
         if (st->energy_mask && !st->lfe) {
            fl_i32 surround_target = target+(fl_i32)(float)(XF(surround_masking)*(coded_bins<<BITRES));
            target = IMAX(target/4, surround_target);
         }
         {
            fl_i32 floor_depth;
            int bins = eBands[nbEBands-2]<<LM;
            floor_depth = (fl_i32)(float)(XF(C*bins<<BITRES)*maxDepth);
            floor_depth = IMAX(floor_depth, target>>2);
            target = IMIN(target, floor_depth);
         }
         if ((!st->energy_mask || st->lfe) && st->constrained_vbr)
            target = base_target+(fl_i32)(float)(XF(.67f)*(target-base_target));
         if (!st->energy_mask && tf_estimate < .2f) {
            xf amount = XF(IMAX(0, IMIN(32000, 96000-equiv_rate)))*3.1e-6f;
            target += (fl_i32)(float)(((amount)*temporal_vbr)*target);
         }
         target = IMIN(2*base_target, target);
      } else {
         target = base_target;
         if (st->silk_offset < 100) target += 12<<BITRES>>(3-LM);
         if (st->silk_offset > 100) target -= 18<<BITRES>>(3-LM);
         target += (fl_i32)(float)((XF(tf_estimate)-.25f)*400.f);
         if (tf_estimate > .7f) target = IMAX(target, 50<<BITRES);
      }
      target = target+tell;
      nbAvailableBytes = (target+(1<<(BITRES+2)))>>(BITRES+3);
      nbAvailableBytes = IMAX(min_allowed, nbAvailableBytes);
      nbAvailableBytes = IMIN(nbCompressedBytes, nbAvailableBytes);
      delta = target-vbr_rate;
      target = nbAvailableBytes<<(BITRES+3);
      if (silence) {
         nbAvailableBytes = 2;
         target = 2*8<<BITRES;
         delta = 0;
      }
      if (st->vbr_count < 970) {
         st->vbr_count++;
         alpha = 1.0L/(st->vbr_count+20);
      } else alpha = .001f;
      if (st->constrained_vbr) st->vbr_reservoir += target-vbr_rate;
      if (st->constrained_vbr) {
         st->vbr_drift += (fl_i32)(float)(alpha*((delta*(1<<lm_diff))-st->vbr_offset-st->vbr_drift));
         st->vbr_offset = -st->vbr_drift;
      }
      if (st->constrained_vbr && st->vbr_reservoir < 0) {
         int adjust = (-st->vbr_reservoir)/(8<<BITRES);
         nbAvailableBytes += silence ? 0 : adjust;
         st->vbr_reservoir = 0;
      }
      nbCompressedBytes = IMIN(nbCompressedBytes, nbAvailableBytes);
      ec_enc_shrink(enc, nbCompressedBytes);
   }
   bits = (((fl_i32)nbCompressedBytes*8)<<BITRES)-(fl_i32)ec_tell_frac(enc)-1;
   anti_collapse_rsv = isTransient && LM >= 2 && bits >= ((LM+2)<<BITRES) ? (1<<BITRES) : 0;
   bits -= anti_collapse_rsv;
   signalBandwidth = end-1;
   if (st->analysis.valid) {
      int min_bandwidth;
      if (equiv_rate < (fl_i32)32000*C) min_bandwidth = 13;
      else if (equiv_rate < (fl_i32)48000*C) min_bandwidth = 16;
      else if (equiv_rate < (fl_i32)60000*C) min_bandwidth = 18;
      else if (equiv_rate < (fl_i32)80000*C) min_bandwidth = 19;
      else min_bandwidth = 20;
      signalBandwidth = IMAX(st->analysis.bandwidth, min_bandwidth);
   }
   if (st->lfe) signalBandwidth = 1;
   codedBands = fl_compute_allocation(start, end, offsets, cap, alloc_trim, &st->intensity, &dual_stereo, bits,
         &balance, pulses, fine_quant, fine_priority, C, LM, enc, 1, st->lastCodedBands, signalBandwidth);
   if (st->lastCodedBands) st->lastCodedBands = IMIN(st->lastCodedBands+1, IMAX(st->lastCodedBands-1, codedBands));
   else st->lastCodedBands = codedBands;
   fl_e_quant_fine_energy(start, end, oldBandE, error, fine_quant, enc, C);
   memset(energyError, 0, nbEBands*CC*sizeof(float));
   fl_e_quant_all_bands(start, end, X, C == 2 ? X+N : NULL, collapse_masks, bandE, pulses, shortBlocks,
         st->spread_decision, dual_stereo, st->intensity, tf_res, nbCompressedBytes*(8<<BITRES)-anti_collapse_rsv,
         balance, enc, LM, codedBands, &st->rng, st->complexity, st->disable_inv);
   if (anti_collapse_rsv > 0) {
      anti_collapse_on = st->consec_transient < 2;
      ec_enc_bits(enc, anti_collapse_on, 1);
   }
   fl_e_quant_energy_finalise(start, end, oldBandE, error, fine_quant, fine_priority, nbCompressedBytes*8-ec_tell(enc), enc, C);
   for (c = 0; c < C; c++)
      for (i = start; i < end; i++)
         energyError[i+c*nbEBands] = FL_MAX(-.5f, FL_MIN(.5f, error[i+c*nbEBands]));
   if (silence)
      for (i = 0; i < C*nbEBands; i++) oldBandE[i] = -28.f;
   st->prefilter_period = pitch_index;
   st->prefilter_gain = gain1;
   st->prefilter_tapset = prefilter_tapset;
   if (CC == 2 && C == 1) memcpy(&oldBandE[nbEBands], oldBandE, nbEBands*sizeof(float));
   if (!isTransient) {
      memcpy(oldLogE2, oldLogE, CC*nbEBands*sizeof(float));
      memcpy(oldLogE, oldBandE, CC*nbEBands*sizeof(float));
   } else {
      for (i = 0; i < CC*nbEBands; i++) oldLogE[i] = FL_MIN(oldLogE[i], oldBandE[i]);
   }
   for (c = 0; c < CC; c++) {
      for (i = 0; i < start; i++) {
         oldBandE[c*nbEBands+i] = 0;
         oldLogE[c*nbEBands+i] = oldLogE2[c*nbEBands+i] = -28.f;
      }
      for (i = end; i < nbEBands; i++) {
         oldBandE[c*nbEBands+i] = 0;
         oldLogE[c*nbEBands+i] = oldLogE2[c*nbEBands+i] = -28.f;
      }
   }
   if (isTransient || transient_got_disabled) st->consec_transient++;
   else st->consec_transient = 0;
   st->rng = enc->rng;
   ec_enc_done(enc);
   if (enc->error) return -3;
   return nbCompressedBytes;
}
