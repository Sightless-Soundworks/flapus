/* Accuracy test for the portable math functions (src/math.c).

   The Opus encoder and the FLAC encoder use these instead of libm, so that the output
   is the same on every platform. Each function is compared with libm's long double
   version over a fixed set of arguments: at most 1 ulp (log10: 2) where long double is
   wider than double, 1 ulp more where it is not (libm's own error adds up). Special
   values must come out as in C99 Annex F.

   Build and run:  gcc -O2 -ffp-contract=off -I.. -o math math.c -lm && ./math */
#define FLAPUS_NO_THREADS
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"
#include <float.h>

static int fails = 0;
static const int wide = LDBL_MANT_DIG > DBL_MANT_DIG;

/* Error of got in ulps of the double nearest to ref. */
static double ulps(double got, long double ref) {
   double r = (double)ref;
   int e;
   if (r == 0) return got == 0 ? 0 : 1e9;
   frexp(r, &e);
   return (double)fabsl((long double)got-ref)/ldexp(1.0, (e < -1021 ? -1021 : e)-53);
}

static void check(const char *name, double x, double y, double got, long double ref, double limit) {
   double u = ulps(got, ref);
   if (!(u <= limit+!wide)) {
      printf("FAIL %s(%.17g%s%.17g): %.17g, want %.17Lg (%.2f ulp)\n", name, x, y == y ? ", " : "",
             y == y ? y : 0.0, got, ref, u);
      fails++;
   }
}

static void special(const char *what, double got, double want) {
   int ok = want != want ? got != got : got == want && signbit(got) == signbit(want);
   if (!ok) {
      printf("FAIL %s: %.17g, want %.17g\n", what, got, want);
      fails++;
   }
}

int main(void) {
   fl_u32 seed = 1;
   double nan = NAN, x, y;
   int i;
   for (i = 0; i < 200000; i++) {
      /* arguments spread over many binades, plus the ranges the encoders use */
      seed = seed*1664525u+1013904223u;
      x = ldexp(1.0+(seed>>8)/16777216.0, (int)(seed%61)-30);
      check("log", x, nan, fl_log(x), logl(x), 1);
      check("log10", x, nan, fl_log10(x), log10l(x), 2);
      check("cos", x, nan, fl_cos(x), cosl(x), 1);
      check("sin", -x, nan, fl_sin(-x), sinl(-x), 1);
      y = (seed%2001)/1000.0-1;
      check("acos", y, nan, fl_acos(y), acosl(y), 1);
      y = (seed%140001)/100.0-700;
      check("exp", y, nan, fl_exp(y), expl(y), 1);
      check("pow", x, y/50, fl_pow(x, y/50), powl(x, y/50), 1);
      check("pow", 10.0, y/20, fl_pow(10.0, y/20), powl(10.0L, y/20), 1);
   }
   /* the double closest to a multiple of pi/2 (6381956970095103*2^797), and large ones */
   x = ldexp(6381956970095103.0, 797);
   check("sin", x, nan, fl_sin(x), sinl(x), 1);
   check("cos", x, nan, fl_cos(x), cosl(x), 1);
   for (i = 0; i < 2000; i++) {
      seed = seed*1664525u+1013904223u;
      x = ldexp(1.0+(seed>>8)/16777216.0, (int)(seed%1000)+20);
      check("sin", x, nan, fl_sin(x), sinl(x), 1);
      check("cos", -x, nan, fl_cos(-x), cosl(-x), 1);
   }
   special("log(0)", fl_log(0), -INFINITY);
   special("log(-1)", fl_log(-1), nan);
   special("log(1)", fl_log(1), 0);
   special("log10(1)", fl_log10(1), 0);
   special("exp(-inf)", fl_exp(-INFINITY), 0);
   special("exp(1000)", fl_exp(1000), INFINITY);
   special("exp(-1000)", fl_exp(-1000), 0);
   special("pow(0,0)", fl_pow(0, 0), 1);
   special("pow(-8,1/3)", fl_pow(-8, 1/3.), nan);
   special("pow(-2,3)", fl_pow(-2, 3), -8);
   special("pow(2,-1074)", fl_pow(2, -1074), 4.9406564584124654e-324);
   special("sin(-0)", fl_sin(-0.0), -0.0);
   special("cos(0)", fl_cos(0), 1);
   special("sin(inf)", fl_sin(INFINITY), nan);
   special("acos(1)", fl_acos(1), 0);
   special("acos(2)", fl_acos(2), nan);
   special("sin(1e22)", fl_sin(1e22), -0.8522008497671888);
   printf(fails ? "%d failures\n" : "math: ok\n", fails);
   return fails != 0;
}
