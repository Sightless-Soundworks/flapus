/* Regression test for the FLAC bit reader (src/flac_dec.c).

   A unary code whose one bit is the last bit of a full 64-bit cache used to shift
   the cache by 64, which is undefined in C. On x86 the shift did nothing, the one
   bit stayed at the bottom of the cache, and the next fill ORed data over it. A
   real FLAC file (Rice parameter 3, quotients up to 76) then decoded wrongly from
   a few samples after such a code, and the MD5 check caught it.

   Build and run:  gcc -O2 -I.. -o bitreader bitreader.c && ./bitreader */
#define FLAPUS_NO_ENCODER
#define FLAPUS_NO_THREADS
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"

static int fails = 0;

static void expect(const char *what, long long got, long long want) {
   if (got != want) {
      printf("FAIL %s: got %lld, want %lld\n", what, got, want);
      fails++;
   }
}

int main(void) {
   /* 63 zeros and a one: the first fill holds exactly these 64 bits, so the one is
      the last bit of the cache. Then 64 zeros and a one: the next window has a zero
      in its last bit, which the stale one used to cover. Then a one bit (q = 0). */
   unsigned char buf[32];
   fl_br b;
   memset(buf, 0, sizeof(buf));
   buf[7] = 0x01;   /* bit 63 */
   buf[16] = 0x80;  /* bit 128: 64 zeros from bit 64 */
   buf[16] |= 0x40; /* bit 129 */
   fl_br_init(&b, buf, sizeof(buf));
   expect("63 zeros ending a full cache", fl_br_unary(&b), 63);
   expect("64 zeros after it", fl_br_unary(&b), 64);
   expect("then a one bit", fl_br_unary(&b), 0);
   expect("bit position", (long long)fl_br_tell(&b), 130);

   /* The same with a Rice value after the code, as in a residual: q = 63, k = 3,
      and the next one bit at bit 140. The stale bit sat at bit 127 and gave 60. */
   memset(buf, 0, sizeof(buf));
   buf[7] = 0x01;
   buf[8] = 0xA0;   /* 101 */
   buf[17] = 0x08;  /* bit 140 */
   fl_br_init(&b, buf, sizeof(buf));
   expect("rice quotient", fl_br_unary(&b), 63);
   expect("rice remainder", fl_br_bits(&b, 3), 5);
   expect("zeros up to bit 140", fl_br_unary(&b), 73);

   printf(fails ? "bitreader: %d failure(s)\n" : "bitreader: ok\n", fails);
   return fails != 0;
}
