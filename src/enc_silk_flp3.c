/* ---- SILK encoder: pitch analysis, noise shaping and frame encoding (float, x87 rounding) ---- */
#define PE_SCRATCH_SIZE 22

static void silk_P_Ana_calc_corr_st3(float cross_corr_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS],
      const float frame[], int start_lag, int sf_length, int nb_subfr, int complexity) {
   const float *target_ptr;
   int i, j, k, lag_counter, lag_low, lag_high, nb_cbk_search, delta, idx, cbk_size;
   float scratch_mem[PE_SCRATCH_SIZE], xcorr[PE_SCRATCH_SIZE];
   const fl_i8 *Lag_range_ptr, *Lag_CB_ptr;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      Lag_range_ptr = &fl_Lag_range_stage3[complexity][0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      nb_cbk_search = fl_nb_cbk_searchs_stage3[complexity];
      cbk_size = PE_NB_CBKS_STAGE3_MAX;
   } else {
      Lag_range_ptr = &fl_Lag_range_stage3_10_ms[0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
      cbk_size = PE_NB_CBKS_STAGE3_10MS;
   }
   target_ptr = &frame[sf_length<<2];
   for (k = 0; k < nb_subfr; k++) {
      lag_counter = 0;
      lag_low = Lag_range_ptr[k*2];
      lag_high = Lag_range_ptr[k*2+1];
      fl_pitch_xcorr(target_ptr, target_ptr-start_lag-lag_high, xcorr, sf_length, lag_high-lag_low+1);
      for (j = lag_low; j <= lag_high; j++) scratch_mem[lag_counter++] = xcorr[lag_high-j];
      delta = Lag_range_ptr[k*2];
      for (i = 0; i < nb_cbk_search; i++) {
         idx = Lag_CB_ptr[k*cbk_size+i]-delta;
         for (j = 0; j < PE_NB_STAGE3_LAGS; j++) cross_corr_st3[k][i][j] = scratch_mem[idx+j];
      }
      target_ptr += sf_length;
   }
}
static void silk_P_Ana_calc_energy_st3(float energies_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS],
      const float frame[], int start_lag, int sf_length, int nb_subfr, int complexity) {
   const float *target_ptr, *basis_ptr;
   xf energy;
   int k, i, j, lag_counter, nb_cbk_search, delta, idx, cbk_size, lag_diff;
   float scratch_mem[PE_SCRATCH_SIZE];
   const fl_i8 *Lag_range_ptr, *Lag_CB_ptr;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      Lag_range_ptr = &fl_Lag_range_stage3[complexity][0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      nb_cbk_search = fl_nb_cbk_searchs_stage3[complexity];
      cbk_size = PE_NB_CBKS_STAGE3_MAX;
   } else {
      Lag_range_ptr = &fl_Lag_range_stage3_10_ms[0][0];
      Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
      cbk_size = PE_NB_CBKS_STAGE3_10MS;
   }
   target_ptr = &frame[sf_length<<2];
   for (k = 0; k < nb_subfr; k++) {
      lag_counter = 0;
      basis_ptr = target_ptr-(start_lag+Lag_range_ptr[k*2]);
      energy = silk_energy_FLP(basis_ptr, sf_length)+1e-3;
      scratch_mem[lag_counter++] = (float)energy;
      lag_diff = Lag_range_ptr[k*2+1]-Lag_range_ptr[k*2]+1;
      for (i = 1; i < lag_diff; i++) {
         energy -= XF(basis_ptr[sf_length-i])*basis_ptr[sf_length-i];
         energy += XF(basis_ptr[-i])*basis_ptr[-i];
         scratch_mem[lag_counter++] = (float)energy;
      }
      delta = Lag_range_ptr[k*2];
      for (i = 0; i < nb_cbk_search; i++) {
         idx = Lag_CB_ptr[k*cbk_size+i]-delta;
         for (j = 0; j < PE_NB_STAGE3_LAGS; j++) energies_st3[k][i][j] = scratch_mem[idx+j];
      }
      target_ptr += sf_length;
   }
}

static int silk_pitch_analysis_core_FLP(const float *frame, int *pitch_out, fl_i16 *lagIndex, fl_i8 *contourIndex,
      float *LTPCorr, int prevLag, const float search_thres1, const float search_thres2, const int Fs_kHz,
      const int complexity, const int nb_subfr) {
   int i, k, d, j;
   float frame_8kHz[PE_MAX_FRAME_LENGTH_MS*8], frame_4kHz[PE_MAX_FRAME_LENGTH_MS*4];
   fl_i16 frame_8_FIX[PE_MAX_FRAME_LENGTH_MS*8], frame_4_FIX[PE_MAX_FRAME_LENGTH_MS*4];
   fl_i32 filt_state[6];
   float C[PE_MAX_NB_SUBFR][(PE_MAX_LAG>>1)+5], xcorr[PE_MAX_LAG_MS*4-PE_MIN_LAG_MS*4+1], CC[PE_NB_CBKS_STAGE2_EXT];
   const float *target_ptr, *basis_ptr;
   int d_srch[PE_D_SRCH_LENGTH], length_d_srch, length_d_comp;
   fl_i16 d_comp[(PE_MAX_LAG>>1)+5];
   float Cmax, CCmax, CCmax_b, CCmax_new, lag_log2, prevLag_log2;
   int CBimax, CBimax_new, lag, start_lag, end_lag, lag_new, cbk_size, nb_cbk_search, lag_counter;
   float energies_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS];
   float cross_corr_st3[PE_MAX_NB_SUBFR][PE_NB_CBKS_STAGE3_MAX][PE_NB_STAGE3_LAGS];
   int frame_length, frame_length_8kHz, frame_length_4kHz, sf_length, sf_length_8kHz, min_lag, min_lag_8kHz;
   int min_lag_4kHz, max_lag, max_lag_8kHz, max_lag_4kHz;
   const fl_i8 *Lag_CB_ptr;
   frame_length = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*Fs_kHz;
   frame_length_4kHz = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*4;
   frame_length_8kHz = (PE_LTP_MEM_LENGTH_MS+nb_subfr*PE_SUBFR_LENGTH_MS)*8;
   sf_length = PE_SUBFR_LENGTH_MS*Fs_kHz;
   sf_length_8kHz = PE_SUBFR_LENGTH_MS*8;
   min_lag = PE_MIN_LAG_MS*Fs_kHz;
   min_lag_4kHz = PE_MIN_LAG_MS*4;
   min_lag_8kHz = PE_MIN_LAG_MS*8;
   max_lag = PE_MAX_LAG_MS*Fs_kHz-1;
   max_lag_4kHz = PE_MAX_LAG_MS*4;
   max_lag_8kHz = PE_MAX_LAG_MS*8-1;
   if (Fs_kHz == 16) {
      fl_i16 frame_16_FIX[16*PE_MAX_FRAME_LENGTH_MS];
      for (k = frame_length-1; k >= 0; k--) frame_16_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
      memset(filt_state, 0, 2*sizeof(fl_i32));
      silk_resampler_down2(filt_state, frame_8_FIX, frame_16_FIX, frame_length);
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8kHz[k] = frame_8_FIX[k];
   } else if (Fs_kHz == 12) {
      fl_i16 frame_12_FIX[12*PE_MAX_FRAME_LENGTH_MS];
      for (k = frame_length-1; k >= 0; k--) frame_12_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
      memset(filt_state, 0, 6*sizeof(fl_i32));
      silk_resampler_down2_3(filt_state, frame_8_FIX, frame_12_FIX, frame_length);
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8kHz[k] = frame_8_FIX[k];
   } else {
      for (k = frame_length_8kHz-1; k >= 0; k--) frame_8_FIX[k] = (fl_i16)silk_SAT16(lrintf(frame[k]));
   }
   memset(filt_state, 0, 2*sizeof(fl_i32));
   silk_resampler_down2(filt_state, frame_4_FIX, frame_8_FIX, frame_length_8kHz);
   for (k = frame_length_4kHz-1; k >= 0; k--) frame_4kHz[k] = frame_4_FIX[k];
   for (i = frame_length_4kHz-1; i > 0; i--) {
      float s = frame_4kHz[i]+frame_4kHz[i-1];
      frame_4kHz[i] = (fl_i16)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
   }
   memset(C, 0, sizeof(float)*nb_subfr*((PE_MAX_LAG>>1)+5));
   target_ptr = &frame_4kHz[sf_length_8kHz/2*4];
   for (k = 0; k < nb_subfr>>1; k++) {
      double cross_corr, e1;
      xf normalizer;
      basis_ptr = target_ptr-min_lag_4kHz;
      fl_pitch_xcorr(target_ptr, target_ptr-max_lag_4kHz, xcorr, sf_length_8kHz, max_lag_4kHz-min_lag_4kHz+1);
      cross_corr = xcorr[max_lag_4kHz-min_lag_4kHz];
      e1 = (double)silk_energy_FLP(target_ptr, sf_length_8kHz);
      normalizer = (silk_energy_FLP(basis_ptr, sf_length_8kHz)+e1)+sf_length_8kHz*4000.0f;
      C[0][min_lag_4kHz] = C[0][min_lag_4kHz]+(float)((XF(cross_corr)*2)/normalizer);
      for (d = min_lag_4kHz+1; d <= max_lag_4kHz; d++) {
         basis_ptr--;
         cross_corr = xcorr[max_lag_4kHz-d];
         normalizer += XF(basis_ptr[0])*basis_ptr[0]-XF(basis_ptr[sf_length_8kHz])*basis_ptr[sf_length_8kHz];
         C[0][d] = C[0][d]+(float)((XF(cross_corr)*2)/normalizer);
      }
      target_ptr += sf_length_8kHz;
   }
   for (i = max_lag_4kHz; i >= min_lag_4kHz; i--) C[0][i] = (float)(C[0][i]-(XF(C[0][i])*i)*(1.0f/4096.0f));
   length_d_srch = 4+2*complexity;
   silk_insertion_sort_decreasing_FLP(&C[0][min_lag_4kHz], d_srch, max_lag_4kHz-min_lag_4kHz+1, length_d_srch);
   Cmax = C[0][min_lag_4kHz];
   if (Cmax < 0.2f) {
      memset(pitch_out, 0, nb_subfr*sizeof(int));
      *LTPCorr = 0;
      *lagIndex = 0;
      *contourIndex = 0;
      return 1;
   }
   {
      xf threshold = XF(Cmax)*search_thres1;
      for (i = 0; i < length_d_srch; i++) {
         if (C[0][min_lag_4kHz+i] > threshold) d_srch[i] = (d_srch[i]+min_lag_4kHz)<<1;
         else {
            length_d_srch = i;
            break;
         }
      }
   }
   for (i = min_lag_8kHz-5; i < max_lag_8kHz+5; i++) d_comp[i] = 0;
   for (i = 0; i < length_d_srch; i++) d_comp[d_srch[i]] = 1;
   for (i = max_lag_8kHz+3; i >= min_lag_8kHz; i--) d_comp[i] += d_comp[i-1]+d_comp[i-2];
   length_d_srch = 0;
   for (i = min_lag_8kHz; i < max_lag_8kHz+1; i++)
      if (d_comp[i+1] > 0) d_srch[length_d_srch++] = i;
   for (i = max_lag_8kHz+3; i >= min_lag_8kHz; i--) d_comp[i] += d_comp[i-1]+d_comp[i-2]+d_comp[i-3];
   length_d_comp = 0;
   for (i = min_lag_8kHz; i < max_lag_8kHz+4; i++)
      if (d_comp[i] > 0) d_comp[length_d_comp++] = (fl_i16)(i-2);
   memset(C, 0, PE_MAX_NB_SUBFR*((PE_MAX_LAG>>1)+5)*sizeof(float));
   target_ptr = Fs_kHz == 8 ? &frame[PE_LTP_MEM_LENGTH_MS*8] : &frame_8kHz[PE_LTP_MEM_LENGTH_MS*8];
   for (k = 0; k < nb_subfr; k++) {
      double energy_tmp = (double)(silk_energy_FLP(target_ptr, sf_length_8kHz)+1.0);
      for (j = 0; j < length_d_comp; j++) {
         xf cc;
         d = d_comp[j];
         basis_ptr = target_ptr-d;
         cc = silk_inner_product_FLP(basis_ptr, target_ptr, sf_length_8kHz);
         if (cc > 0) {
            double cross_corr = (double)cc;
            C[k][d] = (float)((XF(cross_corr)*2)/(silk_energy_FLP(basis_ptr, sf_length_8kHz)+energy_tmp));
         } else C[k][d] = 0;
      }
      target_ptr += sf_length_8kHz;
   }
   CCmax = 0;
   CCmax_b = -1000.0f;
   CBimax = 0;
   lag = -1;
   if (prevLag > 0) {
      if (Fs_kHz == 12) prevLag = (prevLag<<1)/3;
      else if (Fs_kHz == 16) prevLag = prevLag>>1;
      prevLag_log2 = silk_log2((double)(float)prevLag);
   } else prevLag_log2 = 0;
   if (nb_subfr == PE_MAX_NB_SUBFR) {
      cbk_size = PE_NB_CBKS_STAGE2_EXT;
      Lag_CB_ptr = &fl_CB_lags_stage2[0][0];
      nb_cbk_search = Fs_kHz == 8 && complexity > SILK_PE_MIN_COMPLEX ? PE_NB_CBKS_STAGE2_EXT : PE_NB_CBKS_STAGE2;
   } else {
      cbk_size = PE_NB_CBKS_STAGE2_10MS;
      Lag_CB_ptr = &fl_CB_lags_stage2_10_ms[0][0];
      nb_cbk_search = PE_NB_CBKS_STAGE2_10MS;
   }
   for (k = 0; k < length_d_srch; k++) {
      xf ccb;
      d = d_srch[k];
      for (j = 0; j < nb_cbk_search; j++) {
         xf s = 0;
         for (i = 0; i < nb_subfr; i++) s += C[i][d+Lag_CB_ptr[i*cbk_size+j]];
         CC[j] = (float)s;
      }
      CCmax_new = -1000.0f;
      CBimax_new = 0;
      for (i = 0; i < nb_cbk_search; i++)
         if (CC[i] > CCmax_new) {
            CCmax_new = CC[i];
            CBimax_new = i;
         }
      lag_log2 = silk_log2((double)(float)d);
      ccb = CCmax_new-(XF(.2f)*nb_subfr)*lag_log2;
      if (prevLag > 0) {
         xf dl = XF(lag_log2)-prevLag_log2;
         dl *= dl;
         ccb -= (((XF(.2f)*nb_subfr)**LTPCorr)*dl)/(dl+.5f);
      }
      if (ccb > CCmax_b && CCmax_new > XF(search_thres2)*nb_subfr) {
         CCmax_b = (float)ccb;
         CCmax = CCmax_new;
         lag = d;
         CBimax = CBimax_new;
      }
   }
   if (lag == -1) {
      memset(pitch_out, 0, PE_MAX_NB_SUBFR*sizeof(int));
      *LTPCorr = 0;
      *lagIndex = 0;
      *contourIndex = 0;
      return 1;
   }
   *LTPCorr = (float)(XF(CCmax)/nb_subfr);
   if (Fs_kHz > 8) {
      xf energy_tmp, ccmax;
      float contour_bias;
      if (Fs_kHz == 12) lag = silk_RSHIFT_ROUND(silk_SMULBB(lag, 3), 1);
      else lag = lag<<1;
      lag = silk_LIMIT_int(lag, min_lag, max_lag);
      start_lag = silk_max_int(lag-2, min_lag);
      end_lag = silk_min_int(lag+2, max_lag);
      lag_new = lag;
      CBimax = 0;
      ccmax = -1000.0f;
      silk_P_Ana_calc_corr_st3(cross_corr_st3, frame, start_lag, sf_length, nb_subfr, complexity);
      silk_P_Ana_calc_energy_st3(energies_st3, frame, start_lag, sf_length, nb_subfr, complexity);
      lag_counter = 0;
      contour_bias = (float)(XF(0.05f)/lag);
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         nb_cbk_search = (int)fl_nb_cbk_searchs_stage3[complexity];
         cbk_size = PE_NB_CBKS_STAGE3_MAX;
         Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
      } else {
         nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
         cbk_size = PE_NB_CBKS_STAGE3_10MS;
         Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
      }
      target_ptr = &frame[PE_LTP_MEM_LENGTH_MS*Fs_kHz];
      energy_tmp = silk_energy_FLP(target_ptr, nb_subfr*sf_length)+1.0;
      for (d = start_lag; d <= end_lag; d++) {
         for (j = 0; j < nb_cbk_search; j++) {
            xf cross_corr = 0, energy = energy_tmp, cn;
            for (k = 0; k < nb_subfr; k++) {
               cross_corr += cross_corr_st3[k][j][lag_counter];
               energy += energies_st3[k][j][lag_counter];
            }
            if (cross_corr > 0) {
               cn = (float)((cross_corr*2)/energy);
               cn *= 1-XF(contour_bias)*j;
            } else cn = 0;
            if (cn > ccmax && (d+(int)fl_CB_lags_stage3[0][j]) <= max_lag) {
               ccmax = cn;
               lag_new = d;
               CBimax = j;
            }
         }
         lag_counter++;
      }
      for (k = 0; k < nb_subfr; k++) {
         pitch_out[k] = lag_new+Lag_CB_ptr[k*cbk_size+CBimax];
         pitch_out[k] = silk_LIMIT(pitch_out[k], min_lag, PE_MAX_LAG_MS*Fs_kHz);
      }
      *lagIndex = (fl_i16)(lag_new-min_lag);
      *contourIndex = (fl_i8)CBimax;
   } else {
      for (k = 0; k < nb_subfr; k++) {
         pitch_out[k] = lag+Lag_CB_ptr[k*cbk_size+CBimax];
         pitch_out[k] = silk_LIMIT(pitch_out[k], min_lag_8kHz, PE_MAX_LAG_MS*8);
      }
      *lagIndex = (fl_i16)(lag-min_lag_8kHz);
      *contourIndex = (fl_i8)CBimax;
   }
   return 0;
}

static void silk_find_pitch_lags_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, float res[],
      const float x[]) {
   int buf_len;
   float res_nrg, auto_corr[MAX_FIND_PITCH_LPC_ORDER+1], A[MAX_FIND_PITCH_LPC_ORDER];
   float refl_coef[MAX_FIND_PITCH_LPC_ORDER], Wsig[FIND_PITCH_LPC_WIN_MAX], *Wsig_ptr;
   const float *x_buf_ptr, *x_buf;
   buf_len = psEnc->sCmn.la_pitch+psEnc->sCmn.frame_length+psEnc->sCmn.ltp_mem_length;
   x_buf = x-psEnc->sCmn.ltp_mem_length;
   x_buf_ptr = x_buf+buf_len-psEnc->sCmn.pitch_LPC_win_length;
   Wsig_ptr = Wsig;
   silk_apply_sine_window_FLP(Wsig_ptr, x_buf_ptr, 1, psEnc->sCmn.la_pitch);
   Wsig_ptr += psEnc->sCmn.la_pitch;
   x_buf_ptr += psEnc->sCmn.la_pitch;
   memcpy(Wsig_ptr, x_buf_ptr, (psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1))*sizeof(float));
   Wsig_ptr += psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1);
   x_buf_ptr += psEnc->sCmn.pitch_LPC_win_length-(psEnc->sCmn.la_pitch<<1);
   silk_apply_sine_window_FLP(Wsig_ptr, x_buf_ptr, 2, psEnc->sCmn.la_pitch);
   silk_autocorrelation_FLP(auto_corr, Wsig, psEnc->sCmn.pitch_LPC_win_length, psEnc->sCmn.pitchEstimationLPCOrder+1);
   auto_corr[0] = (float)((XF(auto_corr[0])*1e-3f+1)+auto_corr[0]);
   res_nrg = silk_schur_FLP(refl_coef, auto_corr, psEnc->sCmn.pitchEstimationLPCOrder);
   psEncCtrl->predGain = (float)(XF(auto_corr[0])/(res_nrg > 1.0f ? res_nrg : 1.0f));
   silk_k2a_FLP(A, refl_coef, psEnc->sCmn.pitchEstimationLPCOrder);
   silk_bwexpander_FLP(A, psEnc->sCmn.pitchEstimationLPCOrder, 0.99f);
   silk_LPC_analysis_filter_FLP(res, A, x_buf, buf_len, psEnc->sCmn.pitchEstimationLPCOrder);
   if (psEnc->sCmn.indices.signalType != TYPE_NO_VOICE_ACTIVITY && psEnc->sCmn.first_frame_after_reset == 0) {
      xf thr = 0.6f-XF(0.004f)*psEnc->sCmn.pitchEstimationLPCOrder;
      float thrhld;
      thr -= (XF(0.1f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
      thr -= XF(0.15f)*(psEnc->sCmn.prevSignalType>>1);
      thr -= (XF(0.1f)*psEnc->sCmn.input_tilt_Q15)*(1.0f/32768.0f);
      thrhld = (float)thr;
      if (silk_pitch_analysis_core_FLP(res, psEncCtrl->pitchL, &psEnc->sCmn.indices.lagIndex,
            &psEnc->sCmn.indices.contourIndex, &psEnc->LTPCorr, psEnc->sCmn.prevLag,
            (float)(XF(1/65536.0f)*psEnc->sCmn.pitchEstimationThreshold_Q16), thrhld, psEnc->sCmn.fs_kHz,
            psEnc->sCmn.pitchEstimationComplexity, psEnc->sCmn.nb_subfr) == 0)
         psEnc->sCmn.indices.signalType = TYPE_VOICED;
      else psEnc->sCmn.indices.signalType = TYPE_UNVOICED;
   } else {
      memset(psEncCtrl->pitchL, 0, sizeof(psEncCtrl->pitchL));
      psEnc->sCmn.indices.lagIndex = 0;
      psEnc->sCmn.indices.contourIndex = 0;
      psEnc->LTPCorr = 0;
   }
}

static void silk_limit_coefs_FLP(float *coefs, int order) {
   int i, iter, ind = 0;
   for (iter = 0; iter < 10; iter++) {
      float maxabs = -1.0f, chirp;
      for (i = 0; i < order; i++) {
         float tmp = fabsf(coefs[i]);
         if (tmp > maxabs) {
            maxabs = tmp;
            ind = i;
         }
      }
      if (maxabs <= 3.999f) return;
      chirp = (float)(.99f-((XF(.1f)*iter+.8f)*(XF(maxabs)-3.999f))/(XF(maxabs)*(ind+1)));
      silk_bwexpander_FLP(coefs, order, chirp);
   }
}
static void silk_warped_true2monic_coefs(float *coefs, float lambda, int order) {
   int i, iter, ind = 0;
   xf gain, one_m_l2 = 1-XF(lambda)*lambda;
   for (i = order-1; i > 0; i--) coefs[i-1] = (float)(coefs[i-1]-XF(lambda)*coefs[i]);
   gain = one_m_l2/(XF(lambda)*coefs[0]+1);
   for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
   for (iter = 0; iter < 10; iter++) {
      float maxabs = -1.0f, chirp;
      for (i = 0; i < order; i++) {
         float tmp = fabsf(coefs[i]);
         if (tmp > maxabs) {
            maxabs = tmp;
            ind = i;
         }
      }
      if (maxabs <= 3.999f) return;
      for (i = 1; i < order; i++) coefs[i-1] = (float)(coefs[i-1]+XF(lambda)*coefs[i]);
      gain = 1/gain;
      for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
      chirp = (float)(.99f-((XF(.1f)*iter+.8f)*(XF(maxabs)-3.999f))/(XF(maxabs)*(ind+1)));
      /* the hoisted (1-lambda^2) is spilled as float across this call */
      one_m_l2 = (float)one_m_l2;
      silk_bwexpander_FLP(coefs, order, chirp);
      for (i = order-1; i > 0; i--) coefs[i-1] = (float)(coefs[i-1]-XF(lambda)*coefs[i]);
      gain = one_m_l2/(XF(lambda)*coefs[0]+1);
      for (i = 0; i < order; i++) coefs[i] = (float)(coefs[i]*gain);
   }
}

static void silk_noise_shape_analysis_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float *pitch_res, const float *x) {
   silk_shape_state_FLP *psShapeSt = &psEnc->sShape;
   int k, nSamples, nSegs;
   float SNR_adj_dB, BWExp, warping, gain_mult, x_windowed[SHAPE_LPC_WIN_MAX], auto_corr[MAX_SHAPE_LPC_ORDER+1];
   float rc[MAX_SHAPE_LPC_ORDER+1], Tilt, HarmShapeGain_f;
   xf HarmShapeGain, strength, b;
   const float *x_ptr = x-psEnc->sCmn.la_shape, *pitch_res_ptr;
   SNR_adj_dB = psEnc->sCmn.SNR_dB_Q7*(1/128.0f);
   psEncCtrl->input_quality = (float)((XF(.5f)*(psEnc->sCmn.input_quality_bands_Q15[0]+psEnc->sCmn.input_quality_bands_Q15[1]))*(1.0f/32768.0f));
   psEncCtrl->coding_quality = silk_sigmoid_neg((XF(SNR_adj_dB)-20.0f)*-.25f);
   {
      xf snr = SNR_adj_dB;
      if (psEnc->sCmn.useCBR == 0) {
         xf bb = 1-XF(1.0f/256.0f)*psEnc->sCmn.speech_activity_Q8;
         snr -= (((XF(psEncCtrl->coding_quality)*2)*(XF(.5f)*psEncCtrl->input_quality+.5f))*bb)*bb;
      }
      if (psEnc->sCmn.indices.signalType == TYPE_VOICED) snr += XF(psEnc->LTPCorr)*2;
      else snr += ((XF(psEnc->sCmn.SNR_dB_Q7)*-0.4f)*(1/128.0f)+6.0f)*(1-XF(psEncCtrl->input_quality));
      SNR_adj_dB = (float)snr;
   }
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) psEnc->sCmn.indices.quantOffsetType = 0;
   else {
      xf energy_variation = 0;
      float log_energy, log_energy_prev = 0;
      nSamples = 2*psEnc->sCmn.fs_kHz;
      pitch_res_ptr = pitch_res;
      nSegs = silk_SMULBB(SUB_FRAME_LENGTH_MS, psEnc->sCmn.nb_subfr)/2;
      for (k = 0; k < nSegs; k++) {
         float nrg = (float)silk_energy_FLP(pitch_res_ptr, nSamples);
         energy_variation = (float)energy_variation;
         log_energy = silk_log2((double)(XF(nrg)+nSamples));
         if (k > 0) energy_variation += fabsl(XF(log_energy)-log_energy_prev);
         log_energy_prev = log_energy;
         pitch_res_ptr += nSamples;
      }
      psEnc->sCmn.indices.quantOffsetType = energy_variation > XF(0.6f)*(nSegs-1) ? 0 : 1;
   }
   strength = XF(1e-3f)*psEncCtrl->predGain;
   BWExp = (float)(XF(0.94f)/(strength*strength+1));
   warping = (float)(XF(1.52587890625e-05f)*psEnc->sCmn.warping_Q16+XF(0.01f)*psEncCtrl->coding_quality);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      int shift, slope_part, flat_part;
      float nrg, *ar = &psEncCtrl->AR[k*MAX_SHAPE_LPC_ORDER];
      xf g;
      flat_part = psEnc->sCmn.fs_kHz*3;
      slope_part = (psEnc->sCmn.shapeWinLength-flat_part)/2;
      silk_apply_sine_window_FLP(x_windowed, x_ptr, 1, slope_part);
      shift = slope_part;
      memcpy(x_windowed+shift, x_ptr+shift, flat_part*sizeof(float));
      shift += flat_part;
      silk_apply_sine_window_FLP(x_windowed+shift, x_ptr+shift, 2, slope_part);
      x_ptr += psEnc->sCmn.subfr_length;
      if (psEnc->sCmn.warping_Q16 > 0)
         silk_warped_autocorrelation_FLP(auto_corr, x_windowed, warping, psEnc->sCmn.shapeWinLength, psEnc->sCmn.shapingLPCOrder);
      else silk_autocorrelation_FLP(auto_corr, x_windowed, psEnc->sCmn.shapeWinLength, psEnc->sCmn.shapingLPCOrder+1);
      auto_corr[0] = (float)((XF(auto_corr[0])*3e-5f+1.0f)+auto_corr[0]);
      nrg = silk_schur_FLP(rc, auto_corr, psEnc->sCmn.shapingLPCOrder);
      silk_k2a_FLP(ar, rc, psEnc->sCmn.shapingLPCOrder);
      g = fl_m_sqrtf(nrg);
      psEncCtrl->Gains[k] = (float)g;
      if (psEnc->sCmn.warping_Q16 > 0) {
         int i, order = psEnc->sCmn.shapingLPCOrder;
         xf lambda = -XF(warping), gain = ar[order-1];
         for (i = order-2; i >= 0; i--) gain = lambda*gain+ar[i];
         psEncCtrl->Gains[k] = (float)(g*(1/(1-lambda*gain)));
      }
      silk_bwexpander_FLP(ar, psEnc->sCmn.shapingLPCOrder, BWExp);
      if (psEnc->sCmn.warping_Q16 > 0) silk_warped_true2monic_coefs(ar, warping, psEnc->sCmn.shapingLPCOrder);
      else silk_limit_coefs_FLP(ar, psEnc->sCmn.shapingLPCOrder);
   }
   gain_mult = (float)fl_m_pow(2.0, (double)(XF(SNR_adj_dB)*-0.16f));
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++)
      psEncCtrl->Gains[k] = (float)(XF(psEncCtrl->Gains[k])*gain_mult+1.2483305931091309f);
   strength = ((XF(1.0f/32768.0f)*psEnc->sCmn.input_quality_bands_Q15[0]-1)*.5f+1)*4.0f;
   strength *= XF(psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
         b = XF(0.2f)/psEnc->sCmn.fs_kHz+XF(3.0f)/psEncCtrl->pitchL[k];
         psEncCtrl->LF_MA_shp[k] = (float)(b-1);
         psEncCtrl->LF_AR_shp[k] = (float)((1-b)-b*strength);
      }
      Tilt = (float)(-0.25f-(XF(0.26249998807907104f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f));
   } else {
      b = XF(1.3f)/psEnc->sCmn.fs_kHz;
      psEncCtrl->LF_MA_shp[0] = (float)(b-1);
      psEncCtrl->LF_AR_shp[0] = (float)((1-b)-(b*strength)*0.6f);
      for (k = 1; k < psEnc->sCmn.nb_subfr; k++) {
         psEncCtrl->LF_MA_shp[k] = psEncCtrl->LF_MA_shp[0];
         psEncCtrl->LF_AR_shp[k] = psEncCtrl->LF_AR_shp[0];
      }
      Tilt = -0.25f;
   }
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      HarmShapeGain_f = (float)((1-(1-XF(psEncCtrl->coding_quality))*psEncCtrl->input_quality)*0.2f+0.3f);
      HarmShapeGain = fl_m_sqrtf(psEnc->LTPCorr)*HarmShapeGain_f;
   } else HarmShapeGain = 0;
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      psShapeSt->HarmShapeGain_smth = (float)(psShapeSt->HarmShapeGain_smth+(HarmShapeGain-psShapeSt->HarmShapeGain_smth)*0.4f);
      psEncCtrl->HarmShapeGain[k] = psShapeSt->HarmShapeGain_smth;
      psShapeSt->Tilt_smth = (float)(psShapeSt->Tilt_smth+(XF(Tilt)-psShapeSt->Tilt_smth)*0.4f);
      psEncCtrl->Tilt[k] = psShapeSt->Tilt_smth;
   }
}

static void silk_encode_do_VAD_FLP(silk_encoder_state_FLP *psEnc, int activity) {
   const int activity_threshold = SILK_FIX_CONST(SPEECH_ACTIVITY_DTX_THRES, 8);
   silk_VAD_GetSA_Q8_c(&psEnc->sCmn, psEnc->sCmn.inputBuf+1);
   if (activity == VAD_NO_ACTIVITY && psEnc->sCmn.speech_activity_Q8 >= activity_threshold)
      psEnc->sCmn.speech_activity_Q8 = activity_threshold-1;
   if (psEnc->sCmn.speech_activity_Q8 < activity_threshold) {
      psEnc->sCmn.indices.signalType = TYPE_NO_VOICE_ACTIVITY;
      psEnc->sCmn.noSpeechCounter++;
      if (psEnc->sCmn.noSpeechCounter <= NB_SPEECH_FRAMES_BEFORE_DTX) psEnc->sCmn.inDTX = 0;
      else if (psEnc->sCmn.noSpeechCounter > MAX_CONSECUTIVE_DTX+NB_SPEECH_FRAMES_BEFORE_DTX) {
         psEnc->sCmn.noSpeechCounter = NB_SPEECH_FRAMES_BEFORE_DTX;
         psEnc->sCmn.inDTX = 0;
      }
      psEnc->sCmn.VAD_flags[psEnc->sCmn.nFramesEncoded] = 0;
   } else {
      psEnc->sCmn.noSpeechCounter = 0;
      psEnc->sCmn.inDTX = 0;
      psEnc->sCmn.indices.signalType = TYPE_UNVOICED;
      psEnc->sCmn.VAD_flags[psEnc->sCmn.nFramesEncoded] = 1;
   }
}

static void silk_LBRR_encode_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, const float xfw[],
      int condCoding) {
   int k;
   fl_i32 Gains_Q16[MAX_NB_SUBFR];
   float TempGains[MAX_NB_SUBFR];
   SideInfoIndices *psIndices_LBRR = &psEnc->sCmn.indices_LBRR[psEnc->sCmn.nFramesEncoded];
   silk_nsq_state sNSQ_LBRR;
   if (psEnc->sCmn.LBRR_enabled && psEnc->sCmn.speech_activity_Q8 > SILK_FIX_CONST(LBRR_SPEECH_ACTIVITY_THRES, 8)) {
      psEnc->sCmn.LBRR_flags[psEnc->sCmn.nFramesEncoded] = 1;
      memcpy(&sNSQ_LBRR, &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
      memcpy(psIndices_LBRR, &psEnc->sCmn.indices, sizeof(SideInfoIndices));
      memcpy(TempGains, psEncCtrl->Gains, psEnc->sCmn.nb_subfr*sizeof(float));
      if (psEnc->sCmn.nFramesEncoded == 0 || psEnc->sCmn.LBRR_flags[psEnc->sCmn.nFramesEncoded-1] == 0) {
         psEnc->sCmn.LBRRprevLastGainIndex = psEnc->sShape.LastGainIndex;
         psIndices_LBRR->GainsIndices[0] += psEnc->sCmn.LBRR_GainIncreases;
         psIndices_LBRR->GainsIndices[0] = (fl_i8)silk_min_int(psIndices_LBRR->GainsIndices[0], N_LEVELS_QGAIN-1);
      }
      silk_gains_dequant(Gains_Q16, psIndices_LBRR->GainsIndices, &psEnc->sCmn.LBRRprevLastGainIndex,
            condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(XF(1.0f/65536.0f)*Gains_Q16[k]);
      silk_NSQ_wrapper_FLP(psEnc, psEncCtrl, psIndices_LBRR, &sNSQ_LBRR, psEnc->sCmn.pulses_LBRR[psEnc->sCmn.nFramesEncoded], xfw);
      memcpy(psEncCtrl->Gains, TempGains, psEnc->sCmn.nb_subfr*sizeof(float));
   }
}

static int silk_encode_frame_FLP(silk_encoder_state_FLP *psEnc, fl_i32 *pnBytesOut, ec_ctx *psRangeEnc, int condCoding,
      int maxBits, int useCBR) {
   silk_encoder_control_FLP sEncCtrl;
   int i, iter, maxIter, found_upper, found_lower, ret = 0, bits_margin;
   float *x_frame, *res_pitch_frame, res_pitch[2*MAX_FRAME_LENGTH+LA_PITCH_MAX];
   ec_ctx sRangeEnc_copy, sRangeEnc_copy2;
   silk_nsq_state sNSQ_copy[2];
   fl_i32 seed_copy, nBits, nBits_lower, nBits_upper, gainMult_lower, gainMult_upper, gainsID, gainsID_lower, gainsID_upper;
   fl_i16 gainMult_Q8, ec_prevLagIndex_copy, best_gain_mult[MAX_NB_SUBFR];
   int ec_prevSignalType_copy, gain_lock[MAX_NB_SUBFR] = {0}, best_sum[MAX_NB_SUBFR];
   fl_i8 LastGainIndex_copy2;
   fl_i32 pGains_Q16[MAX_NB_SUBFR];
   unsigned char ec_buf_copy[1275];
   memset(&sEncCtrl, 0, sizeof(sEncCtrl));
   bits_margin = useCBR ? 5 : maxBits/4;
   LastGainIndex_copy2 = 0;
   nBits_lower = nBits_upper = gainMult_lower = gainMult_upper = 0;
   psEnc->sCmn.indices.Seed = psEnc->sCmn.frameCounter++&3;
   x_frame = psEnc->x_buf+psEnc->sCmn.ltp_mem_length;
   res_pitch_frame = res_pitch+psEnc->sCmn.ltp_mem_length;
   silk_LP_variable_cutoff(&psEnc->sCmn.sLP, psEnc->sCmn.inputBuf+1, psEnc->sCmn.frame_length);
   for (i = psEnc->sCmn.frame_length-1; i >= 0; i--)
      x_frame[LA_SHAPE_MS*psEnc->sCmn.fs_kHz+i] = psEnc->sCmn.inputBuf[1+i];
   for (i = 0; i < 8; i++) {
      float *p = &x_frame[LA_SHAPE_MS*psEnc->sCmn.fs_kHz+i*(psEnc->sCmn.frame_length>>3)];
      *p = (float)(XF(1e-6f)*(1-(i&2))+*p);
   }
   if (!psEnc->sCmn.prefillFlag) {
      silk_find_pitch_lags_FLP(psEnc, &sEncCtrl, res_pitch, x_frame);
      silk_noise_shape_analysis_FLP(psEnc, &sEncCtrl, res_pitch_frame, x_frame);
      silk_find_pred_coefs_FLP(psEnc, &sEncCtrl, res_pitch_frame, x_frame, condCoding);
      silk_process_gains_FLP(psEnc, &sEncCtrl, condCoding);
      silk_LBRR_encode_FLP(psEnc, &sEncCtrl, x_frame, condCoding);
      maxIter = 6;
      gainMult_Q8 = SILK_FIX_CONST(1, 8);
      found_lower = 0;
      found_upper = 0;
      gainsID = silk_gains_ID(psEnc->sCmn.indices.GainsIndices, psEnc->sCmn.nb_subfr);
      gainsID_lower = -1;
      gainsID_upper = -1;
      memcpy(&sRangeEnc_copy, psRangeEnc, sizeof(ec_ctx));
      memcpy(&sNSQ_copy[0], &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
      seed_copy = psEnc->sCmn.indices.Seed;
      ec_prevLagIndex_copy = psEnc->sCmn.ec_prevLagIndex;
      ec_prevSignalType_copy = psEnc->sCmn.ec_prevSignalType;
      for (iter = 0;; iter++) {
         if (gainsID == gainsID_lower) nBits = nBits_lower;
         else if (gainsID == gainsID_upper) nBits = nBits_upper;
         else {
            if (iter > 0) {
               memcpy(psRangeEnc, &sRangeEnc_copy, sizeof(ec_ctx));
               memcpy(&psEnc->sCmn.sNSQ, &sNSQ_copy[0], sizeof(silk_nsq_state));
               psEnc->sCmn.indices.Seed = (fl_i8)seed_copy;
               psEnc->sCmn.ec_prevLagIndex = ec_prevLagIndex_copy;
               psEnc->sCmn.ec_prevSignalType = ec_prevSignalType_copy;
            }
            silk_NSQ_wrapper_FLP(psEnc, &sEncCtrl, &psEnc->sCmn.indices, &psEnc->sCmn.sNSQ, psEnc->sCmn.pulses, x_frame);
            if (iter == maxIter && !found_lower) memcpy(&sRangeEnc_copy2, psRangeEnc, sizeof(ec_ctx));
            silk_encode_indices(&psEnc->sCmn, psRangeEnc, psEnc->sCmn.nFramesEncoded, 0, condCoding);
            silk_encode_pulses(psRangeEnc, psEnc->sCmn.indices.signalType, psEnc->sCmn.indices.quantOffsetType,
                  psEnc->sCmn.pulses, psEnc->sCmn.frame_length);
            nBits = ec_tell(psRangeEnc);
            if (iter == maxIter && !found_lower && nBits > maxBits) {
               memcpy(psRangeEnc, &sRangeEnc_copy2, sizeof(ec_ctx));
               psEnc->sShape.LastGainIndex = sEncCtrl.lastGainIndexPrev;
               for (i = 0; i < psEnc->sCmn.nb_subfr; i++) psEnc->sCmn.indices.GainsIndices[i] = 4;
               if (condCoding != CODE_CONDITIONALLY) psEnc->sCmn.indices.GainsIndices[0] = sEncCtrl.lastGainIndexPrev;
               psEnc->sCmn.ec_prevLagIndex = ec_prevLagIndex_copy;
               psEnc->sCmn.ec_prevSignalType = ec_prevSignalType_copy;
               for (i = 0; i < psEnc->sCmn.frame_length; i++) psEnc->sCmn.pulses[i] = 0;
               silk_encode_indices(&psEnc->sCmn, psRangeEnc, psEnc->sCmn.nFramesEncoded, 0, condCoding);
               silk_encode_pulses(psRangeEnc, psEnc->sCmn.indices.signalType, psEnc->sCmn.indices.quantOffsetType,
                     psEnc->sCmn.pulses, psEnc->sCmn.frame_length);
               nBits = ec_tell(psRangeEnc);
            }
            if (useCBR == 0 && iter == 0 && nBits <= maxBits) break;
         }
         if (iter == maxIter) {
            if (found_lower && (gainsID == gainsID_lower || nBits > maxBits)) {
               memcpy(psRangeEnc, &sRangeEnc_copy2, sizeof(ec_ctx));
               memcpy(psRangeEnc->buf, ec_buf_copy, sRangeEnc_copy2.offs);
               memcpy(&psEnc->sCmn.sNSQ, &sNSQ_copy[1], sizeof(silk_nsq_state));
               psEnc->sShape.LastGainIndex = LastGainIndex_copy2;
            }
            break;
         }
         if (nBits > maxBits) {
            if (found_lower == 0 && iter >= 2) {
               float l = sEncCtrl.Lambda*1.5f;
               sEncCtrl.Lambda = l > 1.5f ? l : 1.5f;
               psEnc->sCmn.indices.quantOffsetType = 0;
               found_upper = 0;
               gainsID_upper = -1;
            } else {
               found_upper = 1;
               nBits_upper = nBits;
               gainMult_upper = gainMult_Q8;
               gainsID_upper = gainsID;
            }
         } else if (nBits < maxBits-bits_margin) {
            found_lower = 1;
            nBits_lower = nBits;
            gainMult_lower = gainMult_Q8;
            if (gainsID != gainsID_lower) {
               gainsID_lower = gainsID;
               memcpy(&sRangeEnc_copy2, psRangeEnc, sizeof(ec_ctx));
               memcpy(ec_buf_copy, psRangeEnc->buf, psRangeEnc->offs);
               memcpy(&sNSQ_copy[1], &psEnc->sCmn.sNSQ, sizeof(silk_nsq_state));
               LastGainIndex_copy2 = psEnc->sShape.LastGainIndex;
            }
         } else break;
         if (!found_lower && nBits > maxBits) {
            int j;
            for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
               int sum = 0;
               for (j = i*psEnc->sCmn.subfr_length; j < (i+1)*psEnc->sCmn.subfr_length; j++) sum += abs(psEnc->sCmn.pulses[j]);
               if (iter == 0 || (sum < best_sum[i] && !gain_lock[i])) {
                  best_sum[i] = sum;
                  best_gain_mult[i] = gainMult_Q8;
               } else gain_lock[i] = 1;
            }
         }
         if ((found_lower&found_upper) == 0) {
            if (nBits > maxBits) gainMult_Q8 = (fl_i16)silk_min_32(1024, gainMult_Q8*3/2);
            else gainMult_Q8 = (fl_i16)silk_max_32(64, gainMult_Q8*4/5);
         } else {
            gainMult_Q8 = (fl_i16)(gainMult_lower+((gainMult_upper-gainMult_lower)*(maxBits-nBits_lower))/(nBits_upper-nBits_lower));
            if (gainMult_Q8 > silk_ADD_RSHIFT32(gainMult_lower, gainMult_upper-gainMult_lower, 2))
               gainMult_Q8 = (fl_i16)silk_ADD_RSHIFT32(gainMult_lower, gainMult_upper-gainMult_lower, 2);
            else if (gainMult_Q8 < silk_SUB_RSHIFT32(gainMult_upper, gainMult_upper-gainMult_lower, 2))
               gainMult_Q8 = (fl_i16)silk_SUB_RSHIFT32(gainMult_upper, gainMult_upper-gainMult_lower, 2);
         }
         for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
            fl_i16 tmp = gain_lock[i] ? best_gain_mult[i] : gainMult_Q8;
            pGains_Q16[i] = silk_LSHIFT_SAT32(silk_SMULWB(sEncCtrl.GainsUnq_Q16[i], tmp), 8);
         }
         psEnc->sShape.LastGainIndex = sEncCtrl.lastGainIndexPrev;
         silk_gains_quant(psEnc->sCmn.indices.GainsIndices, pGains_Q16, &psEnc->sShape.LastGainIndex,
               condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
         gainsID = silk_gains_ID(psEnc->sCmn.indices.GainsIndices, psEnc->sCmn.nb_subfr);
         for (i = 0; i < psEnc->sCmn.nb_subfr; i++) sEncCtrl.Gains[i] = (float)(XF(1.0f/65536.0f)*pGains_Q16[i]);
      }
   }
   memmove(psEnc->x_buf, &psEnc->x_buf[psEnc->sCmn.frame_length],
         (psEnc->sCmn.ltp_mem_length+LA_SHAPE_MS*psEnc->sCmn.fs_kHz)*sizeof(float));
   if (psEnc->sCmn.prefillFlag) {
      *pnBytesOut = 0;
      return ret;
   }
   psEnc->sCmn.prevLag = sEncCtrl.pitchL[psEnc->sCmn.nb_subfr-1];
   psEnc->sCmn.prevSignalType = psEnc->sCmn.indices.signalType;
   psEnc->sCmn.first_frame_after_reset = 0;
   *pnBytesOut = silk_RSHIFT(ec_tell(psRangeEnc)+7, 3);
   return ret;
}
