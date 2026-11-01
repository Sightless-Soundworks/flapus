/* ---- x87 extended-precision helpers for the encoder ----
   The reference encoder is a 32-bit x87 build: float/double values stay in
   80-bit registers until they are stored. We model register values with
   long double (xf) and round explicitly where the reference stores to memory.
   The math functions below reproduce the MinGW (i686) libm code exactly. */
typedef long double xf;
#define XF(v) ((xf)(v))

#if !(defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
#error "The bit-exact encoder needs GCC/Clang on x86 or x86-64 (80-bit long double and x87 inline asm)."
#endif

/* Sets the x87 control word to 64-bit precision, round to nearest (MinGW default). */
static unsigned short fl_x87_enter(void) {
   unsigned short old, cw = 0x037F;
   __asm__ volatile("fnstcw %0" : "=m"(old));
   __asm__ volatile("fldcw %0" : : "m"(cw));
   return old;
}
static void fl_x87_leave(unsigned short old) { __asm__ volatile("fldcw %0" : : "m"(old)); }

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
static xf fl_m_floor(double d) { return floorl(XF(d)); }
static xf fl_m_floorf(float f) { return floorl(XF(f)); }

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

/* MinGW log(double) (__logl_internal): fyl2xp1 near 1, fyl2x otherwise; rounded to double. */
static double fl_m_log(double d) {
   xf x = XF(d), r;
   xf xm1 = x-1.0L;
   if (fabsl(xm1) > XF(0.29)) {
      __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   } else {
      __asm__("fldln2\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"(xm1) : "st(1)");
   }
   return (double)r;
}

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

/* msvcrt log10/acos take the SSE2 path (near correctly rounded doubles). */
static double fl_m_log10(double d) {
   xf r, x = XF(d);
   __asm__("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)");
   return (double)r;
}
static double fl_m_acos(double d) {
   return (double)acosl(XF(d));
}

/* SSE float->int conversions used by the reference (_mm_cvt_ss2si / cvttss2si). */
static int fl_cvt_ss2si(float f) { return (int)lrintf(f); }
static int fl_cvtt_ss2si(float f) { return (int)f; }
