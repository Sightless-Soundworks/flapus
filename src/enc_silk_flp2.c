/* ---- SILK encoder: float analysis, part 2 ---- */

/* MinGW log2l/exp2l/pow (x87). */
static xf fl_m_log2l(xf x) {
   xf r, xm1 = x-1.0L;
#ifdef FLAPUS_PORTABLE_MATH
   r = fabsl(xm1) > XF(0.29) ? fl_p_log2(x) : fl_p_ln1p(xm1)*FL_P_LOG2E;
#else
   if (fabsl(xm1) > XF(0.29)) __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   else __asm__("fld1\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"(xm1) : "st(1)");
#endif
   return r;
}
static xf fl_m_exp2l(xf x) {
   xf r, i;
   unsigned short cw0, cw1;
   __asm__ volatile("fnstcw %0" : "=m"(cw0));
   cw1 = cw0|0x0c00;
   __asm__ volatile("fldcw %0" : : "m"(cw1));
   __asm__ volatile("frndint" : "=t"(i) : "0"(x));
   __asm__ volatile("fldcw %0" : : "m"(cw0));
   x -= i;
#ifdef FLAPUS_PORTABLE_MATH
   x = fl_p_f2xm1(x);
#else
   __asm__("f2xm1" : "+t"(x));
#endif
   x += 1;
   __asm__("fscale" : "=t"(r) : "0"(x), "u"(i));
   return r;
}
/* pow(x, y) for x > 0 and finite y (the cases used by the SILK encoder). */
static xf fl_m_pow(double x, double y) {
   double ip;
   if (y == 0 || x == 1) return 1;
   ip = trunc(y);
   if (y == ip && ip <= 2147483647.0 && ip >= -2147483648.0) {
      int n = (int)ip;
      unsigned u;
      xf d = fabs(x), rslt;
      if (n < 0) {
         d = 1/d;
         n = -n;
      }
      u = (unsigned)n;
      rslt = (u&1) ? d : XF(1);
      u >>= 1;
      while (u > 0) {
         d *= d;
         if (u&1) rslt *= d;
         u >>= 1;
      }
      return rslt;
   }
   if (y == 0.5) return fl_x87_sqrt(XF(x));
   return (double)fl_m_exp2l(XF(y)*fl_m_log2l(fabsl(XF(x))));
}
static float silk_log2(double x) { return (float)(XF(3.32192809488736)*fl_m_log10(x)); }
/* 1/(1+exp(negx)) as the reference computes silk_sigmoid(-negx). */
static float silk_sigmoid_neg(xf negx) { return (float)(1.0L/(1.0L+fl_m_exp((double)negx))); }

static void silk_LPC_analysis_filter_FLP(float r_LPC[], const float PredCoef[], const float s[], const int length,
      const int Order) {
   int ix, j;
   for (ix = Order; ix < length; ix++) {
      const float *s_ptr = &s[ix-1];
      xf pred = XF(s_ptr[0])*PredCoef[0];
      for (j = 1; j < Order; j++) pred += XF(s_ptr[-j])*PredCoef[j];
      r_LPC[ix] = (float)(s_ptr[1]-pred);
   }
   memset(r_LPC, 0, Order*sizeof(float));
}

static void silk_A2NLSF_FLP(fl_i16 *NLSF_Q15, const float *pAR, const int LPC_order) {
   int i;
   fl_i32 a_fix_Q16[MAX_LPC_ORDER];
   for (i = 0; i < LPC_order; i++) a_fix_Q16[i] = (fl_i32)lrintf(pAR[i]*65536.0f);
   silk_A2NLSF(NLSF_Q15, a_fix_Q16, LPC_order);
}
static void silk_NLSF2A_FLP(float *pAR, const fl_i16 *NLSF_Q15, const int LPC_order) {
   int i;
   fl_i16 a_fix_Q12[MAX_LPC_ORDER];
   silk_NLSF2A(a_fix_Q12, NLSF_Q15, LPC_order, 0);
   for (i = 0; i < LPC_order; i++) pAR[i] = (float)a_fix_Q12[i]*(1.0f/4096.0f);
}
static void silk_process_NLSFs_FLP(silk_encoder_state *psEncC, float PredCoef[2][MAX_LPC_ORDER], fl_i16 NLSF_Q15[],
      const fl_i16 prev_NLSF_Q15[]) {
   int i, j;
   fl_i16 PredCoef_Q12[2][MAX_LPC_ORDER];
   silk_process_NLSFs(psEncC, PredCoef_Q12, NLSF_Q15, prev_NLSF_Q15);
   for (j = 0; j < 2; j++)
      for (i = 0; i < psEncC->predictLPCOrder; i++) PredCoef[j][i] = (float)PredCoef_Q12[j][i]*(1.0f/4096.0f);
}
static void silk_quant_LTP_gains_FLP(float B[], fl_i8 cbk_index[], fl_i8 *periodicity_index, fl_i32 *sum_log_gain_Q7,
      float *pred_gain_dB, const float XX[], const float xX[], const int subfr_len, const int nb_subfr) {
   int i, pred_gain_dB_Q7;
   fl_i16 B_Q14[MAX_NB_SUBFR*LTP_ORDER];
   fl_i32 XX_Q17[MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER], xX_Q17[MAX_NB_SUBFR*LTP_ORDER];
   for (i = 0; i < nb_subfr*LTP_ORDER*LTP_ORDER; i++) XX_Q17[i] = (fl_i32)lrintf(XX[i]*131072.0f);
   for (i = 0; i < nb_subfr*LTP_ORDER; i++) xX_Q17[i] = (fl_i32)lrintf(xX[i]*131072.0f);
   silk_quant_LTP_gains(B_Q14, cbk_index, periodicity_index, sum_log_gain_Q7, &pred_gain_dB_Q7, XX_Q17, xX_Q17,
         subfr_len, nb_subfr, 0);
   for (i = 0; i < nb_subfr*LTP_ORDER; i++) B[i] = (float)B_Q14[i]*(1.0f/16384.0f);
   *pred_gain_dB = (float)pred_gain_dB_Q7*(1.0f/128.0f);
}
static void silk_NSQ_wrapper_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      SideInfoIndices *psIndices, silk_nsq_state *psNSQ, fl_i8 pulses[], const float x[]) {
   int i, j, LTP_scale_Q14, Lambda_Q10, Tilt_Q14[MAX_NB_SUBFR], HarmShapeGain_Q14[MAX_NB_SUBFR];
   fl_i16 x16[MAX_FRAME_LENGTH], PredCoef_Q12[2][MAX_LPC_ORDER], LTPCoef_Q14[LTP_ORDER*MAX_NB_SUBFR];
   fl_i16 AR_Q13[MAX_NB_SUBFR*MAX_SHAPE_LPC_ORDER];
   fl_i32 Gains_Q16[MAX_NB_SUBFR], LF_shp_Q14[MAX_NB_SUBFR];
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++)
      for (j = 0; j < psEnc->sCmn.shapingLPCOrder; j++)
         AR_Q13[i*MAX_SHAPE_LPC_ORDER+j] = (fl_i16)lrintf(psEncCtrl->AR[i*MAX_SHAPE_LPC_ORDER+j]*8192.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
      LF_shp_Q14[i] = silk_LSHIFT32((fl_i32)lrintf(psEncCtrl->LF_AR_shp[i]*16384.0f), 16)|
            (fl_u16)lrintf(psEncCtrl->LF_MA_shp[i]*16384.0f);
      Tilt_Q14[i] = (int)lrintf(psEncCtrl->Tilt[i]*16384.0f);
      HarmShapeGain_Q14[i] = (int)lrintf(psEncCtrl->HarmShapeGain[i]*16384.0f);
   }
   Lambda_Q10 = (int)lrintf(psEncCtrl->Lambda*1024.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr*LTP_ORDER; i++) LTPCoef_Q14[i] = (fl_i16)lrintf(psEncCtrl->LTPCoef[i]*16384.0f);
   for (j = 0; j < 2; j++)
      for (i = 0; i < psEnc->sCmn.predictLPCOrder; i++)
         PredCoef_Q12[j][i] = (fl_i16)lrintf(psEncCtrl->PredCoef[j][i]*4096.0f);
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) Gains_Q16[i] = (fl_i32)lrintf(psEncCtrl->Gains[i]*65536.0f);
   LTP_scale_Q14 = psIndices->signalType == TYPE_VOICED ? fl_LTPScales_table_Q14[psIndices->LTP_scaleIndex] : 0;
   for (i = 0; i < psEnc->sCmn.frame_length; i++) x16[i] = (fl_i16)lrintf(x[i]);
   if (psEnc->sCmn.nStatesDelayedDecision > 1 || psEnc->sCmn.warping_Q16 > 0)
      silk_NSQ_del_dec_c(&psEnc->sCmn, psNSQ, psIndices, x16, pulses, PredCoef_Q12[0], LTPCoef_Q14, AR_Q13,
            HarmShapeGain_Q14, Tilt_Q14, LF_shp_Q14, Gains_Q16, psEncCtrl->pitchL, Lambda_Q10, LTP_scale_Q14);
   else
      silk_NSQ_c(&psEnc->sCmn, psNSQ, psIndices, x16, pulses, PredCoef_Q12[0], LTPCoef_Q14, AR_Q13,
            HarmShapeGain_Q14, Tilt_Q14, LF_shp_Q14, Gains_Q16, psEncCtrl->pitchL, Lambda_Q10, LTP_scale_Q14);
}

static void silk_residual_energy_FLP(float nrgs[], const float x[], float a[2][MAX_LPC_ORDER], const float gains[],
      const int subfr_length, const int nb_subfr, const int LPC_order) {
   int shift = LPC_order+subfr_length;
   float LPC_res[(MAX_FRAME_LENGTH+MAX_NB_SUBFR*MAX_LPC_ORDER)/2], *LPC_res_ptr = LPC_res+LPC_order;
   silk_LPC_analysis_filter_FLP(LPC_res, a[0], x, 2*shift, LPC_order);
   nrgs[0] = (float)(silk_energy_FLP(LPC_res_ptr, subfr_length)*(double)(gains[0]*gains[0]));
   nrgs[1] = (float)(silk_energy_FLP(LPC_res_ptr+shift, subfr_length)*(double)(gains[1]*gains[1]));
   if (nb_subfr == MAX_NB_SUBFR) {
      silk_LPC_analysis_filter_FLP(LPC_res, a[1], x+2*shift, 2*shift, LPC_order);
      nrgs[2] = (float)(silk_energy_FLP(LPC_res_ptr, subfr_length)*(double)(gains[2]*gains[2]));
      nrgs[3] = (float)(silk_energy_FLP(LPC_res_ptr+shift, subfr_length)*(double)(gains[3]*gains[3]));
   }
}

static void silk_LTP_scale_ctrl_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, int condCoding) {
   if (condCoding == CODE_INDEPENDENTLY) {
      int round_loss = psEnc->sCmn.PacketLoss_perc*psEnc->sCmn.nFramesPerPacket;
      if (psEnc->sCmn.LBRR_flag) round_loss = 2+silk_SMULBB(round_loss, round_loss)/100;
      psEnc->sCmn.indices.LTP_scaleIndex = silk_SMULBB((int)psEncCtrl->LTPredCodGain, round_loss) > silk_log2lin(2900-psEnc->sCmn.SNR_dB_Q7);
      psEnc->sCmn.indices.LTP_scaleIndex += silk_SMULBB((int)psEncCtrl->LTPredCodGain, round_loss) > silk_log2lin(3900-psEnc->sCmn.SNR_dB_Q7);
   } else psEnc->sCmn.indices.LTP_scaleIndex = 0;
   psEncCtrl->LTP_scale = (float)fl_LTPScales_table_Q14[psEnc->sCmn.indices.LTP_scaleIndex]*(1.0f/16384.0f);
}

static void silk_find_LPC_FLP(silk_encoder_state *psEncC, fl_i16 NLSF_Q15[], const float x[], const float minInvGain) {
   int k, subfr_length;
   float a[MAX_LPC_ORDER], res_nrg, res_nrg_2nd, res_nrg_interp, a_tmp[MAX_LPC_ORDER];
   float LPC_res[MAX_FRAME_LENGTH+MAX_NB_SUBFR*MAX_LPC_ORDER];
   fl_i16 NLSF0_Q15[MAX_LPC_ORDER];
   subfr_length = psEncC->subfr_length+psEncC->predictLPCOrder;
   psEncC->indices.NLSFInterpCoef_Q2 = 4;
   res_nrg = silk_burg_modified_FLP(a, x, minInvGain, subfr_length, psEncC->nb_subfr, psEncC->predictLPCOrder);
   if (psEncC->useInterpolatedNLSFs && !psEncC->first_frame_after_reset && psEncC->nb_subfr == MAX_NB_SUBFR) {
      res_nrg = (float)(res_nrg-XF(silk_burg_modified_FLP(a_tmp, x+(MAX_NB_SUBFR/2)*subfr_length, minInvGain,
            subfr_length, MAX_NB_SUBFR/2, psEncC->predictLPCOrder)));
      silk_A2NLSF_FLP(NLSF_Q15, a_tmp, psEncC->predictLPCOrder);
      res_nrg_2nd = 3.40282347e+38f;
      for (k = 3; k >= 0; k--) {
         double e1;
         silk_interpolate(NLSF0_Q15, psEncC->prev_NLSFq_Q15, NLSF_Q15, k, psEncC->predictLPCOrder);
         silk_NLSF2A_FLP(a_tmp, NLSF0_Q15, psEncC->predictLPCOrder);
         silk_LPC_analysis_filter_FLP(LPC_res, a_tmp, x, 2*subfr_length, psEncC->predictLPCOrder);
         e1 = (double)silk_energy_FLP(LPC_res+psEncC->predictLPCOrder, subfr_length-psEncC->predictLPCOrder);
         res_nrg_interp = (float)(silk_energy_FLP(LPC_res+psEncC->predictLPCOrder+subfr_length,
               subfr_length-psEncC->predictLPCOrder)+e1);
         if (res_nrg_interp < res_nrg) {
            res_nrg = res_nrg_interp;
            psEncC->indices.NLSFInterpCoef_Q2 = (fl_i8)k;
         } else if (res_nrg_interp > res_nrg_2nd) break;
         res_nrg_2nd = res_nrg_interp;
      }
   }
   if (psEncC->indices.NLSFInterpCoef_Q2 == 4) silk_A2NLSF_FLP(NLSF_Q15, a, psEncC->predictLPCOrder);
}

static void silk_find_pred_coefs_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float res_pitch[], const float x[], int condCoding) {
   int i;
   float XXLTP[MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER], xXLTP[MAX_NB_SUBFR*LTP_ORDER], invGains[MAX_NB_SUBFR], minInvGain;
   float LPC_in_pre[MAX_NB_SUBFR*MAX_LPC_ORDER+MAX_FRAME_LENGTH];
   fl_i16 NLSF_Q15[MAX_LPC_ORDER] = {0};
   for (i = 0; i < psEnc->sCmn.nb_subfr; i++) invGains[i] = 1.0f/psEncCtrl->Gains[i];
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      silk_find_LTP_FLP(XXLTP, xXLTP, res_pitch, psEncCtrl->pitchL, psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr);
      silk_quant_LTP_gains_FLP(psEncCtrl->LTPCoef, psEnc->sCmn.indices.LTPIndex, &psEnc->sCmn.indices.PERIndex,
            &psEnc->sCmn.sum_log_gain_Q7, &psEncCtrl->LTPredCodGain, XXLTP, xXLTP, psEnc->sCmn.subfr_length,
            psEnc->sCmn.nb_subfr);
      silk_LTP_scale_ctrl_FLP(psEnc, psEncCtrl, condCoding);
      silk_LTP_analysis_filter_FLP(LPC_in_pre, x-psEnc->sCmn.predictLPCOrder, psEncCtrl->LTPCoef, psEncCtrl->pitchL,
            invGains, psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr, psEnc->sCmn.predictLPCOrder);
   } else {
      const float *x_ptr = x-psEnc->sCmn.predictLPCOrder;
      float *x_pre_ptr = LPC_in_pre;
      for (i = 0; i < psEnc->sCmn.nb_subfr; i++) {
         silk_scale_copy_vector_FLP(x_pre_ptr, x_ptr, invGains[i], psEnc->sCmn.subfr_length+psEnc->sCmn.predictLPCOrder);
         x_pre_ptr += psEnc->sCmn.subfr_length+psEnc->sCmn.predictLPCOrder;
         x_ptr += psEnc->sCmn.subfr_length;
      }
      memset(psEncCtrl->LTPCoef, 0, psEnc->sCmn.nb_subfr*LTP_ORDER*sizeof(float));
      psEncCtrl->LTPredCodGain = 0;
      psEnc->sCmn.sum_log_gain_Q7 = 0;
   }
   if (psEnc->sCmn.first_frame_after_reset) minInvGain = 1.0f/1e2f;
   else {
      xf m = XF((float)fl_m_pow(2, (double)(XF(psEncCtrl->LTPredCodGain)/3)))/1e4f;
      minInvGain = (float)(m/(XF(.75f)*psEncCtrl->coding_quality+.25f));
   }
   silk_find_LPC_FLP(&psEnc->sCmn, NLSF_Q15, LPC_in_pre, minInvGain);
   silk_process_NLSFs_FLP(&psEnc->sCmn, psEncCtrl->PredCoef, NLSF_Q15, psEnc->sCmn.prev_NLSFq_Q15);
   silk_residual_energy_FLP(psEncCtrl->ResNrg, LPC_in_pre, psEncCtrl->PredCoef, psEncCtrl->Gains,
         psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr, psEnc->sCmn.predictLPCOrder);
   memcpy(psEnc->sCmn.prev_NLSFq_Q15, NLSF_Q15, sizeof(psEnc->sCmn.prev_NLSFq_Q15));
}

static void silk_process_gains_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, int condCoding) {
   silk_shape_state_FLP *psShapeSt = &psEnc->sShape;
   int k;
   fl_i32 pGains_Q16[MAX_NB_SUBFR];
   float InvMaxSqrVal;
   xf quant_offset, lambda;
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      float sig = silk_sigmoid_neg((XF(psEncCtrl->LTPredCodGain)-12.0f)*-.25f);
      xf s = 1-XF(.5f)*sig;
      for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(psEncCtrl->Gains[k]*s);
   }
   InvMaxSqrVal = (float)(fl_m_pow(2.0, (double)((21.0f-XF(1/128.0f)*psEnc->sCmn.SNR_dB_Q7)*.33f))/psEnc->sCmn.subfr_length);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) {
      float gain = psEncCtrl->Gains[k];
      xf g = fl_m_sqrtf((float)(XF(gain)*gain+XF(psEncCtrl->ResNrg[k])*InvMaxSqrVal));
      psEncCtrl->Gains[k] = (float)(g < 32767.0f ? g : XF(32767.0f));
   }
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) pGains_Q16[k] = (fl_i32)(psEncCtrl->Gains[k]*65536.0f);
   memcpy(psEncCtrl->GainsUnq_Q16, pGains_Q16, psEnc->sCmn.nb_subfr*sizeof(fl_i32));
   psEncCtrl->lastGainIndexPrev = psShapeSt->LastGainIndex;
   silk_gains_quant(psEnc->sCmn.indices.GainsIndices, pGains_Q16, &psShapeSt->LastGainIndex,
         condCoding == CODE_CONDITIONALLY, psEnc->sCmn.nb_subfr);
   for (k = 0; k < psEnc->sCmn.nb_subfr; k++) psEncCtrl->Gains[k] = (float)(XF(1.0f/65536.0f)*pGains_Q16[k]);
   if (psEnc->sCmn.indices.signalType == TYPE_VOICED) {
      if (XF(1.0f/32768.0f)*psEnc->sCmn.input_tilt_Q15+psEncCtrl->LTPredCodGain > 1.0f) psEnc->sCmn.indices.quantOffsetType = 0;
      else psEnc->sCmn.indices.quantOffsetType = 1;
   }
   quant_offset = XF(1.0f/1024.0f)*fl_Quantization_Offsets_Q10[psEnc->sCmn.indices.signalType>>1][psEnc->sCmn.indices.quantOffsetType];
   lambda = XF(-0.05f)*psEnc->sCmn.nStatesDelayedDecision+1.2f;
   lambda += (XF(-0.2f)*psEnc->sCmn.speech_activity_Q8)*(1.0f/256.0f);
   lambda += XF(-0.1f)*psEncCtrl->input_quality;
   lambda += XF(-0.2f)*psEncCtrl->coding_quality;
   lambda += XF(0.8f)*quant_offset;
   psEncCtrl->Lambda = (float)lambda;
}

static void silk_find_pitch_lags_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl, float res[],
      const float x[]);
static void silk_noise_shape_analysis_FLP(silk_encoder_state_FLP *psEnc, silk_encoder_control_FLP *psEncCtrl,
      const float *pitch_res, const float *x);
