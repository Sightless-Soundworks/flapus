/* ---- CELT float DSP with SSE/AVX2 evaluation order (decoder side) ---- */

/* Exact fused multiply-add: a*b is exact in double, then round-to-odd for the sum. */
static float fl_fmaf(float a, float b, float c) {
   double p = (double)a*(double)b, s = p+(double)c, bb = s-p;
   double err = (p-(s-bb))+((double)c-bb);
   if (err != 0) {
      fl_u64 bits;
      memcpy(&bits, &s, 8);
      if ((bits&1) == 0) {
         if ((err > 0) == (s > 0)) bits++;
         else bits--;
         memcpy(&s, &bits, 8);
      }
   }
   return (float)s;
}

/* celt_inner_prod_sse: four lanes, then (l0+l2)+(l1+l3), then the tail. */
static float fl_inner_prod(const float *x, const float *y, int N) {
   float s0 = 0, s1 = 0, s2 = 0, s3 = 0, xy;
   int i;
   for (i = 0; i < N-3; i += 4) {
      s0 = s0+x[i]*y[i];
      s1 = s1+x[i+1]*y[i+1];
      s2 = s2+x[i+2]*y[i+2];
      s3 = s3+x[i+3]*y[i+3];
   }
   xy = (s0+s2)+(s1+s3);
   for (; i < N; i++) xy = xy+x[i]*y[i];
   return xy;
}

/* xcorr_kernel_sse */
static void fl_xcorr_kernel(const float *x, const float *y, float sum[4], int len) {
   float a[4], b[4] = {0, 0, 0, 0};
   int j, k;
   for (k = 0; k < 4; k++) a[k] = sum[k];
   for (j = 0; j < len-3; j += 4) {
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      for (k = 0; k < 4; k++) b[k] = b[k]+x[j+1]*y[j+1+k];
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j+2]*y[j+2+k];
      for (k = 0; k < 4; k++) b[k] = b[k]+x[j+3]*y[j+3+k];
   }
   if (j < len) {
      for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      if (++j < len) {
         for (k = 0; k < 4; k++) b[k] = b[k]+x[j]*y[j+k];
         if (++j < len)
            for (k = 0; k < 4; k++) a[k] = a[k]+x[j]*y[j+k];
      }
   }
   for (k = 0; k < 4; k++) sum[k] = a[k]+b[k];
}

/* celt_pitch_xcorr_avx2: 8 FMA lanes per lag, then a fixed pairwise reduction. */
static void fl_pitch_xcorr(const float *x, const float *y, float *xcorr, int len, int max_pitch) {
   int i, k, l, j;
   for (i = 0; i < max_pitch-7; i += 8) {
      for (k = 0; k < 8; k++) {
         float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
         const float *yk = y+i+k;
         for (j = 0; j < len-7; j += 8)
            for (l = 0; l < 8; l++) acc[l] = fl_fmaf(x[j+l], yk[j+l], acc[l]);
         if (j != len)
            for (l = 0; l < len-j; l++) acc[l] = fl_fmaf(x[j+l], yk[j+l], acc[l]);
         xcorr[i+k] = ((acc[0]+acc[4])+(acc[1]+acc[5]))+((acc[2]+acc[6])+(acc[3]+acc[7]));
      }
   }
   for (; i < max_pitch; i++) xcorr[i] = fl_inner_prod(x, y+i, len);
}

/* ---- FFT (kiss_fft, float) ---- */
static void fl_kf_bfly2(fl_cpx *Fout, int m, int N) {
   const float tw = 0.7071067812f;
   int i;
   (void)m;
   for (i = 0; i < N; i++) {
      fl_cpx *Fout2 = Fout+4, t;
      t = Fout2[0];
      Fout2[0].r = Fout[0].r-t.r; Fout2[0].i = Fout[0].i-t.i;
      Fout[0].r += t.r; Fout[0].i += t.i;
      t.r = (Fout2[1].r+Fout2[1].i)*tw;
      t.i = (Fout2[1].i-Fout2[1].r)*tw;
      Fout2[1].r = Fout[1].r-t.r; Fout2[1].i = Fout[1].i-t.i;
      Fout[1].r += t.r; Fout[1].i += t.i;
      t.r = Fout2[2].i;
      t.i = -Fout2[2].r;
      Fout2[2].r = Fout[2].r-t.r; Fout2[2].i = Fout[2].i-t.i;
      Fout[2].r += t.r; Fout[2].i += t.i;
      t.r = (Fout2[3].i-Fout2[3].r)*tw;
      t.i = (-(Fout2[3].i+Fout2[3].r))*tw;
      Fout2[3].r = Fout[3].r-t.r; Fout2[3].i = Fout[3].i-t.i;
      Fout[3].r += t.r; Fout[3].i += t.i;
      Fout += 8;
   }
}
#define FL_CMUL(m,a,b) do { (m).r = (a).r*(b).r-(a).i*(b).i; (m).i = (a).r*(b).i+(a).i*(b).r; } while (0)
static void fl_kf_bfly4(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i;
   if (m == 1) {
      for (i = 0; i < N; i++) {
         fl_cpx s0, s1;
         s0.r = Fout[0].r-Fout[2].r; s0.i = Fout[0].i-Fout[2].i;
         Fout[0].r += Fout[2].r; Fout[0].i += Fout[2].i;
         s1.r = Fout[1].r+Fout[3].r; s1.i = Fout[1].i+Fout[3].i;
         Fout[2].r = Fout[0].r-s1.r; Fout[2].i = Fout[0].i-s1.i;
         Fout[0].r += s1.r; Fout[0].i += s1.i;
         s1.r = Fout[1].r-Fout[3].r; s1.i = Fout[1].i-Fout[3].i;
         Fout[1].r = s0.r+s1.i;
         Fout[1].i = s0.i-s1.r;
         Fout[3].r = s0.r-s1.i;
         Fout[3].i = s0.i+s1.r;
         Fout += 4;
      }
   } else {
      int j, m2 = 2*m, m3 = 3*m;
      fl_cpx s[6], *Fout_beg = Fout;
      for (i = 0; i < N; i++) {
         const fl_cpx *tw1, *tw2, *tw3;
         Fout = Fout_beg+i*mm;
         tw3 = tw2 = tw1 = fl_fft_twiddles48000_960;
         for (j = 0; j < m; j++) {
            FL_CMUL(s[0], Fout[m], *tw1);
            FL_CMUL(s[1], Fout[m2], *tw2);
            FL_CMUL(s[2], Fout[m3], *tw3);
            s[5].r = Fout->r-s[1].r; s[5].i = Fout->i-s[1].i;
            Fout->r += s[1].r; Fout->i += s[1].i;
            s[3].r = s[0].r+s[2].r; s[3].i = s[0].i+s[2].i;
            s[4].r = s[0].r-s[2].r; s[4].i = s[0].i-s[2].i;
            Fout[m2].r = Fout->r-s[3].r; Fout[m2].i = Fout->i-s[3].i;
            tw1 += fstride;
            tw2 += fstride*2;
            tw3 += fstride*3;
            Fout->r += s[3].r; Fout->i += s[3].i;
            Fout[m].r = s[5].r+s[4].i;
            Fout[m].i = s[5].i-s[4].r;
            Fout[m3].r = s[5].r-s[4].i;
            Fout[m3].i = s[5].i+s[4].r;
            ++Fout;
         }
      }
   }
}
static void fl_kf_bfly3(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i, k, m2 = 2*m;
   const fl_cpx *tw1, *tw2;
   fl_cpx s[5], epi3, *Fout_beg = Fout;
   epi3 = fl_fft_twiddles48000_960[fstride*m];
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      tw1 = tw2 = fl_fft_twiddles48000_960;
      k = m;
      do {
         FL_CMUL(s[1], Fout[m], *tw1);
         FL_CMUL(s[2], Fout[m2], *tw2);
         s[3].r = s[1].r+s[2].r; s[3].i = s[1].i+s[2].i;
         s[0].r = s[1].r-s[2].r; s[0].i = s[1].i-s[2].i;
         tw1 += fstride;
         tw2 += fstride*2;
         Fout[m].r = Fout->r-s[3].r*.5f;
         Fout[m].i = Fout->i-s[3].i*.5f;
         s[0].r *= epi3.i; s[0].i *= epi3.i;
         Fout->r += s[3].r; Fout->i += s[3].i;
         Fout[m2].r = Fout[m].r+s[0].i;
         Fout[m2].i = Fout[m].i-s[0].r;
         Fout[m].r = Fout[m].r-s[0].i;
         Fout[m].i = Fout[m].i+s[0].r;
         ++Fout;
      } while (--k);
   }
}
static void fl_kf_bfly5(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   fl_cpx *F0, *F1, *F2, *F3, *F4, s[13], ya, yb, *Fout_beg = Fout;
   const fl_cpx *tw = fl_fft_twiddles48000_960;
   int i, u;
   ya = tw[fstride*m];
   yb = tw[fstride*2*m];
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      F0 = Fout; F1 = F0+m; F2 = F0+2*m; F3 = F0+3*m; F4 = F0+4*m;
      for (u = 0; u < m; ++u) {
         s[0] = *F0;
         FL_CMUL(s[1], *F1, tw[u*fstride]);
         FL_CMUL(s[2], *F2, tw[2*u*fstride]);
         FL_CMUL(s[3], *F3, tw[3*u*fstride]);
         FL_CMUL(s[4], *F4, tw[4*u*fstride]);
         s[7].r = s[1].r+s[4].r; s[7].i = s[1].i+s[4].i;
         s[10].r = s[1].r-s[4].r; s[10].i = s[1].i-s[4].i;
         s[8].r = s[2].r+s[3].r; s[8].i = s[2].i+s[3].i;
         s[9].r = s[2].r-s[3].r; s[9].i = s[2].i-s[3].i;
         F0->r = F0->r+(s[7].r+s[8].r);
         F0->i = F0->i+(s[7].i+s[8].i);
         s[5].r = s[0].r+(s[7].r*ya.r+s[8].r*yb.r);
         s[5].i = s[0].i+(s[7].i*ya.r+s[8].i*yb.r);
         s[6].r = s[10].i*ya.i+s[9].i*yb.i;
         s[6].i = -(s[10].r*ya.i+s[9].r*yb.i);
         F1->r = s[5].r-s[6].r; F1->i = s[5].i-s[6].i;
         F4->r = s[5].r+s[6].r; F4->i = s[5].i+s[6].i;
         s[11].r = s[0].r+(s[7].r*yb.r+s[8].r*ya.r);
         s[11].i = s[0].i+(s[7].i*yb.r+s[8].i*ya.r);
         s[12].r = s[9].i*ya.i-s[10].i*yb.i;
         s[12].i = s[10].r*yb.i-s[9].r*ya.i;
         F2->r = s[11].r+s[12].r; F2->i = s[11].i+s[12].i;
         F3->r = s[11].r-s[12].r; F3->i = s[11].i-s[12].i;
         ++F0; ++F1; ++F2; ++F3; ++F4;
      }
   }
}
static void fl_fft_impl(const fl_fft_state *st, fl_cpx *fout) {
   int m2, m, p, L, i, shift, fstride[8];
   shift = st->shift > 0 ? st->shift : 0;
   fstride[0] = 1;
   L = 0;
   do {
      p = st->factors[2*L];
      m = st->factors[2*L+1];
      fstride[L+1] = fstride[L]*p;
      L++;
   } while (m != 1);
   m = st->factors[2*L-1];
   for (i = L-1; i >= 0; i--) {
      m2 = i != 0 ? st->factors[2*i-1] : 1;
      switch (st->factors[2*i]) {
      case 2: fl_kf_bfly2(fout, m, fstride[i]); break;
      case 4: fl_kf_bfly4(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 3: fl_kf_bfly3(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 5: fl_kf_bfly5(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      }
      m = m2;
   }
}

/* Inverse MDCT with windowed overlap (clt_mdct_backward_c). */
static void fl_mdct_backward(float *in, float *out, int overlap, int shift, int stride) {
   const float *trig = fl_mdct_twiddles960, *window = fl_window120;
   int i, N = 1920, N2, N4;
   for (i = 0; i < shift; i++) {
      N >>= 1;
      trig += N;
   }
   N2 = N>>1;
   N4 = N>>2;
   {
      const float *xp1 = in, *xp2 = in+stride*(N2-1), *t = trig;
      float *yp = out+(overlap>>1);
      const fl_i16 *bitrev = fl_fft_states[shift].bitrev;
      for (i = 0; i < N4; i++) {
         int rev = *bitrev++;
         float x1 = *xp1, x2 = *xp2;
         yp[2*rev+1] = x2*t[i]+x1*t[N4+i];
         yp[2*rev] = x1*t[i]-x2*t[N4+i];
         xp1 += 2*stride;
         xp2 -= 2*stride;
      }
   }
   fl_fft_impl(&fl_fft_states[shift], (fl_cpx*)(out+(overlap>>1)));
   {
      float *yp0 = out+(overlap>>1), *yp1 = out+(overlap>>1)+N2-2;
      const float *t = trig;
      for (i = 0; i < (N4+1)>>1; i++) {
         float re, im, yr, yi, t0, t1;
         re = yp0[1];
         im = yp0[0];
         t0 = t[i];
         t1 = t[N4+i];
         yr = re*t0+im*t1;
         yi = re*t1-im*t0;
         re = yp1[1];
         im = yp1[0];
         yp0[0] = yr;
         yp1[1] = yi;
         t0 = t[(N4-i-1)];
         t1 = t[(N2-i-1)];
         yr = re*t0+im*t1;
         yi = re*t1-im*t0;
         yp1[0] = yr;
         yp0[1] = yi;
         yp0 += 2;
         yp1 -= 2;
      }
   }
   {
      float *xp1 = out+overlap-1, *yp1 = out;
      const float *wp1 = window, *wp2 = window+overlap-1;
      for (i = 0; i < overlap/2; i++) {
         float x1 = *xp1, x2 = *yp1;
         *yp1++ = x2*(*wp2)-x1*(*wp1);
         *xp1-- = x2*(*wp1)+x1*(*wp2);
         wp1++;
         wp2--;
      }
   }
}

/* ---- Comb (pitch post-)filter ---- */
static const float fl_comb_gains[3][3] = {
   {0.3066406250f, 0.2170410156f, 0.1296386719f}, {0.4638671875f, 0.2680664062f, 0.f},
   {0.7998046875f, 0.1000976562f, 0.f}};

/* comb_filter_const_sse: processes multiples of 4 samples. */
static void fl_comb_filter_const(float *y, float *x, int T, int N, float g10, float g11, float g12) {
   int i, k;
   for (i = 0; i < N-3; i += 4) {
      for (k = 0; k < 4; k++) {
         int n = i+k;
         float yi = x[n]+g10*x[n-T];
         float yi2 = g11*(x[n-T+1]+x[n-T-1])+g12*(x[n-T+2]+x[n-T-2]);
         y[n] = yi+yi2;
      }
   }
}

static void fl_comb_filter(float *y, float *x, int T0, int T1, int N, float g0, float g1,
      int tapset0, int tapset1, const float *window, int overlap) {
   int i;
   float g00, g01, g02, g10, g11, g12, x0, x1, x2, x3, x4;
   if (g0 == 0 && g1 == 0) {
      if (x != y) FL_COPY(y, x, N);
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
      float f;
      x0 = x[i-T1+2];
      f = window[i]*window[i];
      y[i] = x[i]+((1.0f-f)*g00)*x[i-T0]
            +((1.0f-f)*g01)*(x[i-T0+1]+x[i-T0-1])
            +((1.0f-f)*g02)*(x[i-T0+2]+x[i-T0-2])
            +(f*g10)*x2
            +(f*g11)*(x1+x3)
            +(f*g12)*(x0+x4);
      x4 = x3;
      x3 = x2;
      x2 = x1;
      x1 = x0;
   }
   if (g1 == 0) {
      if (x != y) FL_COPY(y+overlap, x+overlap, N-overlap);
      return;
   }
   fl_comb_filter_const(y+i, x+i, T1, N-i, g10, g11, g12);
}

/* ---- LPC helpers used by the CELT PLC ---- */
static void fl_celt_lpc(float *lpc, const float *ac, int p) {
   int i, j;
   float r, error = ac[0];
   FL_CLEAR(lpc, p);
   if (ac[0] > 1e-10f) {
      for (i = 0; i < p; i++) {
         float rr = 0;
         for (j = 0; j < i; j++) rr += lpc[j]*ac[i-j];
         rr += ac[i+1];
         r = -(rr/error);
         lpc[i] = r;
         for (j = 0; j < (i+1)>>1; j++) {
            float tmp1 = lpc[j], tmp2 = lpc[i-1-j];
            lpc[j] = tmp1+r*tmp2;
            lpc[i-1-j] = tmp2+r*tmp1;
         }
         error = error-(r*r)*error;
         if (error <= .001f*ac[0]) break;
      }
   }
}
static void fl_celt_fir(const float *x, const float *num, float *y, int N, int ord) {
   int i, j;
   float rnum[CELT_LPC_ORDER];
   for (i = 0; i < ord; i++) rnum[i] = num[ord-i-1];
   for (i = 0; i < N-3; i += 4) {
      float sum[4];
      sum[0] = x[i]; sum[1] = x[i+1]; sum[2] = x[i+2]; sum[3] = x[i+3];
      fl_xcorr_kernel(rnum, x+i-ord, sum, ord);
      y[i] = sum[0]; y[i+1] = sum[1]; y[i+2] = sum[2]; y[i+3] = sum[3];
   }
   for (; i < N; i++) {
      float sum = x[i];
      for (j = 0; j < ord; j++) sum = sum+rnum[j]*x[i+j-ord];
      y[i] = sum;
   }
}
static void fl_celt_iir(const float *x, const float *den, float *_y, int N, int ord, float *mem) {
   int i, j;
   float rden[CELT_LPC_ORDER], y[1024+CELT_OVERLAP+CELT_LPC_ORDER+8];
   for (i = 0; i < ord; i++) rden[i] = den[ord-i-1];
   for (i = 0; i < ord; i++) y[i] = -mem[ord-i-1];
   for (; i < N+ord; i++) y[i] = 0;
   for (i = 0; i < N-3; i += 4) {
      float sum[4];
      sum[0] = x[i]; sum[1] = x[i+1]; sum[2] = x[i+2]; sum[3] = x[i+3];
      fl_xcorr_kernel(rden, y+i, sum, ord);
      y[i+ord] = -sum[0];
      _y[i] = sum[0];
      sum[1] = sum[1]+y[i+ord]*den[0];
      y[i+ord+1] = -sum[1];
      _y[i+1] = sum[1];
      sum[2] = sum[2]+y[i+ord+1]*den[0];
      sum[2] = sum[2]+y[i+ord]*den[1];
      y[i+ord+2] = -sum[2];
      _y[i+2] = sum[2];
      sum[3] = sum[3]+y[i+ord+2]*den[0];
      sum[3] = sum[3]+y[i+ord+1]*den[1];
      sum[3] = sum[3]+y[i+ord]*den[2];
      y[i+ord+3] = -sum[3];
      _y[i+3] = sum[3];
   }
   for (; i < N; i++) {
      float sum = x[i];
      for (j = 0; j < ord; j++) sum -= rden[j]*y[i+j];
      y[i+ord] = sum;
      _y[i] = sum;
   }
   for (i = 0; i < ord; i++) mem[i] = _y[N-i-1];
}
static void fl_celt_autocorr(const float *x, float *ac, const float *window, int overlap, int lag, int n) {
   float d, xx[DEC_PITCH_BUF_SIZE];
   int i, k, fastN = n-lag;
   const float *xptr;
   if (overlap == 0) xptr = x;
   else {
      for (i = 0; i < n; i++) xx[i] = x[i];
      for (i = 0; i < overlap; i++) {
         float w = window[i];
         xx[i] = x[i]*w;
         xx[n-i-1] = x[n-i-1]*w;
      }
      xptr = xx;
   }
   fl_pitch_xcorr(xptr, xptr, ac, fastN, lag+1);
   for (k = 0; k <= lag; k++) {
      for (i = k+fastN, d = 0; i < n; i++) d = d+xptr[i]*xptr[i-k];
      ac[k] += d;
   }
}

/* ---- Pitch search (used by the CELT PLC) ---- */
static void fl_find_best_pitch(float *xcorr, float *y, int len, int max_pitch, int *best_pitch) {
   int i, j;
   float Syy = 1, best_num[2], best_den[2];
   best_num[0] = -1; best_num[1] = -1;
   best_den[0] = 0; best_den[1] = 0;
   best_pitch[0] = 0; best_pitch[1] = 1;
   for (j = 0; j < len; j++) Syy = Syy+y[j]*y[j];
   for (i = 0; i < max_pitch; i++) {
      if (xcorr[i] > 0) {
         float num, xcorr16 = xcorr[i];
         xcorr16 *= 1e-12f;
         num = xcorr16*xcorr16;
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
      Syy += y[i+len]*y[i+len]-y[i]*y[i];
      Syy = FL_MAX(1, Syy);
   }
}
static void fl_celt_fir5(float *x, const float *num, int N) {
   int i;
   float num0 = num[0], num1 = num[1], num2 = num[2], num3 = num[3], num4 = num[4];
   float mem0 = 0, mem1 = 0, mem2 = 0, mem3 = 0, mem4 = 0;
   for (i = 0; i < N; i++) {
      float sum = x[i];
      sum = sum+num0*mem0;
      sum = sum+num1*mem1;
      sum = sum+num2*mem2;
      sum = sum+num3*mem3;
      sum = sum+num4*mem4;
      mem4 = mem3;
      mem3 = mem2;
      mem2 = mem1;
      mem1 = mem0;
      mem0 = x[i];
      x[i] = sum;
   }
}
static void fl_pitch_downsample(float *x[], float *x_lp, int len, int C, int factor) {
   int i, offset = factor/2;
   float ac[5], tmp = 1.0f, lpc[4], lpc2[5], c1 = .8f;
   for (i = 1; i < len; i++)
      x_lp[i] = .25f*x[0][(factor*i-offset)]+.25f*x[0][(factor*i+offset)]+.5f*x[0][factor*i];
   x_lp[0] = .25f*x[0][offset]+.5f*x[0][0];
   if (C == 2) {
      for (i = 1; i < len; i++)
         x_lp[i] += .25f*x[1][(factor*i-offset)]+.25f*x[1][(factor*i+offset)]+.5f*x[1][factor*i];
      x_lp[0] += .25f*x[1][offset]+.5f*x[1][0];
   }
   fl_celt_autocorr(x_lp, ac, NULL, 0, 4, len);
   ac[0] *= 1.0001f;
   for (i = 1; i <= 4; i++) ac[i] -= ac[i]*(.008f*i)*(.008f*i);
   fl_celt_lpc(lpc, ac, 4);
   for (i = 0; i < 4; i++) {
      tmp = .9f*tmp;
      lpc[i] = lpc[i]*tmp;
   }
   lpc2[0] = lpc[0]+.8f;
   lpc2[1] = lpc[1]+c1*lpc[0];
   lpc2[2] = lpc[2]+c1*lpc[1];
   lpc2[3] = lpc[3]+c1*lpc[2];
   lpc2[4] = c1*lpc[3];
   fl_celt_fir5(x_lp, lpc2, len);
}
static void fl_pitch_search(const float *x_lp, float *y, int len, int max_pitch, int *pitch) {
   int i, j, lag, offset, best_pitch[2] = {0, 0};
   float x_lp4[DEC_PITCH_BUF_SIZE/4], y_lp4[DEC_PITCH_BUF_SIZE/2], xcorr[MAX_PERIOD];
   lag = len+max_pitch;
   for (j = 0; j < len>>2; j++) x_lp4[j] = x_lp[2*j];
   for (j = 0; j < lag>>2; j++) y_lp4[j] = y[2*j];
   fl_pitch_xcorr(x_lp4, y_lp4, xcorr, len>>2, max_pitch>>2);
   fl_find_best_pitch(xcorr, y_lp4, len>>2, max_pitch>>2, best_pitch);
   for (i = 0; i < max_pitch>>1; i++) {
      float sum;
      xcorr[i] = 0;
      if (abs(i-2*best_pitch[0]) > 2 && abs(i-2*best_pitch[1]) > 2) continue;
      sum = fl_inner_prod(x_lp, y+i, len>>1);
      xcorr[i] = FL_MAX(-1, sum);
   }
   fl_find_best_pitch(xcorr, y, len>>1, max_pitch>>1, best_pitch);
   if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch>>1)-1) {
      float a = xcorr[best_pitch[0]-1], b = xcorr[best_pitch[0]], c = xcorr[best_pitch[0]+1];
      if ((c-a) > .7f*(b-a)) offset = 1;
      else if ((a-c) > .7f*(b-c)) offset = -1;
      else offset = 0;
   } else offset = 0;
   *pitch = 2*best_pitch[0]-offset;
}
