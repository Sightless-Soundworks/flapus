/* ---- x87 extended-precision helpers for the encoder ----
   The encoder computes with x87 extended precision: float/double values stay in
   80-bit registers until they are stored. Register values are long double (xf)
   and are rounded explicitly where they are stored to memory.
   The math functions below follow the MinGW (i686) libm code. */
typedef long double xf;
#define XF(v) ((xf)(v))

#if !(defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
#error "The Opus encoder needs GCC/Clang on x86 or x86-64 (80-bit long double and x87 inline asm)."
#endif

/* Sets the x87 control word to 64-bit precision, round to nearest (MinGW default). */
static unsigned short fl_x87_enter(void) {
   unsigned short old, cw = 0x037F;
   __asm__ volatile("fnstcw %0" : "=m"(old));
   __asm__ volatile("fldcw %0" : : "m"(cw));
   return old;
}
static void fl_x87_leave(unsigned short old) { __asm__ volatile("fldcw %0" : : "m"(old)); }

#ifdef FLAPUS_PORTABLE_MATH
/* ---- Portable math (FLAPUS_PORTABLE_MATH) ----
   The x87 transcendental instructions fsin, fcos, fyl2x, fyl2xp1, f2xm1 (and
   fpatan inside MinGW's acosl) are not exactly specified: their last bit is
   up to the CPU. Measured over 20000 fixed inputs, an Intel Core i7-13700K
   and an AMD EPYC 7763 disagreed for all five. (On a 3179-file speech corpus
   that changed no packet; rcpps/rsqrtps did, see enc_bands.c.)
   With FLAPUS_PORTABLE_MATH they are replaced by the functions below, which
   only use the operations IEEE 754 defines exactly (+, -, *, /) on long
   double, plus exact exponent manipulation. The output is then the same on
   every x86 CPU. Where the x87 result and ours round differently, a frame can
   take another decision than in the default build.
   Everything else (fsqrt, frndint, fscale, the stores) stays as it is. */

/* x = m * 2^e with m in [0.5, 1), exactly, through the 80-bit layout. x > 0. */
static xf fl_p_frexp(xf x, int *e) {
   unsigned char b[16];
   int ex, adj = 0;
   memcpy(b, &x, sizeof(xf));
   ex = (b[8] | (b[9] << 8)) & 0x7fff;
   if (ex == 0) {  /* subnormal: scale up by 2^64 first (exact) */
      x *= 18446744073709551616.0L;
      adj = 64;
      memcpy(b, &x, sizeof(xf));
      ex = (b[8] | (b[9] << 8)) & 0x7fff;
   }
   *e = ex-16382-adj;
   b[8] = 0xfe;
   b[9] = (unsigned char)((b[9] & 0x80) | 0x3f);
   memcpy(&x, b, sizeof(xf));
   return x;
}

/* 2^n exactly, |n| < 16382. */
static xf fl_p_pow2(int n) {
   unsigned char b[16];
   xf x = 1.0L;
   int ex = 16383+n;
   memcpy(b, &x, sizeof(xf));
   b[8] = (unsigned char)(ex & 0xff);
   b[9] = (unsigned char)((ex >> 8) & 0x7f);
   memcpy(&x, b, sizeof(xf));
   return x;
}

/* Nearest integer, halves away from zero; exact for |t| < 2^62. */
static long long fl_p_nint(xf t) { return (long long)(t < 0 ? t-0.5L : t+0.5L); }

#define FL_P_LN2     0.693147180559945309417232121458176568L
#define FL_P_LN2_HI  0x1.62e42fee00000p-1L       /* 32 bits: e*hi is exact */
#define FL_P_LN2_LO  0x1.a39ef35793c76p-33L
#define FL_P_LOG2E   1.442695040888963407359924681001892137L
#define FL_P_LOG10E  0.434294481903251827651128918916605082L
#define FL_P_PI      3.141592653589793238462643383279502884L
#define FL_P_2_PI    0.636619772367581343075535053490057448L
#define FL_P_PIO2_1  0x1.921fb544p0L             /* pi/2 in three parts, the first */
#define FL_P_PIO2_2  0x1.0b4611a6p-34L           /* two with 33 bits so k*part is */
#define FL_P_PIO2_3  2.02226624879595063154e-21L /* exact for the k used here */
#define FL_P_SQRT3   1.732050807568877293527446341505872367L

/* ln((1+s)/(1-s)) = 2*atanh(s) for |s| <= 0.172: odd series up to s^25. */
static xf fl_p_lnser(xf s) {
   xf z = s*s, p = 1.0L/25;
   int k;
   for (k = 23; k >= 3; k -= 2) p = p*z+1.0L/k;
   return 2*(s+s*z*p);
}
/* ln(x), x > 0: x = m*2^e with m in [sqrt(1/2), sqrt(2)), ln(m) from the series. */
static xf fl_p_ln_parts(xf x, int *e) {
   xf m = fl_p_frexp(x, e);
   if (m < 0.707106781186547524400844362104849039L) {
      m *= 2;
      (*e)--;
   }
   return fl_p_lnser((m-1)/(m+1));
}
static xf fl_p_ln(xf x) {
   int e;
   xf l = fl_p_ln_parts(x, &e);
   return XF(e)*FL_P_LN2_HI+(XF(e)*FL_P_LN2_LO+l);
}
static xf fl_p_log2(xf x) {
   int e;
   xf l = fl_p_ln_parts(x, &e);
   return XF(e)+l*FL_P_LOG2E;
}
/* ln(1+x) for |x| <= 0.29, as fyl2xp1 is used: (1+s)/(1-s) = 1+x. */
static xf fl_p_ln1p(xf x) { return fl_p_lnser(x/(2+x)); }

/* e^t-1 for |t| <= 0.7: t*(1+t/2*(1+t/3*(...(1+t/22)))). */
static xf fl_p_expm1(xf t) {
   xf p = 1;
   int k;
   for (k = 22; k >= 2; k--) p = 1+t*p/k;
   return t*p;
}
/* 2^x-1 for |x| <= 1, as f2xm1. */
static xf fl_p_f2xm1(xf x) { return fl_p_expm1(x*FL_P_LN2); }

/* sin and cos for |r| <= pi/4, series up to r^23 and r^22. */
static xf fl_p_sin_k(xf r) {
   xf z = r*r, p = 1;
   int k;
   for (k = 22; k >= 2; k -= 2) p = 1-z*p/(XF(k)*(k+1));
   return r*p;
}
static xf fl_p_cos_k(xf r) {
   xf z = r*r, p = 1;
   int k;
   for (k = 21; k >= 1; k -= 2) p = 1-z*p/(XF(k)*(k+1));
   return p;
}
/* Reduces x by the nearest multiple k of pi/2 (Cody-Waite); *q = k mod 4. */
static xf fl_p_reduce(xf x, int *q) {
   long long k = fl_p_nint(x*FL_P_2_PI);
   xf kf = XF(k);
   *q = (int)(k & 3);
   return ((x-kf*FL_P_PIO2_1)-kf*FL_P_PIO2_2)-kf*FL_P_PIO2_3;
}
static xf fl_p_sin(xf x) {
   int q;
   xf r = fl_p_reduce(x, &q);
   return q == 0 ? fl_p_sin_k(r) : q == 1 ? fl_p_cos_k(r) : q == 2 ? -fl_p_sin_k(r) : -fl_p_cos_k(r);
}
static xf fl_p_cos(xf x) {
   int q;
   xf r = fl_p_reduce(x, &q);
   return q == 0 ? fl_p_cos_k(r) : q == 1 ? -fl_p_sin_k(r) : q == 2 ? -fl_p_cos_k(r) : fl_p_sin_k(r);
}

/* atan: a > 1 is folded with pi/2-atan(1/a), a > tan(pi/12) with
   pi/6+atan((a*sqrt3-1)/(a+sqrt3)); then the series up to a^37. */
static xf fl_p_atan(xf a) {
   int neg = a < 0, inv = 0, k;
   xf off = 0, z, s = 0;
   if (neg) a = -a;
   if (a > 1) {
      a = 1/a;
      inv = 1;
   }
   if (a > 0.267949192431122706472553658494127633L) {
      a = (a*FL_P_SQRT3-1)/(a+FL_P_SQRT3);
      off = FL_P_PI/6;
   }
   z = a*a;
   for (k = 37; k >= 1; k -= 2) s = 1.0L/k-z*s;
   s = a*s+off;
   if (inv) s = FL_P_PI/2-s;
   return neg ? -s : s;
}
/* acos(x) = 2*atan(sqrt((1-x)/(1+x))); fsqrt is exact in IEEE terms. */
static xf fl_p_acos(xf x) {
   xf v;
   if (x <= -1) return FL_P_PI;
   if (x >= 1) return 0;
   v = (1-x)/(1+x);
   __asm__("fsqrt" : "+t"(v));
   return 2*fl_p_atan(v);
}
#endif /* FLAPUS_PORTABLE_MATH */

static xf fl_x87_sqrt(xf x) {
   __asm__("fsqrt" : "+t"(x));
   return x;
}
/* MinGW sqrt(double): fsqrt of the double argument, unrounded result. */
static xf fl_m_sqrt(double d) {
   if (d == 1.0) return 1.0L;
   return fl_x87_sqrt(XF(d));
}
/* MinGW sqrtf(float): fsqrt of the float argument, unrounded result. */
static xf fl_m_sqrtf(float f) {
   if (f == 1.0f) return 1.0L;
   return fl_x87_sqrt(XF(f));
}
static xf fl_m_floorf(float f) { return floorl(XF(f)); }

#ifdef FLAPUS_PORTABLE_MATH
static double fl_m_sin(double d) { return (double)fl_p_sin(XF(d)); }
static double fl_m_cos(double d) { return (double)fl_p_cos(XF(d)); }
#else
static xf fl_x87_fsin(xf x) {
   __asm__("fsin" : "+t"(x));
   return x;
}
static xf fl_x87_fcos(xf x) {
   __asm__("fcos" : "+t"(x));
   return x;
}
/* MinGW sin/cos(double): fsin/fcos rounded to double (arguments are small here). */
static double fl_m_sin(double d) { return (double)fl_x87_fsin(XF(d)); }
static double fl_m_cos(double d) { return (double)fl_x87_fcos(XF(d)); }
#endif

/* MinGW log(double) (__logl_internal): fyl2xp1 near 1, fyl2x otherwise; rounded to double. */
static double fl_m_log(double d) {
   xf x = XF(d), r;
   xf xm1 = x-1.0L;
#ifdef FLAPUS_PORTABLE_MATH
   r = fabsl(xm1) > XF(0.29) ? fl_p_ln(x) : fl_p_ln1p(xm1);
#else
   if (fabsl(xm1) > XF(0.29)) {
      __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   } else {
      __asm__("fldln2\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"(xm1) : "st(1)");
   }
#endif
   return (double)r;
}

#ifdef FLAPUS_PORTABLE_MATH
/* exp(d) = 2^n * e^r with n = nint(d*log2(e)), r = d-n*ln2 (Cody-Waite). */
static double fl_m_exp(double d) {
   xf x = XF(d), r;
   long long n;
   if (d > 709.782712893384 || d < -745.1332191019411) return d > 0 ? HUGE_VAL : 0.0;
   if (d == 0) return 1.0;
   n = fl_p_nint(x*FL_P_LOG2E);
   r = (x-XF(n)*FL_P_LN2_HI)-XF(n)*FL_P_LN2_LO;
   return (double)((1+fl_p_expm1(r))*fl_p_pow2((int)n));
}
#else
/* MinGW exp(double): transcription of the i686 code (normal range only). */
static double fl_m_exp(double d) {
   static const unsigned char c1b[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xb8, 0xff, 0x3f};
   static const unsigned char c2b[10] = {0x20, 0xfa, 0xee, 0xc2, 0x5f, 0x70, 0xa5, 0xec, 0xed, 0x3f};
   xf x = XF(d), r, c1, c2;
   unsigned short cw0, cw1;
   if (d > 709.782712893384 || d < -745.1332191019411) return d > 0 ? HUGE_VAL : 0.0;
   if (d == 0) return 1.0;
   memcpy(&c1, c1b, 10);
   memcpy(&c2, c2b, 10);
   __asm__(
      "fldl2e\n\t"
      "fmul %%st(1),%%st\n\t"
      "fnstcw %2\n\t"
      "movzwl %2,%%eax\n\t"
      "or $0xc,%%ah\n\t"
      "mov %%ax,%3\n\t"
      "fldcw %3\n\t"
      "frndint\n\t"
      "fld %%st(1)\n\t"
      "frndint\n\t"
      "fldcw %2\n\t"
      "fld %%st(1)\n\t"
      "fldt %4\n\t"
      "fld %%st(2)\n\t"
      "fmul %%st(1),%%st\n\t"
      "fsubp %%st,%%st(2)\n\t"
      "fld %%st(4)\n\t"
      "fsub %%st(3),%%st\n\t"
      "fmulp %%st,%%st(1)\n\t"
      "faddp %%st,%%st(1)\n\t"
      "fldt %5\n\t"
      "fmul %%st(4),%%st\n\t"
      "faddp %%st,%%st(1)\n\t"
      "f2xm1\n\t"
      "fld1\n\t"
      "faddp %%st,%%st(1)\n\t"
      "fstp %%st(1)\n\t"
      "fscale\n\t"
      "fstp %%st(1)\n\t"
      "fstp %%st(1)\n\t"
      : "=t"(r) : "0"(x), "m"(cw0), "m"(cw1), "m"(c1), "m"(c2)
      : "eax", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)");
   return (double)r;
}
#endif

/* msvcrt log10/acos take the SSE2 path (near correctly rounded doubles). */
#ifdef FLAPUS_PORTABLE_MATH
static double fl_m_log10(double d) { return (double)(fl_p_ln(XF(d))*FL_P_LOG10E); }
static double fl_m_acos(double d) { return (double)fl_p_acos(XF(d)); }
#else
static double fl_m_log10(double d) {
   xf r, x = XF(d);
   __asm__("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   return (double)r;
}
static double fl_m_acos(double d) {
   return (double)acosl(XF(d));
}
#endif
