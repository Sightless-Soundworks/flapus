/* Accuracy test for FLAPUS_PORTABLE_MATH (src/enc_x87.c).

   The portable functions replace the x87 transcendental instructions, whose last bit
   differs between CPU makers. They only use exactly specified operations, so they give
   the same result everywhere; this checks that the result is also close to the true
   value. The references are the exact values rounded to the 64-bit mantissa, computed
   with 60-digit decimal arithmetic. Limit: 3 ulp (measured worst case over 4000 random
   inputs per function: 2.36 ulp, for 2^x-1).

   Build and run:  gcc -O2 -I.. -o portable_math portable_math.c && ./portable_math */
#define FLAPUS_PORTABLE_MATH
#define FLAPUS_NO_THREADS
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"

static const struct { const char *f; long double x, want; } cases[] = {
   { "ln",  0.5L,            -0xb17217f7d1cf79acp-64L },
   { "ln",  3L,              0x8c9f53d5681854bbp-63L },
   { "ln",  100.25L,         0x9372023404190040p-61L },
   { "ln",  0.000123L,       -0x900d9fc6b5a15c34p-60L },
   { "ln1p", 0.25L,           0xe47fbe3cd4d10d61p-66L },
   { "ln1p", -0.2L,           -0xe47fbe3cd4d10d61p-66L },
   { "ln1p", 0.0001220703125L, 0xfffc001554d55888p-77L },
   { "f2xm1", 0.5L,            0xd413cccfe7799211p-65L },
   { "f2xm1", -0.75L,          -0xcf901f5ce48ead21p-65L },
   { "f2xm1", 0.0009765625L,   0xb18178ba33b141b5p-74L },
   { "sin", 0.5L,            0xf57743a2582f7f44p-65L },
   { "sin", 3L,              0x9081c36db6aada79p-66L },
   { "sin", -123.5L,         0xd44e337eed47bb6dp-64L },
   { "sin", 399.75L,         -0xb1cb243a10d86a80p-64L },
   { "cos", 0.25L,           0xf80aa4fbef750ba8p-64L },
   { "cos", 1.5L,            0x90deaa7e2fcd3a20p-67L },
   { "cos", 1.5703125L,      0xfdaa217082548988p-75L },
   { "acos", 0.5L,            0x860a91c16b9b2c23p-63L },
   { "acos", -0.875L,         0xa8b807060a5927f8p-62L },
   { "acos", 0.999755859375L, 0xb505e4934dc315a5p-69L },
};

static long double eval(const char *f, long double x) {
   if (!strcmp(f, "ln")) return fl_p_ln(x);
   if (!strcmp(f, "ln1p")) return fl_p_ln1p(x);
   if (!strcmp(f, "f2xm1")) return fl_p_f2xm1(x);
   if (!strcmp(f, "sin")) return fl_p_sin(x);
   if (!strcmp(f, "cos")) return fl_p_cos(x);
   return fl_p_acos(x);
}

int main(void) {
   unsigned short cw = fl_x87_enter();
   int i, fails = 0;
   for (i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
      int e;
      long double got = eval(cases[i].f, cases[i].x);
      long double ulp = ldexpl(1.0L, (frexpl(cases[i].want, &e), e-64));
      long double err = fabsl(got-cases[i].want)/ulp;
      if (!(err <= 3)) {
         printf("FAIL %s(%Lg): %.2Lf ulp\n", cases[i].f, cases[i].x, err);
         fails++;
      }
   }
   fl_x87_leave(cw);
   printf(fails ? "portable_math: %d failure(s)\n" : "portable_math: ok\n", fails);
   return fails != 0;
}
