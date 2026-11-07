/* ---- SILK encoder: float analysis (silk/float, x87 extended precision) ---- */
#define SILK_MAX_ORDER_LPC 24

/* silk_inner_product_FLP_avx2: double FMA lanes, scalar tail on the x87 stack. */
static xf silk_inner_product_FLP(const float *data1, const float *data2, int dataSize) {
   int i, l;
   double a1[4] = {0, 0, 0, 0}, a2[4] = {0, 0, 0, 0}, s[4];
   xf result;
   for (i = 0; i < dataSize-7; i += 8)
      for (l = 0; l < 4; l++) {
         a1[l] = fma((double)data1[i+l], (double)data2[i+l], a1[l]);
         a2[l] = fma((double)data1[i+4+l], (double)data2[i+4+l], a2[l]);
      }
   for (; i < dataSize-3; i += 4)
      for (l = 0; l < 4; l++) a1[l] = fma((double)data1[i+l], (double)data2[i+l], a1[l]);
   for (l = 0; l < 4; l++) s[l] = a1[l]+a2[l];
   s[0] = s[0]+s[2];
   s[1] = s[1]+s[3];
   result = s[0]+s[1];
   for (; i < dataSize; i++) result += XF(data1[i])*data2[i];
   return result;
}
static xf silk_energy_FLP(const float *data, int dataSize) {
   int i;
   xf result = 0;
   for (i = 0; i < dataSize-3; i += 4)
      result += ((XF(data[i])*data[i]+XF(data[i+1])*data[i+1])+XF(data[i+2])*data[i+2])+XF(data[i+3])*data[i+3];
   for (; i < dataSize; i++) result += XF(data[i])*data[i];
   return result;
}
static void silk_autocorrelation_FLP(float *results, const float *inputData, int inputDataSize, int correlationCount) {
   int i;
   if (correlationCount > inputDataSize) correlationCount = inputDataSize;
   for (i = 0; i < correlationCount; i++)
      results[i] = (float)silk_inner_product_FLP(inputData, inputData+i, inputDataSize-i);
}
static void silk_scale_vector_FLP(float *data1, float gain, int dataSize) {
   int i;
   for (i = 0; i < dataSize; i++) data1[i] *= gain;
}
static void silk_scale_copy_vector_FLP(float *data_out, const float *data_in, float gain, int dataSize) {
   int i;
   for (i = 0; i < dataSize; i++) data_out[i] = gain*data_in[i];
}
static void silk_bwexpander_FLP(float *ar, const int d, const float chirp) {
   int i;
   xf cfac = chirp;
   for (i = 0; i < d-1; i++) {
      ar[i] = (float)(ar[i]*cfac);
      cfac *= chirp;
   }
   ar[d-1] = (float)(ar[d-1]*cfac);
}
static void silk_k2a_FLP(float *A, const float *rc, fl_i32 order) {
   int k, n;
   for (k = 0; k < order; k++) {
      float rck = rc[k];
      for (n = 0; n < (k+1)>>1; n++) {
         float tmp1 = A[n], tmp2 = A[k-n-1];
         A[n] = (float)(tmp1+XF(tmp2)*rck);
         A[k-n-1] = (float)(tmp2+XF(tmp1)*rck);
      }
      A[k] = -rck;
   }
}
static float silk_schur_FLP(float refl_coef[], const float auto_corr[], int order) {
   int k, n;
   double C[SILK_MAX_ORDER_LPC+1][2];
   k = 0;
   do C[k][0] = C[k][1] = auto_corr[k];
   while (++k <= order);
   for (k = 0; k < order; k++) {
      xf rc_tmp = -XF(C[k+1][0])/(C[0][1] > 1e-9f ? XF(C[0][1]) : XF(1e-9f));
      refl_coef[k] = (float)rc_tmp;
      for (n = 0; n < order-k; n++) {
         double Ctmp1 = C[n+k+1][0], Ctmp2 = C[n][1];
         C[n+k+1][0] = (double)(Ctmp1+Ctmp2*rc_tmp);
         C[n][1] = (double)(Ctmp2+Ctmp1*rc_tmp);
      }
   }
   return (float)C[0][1];
}
static void silk_apply_sine_window_FLP(float px_win[], const float px[], const int win_type, const int length) {
   int k;
   xf freq, c, S0, S1;
   freq = XF(3.1415926536f)/(length+1);
   c = 2.0f-freq*freq;
   if (win_type < 2) {
      S0 = 0;
      S1 = freq;
   } else {
      S0 = 1;
      S1 = XF(.5f)*c;
   }
   for (k = 0; k < length; k += 4) {
      px_win[k] = (float)((XF(px[k])*.5f)*(S0+S1));
      px_win[k+1] = (float)(XF(px[k+1])*S1);
      S0 = c*S1-S0;
      px_win[k+2] = (float)((XF(px[k+2])*.5f)*(S1+S0));
      px_win[k+3] = (float)(XF(px[k+3])*S0);
      S1 = c*S0-S1;
   }
}
static void silk_warped_autocorrelation_FLP(float *corr, const float *input, const float warping, const int length,
      const int order) {
   int n, i;
   xf tmp1, tmp2;
   double state[25] = {0}, C[25] = {0};
   for (n = 0; n < length; n++) {
      tmp1 = input[n];
      for (i = 0; i < order; i += 2) {
         tmp2 = (state[i]+XF(warping)*state[i+1])-XF(warping)*tmp1;
         state[i] = (double)tmp1;
         C[i] = (double)(C[i]+state[0]*tmp1);
         tmp1 = (state[i+1]+XF(warping)*state[i+2])-XF(warping)*tmp2;
         state[i+1] = (double)tmp2;
         C[i+1] = (double)(C[i+1]+state[0]*tmp2);
      }
      state[order] = (double)tmp1;
      C[order] = (double)(C[order]+tmp1*state[0]);
   }
   for (i = 0; i < order+1; i++) corr[i] = (float)C[i];
}
static float silk_LPC_inverse_pred_gain_FLP(const float *A, fl_i32 order) {
   int k, n;
   xf invGain = 1, rc, rc_mult1, rc_mult2;
   float Atmp[SILK_MAX_ORDER_LPC];
   memcpy(Atmp, A, order*sizeof(float));
   for (k = order-1; k > 0; k--) {
      rc = -XF(Atmp[k]);
      rc_mult1 = 1-rc*rc;
      invGain *= rc_mult1;
      if (invGain*1e4f < 1) return 0;
      rc_mult2 = 1/rc_mult1;
      for (n = 0; n < (k+1)>>1; n++) {
         float tmp1 = Atmp[n], tmp2 = Atmp[k-n-1];
         Atmp[n] = (float)((tmp1-tmp2*rc)*rc_mult2);
         Atmp[k-n-1] = (float)((tmp2-tmp1*rc)*rc_mult2);
      }
   }
   rc = -XF(Atmp[0]);
   rc_mult1 = 1-rc*rc;
   invGain *= rc_mult1;
   if (invGain*1e4f < 1) return 0;
   return (float)invGain;
}
static void silk_insertion_sort_decreasing_FLP(float *a, int *idx, const int L, const int K) {
   float value;
   int i, j;
   for (i = 0; i < K; i++) idx[i] = i;
   for (i = 1; i < K; i++) {
      value = a[i];
      for (j = i-1; j >= 0 && value > a[j]; j--) {
         a[j+1] = a[j];
         idx[j+1] = idx[j];
      }
      a[j+1] = value;
      idx[j+1] = i;
   }
   for (i = K; i < L; i++) {
      value = a[i];
      if (value > a[K-1]) {
         for (j = K-2; j >= 0 && value > a[j]; j--) {
            a[j+1] = a[j];
            idx[j+1] = idx[j];
         }
         a[j+1] = value;
         idx[j+1] = i;
      }
   }
}
static void silk_regularize_correlations_FLP(float *XX, float *xx, const float noise, const int D) {
   int i;
   for (i = 0; i < D; i++) XX[i*D+i] += noise;
   xx[0] += noise;
}
static void silk_corrVector_FLP(const float *x, const float *t, const int L, const int Order, float *Xt) {
   int lag;
   const float *ptr1 = &x[Order-1];
   for (lag = 0; lag < Order; lag++) {
      Xt[lag] = (float)silk_inner_product_FLP(ptr1, t, L);
      ptr1--;
   }
}
static void silk_corrMatrix_FLP(const float *x, const int L, const int Order, float *XX) {
   int j, lag;
   xf energy;
   const float *ptr1 = &x[Order-1], *ptr2;
   energy = silk_energy_FLP(ptr1, L);
   XX[0] = (float)energy;
   for (j = 1; j < Order; j++) {
      energy += XF(ptr1[-j])*ptr1[-j]-XF(ptr1[L-j])*ptr1[L-j];
      XX[j*Order+j] = (float)energy;
   }
   ptr2 = &x[Order-2];
   for (lag = 1; lag < Order; lag++) {
      energy = silk_inner_product_FLP(ptr1, ptr2, L);
      XX[lag*Order] = XX[lag] = (float)energy;
      for (j = 1; j < Order-lag; j++) {
         energy += XF(ptr1[-j])*ptr2[-j]-XF(ptr1[L-j])*ptr2[L-j];
         XX[(lag+j)*Order+j] = XX[j*Order+lag+j] = (float)energy;
      }
      ptr2--;
   }
}
static xf silk_residual_energy_covar_FLP(const float *c, float *wXX, const float *wXx, const float wxx, const int D) {
   int i, j, k;
   xf tmp, nrg = 0, regularization;
   regularization = (XF(wXX[D*D-1])+wXX[0])*1e-8f;
   for (k = 0; k < 10; k++) {
      nrg = wxx;
      tmp = 0;
      for (i = 0; i < D; i++) tmp += XF(wXx[i])*c[i];
      nrg -= tmp*2;
      for (i = 0; i < D; i++) {
         tmp = 0;
         for (j = i+1; j < D; j++) tmp += XF(wXX[i*D+j])*c[j];
         nrg += c[i]*(tmp*2+XF(wXX[i*D+i])*c[i]);
      }
      if (nrg > 0) break;
      for (i = 0; i < D; i++) wXX[i*D+i] = (float)(wXX[i*D+i]+regularization);
      regularization *= 2;
   }
   if (k == 10) nrg = 1;
   return nrg;
}
static void silk_LTP_analysis_filter_FLP(float *LTP_res, const float *x, const float B[], const int pitchL[],
      const float invGains[], const int subfr_length, const int nb_subfr, const int pre_length) {
   const float *x_ptr = x, *x_lag_ptr;
   float *LTP_res_ptr = LTP_res;
   int k, i, j;
   for (k = 0; k < nb_subfr; k++) {
      float inv_gain = invGains[k];
      x_lag_ptr = x_ptr-pitchL[k];
      for (i = 0; i < subfr_length+pre_length; i++) {
         xf v = x_ptr[i];
         for (j = 0; j < LTP_ORDER; j++) v -= XF(B[k*LTP_ORDER+j])*x_lag_ptr[LTP_ORDER/2-j];
         LTP_res_ptr[i] = (float)(v*inv_gain);
         x_lag_ptr++;
      }
      LTP_res_ptr += subfr_length+pre_length;
      x_ptr += subfr_length;
   }
}
static void silk_find_LTP_FLP(float XX[], float xX[], const float r_ptr[], const int lag[], const int subfr_length,
      const int nb_subfr) {
   int k;
   float *xX_ptr = xX, *XX_ptr = XX, xx, temp;
   for (k = 0; k < nb_subfr; k++) {
      const float *lag_ptr = r_ptr-(lag[k]+LTP_ORDER/2);
      xf m;
      silk_corrMatrix_FLP(lag_ptr, subfr_length, LTP_ORDER, XX_ptr);
      silk_corrVector_FLP(lag_ptr, r_ptr, subfr_length, LTP_ORDER, xX_ptr);
      xx = (float)silk_energy_FLP(r_ptr, subfr_length+LTP_ORDER);
      m = (XF(XX_ptr[0])+XX_ptr[24])*0.015f+1;
      temp = (float)(1/(xx > m ? XF(xx) : m));
      silk_scale_vector_FLP(XX_ptr, temp, LTP_ORDER*LTP_ORDER);
      silk_scale_vector_FLP(xX_ptr, temp, LTP_ORDER);
      r_ptr += subfr_length;
      XX_ptr += LTP_ORDER*LTP_ORDER;
      xX_ptr += LTP_ORDER;
   }
}
static float silk_burg_modified_FLP(float A[], const float x[], const float minInvGain, const int subfr_length,
      const int nb_subfr, const int D) {
   int k, n, s, reached_max_gain;
   double C0, C_first_row[SILK_MAX_ORDER_LPC], C_last_row[SILK_MAX_ORDER_LPC];
   double CAf[SILK_MAX_ORDER_LPC+1], CAb[SILK_MAX_ORDER_LPC+1], Af[SILK_MAX_ORDER_LPC];
   xf invGain, num, nrg_f, nrg_b, rc, tmp1, tmp2;
   const float *x_ptr;
   C0 = (double)silk_energy_FLP(x, nb_subfr*subfr_length);
   memset(C_first_row, 0, sizeof(C_first_row));
   for (s = 0; s < nb_subfr; s++) {
      x_ptr = x+s*subfr_length;
      for (n = 1; n < D+1; n++)
         C_first_row[n-1] = (double)(C_first_row[n-1]+silk_inner_product_FLP(x_ptr, x_ptr+n, subfr_length-n));
   }
   memcpy(C_last_row, C_first_row, sizeof(C_first_row));
   CAb[0] = CAf[0] = (double)((C0+XF(C0)*1e-5f)+1e-9f);
   invGain = 1;
   reached_max_gain = 0;
   for (n = 0; n < D; n++) {
      for (s = 0; s < nb_subfr; s++) {
         x_ptr = x+s*subfr_length;
         tmp1 = x_ptr[n];
         tmp2 = x_ptr[subfr_length-n-1];
         for (k = 0; k < n; k++) {
            C_first_row[k] = (double)(C_first_row[k]-XF(x_ptr[n-k-1])*x_ptr[n]);
            C_last_row[k] = (double)(C_last_row[k]-XF(x_ptr[subfr_length-n+k])*x_ptr[subfr_length-n-1]);
            tmp1 += XF(x_ptr[n-k-1])*Af[k];
            tmp2 += XF(x_ptr[subfr_length-n+k])*Af[k];
         }
         for (k = 0; k <= n; k++) {
            CAf[k] = (double)(CAf[k]-tmp1*x_ptr[n-k]);
            CAb[k] = (double)(CAb[k]-tmp2*x_ptr[subfr_length-n+k-1]);
         }
      }
      tmp1 = C_first_row[n];
      tmp2 = C_last_row[n];
      for (k = 0; k < n; k++) {
         tmp1 += XF(C_last_row[n-k-1])*Af[k];
         tmp2 += XF(C_first_row[n-k-1])*Af[k];
      }
      CAf[n+1] = (double)tmp1;
      CAb[n+1] = (double)tmp2;
      num = tmp2;
      nrg_b = CAb[0];
      nrg_f = CAf[0];
      for (k = 0; k < n; k++) {
         num += XF(CAb[n-k])*Af[k];
         nrg_b += XF(CAb[k+1])*Af[k];
         nrg_f += XF(CAf[k+1])*Af[k];
      }
      rc = (num*-2.0f)/(nrg_f+nrg_b);
      tmp1 = invGain*(1-rc*rc);
      if (tmp1 <= minInvGain) {
         double num_d = (double)num;
         rc = fl_m_sqrt((double)(1-minInvGain/invGain));
         if (num_d > 0) rc = -rc;
         invGain = minInvGain;
         reached_max_gain = 1;
      } else invGain = tmp1;
      for (k = 0; k < (n+1)>>1; k++) {
         double a1 = Af[k], a2 = Af[n-k-1];
         Af[k] = (double)(a1+rc*a2);
         Af[n-k-1] = (double)(a2+rc*a1);
      }
      Af[n] = (double)rc;
      if (reached_max_gain) {
         for (k = n+1; k < D; k++) Af[k] = 0;
         break;
      }
      for (k = 0; k <= n+1; k++) {
         double t = CAf[k];
         CAf[k] = (double)(t+rc*CAb[n-k+1]);
         CAb[n-k+1] = (double)(CAb[n-k+1]+rc*t);
      }
   }
   if (reached_max_gain) {
      double ig = (double)invGain;
      for (k = 0; k < D; k++) A[k] = -(float)Af[k];
      for (s = 0; s < nb_subfr; s++) C0 = (double)(C0-silk_energy_FLP(x+s*subfr_length, D));
      nrg_f = XF(C0)*ig;
   } else {
      nrg_f = CAf[0];
      tmp1 = 1;
      for (k = 0; k < D; k++) {
         double Atmp = Af[k];
         nrg_f += XF(CAf[k+1])*Atmp;
         tmp1 += XF(Atmp)*Atmp;
         A[k] = -(float)Atmp;
      }
      nrg_f -= (XF(C0)*1e-5f)*tmp1;
   }
   return (float)nrg_f;
}
