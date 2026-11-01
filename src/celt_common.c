/* ---- CELT: mode, bit allocation, Laplace and PVQ coding (integer parts) ---- */
#define CELT_NB_EBANDS 21
#define CELT_OVERLAP 120
#define CELT_SHORT_MDCT 120
#define CELT_MAX_LM 3
#define MAX_PERIOD 1024
#define DEC_PITCH_BUF_SIZE 2048
#define COMBFILTER_MINPERIOD 15
#define CELT_LPC_ORDER 24
#define SPREAD_NONE 0
#define SPREAD_LIGHT 1
#define SPREAD_NORMAL 2
#define SPREAD_AGGRESSIVE 3
#define MAX_FINE_BITS 8
#define FINE_OFFSET 21
#define QTHETA_OFFSET 4
#define QTHETA_OFFSET_TWOPHASE 16
#define LOG_MAX_PSEUDO 6
#define ALLOC_STEPS 6

static const unsigned char fl_trim_icdf[11] = {126, 124, 119, 109, 87, 41, 19, 9, 4, 2, 0};
static const unsigned char fl_spread_icdf[4] = {25, 23, 2, 0};
static const unsigned char fl_tapset_icdf[3] = {2, 1, 0};
static const signed char fl_tf_select_table[4][8] = {
   {0, -1, 0, -1, 0,-1, 0,-1}, {0, -1, 0, -2, 1, 0, 1,-1},
   {0, -2, 0, -3, 2, 0, 1,-1}, {0, -2, 0, -3, 3, 0, 1,-1}};
static const float fl_preemph[4] = {0.85000610f, 0.0f, 1.0f, 1.0f};

typedef struct {
   int nfft;
   float scale;
   int shift;
   fl_i16 factors[16];
   const fl_i16 *bitrev;
} fl_fft_state;

static const fl_fft_state fl_fft_states[4] = {
   {480, 0.0020833334f, -1, {5, 96, 3, 32, 4, 8, 2, 4, 4, 1, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev480},
   {240, 0.0041666669f, 1, {5, 48, 3, 16, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev240},
   {120, 0.0083333338f, 2, {5, 24, 3, 8, 2, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev120},
   {60, 0.016666668f, 3, {5, 12, 3, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, fl_fft_bitrev60}};

static const fl_u32 *const fl_pvq_u_row[15] = {
   fl_pvq_u_data+0, fl_pvq_u_data+176, fl_pvq_u_data+351, fl_pvq_u_data+525, fl_pvq_u_data+698,
   fl_pvq_u_data+870, fl_pvq_u_data+1041, fl_pvq_u_data+1131, fl_pvq_u_data+1178, fl_pvq_u_data+1207,
   fl_pvq_u_data+1226, fl_pvq_u_data+1240, fl_pvq_u_data+1248, fl_pvq_u_data+1254, fl_pvq_u_data+1257};
#define CELT_PVQ_U(n,k) (fl_pvq_u_row[IMIN(n,k)][IMAX(n,k)])
#define CELT_PVQ_V(n,k) (CELT_PVQ_U(n,k)+CELT_PVQ_U(n,(k)+1))

/* FLOAT_APPROX versions of log2/exp2 (libopus default on x86). */
static const float fl_log2_x_norm_coeff[8] = {
   1.000000000000000000000000000f, 8.88888895511627197265625e-01f,
   8.00000000000000000000000e-01f, 7.27272748947143554687500e-01f,
   6.66666686534881591796875e-01f, 6.15384638309478759765625e-01f,
   5.71428596973419189453125e-01f, 5.33333361148834228515625e-01f};
static const float fl_log2_y_norm_coeff[8] = {
   0.0000000000000000000000000000f, 1.699250042438507080078125e-01f,
   3.219280838966369628906250e-01f, 4.594316184520721435546875e-01f,
   5.849624872207641601562500e-01f, 7.004396915435791015625000e-01f,
   8.073549270629882812500000e-01f, 9.068905711174011230468750e-01f};
static float fl_celt_log2(float x) {
   fl_i32 integer, range_idx;
   union { float f; fl_u32 i; } in;
   in.f = x;
   integer = (fl_i32)(in.i>>23)-127;
   in.i = (fl_i32)in.i-(fl_i32)((fl_u32)integer<<23);
   range_idx = (in.i>>20)&0x7;
   in.f = in.f*fl_log2_x_norm_coeff[range_idx]-1.0625f;
   in.f = 8.74628424644470214843750000e-02f+in.f*(1.357829570770263671875000000000f
         +in.f*(-6.3897705078125000000000000e-01f
         +in.f*(4.01971250772476196289062500e-01f
         +in.f*(-2.8415444493293762207031250e-01f))));
   return integer+in.f+fl_log2_y_norm_coeff[range_idx];
}
static float fl_celt_exp2(float x) {
   fl_i32 integer;
   float frac;
   union { float f; fl_u32 i; } res;
   integer = (int)floor(x);
   if (integer < -50) return 0;
   frac = x-integer;
   res.f = 9.999999403953552246093750000000e-01f+frac*(6.931530833244323730468750000000e-01f
         +frac*(2.401536107063293457031250000000e-01f
         +frac*(5.582631751894950866699218750000e-02f
         +frac*(8.989339694380760192871093750000e-03f
         +frac*(1.877576694823801517486572265625e-03f)))));
   res.i = (fl_u32)((fl_i32)res.i+(fl_i32)((fl_u32)integer<<23))&0x7fffffff;
   return res.f;
}

static fl_u32 fl_lcg_rand(fl_u32 seed) { return 1664525*seed+1013904223; }

static void fl_init_caps(int *cap, int LM, int C) {
   int i;
   for (i = 0; i < CELT_NB_EBANDS; i++) {
      int N = (fl_eband5ms[i+1]-fl_eband5ms[i])<<LM;
      cap[i] = (fl_cache_caps50[CELT_NB_EBANDS*(2*LM+C-1)+i]+64)*C*N>>2;
   }
}

static int fl_get_pulses(int i) { return i < 8 ? i : (8+(i&7))<<((i>>3)-1); }

static int fl_bits2pulses(int band, int LM, int bits) {
   int i, lo, hi;
   const unsigned char *cache;
   LM++;
   cache = fl_cache_bits50+fl_cache_index50[LM*CELT_NB_EBANDS+band];
   lo = 0;
   hi = cache[0];
   bits--;
   for (i = 0; i < LOG_MAX_PSEUDO; i++) {
      int mid = (lo+hi+1)>>1;
      if ((int)cache[mid] >= bits) hi = mid;
      else lo = mid;
   }
   if (bits-(lo == 0 ? -1 : (int)cache[lo]) <= (int)cache[hi]-bits) return lo;
   return hi;
}

static int fl_pulses2bits(int band, int LM, int pulses) {
   const unsigned char *cache;
   LM++;
   cache = fl_cache_bits50+fl_cache_index50[LM*CELT_NB_EBANDS+band];
   return pulses == 0 ? 0 : cache[pulses]+1;
}

static int fl_interp_bits2pulses(int start, int end, int skip_start, const int *bits1, const int *bits2,
      const int *thresh, const int *cap, fl_i32 total, fl_i32 *_balance, int skip_rsv, int *intensity,
      int intensity_rsv, int *dual_stereo, int dual_stereo_rsv, int *bits, int *ebits, int *fine_priority,
      int C, int LM, ec_ctx *ec, int encode, int prev, int signalBandwidth) {
   const fl_i16 *eb = fl_eband5ms;
   fl_i32 psum, left, percoeff, balance;
   int lo, hi, i, j, logM, stereo, codedBands = -1, alloc_floor, done;
   alloc_floor = C<<BITRES;
   stereo = C > 1;
   logM = LM<<BITRES;
   lo = 0;
   hi = 1<<ALLOC_STEPS;
   for (i = 0; i < ALLOC_STEPS; i++) {
      int mid = (lo+hi)>>1;
      psum = 0;
      done = 0;
      for (j = end; j-- > start;) {
         int tmp = bits1[j]+(mid*(fl_i32)bits2[j]>>ALLOC_STEPS);
         if (tmp >= thresh[j] || done) {
            done = 1;
            psum += IMIN(tmp, cap[j]);
         } else if (tmp >= alloc_floor) psum += alloc_floor;
      }
      if (psum > total) hi = mid;
      else lo = mid;
   }
   psum = 0;
   done = 0;
   for (j = end; j-- > start;) {
      int tmp = bits1[j]+((fl_i32)lo*bits2[j]>>ALLOC_STEPS);
      if (tmp < thresh[j] && !done) {
         if (tmp >= alloc_floor) tmp = alloc_floor;
         else tmp = 0;
      } else done = 1;
      tmp = IMIN(tmp, cap[j]);
      bits[j] = tmp;
      psum += tmp;
   }
   for (codedBands = end;; codedBands--) {
      int band_width, band_bits, rem;
      j = codedBands-1;
      if (j <= skip_start) {
         total += skip_rsv;
         break;
      }
      left = total-psum;
      percoeff = (fl_i32)((fl_u32)left/(fl_u32)(eb[codedBands]-eb[start]));
      left -= (eb[codedBands]-eb[start])*percoeff;
      rem = IMAX(left-(eb[j]-eb[start]), 0);
      band_width = eb[codedBands]-eb[j];
      band_bits = (int)(bits[j]+percoeff*band_width+rem);
      if (band_bits >= IMAX(thresh[j], alloc_floor+(1<<BITRES))) {
         if (encode) {
            int depth_threshold;
            if (codedBands > 17) depth_threshold = j < prev ? 7 : 9;
            else depth_threshold = 0;
            if (codedBands <= start+2 || (band_bits > (depth_threshold*band_width<<LM<<BITRES)>>4 && j <= signalBandwidth)) {
               ec_enc_bit_logp(ec, 1, 1);
               break;
            }
            ec_enc_bit_logp(ec, 0, 1);
         } else if (ec_dec_bit_logp(ec, 1)) break;
         psum += 1<<BITRES;
         band_bits -= 1<<BITRES;
      }
      psum -= bits[j]+intensity_rsv;
      if (intensity_rsv > 0) intensity_rsv = fl_log2_frac_table[j-start];
      psum += intensity_rsv;
      if (band_bits >= alloc_floor) {
         psum += alloc_floor;
         bits[j] = alloc_floor;
      } else bits[j] = 0;
   }
   if (intensity_rsv > 0) {
      if (encode) {
         *intensity = IMIN(*intensity, codedBands);
         ec_enc_uint(ec, *intensity-start, codedBands+1-start);
      } else *intensity = start+ec_dec_uint(ec, codedBands+1-start);
   } else *intensity = 0;
   if (*intensity <= start) {
      total += dual_stereo_rsv;
      dual_stereo_rsv = 0;
   }
   if (dual_stereo_rsv > 0) {
      if (encode) ec_enc_bit_logp(ec, *dual_stereo, 1);
      else *dual_stereo = ec_dec_bit_logp(ec, 1);
   } else *dual_stereo = 0;
   left = total-psum;
   percoeff = (fl_i32)((fl_u32)left/(fl_u32)(eb[codedBands]-eb[start]));
   left -= (eb[codedBands]-eb[start])*percoeff;
   for (j = start; j < codedBands; j++) bits[j] += ((int)percoeff*(eb[j+1]-eb[j]));
   for (j = start; j < codedBands; j++) {
      int tmp = (int)IMIN(left, eb[j+1]-eb[j]);
      bits[j] += tmp;
      left -= tmp;
   }
   balance = 0;
   for (j = start; j < codedBands; j++) {
      int N0, N, den, offset, NClogN;
      fl_i32 excess, bit;
      N0 = eb[j+1]-eb[j];
      N = N0<<LM;
      bit = (fl_i32)bits[j]+balance;
      if (N > 1) {
         excess = FL_MAX(bit-cap[j], 0);
         bits[j] = bit-excess;
         den = (C*N+((C == 2 && N > 2 && !*dual_stereo && j < *intensity) ? 1 : 0));
         NClogN = den*(fl_logN400[j]+logM);
         offset = (NClogN>>1)-den*FINE_OFFSET;
         if (N == 2) offset += den<<BITRES>>2;
         if (bits[j]+offset < den*2<<BITRES) offset += NClogN>>2;
         else if (bits[j]+offset < den*3<<BITRES) offset += NClogN>>3;
         ebits[j] = IMAX(0, (bits[j]+offset+(den<<(BITRES-1))));
         ebits[j] = (int)((fl_u32)ebits[j]/(fl_u32)den)>>BITRES;
         if (C*ebits[j] > (bits[j]>>BITRES)) ebits[j] = bits[j]>>stereo>>BITRES;
         ebits[j] = IMIN(ebits[j], MAX_FINE_BITS);
         fine_priority[j] = ebits[j]*(den<<BITRES) >= bits[j]+offset;
         bits[j] -= C*ebits[j]<<BITRES;
      } else {
         excess = FL_MAX(0, bit-(C<<BITRES));
         bits[j] = bit-excess;
         ebits[j] = 0;
         fine_priority[j] = 1;
      }
      if (excess > 0) {
         int extra_fine, extra_bits;
         extra_fine = IMIN(excess>>(stereo+BITRES), MAX_FINE_BITS-ebits[j]);
         ebits[j] += extra_fine;
         extra_bits = extra_fine*C<<BITRES;
         fine_priority[j] = extra_bits >= excess-balance;
         excess -= extra_bits;
      }
      balance = excess;
   }
   *_balance = balance;
   for (; j < end; j++) {
      ebits[j] = bits[j]>>stereo>>BITRES;
      bits[j] = 0;
      fine_priority[j] = ebits[j] < 1;
   }
   return codedBands;
}

static int fl_compute_allocation(int start, int end, const int *offsets, const int *cap, int alloc_trim,
      int *intensity, int *dual_stereo, fl_i32 total, fl_i32 *balance, int *pulses, int *ebits,
      int *fine_priority, int C, int LM, ec_ctx *ec, int encode, int prev, int signalBandwidth) {
   const fl_i16 *eb = fl_eband5ms;
   int lo, hi, len = CELT_NB_EBANDS, j, skip_start, skip_rsv, intensity_rsv, dual_stereo_rsv;
   int bits1[CELT_NB_EBANDS], bits2[CELT_NB_EBANDS], thresh[CELT_NB_EBANDS], trim_offset[CELT_NB_EBANDS];
   total = IMAX(total, 0);
   skip_start = start;
   skip_rsv = total >= 1<<BITRES ? 1<<BITRES : 0;
   total -= skip_rsv;
   intensity_rsv = dual_stereo_rsv = 0;
   if (C == 2) {
      intensity_rsv = fl_log2_frac_table[end-start];
      if (intensity_rsv > total) intensity_rsv = 0;
      else {
         total -= intensity_rsv;
         dual_stereo_rsv = total >= 1<<BITRES ? 1<<BITRES : 0;
         total -= dual_stereo_rsv;
      }
   }
   for (j = start; j < end; j++) {
      thresh[j] = IMAX((C)<<BITRES, (3*(eb[j+1]-eb[j])<<LM<<BITRES)>>4);
      trim_offset[j] = C*(eb[j+1]-eb[j])*(alloc_trim-5-LM)*(end-j-1)*(1<<(LM+BITRES))>>6;
      if ((eb[j+1]-eb[j])<<LM == 1) trim_offset[j] -= C<<BITRES;
   }
   lo = 1;
   hi = 11-1;
   do {
      int done = 0, psum = 0, mid = (lo+hi)>>1;
      for (j = end; j-- > start;) {
         int bitsj, N = eb[j+1]-eb[j];
         bitsj = C*N*fl_band_allocation[mid*len+j]<<LM>>2;
         if (bitsj > 0) bitsj = IMAX(0, bitsj+trim_offset[j]);
         bitsj += offsets[j];
         if (bitsj >= thresh[j] || done) {
            done = 1;
            psum += IMIN(bitsj, cap[j]);
         } else if (bitsj >= C<<BITRES) psum += C<<BITRES;
      }
      if (psum > total) hi = mid-1;
      else lo = mid+1;
   } while (lo <= hi);
   hi = lo--;
   for (j = start; j < end; j++) {
      int bits1j, bits2j, N = eb[j+1]-eb[j];
      bits1j = C*N*fl_band_allocation[lo*len+j]<<LM>>2;
      bits2j = hi >= 11 ? cap[j] : C*N*fl_band_allocation[hi*len+j]<<LM>>2;
      if (bits1j > 0) bits1j = IMAX(0, bits1j+trim_offset[j]);
      if (bits2j > 0) bits2j = IMAX(0, bits2j+trim_offset[j]);
      if (lo > 0) bits1j += offsets[j];
      bits2j += offsets[j];
      if (offsets[j] > 0) skip_start = j;
      bits2j = IMAX(0, bits2j-bits1j);
      bits1[j] = bits1j;
      bits2[j] = bits2j;
   }
   return fl_interp_bits2pulses(start, end, skip_start, bits1, bits2, thresh, cap, total, balance, skip_rsv,
         intensity, intensity_rsv, dual_stereo, dual_stereo_rsv, pulses, ebits, fine_priority, C, LM, ec,
         encode, prev, signalBandwidth);
}

/* Laplace coding of coarse energy */
#define LAPLACE_MINP 1
#define LAPLACE_NMIN 16
static unsigned fl_laplace_get_freq1(unsigned fs0, int decay) {
   unsigned ft = 32768-LAPLACE_MINP*(2*LAPLACE_NMIN)-fs0;
   return ft*(fl_i32)(16384-decay)>>15;
}
static void fl_laplace_encode(ec_ctx *enc, int *value, unsigned fs, int decay) {
   unsigned fl = 0;
   int val = *value;
   if (val) {
      int s, i;
      s = -(val < 0);
      val = (val+s)^s;
      fl = fs;
      fs = fl_laplace_get_freq1(fs, decay);
      for (i = 1; fs > 0 && i < val; i++) {
         fs *= 2;
         fl += fs+2*LAPLACE_MINP;
         fs = (fs*(fl_i32)decay)>>15;
      }
      if (!fs) {
         int di, ndi_max;
         ndi_max = (32768-fl+LAPLACE_MINP-1);
         ndi_max = (ndi_max-s)>>1;
         di = IMIN(val-i, ndi_max-1);
         fl += (2*di+1+s)*LAPLACE_MINP;
         fs = IMIN(LAPLACE_MINP, 32768-fl);
         *value = (i+di+s)^s;
      } else {
         fs += LAPLACE_MINP;
         fl += fs&~s;
      }
   }
   ec_encode_bin(enc, fl, fl+fs, 15);
}
static int fl_laplace_decode(ec_ctx *dec, unsigned fs, int decay) {
   int val = 0;
   unsigned fl, fm;
   fm = ec_decode_bin(dec, 15);
   fl = 0;
   if (fm >= fs) {
      val++;
      fl = fs;
      fs = fl_laplace_get_freq1(fs, decay)+LAPLACE_MINP;
      while (fs > LAPLACE_MINP && fm >= fl+2*fs) {
         fs *= 2;
         fl += fs;
         fs = ((fs-2*LAPLACE_MINP)*(fl_i32)decay)>>15;
         fs += LAPLACE_MINP;
         val++;
      }
      if (fs <= LAPLACE_MINP) {
         int di = (fm-fl)>>1;
         val += di;
         fl += 2*di*LAPLACE_MINP;
      }
      if (fm < fl+fs) val = -val;
      else fl += fs;
   }
   ec_dec_update(dec, fl, IMIN(fl+fs, 32768), 32768);
   return val;
}

/* PVQ codeword index <-> pulse vector */
static fl_u32 fl_icwrs(int n, const int *y) {
   fl_u32 i;
   int j, k;
   j = n-1;
   i = y[j] < 0;
   k = abs(y[j]);
   do {
      j--;
      i += CELT_PVQ_U(n-j, k);
      k += abs(y[j]);
      if (y[j] < 0) i += CELT_PVQ_U(n-j, k+1);
   } while (j > 0);
   return i;
}
static void fl_encode_pulses(const int *y, int n, int k, ec_ctx *enc) {
   ec_enc_uint(enc, fl_icwrs(n, y), CELT_PVQ_V(n, k));
}
/* Returns the squared norm of the decoded vector. */
static float fl_cwrsi(int n, int k, fl_u32 i, int *y) {
   fl_u32 p;
   int s, k0;
   fl_i16 val;
   float yy = 0;
   while (n > 2) {
      fl_u32 q;
      if (k >= n) {
         const fl_u32 *row = fl_pvq_u_row[n];
         p = row[k+1];
         s = -(i >= p);
         i -= p&s;
         k0 = k;
         q = row[n];
         if (q > i) {
            k = n;
            do p = fl_pvq_u_row[--k][n];
            while (p > i);
         } else for (p = row[k]; p > i; p = row[k]) k--;
         i -= p;
         val = (k0-k+s)^s;
         *y++ = val;
         yy = yy+(float)val*(float)val;
      } else {
         p = fl_pvq_u_row[k][n];
         q = fl_pvq_u_row[k+1][n];
         if (p <= i && i < q) {
            i -= p;
            *y++ = 0;
         } else {
            s = -(i >= q);
            i -= q&s;
            k0 = k;
            do p = fl_pvq_u_row[--k][n];
            while (p > i);
            i -= p;
            val = (k0-k+s)^s;
            *y++ = val;
            yy = yy+(float)val*(float)val;
         }
      }
      n--;
   }
   p = 2*k+1;
   s = -(i >= p);
   i -= p&s;
   k0 = k;
   k = (i+1)>>1;
   if (k) i -= 2*k-1;
   val = (k0-k+s)^s;
   *y++ = val;
   yy = yy+(float)val*(float)val;
   s = -(int)i;
   val = (k+s)^s;
   *y = val;
   yy = yy+(float)val*(float)val;
   return yy;
}
static float fl_decode_pulses(int *y, int n, int k, ec_ctx *dec) {
   return fl_cwrsi(n, k, ec_dec_uint(dec, CELT_PVQ_V(n, k)), y);
}

/* Integer helpers shared by the band coders */
static fl_i16 fl_bitexact_cos(fl_i16 x) {
   fl_i32 tmp;
   fl_i16 x2;
   tmp = (4096+((fl_i32)(x)*(x)))>>13;
   x2 = tmp;
   x2 = (32767-x2)+FL_FRAC_MUL16(x2, (-7651+FL_FRAC_MUL16(x2, (8277+FL_FRAC_MUL16(-626, x2)))));
   return 1+x2;
}
static int fl_bitexact_log2tan(int isin, int icos) {
   int lc = EC_ILOG(icos), ls = EC_ILOG(isin);
   icos <<= 15-lc;
   isin <<= 15-ls;
   return (ls-lc)*(1<<11)+FL_FRAC_MUL16(isin, FL_FRAC_MUL16(isin, -2597)+7932)
         -FL_FRAC_MUL16(icos, FL_FRAC_MUL16(icos, -2597)+7932);
}
static int fl_compute_qn(int N, int b, int offset, int pulse_cap, int stereo) {
   static const fl_i16 exp2_table8[8] = {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
   int qn, qb, N2 = 2*N-1;
   if (stereo && N == 2) N2--;
   qb = (b+N2*offset)/N2;
   qb = IMIN(b-pulse_cap-(4<<BITRES), qb);
   qb = IMIN(8<<BITRES, qb);
   if (qb < (1<<BITRES>>1)) qn = 1;
   else {
      qn = exp2_table8[qb&0x7]>>(14-(qb>>BITRES));
      qn = (qn+1)>>1<<1;
   }
   return qn;
}
static fl_u32 fl_isqrt32(fl_u32 val) {
   fl_u32 b, g = 0;
   int bshift = (EC_ILOG(val)-1)>>1;
   b = 1U<<bshift;
   do {
      fl_u32 t = (((fl_u32)g<<1)+b)<<bshift;
      if (t <= val) {
         g += b;
         val -= t;
      }
      b >>= 1;
      bshift--;
   } while (bshift >= 0);
   return g;
}
static unsigned fl_extract_collapse_mask(int *iy, int N, int B) {
   unsigned collapse_mask;
   int N0, i;
   if (B <= 1) return 1;
   N0 = N/B;
   collapse_mask = 0;
   i = 0;
   do {
      int j = 0;
      unsigned tmp = 0;
      do tmp |= iy[i*N0+j];
      while (++j < N0);
      collapse_mask |= (tmp != 0)<<i;
   } while (++i < B);
   return collapse_mask;
}
