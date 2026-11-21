/* ---- Portable math ----
   log, log10, exp, pow, sin, cos and acos from fdlibm (see LICENSE), in plain C double
   arithmetic, so that the results are the same on every platform and compiler. Errors
   are at most 1 ulp (log10: 2). sqrt, floor and fabs are exact everywhere (libm). */

static fl_u32 fl_hi(double x) {
   fl_u64 u;
   memcpy(&u, &x, 8);
   return (fl_u32)(u>>32);
}
static fl_u32 fl_lo(double x) {
   fl_u64 u;
   memcpy(&u, &x, 8);
   return (fl_u32)u;
}
static double fl_make(fl_u32 hi, fl_u32 lo) {
   fl_u64 u = (fl_u64)hi<<32|lo;
   double x;
   memcpy(&x, &u, 8);
   return x;
}
static double fl_with_hi(double x, fl_u32 hi) { return fl_make(hi, fl_lo(x)); }
#ifdef FLAPUS_HAVE_ENCODER
static double fl_with_lo(double x, fl_u32 lo) { return fl_make(fl_hi(x), lo); }

/* x*2^n, exact for normal results */
static double fl_scalbn(double x, int n) {
   while (n > 1023) {
      x *= 8.98846567431158e+307;  /* 2^1023 */
      n -= 1023;
   }
   while (n < -1022) {
      x *= 2.2250738585072014e-308;  /* 2^-1022 */
      n += 1022;
   }
   return x*fl_make((fl_u32)(n+1023)<<20, 0);
}
#endif

static const double fl_ln2_hi = 6.93147180369123816490e-01, fl_ln2_lo = 1.90821492927058770002e-10,
   fl_two54 = 1.80143985094819840000e+16,
   fl_Lg1 = 6.666666666666735130e-01, fl_Lg2 = 3.999999999940941908e-01, fl_Lg3 = 2.857142874366239149e-01,
   fl_Lg4 = 2.222219843214978396e-01, fl_Lg5 = 1.818357216161805012e-01, fl_Lg6 = 1.531383769920937332e-01,
   fl_Lg7 = 1.479819860511658591e-01;

/* fdlibm e_log.c */
static double fl_log(double x) {
   double hfsq, f, s, z, R, w, t1, t2, dk;
   fl_i32 k = 0, hx = (fl_i32)fl_hi(x), i, j;
   fl_u32 lx = fl_lo(x);
   if (hx < 0x00100000) {  /* x < 2^-1022 */
      if (((hx&0x7fffffff)|lx) == 0) return -fl_two54/0.0;  /* log(+-0) = -inf */
      if (hx < 0) return (x-x)/0.0;                          /* log(-#) = NaN */
      k -= 54;
      x *= fl_two54;
      hx = (fl_i32)fl_hi(x);
   }
   if (hx >= 0x7ff00000) return x+x;
   k += (hx>>20)-1023;
   hx &= 0x000fffff;
   i = (hx+0x95f64)&0x100000;
   x = fl_with_hi(x, (fl_u32)(hx|(i^0x3ff00000)));  /* normalize x or x/2 */
   k += i>>20;
   f = x-1.0;
   if ((0x000fffff&(2+hx)) < 3) {  /* |f| < 2^-20 */
      if (f == 0.0) {
         if (k == 0) return 0.0;
         dk = (double)k;
         return dk*fl_ln2_hi+dk*fl_ln2_lo;
      }
      R = f*f*(0.5-0.33333333333333333*f);
      if (k == 0) return f-R;
      dk = (double)k;
      return dk*fl_ln2_hi-((R-dk*fl_ln2_lo)-f);
   }
   s = f/(2.0+f);
   dk = (double)k;
   z = s*s;
   i = hx-0x6147a;
   w = z*z;
   j = 0x6b851-hx;
   t1 = w*(fl_Lg2+w*(fl_Lg4+w*fl_Lg6));
   t2 = z*(fl_Lg1+w*(fl_Lg3+w*(fl_Lg5+w*fl_Lg7)));
   i |= j;
   R = t2+t1;
   if (i > 0) {
      hfsq = 0.5*f*f;
      if (k == 0) return f-(hfsq-s*(hfsq+R));
      return dk*fl_ln2_hi-((hfsq-(s*(hfsq+R)+dk*fl_ln2_lo))-f);
   }
   if (k == 0) return f-s*(f-R);
   return dk*fl_ln2_hi-((s*(f-R)-dk*fl_ln2_lo)-f);
}

#ifdef FLAPUS_HAVE_ENCODER
/* fdlibm e_log10.c */
static double fl_log10(double x) {
   static const double ivln10 = 4.34294481903251816668e-01, log10_2hi = 3.01029995663611771306e-01,
      log10_2lo = 3.69423907715893078616e-13;
   double y, z;
   fl_i32 i, k = 0, hx = (fl_i32)fl_hi(x);
   fl_u32 lx = fl_lo(x);
   if (hx < 0x00100000) {
      if (((hx&0x7fffffff)|lx) == 0) return -fl_two54/0.0;
      if (hx < 0) return (x-x)/0.0;
      k -= 54;
      x *= fl_two54;
      hx = (fl_i32)fl_hi(x);
   }
   if (hx >= 0x7ff00000) return x+x;
   k += (hx>>20)-1023;
   i = (fl_i32)(((fl_u32)k&0x80000000)>>31);
   hx = (hx&0x000fffff)|((0x3ff-i)<<20);
   y = (double)(k+i);
   x = fl_with_hi(x, (fl_u32)hx);
   z = y*log10_2lo+ivln10*fl_log(x);
   return z+y*log10_2hi;
}
#endif

#ifdef FLAPUS_HAVE_ENCODER
/* fdlibm e_exp.c */
static double fl_exp(double x) {
   static const double halF[2] = {0.5, -0.5}, o_threshold = 7.09782712893383973096e+02,
      u_threshold = -7.45133219101941108420e+02, ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01},
      ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10}, invln2 = 1.44269504088896338700e+00,
      P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
      P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08, huge = 1.0e+300, twom1000 = 9.33263618503218878990e-302;
   double y, hi = 0, lo = 0, c, t;
   fl_i32 k = 0, xsb;
   fl_u32 hx = fl_hi(x);
   xsb = (fl_i32)((hx>>31)&1);
   hx &= 0x7fffffff;
   if (hx >= 0x40862E42) {  /* |x| >= 709.78 */
      if (hx >= 0x7ff00000) {
         if (((hx&0xfffff)|fl_lo(x)) != 0) return x+x;  /* NaN */
         return xsb == 0 ? x : 0.0;
      }
      if (x > o_threshold) return huge*huge;
      if (x < u_threshold) return twom1000*twom1000;
   }
   if (hx > 0x3fd62e42) {  /* |x| > 0.5 ln2 */
      if (hx < 0x3FF0A2B2) {  /* |x| < 1.5 ln2 */
         hi = x-ln2HI[xsb];
         lo = ln2LO[xsb];
         k = 1-xsb-xsb;
      } else {
         k = (fl_i32)(invln2*x+halF[xsb]);
         t = k;
         hi = x-t*ln2HI[0];
         lo = t*ln2LO[0];
      }
      x = hi-lo;
   } else if (hx < 0x3e300000) {  /* |x| < 2^-28 */
      if (huge+x > 1.0) return 1.0+x;
   } else k = 0;
   t = x*x;
   c = x-t*(P1+t*(P2+t*(P3+t*(P4+t*P5))));
   if (k == 0) return 1.0-((x*c)/(c-2.0)-x);
   y = 1.0-((lo-(x*c)/(2.0-c))-hi);
   return fl_scalbn(y, k);
}
#endif

#ifdef FLAPUS_HAVE_ENCODER
/* fdlibm e_pow.c */
static double fl_pow(double x, double y) {
   static const double bp[] = {1.0, 1.5}, dp_h[] = {0.0, 5.84962487220764160156e-01},
      dp_l[] = {0.0, 1.35003920212974897128e-08}, two53 = 9007199254740992.0, huge = 1.0e300, tiny = 1.0e-300,
      L1 = 5.99999999999994648725e-01, L2 = 4.28571428578550184252e-01, L3 = 3.33333329818377432918e-01,
      L4 = 2.72728123808534006489e-01, L5 = 2.30660745775561754067e-01, L6 = 2.06975017800338417784e-01,
      P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
      P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08, lg2 = 6.93147180559945286227e-01,
      lg2_h = 6.93147182464599609375e-01, lg2_l = -1.90465429995776804525e-09, ovt = 8.0085662595372944372e-17,
      cp = 9.61796693925975554329e-01, cp_h = 9.61796700954437255859e-01, cp_l = -7.02846165095275826516e-09,
      ivln2 = 1.44269504088896338700e+00, ivln2_h = 1.44269502162933349609e+00, ivln2_l = 1.92596299112661746887e-08;
   double z, ax, z_h, z_l, p_h, p_l, y1, t1, t2, r, s, t, u, v, w, ss, s2, s_h, s_l, t_h, t_l;
   fl_i32 i, j, k, yisint, n, hx, hy, ix, iy;
   fl_u32 lx, ly;
   hx = (fl_i32)fl_hi(x);
   lx = fl_lo(x);
   hy = (fl_i32)fl_hi(y);
   ly = fl_lo(y);
   ix = hx&0x7fffffff;
   iy = hy&0x7fffffff;
   if ((iy|ly) == 0) return 1.0;  /* y = 0 */
   if (hx == 0x3ff00000 && lx == 0) return 1.0;  /* x = 1 */
   if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0) || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0)) return x+y;
   /* yisint: 0 not an integer, 1 odd, 2 even (for x < 0) */
   yisint = 0;
   if (hx < 0) {
      if (iy >= 0x43400000) yisint = 2;
      else if (iy >= 0x3ff00000) {
         k = (iy>>20)-0x3ff;
         if (k > 20) {
            j = (fl_i32)(ly>>(52-k));
            if (((fl_u32)j<<(52-k)) == ly) yisint = 2-(j&1);
         } else if (ly == 0) {
            j = iy>>(20-k);
            if ((j<<(20-k)) == iy) yisint = 2-(j&1);
         }
      }
   }
   if (ly == 0) {
      if (iy == 0x7ff00000) {  /* y = +-inf */
         if (((ix-0x3ff00000)|lx) == 0) return 1.0;
         if (ix >= 0x3ff00000) return hy >= 0 ? y : 0.0;
         return hy < 0 ? -y : 0.0;
      }
      if (iy == 0x3ff00000) return hy < 0 ? 1.0/x : x;  /* y = +-1 */
      if (hy == 0x40000000) return x*x;                 /* y = 2 */
      if (hy == 0x3fe00000 && hx >= 0) return sqrt(x);  /* y = 0.5 */
   }
   ax = fabs(x);
   if (lx == 0 && (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000)) {  /* x = +-0, +-inf, +-1 */
      z = ax;
      if (hy < 0) z = 1.0/z;
      if (hx < 0) {
         if (((ix-0x3ff00000)|yisint) == 0) z = (z-z)/(z-z);
         else if (yisint == 1) z = -z;
      }
      return z;
   }
   n = (fl_i32)(((fl_u32)hx>>31)-1);
   if ((n|yisint) == 0) return (x-x)/(x-x);  /* (x < 0)^(non-int) */
   s = 1.0;
   if ((n|(yisint-1)) == 0) s = -1.0;  /* (-ve)^(odd int) */
   if (iy > 0x41e00000) {  /* |y| > 2^31 */
      if (iy > 0x43f00000) {
         if (ix <= 0x3fefffff) return hy < 0 ? huge*huge : tiny*tiny;
         if (ix >= 0x3ff00000) return hy > 0 ? huge*huge : tiny*tiny;
      }
      if (ix < 0x3fefffff) return hy < 0 ? s*huge*huge : s*tiny*tiny;
      if (ix > 0x3ff00000) return hy > 0 ? s*huge*huge : s*tiny*tiny;
      t = ax-1.0;
      w = (t*t)*(0.5-t*(0.3333333333333333333333-t*0.25));
      u = ivln2_h*t;
      v = t*ivln2_l-w*ivln2;
      t1 = fl_with_lo(u+v, 0);
      t2 = v-(t1-u);
   } else {
      double ss2;
      n = 0;
      if (ix < 0x00100000) {
         ax *= two53;
         n -= 53;
         ix = (fl_i32)fl_hi(ax);
      }
      n += ((ix)>>20)-0x3ff;
      j = ix&0x000fffff;
      ix = j|0x3ff00000;
      if (j <= 0x3988E) k = 0;
      else if (j < 0xBB67A) k = 1;
      else {
         k = 0;
         n += 1;
         ix -= 0x00100000;
      }
      ax = fl_with_hi(ax, (fl_u32)ix);
      u = ax-bp[k];
      v = 1.0/(ax+bp[k]);
      ss = u*v;
      s_h = fl_with_lo(ss, 0);
      t_h = fl_make((fl_u32)(((ix>>1)|0x20000000)+0x00080000+(k<<18)), 0);
      t_l = ax-(t_h-bp[k]);
      s_l = v*((u-s_h*t_h)-s_h*t_l);
      s2 = ss*ss;
      r = s2*s2*(L1+s2*(L2+s2*(L3+s2*(L4+s2*(L5+s2*L6)))));
      r += s_l*(s_h+ss);
      ss2 = s_h*s_h;
      t_h = fl_with_lo(3.0+ss2+r, 0);
      t_l = r-((t_h-3.0)-ss2);
      u = s_h*t_h;
      v = s_l*t_h+t_l*ss;
      p_h = fl_with_lo(u+v, 0);
      p_l = v-(p_h-u);
      z_h = cp_h*p_h;
      z_l = cp_l*p_h+p_l*cp+dp_l[k];
      t = (double)n;
      t1 = fl_with_lo(((z_h+z_l)+dp_h[k])+t, 0);
      t2 = z_l-(((t1-t)-dp_h[k])-z_h);
   }
   /* (y1+y2)*(t1+t2) */
   y1 = fl_with_lo(y, 0);
   p_l = (y-y1)*t1+y*t2;
   p_h = y1*t1;
   z = p_l+p_h;
   j = (fl_i32)fl_hi(z);
   i = (fl_i32)fl_lo(z);
   if (j >= 0x40900000) {  /* z >= 1024 */
      if (((j-0x40900000)|i) != 0 || p_l+ovt > z-p_h) return s*huge*huge;
   } else if ((j&0x7fffffff) >= 0x4090cc00) {  /* z <= -1075 */
      if (((j-(fl_i32)0xc090cc00)|i) != 0 || p_l <= z-p_h) return s*tiny*tiny;
   }
   i = j&0x7fffffff;
   k = (i>>20)-0x3ff;
   n = 0;
   if (i > 0x3fe00000) {
      n = j+(0x00100000>>(k+1));
      k = ((n&0x7fffffff)>>20)-0x3ff;
      t = fl_make((fl_u32)(n&~(0x000fffff>>k)), 0);
      n = ((n&0x000fffff)|0x00100000)>>(20-k);
      if (j < 0) n = -n;
      p_h -= t;
   }
   t = fl_with_lo(p_l+p_h, 0);
   u = t*lg2_h;
   v = (p_l-(t-p_h))*lg2+t*lg2_l;
   z = u+v;
   w = v-(z-u);
   t = z*z;
   t1 = z-t*(P1+t*(P2+t*(P3+t*(P4+t*P5))));
   r = (z*t1)/(t1-2.0)-(w+z*w);
   z = 1.0-(r-z);
   j = (fl_i32)fl_hi(z);
   j += (fl_i32)((fl_u32)n<<20);
   if ((j>>20) <= 0) z = fl_scalbn(z, n);
   else z = fl_with_hi(z, (fl_u32)j);
   return s*z;
}
#endif

/* fdlibm k_sin.c and k_cos.c: |x| <= pi/4 */
static double fl_sin_k(double x, double y, int iy) {
   static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
      S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08,
      S6 = 1.58969099521155010221e-10;
   double z = x*x, v = z*x, r = S2+z*(S3+z*(S4+z*(S5+z*S6)));
   if (iy == 0) return x+v*(S1+z*r);
   return x-((z*(0.5*y-v*r)-y)-v*S1);
}
static double fl_cos_k(double x, double y) {
   static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
      C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09,
      C6 = -1.13596475577881948265e-11;
   double z = x*x, r = z*(C1+z*(C2+z*(C3+z*(C4+z*(C5+z*C6))))), hz, a, qx;
   fl_i32 ix = (fl_i32)(fl_hi(x)&0x7fffffff);
   if (ix < 0x3FD33333) return 1.0-(0.5*z-(z*r-x*y));  /* |x| < 0.3 */
   qx = ix > 0x3fe90000 ? 0.28125 : fl_make((fl_u32)(ix-0x00200000), 0);  /* x/4 */
   hz = 0.5*z-qx;
   a = 1.0-qx;
   return a-(hz-(z*r-x*y));
}

/* Bits [pos, pos+n) of the little-endian 32-bit limbs p (n <= 64; bits below 0 are 0). */
static fl_u64 fl_bits(const fl_u32 *p, int pos, int n) {
   fl_u64 r = 0;
   int i;
   for (i = n-1; i >= 0; i--) {
      int b = pos+i;
      r = r<<1|(b >= 0 ? (p[b>>5]>>(b&31))&1 : 0);
   }
   return r;
}

/* Exact a*b as p+e (Dekker, no fused multiply-add). */
static void fl_two_prod(double a, double b, double *p, double *e) {
   double c = 134217729.0, t, ah, al, bh, bl;
   t = c*a;
   ah = t-(t-a);
   al = a-ah;
   t = c*b;
   bh = t-(t-b);
   bl = b-bh;
   *p = a*b;
   *e = ((ah*bh-*p)+ah*bl+al*bh)+al*bl;
}

/* fl_rem_pio2() for |x| > 2^20*pi/2: x*2/pi with 192 bits of 2/pi from where they matter
   (Payne-Hanek). */
static int fl_rem_pio2_large(double x, double *y) {
   static const fl_u32 two_over_pi[38] = {
      0xa2f9836e, 0x4e441529, 0xfc2757d1, 0xf534ddc0, 0xdb629599, 0x3c439041,
      0xfe5163ab, 0xdebbc561, 0xb7246e3a, 0x424dd2e0, 0x06492eea, 0x09d1921c,
      0xfe1deb1c, 0xb129a73e, 0xe88235f5, 0x2ebb4484, 0xe99c7026, 0xb45f7e41,
      0x3991d639, 0x835339f4, 0x9c845f8b, 0xbdf9283b, 0x1ff897ff, 0xde05980f,
      0xef2f118b, 0x5a0a6d1f, 0x6d367ecf, 0x27cb09b7, 0x4f463f66, 0x9e5fea2d,
      0x7527bac7, 0xebe5f17b, 0x3d0739f7, 0x8a5292ea, 0x6bfb5fb1, 0x1f8d5d08,
      0x56033046, 0xfc7b6bab};
   fl_u32 b[6], p[8], m[2];
   fl_u64 hi = (fl_hi(x)&0xfffff)|0x100000, top, next;
   double f, fl, ph, pe, t;
   int e = (int)((fl_hi(x)>>20)&0x7ff)-1075, k0 = e-1 > 1 ? e-1 : 1, s, i, j, n, L, neg = 0;
   m[0] = fl_lo(x);
   m[1] = (fl_u32)hi;
   /* b = bits k0..k0+191 of 2/pi (bit 1 is the first after the point) */
   for (i = 0; i < 6; i++) {
      fl_u32 w = 0;
      for (j = 0; j < 32; j++) {
         int k = k0+32*(5-i)+j;
         w = w<<1|((two_over_pi[(k-1)>>5]>>(31-((k-1)&31)))&1);
      }
      b[i] = w;
   }
   /* p = m*b; |x|*2/pi = p*2^-s, mod 4 */
   memset(p, 0, sizeof(p));
   for (i = 0; i < 2; i++) {
      fl_u64 c = 0;
      for (j = 0; j < 6; j++) {
         c += (fl_u64)m[i]*b[j]+p[i+j];
         p[i+j] = (fl_u32)c;
         c >>= 32;
      }
      p[i+6] = (fl_u32)c;
   }
   s = k0+191-e;
   n = (int)fl_bits(p, s, 2);
   if (fl_bits(p, s-1, 1)) {
      /* fraction >= 1/2: take it from the next multiple, 2^s minus the fraction */
      fl_u64 c = 1;
      for (i = 0; i < 8; i++) {
         c += (fl_u32)~p[i];
         p[i] = (fl_u32)c;
         c >>= 32;
      }
      n++;
      neg = 1;
   }
   for (L = s-1; L >= 0 && !fl_bits(p, L, 1); L--) {}
   if (L < 0) {
      y[0] = y[1] = 0;
   } else {
      top = fl_bits(p, L-63, 64);
      next = fl_bits(p, L-127, 64);
      f = ldexp((double)(top>>11), L-52-s);
      fl = ldexp((double)(top&0x7ff)+(double)next*5.421010862427522e-20, L-63-s);  /* next*2^-64 */
      fl_two_prod(f, 1.57079632679489655800e+00, &ph, &pe);
      t = pe+f*6.12323399573676603587e-17+fl*1.57079632679489655800e+00;
      y[0] = ph+t;
      y[1] = t-(y[0]-ph);
      if (neg) {
         y[0] = -y[0];
         y[1] = -y[1];
      }
   }
   if (x < 0) {
      y[0] = -y[0];
      y[1] = -y[1];
      return -n;
   }
   return n;
}

/* x minus the nearest multiple n of pi/2 as y[0]+y[1] (fdlibm e_rem_pio2.c). Returns n. */
static int fl_rem_pio2(double x, double *y) {
   static const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
      pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11,
      pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
      pio2_3t = 8.47842766036889956997e-32;
   double w, t, r, fn;
   fl_i32 i, j, n, ix = (fl_i32)(fl_hi(x)&0x7fffffff);
   if (ix > 0x413921fb) return fl_rem_pio2_large(x, y);
   t = fabs(x);
   n = (fl_i32)(t*invpio2+0.5);
   fn = (double)n;
   r = t-fn*pio2_1;
   w = fn*pio2_1t;
   j = ix>>20;
   y[0] = r-w;
   i = j-(fl_i32)((fl_hi(y[0])>>20)&0x7ff);
   if (i > 16) {  /* 2nd iteration needed */
      t = r;
      w = fn*pio2_2;
      r = t-w;
      w = fn*pio2_2t-((t-r)-w);
      y[0] = r-w;
      i = j-(fl_i32)((fl_hi(y[0])>>20)&0x7ff);
      if (i > 49) {  /* 3rd iteration */
         t = r;
         w = fn*pio2_3;
         r = t-w;
         w = fn*pio2_3t-((t-r)-w);
         y[0] = r-w;
      }
   }
   y[1] = (r-y[0])-w;
   if (x < 0) {
      y[0] = -y[0];
      y[1] = -y[1];
      return -n;
   }
   return n;
}

#ifdef FLAPUS_HAVE_ENCODER
static double fl_sin(double x) {
   double y[2];
   fl_i32 ix = (fl_i32)(fl_hi(x)&0x7fffffff);
   if (ix < 0x3e400000) return x;  /* |x| < 2^-27, also keeps -0 */
   if (ix <= 0x3fe921fb) return fl_sin_k(x, 0.0, 0);
   if (ix >= 0x7ff00000) return x-x;
   switch (fl_rem_pio2(x, y)&3) {
   case 0: return fl_sin_k(y[0], y[1], 1);
   case 1: return fl_cos_k(y[0], y[1]);
   case 2: return -fl_sin_k(y[0], y[1], 1);
   default: return -fl_cos_k(y[0], y[1]);
   }
}
#endif

static double fl_cos(double x) {
   double y[2];
   fl_i32 ix = (fl_i32)(fl_hi(x)&0x7fffffff);
   if (ix <= 0x3fe921fb) return fl_cos_k(x, 0.0);
   if (ix >= 0x7ff00000) return x-x;
   switch (fl_rem_pio2(x, y)&3) {
   case 0: return fl_cos_k(y[0], y[1]);
   case 1: return -fl_sin_k(y[0], y[1], 1);
   case 2: return -fl_cos_k(y[0], y[1]);
   default: return fl_sin_k(y[0], y[1], 1);
   }
}

#ifdef FLAPUS_HAVE_ENCODER
/* fdlibm e_acos.c */
static double fl_acos(double x) {
   static const double pi = 3.14159265358979311600e+00, pio2_hi = 1.57079632679489655800e+00,
      pio2_lo = 6.12323399573676603587e-17, pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01,
      pS2 = 2.01212532134862925881e-01, pS3 = -4.00555345006794114027e-02, pS4 = 7.91534994289814532176e-04,
      pS5 = 3.47933107596021167570e-05, qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00,
      qS3 = -6.88283971605453293030e-01, qS4 = 7.70381505559019352791e-02;
   double z, p, q, r, w, s, c, df;
   fl_i32 hx = (fl_i32)fl_hi(x), ix = hx&0x7fffffff;
   if (ix >= 0x3ff00000) {  /* |x| >= 1 */
      if (((ix-0x3ff00000)|fl_lo(x)) == 0) return hx > 0 ? 0.0 : pi+2.0*pio2_lo;
      return (x-x)/(x-x);
   }
   if (ix < 0x3fe00000) {  /* |x| < 0.5 */
      if (ix <= 0x3c600000) return pio2_hi+pio2_lo;
      z = x*x;
      p = z*(pS0+z*(pS1+z*(pS2+z*(pS3+z*(pS4+z*pS5)))));
      q = 1.0+z*(qS1+z*(qS2+z*(qS3+z*qS4)));
      r = p/q;
      return pio2_hi-(x-(pio2_lo-x*r));
   }
   if (hx < 0) {  /* x < -0.5 */
      z = (1.0+x)*0.5;
      p = z*(pS0+z*(pS1+z*(pS2+z*(pS3+z*(pS4+z*pS5)))));
      q = 1.0+z*(qS1+z*(qS2+z*(qS3+z*qS4)));
      s = sqrt(z);
      r = p/q;
      w = r*s-pio2_lo;
      return pi-2.0*(s+w);
   }
   z = (1.0-x)*0.5;  /* x > 0.5 */
   s = sqrt(z);
   df = fl_with_lo(s, 0);
   c = (z-df*df)/(s+df);
   p = z*(pS0+z*(pS1+z*(pS2+z*(pS3+z*(pS4+z*pS5)))));
   q = 1.0+z*(qS1+z*(qS2+z*(qS3+z*qS4)));
   r = p/q;
   w = r*s+c;
   return 2.0*(df+w);
}
#endif
