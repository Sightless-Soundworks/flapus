/* ---- SILK: fixed-point macros and routines shared by encoder and decoder ---- */
#define silk_int32_MAX 0x7FFFFFFF
#define silk_int32_MIN ((fl_i32)0x80000000)
#define silk_int16_MAX 0x7FFF
#define silk_int16_MIN ((fl_i16)0x8000)
#define silk_int8_MAX 0x7F
#define silk_int8_MIN ((fl_i8)0x80)
#define silk_uint8_MAX 0xFF

#define silk_SMULWB(a32,b32) ((fl_i32)(((a32)*(fl_i64)((fl_i16)(b32)))>>16))
#define silk_SMLAWB(a32,b32,c32) ((fl_i32)((a32)+(((b32)*(fl_i64)((fl_i16)(c32)))>>16)))
#define silk_SMULWT(a32,b32) ((fl_i32)(((a32)*(fl_i64)((b32)>>16))>>16))
#define silk_SMLAWT(a32,b32,c32) ((fl_i32)((a32)+(((b32)*((fl_i64)(c32)>>16))>>16)))
#define silk_SMULBB(a32,b32) ((fl_i32)((fl_i16)(a32))*(fl_i32)((fl_i16)(b32)))
#define silk_SMLABB(a32,b32,c32) ((a32)+((fl_i32)((fl_i16)(b32)))*(fl_i32)((fl_i16)(c32)))
#define silk_SMULBT(a32,b32) ((fl_i32)((fl_i16)(a32))*((b32)>>16))
#define silk_SMLABT(a32,b32,c32) ((a32)+((fl_i32)((fl_i16)(b32)))*((c32)>>16))
#define silk_SMLAL(a64,b32,c32) ((a64)+((fl_i64)(b32)*(fl_i64)(c32)))
#define silk_SMULWW(a32,b32) ((fl_i32)(((fl_i64)(a32)*(b32))>>16))
#define silk_SMLAWW(a32,b32,c32) ((fl_i32)((a32)+(((fl_i64)(b32)*(c32))>>16)))
#define silk_ADD_SAT32(a,b) ((((fl_u32)(a)+(fl_u32)(b))&0x80000000) == 0 ? \
   ((((a)&(b))&0x80000000) != 0 ? silk_int32_MIN : (a)+(b)) : \
   ((((a)|(b))&0x80000000) == 0 ? silk_int32_MAX : (a)+(b)))
#define silk_SUB_SAT32(a,b) ((((fl_u32)(a)-(fl_u32)(b))&0x80000000) == 0 ? \
   (((a)&((b)^0x80000000)&0x80000000) ? silk_int32_MIN : (a)-(b)) : \
   ((((a)^0x80000000)&(b)&0x80000000) ? silk_int32_MAX : (a)-(b)))
#define silk_MUL(a32,b32) ((a32)*(b32))
#define silk_MLA(a32,b32,c32) ((a32)+((b32)*(c32)))
#define silk_SMULTT(a32,b32) (((a32)>>16)*((b32)>>16))
#define silk_SMLATT(a32,b32,c32) ((a32)+((b32)>>16)*((c32)>>16))
#define silk_SMLALBB(a64,b16,c16) ((a64)+(fl_i64)((fl_i32)(b16)*(fl_i32)(c16)))
#define silk_SMULL(a32,b32) ((fl_i64)(a32)*(b32))
#define silk_ADD32_ovflw(a,b) ((fl_i32)((fl_u32)(a)+(fl_u32)(b)))
#define silk_SUB32_ovflw(a,b) ((fl_i32)((fl_u32)(a)-(fl_u32)(b)))
#define silk_MLA_ovflw(a32,b32,c32) silk_ADD32_ovflw((a32), (fl_u32)(b32)*(fl_u32)(c32))
#define silk_SMLABB_ovflw(a32,b32,c32) (silk_ADD32_ovflw((a32), ((fl_i32)((fl_i16)(b32)))*(fl_i32)((fl_i16)(c32))))
#define silk_DIV32_16(a32,b16) ((fl_i32)((a32)/(b16)))
#define silk_DIV32(a32,b32) ((fl_i32)((a32)/(b32)))
#define silk_ADD32(a,b) ((a)+(b))
#define silk_SUB32(a,b) ((a)-(b))
#define silk_SAT16(a) ((a) > silk_int16_MAX ? silk_int16_MAX : ((a) < silk_int16_MIN ? silk_int16_MIN : (a)))
#define silk_SAT32(a) ((a) > silk_int32_MAX ? silk_int32_MAX : ((a) < silk_int32_MIN ? silk_int32_MIN : (a)))
#define silk_ADD_SAT16(a,b) (fl_i16)silk_SAT16(((fl_i32)(a))+(b))
#define silk_SUB_SAT16(a,b) (fl_i16)silk_SAT16(((fl_i32)(a))-(b))
#define silk_POS_SAT32(a) ((a) > silk_int32_MAX ? silk_int32_MAX : (a))
#define silk_ADD_POS_SAT32(a,b) ((((fl_u32)(a)+(fl_u32)(b))&0x80000000) ? silk_int32_MAX : ((a)+(b)))
#define silk_LSHIFT32(a,shift) ((fl_i32)((fl_u32)(a)<<(shift)))
#define silk_LSHIFT(a,shift) silk_LSHIFT32(a, shift)
#define silk_LSHIFT64(a,shift) ((fl_i64)((fl_u64)(a)<<(shift)))
#define silk_RSHIFT(a,shift) ((a)>>(shift))
#define silk_RSHIFT32(a,shift) ((a)>>(shift))
#define silk_RSHIFT64(a,shift) ((a)>>(shift))
#define silk_LIMIT(a,limit1,limit2) ((limit1) > (limit2) ? ((a) > (limit1) ? (limit1) : ((a) < (limit2) ? (limit2) : (a))) \
   : ((a) > (limit2) ? (limit2) : ((a) < (limit1) ? (limit1) : (a))))
#define silk_LIMIT_int silk_LIMIT
#define silk_LIMIT_16 silk_LIMIT
#define silk_LIMIT_32 silk_LIMIT
#define silk_LSHIFT_SAT32(a,shift) (silk_LSHIFT32(silk_LIMIT((a), silk_RSHIFT32(silk_int32_MIN, (shift)), \
   silk_RSHIFT32(silk_int32_MAX, (shift))), (shift)))
#define silk_LSHIFT_ovflw(a,shift) ((fl_i32)((fl_u32)(a)<<(shift)))
#define silk_ADD_LSHIFT32(a,b,shift) ((a)+silk_LSHIFT32((b), (shift)))
#define silk_ADD_RSHIFT32(a,b,shift) ((a)+silk_RSHIFT32((b), (shift)))
#define silk_ADD_RSHIFT_uint(a,b,shift) ((a)+((b)>>(shift)))
#define silk_SUB_LSHIFT32(a,b,shift) ((a)-silk_LSHIFT32((b), (shift)))
#define silk_SUB_RSHIFT32(a,b,shift) ((a)-silk_RSHIFT32((b), (shift)))
#define silk_RSHIFT_ROUND(a,shift) ((shift) == 1 ? ((a)>>1)+((a)&1) : (((a)>>((shift)-1))+1)>>1)
#define silk_RSHIFT_ROUND64(a,shift) ((shift) == 1 ? ((a)>>1)+((a)&1) : (((a)>>((shift)-1))+1)>>1)
#define silk_min(a,b) (((a) < (b)) ? (a) : (b))
#define silk_max(a,b) (((a) > (b)) ? (a) : (b))
#define silk_min_int silk_min
#define silk_max_int silk_max
#define silk_min_16 silk_min
#define silk_max_16 silk_max
#define silk_min_32 silk_min
#define silk_max_32 silk_max
#define silk_abs(a) (((a) > 0) ? (a) : -(a))
#define silk_abs_int32(a) (((a)^((a)>>31))-((a)>>31))
#define SILK_FIX_CONST(C,Q) ((fl_i32)((C)*((fl_i64)1<<(Q))+0.5))
#define silk_RAND(seed) (silk_MLA_ovflw(907633515, (seed), 196314165))
#define silk_SMMUL(a32,b32) (fl_i32)silk_RSHIFT64(silk_SMULL((a32), (b32)), 32)

/* SILK constants */
#define MAX_FRAMES_PER_PACKET 3
#define MAX_FS_KHZ 16
#define MAX_API_FS_KHZ 48
#define TYPE_NO_VOICE_ACTIVITY 0
#define TYPE_UNVOICED 1
#define TYPE_VOICED 2
#define CODE_INDEPENDENTLY 0
#define CODE_INDEPENDENTLY_NO_LTP_SCALING 1
#define CODE_CONDITIONALLY 2
#define STEREO_QUANT_TAB_SIZE 16
#define STEREO_QUANT_SUB_STEPS 5
#define STEREO_INTERP_LEN_MS 8
#define PITCH_EST_MIN_LAG_MS 2
#define PITCH_EST_MAX_LAG_MS 18
#define MAX_NB_SUBFR 4
#define LTP_MEM_LENGTH_MS 20
#define SUB_FRAME_LENGTH_MS 5
#define MAX_SUB_FRAME_LENGTH (SUB_FRAME_LENGTH_MS*MAX_FS_KHZ)
#define MAX_FRAME_LENGTH_MS (SUB_FRAME_LENGTH_MS*MAX_NB_SUBFR)
#define MAX_FRAME_LENGTH (MAX_FRAME_LENGTH_MS*MAX_FS_KHZ)
#define N_LEVELS_QGAIN 64
#define MAX_DELTA_GAIN_QUANT 36
#define MIN_DELTA_GAIN_QUANT -4
#define MIN_QGAIN_DB 2
#define MAX_QGAIN_DB 88
#define OFFSET_VL_Q10 32
#define OFFSET_VH_Q10 100
#define OFFSET_UVL_Q10 100
#define OFFSET_UVH_Q10 240
#define QUANT_LEVEL_ADJUST_Q10 80
#define MAX_LPC_STABILIZE_ITERATIONS 16
#define MAX_PREDICTION_POWER_GAIN 1e4f
#define MAX_LPC_ORDER 16
#define MIN_LPC_ORDER 10
#define LTP_ORDER 5
#define NB_LTP_CBKS 3
#define SHELL_CODEC_FRAME_LENGTH 16
#define LOG2_SHELL_CODEC_FRAME_LENGTH 4
#define MAX_NB_SHELL_BLOCKS (MAX_FRAME_LENGTH/SHELL_CODEC_FRAME_LENGTH)
#define N_RATE_LEVELS 10
#define SILK_MAX_PULSES 16
#define LSF_COS_TAB_SZ_FIX 128
#define NLSF_QUANT_MAX_AMPLITUDE 4
#define NLSF_QUANT_LEVEL_ADJ 0.1
#define TRANSITION_NB 3
#define TRANSITION_NA 2
#define TRANSITION_INT_NUM 5
#define BWE_AFTER_LOSS_Q16 63570
#define CNG_BUF_MASK_MAX 255
#define CNG_GAIN_SMTH_Q16 4634
#define CNG_GAIN_SMTH_THRESHOLD_Q16 46396
#define CNG_NLSF_SMTH_Q16 16348
#define SILK_MAX_ORDER_LPC 24
#define PE_MAX_NB_SUBFR 4
#define PE_MAX_LAG_MS 18
#define PE_MIN_LAG_MS 2
#define PE_NB_CBKS_STAGE2 3
#define PE_NB_CBKS_STAGE2_EXT 11
#define PE_NB_CBKS_STAGE3_MAX 34
#define PE_NB_CBKS_STAGE3_MID 24
#define PE_NB_CBKS_STAGE3_MIN 16
#define PE_NB_CBKS_STAGE3_10MS 12
#define PE_NB_CBKS_STAGE2_10MS 3
#define SILK_PE_MAX_COMPLEX 2
#define RESAMPLER_DOWN_ORDER_FIR0 18
#define RESAMPLER_DOWN_ORDER_FIR1 24
#define RESAMPLER_DOWN_ORDER_FIR2 36
#define RESAMPLER_ORDER_FIR_12 8
#define FLAG_DECODE_NORMAL 0
#define FLAG_PACKET_LOST 1
#define FLAG_DECODE_LBRR 2

typedef struct {
   const fl_i16 nVectors;
   const fl_i16 order;
   const fl_i16 quantStepSize_Q16;
   const fl_i16 invQuantStepSize_Q6;
   const fl_u8 *CB1_NLSF_Q8;
   const fl_i16 *CB1_Wght_Q9;
   const fl_u8 *CB1_iCDF;
   const fl_u8 *pred_Q8;
   const fl_u8 *ec_sel;
   const fl_u8 *ec_iCDF;
   const fl_u8 *ec_Rates_Q5;
   const fl_i16 *deltaMin_Q15;
} fl_nlsf_cb;

#include "tables_silk.c"

static const fl_i16 silk_resampler_up2_hq_0[3] = {1746, 14986, 39083-65536};
static const fl_i16 silk_resampler_up2_hq_1[3] = {6854, 25769, 55542-65536};

static fl_i32 silk_CLZ16(fl_i16 in16) { return 32-EC_ILOG(in16<<16|0x8000); }
static fl_i32 silk_CLZ32(fl_i32 in32) { return in32 ? 32-EC_ILOG(in32) : 32; }
static fl_i32 silk_CLZ64(fl_i64 in) {
   fl_i32 in_upper = (fl_i32)silk_RSHIFT64(in, 32);
   if (in_upper == 0) return 32+silk_CLZ32((fl_i32)in);
   return silk_CLZ32(in_upper);
}
static fl_i32 silk_ROR32(fl_i32 a32, int rot) {
   fl_u32 x = (fl_u32)a32, r = (fl_u32)rot, m = (fl_u32)-rot;
   if (rot == 0) return a32;
   else if (rot < 0) return (fl_i32)((x<<m)|(x>>(32-m)));
   else return (fl_i32)((x<<(32-r))|(x>>r));
}
static void silk_CLZ_FRAC(fl_i32 in, fl_i32 *lz, fl_i32 *frac_Q7) {
   fl_i32 lzeros = silk_CLZ32(in);
   *lz = lzeros;
   *frac_Q7 = silk_ROR32(in, 24-lzeros)&0x7f;
}
static fl_i32 silk_SQRT_APPROX(fl_i32 x) {
   fl_i32 y, lz, frac_Q7;
   if (x <= 0) return 0;
   silk_CLZ_FRAC(x, &lz, &frac_Q7);
   y = (lz&1) ? 32768 : 46214;
   y >>= silk_RSHIFT(lz, 1);
   y = silk_SMLAWB(y, y, silk_SMULBB(213, frac_Q7));
   return y;
}
static fl_i32 silk_DIV32_varQ(const fl_i32 a32, const fl_i32 b32, const int Qres) {
   int a_headrm, b_headrm, lshift;
   fl_i32 b32_inv, a32_nrm, b32_nrm, result;
   a_headrm = silk_CLZ32(silk_abs(a32))-1;
   a32_nrm = silk_LSHIFT(a32, a_headrm);
   b_headrm = silk_CLZ32(silk_abs(b32))-1;
   b32_nrm = silk_LSHIFT(b32, b_headrm);
   b32_inv = silk_DIV32_16(silk_int32_MAX>>2, silk_RSHIFT(b32_nrm, 16));
   result = silk_SMULWB(a32_nrm, b32_inv);
   a32_nrm = silk_SUB32_ovflw(a32_nrm, silk_LSHIFT_ovflw(silk_SMMUL(b32_nrm, result), 3));
   result = silk_SMLAWB(result, a32_nrm, b32_inv);
   lshift = 29+a_headrm-b_headrm-Qres;
   if (lshift < 0) return silk_LSHIFT_SAT32(result, -lshift);
   if (lshift < 32) return silk_RSHIFT(result, lshift);
   return 0;
}
static fl_i32 silk_INVERSE32_varQ(const fl_i32 b32, const int Qres) {
   int b_headrm, lshift;
   fl_i32 b32_inv, b32_nrm, err_Q32, result;
   b_headrm = silk_CLZ32(silk_abs(b32))-1;
   b32_nrm = silk_LSHIFT(b32, b_headrm);
   b32_inv = silk_DIV32_16(silk_int32_MAX>>2, silk_RSHIFT(b32_nrm, 16));
   result = silk_LSHIFT(b32_inv, 16);
   err_Q32 = silk_LSHIFT(((fl_i32)1<<29)-silk_SMULWB(b32_nrm, b32_inv), 3);
   result = silk_SMLAWW(result, err_Q32, b32_inv);
   lshift = 61-b_headrm-Qres;
   if (lshift <= 0) return silk_LSHIFT_SAT32(result, -lshift);
   if (lshift < 32) return silk_RSHIFT(result, lshift);
   return 0;
}

static fl_i32 silk_log2lin(const fl_i32 inLog_Q7) {
   fl_i32 out, frac_Q7;
   if (inLog_Q7 < 0) return 0;
   else if (inLog_Q7 >= 3967) return silk_int32_MAX;
   out = silk_LSHIFT(1, silk_RSHIFT(inLog_Q7, 7));
   frac_Q7 = inLog_Q7&0x7F;
   if (inLog_Q7 < 2048)
      out = silk_ADD_RSHIFT32(out, silk_MUL(out, silk_SMLAWB(frac_Q7, silk_SMULBB(frac_Q7, 128-frac_Q7), -174)), 7);
   else
      out = silk_MLA(out, silk_RSHIFT(out, 7), silk_SMLAWB(frac_Q7, silk_SMULBB(frac_Q7, 128-frac_Q7), -174));
   return out;
}
static fl_i32 silk_lin2log(const fl_i32 inLin) {
   fl_i32 lz, frac_Q7;
   silk_CLZ_FRAC(inLin, &lz, &frac_Q7);
   return silk_ADD_LSHIFT32(silk_SMLAWB(frac_Q7, silk_MUL(frac_Q7, 128-frac_Q7), 179), 31-lz, 7);
}

static void silk_insertion_sort_increasing(fl_i32 *a, int *idx, const int L, const int K) {
   fl_i32 value;
   int i, j;
   for (i = 0; i < K; i++) idx[i] = i;
   for (i = 1; i < K; i++) {
      value = a[i];
      for (j = i-1; (j >= 0) && (value < a[j]); j--) {
         a[j+1] = a[j];
         idx[j+1] = idx[j];
      }
      a[j+1] = value;
      idx[j+1] = i;
   }
   for (i = K; i < L; i++) {
      value = a[i];
      if (value < a[K-1]) {
         for (j = K-2; (j >= 0) && (value < a[j]); j--) {
            a[j+1] = a[j];
            idx[j+1] = idx[j];
         }
         a[j+1] = value;
         idx[j+1] = i;
      }
   }
}
static void silk_insertion_sort_increasing_all_values_int16(fl_i16 *a, const int L) {
   int value, i, j;
   for (i = 1; i < L; i++) {
      value = a[i];
      for (j = i-1; (j >= 0) && (value < a[j]); j--) a[j+1] = a[j];
      a[j+1] = value;
   }
}

static void silk_sum_sqr_shift(fl_i32 *energy, int *shift, const fl_i16 *x, int len) {
   int i, shft;
   fl_u32 nrg_tmp;
   fl_i32 nrg;
   shft = 31-silk_CLZ32(len);
   nrg = len;
   for (i = 0; i < len-1; i += 2) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg_tmp = silk_SMLABB_ovflw(nrg_tmp, x[i+1], x[i+1]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   if (i < len) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   shft = silk_max_32(0, shft+3-silk_CLZ32(nrg));
   nrg = 0;
   for (i = 0; i < len-1; i += 2) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg_tmp = silk_SMLABB_ovflw(nrg_tmp, x[i+1], x[i+1]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   if (i < len) {
      nrg_tmp = silk_SMULBB(x[i], x[i]);
      nrg = (fl_i32)silk_ADD_RSHIFT_uint(nrg, nrg_tmp, shft);
   }
   *shift = shft;
   *energy = nrg;
}

static void silk_bwexpander(fl_i16 *ar, const int d, fl_i32 chirp_Q16) {
   int i;
   fl_i32 chirp_minus_one_Q16 = chirp_Q16-65536;
   for (i = 0; i < d-1; i++) {
      ar[i] = (fl_i16)silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, ar[i]), 16);
      chirp_Q16 += silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, chirp_minus_one_Q16), 16);
   }
   ar[d-1] = (fl_i16)silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, ar[d-1]), 16);
}
static void silk_bwexpander_32(fl_i32 *ar, const int d, fl_i32 chirp_Q16) {
   int i;
   fl_i32 chirp_minus_one_Q16 = chirp_Q16-65536;
   for (i = 0; i < d-1; i++) {
      ar[i] = silk_SMULWW(chirp_Q16, ar[i]);
      chirp_Q16 += silk_RSHIFT_ROUND(silk_MUL(chirp_Q16, chirp_minus_one_Q16), 16);
   }
   ar[d-1] = silk_SMULWW(chirp_Q16, ar[d-1]);
}

static void silk_LPC_analysis_filter(fl_i16 *out, const fl_i16 *in, const fl_i16 *B, const fl_i32 len, const fl_i32 d) {
   int j, ix;
   fl_i32 out32_Q12, out32;
   const fl_i16 *in_ptr;
   for (ix = d; ix < len; ix++) {
      in_ptr = &in[ix-1];
      out32_Q12 = silk_SMULBB(in_ptr[0], B[0]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-1], B[1]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-2], B[2]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-3], B[3]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-4], B[4]);
      out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-5], B[5]);
      for (j = 6; j < d; j += 2) {
         out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-j], B[j]);
         out32_Q12 = silk_SMLABB_ovflw(out32_Q12, in_ptr[-j-1], B[j+1]);
      }
      out32_Q12 = silk_SUB32_ovflw(silk_LSHIFT((fl_i32)in_ptr[1], 12), out32_Q12);
      out32 = silk_RSHIFT_ROUND(out32_Q12, 12);
      out[ix] = (fl_i16)silk_SAT16(out32);
   }
   memset(out, 0, d*sizeof(fl_i16));
}

#define INV_QA 24
#define A_LIMIT SILK_FIX_CONST(0.99975, INV_QA)
#define MUL32_FRAC_Q(a32,b32,Q) ((fl_i32)(silk_RSHIFT_ROUND64(silk_SMULL(a32, b32), Q)))
static fl_i32 silk_LPC_inverse_pred_gain_QA(fl_i32 A_QA[SILK_MAX_ORDER_LPC], const int order) {
   int k, n, mult2Q;
   fl_i32 invGain_Q30, rc_Q31, rc_mult1_Q30, rc_mult2, tmp1, tmp2;
   invGain_Q30 = SILK_FIX_CONST(1, 30);
   for (k = order-1; k > 0; k--) {
      if ((A_QA[k] > A_LIMIT) || (A_QA[k] < -A_LIMIT)) return 0;
      rc_Q31 = -silk_LSHIFT(A_QA[k], 31-INV_QA);
      rc_mult1_Q30 = silk_SUB32(SILK_FIX_CONST(1, 30), silk_SMMUL(rc_Q31, rc_Q31));
      invGain_Q30 = silk_LSHIFT(silk_SMMUL(invGain_Q30, rc_mult1_Q30), 2);
      if (invGain_Q30 < SILK_FIX_CONST(1.0f/MAX_PREDICTION_POWER_GAIN, 30)) return 0;
      mult2Q = 32-silk_CLZ32(silk_abs(rc_mult1_Q30));
      rc_mult2 = silk_INVERSE32_varQ(rc_mult1_Q30, mult2Q+30);
      for (n = 0; n < (k+1)>>1; n++) {
         fl_i64 tmp64;
         tmp1 = A_QA[n];
         tmp2 = A_QA[k-n-1];
         tmp64 = silk_RSHIFT_ROUND64(silk_SMULL(silk_SUB_SAT32(tmp1, MUL32_FRAC_Q(tmp2, rc_Q31, 31)), rc_mult2), mult2Q);
         if (tmp64 > silk_int32_MAX || tmp64 < silk_int32_MIN) return 0;
         A_QA[n] = (fl_i32)tmp64;
         tmp64 = silk_RSHIFT_ROUND64(silk_SMULL(silk_SUB_SAT32(tmp2, MUL32_FRAC_Q(tmp1, rc_Q31, 31)), rc_mult2), mult2Q);
         if (tmp64 > silk_int32_MAX || tmp64 < silk_int32_MIN) return 0;
         A_QA[k-n-1] = (fl_i32)tmp64;
      }
   }
   if ((A_QA[k] > A_LIMIT) || (A_QA[k] < -A_LIMIT)) return 0;
   rc_Q31 = -silk_LSHIFT(A_QA[0], 31-INV_QA);
   rc_mult1_Q30 = silk_SUB32(SILK_FIX_CONST(1, 30), silk_SMMUL(rc_Q31, rc_Q31));
   invGain_Q30 = silk_LSHIFT(silk_SMMUL(invGain_Q30, rc_mult1_Q30), 2);
   if (invGain_Q30 < SILK_FIX_CONST(1.0f/MAX_PREDICTION_POWER_GAIN, 30)) return 0;
   return invGain_Q30;
}
static fl_i32 silk_LPC_inverse_pred_gain(const fl_i16 *A_Q12, const int order) {
   int k;
   fl_i32 Atmp_QA[SILK_MAX_ORDER_LPC], DC_resp = 0;
   for (k = 0; k < order; k++) {
      DC_resp += (fl_i32)A_Q12[k];
      Atmp_QA[k] = silk_LSHIFT32((fl_i32)A_Q12[k], INV_QA-12);
   }
   if (DC_resp >= 4096) return 0;
   return silk_LPC_inverse_pred_gain_QA(Atmp_QA, order);
}

static void silk_LPC_fit(fl_i16 *a_QOUT, fl_i32 *a_QIN, const int QOUT, const int QIN, const int d) {
   int i, k, idx = 0;
   fl_i32 maxabs, absval, chirp_Q16;
   for (i = 0; i < 10; i++) {
      maxabs = 0;
      for (k = 0; k < d; k++) {
         absval = silk_abs(a_QIN[k]);
         if (absval > maxabs) {
            maxabs = absval;
            idx = k;
         }
      }
      maxabs = silk_RSHIFT_ROUND(maxabs, QIN-QOUT);
      if (maxabs > silk_int16_MAX) {
         maxabs = silk_min(maxabs, 163838);
         chirp_Q16 = SILK_FIX_CONST(0.999, 16)-silk_DIV32(silk_LSHIFT(maxabs-silk_int16_MAX, 14),
               silk_RSHIFT32(silk_MUL(maxabs, idx+1), 2));
         silk_bwexpander_32(a_QIN, d, chirp_Q16);
      } else break;
   }
   if (i == 10) {
      for (k = 0; k < d; k++) {
         a_QOUT[k] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(a_QIN[k], QIN-QOUT));
         a_QIN[k] = silk_LSHIFT((fl_i32)a_QOUT[k], QIN-QOUT);
      }
   } else {
      for (k = 0; k < d; k++) a_QOUT[k] = (fl_i16)silk_RSHIFT_ROUND(a_QIN[k], QIN-QOUT);
   }
}

#define NLSF2A_QA 16
static void silk_NLSF2A_find_poly(fl_i32 *out, const fl_i32 *cLSF, int dd) {
   int k, n;
   fl_i32 ftmp;
   out[0] = silk_LSHIFT(1, NLSF2A_QA);
   out[1] = -cLSF[0];
   for (k = 1; k < dd; k++) {
      ftmp = cLSF[2*k];
      out[k+1] = silk_LSHIFT(out[k-1], 1)-(fl_i32)silk_RSHIFT_ROUND64(silk_SMULL(ftmp, out[k]), NLSF2A_QA);
      for (n = k; n > 1; n--) out[n] += out[n-2]-(fl_i32)silk_RSHIFT_ROUND64(silk_SMULL(ftmp, out[n-1]), NLSF2A_QA);
      out[1] -= ftmp;
   }
}
static void silk_NLSF2A(fl_i16 *a_Q12, const fl_i16 *NLSF, const int d) {
   static const unsigned char ordering16[16] = {0, 15, 8, 7, 4, 11, 12, 3, 2, 13, 10, 5, 6, 9, 14, 1};
   static const unsigned char ordering10[10] = {0, 9, 6, 3, 4, 5, 8, 1, 2, 7};
   const unsigned char *ordering;
   int k, i, dd;
   fl_i32 cos_LSF_QA[SILK_MAX_ORDER_LPC] = {0}, P[SILK_MAX_ORDER_LPC/2+1], Q[SILK_MAX_ORDER_LPC/2+1];
   fl_i32 Ptmp, Qtmp, f_int, f_frac, cos_val, delta, a32_QA1[SILK_MAX_ORDER_LPC];
   ordering = d == 16 ? ordering16 : ordering10;
   for (k = 0; k < d; k++) {
      f_int = silk_RSHIFT(NLSF[k], 15-7);
      f_frac = NLSF[k]-silk_LSHIFT(f_int, 15-7);
      cos_val = fl_LSFCosTab_FIX_Q12[f_int];
      delta = fl_LSFCosTab_FIX_Q12[f_int+1]-cos_val;
      cos_LSF_QA[ordering[k]] = silk_RSHIFT_ROUND(silk_LSHIFT(cos_val, 8)+silk_MUL(delta, f_frac), 20-NLSF2A_QA);
   }
   dd = silk_RSHIFT(d, 1);
   silk_NLSF2A_find_poly(P, &cos_LSF_QA[0], dd);
   silk_NLSF2A_find_poly(Q, &cos_LSF_QA[1], dd);
   for (k = 0; k < dd; k++) {
      Ptmp = P[k+1]+P[k];
      Qtmp = Q[k+1]-Q[k];
      a32_QA1[k] = -Qtmp-Ptmp;
      a32_QA1[d-k-1] = Qtmp-Ptmp;
   }
   silk_LPC_fit(a_Q12, a32_QA1, 12, NLSF2A_QA+1, d);
   for (i = 0; silk_LPC_inverse_pred_gain(a_Q12, d) == 0 && i < MAX_LPC_STABILIZE_ITERATIONS; i++) {
      silk_bwexpander_32(a32_QA1, d, 65536-silk_LSHIFT(2, i));
      for (k = 0; k < d; k++) a_Q12[k] = (fl_i16)silk_RSHIFT_ROUND(a32_QA1[k], NLSF2A_QA+1-12);
   }
}

static void silk_NLSF_stabilize(fl_i16 *NLSF_Q15, const fl_i16 *NDeltaMin_Q15, const int L) {
   int i, I = 0, k, loops;
   fl_i16 center_freq_Q15;
   fl_i32 diff_Q15, min_diff_Q15, min_center_Q15, max_center_Q15;
   for (loops = 0; loops < 20; loops++) {
      min_diff_Q15 = NLSF_Q15[0]-NDeltaMin_Q15[0];
      I = 0;
      for (i = 1; i <= L-1; i++) {
         diff_Q15 = NLSF_Q15[i]-(NLSF_Q15[i-1]+NDeltaMin_Q15[i]);
         if (diff_Q15 < min_diff_Q15) {
            min_diff_Q15 = diff_Q15;
            I = i;
         }
      }
      diff_Q15 = (1<<15)-(NLSF_Q15[L-1]+NDeltaMin_Q15[L]);
      if (diff_Q15 < min_diff_Q15) {
         min_diff_Q15 = diff_Q15;
         I = L;
      }
      if (min_diff_Q15 >= 0) return;
      if (I == 0) NLSF_Q15[0] = NDeltaMin_Q15[0];
      else if (I == L) NLSF_Q15[L-1] = (1<<15)-NDeltaMin_Q15[L];
      else {
         min_center_Q15 = 0;
         for (k = 0; k < I; k++) min_center_Q15 += NDeltaMin_Q15[k];
         min_center_Q15 += silk_RSHIFT(NDeltaMin_Q15[I], 1);
         max_center_Q15 = 1<<15;
         for (k = L; k > I; k--) max_center_Q15 -= NDeltaMin_Q15[k];
         max_center_Q15 -= silk_RSHIFT(NDeltaMin_Q15[I], 1);
         center_freq_Q15 = (fl_i16)silk_LIMIT_32(silk_RSHIFT_ROUND((fl_i32)NLSF_Q15[I-1]+(fl_i32)NLSF_Q15[I], 1),
               min_center_Q15, max_center_Q15);
         NLSF_Q15[I-1] = center_freq_Q15-silk_RSHIFT(NDeltaMin_Q15[I], 1);
         NLSF_Q15[I] = NLSF_Q15[I-1]+NDeltaMin_Q15[I];
      }
   }
   if (loops == 20) {
      silk_insertion_sort_increasing_all_values_int16(&NLSF_Q15[0], L);
      NLSF_Q15[0] = silk_max_int(NLSF_Q15[0], NDeltaMin_Q15[0]);
      for (i = 1; i < L; i++) NLSF_Q15[i] = silk_max_int(NLSF_Q15[i], silk_ADD_SAT16(NLSF_Q15[i-1], NDeltaMin_Q15[i]));
      NLSF_Q15[L-1] = silk_min_int(NLSF_Q15[L-1], (1<<15)-NDeltaMin_Q15[L]);
      for (i = L-2; i >= 0; i--) NLSF_Q15[i] = silk_min_int(NLSF_Q15[i], NLSF_Q15[i+1]-NDeltaMin_Q15[i+1]);
   }
}

static void silk_NLSF_unpack(fl_i16 ec_ix[], fl_u8 pred_Q8[], const fl_nlsf_cb *cb, const int CB1_index) {
   int i;
   fl_u8 entry;
   const fl_u8 *ec_sel_ptr = &cb->ec_sel[CB1_index*cb->order/2];
   for (i = 0; i < cb->order; i += 2) {
      entry = *ec_sel_ptr++;
      ec_ix[i] = silk_SMULBB(silk_RSHIFT(entry, 1)&7, 2*NLSF_QUANT_MAX_AMPLITUDE+1);
      pred_Q8[i] = cb->pred_Q8[i+(entry&1)*(cb->order-1)];
      ec_ix[i+1] = silk_SMULBB(silk_RSHIFT(entry, 5)&7, 2*NLSF_QUANT_MAX_AMPLITUDE+1);
      pred_Q8[i+1] = cb->pred_Q8[i+(silk_RSHIFT(entry, 4)&1)*(cb->order-1)+1];
   }
}
static void silk_NLSF_residual_dequant(fl_i16 x_Q10[], const fl_i8 indices[], const fl_u8 pred_coef_Q8[],
      const int quant_step_size_Q16, const fl_i16 order) {
   int i, out_Q10, pred_Q10;
   out_Q10 = 0;
   for (i = order-1; i >= 0; i--) {
      pred_Q10 = silk_RSHIFT(silk_SMULBB(out_Q10, (fl_i16)pred_coef_Q8[i]), 8);
      out_Q10 = silk_LSHIFT(indices[i], 10);
      if (out_Q10 > 0) out_Q10 = out_Q10-SILK_FIX_CONST(NLSF_QUANT_LEVEL_ADJ, 10);
      else if (out_Q10 < 0) out_Q10 = out_Q10+SILK_FIX_CONST(NLSF_QUANT_LEVEL_ADJ, 10);
      out_Q10 = silk_SMLAWB(pred_Q10, (fl_i32)out_Q10, quant_step_size_Q16);
      x_Q10[i] = out_Q10;
   }
}
static void silk_NLSF_decode(fl_i16 *pNLSF_Q15, fl_i8 *NLSFIndices, const fl_nlsf_cb *cb) {
   int i;
   fl_u8 pred_Q8[MAX_LPC_ORDER];
   fl_i16 ec_ix[MAX_LPC_ORDER], res_Q10[MAX_LPC_ORDER];
   fl_i32 NLSF_Q15_tmp;
   const fl_u8 *pCB_element;
   const fl_i16 *pCB_Wght_Q9;
   silk_NLSF_unpack(ec_ix, pred_Q8, cb, NLSFIndices[0]);
   silk_NLSF_residual_dequant(res_Q10, &NLSFIndices[1], pred_Q8, cb->quantStepSize_Q16, cb->order);
   pCB_element = &cb->CB1_NLSF_Q8[NLSFIndices[0]*cb->order];
   pCB_Wght_Q9 = &cb->CB1_Wght_Q9[NLSFIndices[0]*cb->order];
   for (i = 0; i < cb->order; i++) {
      NLSF_Q15_tmp = silk_ADD_LSHIFT32(silk_DIV32_16(silk_LSHIFT((fl_i32)res_Q10[i], 14), pCB_Wght_Q9[i]),
            (fl_i16)pCB_element[i], 7);
      pNLSF_Q15[i] = (fl_i16)silk_LIMIT(NLSF_Q15_tmp, 0, 32767);
   }
   silk_NLSF_stabilize(pNLSF_Q15, cb->deltaMin_Q15, cb->order);
}

#define GAIN_OFFSET ((MIN_QGAIN_DB*128)/6+16*128)
#define GAIN_SCALE_Q16 ((65536*(N_LEVELS_QGAIN-1))/(((MAX_QGAIN_DB-MIN_QGAIN_DB)*128)/6))
#define GAIN_INV_SCALE_Q16 ((65536*(((MAX_QGAIN_DB-MIN_QGAIN_DB)*128)/6))/(N_LEVELS_QGAIN-1))
static void silk_gains_quant(fl_i8 ind[MAX_NB_SUBFR], fl_i32 gain_Q16[MAX_NB_SUBFR], fl_i8 *prev_ind,
      const int conditional, const int nb_subfr) {
   int k, double_step_size_threshold;
   for (k = 0; k < nb_subfr; k++) {
      ind[k] = silk_SMULWB(GAIN_SCALE_Q16, silk_lin2log(gain_Q16[k])-GAIN_OFFSET);
      if (ind[k] < *prev_ind) ind[k]++;
      ind[k] = silk_LIMIT_int(ind[k], 0, N_LEVELS_QGAIN-1);
      if (k == 0 && conditional == 0) {
         ind[k] = silk_LIMIT_int(ind[k], *prev_ind+MIN_DELTA_GAIN_QUANT, N_LEVELS_QGAIN-1);
         *prev_ind = ind[k];
      } else {
         ind[k] = ind[k]-*prev_ind;
         double_step_size_threshold = 2*MAX_DELTA_GAIN_QUANT-N_LEVELS_QGAIN+*prev_ind;
         if (ind[k] > double_step_size_threshold)
            ind[k] = double_step_size_threshold+silk_RSHIFT(ind[k]-double_step_size_threshold+1, 1);
         ind[k] = silk_LIMIT_int(ind[k], MIN_DELTA_GAIN_QUANT, MAX_DELTA_GAIN_QUANT);
         if (ind[k] > double_step_size_threshold) {
            *prev_ind += silk_LSHIFT(ind[k], 1)-double_step_size_threshold;
            *prev_ind = silk_min_int(*prev_ind, N_LEVELS_QGAIN-1);
         } else *prev_ind += ind[k];
         ind[k] -= MIN_DELTA_GAIN_QUANT;
      }
      gain_Q16[k] = silk_log2lin(silk_min_32(silk_SMULWB(GAIN_INV_SCALE_Q16, *prev_ind)+GAIN_OFFSET, 3967));
   }
}
static void silk_gains_dequant(fl_i32 gain_Q16[MAX_NB_SUBFR], const fl_i8 ind[MAX_NB_SUBFR], fl_i8 *prev_ind,
      const int conditional, const int nb_subfr) {
   int k, ind_tmp, double_step_size_threshold;
   for (k = 0; k < nb_subfr; k++) {
      if (k == 0 && conditional == 0) {
         *prev_ind = silk_max_int(ind[k], *prev_ind-16);
      } else {
         ind_tmp = ind[k]+MIN_DELTA_GAIN_QUANT;
         double_step_size_threshold = 2*MAX_DELTA_GAIN_QUANT-N_LEVELS_QGAIN+*prev_ind;
         if (ind_tmp > double_step_size_threshold) *prev_ind += silk_LSHIFT(ind_tmp, 1)-double_step_size_threshold;
         else *prev_ind += ind_tmp;
      }
      *prev_ind = silk_LIMIT_int(*prev_ind, 0, N_LEVELS_QGAIN-1);
      gain_Q16[k] = silk_log2lin(silk_min_32(silk_SMULWB(GAIN_INV_SCALE_Q16, *prev_ind)+GAIN_OFFSET, 3967));
   }
}
static fl_i32 silk_gains_ID(const fl_i8 ind[MAX_NB_SUBFR], const int nb_subfr) {
   int k;
   fl_i32 gainsID = 0;
   for (k = 0; k < nb_subfr; k++) gainsID = silk_ADD_LSHIFT32(ind[k], gainsID, 8);
   return gainsID;
}

static void silk_decode_pitch(fl_i16 lagIndex, fl_i8 contourIndex, int pitch_lags[], const int Fs_kHz, const int nb_subfr) {
   int lag, k, min_lag, max_lag, cbk_size;
   const fl_i8 *Lag_CB_ptr;
   if (Fs_kHz == 8) {
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         Lag_CB_ptr = &fl_CB_lags_stage2[0][0];
         cbk_size = PE_NB_CBKS_STAGE2_EXT;
      } else {
         Lag_CB_ptr = &fl_CB_lags_stage2_10_ms[0][0];
         cbk_size = PE_NB_CBKS_STAGE2_10MS;
      }
   } else {
      if (nb_subfr == PE_MAX_NB_SUBFR) {
         Lag_CB_ptr = &fl_CB_lags_stage3[0][0];
         cbk_size = PE_NB_CBKS_STAGE3_MAX;
      } else {
         Lag_CB_ptr = &fl_CB_lags_stage3_10_ms[0][0];
         cbk_size = PE_NB_CBKS_STAGE3_10MS;
      }
   }
   min_lag = silk_SMULBB(PE_MIN_LAG_MS, Fs_kHz);
   max_lag = silk_SMULBB(PE_MAX_LAG_MS, Fs_kHz);
   lag = min_lag+lagIndex;
   for (k = 0; k < nb_subfr; k++) {
      pitch_lags[k] = lag+Lag_CB_ptr[k*cbk_size+contourIndex];
      pitch_lags[k] = silk_LIMIT(pitch_lags[k], min_lag, max_lag);
   }
}

/* Shell coder */
static void silk_combine_pulses(int *out, const int *in, const int len) {
   int k;
   for (k = 0; k < len; k++) out[k] = in[2*k]+in[2*k+1];
}
static void silk_encode_split(ec_ctx *enc, const int p_child1, const int p, const fl_u8 *shell_table) {
   if (p > 0) ec_enc_icdf(enc, p_child1, &shell_table[fl_shell_code_table_offsets[p]], 8);
}
static void silk_decode_split(fl_i16 *p_child1, fl_i16 *p_child2, ec_ctx *dec, const int p, const fl_u8 *shell_table) {
   if (p > 0) {
      p_child1[0] = ec_dec_icdf(dec, &shell_table[fl_shell_code_table_offsets[p]], 8);
      p_child2[0] = p-p_child1[0];
   } else {
      p_child1[0] = 0;
      p_child2[0] = 0;
   }
}
static void silk_shell_encoder(ec_ctx *enc, const int *pulses0) {
   int pulses1[8], pulses2[4], pulses3[2], pulses4[1];
   silk_combine_pulses(pulses1, pulses0, 8);
   silk_combine_pulses(pulses2, pulses1, 4);
   silk_combine_pulses(pulses3, pulses2, 2);
   silk_combine_pulses(pulses4, pulses3, 1);
   silk_encode_split(enc, pulses3[0], pulses4[0], fl_shell_code_table3);
   silk_encode_split(enc, pulses2[0], pulses3[0], fl_shell_code_table2);
   silk_encode_split(enc, pulses1[0], pulses2[0], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[0], pulses1[0], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[2], pulses1[1], fl_shell_code_table0);
   silk_encode_split(enc, pulses1[2], pulses2[1], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[4], pulses1[2], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[6], pulses1[3], fl_shell_code_table0);
   silk_encode_split(enc, pulses2[2], pulses3[1], fl_shell_code_table2);
   silk_encode_split(enc, pulses1[4], pulses2[2], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[8], pulses1[4], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[10], pulses1[5], fl_shell_code_table0);
   silk_encode_split(enc, pulses1[6], pulses2[3], fl_shell_code_table1);
   silk_encode_split(enc, pulses0[12], pulses1[6], fl_shell_code_table0);
   silk_encode_split(enc, pulses0[14], pulses1[7], fl_shell_code_table0);
}
static void silk_shell_decoder(fl_i16 *pulses0, ec_ctx *dec, const int pulses4) {
   fl_i16 pulses3[2], pulses2[4], pulses1[8];
   silk_decode_split(&pulses3[0], &pulses3[1], dec, pulses4, fl_shell_code_table3);
   silk_decode_split(&pulses2[0], &pulses2[1], dec, pulses3[0], fl_shell_code_table2);
   silk_decode_split(&pulses1[0], &pulses1[1], dec, pulses2[0], fl_shell_code_table1);
   silk_decode_split(&pulses0[0], &pulses0[1], dec, pulses1[0], fl_shell_code_table0);
   silk_decode_split(&pulses0[2], &pulses0[3], dec, pulses1[1], fl_shell_code_table0);
   silk_decode_split(&pulses1[2], &pulses1[3], dec, pulses2[1], fl_shell_code_table1);
   silk_decode_split(&pulses0[4], &pulses0[5], dec, pulses1[2], fl_shell_code_table0);
   silk_decode_split(&pulses0[6], &pulses0[7], dec, pulses1[3], fl_shell_code_table0);
   silk_decode_split(&pulses2[2], &pulses2[3], dec, pulses3[1], fl_shell_code_table2);
   silk_decode_split(&pulses1[4], &pulses1[5], dec, pulses2[2], fl_shell_code_table1);
   silk_decode_split(&pulses0[8], &pulses0[9], dec, pulses1[4], fl_shell_code_table0);
   silk_decode_split(&pulses0[10], &pulses0[11], dec, pulses1[5], fl_shell_code_table0);
   silk_decode_split(&pulses1[6], &pulses1[7], dec, pulses2[3], fl_shell_code_table1);
   silk_decode_split(&pulses0[12], &pulses0[13], dec, pulses1[6], fl_shell_code_table0);
   silk_decode_split(&pulses0[14], &pulses0[15], dec, pulses1[7], fl_shell_code_table0);
}

static void silk_encode_signs(ec_ctx *enc, const fl_i8 pulses[], int length, const int signalType,
      const int quantOffsetType, const int sum_pulses[MAX_NB_SHELL_BLOCKS]) {
   int i, j, p;
   fl_u8 icdf[2];
   const fl_i8 *q_ptr = pulses;
   const fl_u8 *icdf_ptr;
   icdf[1] = 0;
   i = silk_SMULBB(7, silk_ADD_LSHIFT32(quantOffsetType, signalType, 1));
   icdf_ptr = &fl_sign_iCDF[i];
   length = silk_RSHIFT(length+SHELL_CODEC_FRAME_LENGTH/2, LOG2_SHELL_CODEC_FRAME_LENGTH);
   for (i = 0; i < length; i++) {
      p = sum_pulses[i];
      if (p > 0) {
         icdf[0] = icdf_ptr[silk_min(p&0x1F, 6)];
         for (j = 0; j < SHELL_CODEC_FRAME_LENGTH; j++)
            if (q_ptr[j] != 0) ec_enc_icdf(enc, silk_RSHIFT(q_ptr[j], 15)+1, icdf, 8);
      }
      q_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
}
static void silk_decode_signs(ec_ctx *dec, fl_i16 pulses[], int length, const int signalType,
      const int quantOffsetType, const int sum_pulses[MAX_NB_SHELL_BLOCKS]) {
   int i, j, p;
   fl_u8 icdf[2];
   fl_i16 *q_ptr = pulses;
   const fl_u8 *icdf_ptr;
   icdf[1] = 0;
   i = silk_SMULBB(7, silk_ADD_LSHIFT32(quantOffsetType, signalType, 1));
   icdf_ptr = &fl_sign_iCDF[i];
   length = silk_RSHIFT(length+SHELL_CODEC_FRAME_LENGTH/2, LOG2_SHELL_CODEC_FRAME_LENGTH);
   for (i = 0; i < length; i++) {
      p = sum_pulses[i];
      if (p > 0) {
         icdf[0] = icdf_ptr[silk_min(p&0x1F, 6)];
         for (j = 0; j < SHELL_CODEC_FRAME_LENGTH; j++)
            if (q_ptr[j] > 0) q_ptr[j] *= silk_LSHIFT(ec_dec_icdf(dec, icdf, 8), 1)-1;
      }
      q_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
}

/* ---- SILK resampler ---- */
#define SILK_RESAMPLER_MAX_FIR_ORDER 36
#define SILK_RESAMPLER_MAX_IIR_ORDER 6
#define RESAMPLER_MAX_BATCH_SIZE_MS 10
typedef struct {
   fl_i32 sIIR[SILK_RESAMPLER_MAX_IIR_ORDER];
   union { fl_i32 i32[SILK_RESAMPLER_MAX_FIR_ORDER]; fl_i16 i16[SILK_RESAMPLER_MAX_FIR_ORDER]; } sFIR;
   fl_i16 delayBuf[96];
   int resampler_function, batchSize;
   fl_i32 invRatio_Q16;
   int FIR_Order, FIR_Fracs, Fs_in_kHz, Fs_out_kHz, inputDelay;
   const fl_i16 *Coefs;
} silk_resampler_state;

static const fl_i8 silk_delay_matrix_enc[6][3] = {{6, 0, 3}, {0, 7, 3}, {0, 1, 10}, {0, 2, 6}, {18, 10, 12}, {0, 0, 44}};
static const fl_i8 silk_delay_matrix_dec[3][6] = {{4, 0, 2, 0, 0, 0}, {0, 9, 4, 7, 4, 4}, {0, 3, 12, 7, 7, 7}};
#define rateID(R) IMIN(5, ((((R)>>12)-((R) > 16000))>>((R) > 24000))-1)

static int silk_resampler_init(silk_resampler_state *S, fl_i32 Fs_Hz_in, fl_i32 Fs_Hz_out, int forEnc) {
   int up2x;
   memset(S, 0, sizeof(*S));
   if (forEnc) S->inputDelay = silk_delay_matrix_enc[rateID(Fs_Hz_in)][rateID(Fs_Hz_out)];
   else S->inputDelay = silk_delay_matrix_dec[rateID(Fs_Hz_in)][rateID(Fs_Hz_out)];
   S->Fs_in_kHz = silk_DIV32_16(Fs_Hz_in, 1000);
   S->Fs_out_kHz = silk_DIV32_16(Fs_Hz_out, 1000);
   S->batchSize = S->Fs_in_kHz*RESAMPLER_MAX_BATCH_SIZE_MS;
   up2x = 0;
   if (Fs_Hz_out > Fs_Hz_in) {
      if (Fs_Hz_out == silk_MUL(Fs_Hz_in, 2)) S->resampler_function = 1;
      else {
         S->resampler_function = 2;
         up2x = 1;
      }
   } else if (Fs_Hz_out < Fs_Hz_in) {
      S->resampler_function = 3;
      if (silk_MUL(Fs_Hz_out, 4) == silk_MUL(Fs_Hz_in, 3)) {
         S->FIR_Fracs = 3; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR0; S->Coefs = fl_Resampler_3_4_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 3) == silk_MUL(Fs_Hz_in, 2)) {
         S->FIR_Fracs = 2; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR0; S->Coefs = fl_Resampler_2_3_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 2) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR1; S->Coefs = fl_Resampler_1_2_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 3) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_3_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 4) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_4_COEFS;
      } else if (silk_MUL(Fs_Hz_out, 6) == Fs_Hz_in) {
         S->FIR_Fracs = 1; S->FIR_Order = RESAMPLER_DOWN_ORDER_FIR2; S->Coefs = fl_Resampler_1_6_COEFS;
      } else return -1;
   } else S->resampler_function = 0;
   S->invRatio_Q16 = silk_LSHIFT32(silk_DIV32(silk_LSHIFT32(Fs_Hz_in, 14+up2x), Fs_Hz_out), 2);
   while (silk_SMULWW(S->invRatio_Q16, Fs_Hz_out) < silk_LSHIFT32(Fs_Hz_in, up2x)) S->invRatio_Q16++;
   return 0;
}

static void silk_resampler_private_up2_HQ(fl_i32 *S, fl_i16 *out, const fl_i16 *in, fl_i32 len) {
   fl_i32 k, in32, out32_1, out32_2, Y, X;
   for (k = 0; k < len; k++) {
      in32 = silk_LSHIFT((fl_i32)in[k], 10);
      Y = in32-S[0];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_0[0]);
      out32_1 = S[0]+X;
      S[0] = in32+X;
      Y = out32_1-S[1];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_0[1]);
      out32_2 = S[1]+X;
      S[1] = out32_1+X;
      Y = out32_2-S[2];
      X = silk_SMLAWB(Y, Y, silk_resampler_up2_hq_0[2]);
      out32_1 = S[2]+X;
      S[2] = out32_2+X;
      out[2*k] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(out32_1, 10));
      Y = in32-S[3];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_1[0]);
      out32_1 = S[3]+X;
      S[3] = in32+X;
      Y = out32_1-S[4];
      X = silk_SMULWB(Y, silk_resampler_up2_hq_1[1]);
      out32_2 = S[4]+X;
      S[4] = out32_1+X;
      Y = out32_2-S[5];
      X = silk_SMLAWB(Y, Y, silk_resampler_up2_hq_1[2]);
      out32_1 = S[5]+X;
      S[5] = out32_2+X;
      out[2*k+1] = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(out32_1, 10));
   }
}

static fl_i16 *silk_resampler_IIR_FIR_INTERPOL(fl_i16 *out, fl_i16 *buf, fl_i32 max_index_Q16, fl_i32 index_increment_Q16) {
   fl_i32 index_Q16, res_Q15, table_index;
   fl_i16 *buf_ptr;
   for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
      table_index = silk_SMULWB(index_Q16&0xFFFF, 12);
      buf_ptr = &buf[index_Q16>>16];
      res_Q15 = silk_SMULBB(buf_ptr[0], fl_resampler_frac_FIR_12[table_index][0]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[1], fl_resampler_frac_FIR_12[table_index][1]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[2], fl_resampler_frac_FIR_12[table_index][2]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[3], fl_resampler_frac_FIR_12[table_index][3]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[4], fl_resampler_frac_FIR_12[11-table_index][3]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[5], fl_resampler_frac_FIR_12[11-table_index][2]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[6], fl_resampler_frac_FIR_12[11-table_index][1]);
      res_Q15 = silk_SMLABB(res_Q15, buf_ptr[7], fl_resampler_frac_FIR_12[11-table_index][0]);
      *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q15, 15));
   }
   return out;
}
static void silk_resampler_private_IIR_FIR(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   fl_i32 nSamplesIn, max_index_Q16, index_increment_Q16;
   fl_i16 buf[2*RESAMPLER_MAX_BATCH_SIZE_MS*48+RESAMPLER_ORDER_FIR_12];
   memcpy(buf, S->sFIR.i16, RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
   index_increment_Q16 = S->invRatio_Q16;
   while (1) {
      nSamplesIn = silk_min(inLen, S->batchSize);
      silk_resampler_private_up2_HQ(S->sIIR, &buf[RESAMPLER_ORDER_FIR_12], in, nSamplesIn);
      max_index_Q16 = silk_LSHIFT32(nSamplesIn, 16+1);
      out = silk_resampler_IIR_FIR_INTERPOL(out, buf, max_index_Q16, index_increment_Q16);
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if (inLen > 0) memcpy(buf, &buf[nSamplesIn<<1], RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
      else break;
   }
   memcpy(S->sFIR.i16, &buf[nSamplesIn<<1], RESAMPLER_ORDER_FIR_12*sizeof(fl_i16));
}

static void silk_resampler_private_AR2(fl_i32 S[], fl_i32 out_Q8[], const fl_i16 in[], const fl_i16 A_Q14[], fl_i32 len) {
   fl_i32 k, out32;
   for (k = 0; k < len; k++) {
      out32 = silk_ADD_LSHIFT32(S[0], (fl_i32)in[k], 8);
      out_Q8[k] = out32;
      out32 = silk_LSHIFT(out32, 2);
      S[0] = silk_SMLAWB(S[1], out32, A_Q14[0]);
      S[1] = silk_SMULWB(out32, A_Q14[1]);
   }
}
static fl_i16 *silk_resampler_down_FIR_INTERPOL(fl_i16 *out, fl_i32 *buf, const fl_i16 *FIR_Coefs, int FIR_Order,
      int FIR_Fracs, fl_i32 max_index_Q16, fl_i32 index_increment_Q16) {
   fl_i32 index_Q16, res_Q6, *buf_ptr, interpol_ind;
   const fl_i16 *ip;
   int j;
   switch (FIR_Order) {
   case RESAMPLER_DOWN_ORDER_FIR0:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         interpol_ind = silk_SMULWB(index_Q16&0xFFFF, FIR_Fracs);
         ip = &FIR_Coefs[RESAMPLER_DOWN_ORDER_FIR0/2*interpol_ind];
         res_Q6 = silk_SMULWB(buf_ptr[0], ip[0]);
         for (j = 1; j < 9; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j], ip[j]);
         ip = &FIR_Coefs[RESAMPLER_DOWN_ORDER_FIR0/2*(FIR_Fracs-1-interpol_ind)];
         for (j = 0; j < 9; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[17-j], ip[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   case RESAMPLER_DOWN_ORDER_FIR1:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         res_Q6 = silk_SMULWB(buf_ptr[0]+buf_ptr[23], FIR_Coefs[0]);
         for (j = 1; j < 12; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j]+buf_ptr[23-j], FIR_Coefs[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   case RESAMPLER_DOWN_ORDER_FIR2:
      for (index_Q16 = 0; index_Q16 < max_index_Q16; index_Q16 += index_increment_Q16) {
         buf_ptr = buf+silk_RSHIFT(index_Q16, 16);
         res_Q6 = silk_SMULWB(buf_ptr[0]+buf_ptr[35], FIR_Coefs[0]);
         for (j = 1; j < 18; j++) res_Q6 = silk_SMLAWB(res_Q6, buf_ptr[j]+buf_ptr[35-j], FIR_Coefs[j]);
         *out++ = (fl_i16)silk_SAT16(silk_RSHIFT_ROUND(res_Q6, 6));
      }
      break;
   }
   return out;
}
static void silk_resampler_private_down_FIR(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   fl_i32 nSamplesIn, max_index_Q16, index_increment_Q16;
   fl_i32 buf[RESAMPLER_MAX_BATCH_SIZE_MS*48+SILK_RESAMPLER_MAX_FIR_ORDER];
   const fl_i16 *FIR_Coefs;
   memcpy(buf, S->sFIR.i32, S->FIR_Order*sizeof(fl_i32));
   FIR_Coefs = &S->Coefs[2];
   index_increment_Q16 = S->invRatio_Q16;
   while (1) {
      nSamplesIn = silk_min(inLen, S->batchSize);
      silk_resampler_private_AR2(S->sIIR, &buf[S->FIR_Order], in, S->Coefs, nSamplesIn);
      max_index_Q16 = silk_LSHIFT32(nSamplesIn, 16);
      out = silk_resampler_down_FIR_INTERPOL(out, buf, FIR_Coefs, S->FIR_Order, S->FIR_Fracs, max_index_Q16,
            index_increment_Q16);
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if (inLen > 1) memcpy(buf, &buf[nSamplesIn], S->FIR_Order*sizeof(fl_i32));
      else break;
   }
   memcpy(S->sFIR.i32, &buf[nSamplesIn], S->FIR_Order*sizeof(fl_i32));
}

static int silk_resampler(silk_resampler_state *S, fl_i16 out[], const fl_i16 in[], fl_i32 inLen) {
   int nSamples = S->Fs_in_kHz-S->inputDelay;
   memcpy(&S->delayBuf[S->inputDelay], in, nSamples*sizeof(fl_i16));
   switch (S->resampler_function) {
   case 1:
      silk_resampler_private_up2_HQ(S->sIIR, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_up2_HQ(S->sIIR, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   case 2:
      silk_resampler_private_IIR_FIR(S, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_IIR_FIR(S, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   case 3:
      silk_resampler_private_down_FIR(S, out, S->delayBuf, S->Fs_in_kHz);
      silk_resampler_private_down_FIR(S, &out[S->Fs_out_kHz], &in[nSamples], inLen-S->Fs_in_kHz);
      break;
   default:
      memcpy(out, S->delayBuf, S->Fs_in_kHz*sizeof(fl_i16));
      memcpy(&out[S->Fs_out_kHz], &in[nSamples], (inLen-S->Fs_in_kHz)*sizeof(fl_i16));
   }
   memcpy(S->delayBuf, &in[inLen-S->inputDelay], S->inputDelay*sizeof(fl_i16));
   return 0;
}
