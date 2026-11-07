/* ---- CELT encoder DSP with x87 rounding behaviour ---- */

/* kiss FFT butterflies: intermediates stay extended except where they are spilled. */
static void fl_e_bfly2(fl_cpx *Fout, int N) {
   const xf tw = 0.7071067812f;
   int i;
   for (i = 0; i < N; i++) {
      fl_cpx *F2 = Fout+4;
      xf tr, ti;
      float f2r = F2[0].r, f2i = F2[0].i;
      F2[0].r = (float)(XF(Fout[0].r)-f2r);
      F2[0].i = (float)(XF(Fout[0].i)-f2i);
      Fout[0].r = (float)(XF(Fout[0].r)+f2r);
      Fout[0].i = (float)(XF(Fout[0].i)+f2i);
      tr = (XF(F2[1].r)+F2[1].i)*tw;
      ti = (XF(F2[1].i)-F2[1].r)*tw;
      F2[1].r = (float)(XF(Fout[1].r)-tr);
      F2[1].i = (float)(XF(Fout[1].i)-ti);
      Fout[1].r = (float)(XF(Fout[1].r)+tr);
      Fout[1].i = (float)(XF(Fout[1].i)+ti);
      tr = F2[2].i;
      ti = -XF(F2[2].r);
      F2[2].r = (float)(XF(Fout[2].r)-tr);
      F2[2].i = (float)(XF(Fout[2].i)-ti);
      Fout[2].r = (float)(XF(Fout[2].r)+tr);
      Fout[2].i = (float)(XF(Fout[2].i)+ti);
      tr = (XF(F2[3].i)-F2[3].r)*tw;
      ti = -(XF(F2[3].i)+F2[3].r)*tw;
      F2[3].r = (float)(XF(Fout[3].r)-tr);
      F2[3].i = (float)(XF(Fout[3].i)-ti);
      Fout[3].r = (float)(XF(Fout[3].r)+tr);
      Fout[3].i = (float)(XF(Fout[3].i)+ti);
      Fout += 8;
   }
}
static void fl_e_bfly4(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i;
   if (m == 1) {
      for (i = 0; i < N; i++) {
         xf s0r = XF(Fout[0].r)-Fout[2].r, s0i = XF(Fout[0].i)-Fout[2].i;
         xf f0r = XF(Fout[0].r)+Fout[2].r, f0i = XF(Fout[0].i)+Fout[2].i;
         xf s1r = XF(Fout[1].r)+Fout[3].r, s1i = XF(Fout[1].i)+Fout[3].i;
         Fout[2].r = (float)(f0r-s1r);
         Fout[2].i = (float)(f0i-s1i);
         Fout[0].r = (float)(f0r+s1r);
         Fout[0].i = (float)(f0i+s1i);
         s1r = XF(Fout[1].r)-Fout[3].r;
         s1i = XF(Fout[1].i)-Fout[3].i;
         Fout[1].r = (float)(s0r+s1i);
         Fout[1].i = (float)(s0i-s1r);
         Fout[3].r = (float)(s0r-s1i);
         Fout[3].i = (float)(s0i+s1r);
         Fout += 4;
      }
   } else {
      int j, m2 = 2*m, m3 = 3*m;
      fl_cpx *Fout_beg = Fout;
      for (i = 0; i < N; i++) {
         const fl_cpx *tw1, *tw2, *tw3;
         Fout = Fout_beg+i*mm;
         tw3 = tw2 = tw1 = fl_fft_twiddles48000_960;
         for (j = 0; j < m; j++) {
            float s0r, s0i;
            xf s1r, s1i, s2r, s2i, s3r, s3i, s4r, s4i, s5r, s5i, f0r, f0i;
            s0r = (float)(XF(Fout[m].r)*tw1->r-XF(Fout[m].i)*tw1->i);
            s0i = (float)(XF(Fout[m].r)*tw1->i+XF(Fout[m].i)*tw1->r);
            s1r = XF(Fout[m2].r)*tw2->r-XF(Fout[m2].i)*tw2->i;
            s1i = XF(Fout[m2].r)*tw2->i+XF(Fout[m2].i)*tw2->r;
            s2r = XF(Fout[m3].r)*tw3->r-XF(Fout[m3].i)*tw3->i;
            s2i = XF(Fout[m3].r)*tw3->i+XF(Fout[m3].i)*tw3->r;
            s5r = XF(Fout->r)-s1r;
            s5i = XF(Fout->i)-s1i;
            f0r = XF(Fout->r)+s1r;
            f0i = XF(Fout->i)+s1i;
            Fout->r = (float)f0r;
            Fout->i = (float)f0i;
            s3r = s0r+s2r;
            s3i = s0i+s2i;
            s4r = s0r-s2r;
            s4i = s0i-s2i;
            Fout[m2].r = (float)(f0r-s3r);
            Fout[m2].i = (float)(f0i-s3i);
            tw1 += fstride;
            tw2 += fstride*2;
            tw3 += fstride*3;
            Fout->r = (float)(s3r+Fout->r);
            Fout->i = (float)(s3i+Fout->i);
            Fout[m].r = (float)(s5r+s4i);
            Fout[m].i = (float)(s5i-s4r);
            Fout[m3].r = (float)(s5r-s4i);
            Fout[m3].i = (float)(s5i+s4r);
            ++Fout;
         }
      }
   }
}
static void fl_e_bfly3(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   int i, k, m2 = 2*m;
   const fl_cpx *tw1, *tw2;
   fl_cpx *Fout_beg = Fout;
   float epi3i = fl_fft_twiddles48000_960[fstride*m].i;
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      tw1 = tw2 = fl_fft_twiddles48000_960;
      k = m;
      do {
         xf s1r, s1i, s2r, s2i, s3r, s3i, s0r, s0i;
         float fmr, fmi;
         s1r = XF(Fout[m].r)*tw1->r-XF(Fout[m].i)*tw1->i;
         s1i = XF(Fout[m].r)*tw1->i+XF(Fout[m].i)*tw1->r;
         s2r = XF(Fout[m2].r)*tw2->r-XF(Fout[m2].i)*tw2->i;
         s2i = XF(Fout[m2].r)*tw2->i+XF(Fout[m2].i)*tw2->r;
         s3r = s1r+s2r;
         s3i = s1i+s2i;
         s0r = s1r-s2r;
         s0i = s1i-s2i;
         tw1 += fstride;
         tw2 += fstride*2;
         fmr = (float)(XF(Fout->r)-XF(.5f)*s3r);
         fmi = (float)(XF(Fout->i)-XF(.5f)*s3i);
         Fout[m].r = fmr;
         Fout[m].i = fmi;
         s0r *= epi3i;
         s0i *= epi3i;
         Fout->r = (float)(s3r+Fout->r);
         Fout->i = (float)(s3i+Fout->i);
         Fout[m2].r = (float)(XF(fmr)+s0i);
         Fout[m2].i = (float)(XF(fmi)-s0r);
         Fout[m].r = (float)(XF(fmr)-s0i);
         Fout[m].i = (float)(XF(fmi)+s0r);
         ++Fout;
      } while (--k);
   }
}
static void fl_e_bfly5(fl_cpx *Fout, int fstride, int m, int N, int mm) {
   fl_cpx *F0, *F1, *F2, *F3, *F4, *Fout_beg = Fout;
   const fl_cpx *tw = fl_fft_twiddles48000_960;
   float yar = tw[fstride*m].r, yai = tw[fstride*m].i, ybr = tw[fstride*2*m].r, ybi = tw[fstride*2*m].i;
   int i, u;
   for (i = 0; i < N; i++) {
      Fout = Fout_beg+i*mm;
      F0 = Fout; F1 = F0+m; F2 = F0+2*m; F3 = F0+3*m; F4 = F0+4*m;
      for (u = 0; u < m; ++u) {
         float s0r = F0->r, s0i = F0->i, s1r, s1i, s7i_f, s9r, s9i, s10r, s10i;
         xf s2r, s2i, s3r, s3i, s4r, s4i, s7r, s7i, s8r, s8i, s5r, s5i, s6r, s6i, s11r, s11i, s12r, s12i;
         const fl_cpx *t1 = &tw[u*fstride], *t2 = &tw[2*u*fstride], *t3 = &tw[3*u*fstride], *t4 = &tw[4*u*fstride];
         s1r = (float)(XF(F1->r)*t1->r-XF(F1->i)*t1->i);
         s1i = (float)(XF(F1->r)*t1->i+XF(F1->i)*t1->r);
         s2r = XF(F2->r)*t2->r-XF(F2->i)*t2->i;
         s2i = XF(F2->r)*t2->i+XF(F2->i)*t2->r;
         s3r = XF(F3->r)*t3->r-XF(F3->i)*t3->i;
         s3i = XF(F3->r)*t3->i+XF(F3->i)*t3->r;
         s4r = XF(F4->r)*t4->r-XF(F4->i)*t4->i;
         s4i = XF(F4->r)*t4->i+XF(F4->i)*t4->r;
         s7r = s1r+s4r;
         s7i = s1i+s4i;
         s7i_f = (float)s7i;
         s10r = (float)(s1r-s4r);
         s10i = (float)(s1i-s4i);
         s8r = s2r+s3r;
         s8i = s2i+s3i;
         s9r = (float)(s2r-s3r);
         s9i = (float)(s2i-s3i);
         F0->r = (float)((s7r+s8r)+s0r);
         F0->i = (float)((s7i+s8i)+s0i);
         s5r = (yar*s7r+ybr*s8r)+s0r;
         s5i = (s7i*yar+ybr*s8i)+s0i;
         s6r = XF(yai)*s10i+XF(ybi)*s9i;
         s6i = -(XF(yai)*s10r+XF(ybi)*s9r);
         F1->r = (float)(s5r-s6r);
         F1->i = (float)(s5i-s6i);
         F4->r = (float)(s6r+s5r);
         F4->i = (float)(s5i+s6i);
         s11r = (s7r*ybr+s8r*yar)+s0r;
         s11i = (XF(s7i_f)*ybr+s8i*yar)+s0i;
         s12r = XF(s9i)*yai-XF(s10i)*ybi;
         s12i = XF(s10r)*ybi-XF(s9r)*yai;
         F2->r = (float)(s11r+s12r);
         F2->i = (float)(s11i+s12i);
         F3->r = (float)(s11r-s12r);
         F3->i = (float)(s11i-s12i);
         ++F0; ++F1; ++F2; ++F3; ++F4;
      }
   }
}
static void fl_e_fft_impl(const fl_fft_state *st, fl_cpx *fout) {
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
      case 2: fl_e_bfly2(fout, fstride[i]); break;
      case 4: fl_e_bfly4(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 3: fl_e_bfly3(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      case 5: fl_e_bfly5(fout, fstride[i]<<shift, m, fstride[i], m2); break;
      }
      m = m2;
   }
}
/* opus_fft_c: scaled bit-reversed copy then butterflies. */
static void fl_e_fft(const fl_fft_state *st, const fl_cpx *fin, fl_cpx *fout) {
   int i;
   for (i = 0; i < st->nfft; i++) {
      fout[st->bitrev[i]].r = (float)(XF(fin[i].r)*st->scale);
      fout[st->bitrev[i]].i = (float)(XF(fin[i].i)*st->scale);
   }
   fl_e_fft_impl(st, fout);
}
