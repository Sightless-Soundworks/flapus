/* Sanity test for the rcpps/rsqrtps tables (src/enc_rcp_tables.c) behind
   FLAPUS_PORTABLE_MATH.

   tools/gen_rcp.c builds the tables on an Intel CPU and checks them against the
   hardware for all 2^32 inputs; that cannot be repeated on another CPU. This test runs
   anywhere: every table entry stands for a block of inputs, and at both ends of every
   block the emulated result must be within Intel's documented bound of the exact
   value (relative error <= 1.5*2^-12). It also checks a few values and the special
   cases.

   Build and run:  gcc -O2 -msse2 -I.. -o rcp_tables rcp_tables.c && ./rcp_tables */
#define FLAPUS_PORTABLE_MATH
#define FLAPUS_NO_THREADS
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"

static int fails = 0;

static float f_of(unsigned u) { float f; memcpy(&f, &u, 4); return f; }

static void bound(const char *what, unsigned x, unsigned r, double exact) {
   double err = fabs((double)f_of(r)-exact)/exact;
   if (!(err <= 1.5/4096)) {
      if (fails < 10) printf("FAIL %s %08x: %08x, relative error %.3g\n", what, x, r, err);
      fails++;
   }
}

static void same(const char *what, unsigned got, unsigned want) {
   if (got != want) {
      printf("FAIL %s: %08x, want %08x\n", what, got, want);
      fails++;
   }
}

int main(void) {
   unsigned i, e;
   for (i = 0; i < 2048; i++) {
      /* rcp: block i covers mantissas i<<12 .. i<<12|0xfff; try several exponents */
      for (e = 1; e < 253; e += 31) {
         unsigned lo = e << 23 | i << 12, hi = lo | 0xfff;
         bound("rcp", lo, fl_p_rcp_bits(lo), 1.0/f_of(lo));
         bound("rcp", hi, fl_p_rcp_bits(hi), 1.0/f_of(hi));
      }
      /* rsqrt: block i covers exponent parity i>>10, mantissas (i&0x3ff)<<13 .. |0x1fff */
      for (e = 1+(i >> 10); e < 254; e += 32) {
         unsigned lo = e << 23 | (i & 0x3ff) << 13, hi = lo | 0x1fff;
         if (((e-127) & 1) != (i >> 10)) continue;
         bound("rsqrt", lo, fl_p_rsqrt_bits(lo), 1.0/sqrt(f_of(lo)));
         bound("rsqrt", hi, fl_p_rsqrt_bits(hi), 1.0/sqrt(f_of(hi)));
      }
   }
   /* Values from an Intel Core i7-13700K, and the special cases. */
   same("rcp(1)", fl_p_rcp_bits(0x3f800000u), 0x3f7ff000u);
   same("rcp(-2)", fl_p_rcp_bits(0xc0000000u), 0xbefff000u);
   same("rcp(0)", fl_p_rcp_bits(0x00000000u), 0x7f800000u);
   same("rcp(-0)", fl_p_rcp_bits(0x80000000u), 0xff800000u);
   same("rcp(inf)", fl_p_rcp_bits(0x7f800000u), 0x00000000u);
   same("rsqrt(1)", fl_p_rsqrt_bits(0x3f800000u), 0x3f7ff000u);
   same("rsqrt(-1)", fl_p_rsqrt_bits(0xbf800000u), 0xffc00000u);
   same("rsqrt(0)", fl_p_rsqrt_bits(0x00000000u), 0x7f800000u);
   same("rsqrt(inf)", fl_p_rsqrt_bits(0x7f800000u), 0x00000000u);
   printf(fails ? "rcp_tables: %d failure(s)\n" : "rcp_tables: ok\n", fails);
   return fails != 0;
}
