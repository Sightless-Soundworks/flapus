/* ---- SILK decoder ---- */
#define PLC_BWE_COEF 0.99
#define V_PITCH_GAIN_START_MIN_Q14 11469
#define V_PITCH_GAIN_START_MAX_Q14 15565
#define PLC_MAX_PITCH_LAG_MS 18
#define RAND_BUF_SIZE 128
#define RAND_BUF_MASK (RAND_BUF_SIZE-1)
#define LOG2_INV_LPC_GAIN_HIGH_THRES 3
#define LOG2_INV_LPC_GAIN_LOW_THRES 8
#define PITCH_DRIFT_FAC_Q16 655

typedef struct {
   fl_i8 GainsIndices[MAX_NB_SUBFR];
   fl_i8 LTPIndex[MAX_NB_SUBFR];
   fl_i8 NLSFIndices[MAX_LPC_ORDER+1];
   fl_i16 lagIndex;
   fl_i8 contourIndex, signalType, quantOffsetType, NLSFInterpCoef_Q2, PERIndex, LTP_scaleIndex, Seed;
} silk_side_info;

typedef struct {
   fl_i32 pitchL_Q8;
   fl_i16 LTPCoef_Q14[LTP_ORDER];
   fl_i16 prevLPC_Q12[MAX_LPC_ORDER];
   int last_frame_lost;
   fl_i32 rand_seed;
   fl_i16 randScale_Q14;
   fl_i32 conc_energy;
   int conc_energy_shift;
   fl_i16 prevLTP_scale_Q14;
   fl_i32 prevGain_Q16[2];
   int fs_kHz, nb_subfr, subfr_length;
} silk_plc_state;

typedef struct {
   fl_i32 CNG_exc_buf_Q14[MAX_FRAME_LENGTH];
   fl_i16 CNG_smth_NLSF_Q15[MAX_LPC_ORDER];
   fl_i32 CNG_synth_state[MAX_LPC_ORDER];
   fl_i32 CNG_smth_Gain_Q16, rand_seed;
   int fs_kHz;
} silk_cng_state;

typedef struct {
   /* fields from prev_gain_Q16 on are cleared on reset */
   fl_i32 prev_gain_Q16;
   fl_i32 exc_Q14[MAX_FRAME_LENGTH];
   fl_i32 sLPC_Q14_buf[MAX_LPC_ORDER];
   fl_i16 outBuf[MAX_FRAME_LENGTH+2*MAX_SUB_FRAME_LENGTH];
   int lagPrev;
   fl_i8 LastGainIndex;
   int fs_kHz;
   fl_i32 fs_API_hz;
   int nb_subfr, frame_length, subfr_length, ltp_mem_length, LPC_order;
   fl_i16 prevNLSF_Q15[MAX_LPC_ORDER];
   int first_frame_after_reset;
   const fl_u8 *pitch_lag_low_bits_iCDF, *pitch_contour_iCDF;
   int nFramesDecoded, nFramesPerPacket, ec_prevSignalType;
   fl_i16 ec_prevLagIndex;
   int VAD_flags[MAX_FRAMES_PER_PACKET], LBRR_flag, LBRR_flags[MAX_FRAMES_PER_PACKET];
   silk_resampler_state resampler_state;
   const fl_nlsf_cb *psNLSF_CB;
   silk_side_info indices;
   silk_cng_state sCNG;
   int lossCnt, prevSignalType;
   silk_plc_state sPLC;
} silk_dec_state;

typedef struct {
   int pitchL[MAX_NB_SUBFR];
   fl_i32 Gains_Q16[MAX_NB_SUBFR];
   fl_i16 PredCoef_Q12[2][MAX_LPC_ORDER];
   fl_i16 LTPCoef_Q14[LTP_ORDER*MAX_NB_SUBFR];
   int LTP_scale_Q14;
} silk_dec_ctrl;

typedef struct {
   fl_i16 pred_prev_Q13[2], sMid[2], sSide[2];
} silk_stereo_dec;

typedef struct {
   silk_dec_state channel_state[2];
   silk_stereo_dec sStereo;
   int nChannelsAPI, nChannelsInternal, prev_decode_only_middle;
} silk_decoder;

typedef struct {
   int nChannelsAPI, nChannelsInternal;
   fl_i32 API_sampleRate, internalSampleRate;
   int payloadSize_ms, prevPitchLag;
} silk_dec_control;

static void silk_CNG_Reset(silk_dec_state *psDec) {
   int i, NLSF_step_Q15, NLSF_acc_Q15;
   NLSF_step_Q15 = silk_DIV32_16(silk_int16_MAX, psDec->LPC_order+1);
   NLSF_acc_Q15 = 0;
   for (i = 0; i < psDec->LPC_order; i++) {
      NLSF_acc_Q15 += NLSF_step_Q15;
      psDec->sCNG.CNG_smth_NLSF_Q15[i] = NLSF_acc_Q15;
   }
   psDec->sCNG.CNG_smth_Gain_Q16 = 0;
   psDec->sCNG.rand_seed = 3176576;
}
static void silk_PLC_Reset(silk_dec_state *psDec) {
   psDec->sPLC.pitchL_Q8 = silk_LSHIFT(psDec->frame_length, 8-1);
   psDec->sPLC.prevGain_Q16[0] = SILK_FIX_CONST(1, 16);
   psDec->sPLC.prevGain_Q16[1] = SILK_FIX_CONST(1, 16);
   psDec->sPLC.subfr_length = 20;
   psDec->sPLC.nb_subfr = 2;
}
static void silk_reset_decoder(silk_dec_state *psDec) {
   memset(&psDec->prev_gain_Q16, 0, sizeof(*psDec)-((char*)&psDec->prev_gain_Q16-(char*)psDec));
   psDec->first_frame_after_reset = 1;
   psDec->prev_gain_Q16 = 65536;
   silk_CNG_Reset(psDec);
   silk_PLC_Reset(psDec);
}
static void silk_init_decoder(silk_dec_state *psDec) {
   memset(psDec, 0, sizeof(*psDec));
   silk_reset_decoder(psDec);
}

static int silk_decoder_set_fs(silk_dec_state *psDec, int fs_kHz, fl_i32 fs_API_Hz) {
   int frame_length, ret = 0;
   psDec->subfr_length = silk_SMULBB(SUB_FRAME_LENGTH_MS, fs_kHz);
   frame_length = silk_SMULBB(psDec->nb_subfr, psDec->subfr_length);
   if (psDec->fs_kHz != fs_kHz || psDec->fs_API_hz != fs_API_Hz) {
      ret += silk_resampler_init(&psDec->resampler_state, silk_SMULBB(fs_kHz, 1000), fs_API_Hz, 0);
      psDec->fs_API_hz = fs_API_Hz;
   }
   if (psDec->fs_kHz != fs_kHz || frame_length != psDec->frame_length) {
      if (fs_kHz == 8) {
         psDec->pitch_contour_iCDF = psDec->nb_subfr == MAX_NB_SUBFR ? fl_pitch_contour_NB_iCDF : fl_pitch_contour_10_ms_NB_iCDF;
      } else {
         psDec->pitch_contour_iCDF = psDec->nb_subfr == MAX_NB_SUBFR ? fl_pitch_contour_iCDF : fl_pitch_contour_10_ms_iCDF;
      }
      if (psDec->fs_kHz != fs_kHz) {
         psDec->ltp_mem_length = silk_SMULBB(LTP_MEM_LENGTH_MS, fs_kHz);
         if (fs_kHz == 8 || fs_kHz == 12) {
            psDec->LPC_order = MIN_LPC_ORDER;
            psDec->psNLSF_CB = &fl_NLSF_CB_NB_MB;
         } else {
            psDec->LPC_order = MAX_LPC_ORDER;
            psDec->psNLSF_CB = &fl_NLSF_CB_WB;
         }
         if (fs_kHz == 16) psDec->pitch_lag_low_bits_iCDF = fl_uniform8_iCDF;
         else if (fs_kHz == 12) psDec->pitch_lag_low_bits_iCDF = fl_uniform6_iCDF;
         else psDec->pitch_lag_low_bits_iCDF = fl_uniform4_iCDF;
         psDec->first_frame_after_reset = 1;
         psDec->lagPrev = 100;
         psDec->LastGainIndex = 10;
         psDec->prevSignalType = TYPE_NO_VOICE_ACTIVITY;
         memset(psDec->outBuf, 0, sizeof(psDec->outBuf));
         memset(psDec->sLPC_Q14_buf, 0, sizeof(psDec->sLPC_Q14_buf));
      }
      psDec->fs_kHz = fs_kHz;
      psDec->frame_length = frame_length;
   }
   return ret;
}

static void silk_decode_indices(silk_dec_state *psDec, ec_ctx *dec, int FrameIndex, int decode_LBRR, int condCoding) {
   int i, k, Ix, decode_absolute_lagIndex, delta_lagIndex;
   fl_i16 ec_ix[MAX_LPC_ORDER];
   fl_u8 pred_Q8[MAX_LPC_ORDER];
   silk_side_info *ind = &psDec->indices;
   if (decode_LBRR || psDec->VAD_flags[FrameIndex]) Ix = ec_dec_icdf(dec, fl_type_offset_VAD_iCDF, 8)+2;
   else Ix = ec_dec_icdf(dec, fl_type_offset_no_VAD_iCDF, 8);
   ind->signalType = (fl_i8)silk_RSHIFT(Ix, 1);
   ind->quantOffsetType = (fl_i8)(Ix&1);
   if (condCoding == CODE_CONDITIONALLY) {
      ind->GainsIndices[0] = (fl_i8)ec_dec_icdf(dec, fl_delta_gain_iCDF, 8);
   } else {
      ind->GainsIndices[0] = (fl_i8)silk_LSHIFT(ec_dec_icdf(dec, fl_gain_iCDF[ind->signalType], 8), 3);
      ind->GainsIndices[0] += (fl_i8)ec_dec_icdf(dec, fl_uniform8_iCDF, 8);
   }
   for (i = 1; i < psDec->nb_subfr; i++) ind->GainsIndices[i] = (fl_i8)ec_dec_icdf(dec, fl_delta_gain_iCDF, 8);
   ind->NLSFIndices[0] = (fl_i8)ec_dec_icdf(dec, &psDec->psNLSF_CB->CB1_iCDF[(ind->signalType>>1)*psDec->psNLSF_CB->nVectors], 8);
   silk_NLSF_unpack(ec_ix, pred_Q8, psDec->psNLSF_CB, ind->NLSFIndices[0]);
   for (i = 0; i < psDec->psNLSF_CB->order; i++) {
      Ix = ec_dec_icdf(dec, &psDec->psNLSF_CB->ec_iCDF[ec_ix[i]], 8);
      if (Ix == 0) Ix -= ec_dec_icdf(dec, fl_NLSF_EXT_iCDF, 8);
      else if (Ix == 2*NLSF_QUANT_MAX_AMPLITUDE) Ix += ec_dec_icdf(dec, fl_NLSF_EXT_iCDF, 8);
      ind->NLSFIndices[i+1] = (fl_i8)(Ix-NLSF_QUANT_MAX_AMPLITUDE);
   }
   if (psDec->nb_subfr == MAX_NB_SUBFR) ind->NLSFInterpCoef_Q2 = (fl_i8)ec_dec_icdf(dec, fl_NLSF_interpolation_factor_iCDF, 8);
   else ind->NLSFInterpCoef_Q2 = 4;
   if (ind->signalType == TYPE_VOICED) {
      decode_absolute_lagIndex = 1;
      if (condCoding == CODE_CONDITIONALLY && psDec->ec_prevSignalType == TYPE_VOICED) {
         delta_lagIndex = (fl_i16)ec_dec_icdf(dec, fl_pitch_delta_iCDF, 8);
         if (delta_lagIndex > 0) {
            delta_lagIndex = delta_lagIndex-9;
            ind->lagIndex = (fl_i16)(psDec->ec_prevLagIndex+delta_lagIndex);
            decode_absolute_lagIndex = 0;
         }
      }
      if (decode_absolute_lagIndex) {
         ind->lagIndex = (fl_i16)ec_dec_icdf(dec, fl_pitch_lag_iCDF, 8)*silk_RSHIFT(psDec->fs_kHz, 1);
         ind->lagIndex += (fl_i16)ec_dec_icdf(dec, psDec->pitch_lag_low_bits_iCDF, 8);
      }
      psDec->ec_prevLagIndex = ind->lagIndex;
      ind->contourIndex = (fl_i8)ec_dec_icdf(dec, psDec->pitch_contour_iCDF, 8);
      ind->PERIndex = (fl_i8)ec_dec_icdf(dec, fl_LTP_per_index_iCDF, 8);
      for (k = 0; k < psDec->nb_subfr; k++)
         ind->LTPIndex[k] = (fl_i8)ec_dec_icdf(dec, fl_LTP_gain_iCDF_ptrs[ind->PERIndex], 8);
      if (condCoding == CODE_INDEPENDENTLY) ind->LTP_scaleIndex = (fl_i8)ec_dec_icdf(dec, fl_LTPscale_iCDF, 8);
      else ind->LTP_scaleIndex = 0;
   }
   psDec->ec_prevSignalType = ind->signalType;
   ind->Seed = (fl_i8)ec_dec_icdf(dec, fl_uniform4_iCDF, 8);
}

static void silk_decode_pulses(ec_ctx *dec, fl_i16 pulses[], const int signalType, const int quantOffsetType, const int frame_length) {
   int i, j, k, iter, abs_q, nLS, RateLevelIndex;
   int sum_pulses[MAX_NB_SHELL_BLOCKS], nLshifts[MAX_NB_SHELL_BLOCKS];
   fl_i16 *pulses_ptr;
   const fl_u8 *cdf_ptr;
   RateLevelIndex = ec_dec_icdf(dec, fl_rate_levels_iCDF[signalType>>1], 8);
   iter = silk_RSHIFT(frame_length, LOG2_SHELL_CODEC_FRAME_LENGTH);
   if (iter*SHELL_CODEC_FRAME_LENGTH < frame_length) iter++;
   cdf_ptr = fl_pulses_per_block_iCDF[RateLevelIndex];
   for (i = 0; i < iter; i++) {
      nLshifts[i] = 0;
      sum_pulses[i] = ec_dec_icdf(dec, cdf_ptr, 8);
      while (sum_pulses[i] == SILK_MAX_PULSES+1) {
         nLshifts[i]++;
         sum_pulses[i] = ec_dec_icdf(dec, fl_pulses_per_block_iCDF[N_RATE_LEVELS-1]+(nLshifts[i] == 10), 8);
      }
   }
   for (i = 0; i < iter; i++) {
      if (sum_pulses[i] > 0) silk_shell_decoder(&pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)], dec, sum_pulses[i]);
      else memset(&pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)], 0, SHELL_CODEC_FRAME_LENGTH*sizeof(pulses[0]));
   }
   for (i = 0; i < iter; i++) {
      if (nLshifts[i] > 0) {
         nLS = nLshifts[i];
         pulses_ptr = &pulses[silk_SMULBB(i, SHELL_CODEC_FRAME_LENGTH)];
         for (k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++) {
            abs_q = pulses_ptr[k];
            for (j = 0; j < nLS; j++) {
               abs_q = silk_LSHIFT(abs_q, 1);
               abs_q += ec_dec_icdf(dec, fl_lsb_iCDF, 8);
            }
            pulses_ptr[k] = abs_q;
         }
         sum_pulses[i] |= nLS<<5;
      }
   }
   silk_decode_signs(dec, pulses, frame_length, signalType, quantOffsetType, sum_pulses);
}

static void silk_decode_parameters(silk_dec_state *psDec, silk_dec_ctrl *ctrl, int condCoding) {
   int i, k, Ix;
   fl_i16 pNLSF_Q15[MAX_LPC_ORDER], pNLSF0_Q15[MAX_LPC_ORDER];
   const fl_i8 *cbk_ptr_Q7;
   silk_gains_dequant(ctrl->Gains_Q16, psDec->indices.GainsIndices, &psDec->LastGainIndex,
         condCoding == CODE_CONDITIONALLY, psDec->nb_subfr);
   silk_NLSF_decode(pNLSF_Q15, psDec->indices.NLSFIndices, psDec->psNLSF_CB);
   silk_NLSF2A(ctrl->PredCoef_Q12[1], pNLSF_Q15, psDec->LPC_order);
   if (psDec->first_frame_after_reset == 1) psDec->indices.NLSFInterpCoef_Q2 = 4;
   if (psDec->indices.NLSFInterpCoef_Q2 < 4) {
      for (i = 0; i < psDec->LPC_order; i++)
         pNLSF0_Q15[i] = psDec->prevNLSF_Q15[i]+silk_RSHIFT(silk_MUL(psDec->indices.NLSFInterpCoef_Q2,
               pNLSF_Q15[i]-psDec->prevNLSF_Q15[i]), 2);
      silk_NLSF2A(ctrl->PredCoef_Q12[0], pNLSF0_Q15, psDec->LPC_order);
   } else {
      memcpy(ctrl->PredCoef_Q12[0], ctrl->PredCoef_Q12[1], psDec->LPC_order*sizeof(fl_i16));
   }
   memcpy(psDec->prevNLSF_Q15, pNLSF_Q15, psDec->LPC_order*sizeof(fl_i16));
   if (psDec->lossCnt) {
      silk_bwexpander(ctrl->PredCoef_Q12[0], psDec->LPC_order, BWE_AFTER_LOSS_Q16);
      silk_bwexpander(ctrl->PredCoef_Q12[1], psDec->LPC_order, BWE_AFTER_LOSS_Q16);
   }
   if (psDec->indices.signalType == TYPE_VOICED) {
      silk_decode_pitch(psDec->indices.lagIndex, psDec->indices.contourIndex, ctrl->pitchL, psDec->fs_kHz, psDec->nb_subfr);
      cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[psDec->indices.PERIndex];
      for (k = 0; k < psDec->nb_subfr; k++) {
         Ix = psDec->indices.LTPIndex[k];
         for (i = 0; i < LTP_ORDER; i++)
            ctrl->LTPCoef_Q14[k*LTP_ORDER+i] = silk_LSHIFT(cbk_ptr_Q7[Ix*LTP_ORDER+i], 7);
      }
      Ix = psDec->indices.LTP_scaleIndex;
      ctrl->LTP_scale_Q14 = fl_LTPScales_table_Q14[Ix];
   } else {
      memset(ctrl->pitchL, 0, psDec->nb_subfr*sizeof(int));
      memset(ctrl->LTPCoef_Q14, 0, LTP_ORDER*psDec->nb_subfr*sizeof(fl_i16));
      psDec->indices.PERIndex = 0;
      ctrl->LTP_scale_Q14 = 0;
   }
}

static void silk_decode_core(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 xq[], const fl_i16 pulses[MAX_FRAME_LENGTH]) {
   int i, j, k, lag = 0, start_idx, sLTP_buf_idx, NLSF_interpolation_flag, signalType;
   fl_i16 *A_Q12, *B_Q14, *pxq, A_Q12_tmp[MAX_LPC_ORDER];
   fl_i16 sLTP[MAX_FRAME_LENGTH*2];
   fl_i32 sLTP_Q15[MAX_FRAME_LENGTH*3], res_Q14[MAX_SUB_FRAME_LENGTH], sLPC_Q14[MAX_SUB_FRAME_LENGTH+MAX_LPC_ORDER];
   fl_i32 LTP_pred_Q13, LPC_pred_Q10, Gain_Q10, inv_gain_Q31, gain_adj_Q16, rand_seed, offset_Q10;
   fl_i32 *pred_lag_ptr, *pexc_Q14, *pres_Q14;
   offset_Q10 = fl_Quantization_Offsets_Q10[psDec->indices.signalType>>1][psDec->indices.quantOffsetType];
   NLSF_interpolation_flag = psDec->indices.NLSFInterpCoef_Q2 < 1<<2;
   rand_seed = psDec->indices.Seed;
   for (i = 0; i < psDec->frame_length; i++) {
      rand_seed = silk_RAND(rand_seed);
      psDec->exc_Q14[i] = silk_LSHIFT((fl_i32)pulses[i], 14);
      if (psDec->exc_Q14[i] > 0) psDec->exc_Q14[i] -= QUANT_LEVEL_ADJUST_Q10<<4;
      else if (psDec->exc_Q14[i] < 0) psDec->exc_Q14[i] += QUANT_LEVEL_ADJUST_Q10<<4;
      psDec->exc_Q14[i] += offset_Q10<<4;
      if (rand_seed < 0) psDec->exc_Q14[i] = -psDec->exc_Q14[i];
      rand_seed = silk_ADD32_ovflw(rand_seed, pulses[i]);
   }
   memcpy(sLPC_Q14, psDec->sLPC_Q14_buf, MAX_LPC_ORDER*sizeof(fl_i32));
   pexc_Q14 = psDec->exc_Q14;
   pxq = xq;
   sLTP_buf_idx = psDec->ltp_mem_length;
   for (k = 0; k < psDec->nb_subfr; k++) {
      pres_Q14 = res_Q14;
      A_Q12 = ctrl->PredCoef_Q12[k>>1];
      memcpy(A_Q12_tmp, A_Q12, psDec->LPC_order*sizeof(fl_i16));
      B_Q14 = &ctrl->LTPCoef_Q14[k*LTP_ORDER];
      signalType = psDec->indices.signalType;
      Gain_Q10 = silk_RSHIFT(ctrl->Gains_Q16[k], 6);
      inv_gain_Q31 = silk_INVERSE32_varQ(ctrl->Gains_Q16[k], 47);
      if (ctrl->Gains_Q16[k] != psDec->prev_gain_Q16) {
         gain_adj_Q16 = silk_DIV32_varQ(psDec->prev_gain_Q16, ctrl->Gains_Q16[k], 16);
         for (i = 0; i < MAX_LPC_ORDER; i++) sLPC_Q14[i] = silk_SMULWW(gain_adj_Q16, sLPC_Q14[i]);
      } else gain_adj_Q16 = (fl_i32)1<<16;
      psDec->prev_gain_Q16 = ctrl->Gains_Q16[k];
      if (psDec->lossCnt && psDec->prevSignalType == TYPE_VOICED && psDec->indices.signalType != TYPE_VOICED && k < MAX_NB_SUBFR/2) {
         memset(B_Q14, 0, LTP_ORDER*sizeof(fl_i16));
         B_Q14[LTP_ORDER/2] = SILK_FIX_CONST(0.25, 14);
         signalType = TYPE_VOICED;
         ctrl->pitchL[k] = psDec->lagPrev;
      }
      if (signalType == TYPE_VOICED) {
         lag = ctrl->pitchL[k];
         if (k == 0 || (k == 2 && NLSF_interpolation_flag)) {
            start_idx = psDec->ltp_mem_length-lag-psDec->LPC_order-LTP_ORDER/2;
            if (k == 2) memcpy(&psDec->outBuf[psDec->ltp_mem_length], xq, 2*psDec->subfr_length*sizeof(fl_i16));
            silk_LPC_analysis_filter(&sLTP[start_idx], &psDec->outBuf[start_idx+k*psDec->subfr_length], A_Q12,
                  psDec->ltp_mem_length-start_idx, psDec->LPC_order);
            if (k == 0) inv_gain_Q31 = silk_LSHIFT(silk_SMULWB(inv_gain_Q31, ctrl->LTP_scale_Q14), 2);
            for (i = 0; i < lag+LTP_ORDER/2; i++)
               sLTP_Q15[sLTP_buf_idx-i-1] = silk_SMULWB(inv_gain_Q31, sLTP[psDec->ltp_mem_length-i-1]);
         } else if (gain_adj_Q16 != (fl_i32)1<<16) {
            for (i = 0; i < lag+LTP_ORDER/2; i++)
               sLTP_Q15[sLTP_buf_idx-i-1] = silk_SMULWW(gain_adj_Q16, sLTP_Q15[sLTP_buf_idx-i-1]);
         }
      }
      if (signalType == TYPE_VOICED) {
         pred_lag_ptr = &sLTP_Q15[sLTP_buf_idx-lag+LTP_ORDER/2];
         for (i = 0; i < psDec->subfr_length; i++) {
            LTP_pred_Q13 = 2;
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[0], B_Q14[0]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-1], B_Q14[1]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-2], B_Q14[2]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-3], B_Q14[3]);
            LTP_pred_Q13 = silk_SMLAWB(LTP_pred_Q13, pred_lag_ptr[-4], B_Q14[4]);
            pred_lag_ptr++;
            pres_Q14[i] = silk_ADD_LSHIFT32(pexc_Q14[i], LTP_pred_Q13, 1);
            sLTP_Q15[sLTP_buf_idx] = silk_LSHIFT(pres_Q14[i], 1);
            sLTP_buf_idx++;
         }
      } else pres_Q14 = pexc_Q14;
      for (i = 0; i < psDec->subfr_length; i++) {
         LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
         for (j = 0; j < psDec->LPC_order; j++)
            LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, sLPC_Q14[MAX_LPC_ORDER+i-j-1], A_Q12_tmp[j]);
         sLPC_Q14[MAX_LPC_ORDER+i] = silk_ADD_SAT32(pres_Q14[i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
         pxq[i] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(sLPC_Q14[MAX_LPC_ORDER+i], Gain_Q10), 8));
      }
      memcpy(sLPC_Q14, &sLPC_Q14[psDec->subfr_length], MAX_LPC_ORDER*sizeof(fl_i32));
      pexc_Q14 += psDec->subfr_length;
      pxq += psDec->subfr_length;
   }
   memcpy(psDec->sLPC_Q14_buf, sLPC_Q14, MAX_LPC_ORDER*sizeof(fl_i32));
}

/* ---- SILK packet loss concealment ---- */
static const fl_i16 HARM_ATT_Q15[2] = {32440, 31130};
static const fl_i16 PLC_RAND_ATTENUATE_V_Q15[2] = {31130, 26214};
static const fl_i16 PLC_RAND_ATTENUATE_UV_Q15[2] = {32440, 29491};

static void silk_PLC_update(silk_dec_state *psDec, silk_dec_ctrl *ctrl) {
   fl_i32 LTP_Gain_Q14, temp_LTP_Gain_Q14;
   int i, j;
   silk_plc_state *psPLC = &psDec->sPLC;
   psDec->prevSignalType = psDec->indices.signalType;
   LTP_Gain_Q14 = 0;
   if (psDec->indices.signalType == TYPE_VOICED) {
      for (j = 0; j*psDec->subfr_length < ctrl->pitchL[psDec->nb_subfr-1]; j++) {
         if (j == psDec->nb_subfr) break;
         temp_LTP_Gain_Q14 = 0;
         for (i = 0; i < LTP_ORDER; i++) temp_LTP_Gain_Q14 += ctrl->LTPCoef_Q14[(psDec->nb_subfr-1-j)*LTP_ORDER+i];
         if (temp_LTP_Gain_Q14 > LTP_Gain_Q14) {
            LTP_Gain_Q14 = temp_LTP_Gain_Q14;
            memcpy(psPLC->LTPCoef_Q14, &ctrl->LTPCoef_Q14[silk_SMULBB(psDec->nb_subfr-1-j, LTP_ORDER)], LTP_ORDER*sizeof(fl_i16));
            psPLC->pitchL_Q8 = silk_LSHIFT(ctrl->pitchL[psDec->nb_subfr-1-j], 8);
         }
      }
      memset(psPLC->LTPCoef_Q14, 0, LTP_ORDER*sizeof(fl_i16));
      psPLC->LTPCoef_Q14[LTP_ORDER/2] = LTP_Gain_Q14;
      if (LTP_Gain_Q14 < V_PITCH_GAIN_START_MIN_Q14) {
         int scale_Q10;
         fl_i32 tmp = silk_LSHIFT(V_PITCH_GAIN_START_MIN_Q14, 10);
         scale_Q10 = silk_DIV32(tmp, silk_max(LTP_Gain_Q14, 1));
         for (i = 0; i < LTP_ORDER; i++)
            psPLC->LTPCoef_Q14[i] = silk_RSHIFT(silk_SMULBB(psPLC->LTPCoef_Q14[i], scale_Q10), 10);
      } else if (LTP_Gain_Q14 > V_PITCH_GAIN_START_MAX_Q14) {
         int scale_Q14;
         fl_i32 tmp = silk_LSHIFT(V_PITCH_GAIN_START_MAX_Q14, 14);
         scale_Q14 = silk_DIV32(tmp, silk_max(LTP_Gain_Q14, 1));
         for (i = 0; i < LTP_ORDER; i++)
            psPLC->LTPCoef_Q14[i] = silk_RSHIFT(silk_SMULBB(psPLC->LTPCoef_Q14[i], scale_Q14), 14);
      }
   } else {
      psPLC->pitchL_Q8 = silk_LSHIFT(silk_SMULBB(psDec->fs_kHz, 18), 8);
      memset(psPLC->LTPCoef_Q14, 0, LTP_ORDER*sizeof(fl_i16));
   }
   memcpy(psPLC->prevLPC_Q12, ctrl->PredCoef_Q12[1], psDec->LPC_order*sizeof(fl_i16));
   psPLC->prevLTP_scale_Q14 = ctrl->LTP_scale_Q14;
   memcpy(psPLC->prevGain_Q16, &ctrl->Gains_Q16[psDec->nb_subfr-2], 2*sizeof(fl_i32));
   psPLC->subfr_length = psDec->subfr_length;
   psPLC->nb_subfr = psDec->nb_subfr;
}

static void silk_PLC_energy(fl_i32 *energy1, int *shift1, fl_i32 *energy2, int *shift2, const fl_i32 *exc_Q14,
      const fl_i32 *prevGain_Q10, int subfr_length, int nb_subfr) {
   int i, k;
   fl_i16 exc_buf[2*MAX_SUB_FRAME_LENGTH], *exc_buf_ptr = exc_buf;
   for (k = 0; k < 2; k++) {
      for (i = 0; i < subfr_length; i++)
         exc_buf_ptr[i] = (fl_i16)silk_SAT16(silk_RSHIFT(silk_SMULWW(exc_Q14[i+(k+nb_subfr-2)*subfr_length], prevGain_Q10[k]), 8));
      exc_buf_ptr += subfr_length;
   }
   silk_sum_sqr_shift(energy1, shift1, exc_buf, subfr_length);
   silk_sum_sqr_shift(energy2, shift2, &exc_buf[subfr_length], subfr_length);
}

static void silk_PLC_conceal(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[]) {
   int i, j, k, lag, idx, sLTP_buf_idx, shift1, shift2;
   fl_i32 rand_seed, harm_Gain_Q15, rand_Gain_Q15, inv_gain_Q30, energy1, energy2, *rand_ptr, *pred_lag_ptr;
   fl_i32 LPC_pred_Q10, LTP_pred_Q12, *sLPC_Q14_ptr, prevGain_Q10[2];
   fl_i16 rand_scale_Q14, *B_Q14, A_Q12[MAX_LPC_ORDER], sLTP[MAX_FRAME_LENGTH*2];
   fl_i32 sLTP_Q14[MAX_FRAME_LENGTH*3];
   silk_plc_state *psPLC = &psDec->sPLC;
   prevGain_Q10[0] = silk_RSHIFT(psPLC->prevGain_Q16[0], 6);
   prevGain_Q10[1] = silk_RSHIFT(psPLC->prevGain_Q16[1], 6);
   if (psDec->first_frame_after_reset) memset(psPLC->prevLPC_Q12, 0, sizeof(psPLC->prevLPC_Q12));
   silk_PLC_energy(&energy1, &shift1, &energy2, &shift2, psDec->exc_Q14, prevGain_Q10, psDec->subfr_length, psDec->nb_subfr);
   if (silk_RSHIFT(energy1, shift2) < silk_RSHIFT(energy2, shift1))
      rand_ptr = &psDec->exc_Q14[silk_max_int(0, (psPLC->nb_subfr-1)*psPLC->subfr_length-RAND_BUF_SIZE)];
   else
      rand_ptr = &psDec->exc_Q14[silk_max_int(0, psPLC->nb_subfr*psPLC->subfr_length-RAND_BUF_SIZE)];
   B_Q14 = psPLC->LTPCoef_Q14;
   rand_scale_Q14 = psPLC->randScale_Q14;
   harm_Gain_Q15 = HARM_ATT_Q15[silk_min_int(1, psDec->lossCnt)];
   if (psDec->prevSignalType == TYPE_VOICED) rand_Gain_Q15 = PLC_RAND_ATTENUATE_V_Q15[silk_min_int(1, psDec->lossCnt)];
   else rand_Gain_Q15 = PLC_RAND_ATTENUATE_UV_Q15[silk_min_int(1, psDec->lossCnt)];
   silk_bwexpander(psPLC->prevLPC_Q12, psDec->LPC_order, SILK_FIX_CONST(PLC_BWE_COEF, 16));
   memcpy(A_Q12, psPLC->prevLPC_Q12, psDec->LPC_order*sizeof(fl_i16));
   if (psDec->lossCnt == 0) {
      rand_scale_Q14 = 1<<14;
      if (psDec->prevSignalType == TYPE_VOICED) {
         for (i = 0; i < LTP_ORDER; i++) rand_scale_Q14 -= B_Q14[i];
         rand_scale_Q14 = silk_max_16(3277, rand_scale_Q14);
         rand_scale_Q14 = (fl_i16)silk_RSHIFT(silk_SMULBB(rand_scale_Q14, psPLC->prevLTP_scale_Q14), 14);
      } else {
         fl_i32 invGain_Q30, down_scale_Q30;
         invGain_Q30 = silk_LPC_inverse_pred_gain(psPLC->prevLPC_Q12, psDec->LPC_order);
         down_scale_Q30 = silk_min_32(silk_RSHIFT((fl_i32)1<<30, LOG2_INV_LPC_GAIN_HIGH_THRES), invGain_Q30);
         down_scale_Q30 = silk_max_32(silk_RSHIFT((fl_i32)1<<30, LOG2_INV_LPC_GAIN_LOW_THRES), down_scale_Q30);
         down_scale_Q30 = silk_LSHIFT(down_scale_Q30, LOG2_INV_LPC_GAIN_HIGH_THRES);
         rand_Gain_Q15 = silk_RSHIFT(silk_SMULWB(down_scale_Q30, rand_Gain_Q15), 14);
      }
   }
   rand_seed = psPLC->rand_seed;
   lag = silk_RSHIFT_ROUND(psPLC->pitchL_Q8, 8);
   sLTP_buf_idx = psDec->ltp_mem_length;
   idx = psDec->ltp_mem_length-lag-psDec->LPC_order-LTP_ORDER/2;
   silk_LPC_analysis_filter(&sLTP[idx], &psDec->outBuf[idx], A_Q12, psDec->ltp_mem_length-idx, psDec->LPC_order);
   inv_gain_Q30 = silk_INVERSE32_varQ(psPLC->prevGain_Q16[1], 46);
   inv_gain_Q30 = silk_min(inv_gain_Q30, silk_int32_MAX>>1);
   for (i = idx+psDec->LPC_order; i < psDec->ltp_mem_length; i++) sLTP_Q14[i] = silk_SMULWB(inv_gain_Q30, sLTP[i]);
   for (k = 0; k < psDec->nb_subfr; k++) {
      pred_lag_ptr = &sLTP_Q14[sLTP_buf_idx-lag+LTP_ORDER/2];
      for (i = 0; i < psDec->subfr_length; i++) {
         LTP_pred_Q12 = 2;
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[0], B_Q14[0]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-1], B_Q14[1]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-2], B_Q14[2]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-3], B_Q14[3]);
         LTP_pred_Q12 = silk_SMLAWB(LTP_pred_Q12, pred_lag_ptr[-4], B_Q14[4]);
         pred_lag_ptr++;
         rand_seed = silk_RAND(rand_seed);
         idx = silk_RSHIFT(rand_seed, 25)&RAND_BUF_MASK;
         sLTP_Q14[sLTP_buf_idx] = silk_LSHIFT32(silk_SMLAWB(LTP_pred_Q12, rand_ptr[idx], rand_scale_Q14), 2);
         sLTP_buf_idx++;
      }
      for (j = 0; j < LTP_ORDER; j++) B_Q14[j] = silk_RSHIFT(silk_SMULBB(harm_Gain_Q15, B_Q14[j]), 15);
      rand_scale_Q14 = silk_RSHIFT(silk_SMULBB(rand_scale_Q14, rand_Gain_Q15), 15);
      psPLC->pitchL_Q8 = silk_SMLAWB(psPLC->pitchL_Q8, psPLC->pitchL_Q8, PITCH_DRIFT_FAC_Q16);
      psPLC->pitchL_Q8 = silk_min_32(psPLC->pitchL_Q8, silk_LSHIFT(silk_SMULBB(PLC_MAX_PITCH_LAG_MS, psDec->fs_kHz), 8));
      lag = silk_RSHIFT_ROUND(psPLC->pitchL_Q8, 8);
   }
   sLPC_Q14_ptr = &sLTP_Q14[psDec->ltp_mem_length-MAX_LPC_ORDER];
   memcpy(sLPC_Q14_ptr, psDec->sLPC_Q14_buf, MAX_LPC_ORDER*sizeof(fl_i32));
   for (i = 0; i < psDec->frame_length; i++) {
      LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
      for (j = 0; j < psDec->LPC_order; j++)
         LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, sLPC_Q14_ptr[MAX_LPC_ORDER+i-j-1], A_Q12[j]);
      sLPC_Q14_ptr[MAX_LPC_ORDER+i] = silk_ADD_SAT32(sLPC_Q14_ptr[MAX_LPC_ORDER+i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
      frame[i] = (fl_i16)silk_SAT16(silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(sLPC_Q14_ptr[MAX_LPC_ORDER+i], prevGain_Q10[1]), 8)));
   }
   memcpy(psDec->sLPC_Q14_buf, &sLPC_Q14_ptr[psDec->frame_length], MAX_LPC_ORDER*sizeof(fl_i32));
   psPLC->rand_seed = rand_seed;
   psPLC->randScale_Q14 = rand_scale_Q14;
   for (i = 0; i < MAX_NB_SUBFR; i++) ctrl->pitchL[i] = lag;
}

static void silk_PLC(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[], int lost) {
   if (psDec->fs_kHz != psDec->sPLC.fs_kHz) {
      silk_PLC_Reset(psDec);
      psDec->sPLC.fs_kHz = psDec->fs_kHz;
   }
   if (lost) {
      silk_PLC_conceal(psDec, ctrl, frame);
      psDec->lossCnt++;
   } else silk_PLC_update(psDec, ctrl);
}

static void silk_PLC_glue_frames(silk_dec_state *psDec, fl_i16 frame[], int length) {
   int i, energy_shift;
   fl_i32 energy;
   silk_plc_state *psPLC = &psDec->sPLC;
   if (psDec->lossCnt) {
      silk_sum_sqr_shift(&psPLC->conc_energy, &psPLC->conc_energy_shift, frame, length);
      psPLC->last_frame_lost = 1;
   } else {
      if (psDec->sPLC.last_frame_lost) {
         silk_sum_sqr_shift(&energy, &energy_shift, frame, length);
         if (energy_shift > psPLC->conc_energy_shift)
            psPLC->conc_energy = silk_RSHIFT(psPLC->conc_energy, energy_shift-psPLC->conc_energy_shift);
         else if (energy_shift < psPLC->conc_energy_shift)
            energy = silk_RSHIFT(energy, psPLC->conc_energy_shift-energy_shift);
         if (energy > psPLC->conc_energy) {
            fl_i32 frac_Q24, LZ, gain_Q16, slope_Q16;
            LZ = silk_CLZ32(psPLC->conc_energy);
            LZ = LZ-1;
            psPLC->conc_energy = silk_LSHIFT(psPLC->conc_energy, LZ);
            energy = silk_RSHIFT(energy, silk_max_32(24-LZ, 0));
            frac_Q24 = silk_DIV32(psPLC->conc_energy, silk_max(energy, 1));
            gain_Q16 = silk_LSHIFT(silk_SQRT_APPROX(frac_Q24), 4);
            slope_Q16 = silk_DIV32_16(((fl_i32)1<<16)-gain_Q16, length);
            slope_Q16 = silk_LSHIFT(slope_Q16, 2);
            for (i = 0; i < length; i++) {
               frame[i] = silk_SMULWB(gain_Q16, frame[i]);
               gain_Q16 += slope_Q16;
               if (gain_Q16 > (fl_i32)1<<16) break;
            }
         }
      }
      psPLC->last_frame_lost = 0;
   }
}

/* ---- Comfort noise generation ---- */
static void silk_CNG_exc(fl_i32 exc_Q14[], fl_i32 exc_buf_Q14[], int length, fl_i32 *rand_seed) {
   fl_i32 seed;
   int i, idx, exc_mask = CNG_BUF_MASK_MAX;
   while (exc_mask > length) exc_mask = silk_RSHIFT(exc_mask, 1);
   seed = *rand_seed;
   for (i = 0; i < length; i++) {
      seed = silk_RAND(seed);
      idx = (int)(silk_RSHIFT(seed, 24)&exc_mask);
      exc_Q14[i] = exc_buf_Q14[idx];
   }
   *rand_seed = seed;
}
static void silk_CNG(silk_dec_state *psDec, silk_dec_ctrl *ctrl, fl_i16 frame[], int length) {
   int i, j, subfr;
   fl_i32 LPC_pred_Q10, max_Gain_Q16, gain_Q16, gain_Q10;
   fl_i16 A_Q12[MAX_LPC_ORDER];
   silk_cng_state *psCNG = &psDec->sCNG;
   if (psDec->fs_kHz != psCNG->fs_kHz) {
      silk_CNG_Reset(psDec);
      psCNG->fs_kHz = psDec->fs_kHz;
   }
   if (psDec->lossCnt == 0 && psDec->prevSignalType == TYPE_NO_VOICE_ACTIVITY) {
      for (i = 0; i < psDec->LPC_order; i++)
         psCNG->CNG_smth_NLSF_Q15[i] += silk_SMULWB((fl_i32)psDec->prevNLSF_Q15[i]-(fl_i32)psCNG->CNG_smth_NLSF_Q15[i], CNG_NLSF_SMTH_Q16);
      max_Gain_Q16 = 0;
      subfr = 0;
      for (i = 0; i < psDec->nb_subfr; i++) {
         if (ctrl->Gains_Q16[i] > max_Gain_Q16) {
            max_Gain_Q16 = ctrl->Gains_Q16[i];
            subfr = i;
         }
      }
      memmove(&psCNG->CNG_exc_buf_Q14[psDec->subfr_length], psCNG->CNG_exc_buf_Q14, (psDec->nb_subfr-1)*psDec->subfr_length*sizeof(fl_i32));
      memcpy(psCNG->CNG_exc_buf_Q14, &psDec->exc_Q14[subfr*psDec->subfr_length], psDec->subfr_length*sizeof(fl_i32));
      for (i = 0; i < psDec->nb_subfr; i++) {
         psCNG->CNG_smth_Gain_Q16 += silk_SMULWB(ctrl->Gains_Q16[i]-psCNG->CNG_smth_Gain_Q16, CNG_GAIN_SMTH_Q16);
         if (silk_SMULWW(psCNG->CNG_smth_Gain_Q16, CNG_GAIN_SMTH_THRESHOLD_Q16) > ctrl->Gains_Q16[i])
            psCNG->CNG_smth_Gain_Q16 = ctrl->Gains_Q16[i];
      }
   }
   if (psDec->lossCnt) {
      fl_i32 CNG_sig_Q14[MAX_FRAME_LENGTH+MAX_LPC_ORDER];
      gain_Q16 = silk_SMULWW(psDec->sPLC.randScale_Q14, psDec->sPLC.prevGain_Q16[1]);
      if (gain_Q16 >= (1<<21) || psCNG->CNG_smth_Gain_Q16 > (1<<23)) {
         gain_Q16 = silk_SMULTT(gain_Q16, gain_Q16);
         gain_Q16 = silk_SUB_LSHIFT32(silk_SMULTT(psCNG->CNG_smth_Gain_Q16, psCNG->CNG_smth_Gain_Q16), gain_Q16, 5);
         gain_Q16 = silk_LSHIFT32(silk_SQRT_APPROX(gain_Q16), 16);
      } else {
         gain_Q16 = silk_SMULWW(gain_Q16, gain_Q16);
         gain_Q16 = silk_SUB_LSHIFT32(silk_SMULWW(psCNG->CNG_smth_Gain_Q16, psCNG->CNG_smth_Gain_Q16), gain_Q16, 5);
         gain_Q16 = silk_LSHIFT32(silk_SQRT_APPROX(gain_Q16), 8);
      }
      gain_Q10 = silk_RSHIFT(gain_Q16, 6);
      silk_CNG_exc(CNG_sig_Q14+MAX_LPC_ORDER, psCNG->CNG_exc_buf_Q14, length, &psCNG->rand_seed);
      silk_NLSF2A(A_Q12, psCNG->CNG_smth_NLSF_Q15, psDec->LPC_order);
      memcpy(CNG_sig_Q14, psCNG->CNG_synth_state, MAX_LPC_ORDER*sizeof(fl_i32));
      for (i = 0; i < length; i++) {
         LPC_pred_Q10 = silk_RSHIFT(psDec->LPC_order, 1);
         for (j = 0; j < psDec->LPC_order; j++)
            LPC_pred_Q10 = silk_SMLAWB(LPC_pred_Q10, CNG_sig_Q14[MAX_LPC_ORDER+i-j-1], A_Q12[j]);
         CNG_sig_Q14[MAX_LPC_ORDER+i] = silk_ADD_SAT32(CNG_sig_Q14[MAX_LPC_ORDER+i], silk_LSHIFT_SAT32(LPC_pred_Q10, 4));
         frame[i] = (fl_i16)silk_ADD_SAT16(frame[i], silk_SAT16(silk_RSHIFT_ROUND(silk_SMULWW(CNG_sig_Q14[MAX_LPC_ORDER+i], gain_Q10), 8)));
      }
      memcpy(psCNG->CNG_synth_state, &CNG_sig_Q14[length], MAX_LPC_ORDER*sizeof(fl_i32));
   } else {
      memset(psCNG->CNG_synth_state, 0, psDec->LPC_order*sizeof(fl_i32));
   }
}

static int silk_decode_frame(silk_dec_state *psDec, ec_ctx *dec, fl_i16 pOut[], fl_i32 *pN, int lostFlag, int condCoding) {
   silk_dec_ctrl ctrl;
   int L, mv_len;
   L = psDec->frame_length;
   ctrl.LTP_scale_Q14 = 0;
   if (lostFlag == FLAG_DECODE_NORMAL || (lostFlag == FLAG_DECODE_LBRR && psDec->LBRR_flags[psDec->nFramesDecoded] == 1)) {
      fl_i16 pulses[(MAX_FRAME_LENGTH+SHELL_CODEC_FRAME_LENGTH-1)&~(SHELL_CODEC_FRAME_LENGTH-1)];
      silk_decode_indices(psDec, dec, psDec->nFramesDecoded, lostFlag, condCoding);
      silk_decode_pulses(dec, pulses, psDec->indices.signalType, psDec->indices.quantOffsetType, psDec->frame_length);
      silk_decode_parameters(psDec, &ctrl, condCoding);
      silk_decode_core(psDec, &ctrl, pOut, pulses);
      mv_len = psDec->ltp_mem_length-psDec->frame_length;
      memmove(psDec->outBuf, &psDec->outBuf[psDec->frame_length], mv_len*sizeof(fl_i16));
      memcpy(&psDec->outBuf[mv_len], pOut, psDec->frame_length*sizeof(fl_i16));
      silk_PLC(psDec, &ctrl, pOut, 0);
      psDec->lossCnt = 0;
      psDec->prevSignalType = psDec->indices.signalType;
      psDec->first_frame_after_reset = 0;
   } else {
      silk_PLC(psDec, &ctrl, pOut, 1);
      mv_len = psDec->ltp_mem_length-psDec->frame_length;
      memmove(psDec->outBuf, &psDec->outBuf[psDec->frame_length], mv_len*sizeof(fl_i16));
      memcpy(&psDec->outBuf[mv_len], pOut, psDec->frame_length*sizeof(fl_i16));
   }
   silk_CNG(psDec, &ctrl, pOut, L);
   silk_PLC_glue_frames(psDec, pOut, L);
   psDec->lagPrev = ctrl.pitchL[psDec->nb_subfr-1];
   *pN = L;
   return 0;
}

static void silk_stereo_decode_pred(ec_ctx *dec, fl_i32 pred_Q13[]) {
   int n, ix[2][3];
   fl_i32 low_Q13, step_Q13;
   n = ec_dec_icdf(dec, fl_stereo_pred_joint_iCDF, 8);
   ix[0][2] = silk_DIV32_16(n, 5);
   ix[1][2] = n-5*ix[0][2];
   for (n = 0; n < 2; n++) {
      ix[n][0] = ec_dec_icdf(dec, fl_uniform3_iCDF, 8);
      ix[n][1] = ec_dec_icdf(dec, fl_uniform5_iCDF, 8);
   }
   for (n = 0; n < 2; n++) {
      ix[n][0] += 3*ix[n][2];
      low_Q13 = fl_stereo_pred_quant_Q13[ix[n][0]];
      step_Q13 = silk_SMULWB(fl_stereo_pred_quant_Q13[ix[n][0]+1]-low_Q13, SILK_FIX_CONST(0.5/STEREO_QUANT_SUB_STEPS, 16));
      pred_Q13[n] = silk_SMLABB(low_Q13, step_Q13, 2*ix[n][1]+1);
   }
   pred_Q13[0] -= pred_Q13[1];
}

static void silk_stereo_MS_to_LR(silk_stereo_dec *state, fl_i16 x1[], fl_i16 x2[], const fl_i32 pred_Q13[], int fs_kHz, int frame_length) {
   int n, denom_Q16, delta0_Q13, delta1_Q13;
   fl_i32 sum, diff, pred0_Q13, pred1_Q13;
   memcpy(x1, state->sMid, 2*sizeof(fl_i16));
   memcpy(x2, state->sSide, 2*sizeof(fl_i16));
   memcpy(state->sMid, &x1[frame_length], 2*sizeof(fl_i16));
   memcpy(state->sSide, &x2[frame_length], 2*sizeof(fl_i16));
   pred0_Q13 = state->pred_prev_Q13[0];
   pred1_Q13 = state->pred_prev_Q13[1];
   denom_Q16 = silk_DIV32_16((fl_i32)1<<16, STEREO_INTERP_LEN_MS*fs_kHz);
   delta0_Q13 = silk_RSHIFT_ROUND(silk_SMULBB(pred_Q13[0]-state->pred_prev_Q13[0], denom_Q16), 16);
   delta1_Q13 = silk_RSHIFT_ROUND(silk_SMULBB(pred_Q13[1]-state->pred_prev_Q13[1], denom_Q16), 16);
   for (n = 0; n < STEREO_INTERP_LEN_MS*fs_kHz; n++) {
      pred0_Q13 += delta0_Q13;
      pred1_Q13 += delta1_Q13;
      sum = silk_LSHIFT(silk_ADD_LSHIFT32(x1[n]+(fl_i32)x1[n+2], x1[n+1], 1), 9);
      sum = silk_SMLAWB(silk_LSHIFT((fl_i32)x2[n+1], 8), sum, pred0_Q13);
      sum = silk_SMLAWB(sum, silk_LSHIFT((fl_i32)x1[n+1], 11), pred1_Q13);
      x2[n+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(sum, 8));
   }
   pred0_Q13 = pred_Q13[0];
   pred1_Q13 = pred_Q13[1];
   for (n = STEREO_INTERP_LEN_MS*fs_kHz; n < frame_length; n++) {
      sum = silk_LSHIFT(silk_ADD_LSHIFT32(x1[n]+(fl_i32)x1[n+2], x1[n+1], 1), 9);
      sum = silk_SMLAWB(silk_LSHIFT((fl_i32)x2[n+1], 8), sum, pred0_Q13);
      sum = silk_SMLAWB(sum, silk_LSHIFT((fl_i32)x1[n+1], 11), pred1_Q13);
      x2[n+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(sum, 8));
   }
   state->pred_prev_Q13[0] = pred_Q13[0];
   state->pred_prev_Q13[1] = pred_Q13[1];
   for (n = 0; n < frame_length; n++) {
      sum = x1[n+1]+(fl_i32)x2[n+1];
      diff = x1[n+1]-(fl_i32)x2[n+1];
      x1[n+1] = (fl_i16)silk_SAT16(sum);
      x2[n+1] = (fl_i16)silk_SAT16(diff);
   }
}

static void silk_dec_reset_all(silk_decoder *d) {
   int n;
   for (n = 0; n < 2; n++) silk_reset_decoder(&d->channel_state[n]);
   memset(&d->sStereo, 0, sizeof(d->sStereo));
   d->prev_decode_only_middle = 0;
}
static void silk_dec_init_all(silk_decoder *d) {
   int n;
   memset(d, 0, sizeof(*d));
   for (n = 0; n < 2; n++) silk_init_decoder(&d->channel_state[n]);
}

/* Decodes one SILK frame (10 or 20 ms) into float output at the API rate. */
static int silk_Decode(silk_decoder *psDec, silk_dec_control *decControl, int lostFlag, int newPacketFlag,
      ec_ctx *dec, float *samplesOut, fl_i32 *nSamplesOut) {
   int i, n, decode_only_middle = 0, ret = 0, has_side, stereo_to_mono;
   fl_i32 nSamplesOutDec = 0, LBRR_symbol, MS_pred_Q13[2] = {0, 0};
   fl_i16 samplesOut1_tmp_storage[2*(MAX_FRAME_LENGTH+2)], *samplesOut1_tmp[2];
   fl_i16 samplesOut2_tmp[MAX_API_FS_KHZ*MAX_FRAME_LENGTH_MS], *resample_out_ptr;
   silk_dec_state *channel_state = psDec->channel_state;
   if (newPacketFlag)
      for (n = 0; n < decControl->nChannelsInternal; n++) channel_state[n].nFramesDecoded = 0;
   if (decControl->nChannelsInternal > psDec->nChannelsInternal) silk_init_decoder(&channel_state[1]);
   stereo_to_mono = decControl->nChannelsInternal == 1 && psDec->nChannelsInternal == 2 &&
         (decControl->internalSampleRate == 1000*channel_state[0].fs_kHz);
   if (channel_state[0].nFramesDecoded == 0) {
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         int fs_kHz_dec;
         if (decControl->payloadSize_ms == 0 || decControl->payloadSize_ms == 10) {
            channel_state[n].nFramesPerPacket = 1;
            channel_state[n].nb_subfr = 2;
         } else if (decControl->payloadSize_ms == 20) {
            channel_state[n].nFramesPerPacket = 1;
            channel_state[n].nb_subfr = 4;
         } else if (decControl->payloadSize_ms == 40) {
            channel_state[n].nFramesPerPacket = 2;
            channel_state[n].nb_subfr = 4;
         } else if (decControl->payloadSize_ms == 60) {
            channel_state[n].nFramesPerPacket = 3;
            channel_state[n].nb_subfr = 4;
         } else return -203;
         fs_kHz_dec = (decControl->internalSampleRate>>10)+1;
         if (fs_kHz_dec != 8 && fs_kHz_dec != 12 && fs_kHz_dec != 16) return -200;
         ret += silk_decoder_set_fs(&channel_state[n], fs_kHz_dec, decControl->API_sampleRate);
      }
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 2 && (psDec->nChannelsAPI == 1 || psDec->nChannelsInternal == 1)) {
      memset(psDec->sStereo.pred_prev_Q13, 0, sizeof(psDec->sStereo.pred_prev_Q13));
      memset(psDec->sStereo.sSide, 0, sizeof(psDec->sStereo.sSide));
      memcpy(&channel_state[1].resampler_state, &channel_state[0].resampler_state, sizeof(silk_resampler_state));
   }
   psDec->nChannelsAPI = decControl->nChannelsAPI;
   psDec->nChannelsInternal = decControl->nChannelsInternal;
   if (lostFlag != FLAG_PACKET_LOST && channel_state[0].nFramesDecoded == 0) {
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         for (i = 0; i < channel_state[n].nFramesPerPacket; i++) channel_state[n].VAD_flags[i] = ec_dec_bit_logp(dec, 1);
         channel_state[n].LBRR_flag = ec_dec_bit_logp(dec, 1);
      }
      for (n = 0; n < decControl->nChannelsInternal; n++) {
         memset(channel_state[n].LBRR_flags, 0, sizeof(channel_state[n].LBRR_flags));
         if (channel_state[n].LBRR_flag) {
            if (channel_state[n].nFramesPerPacket == 1) channel_state[n].LBRR_flags[0] = 1;
            else {
               LBRR_symbol = ec_dec_icdf(dec, fl_LBRR_flags_iCDF_ptr[channel_state[n].nFramesPerPacket-2], 8)+1;
               for (i = 0; i < channel_state[n].nFramesPerPacket; i++) channel_state[n].LBRR_flags[i] = silk_RSHIFT(LBRR_symbol, i)&1;
            }
         }
      }
      if (lostFlag == FLAG_DECODE_NORMAL) {
         for (i = 0; i < channel_state[0].nFramesPerPacket; i++) {
            for (n = 0; n < decControl->nChannelsInternal; n++) {
               if (channel_state[n].LBRR_flags[i]) {
                  fl_i16 pulses[MAX_FRAME_LENGTH];
                  int condCoding;
                  if (decControl->nChannelsInternal == 2 && n == 0) {
                     silk_stereo_decode_pred(dec, MS_pred_Q13);
                     if (channel_state[1].LBRR_flags[i] == 0) decode_only_middle = ec_dec_icdf(dec, fl_stereo_only_code_mid_iCDF, 8);
                  }
                  condCoding = (i > 0 && channel_state[n].LBRR_flags[i-1]) ? CODE_CONDITIONALLY : CODE_INDEPENDENTLY;
                  silk_decode_indices(&channel_state[n], dec, i, 1, condCoding);
                  silk_decode_pulses(dec, pulses, channel_state[n].indices.signalType,
                        channel_state[n].indices.quantOffsetType, channel_state[n].frame_length);
               }
            }
         }
      }
   }
   if (decControl->nChannelsInternal == 2) {
      if (lostFlag == FLAG_DECODE_NORMAL ||
            (lostFlag == FLAG_DECODE_LBRR && channel_state[0].LBRR_flags[channel_state[0].nFramesDecoded] == 1)) {
         silk_stereo_decode_pred(dec, MS_pred_Q13);
         if ((lostFlag == FLAG_DECODE_NORMAL && channel_state[1].VAD_flags[channel_state[0].nFramesDecoded] == 0) ||
               (lostFlag == FLAG_DECODE_LBRR && channel_state[1].LBRR_flags[channel_state[0].nFramesDecoded] == 0))
            decode_only_middle = ec_dec_icdf(dec, fl_stereo_only_code_mid_iCDF, 8);
         else decode_only_middle = 0;
      } else {
         for (n = 0; n < 2; n++) MS_pred_Q13[n] = psDec->sStereo.pred_prev_Q13[n];
      }
   }
   if (decControl->nChannelsInternal == 2 && decode_only_middle == 0 && psDec->prev_decode_only_middle == 1) {
      memset(psDec->channel_state[1].outBuf, 0, sizeof(psDec->channel_state[1].outBuf));
      memset(psDec->channel_state[1].sLPC_Q14_buf, 0, sizeof(psDec->channel_state[1].sLPC_Q14_buf));
      psDec->channel_state[1].lagPrev = 100;
      psDec->channel_state[1].LastGainIndex = 10;
      psDec->channel_state[1].prevSignalType = TYPE_NO_VOICE_ACTIVITY;
      psDec->channel_state[1].first_frame_after_reset = 1;
   }
   samplesOut1_tmp[0] = samplesOut1_tmp_storage;
   samplesOut1_tmp[1] = samplesOut1_tmp_storage+channel_state[0].frame_length+2;
   if (lostFlag == FLAG_DECODE_NORMAL) has_side = !decode_only_middle;
   else has_side = !psDec->prev_decode_only_middle || (decControl->nChannelsInternal == 2 && lostFlag == FLAG_DECODE_LBRR &&
         channel_state[1].LBRR_flags[channel_state[1].nFramesDecoded] == 1);
   for (n = 0; n < decControl->nChannelsInternal; n++) {
      if (n == 0 || has_side) {
         int FrameIndex, condCoding;
         FrameIndex = channel_state[0].nFramesDecoded-n;
         if (FrameIndex <= 0) condCoding = CODE_INDEPENDENTLY;
         else if (lostFlag == FLAG_DECODE_LBRR) condCoding = channel_state[n].LBRR_flags[FrameIndex-1] ? CODE_CONDITIONALLY : CODE_INDEPENDENTLY;
         else if (n > 0 && psDec->prev_decode_only_middle) condCoding = CODE_INDEPENDENTLY_NO_LTP_SCALING;
         else condCoding = CODE_CONDITIONALLY;
         ret += silk_decode_frame(&channel_state[n], dec, &samplesOut1_tmp[n][2], &nSamplesOutDec, lostFlag, condCoding);
      } else {
         memset(&samplesOut1_tmp[n][2], 0, nSamplesOutDec*sizeof(fl_i16));
      }
      channel_state[n].nFramesDecoded++;
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 2) {
      silk_stereo_MS_to_LR(&psDec->sStereo, samplesOut1_tmp[0], samplesOut1_tmp[1], MS_pred_Q13, channel_state[0].fs_kHz, nSamplesOutDec);
   } else {
      memcpy(samplesOut1_tmp[0], psDec->sStereo.sMid, 2*sizeof(fl_i16));
      memcpy(psDec->sStereo.sMid, &samplesOut1_tmp[0][nSamplesOutDec], 2*sizeof(fl_i16));
   }
   *nSamplesOut = silk_DIV32(nSamplesOutDec*decControl->API_sampleRate, silk_SMULBB(channel_state[0].fs_kHz, 1000));
   resample_out_ptr = samplesOut2_tmp;
   for (n = 0; n < silk_min(decControl->nChannelsAPI, decControl->nChannelsInternal); n++) {
      ret += silk_resampler(&channel_state[n].resampler_state, resample_out_ptr, &samplesOut1_tmp[n][1], nSamplesOutDec);
      if (decControl->nChannelsAPI == 2) {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[n+2*i] = resample_out_ptr[i]*(1/32768.f);
      } else {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[i] = resample_out_ptr[i]*(1/32768.f);
      }
   }
   if (decControl->nChannelsAPI == 2 && decControl->nChannelsInternal == 1) {
      if (stereo_to_mono) {
         ret += silk_resampler(&channel_state[1].resampler_state, resample_out_ptr, &samplesOut1_tmp[0][1], nSamplesOutDec);
         for (i = 0; i < *nSamplesOut; i++) samplesOut[1+2*i] = resample_out_ptr[i]*(1/32768.f);
      } else {
         for (i = 0; i < *nSamplesOut; i++) samplesOut[1+2*i] = samplesOut[0+2*i];
      }
   }
   if (channel_state[0].prevSignalType == TYPE_VOICED) {
      static const int mult_tab[3] = {6, 4, 3};
      decControl->prevPitchLag = channel_state[0].lagPrev*mult_tab[(channel_state[0].fs_kHz-8)>>2];
   } else decControl->prevPitchLag = 0;
   if (lostFlag == FLAG_PACKET_LOST) {
      for (i = 0; i < psDec->nChannelsInternal; i++) psDec->channel_state[i].LastGainIndex = 10;
   } else psDec->prev_decode_only_middle = decode_only_middle;
   return ret;
}
