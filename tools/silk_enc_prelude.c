#define silk_encoder_state_Fxx silk_encoder_state_FLP
#define silk_encode_frame_Fxx silk_encode_frame_FLP
#define silk_encode_do_VAD_Fxx silk_encode_do_VAD_FLP
#define silk_VAD_GetSA_Q8(a, b, arch) silk_VAD_GetSA_Q8_c(a, b)
#define silk_VQ_WMat_EC(a, b, c, d, e, f, g, h, i, j, k, l, arch) silk_VQ_WMat_EC_c(a, b, c, d, e, f, g, h, i, j, k, l)
#define silk_biquad_alt_stride2(a, b, c, d, e, f, arch) silk_biquad_alt_stride2_c(a, b, c, d, e, f)
#define RES2INT16(a) ((opus_int16)lrintf(FL_MAX(-32768.f, FL_MIN(32767.f, (a)*32768.f))))
#define silk_memcpy memcpy
#define silk_memset memset
#define silk_memmove memmove
#define celt_assert(x)
#define silk_assert(x)
#define celt_assert2(x, y)
#define SAVE_STACK
#define RESTORE_STACK
#define VARDECL(type, var) type *var
#define ALLOC(var, size, type) type var##_buf_[(size) > 0 ? (size) : 1]; var = var##_buf_
#define SILK_NO_ERROR 0
#define SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES -101
#define SILK_ENC_FS_NOT_SUPPORTED -102
#define SILK_ENC_PACKET_SIZE_NOT_SUPPORTED -103
#define SILK_ENC_INVALID_LOSS_RATE -105
#define SILK_ENC_INVALID_COMPLEXITY_SETTING -106
#define SILK_ENC_INVALID_INBAND_FEC_SETTING -107
#define SILK_ENC_INVALID_DTX_SETTING -108
#define SILK_ENC_INVALID_CBR_SETTING -109
#define SILK_ENC_INTERNAL_ERROR -110
#define SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR -111
#define opus_likely(x) (x)
#define opus_unlikely(x) (x)
#define silk_NLSF2A(a, b, c, arch) (silk_NLSF2A)(a, b, c)
#define opus_res float
#define silk_LPC_analysis_filter(a, b, c, d, e, arch) (silk_LPC_analysis_filter)(a, b, c, d, e)
#define silk_resampler_down2_0 9872
#define silk_resampler_down2_1 (39809-65536)
static void silk_float2short_array(fl_i16 *out, const float *in, fl_i32 length) {
   fl_i32 k;
   for (k = length-1; k >= 0; k--) out[k] = (fl_i16)silk_SAT16(lrintf(in[k]));
}
static void silk_short2float_array(float *out, const fl_i16 *in, fl_i32 length) {
   fl_i32 k;
   for (k = length-1; k >= 0; k--) out[k] = (float)in[k];
}
#define silk_short_prediction_create_arch_coef(a, b, c) ((void)(a), (void)(b), (void)(c))
