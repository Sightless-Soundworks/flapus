/* ---- SILK encoder: fixed-point parts (ported from libopus, see LICENSE notes) ---- */
#define silk_encoder_state_Fxx silk_encoder_state_FLP
#define silk_encode_frame_Fxx silk_encode_frame_FLP
#define silk_encode_do_VAD_Fxx silk_encode_do_VAD_FLP
#define silk_VAD_GetSA_Q8(a, b, arch) silk_VAD_GetSA_Q8_c(a, b)
#define silk_VQ_WMat_EC(a, b, c, d, e, f, g, h, i, j, k, l, arch) silk_VQ_WMat_EC_c(a, b, c, d, e, f, g, h, i, j, k, l)
#define silk_biquad_alt_stride2(a, b, c, d, e, f, arch) silk_biquad_alt_stride2_c(a, b, c, d, e, f)
#define RES2INT16(a) ((fl_i16)lrintf(FL_MAX(-32768.f, FL_MIN(32767.f, (a)*32768.f))))
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

#define ENCODER_NUM_CHANNELS 2
#define DECODER_NUM_CHANNELS 2
#define MIN_TARGET_RATE_BPS 5000
#define MAX_TARGET_RATE_BPS 80000
#define LBRR_NB_MIN_RATE_BPS 12000
#define LBRR_MB_MIN_RATE_BPS 14000
#define LBRR_WB_MIN_RATE_BPS 16000
#define NB_SPEECH_FRAMES_BEFORE_DTX 10
#define MAX_CONSECUTIVE_DTX 20
#define DTX_ACTIVITY_THRESHOLD 0.1f
#define VAD_NO_DECISION -1
#define VAD_NO_ACTIVITY 0
#define VAD_ACTIVITY 1
#define STEREO_RATIO_SMOOTH_COEF 0.01
#define LA_PITCH_MS 2
#define LA_PITCH_MAX ( LA_PITCH_MS * MAX_FS_KHZ )
#define MAX_FIND_PITCH_LPC_ORDER 16
#define FIND_PITCH_LPC_WIN_MS ( 20 + (LA_PITCH_MS << 1) )
#define FIND_PITCH_LPC_WIN_MS_2_SF ( 10 + (LA_PITCH_MS << 1) )
#define FIND_PITCH_LPC_WIN_MAX ( FIND_PITCH_LPC_WIN_MS * MAX_FS_KHZ )
#define LA_SHAPE_MS 5
#define LA_SHAPE_MAX ( LA_SHAPE_MS * MAX_FS_KHZ )
#define SHAPE_LPC_WIN_MAX ( 15 * MAX_FS_KHZ )
#define MAX_PREDICTION_POWER_GAIN_AFTER_RESET 1e2f
#define USE_HARM_SHAPING 1
#define MAX_SHAPE_LPC_ORDER 24
#define HARM_SHAPE_FIR_TAPS 3
#define MAX_DEL_DEC_STATES 4
#define LTP_BUF_LENGTH 512
#define LTP_MASK ( LTP_BUF_LENGTH - 1 )
#define DECISION_DELAY 40
#define MAX_MATRIX_SIZE MAX_LPC_ORDER
#define NSQ_LPC_BUF_LENGTH MAX_LPC_ORDER
#define VAD_N_BANDS 4
#define VAD_INTERNAL_SUBFRAMES_LOG2 2
#define VAD_INTERNAL_SUBFRAMES ( 1 << VAD_INTERNAL_SUBFRAMES_LOG2 )
#define VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 1024
#define VAD_NOISE_LEVELS_BIAS 50
#define VAD_NEGATIVE_OFFSET_Q5 128
#define VAD_SNR_FACTOR_Q16 45000
#define VAD_SNR_SMOOTH_COEF_Q18 4096
#define NLSF_W_Q 2
#define NLSF_VQ_MAX_VECTORS 32
#define NLSF_QUANT_MAX_AMPLITUDE_EXT 10
#define NLSF_QUANT_DEL_DEC_STATES_LOG2 2
#define NLSF_QUANT_DEL_DEC_STATES ( 1 << NLSF_QUANT_DEL_DEC_STATES_LOG2 )
#define TRANSITION_TIME_MS 5120
#define TRANSITION_FRAMES ( TRANSITION_TIME_MS / MAX_FRAME_LENGTH_MS )
#define TRANSITION_INT_STEPS ( TRANSITION_FRAMES / ( TRANSITION_INT_NUM - 1 ) )
#define BITRESERVOIR_DECAY_TIME_MS 500
#define FIND_PITCH_WHITE_NOISE_FRACTION 1e-3f
#define FIND_PITCH_BANDWIDTH_EXPANSION 0.99f
#define FIND_LPC_COND_FAC 1e-5f
#define MAX_SUM_LOG_GAIN_DB 250.0f
#define LTP_CORR_INV_MAX 0.03f
#define VARIABLE_HP_SMTH_COEF1 0.1f
#define VARIABLE_HP_SMTH_COEF2 0.015f
#define VARIABLE_HP_MAX_DELTA_FREQ 0.4f
#define VARIABLE_HP_MIN_CUTOFF_HZ 60
#define VARIABLE_HP_MAX_CUTOFF_HZ 100
#define SPEECH_ACTIVITY_DTX_THRES 0.05f
#define LBRR_SPEECH_ACTIVITY_THRES 0.3f
#define BG_SNR_DECR_dB 2.0f
#define HARM_SNR_INCR_dB 2.0f
#define SPARSE_SNR_INCR_dB 2.0f
#define ENERGY_VARIATION_THRESHOLD_QNT_OFFSET 0.6f
#define WARPING_MULTIPLIER 0.015f
#define SHAPE_WHITE_NOISE_FRACTION 3e-5f
#define BANDWIDTH_EXPANSION 0.94f
#define HARMONIC_SHAPING 0.3f
#define HIGH_RATE_OR_LOW_QUALITY_HARMONIC_SHAPING 0.2f
#define HP_NOISE_COEF 0.25f
#define HARM_HP_NOISE_COEF 0.35f
#define INPUT_TILT 0.05f
#define HIGH_RATE_INPUT_TILT 0.1f
#define LOW_FREQ_SHAPING 4.0f
#define LOW_QUALITY_LOW_FREQ_SHAPING_DECR 0.5f
#define SUBFR_SMTH_COEF 0.4f
#define LAMBDA_OFFSET 1.2f
#define LAMBDA_SPEECH_ACT -0.2f
#define LAMBDA_DELAYED_DECISIONS -0.05f
#define LAMBDA_INPUT_QUALITY -0.1f
#define LAMBDA_CODING_QUALITY -0.2f
#define LAMBDA_QUANT_OFFSET 0.8f
#define REDUCE_BITRATE_10_MS_BPS 2200
#define MAX_BANDWIDTH_SWITCH_DELAY_MS 5000
#define PE_MAX_FS_KHZ 16
#define PE_SUBFR_LENGTH_MS 5
#define PE_LTP_MEM_LENGTH_MS ( 4 * PE_SUBFR_LENGTH_MS )
#define PE_MAX_FRAME_LENGTH_MS ( PE_LTP_MEM_LENGTH_MS + PE_MAX_NB_SUBFR * PE_SUBFR_LENGTH_MS )
#define PE_MAX_FRAME_LENGTH ( PE_MAX_FRAME_LENGTH_MS * PE_MAX_FS_KHZ )
#define PE_MAX_FRAME_LENGTH_ST_1 ( PE_MAX_FRAME_LENGTH >> 2 )
#define PE_MAX_FRAME_LENGTH_ST_2 ( PE_MAX_FRAME_LENGTH >> 1 )
#define PE_MAX_LAG ( PE_MAX_LAG_MS * PE_MAX_FS_KHZ )
#define PE_MIN_LAG ( PE_MIN_LAG_MS * PE_MAX_FS_KHZ )
#define PE_D_SRCH_LENGTH 24
#define PE_NB_STAGE3_LAGS 5
#define PE_SHORTLAG_BIAS 0.2f
#define PE_PREVLAG_BIAS 0.2f
#define PE_FLATCONTOUR_BIAS 0.05f
#define SILK_PE_MIN_COMPLEX 0
#define SILK_PE_MID_COMPLEX 1
#define silk_DWORD_ALIGN __attribute__((aligned(4)))
#define silk_MUL_uint(a32,b32) silk_MUL(a32, b32)
#define silk_MLA_uint(a32,b32,c32) silk_MLA(a32, b32, c32)
#define silk_ADD16(a,b) ((a) + (b))
#define silk_ADD64(a,b) ((a) + (b))
#define silk_SUB16(a,b) ((a) - (b))
#define silk_SUB64(a,b) ((a) - (b))
#define silk_SAT8(a) ((a) > silk_int8_MAX ? silk_int8_MAX : \
                                 ((a) < silk_int8_MIN ? silk_int8_MIN : (a)))
#define silk_CHECK_FIT8(a) (a)
#define silk_CHECK_FIT16(a) (a)
#define silk_CHECK_FIT32(a) (a)
#define silk_ADD_SAT64(a,b) ((((a) + (b)) & 0x8000000000000000LL) == 0 ? \
                                 ((((a) & (b)) & 0x8000000000000000LL) != 0 ? silk_int64_MIN : (a)+(b)) : \
                                 ((((a) | (b)) & 0x8000000000000000LL) == 0 ? silk_int64_MAX : (a)+(b)) )
#define silk_SUB_SAT64(a,b) ((((a)-(b)) & 0x8000000000000000LL) == 0 ? \
                                 (( (a) & ((b)^0x8000000000000000LL) & 0x8000000000000000LL) ? silk_int64_MIN : (a)-(b)) : \
                                 ((((a)^0x8000000000000000LL) & (b) & 0x8000000000000000LL) ? silk_int64_MAX : (a)-(b)) )
#define silk_ADD_POS_SAT8(a,b) ((((a)+(b)) & 0x80) ? silk_int8_MAX : ((a)+(b)))
#define silk_ADD_POS_SAT16(a,b) ((((a)+(b)) & 0x8000) ? silk_int16_MAX : ((a)+(b)))
#define silk_LSHIFT8(a,shift) ((fl_i8)((fl_u8)(a)<<(shift)))
#define silk_LSHIFT16(a,shift) ((fl_i16)((fl_u16)(a)<<(shift)))
#define silk_RSHIFT8(a,shift) ((a)>>(shift))
#define silk_RSHIFT16(a,shift) ((a)>>(shift))
#define silk_LSHIFT_uint(a,shift) ((a) << (shift))
#define silk_RSHIFT_uint(a,shift) ((a) >> (shift))
#define silk_ADD_LSHIFT(a,b,shift) ((a) + silk_LSHIFT((b), (shift)))
#define silk_ADD_LSHIFT_uint(a,b,shift) ((a) + silk_LSHIFT_uint((b), (shift)))
#define silk_ADD_RSHIFT(a,b,shift) ((a) + silk_RSHIFT((b), (shift)))
#define silk_NSHIFT_MUL_32_32(a,b) ( -(31- (32-silk_CLZ32(silk_abs(a)) + (32-silk_CLZ32(silk_abs(b))))) )
#define silk_NSHIFT_MUL_16_16(a,b) ( -(15- (16-silk_CLZ16(silk_abs(a)) + (16-silk_CLZ16(silk_abs(b))))) )
#define silk_abs_int(a) (((a) ^ ((a) >> (8 * sizeof(a) - 1))) - ((a) >> (8 * sizeof(a) - 1)))
#define silk_abs_int64(a) (((a) > 0) ? (a) : -(a))
#define silk_sign(a) ((a) > 0 ? 1 : ( (a) < 0 ? -1 : 0 ))
#define RAND_MULTIPLIER 196314165
#define RAND_INCREMENT 907633515
#define silk_burg_modified(res_nrg,res_nrg_Q,A_Q16,x,minInvGain_Q30,subfr_length,nb_subfr,D,arch) \
   ((void)(arch), silk_burg_modified_c(res_nrg, res_nrg_Q, A_Q16, x, minInvGain_Q30, subfr_length, nb_subfr, D, arch))
#define silk_inner_prod16(inVec1,inVec2,len,arch) \
   ((void)(arch),silk_inner_prod16_c(inVec1, inVec2, len))
#define matrix_ptr(Matrix_base_adr,row,column,N) \
   (*((Matrix_base_adr) + ((row)*(N)+(column))))
#define matrix_adr(Matrix_base_adr,row,column,N) \
    ((Matrix_base_adr) + ((row)*(N)+(column)))
#define matrix_c_ptr(Matrix_base_adr,row,column,M) \
   (*((Matrix_base_adr) + ((row)+(M)*(column))))
#define RESAMPLER_MAX_FS_KHZ 48
#define RESAMPLER_MAX_BATCH_SIZE_IN ( RESAMPLER_MAX_BATCH_SIZE_MS * RESAMPLER_MAX_FS_KHZ )

typedef struct {
   fl_i16 xq[ 2 * MAX_FRAME_LENGTH ];
   fl_i32 sLTP_shp_Q14[ 2 * MAX_FRAME_LENGTH ];
   fl_i32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
   fl_i32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
   fl_i32 sLF_AR_shp_Q14;
   fl_i32 sDiff_shp_Q14;
   int lagPrev;
   int sLTP_buf_idx;
   int sLTP_shp_buf_idx;
   fl_i32 rand_seed;
   fl_i32 prev_gain_Q16;
   int rewhite_flag;
} silk_nsq_state;

typedef struct {
   fl_i32 AnaState[ 2 ];
   fl_i32 AnaState1[ 2 ];
   fl_i32 AnaState2[ 2 ];
   fl_i32 XnrgSubfr[ VAD_N_BANDS ];
   fl_i32 NrgRatioSmth_Q8[ VAD_N_BANDS ];
   fl_i16 HPstate;
   fl_i32 NL[ VAD_N_BANDS ];
   fl_i32 inv_NL[ VAD_N_BANDS ];
   fl_i32 NoiseLevelBias[ VAD_N_BANDS ];
   fl_i32 counter;
} silk_VAD_state;

typedef struct {
   fl_i32 In_LP_State[ 2 ];
   fl_i32 transition_frame_no;
   int mode;
   fl_i32 saved_fs_kHz;
} silk_LP_state;

typedef struct {
   fl_i16 pred_prev_Q13[ 2 ];
   fl_i16 sMid[ 2 ];
   fl_i16 sSide[ 2 ];
   fl_i32 mid_side_amp_Q0[ 4 ];
   fl_i16 smth_width_Q14;
   fl_i16 width_prev_Q14;
   fl_i16 silent_side_len;
   fl_i8 predIx[ MAX_FRAMES_PER_PACKET ][ 2 ][ 3 ];
   fl_i8 mid_only_flags[ MAX_FRAMES_PER_PACKET ];
} stereo_enc_state;

typedef struct {
   fl_i8 GainsIndices[ MAX_NB_SUBFR ];
   fl_i8 LTPIndex[ MAX_NB_SUBFR ];
   fl_i8 NLSFIndices[ MAX_LPC_ORDER + 1 ];
   fl_i16 lagIndex;
   fl_i8 contourIndex;
   fl_i8 signalType;
   fl_i8 quantOffsetType;
   fl_i8 NLSFInterpCoef_Q2;
   fl_i8 PERIndex;
   fl_i8 LTP_scaleIndex;
   fl_i8 Seed;
} SideInfoIndices;

typedef struct {
   fl_i32 In_HP_State[ 2 ];
   fl_i32 variable_HP_smth1_Q15;
   fl_i32 variable_HP_smth2_Q15;
   silk_LP_state sLP;
   silk_VAD_state sVAD;
   silk_nsq_state sNSQ;
   fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ];
   int speech_activity_Q8;
   int allow_bandwidth_switch;
   fl_i8 LBRRprevLastGainIndex;
   fl_i8 prevSignalType;
   int prevLag;
   int pitch_LPC_win_length;
   int max_pitch_lag;
   fl_i32 API_fs_Hz;
   fl_i32 prev_API_fs_Hz;
   int maxInternal_fs_Hz;
   int minInternal_fs_Hz;
   int desiredInternal_fs_Hz;
   int fs_kHz;
   int nb_subfr;
   int frame_length;
   int subfr_length;
   int ltp_mem_length;
   int la_pitch;
   int la_shape;
   int shapeWinLength;
   fl_i32 TargetRate_bps;
   int PacketSize_ms;
   int PacketLoss_perc;
   fl_i32 frameCounter;
   int Complexity;
   int nStatesDelayedDecision;
   int useInterpolatedNLSFs;
   int shapingLPCOrder;
   int predictLPCOrder;
   int pitchEstimationComplexity;
   int pitchEstimationLPCOrder;
   fl_i32 pitchEstimationThreshold_Q16;
   fl_i32 sum_log_gain_Q7;
   int NLSF_MSVQ_Survivors;
   int first_frame_after_reset;
   int controlled_since_last_payload;
   int warping_Q16;
   int useCBR;
   int prefillFlag;
   const fl_u8 *pitch_lag_low_bits_iCDF;
   const fl_u8 *pitch_contour_iCDF;
   const fl_nlsf_cb *psNLSF_CB;
   int input_quality_bands_Q15[ VAD_N_BANDS ];
   int input_tilt_Q15;
   int SNR_dB_Q7;
   fl_i8 VAD_flags[ MAX_FRAMES_PER_PACKET ];
   fl_i8 LBRR_flag;
   int LBRR_flags[ MAX_FRAMES_PER_PACKET ];
   SideInfoIndices indices;
   fl_i8 pulses[ MAX_FRAME_LENGTH ];
   int arch;
   fl_i16 inputBuf[ MAX_FRAME_LENGTH + 2 ];
   int inputBufIx;
   int nFramesPerPacket;
   int nFramesEncoded;
   int nChannelsAPI;
   int nChannelsInternal;
   int channelNb;
   int frames_since_onset;
   int ec_prevSignalType;
   fl_i16 ec_prevLagIndex;
   silk_resampler_state resampler_state;
   int useDTX;
   int inDTX;
   int noSpeechCounter;
   int useInBandFEC;
   int LBRR_enabled;
   int LBRR_GainIncreases;
   SideInfoIndices indices_LBRR[ MAX_FRAMES_PER_PACKET ];
   fl_i8 pulses_LBRR[ MAX_FRAMES_PER_PACKET ][ MAX_FRAME_LENGTH ];
} silk_encoder_state;

typedef struct {
   fl_i8 LastGainIndex;
   float HarmShapeGain_smth;
   float Tilt_smth;
} silk_shape_state_FLP;

typedef struct {
   silk_encoder_state sCmn;
   silk_shape_state_FLP sShape;
   float x_buf[ 2 * MAX_FRAME_LENGTH + LA_SHAPE_MAX ];
   float LTPCorr;
} silk_encoder_state_FLP;

typedef struct {
   float Gains[ MAX_NB_SUBFR ];
   float PredCoef[ 2 ][ MAX_LPC_ORDER ];
   float LTPCoef[LTP_ORDER * MAX_NB_SUBFR];
   float LTP_scale;
   int pitchL[ MAX_NB_SUBFR ];
   float AR[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ];
   float LF_MA_shp[ MAX_NB_SUBFR ];
   float LF_AR_shp[ MAX_NB_SUBFR ];
   float Tilt[ MAX_NB_SUBFR ];
   float HarmShapeGain[ MAX_NB_SUBFR ];
   float Lambda;
   float input_quality;
   float coding_quality;
   float predGain;
   float LTPredCodGain;
   float ResNrg[ MAX_NB_SUBFR ];
   fl_i32 GainsUnq_Q16[ MAX_NB_SUBFR ];
   fl_i8 lastGainIndexPrev;
} silk_encoder_control_FLP;

typedef struct {
   stereo_enc_state sStereo;
   fl_i32 nBitsUsedLBRR;
   fl_i32 nBitsExceeded;
   int nChannelsAPI;
   int nChannelsInternal;
   int nPrevChannelsInternal;
   int timeSinceSwitchAllowed_ms;
   int allowBandwidthSwitch;
   int prev_decode_only_middle;
   silk_encoder_state_FLP state_Fxx[ ENCODER_NUM_CHANNELS ];
} fl_silk_encoder;

static int silk_encode_frame_FLP(silk_encoder_state_FLP *psEnc, fl_i32 *pnBytesOut, ec_ctx *psRangeEnc,
    int condCoding, int maxBits, int useCBR);
static void silk_encode_do_VAD_FLP(silk_encoder_state_FLP *psEnc, int activity);


static const unsigned char silk_TargetRate_NB_21[117 - 10] = {
                                  0, 15, 39, 52, 61, 68,
   74, 79, 84, 88, 92, 95, 99,102,105,108,111,114,117,119,122,124,
   126,129,131,133,135,137,139,142,143,145,147,149,151,153,155,157,
   158,160,162,163,165,167,168,170,171,173,174,176,177,179,180,182,
   183,185,186,187,189,190,192,193,194,196,197,199,200,201,203,204,
   205,207,208,209,211,212,213,215,216,217,219,220,221,223,224,225,
   227,228,230,231,232,234,235,236,238,239,241,242,243,245,246,248,
   249,250,252,253,255
};
static const unsigned char silk_TargetRate_MB_21[165 - 10] = {
                                  0, 0, 28, 43, 52, 59,
   65, 70, 74, 78, 81, 85, 87, 90, 93, 95, 98,100,102,105,107,109,
   111,113,115,116,118,120,122,123,125,127,128,130,131,133,134,136,
   137,138,140,141,143,144,145,147,148,149,151,152,153,154,156,157,
   158,159,160,162,163,164,165,166,167,168,169,171,172,173,174,175,
   176,177,178,179,180,181,182,183,184,185,186,187,188,188,189,190,
   191,192,193,194,195,196,197,198,199,200,201,202,203,203,204,205,
   206,207,208,209,210,211,212,213,214,214,215,216,217,218,219,220,
   221,222,223,224,224,225,226,227,228,229,230,231,232,233,234,235,
   236,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,
   251,252,253,254,255
};
static const unsigned char silk_TargetRate_WB_21[201 - 10] = {
                                  0, 0, 0, 8, 29, 41,
   49, 56, 62, 66, 70, 74, 77, 80, 83, 86, 88, 91, 93, 95, 97, 99,
   101,103,105,107,108,110,112,113,115,116,118,119,121,122,123,125,
   126,127,129,130,131,132,134,135,136,137,138,140,141,142,143,144,
   145,146,147,148,149,150,151,152,153,154,156,157,158,159,159,160,
   161,162,163,164,165,166,167,168,169,170,171,171,172,173,174,175,
   176,177,177,178,179,180,181,181,182,183,184,185,185,186,187,188,
   189,189,190,191,192,192,193,194,195,195,196,197,198,198,199,200,
   200,201,202,203,203,204,205,206,206,207,208,209,209,210,211,211,
   212,213,214,214,215,216,216,217,218,219,219,220,221,221,222,223,
   224,224,225,226,226,227,228,229,229,230,231,232,232,233,234,234,
   235,236,237,237,238,239,240,240,241,242,243,243,244,245,246,246,
   247,248,249,249,250,251,252,253,255
};

#define silk_encoder_state_Fxx silk_encoder_state_FLP
static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
);
static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
);
static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
);
static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
);

static const fl_i32 sigm_LUT_slope_Q10[ 6 ] = {
   237, 153, 73, 30, 12, 7
};
static const fl_i32 sigm_LUT_pos_Q15[ 6 ] = {
   16384, 23955, 28861, 31213, 32178, 32548
};
static const fl_i32 sigm_LUT_neg_Q15[ 6 ] = {
   16384, 8812, 3906, 1554, 589, 219
};

static const fl_i16 A_fb1_20 = 5394 << 1;
static const fl_i16 A_fb1_21 = -24290;

static const fl_i32 tiltWeights[ VAD_N_BANDS ] = { 30000, 6000, -12000, -12000 };

#define BIN_DIV_STEPS_A2NLSF_FIX 3
#define MAX_ITERATIONS_A2NLSF_FIX 16

#define SILK_NSQ_H

#define silk_noise_shape_quantizer_short_prediction(in,coef,coefRev,order,arch) ((void)arch,silk_noise_shape_quantizer_short_prediction_c(in, coef, order))

#define silk_NSQ_noise_shape_feedback_loop(data0,data1,coef,order,arch) ((void)arch,silk_NSQ_noise_shape_feedback_loop_c(data0, data1, coef, order))

static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
);

typedef struct {
   fl_i32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
   fl_i32 RandState[ DECISION_DELAY ];
   fl_i32 Q_Q10[ DECISION_DELAY ];
   fl_i32 Xq_Q14[ DECISION_DELAY ];
   fl_i32 Pred_Q15[ DECISION_DELAY ];
   fl_i32 Shape_Q14[ DECISION_DELAY ];
   fl_i32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
   fl_i32 LF_AR_Q14;
   fl_i32 Diff_Q14;
   fl_i32 Seed;
   fl_i32 SeedInit;
   fl_i32 RD_Q10;
} NSQ_del_dec_struct;
typedef struct {
   fl_i32 Q_Q10;
   fl_i32 RD_Q10;
   fl_i32 xq_Q14;
   fl_i32 LF_AR_Q14;
   fl_i32 Diff_Q14;
   fl_i32 sLTP_shp_Q14;
   fl_i32 LPC_exc_Q14;
} NSQ_sample_struct;
typedef NSQ_sample_struct NSQ_sample_pair[ 2 ];
static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
);
static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
);

#define ORDER_FIR 4

static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
);

static int silk_init_encoder(
   silk_encoder_state_Fxx *psEnc,
   int arch
);
static int check_control_input(
   fl_silk_ctrl *encControl
);
static int silk_control_audio_bandwidth(
   silk_encoder_state *psEncC,
   fl_silk_ctrl *encControl
);
static int silk_control_SNR(
   silk_encoder_state *psEncC,
   fl_i32 TargetRate_bps
);
static int silk_control_encoder(
   silk_encoder_state_Fxx *psEnc,
   fl_silk_ctrl *encControl,
   const int allow_bw_switch,
   const int channelNb,
   const int force_fs_kHz
);
static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
);
static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
);
static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
);
static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
);
static void silk_HP_variable_cutoff(
   silk_encoder_state_Fxx state_Fxx[]
);
static inline void silk_LP_interpolate_filter_taps(
   fl_i32 B_Q28[ TRANSITION_NB ],
   fl_i32 A_Q28[ TRANSITION_NA ],
   const int ind,
   const fl_i32 fac_Q16
);
static void silk_LP_variable_cutoff(
   silk_LP_state *psLP,
   fl_i16 *frame,
   const int frame_length
);
static void silk_biquad_alt_stride1(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
);
static void silk_biquad_alt_stride2_c(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
);
static int silk_sigm_Q15(
   int in_Q5
);
static void silk_ana_filt_bank_1(
   const fl_i16 *in,
   fl_i32 *S,
   fl_i16 *outL,
   fl_i16 *outH,
   const fl_i32 N
);
static int silk_VAD_Init(
   silk_VAD_state *psSilk_VAD
);
static int silk_VAD_GetSA_Q8_c(
   silk_encoder_state *psEncC,
   const fl_i16 pIn[]
);
static void silk_VAD_GetNoiseLevels(
   const fl_i32 pX[ VAD_N_BANDS ],
   silk_VAD_state *psSilk_VAD
);
static fl_i32 silk_inner_prod_aligned_scale(
   const fl_i16 *const inVec1,
   const fl_i16 *const inVec2,
   const int scale,
   const int len
);
static fl_i32 silk_stereo_find_predictor(
   fl_i32 *ratio_Q14,
   const fl_i16 x[],
   const fl_i16 y[],
   fl_i32 mid_res_amp_Q0[],
   int length,
   int smooth_coef_Q16
);
static void silk_stereo_quant_pred(
   fl_i32 pred_Q13[],
   fl_i8 ix[ 2 ][ 3 ]
);
static void silk_stereo_encode_pred(
   ec_ctx *psRangeEnc,
   fl_i8 ix[ 2 ][ 3 ]
);
static void silk_stereo_encode_mid_only(
   ec_ctx *psRangeEnc,
   fl_i8 mid_only_flag
);
static void silk_stereo_LR_to_MS(
   stereo_enc_state *state,
   fl_i16 x1[],
   fl_i16 x2[],
   fl_i8 ix[ 2 ][ 3 ],
   fl_i8 *mid_only_flag,
   fl_i32 mid_side_rates_bps[],
   fl_i32 total_rate_bps,
   int prev_speech_act_Q8,
   int toMono,
   int fs_kHz,
   int frame_length
);
static inline void silk_A2NLSF_trans_poly(
   fl_i32 *p,
   const int dd
);
static inline fl_i32 silk_A2NLSF_eval_poly(
   fl_i32 *p,
   const fl_i32 x,
   const int dd
);
static inline void silk_A2NLSF_init(
   const fl_i32 *a_Q16,
   fl_i32 *P,
   fl_i32 *Q,
   const int dd
);
static void silk_A2NLSF(
   fl_i16 *NLSF,
   fl_i32 *a_Q16,
   const int d
);
static void silk_NLSF_VQ_weights_laroia(
   fl_i16 *pNLSFW_Q_OUT,
   const fl_i16 *pNLSF_Q15,
   const int D
);
static void silk_NLSF_VQ(
   fl_i32 err_Q24[],
   const fl_i16 in_Q15[],
   const fl_u8 pCB_Q8[],
   const fl_i16 pWght_Q9[],
   const int K,
   const int LPC_order
);
static fl_i32 silk_NLSF_del_dec_quant(
   fl_i8 indices[],
   const fl_i16 x_Q10[],
   const fl_i16 w_Q5[],
   const fl_u8 pred_coef_Q8[],
   const fl_i16 ec_ix[],
   const fl_u8 ec_rates_Q5[],
   const int quant_step_size_Q16,
   const fl_i16 inv_quant_step_size_Q6,
   const fl_i32 mu_Q20,
   const fl_i16 order
);
static fl_i32 silk_NLSF_encode(
       fl_i8 *NLSFIndices,
       fl_i16 *pNLSF_Q15,
   const fl_nlsf_cb *psNLSF_CB,
   const fl_i16 *pW_Q2,
   const int NLSF_mu_Q20,
   const int nSurvivors,
   const int signalType
);
static void silk_interpolate(
   fl_i16 xi[ MAX_LPC_ORDER ],
   const fl_i16 x0[ MAX_LPC_ORDER ],
   const fl_i16 x1[ MAX_LPC_ORDER ],
   const int ifact_Q2,
   const int d
);
static void silk_process_NLSFs(
   silk_encoder_state *psEncC,
   fl_i16 PredCoef_Q12[ 2 ][ MAX_LPC_ORDER ],
   fl_i16 pNLSF_Q15[ MAX_LPC_ORDER ],
   const fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ]
);
static void silk_VQ_WMat_EC_c(
   fl_i8 *ind,
   fl_i32 *res_nrg_Q15,
   fl_i32 *rate_dist_Q8,
   int *gain_Q7,
   const fl_i32 *XX_Q17,
   const fl_i32 *xX_Q17,
   const fl_i8 *cb_Q7,
   const fl_u8 *cb_gain_Q7,
   const fl_u8 *cl_Q5,
   const int subfr_len,
   const fl_i32 max_gain_Q7,
   const int L
);
static void silk_quant_LTP_gains(
   fl_i16 B_Q14[ MAX_NB_SUBFR * LTP_ORDER ],
   fl_i8 cbk_index[ MAX_NB_SUBFR ],
   fl_i8 *periodicity_index,
   fl_i32 *sum_log_gain_Q7,
   int *pred_gain_dB_Q7,
   const fl_i32 XX_Q17[ MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER ],
   const fl_i32 xX_Q17[ MAX_NB_SUBFR*LTP_ORDER ],
   const int subfr_len,
   const int nb_subfr,
   int arch
);
static void silk_encode_indices(
   silk_encoder_state *psEncC,
   ec_ctx *psRangeEnc,
   int FrameIndex,
   int encode_LBRR,
   int condCoding
);
static inline int combine_and_check(
   int *pulses_comb,
   const int *pulses_in,
   int max_pulses,
   int len
);
static void silk_encode_pulses(
   ec_ctx *psRangeEnc,
   const int signalType,
   const int quantOffsetType,
   fl_i8 pulses[],
   const int frame_length
);
static inline fl_i32 silk_noise_shape_quantizer_short_prediction_c(const fl_i32 *buf32, const fl_i16 *coef16, int order);
static inline fl_i32 silk_NSQ_noise_shape_feedback_loop_c(const fl_i32 *data0, fl_i32 *data1, const fl_i16 *coef, int order);
static void silk_NSQ_c
(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
);
static void silk_noise_shape_quantizer(
   silk_nsq_state *NSQ,
   int signalType,
   const fl_i32 x_sc_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int shapingLPCOrder,
   int predictLPCOrder,
   int arch
);
static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
);
static void silk_NSQ_del_dec_c(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
);
static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
);
static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
);
static void silk_resampler_down2(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
);
static void silk_resampler_down2_3(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
);
static int silk_Get_Encoder_Size(
   int *encSizeBytes,
   int channels
);
static int silk_InitEncoder(
   void *encState,
   int channels,
   int arch,
   fl_silk_ctrl *encStatus
);
static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
);
static int silk_Encode(
   void *encState,
   fl_silk_ctrl *encControl,
   const opus_res *samplesIn,
   int nSamplesIn,
   ec_ctx *psRangeEnc,
   fl_i32 *nBytesOut,
   const int prefillFlag,
   int activity
);

static int silk_init_encoder(
   silk_encoder_state_Fxx *psEnc,
   int arch
)
{
   int ret = 0;
   silk_memset( psEnc, 0, sizeof( silk_encoder_state_Fxx ) );
   psEnc->sCmn.arch = arch;
   psEnc->sCmn.variable_HP_smth1_Q15 = silk_LSHIFT( silk_lin2log( SILK_FIX_CONST( VARIABLE_HP_MIN_CUTOFF_HZ, 16 ) ) - ( 16 << 7 ), 8 );
   psEnc->sCmn.variable_HP_smth2_Q15 = psEnc->sCmn.variable_HP_smth1_Q15;
   psEnc->sCmn.first_frame_after_reset = 1;
   ret += silk_VAD_Init( &psEnc->sCmn.sVAD );
   return ret;
}

static int check_control_input(
   fl_silk_ctrl *encControl
)
{
   celt_assert( encControl != NULL );
   if( ( ( encControl->API_sampleRate != 8000 ) &&
       ( encControl->API_sampleRate != 12000 ) &&
       ( encControl->API_sampleRate != 16000 ) &&
       ( encControl->API_sampleRate != 24000 ) &&
       ( encControl->API_sampleRate != 32000 ) &&
       ( encControl->API_sampleRate != 44100 ) &&
       ( encControl->API_sampleRate != 48000 ) ) ||
      ( ( encControl->desiredInternalSampleRate != 8000 ) &&
       ( encControl->desiredInternalSampleRate != 12000 ) &&
       ( encControl->desiredInternalSampleRate != 16000 ) ) ||
      ( ( encControl->maxInternalSampleRate != 8000 ) &&
       ( encControl->maxInternalSampleRate != 12000 ) &&
       ( encControl->maxInternalSampleRate != 16000 ) ) ||
      ( ( encControl->minInternalSampleRate != 8000 ) &&
       ( encControl->minInternalSampleRate != 12000 ) &&
       ( encControl->minInternalSampleRate != 16000 ) ) ||
       ( encControl->minInternalSampleRate > encControl->desiredInternalSampleRate ) ||
       ( encControl->maxInternalSampleRate < encControl->desiredInternalSampleRate ) ||
       ( encControl->minInternalSampleRate > encControl->maxInternalSampleRate ) ) {
      celt_assert( 0 );
      return SILK_ENC_FS_NOT_SUPPORTED;
   }
   if( encControl->payloadSize_ms != 10 &&
      encControl->payloadSize_ms != 20 &&
      encControl->payloadSize_ms != 40 &&
      encControl->payloadSize_ms != 60 ) {
      celt_assert( 0 );
      return SILK_ENC_PACKET_SIZE_NOT_SUPPORTED;
   }
   if( encControl->packetLossPercentage < 0 || encControl->packetLossPercentage > 100 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_LOSS_RATE;
   }
   if( encControl->useDTX < 0 || encControl->useDTX > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_DTX_SETTING;
   }
   if( encControl->useCBR < 0 || encControl->useCBR > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_CBR_SETTING;
   }
   if( encControl->useInBandFEC < 0 || encControl->useInBandFEC > 1 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_INBAND_FEC_SETTING;
   }
   if( encControl->nChannelsAPI < 1 || encControl->nChannelsAPI > ENCODER_NUM_CHANNELS ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->nChannelsInternal < 1 || encControl->nChannelsInternal > ENCODER_NUM_CHANNELS ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->nChannelsInternal > encControl->nChannelsAPI ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_NUMBER_OF_CHANNELS_ERROR;
   }
   if( encControl->complexity < 0 || encControl->complexity > 10 ) {
      celt_assert( 0 );
      return SILK_ENC_INVALID_COMPLEXITY_SETTING;
   }
   return SILK_NO_ERROR;
}

static int silk_control_audio_bandwidth(
   silk_encoder_state *psEncC,
   fl_silk_ctrl *encControl
)
{
   int fs_kHz;
   int orig_kHz;
   fl_i32 fs_Hz;
   orig_kHz = psEncC->fs_kHz;
   if( orig_kHz == 0 ) {
      orig_kHz = psEncC->sLP.saved_fs_kHz;
   }
   fs_kHz = orig_kHz;
   fs_Hz = silk_SMULBB( fs_kHz, 1000 );
   if( fs_Hz == 0 ) {
      fs_Hz = silk_min( psEncC->desiredInternal_fs_Hz, psEncC->API_fs_Hz );
      fs_kHz = silk_DIV32_16( fs_Hz, 1000 );
   } else if( fs_Hz > psEncC->API_fs_Hz || fs_Hz > psEncC->maxInternal_fs_Hz || fs_Hz < psEncC->minInternal_fs_Hz ) {
      fs_Hz = psEncC->API_fs_Hz;
      fs_Hz = silk_min( fs_Hz, psEncC->maxInternal_fs_Hz );
      fs_Hz = silk_max( fs_Hz, psEncC->minInternal_fs_Hz );
      fs_kHz = silk_DIV32_16( fs_Hz, 1000 );
   } else {
      if( psEncC->sLP.transition_frame_no >= TRANSITION_FRAMES ) {
         psEncC->sLP.mode = 0;
      }
      if( psEncC->allow_bandwidth_switch || encControl->opusCanSwitch ) {
         if( silk_SMULBB( orig_kHz, 1000 ) > psEncC->desiredInternal_fs_Hz )
         {
            if( psEncC->sLP.mode == 0 ) {
               psEncC->sLP.transition_frame_no = TRANSITION_FRAMES;
               silk_memset( psEncC->sLP.In_LP_State, 0, sizeof( psEncC->sLP.In_LP_State ) );
            }
            if( encControl->opusCanSwitch ) {
               psEncC->sLP.mode = 0;
               fs_kHz = orig_kHz == 16 ? 12 : 8;
            } else {
              if( psEncC->sLP.transition_frame_no <= 0 ) {
                 encControl->switchReady = 1;
                 encControl->maxBits -= encControl->maxBits * 5 / ( encControl->payloadSize_ms + 5 );
              } else {
                 psEncC->sLP.mode = -2;
              }
            }
         }
         else
         if( silk_SMULBB( orig_kHz, 1000 ) < psEncC->desiredInternal_fs_Hz )
         {
            if( encControl->opusCanSwitch ) {
               fs_kHz = orig_kHz == 8 ? 12 : 16;
               psEncC->sLP.transition_frame_no = 0;
               silk_memset( psEncC->sLP.In_LP_State, 0, sizeof( psEncC->sLP.In_LP_State ) );
               psEncC->sLP.mode = 1;
            } else {
              if( psEncC->sLP.mode == 0 ) {
                 encControl->switchReady = 1;
                 encControl->maxBits -= encControl->maxBits * 5 / ( encControl->payloadSize_ms + 5 );
              } else {
                 psEncC->sLP.mode = 1;
              }
            }
         } else {
           if (psEncC->sLP.mode<0)
             psEncC->sLP.mode = 1;
         }
      }
   }
   return fs_kHz;
}

static int silk_control_SNR(
   silk_encoder_state *psEncC,
   fl_i32 TargetRate_bps
)
{
   int id;
   int bound;
   const unsigned char *snr_table;
   psEncC->TargetRate_bps = TargetRate_bps;
   if( psEncC->nb_subfr == 2 ) {
      TargetRate_bps -= 2000 + psEncC->fs_kHz/16;
   }
   if( psEncC->fs_kHz == 8 ) {
      bound = sizeof(silk_TargetRate_NB_21);
      snr_table = silk_TargetRate_NB_21;
   } else if( psEncC->fs_kHz == 12 ) {
      bound = sizeof(silk_TargetRate_MB_21);
      snr_table = silk_TargetRate_MB_21;
   } else {
      bound = sizeof(silk_TargetRate_WB_21);
      snr_table = silk_TargetRate_WB_21;
   }
   id = (TargetRate_bps+200)/400;
   id = silk_min(id - 10, bound-1);
   if( id <= 0 ) {
      psEncC->SNR_dB_Q7 = 0;
   } else {
      psEncC->SNR_dB_Q7 = snr_table[id]*21;
   }
   return SILK_NO_ERROR;
}

static int silk_control_encoder(
   silk_encoder_state_Fxx *psEnc,
   fl_silk_ctrl *encControl,
   const int allow_bw_switch,
   const int channelNb,
   const int force_fs_kHz
)
{
   int fs_kHz, ret = 0;
   psEnc->sCmn.useDTX = encControl->useDTX;
   psEnc->sCmn.useCBR = encControl->useCBR;
   psEnc->sCmn.API_fs_Hz = encControl->API_sampleRate;
   psEnc->sCmn.maxInternal_fs_Hz = encControl->maxInternalSampleRate;
   psEnc->sCmn.minInternal_fs_Hz = encControl->minInternalSampleRate;
   psEnc->sCmn.desiredInternal_fs_Hz = encControl->desiredInternalSampleRate;
   psEnc->sCmn.useInBandFEC = encControl->useInBandFEC;
   psEnc->sCmn.nChannelsAPI = encControl->nChannelsAPI;
   psEnc->sCmn.nChannelsInternal = encControl->nChannelsInternal;
   psEnc->sCmn.allow_bandwidth_switch = allow_bw_switch;
   psEnc->sCmn.channelNb = channelNb;
   if( psEnc->sCmn.controlled_since_last_payload != 0 && psEnc->sCmn.prefillFlag == 0 ) {
      if( psEnc->sCmn.API_fs_Hz != psEnc->sCmn.prev_API_fs_Hz && psEnc->sCmn.fs_kHz > 0 ) {
         ret += silk_setup_resamplers( psEnc, psEnc->sCmn.fs_kHz );
      }
      return ret;
   }
   fs_kHz = silk_control_audio_bandwidth( &psEnc->sCmn, encControl );
   if( force_fs_kHz ) {
     fs_kHz = force_fs_kHz;
   }
   ret += silk_setup_resamplers( psEnc, fs_kHz );
   ret += silk_setup_fs( psEnc, fs_kHz, encControl->payloadSize_ms );
   ret += silk_setup_complexity( &psEnc->sCmn, encControl->complexity );
   psEnc->sCmn.PacketLoss_perc = encControl->packetLossPercentage;
   ret += silk_setup_LBRR( &psEnc->sCmn, encControl );
   psEnc->sCmn.controlled_since_last_payload = 1;
   return ret;
}

static int silk_setup_resamplers(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz
)
{
   int ret = SILK_NO_ERROR;
   SAVE_STACK;
   if( psEnc->sCmn.fs_kHz != fs_kHz || psEnc->sCmn.prev_API_fs_Hz != psEnc->sCmn.API_fs_Hz )
   {
      if( psEnc->sCmn.fs_kHz == 0 ) {
         ret += silk_resampler_init( &psEnc->sCmn.resampler_state, psEnc->sCmn.API_fs_Hz, fs_kHz * 1000, 1 );
      } else {
         VARDECL( fl_i16, x_buf_API_fs_Hz );
         VARDECL( silk_resampler_state, temp_resampler_state );
         VARDECL( fl_i16, x_bufFIX );
         fl_i32 new_buf_samples;
         fl_i32 api_buf_samples;
         fl_i32 old_buf_samples;
         fl_i32 buf_length_ms;
         buf_length_ms = silk_LSHIFT( psEnc->sCmn.nb_subfr * 5, 1 ) + LA_SHAPE_MS;
         old_buf_samples = buf_length_ms * psEnc->sCmn.fs_kHz;
         new_buf_samples = buf_length_ms * fs_kHz;
         ALLOC( x_bufFIX, silk_max( old_buf_samples, new_buf_samples ),
              fl_i16 );
         silk_float2short_array( x_bufFIX, psEnc->x_buf, old_buf_samples );
         ALLOC( temp_resampler_state, 1, silk_resampler_state );
         ret += silk_resampler_init( temp_resampler_state, silk_SMULBB( psEnc->sCmn.fs_kHz, 1000 ), psEnc->sCmn.API_fs_Hz, 0 );
         api_buf_samples = buf_length_ms * silk_DIV32_16( psEnc->sCmn.API_fs_Hz, 1000 );
         ALLOC( x_buf_API_fs_Hz, api_buf_samples, fl_i16 );
         ret += silk_resampler( temp_resampler_state, x_buf_API_fs_Hz, x_bufFIX, old_buf_samples );
         ret += silk_resampler_init( &psEnc->sCmn.resampler_state, psEnc->sCmn.API_fs_Hz, silk_SMULBB( fs_kHz, 1000 ), 1 );
         ret += silk_resampler( &psEnc->sCmn.resampler_state, x_bufFIX, x_buf_API_fs_Hz, api_buf_samples );
         silk_short2float_array( psEnc->x_buf, x_bufFIX, new_buf_samples);
      }
   }
   psEnc->sCmn.prev_API_fs_Hz = psEnc->sCmn.API_fs_Hz;
   RESTORE_STACK;
   return ret;
}

static int silk_setup_fs(
   silk_encoder_state_Fxx *psEnc,
   int fs_kHz,
   int PacketSize_ms
)
{
   int ret = SILK_NO_ERROR;
   if( PacketSize_ms != psEnc->sCmn.PacketSize_ms ) {
      if( ( PacketSize_ms != 10 ) &&
         ( PacketSize_ms != 20 ) &&
         ( PacketSize_ms != 40 ) &&
         ( PacketSize_ms != 60 ) ) {
         ret = SILK_ENC_PACKET_SIZE_NOT_SUPPORTED;
      }
      if( PacketSize_ms <= 10 ) {
         psEnc->sCmn.nFramesPerPacket = 1;
         psEnc->sCmn.nb_subfr = PacketSize_ms == 10 ? 2 : 1;
         psEnc->sCmn.frame_length = silk_SMULBB( PacketSize_ms, fs_kHz );
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS_2_SF, fs_kHz );
         if( psEnc->sCmn.fs_kHz == 8 ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_iCDF;
         }
      } else {
         psEnc->sCmn.nFramesPerPacket = silk_DIV32_16( PacketSize_ms, MAX_FRAME_LENGTH_MS );
         psEnc->sCmn.nb_subfr = MAX_NB_SUBFR;
         psEnc->sCmn.frame_length = silk_SMULBB( 20, fs_kHz );
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS, fs_kHz );
         if( psEnc->sCmn.fs_kHz == 8 ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_iCDF;
         }
      }
      psEnc->sCmn.PacketSize_ms = PacketSize_ms;
      psEnc->sCmn.TargetRate_bps = 0;
   }
   celt_assert( fs_kHz == 8 || fs_kHz == 12 || fs_kHz == 16 );
   celt_assert( psEnc->sCmn.nb_subfr == 2 || psEnc->sCmn.nb_subfr == 4 );
   if( psEnc->sCmn.fs_kHz != fs_kHz ) {
      silk_memset( &psEnc->sShape, 0, sizeof( psEnc->sShape ) );
      silk_memset( &psEnc->sCmn.sNSQ, 0, sizeof( psEnc->sCmn.sNSQ ) );
      silk_memset( psEnc->sCmn.prev_NLSFq_Q15, 0, sizeof( psEnc->sCmn.prev_NLSFq_Q15 ) );
      silk_memset( &psEnc->sCmn.sLP.In_LP_State, 0, sizeof( psEnc->sCmn.sLP.In_LP_State ) );
      psEnc->sCmn.inputBufIx = 0;
      psEnc->sCmn.nFramesEncoded = 0;
      psEnc->sCmn.TargetRate_bps = 0;
      psEnc->sCmn.prevLag = 100;
      psEnc->sCmn.first_frame_after_reset = 1;
      psEnc->sShape.LastGainIndex = 10;
      psEnc->sCmn.sNSQ.lagPrev = 100;
      psEnc->sCmn.sNSQ.prev_gain_Q16 = 65536;
      psEnc->sCmn.prevSignalType = TYPE_NO_VOICE_ACTIVITY;
      psEnc->sCmn.fs_kHz = fs_kHz;
      if( psEnc->sCmn.fs_kHz == 8 ) {
         if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_NB_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_NB_iCDF;
         }
      } else {
         if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_iCDF;
         } else {
            psEnc->sCmn.pitch_contour_iCDF = fl_pitch_contour_10_ms_iCDF;
         }
      }
      if( psEnc->sCmn.fs_kHz == 8 || psEnc->sCmn.fs_kHz == 12 ) {
         psEnc->sCmn.predictLPCOrder = MIN_LPC_ORDER;
         psEnc->sCmn.psNLSF_CB = &fl_NLSF_CB_NB_MB;
      } else {
         psEnc->sCmn.predictLPCOrder = MAX_LPC_ORDER;
         psEnc->sCmn.psNLSF_CB = &fl_NLSF_CB_WB;
      }
      psEnc->sCmn.subfr_length = SUB_FRAME_LENGTH_MS * fs_kHz;
      psEnc->sCmn.frame_length = silk_SMULBB( psEnc->sCmn.subfr_length, psEnc->sCmn.nb_subfr );
      psEnc->sCmn.ltp_mem_length = silk_SMULBB( LTP_MEM_LENGTH_MS, fs_kHz );
      psEnc->sCmn.la_pitch = silk_SMULBB( LA_PITCH_MS, fs_kHz );
      psEnc->sCmn.max_pitch_lag = silk_SMULBB( 18, fs_kHz );
      if( psEnc->sCmn.nb_subfr == MAX_NB_SUBFR ) {
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS, fs_kHz );
      } else {
         psEnc->sCmn.pitch_LPC_win_length = silk_SMULBB( FIND_PITCH_LPC_WIN_MS_2_SF, fs_kHz );
      }
      if( psEnc->sCmn.fs_kHz == 16 ) {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform8_iCDF;
      } else if( psEnc->sCmn.fs_kHz == 12 ) {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform6_iCDF;
      } else {
         psEnc->sCmn.pitch_lag_low_bits_iCDF = fl_uniform4_iCDF;
      }
   }
   celt_assert( ( psEnc->sCmn.subfr_length * psEnc->sCmn.nb_subfr ) == psEnc->sCmn.frame_length );
   return ret;
}

static int silk_setup_complexity(
   silk_encoder_state *psEncC,
   int Complexity
)
{
   int ret = 0;
   celt_assert( Complexity >= 0 && Complexity <= 10 );
   if( Complexity < 1 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MIN_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.8, 16 );
      psEncC->pitchEstimationLPCOrder = 6;
      psEncC->shapingLPCOrder = 12;
      psEncC->la_shape = 3 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 1;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 2;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 2 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.76, 16 );
      psEncC->pitchEstimationLPCOrder = 8;
      psEncC->shapingLPCOrder = 14;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 1;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 3;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 3 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MIN_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.8, 16 );
      psEncC->pitchEstimationLPCOrder = 6;
      psEncC->shapingLPCOrder = 12;
      psEncC->la_shape = 3 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 2;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 4 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.76, 16 );
      psEncC->pitchEstimationLPCOrder = 8;
      psEncC->shapingLPCOrder = 14;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 0;
      psEncC->NLSF_MSVQ_Survivors = 4;
      psEncC->warping_Q16 = 0;
   } else if( Complexity < 6 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.74, 16 );
      psEncC->pitchEstimationLPCOrder = 10;
      psEncC->shapingLPCOrder = 16;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 2;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 6;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   } else if( Complexity < 8 ) {
      psEncC->pitchEstimationComplexity = SILK_PE_MID_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.72, 16 );
      psEncC->pitchEstimationLPCOrder = 12;
      psEncC->shapingLPCOrder = 20;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = 3;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 8;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   } else {
      psEncC->pitchEstimationComplexity = SILK_PE_MAX_COMPLEX;
      psEncC->pitchEstimationThreshold_Q16 = SILK_FIX_CONST( 0.7, 16 );
      psEncC->pitchEstimationLPCOrder = 16;
      psEncC->shapingLPCOrder = 24;
      psEncC->la_shape = 5 * psEncC->fs_kHz;
      psEncC->nStatesDelayedDecision = MAX_DEL_DEC_STATES;
      psEncC->useInterpolatedNLSFs = 1;
      psEncC->NLSF_MSVQ_Survivors = 16;
      psEncC->warping_Q16 = psEncC->fs_kHz * SILK_FIX_CONST( WARPING_MULTIPLIER, 16 );
   }
   psEncC->pitchEstimationLPCOrder = silk_min_int( psEncC->pitchEstimationLPCOrder, psEncC->predictLPCOrder );
   psEncC->shapeWinLength = SUB_FRAME_LENGTH_MS * psEncC->fs_kHz + 2 * psEncC->la_shape;
   psEncC->Complexity = Complexity;
   celt_assert( psEncC->pitchEstimationLPCOrder <= MAX_FIND_PITCH_LPC_ORDER );
   celt_assert( psEncC->shapingLPCOrder <= MAX_SHAPE_LPC_ORDER );
   celt_assert( psEncC->nStatesDelayedDecision <= MAX_DEL_DEC_STATES );
   celt_assert( psEncC->warping_Q16 <= 32767 );
   celt_assert( psEncC->la_shape <= LA_SHAPE_MAX );
   celt_assert( psEncC->shapeWinLength <= SHAPE_LPC_WIN_MAX );
   return ret;
}

static inline int silk_setup_LBRR(
   silk_encoder_state *psEncC,
   const fl_silk_ctrl *encControl
)
{
   int LBRR_in_previous_packet, ret = SILK_NO_ERROR;
   LBRR_in_previous_packet = psEncC->LBRR_enabled;
   psEncC->LBRR_enabled = encControl->LBRR_coded;
   if( psEncC->LBRR_enabled ) {
      if( LBRR_in_previous_packet == 0 ) {
         psEncC->LBRR_GainIncreases = 7;
      } else {
         psEncC->LBRR_GainIncreases = silk_max_int( 7 - silk_SMULWB( (fl_i32)psEncC->PacketLoss_perc, SILK_FIX_CONST( 0.2, 16 ) ), 3 );
      }
   }
   return ret;
}

static void silk_HP_variable_cutoff(
   silk_encoder_state_Fxx state_Fxx[]
)
{
  int quality_Q15;
  fl_i32 pitch_freq_Hz_Q16, pitch_freq_log_Q7, delta_freq_Q7;
  silk_encoder_state *psEncC1 = &state_Fxx[ 0 ].sCmn;
  if( psEncC1->prevSignalType == TYPE_VOICED ) {
    pitch_freq_Hz_Q16 = silk_DIV32_16( silk_LSHIFT( silk_MUL( psEncC1->fs_kHz, 1000 ), 16 ), psEncC1->prevLag );
    pitch_freq_log_Q7 = silk_lin2log( pitch_freq_Hz_Q16 ) - ( 16 << 7 );
    quality_Q15 = psEncC1->input_quality_bands_Q15[ 0 ];
    pitch_freq_log_Q7 = silk_SMLAWB( pitch_freq_log_Q7, silk_SMULWB( silk_LSHIFT( -quality_Q15, 2 ), quality_Q15 ),
         pitch_freq_log_Q7 - ( silk_lin2log( SILK_FIX_CONST( VARIABLE_HP_MIN_CUTOFF_HZ, 16 ) ) - ( 16 << 7 ) ) );
    delta_freq_Q7 = pitch_freq_log_Q7 - silk_RSHIFT( psEncC1->variable_HP_smth1_Q15, 8 );
    if( delta_freq_Q7 < 0 ) {
      delta_freq_Q7 = silk_MUL( delta_freq_Q7, 3 );
    }
    delta_freq_Q7 = silk_LIMIT_32( delta_freq_Q7, -SILK_FIX_CONST( VARIABLE_HP_MAX_DELTA_FREQ, 7 ), SILK_FIX_CONST( VARIABLE_HP_MAX_DELTA_FREQ, 7 ) );
    psEncC1->variable_HP_smth1_Q15 = silk_SMLAWB( psEncC1->variable_HP_smth1_Q15,
         silk_SMULBB( psEncC1->speech_activity_Q8, delta_freq_Q7 ), SILK_FIX_CONST( VARIABLE_HP_SMTH_COEF1, 16 ) );
    psEncC1->variable_HP_smth1_Q15 = silk_LIMIT_32( psEncC1->variable_HP_smth1_Q15,
         silk_LSHIFT( silk_lin2log( VARIABLE_HP_MIN_CUTOFF_HZ ), 8 ),
         silk_LSHIFT( silk_lin2log( VARIABLE_HP_MAX_CUTOFF_HZ ), 8 ) );
  }
}

static inline void silk_LP_interpolate_filter_taps(
   fl_i32 B_Q28[ TRANSITION_NB ],
   fl_i32 A_Q28[ TRANSITION_NA ],
   const int ind,
   const fl_i32 fac_Q16
)
{
   int nb, na;
   if( ind < TRANSITION_INT_NUM - 1 ) {
      if( fac_Q16 > 0 ) {
         if( fac_Q16 < 32768 ) {
            for( nb = 0; nb < TRANSITION_NB; nb++ ) {
               B_Q28[ nb ] = silk_SMLAWB(
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ] -
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fac_Q16 );
            }
            for( na = 0; na < TRANSITION_NA; na++ ) {
               A_Q28[ na ] = silk_SMLAWB(
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ] -
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fac_Q16 );
            }
         } else {
            silk_assert( fac_Q16 - ( 1 << 16 ) == silk_SAT16( fac_Q16 - ( 1 << 16 ) ) );
            for( nb = 0; nb < TRANSITION_NB; nb++ ) {
               B_Q28[ nb ] = silk_SMLAWB(
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ],
                  fl_Transition_LP_B_Q28[ ind + 1 ][ nb ] -
                  fl_Transition_LP_B_Q28[ ind ][ nb ],
                  fac_Q16 - ( (fl_i32)1 << 16 ) );
            }
            for( na = 0; na < TRANSITION_NA; na++ ) {
               A_Q28[ na ] = silk_SMLAWB(
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ],
                  fl_Transition_LP_A_Q28[ ind + 1 ][ na ] -
                  fl_Transition_LP_A_Q28[ ind ][ na ],
                  fac_Q16 - ( (fl_i32)1 << 16 ) );
            }
         }
      } else {
         silk_memcpy( B_Q28, fl_Transition_LP_B_Q28[ ind ], TRANSITION_NB * sizeof( fl_i32 ) );
         silk_memcpy( A_Q28, fl_Transition_LP_A_Q28[ ind ], TRANSITION_NA * sizeof( fl_i32 ) );
      }
   } else {
      silk_memcpy( B_Q28, fl_Transition_LP_B_Q28[ TRANSITION_INT_NUM - 1 ], TRANSITION_NB * sizeof( fl_i32 ) );
      silk_memcpy( A_Q28, fl_Transition_LP_A_Q28[ TRANSITION_INT_NUM - 1 ], TRANSITION_NA * sizeof( fl_i32 ) );
   }
}

static void silk_LP_variable_cutoff(
   silk_LP_state *psLP,
   fl_i16 *frame,
   const int frame_length
)
{
   fl_i32 B_Q28[ TRANSITION_NB ], A_Q28[ TRANSITION_NA ], fac_Q16 = 0;
   int ind = 0;
   silk_assert( psLP->transition_frame_no >= 0 && psLP->transition_frame_no <= TRANSITION_FRAMES );
   if( psLP->mode != 0 ) {
      fac_Q16 = silk_LSHIFT( TRANSITION_FRAMES - psLP->transition_frame_no, 16 - 6 );
      fac_Q16 = silk_DIV32_16( silk_LSHIFT( TRANSITION_FRAMES - psLP->transition_frame_no, 16 ), TRANSITION_FRAMES );
      ind = silk_RSHIFT( fac_Q16, 16 );
      fac_Q16 -= silk_LSHIFT( ind, 16 );
      silk_assert( ind >= 0 );
      silk_assert( ind < TRANSITION_INT_NUM );
      silk_LP_interpolate_filter_taps( B_Q28, A_Q28, ind, fac_Q16 );
      psLP->transition_frame_no = silk_LIMIT( psLP->transition_frame_no + psLP->mode, 0, TRANSITION_FRAMES );
      silk_assert( TRANSITION_NB == 3 && TRANSITION_NA == 2 );
      silk_biquad_alt_stride1( frame, B_Q28, A_Q28, psLP->In_LP_State, frame, frame_length);
   }
}

static void silk_biquad_alt_stride1(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
)
{
   int k;
   fl_i32 inval, A0_U_Q28, A0_L_Q28, A1_U_Q28, A1_L_Q28, out32_Q14;
   A0_L_Q28 = ( -A_Q28[ 0 ] ) & 0x00003FFF;
   A0_U_Q28 = silk_RSHIFT( -A_Q28[ 0 ], 14 );
   A1_L_Q28 = ( -A_Q28[ 1 ] ) & 0x00003FFF;
   A1_U_Q28 = silk_RSHIFT( -A_Q28[ 1 ], 14 );
   for( k = 0; k < len; k++ ) {
      inval = in[ k ];
      out32_Q14 = silk_LSHIFT( silk_SMLAWB( S[ 0 ], B_Q28[ 0 ], inval ), 2 );
      S[ 0 ] = S[1] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14, A0_L_Q28 ), 14 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], out32_Q14, A0_U_Q28 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], B_Q28[ 1 ], inval);
      S[ 1 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14, A1_L_Q28 ), 14 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], out32_Q14, A1_U_Q28 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], B_Q28[ 2 ], inval );
      out[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14 + (1<<14) - 1, 14 ) );
   }
}

static void silk_biquad_alt_stride2_c(
   const fl_i16 *in,
   const fl_i32 *B_Q28,
   const fl_i32 *A_Q28,
   fl_i32 *S,
   fl_i16 *out,
   const fl_i32 len
)
{
   int k;
   fl_i32 A0_U_Q28, A0_L_Q28, A1_U_Q28, A1_L_Q28, out32_Q14[ 2 ];
   A0_L_Q28 = ( -A_Q28[ 0 ] ) & 0x00003FFF;
   A0_U_Q28 = silk_RSHIFT( -A_Q28[ 0 ], 14 );
   A1_L_Q28 = ( -A_Q28[ 1 ] ) & 0x00003FFF;
   A1_U_Q28 = silk_RSHIFT( -A_Q28[ 1 ], 14 );
   for( k = 0; k < len; k++ ) {
      out32_Q14[ 0 ] = silk_LSHIFT( silk_SMLAWB( S[ 0 ], B_Q28[ 0 ], in[ 2 * k + 0 ] ), 2 );
      out32_Q14[ 1 ] = silk_LSHIFT( silk_SMLAWB( S[ 2 ], B_Q28[ 0 ], in[ 2 * k + 1 ] ), 2 );
      S[ 0 ] = S[ 1 ] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 0 ], A0_L_Q28 ), 14 );
      S[ 2 ] = S[ 3 ] + silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 1 ], A0_L_Q28 ), 14 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], out32_Q14[ 0 ], A0_U_Q28 );
      S[ 2 ] = silk_SMLAWB( S[ 2 ], out32_Q14[ 1 ], A0_U_Q28 );
      S[ 0 ] = silk_SMLAWB( S[ 0 ], B_Q28[ 1 ], in[ 2 * k + 0 ] );
      S[ 2 ] = silk_SMLAWB( S[ 2 ], B_Q28[ 1 ], in[ 2 * k + 1 ] );
      S[ 1 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 0 ], A1_L_Q28 ), 14 );
      S[ 3 ] = silk_RSHIFT_ROUND( silk_SMULWB( out32_Q14[ 1 ], A1_L_Q28 ), 14 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], out32_Q14[ 0 ], A1_U_Q28 );
      S[ 3 ] = silk_SMLAWB( S[ 3 ], out32_Q14[ 1 ], A1_U_Q28 );
      S[ 1 ] = silk_SMLAWB( S[ 1 ], B_Q28[ 2 ], in[ 2 * k + 0 ] );
      S[ 3 ] = silk_SMLAWB( S[ 3 ], B_Q28[ 2 ], in[ 2 * k + 1 ] );
      out[ 2 * k + 0 ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14[ 0 ] + (1<<14) - 1, 14 ) );
      out[ 2 * k + 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT( out32_Q14[ 1 ] + (1<<14) - 1, 14 ) );
   }
}

static int silk_sigm_Q15(
   int in_Q5
)
{
   int ind;
   if( in_Q5 < 0 ) {
      in_Q5 = -in_Q5;
      if( in_Q5 >= 6 * 32 ) {
         return 0;
      } else {
         ind = silk_RSHIFT( in_Q5, 5 );
         return( sigm_LUT_neg_Q15[ ind ] - silk_SMULBB( sigm_LUT_slope_Q10[ ind ], in_Q5 & 0x1F ) );
      }
   } else {
      if( in_Q5 >= 6 * 32 ) {
         return 32767;
      } else {
         ind = silk_RSHIFT( in_Q5, 5 );
         return( sigm_LUT_pos_Q15[ ind ] + silk_SMULBB( sigm_LUT_slope_Q10[ ind ], in_Q5 & 0x1F ) );
      }
   }
}

static void silk_ana_filt_bank_1(
   const fl_i16 *in,
   fl_i32 *S,
   fl_i16 *outL,
   fl_i16 *outH,
   const fl_i32 N
)
{
   int k, N2 = silk_RSHIFT( N, 1 );
   fl_i32 in32, X, Y, out_1, out_2;
   for( k = 0; k < N2; k++ ) {
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k ], 10 );
      Y = silk_SUB32( in32, S[ 0 ] );
      X = silk_SMLAWB( Y, Y, A_fb1_21 );
      out_1 = silk_ADD32( S[ 0 ], X );
      S[ 0 ] = silk_ADD32( in32, X );
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k + 1 ], 10 );
      Y = silk_SUB32( in32, S[ 1 ] );
      X = silk_SMULWB( Y, A_fb1_20 );
      out_2 = silk_ADD32( S[ 1 ], X );
      S[ 1 ] = silk_ADD32( in32, X );
      outL[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_ADD32( out_2, out_1 ), 11 ) );
      outH[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_SUB32( out_2, out_1 ), 11 ) );
   }
}

static int silk_VAD_Init(
   silk_VAD_state *psSilk_VAD
)
{
   int b, ret = 0;
   silk_memset( psSilk_VAD, 0, sizeof( silk_VAD_state ) );
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NoiseLevelBias[ b ] = silk_max_32( silk_DIV32_16( VAD_NOISE_LEVELS_BIAS, b + 1 ), 1 );
   }
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NL[ b ] = silk_MUL( 100, psSilk_VAD->NoiseLevelBias[ b ] );
      psSilk_VAD->inv_NL[ b ] = silk_DIV32( silk_int32_MAX, psSilk_VAD->NL[ b ] );
   }
   psSilk_VAD->counter = 15;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NrgRatioSmth_Q8[ b ] = 100 * 256;
   }
   return( ret );
}

static int silk_VAD_GetSA_Q8_c(
   silk_encoder_state *psEncC,
   const fl_i16 pIn[]
)
{
   int SA_Q15, pSNR_dB_Q7, input_tilt;
   int decimated_framelength1, decimated_framelength2;
   int decimated_framelength;
   int dec_subframe_length, dec_subframe_offset, SNR_Q7, i, b, s;
   fl_i32 sumSquared, smooth_coef_Q16;
   fl_i16 HPstateTmp;
   VARDECL( fl_i16, X );
   fl_i32 Xnrg[ VAD_N_BANDS ];
   fl_i32 NrgToNoiseRatio_Q8[ VAD_N_BANDS ];
   fl_i32 speech_nrg, x_tmp;
   int X_offset[ VAD_N_BANDS ];
   int ret = 0;
   silk_VAD_state *psSilk_VAD = &psEncC->sVAD;
   SAVE_STACK;
   silk_assert( VAD_N_BANDS == 4 );
   celt_assert( MAX_FRAME_LENGTH >= psEncC->frame_length );
   celt_assert( psEncC->frame_length <= 512 );
   celt_assert( psEncC->frame_length == 8 * silk_RSHIFT( psEncC->frame_length, 3 ) );
   decimated_framelength1 = silk_RSHIFT( psEncC->frame_length, 1 );
   decimated_framelength2 = silk_RSHIFT( psEncC->frame_length, 2 );
   decimated_framelength = silk_RSHIFT( psEncC->frame_length, 3 );
   X_offset[ 0 ] = 0;
   X_offset[ 1 ] = decimated_framelength + decimated_framelength2;
   X_offset[ 2 ] = X_offset[ 1 ] + decimated_framelength;
   X_offset[ 3 ] = X_offset[ 2 ] + decimated_framelength2;
   ALLOC( X, X_offset[ 3 ] + decimated_framelength1, fl_i16 );
   silk_ana_filt_bank_1( pIn, &psSilk_VAD->AnaState[ 0 ],
      X, &X[ X_offset[ 3 ] ], psEncC->frame_length );
   silk_ana_filt_bank_1( X, &psSilk_VAD->AnaState1[ 0 ],
      X, &X[ X_offset[ 2 ] ], decimated_framelength1 );
   silk_ana_filt_bank_1( X, &psSilk_VAD->AnaState2[ 0 ],
      X, &X[ X_offset[ 1 ] ], decimated_framelength2 );
   X[ decimated_framelength - 1 ] = silk_RSHIFT( X[ decimated_framelength - 1 ], 1 );
   HPstateTmp = X[ decimated_framelength - 1 ];
   for( i = decimated_framelength - 1; i > 0; i-- ) {
      X[ i - 1 ] = silk_RSHIFT( X[ i - 1 ], 1 );
      X[ i ] -= X[ i - 1 ];
   }
   X[ 0 ] -= psSilk_VAD->HPstate;
   psSilk_VAD->HPstate = HPstateTmp;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      decimated_framelength = silk_RSHIFT( psEncC->frame_length, silk_min_int( VAD_N_BANDS - b, VAD_N_BANDS - 1 ) );
      dec_subframe_length = silk_RSHIFT( decimated_framelength, VAD_INTERNAL_SUBFRAMES_LOG2 );
      dec_subframe_offset = 0;
      Xnrg[ b ] = psSilk_VAD->XnrgSubfr[ b ];
      for( s = 0; s < VAD_INTERNAL_SUBFRAMES; s++ ) {
         sumSquared = 0;
         for( i = 0; i < dec_subframe_length; i++ ) {
            x_tmp = silk_RSHIFT(
               X[ X_offset[ b ] + i + dec_subframe_offset ], 3 );
            sumSquared = silk_SMLABB( sumSquared, x_tmp, x_tmp );
            silk_assert( sumSquared >= 0 );
         }
         if( s < VAD_INTERNAL_SUBFRAMES - 1 ) {
            Xnrg[ b ] = silk_ADD_POS_SAT32( Xnrg[ b ], sumSquared );
         } else {
            Xnrg[ b ] = silk_ADD_POS_SAT32( Xnrg[ b ], silk_RSHIFT( sumSquared, 1 ) );
         }
         dec_subframe_offset += dec_subframe_length;
      }
      psSilk_VAD->XnrgSubfr[ b ] = sumSquared;
   }
   silk_VAD_GetNoiseLevels( &Xnrg[ 0 ], psSilk_VAD );
   sumSquared = 0;
   input_tilt = 0;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      speech_nrg = Xnrg[ b ] - psSilk_VAD->NL[ b ];
      if( speech_nrg > 0 ) {
         if( ( Xnrg[ b ] & 0xFF800000 ) == 0 ) {
            NrgToNoiseRatio_Q8[ b ] = silk_DIV32( silk_LSHIFT( Xnrg[ b ], 8 ), psSilk_VAD->NL[ b ] + 1 );
         } else {
            NrgToNoiseRatio_Q8[ b ] = silk_DIV32( Xnrg[ b ], silk_RSHIFT( psSilk_VAD->NL[ b ], 8 ) + 1 );
         }
         SNR_Q7 = silk_lin2log( NrgToNoiseRatio_Q8[ b ] ) - 8 * 128;
         sumSquared = silk_SMLABB( sumSquared, SNR_Q7, SNR_Q7 );
         if( speech_nrg < ( (fl_i32)1 << 20 ) ) {
            SNR_Q7 = silk_SMULWB( silk_LSHIFT( silk_SQRT_APPROX( speech_nrg ), 6 ), SNR_Q7 );
         }
         input_tilt = silk_SMLAWB( input_tilt, tiltWeights[ b ], SNR_Q7 );
      } else {
         NrgToNoiseRatio_Q8[ b ] = 256;
      }
   }
   sumSquared = silk_DIV32_16( sumSquared, VAD_N_BANDS );
   pSNR_dB_Q7 = (fl_i16)( 3 * silk_SQRT_APPROX( sumSquared ) );
   SA_Q15 = silk_sigm_Q15( silk_SMULWB( VAD_SNR_FACTOR_Q16, pSNR_dB_Q7 ) - VAD_NEGATIVE_OFFSET_Q5 );
   psEncC->input_tilt_Q15 = silk_LSHIFT( silk_sigm_Q15( input_tilt ) - 16384, 1 );
   speech_nrg = 0;
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      speech_nrg += ( b + 1 ) * silk_RSHIFT( Xnrg[ b ] - psSilk_VAD->NL[ b ], 4 );
   }
   if( psEncC->frame_length == 20 * psEncC->fs_kHz ) {
      speech_nrg = silk_RSHIFT32( speech_nrg, 1 );
   }
   if( speech_nrg <= 0 ) {
      SA_Q15 = silk_RSHIFT( SA_Q15, 1 );
   } else if( speech_nrg < 16384 ) {
      speech_nrg = silk_LSHIFT32( speech_nrg, 16 );
      speech_nrg = silk_SQRT_APPROX( speech_nrg );
      SA_Q15 = silk_SMULWB( 32768 + speech_nrg, SA_Q15 );
   }
   psEncC->speech_activity_Q8 = silk_min_int( silk_RSHIFT( SA_Q15, 7 ), silk_uint8_MAX );
   smooth_coef_Q16 = silk_SMULWB( VAD_SNR_SMOOTH_COEF_Q18, silk_SMULWB( (fl_i32)SA_Q15, SA_Q15 ) );
   if( psEncC->frame_length == 10 * psEncC->fs_kHz ) {
      smooth_coef_Q16 >>= 1;
   }
   for( b = 0; b < VAD_N_BANDS; b++ ) {
      psSilk_VAD->NrgRatioSmth_Q8[ b ] = silk_SMLAWB( psSilk_VAD->NrgRatioSmth_Q8[ b ],
         NrgToNoiseRatio_Q8[ b ] - psSilk_VAD->NrgRatioSmth_Q8[ b ], smooth_coef_Q16 );
      SNR_Q7 = 3 * ( silk_lin2log( psSilk_VAD->NrgRatioSmth_Q8[b] ) - 8 * 128 );
      psEncC->input_quality_bands_Q15[ b ] = silk_sigm_Q15( silk_RSHIFT( SNR_Q7 - 16 * 128, 4 ) );
   }
   RESTORE_STACK;
   return( ret );
}

static void silk_VAD_GetNoiseLevels(
   const fl_i32 pX[ VAD_N_BANDS ],
   silk_VAD_state *psSilk_VAD
)
{
   int k;
   fl_i32 nl, nrg, inv_nrg;
   int coef, min_coef;
   if( psSilk_VAD->counter < 1000 ) {
      min_coef = silk_DIV32_16( silk_int16_MAX, silk_RSHIFT( psSilk_VAD->counter, 4 ) + 1 );
      psSilk_VAD->counter++;
   } else {
      min_coef = 0;
   }
   for( k = 0; k < VAD_N_BANDS; k++ ) {
      nl = psSilk_VAD->NL[ k ];
      silk_assert( nl >= 0 );
      nrg = silk_ADD_POS_SAT32( pX[ k ], psSilk_VAD->NoiseLevelBias[ k ] );
      silk_assert( nrg > 0 );
      inv_nrg = silk_DIV32( silk_int32_MAX, nrg );
      silk_assert( inv_nrg >= 0 );
      if( nrg > silk_LSHIFT( nl, 3 ) ) {
         coef = VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 >> 3;
      } else if( nrg < nl ) {
         coef = VAD_NOISE_LEVEL_SMOOTH_COEF_Q16;
      } else {
         coef = silk_SMULWB( silk_SMULWW( inv_nrg, nl ), VAD_NOISE_LEVEL_SMOOTH_COEF_Q16 << 1 );
      }
      coef = silk_max_int( coef, min_coef );
      psSilk_VAD->inv_NL[ k ] = silk_SMLAWB( psSilk_VAD->inv_NL[ k ], inv_nrg - psSilk_VAD->inv_NL[ k ], coef );
      silk_assert( psSilk_VAD->inv_NL[ k ] >= 0 );
      nl = silk_DIV32( silk_int32_MAX, psSilk_VAD->inv_NL[ k ] );
      silk_assert( nl >= 0 );
      nl = silk_min( nl, 0x00FFFFFF );
      psSilk_VAD->NL[ k ] = nl;
   }
}

static fl_i32 silk_inner_prod_aligned_scale(
   const fl_i16 *const inVec1,
   const fl_i16 *const inVec2,
   const int scale,
   const int len
)
{
   int i;
   fl_i32 sum = 0;
   for( i = 0; i < len; i++ ) {
      sum = silk_ADD_RSHIFT32( sum, silk_SMULBB( inVec1[ i ], inVec2[ i ] ), scale );
   }
   return sum;
}

static fl_i32 silk_stereo_find_predictor(
   fl_i32 *ratio_Q14,
   const fl_i16 x[],
   const fl_i16 y[],
   fl_i32 mid_res_amp_Q0[],
   int length,
   int smooth_coef_Q16
)
{
   int scale, scale1, scale2;
   fl_i32 nrgx, nrgy, corr, pred_Q13, pred2_Q10;
   silk_sum_sqr_shift( &nrgx, &scale1, x, length );
   silk_sum_sqr_shift( &nrgy, &scale2, y, length );
   scale = silk_max_int( scale1, scale2 );
   scale = scale + ( scale & 1 );
   nrgy = silk_RSHIFT32( nrgy, scale - scale2 );
   nrgx = silk_RSHIFT32( nrgx, scale - scale1 );
   nrgx = silk_max_int( nrgx, 1 );
   corr = silk_inner_prod_aligned_scale( x, y, scale, length );
   pred_Q13 = silk_DIV32_varQ( corr, nrgx, 13 );
   pred_Q13 = silk_LIMIT( pred_Q13, -(1 << 14), 1 << 14 );
   pred2_Q10 = silk_SMULWB( pred_Q13, pred_Q13 );
   smooth_coef_Q16 = (int)silk_max_int( smooth_coef_Q16, silk_abs( pred2_Q10 ) );
   silk_assert( smooth_coef_Q16 < 32768 );
   scale = silk_RSHIFT( scale, 1 );
   mid_res_amp_Q0[ 0 ] = silk_SMLAWB( mid_res_amp_Q0[ 0 ], silk_LSHIFT( silk_SQRT_APPROX( nrgx ), scale ) - mid_res_amp_Q0[ 0 ],
      smooth_coef_Q16 );
   nrgy = silk_SUB_LSHIFT32( nrgy, silk_SMULWB( corr, pred_Q13 ), 3 + 1 );
   nrgy = silk_ADD_LSHIFT32( nrgy, silk_SMULWB( nrgx, pred2_Q10 ), 6 );
   mid_res_amp_Q0[ 1 ] = silk_SMLAWB( mid_res_amp_Q0[ 1 ], silk_LSHIFT( silk_SQRT_APPROX( nrgy ), scale ) - mid_res_amp_Q0[ 1 ],
      smooth_coef_Q16 );
   *ratio_Q14 = silk_DIV32_varQ( mid_res_amp_Q0[ 1 ], silk_max( mid_res_amp_Q0[ 0 ], 1 ), 14 );
   *ratio_Q14 = silk_LIMIT( *ratio_Q14, 0, 32767 );
   return pred_Q13;
}

static void silk_stereo_quant_pred(
   fl_i32 pred_Q13[],
   fl_i8 ix[ 2 ][ 3 ]
)
{
   int i, j, n;
   fl_i32 low_Q13, step_Q13, lvl_Q13, err_min_Q13, err_Q13, quant_pred_Q13 = 0;
   for( n = 0; n < 2; n++ ) {
      err_min_Q13 = silk_int32_MAX;
      for( i = 0; i < STEREO_QUANT_TAB_SIZE - 1; i++ ) {
         low_Q13 = fl_stereo_pred_quant_Q13[ i ];
         step_Q13 = silk_SMULWB( fl_stereo_pred_quant_Q13[ i + 1 ] - low_Q13,
            SILK_FIX_CONST( 0.5 / STEREO_QUANT_SUB_STEPS, 16 ) );
         for( j = 0; j < STEREO_QUANT_SUB_STEPS; j++ ) {
            lvl_Q13 = silk_SMLABB( low_Q13, step_Q13, 2 * j + 1 );
            err_Q13 = silk_abs( pred_Q13[ n ] - lvl_Q13 );
            if( err_Q13 < err_min_Q13 ) {
               err_min_Q13 = err_Q13;
               quant_pred_Q13 = lvl_Q13;
               ix[ n ][ 0 ] = i;
               ix[ n ][ 1 ] = j;
            } else {
               goto done;
            }
         }
      }
      done:
      ix[ n ][ 2 ] = silk_DIV32_16( ix[ n ][ 0 ], 3 );
      ix[ n ][ 0 ] -= ix[ n ][ 2 ] * 3;
      pred_Q13[ n ] = quant_pred_Q13;
   }
   pred_Q13[ 0 ] -= pred_Q13[ 1 ];
}

static void silk_stereo_encode_pred(
   ec_ctx *psRangeEnc,
   fl_i8 ix[ 2 ][ 3 ]
)
{
   int n;
   n = 5 * ix[ 0 ][ 2 ] + ix[ 1 ][ 2 ];
   celt_assert( n < 25 );
   ec_enc_icdf( psRangeEnc, n, fl_stereo_pred_joint_iCDF, 8 );
   for( n = 0; n < 2; n++ ) {
      celt_assert( ix[ n ][ 0 ] < 3 );
      celt_assert( ix[ n ][ 1 ] < STEREO_QUANT_SUB_STEPS );
      ec_enc_icdf( psRangeEnc, ix[ n ][ 0 ], fl_uniform3_iCDF, 8 );
      ec_enc_icdf( psRangeEnc, ix[ n ][ 1 ], fl_uniform5_iCDF, 8 );
   }
}

static void silk_stereo_encode_mid_only(
   ec_ctx *psRangeEnc,
   fl_i8 mid_only_flag
)
{
   ec_enc_icdf( psRangeEnc, mid_only_flag, fl_stereo_only_code_mid_iCDF, 8 );
}

static void silk_stereo_LR_to_MS(
   stereo_enc_state *state,
   fl_i16 x1[],
   fl_i16 x2[],
   fl_i8 ix[ 2 ][ 3 ],
   fl_i8 *mid_only_flag,
   fl_i32 mid_side_rates_bps[],
   fl_i32 total_rate_bps,
   int prev_speech_act_Q8,
   int toMono,
   int fs_kHz,
   int frame_length
)
{
   int n, is10msFrame, denom_Q16, delta0_Q13, delta1_Q13;
   fl_i32 sum, diff, smooth_coef_Q16, pred_Q13[ 2 ], pred0_Q13, pred1_Q13;
   fl_i32 LP_ratio_Q14, HP_ratio_Q14, frac_Q16, frac_3_Q16, min_mid_rate_bps, width_Q14, w_Q24, deltaw_Q24;
   VARDECL( fl_i16, side );
   VARDECL( fl_i16, LP_mid );
   VARDECL( fl_i16, HP_mid );
   VARDECL( fl_i16, LP_side );
   VARDECL( fl_i16, HP_side );
   fl_i16 *mid = &x1[ -2 ];
   SAVE_STACK;
   ALLOC( side, frame_length + 2, fl_i16 );
   for( n = 0; n < frame_length + 2; n++ ) {
      sum = x1[ n - 2 ] + (fl_i32)x2[ n - 2 ];
      diff = x1[ n - 2 ] - (fl_i32)x2[ n - 2 ];
      mid[ n ] = (fl_i16)silk_RSHIFT_ROUND( sum, 1 );
      side[ n ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( diff, 1 ) );
   }
   silk_memcpy( mid, state->sMid, 2 * sizeof( fl_i16 ) );
   silk_memcpy( side, state->sSide, 2 * sizeof( fl_i16 ) );
   silk_memcpy( state->sMid, &mid[ frame_length ], 2 * sizeof( fl_i16 ) );
   silk_memcpy( state->sSide, &side[ frame_length ], 2 * sizeof( fl_i16 ) );
   ALLOC( LP_mid, frame_length, fl_i16 );
   ALLOC( HP_mid, frame_length, fl_i16 );
   for( n = 0; n < frame_length; n++ ) {
      sum = silk_RSHIFT_ROUND( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 2 );
      LP_mid[ n ] = sum;
      HP_mid[ n ] = mid[ n + 1 ] - sum;
   }
   ALLOC( LP_side, frame_length, fl_i16 );
   ALLOC( HP_side, frame_length, fl_i16 );
   for( n = 0; n < frame_length; n++ ) {
      sum = silk_RSHIFT_ROUND( silk_ADD_LSHIFT32( side[ n ] + (fl_i32)side[ n + 2 ], side[ n + 1 ], 1 ), 2 );
      LP_side[ n ] = sum;
      HP_side[ n ] = side[ n + 1 ] - sum;
   }
   is10msFrame = frame_length == 10 * fs_kHz;
   smooth_coef_Q16 = is10msFrame ?
      SILK_FIX_CONST( STEREO_RATIO_SMOOTH_COEF / 2, 16 ) :
      SILK_FIX_CONST( STEREO_RATIO_SMOOTH_COEF, 16 );
   smooth_coef_Q16 = silk_SMULWB( silk_SMULBB( prev_speech_act_Q8, prev_speech_act_Q8 ), smooth_coef_Q16 );
   pred_Q13[ 0 ] = silk_stereo_find_predictor( &LP_ratio_Q14, LP_mid, LP_side, &state->mid_side_amp_Q0[ 0 ], frame_length, smooth_coef_Q16 );
   pred_Q13[ 1 ] = silk_stereo_find_predictor( &HP_ratio_Q14, HP_mid, HP_side, &state->mid_side_amp_Q0[ 2 ], frame_length, smooth_coef_Q16 );
   frac_Q16 = silk_SMLABB( HP_ratio_Q14, LP_ratio_Q14, 3 );
   frac_Q16 = silk_min( frac_Q16, SILK_FIX_CONST( 1, 16 ) );
   total_rate_bps -= is10msFrame ? 1200 : 600;
   if( total_rate_bps < 1 ) {
      total_rate_bps = 1;
   }
   min_mid_rate_bps = silk_SMLABB( 2000, fs_kHz, 600 );
   silk_assert( min_mid_rate_bps < 32767 );
   frac_3_Q16 = silk_MUL( 3, frac_Q16 );
   mid_side_rates_bps[ 0 ] = silk_DIV32_varQ( total_rate_bps, SILK_FIX_CONST( 8 + 5, 16 ) + frac_3_Q16, 16+3 );
   if( mid_side_rates_bps[ 0 ] < min_mid_rate_bps ) {
      mid_side_rates_bps[ 0 ] = min_mid_rate_bps;
      mid_side_rates_bps[ 1 ] = total_rate_bps - mid_side_rates_bps[ 0 ];
      width_Q14 = silk_DIV32_varQ( silk_LSHIFT( mid_side_rates_bps[ 1 ], 1 ) - min_mid_rate_bps,
         silk_SMULWB( SILK_FIX_CONST( 1, 16 ) + frac_3_Q16, min_mid_rate_bps ), 14+2 );
      width_Q14 = silk_LIMIT( width_Q14, 0, SILK_FIX_CONST( 1, 14 ) );
   } else {
      mid_side_rates_bps[ 1 ] = total_rate_bps - mid_side_rates_bps[ 0 ];
      width_Q14 = SILK_FIX_CONST( 1, 14 );
   }
   state->smth_width_Q14 = (fl_i16)silk_SMLAWB( state->smth_width_Q14, width_Q14 - state->smth_width_Q14, smooth_coef_Q16 );
   *mid_only_flag = 0;
   if( toMono ) {
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
      silk_stereo_quant_pred( pred_Q13, ix );
   } else if( state->width_prev_Q14 == 0 &&
      ( 8 * total_rate_bps < 13 * min_mid_rate_bps || silk_SMULWB( frac_Q16, state->smth_width_Q14 ) < SILK_FIX_CONST( 0.05, 14 ) ) )
   {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
      mid_side_rates_bps[ 0 ] = total_rate_bps;
      mid_side_rates_bps[ 1 ] = 0;
      *mid_only_flag = 1;
   } else if( state->width_prev_Q14 != 0 &&
      ( 8 * total_rate_bps < 11 * min_mid_rate_bps || silk_SMULWB( frac_Q16, state->smth_width_Q14 ) < SILK_FIX_CONST( 0.02, 14 ) ) )
   {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = 0;
      pred_Q13[ 0 ] = 0;
      pred_Q13[ 1 ] = 0;
   } else if( state->smth_width_Q14 > SILK_FIX_CONST( 0.95, 14 ) ) {
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = SILK_FIX_CONST( 1, 14 );
   } else {
      pred_Q13[ 0 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 0 ] ), 14 );
      pred_Q13[ 1 ] = silk_RSHIFT( silk_SMULBB( state->smth_width_Q14, pred_Q13[ 1 ] ), 14 );
      silk_stereo_quant_pred( pred_Q13, ix );
      width_Q14 = state->smth_width_Q14;
   }
   if( *mid_only_flag == 1 ) {
      state->silent_side_len += frame_length - STEREO_INTERP_LEN_MS * fs_kHz;
      if( state->silent_side_len < LA_SHAPE_MS * fs_kHz ) {
         *mid_only_flag = 0;
      } else {
         state->silent_side_len = 10000;
      }
   } else {
      state->silent_side_len = 0;
   }
   if( *mid_only_flag == 0 && mid_side_rates_bps[ 1 ] < 1 ) {
      mid_side_rates_bps[ 1 ] = 1;
      mid_side_rates_bps[ 0 ] = silk_max_int( 1, total_rate_bps - mid_side_rates_bps[ 1 ]);
   }
   pred0_Q13 = -state->pred_prev_Q13[ 0 ];
   pred1_Q13 = -state->pred_prev_Q13[ 1 ];
   w_Q24 = silk_LSHIFT( state->width_prev_Q14, 10 );
   denom_Q16 = silk_DIV32_16( (fl_i32)1 << 16, STEREO_INTERP_LEN_MS * fs_kHz );
   delta0_Q13 = -silk_RSHIFT_ROUND( silk_SMULBB( pred_Q13[ 0 ] - state->pred_prev_Q13[ 0 ], denom_Q16 ), 16 );
   delta1_Q13 = -silk_RSHIFT_ROUND( silk_SMULBB( pred_Q13[ 1 ] - state->pred_prev_Q13[ 1 ], denom_Q16 ), 16 );
   deltaw_Q24 = silk_LSHIFT( silk_SMULWB( width_Q14 - state->width_prev_Q14, denom_Q16 ), 10 );
   for( n = 0; n < STEREO_INTERP_LEN_MS * fs_kHz; n++ ) {
      pred0_Q13 += delta0_Q13;
      pred1_Q13 += delta1_Q13;
      w_Q24 += deltaw_Q24;
      sum = silk_LSHIFT( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 9 );
      sum = silk_SMLAWB( silk_SMULWB( w_Q24, side[ n + 1 ] ), sum, pred0_Q13 );
      sum = silk_SMLAWB( sum, silk_LSHIFT( (fl_i32)mid[ n + 1 ], 11 ), pred1_Q13 );
      x2[ n - 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( sum, 8 ) );
   }
   pred0_Q13 = -pred_Q13[ 0 ];
   pred1_Q13 = -pred_Q13[ 1 ];
   w_Q24 = silk_LSHIFT( width_Q14, 10 );
   for( n = STEREO_INTERP_LEN_MS * fs_kHz; n < frame_length; n++ ) {
      sum = silk_LSHIFT( silk_ADD_LSHIFT32( mid[ n ] + (fl_i32)mid[ n + 2 ], mid[ n + 1 ], 1 ), 9 );
      sum = silk_SMLAWB( silk_SMULWB( w_Q24, side[ n + 1 ] ), sum, pred0_Q13 );
      sum = silk_SMLAWB( sum, silk_LSHIFT( (fl_i32)mid[ n + 1 ], 11 ), pred1_Q13 );
      x2[ n - 1 ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( sum, 8 ) );
   }
   state->pred_prev_Q13[ 0 ] = (fl_i16)pred_Q13[ 0 ];
   state->pred_prev_Q13[ 1 ] = (fl_i16)pred_Q13[ 1 ];
   state->width_prev_Q14 = (fl_i16)width_Q14;
   RESTORE_STACK;
}

static inline void silk_A2NLSF_trans_poly(
   fl_i32 *p,
   const int dd
)
{
   int k, n;
   for( k = 2; k <= dd; k++ ) {
      for( n = dd; n > k; n-- ) {
         p[ n - 2 ] -= p[ n ];
      }
      p[ k - 2 ] -= silk_LSHIFT( p[ k ], 1 );
   }
}

static inline fl_i32 silk_A2NLSF_eval_poly(
   fl_i32 *p,
   const fl_i32 x,
   const int dd
)
{
   int n;
   fl_i32 x_Q16, y32;
   y32 = p[ dd ];
   x_Q16 = silk_LSHIFT( x, 4 );
   if ( opus_likely( 8 == dd ) )
   {
      y32 = silk_SMLAWW( p[ 7 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 6 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 5 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 4 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 3 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 2 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 1 ], y32, x_Q16 );
      y32 = silk_SMLAWW( p[ 0 ], y32, x_Q16 );
   }
   else
   {
      for( n = dd - 1; n >= 0; n-- ) {
         y32 = silk_SMLAWW( p[ n ], y32, x_Q16 );
      }
   }
   return y32;
}

static inline void silk_A2NLSF_init(
   const fl_i32 *a_Q16,
   fl_i32 *P,
   fl_i32 *Q,
   const int dd
)
{
   int k;
   P[dd] = silk_LSHIFT( 1, 16 );
   Q[dd] = silk_LSHIFT( 1, 16 );
   for( k = 0; k < dd; k++ ) {
      P[ k ] = -a_Q16[ dd - k - 1 ] - a_Q16[ dd + k ];
      Q[ k ] = -a_Q16[ dd - k - 1 ] + a_Q16[ dd + k ];
   }
   for( k = dd; k > 0; k-- ) {
      P[ k - 1 ] -= P[ k ];
      Q[ k - 1 ] += Q[ k ];
   }
   silk_A2NLSF_trans_poly( P, dd );
   silk_A2NLSF_trans_poly( Q, dd );
}

static void silk_A2NLSF(
   fl_i16 *NLSF,
   fl_i32 *a_Q16,
   const int d
)
{
   int i, k, m, dd, root_ix, ffrac;
   fl_i32 xlo, xhi, xmid;
   fl_i32 ylo, yhi, ymid, thr;
   fl_i32 nom, den;
   fl_i32 P[ SILK_MAX_ORDER_LPC / 2 + 1 ];
   fl_i32 Q[ SILK_MAX_ORDER_LPC / 2 + 1 ];
   fl_i32 *PQ[ 2 ];
   fl_i32 *p;
   PQ[ 0 ] = P;
   PQ[ 1 ] = Q;
   dd = silk_RSHIFT( d, 1 );
   silk_A2NLSF_init( a_Q16, P, Q, dd );
   p = P;
   xlo = fl_LSFCosTab_FIX_Q12[ 0 ];
   ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
   if( ylo < 0 ) {
      NLSF[ 0 ] = 0;
      p = Q;
      ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
      root_ix = 1;
   } else {
      root_ix = 0;
   }
   k = 1;
   i = 0;
   thr = 0;
   while( 1 ) {
      xhi = fl_LSFCosTab_FIX_Q12[ k ];
      yhi = silk_A2NLSF_eval_poly( p, xhi, dd );
      if( ( ylo <= 0 && yhi >= thr ) || ( ylo >= 0 && yhi <= -thr ) ) {
         if( yhi == 0 ) {
            thr = 1;
         } else {
            thr = 0;
         }
         ffrac = -256;
         for( m = 0; m < BIN_DIV_STEPS_A2NLSF_FIX; m++ ) {
            xmid = silk_RSHIFT_ROUND( xlo + xhi, 1 );
            ymid = silk_A2NLSF_eval_poly( p, xmid, dd );
            if( ( ylo <= 0 && ymid >= 0 ) || ( ylo >= 0 && ymid <= 0 ) ) {
               xhi = xmid;
               yhi = ymid;
            } else {
               xlo = xmid;
               ylo = ymid;
               ffrac = silk_ADD_RSHIFT( ffrac, 128, m );
            }
         }
         if( silk_abs( ylo ) < 65536 ) {
            den = ylo - yhi;
            nom = silk_LSHIFT( ylo, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) + silk_RSHIFT( den, 1 );
            if( den != 0 ) {
               ffrac += silk_DIV32( nom, den );
            }
         } else {
            ffrac += silk_DIV32( ylo, silk_RSHIFT( ylo - yhi, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) );
         }
         NLSF[ root_ix ] = (fl_i16)silk_min_32( silk_LSHIFT( (fl_i32)k, 8 ) + ffrac, silk_int16_MAX );
         silk_assert( NLSF[ root_ix ] >= 0 );
         root_ix++;
         if( root_ix >= d ) {
            break;
         }
         p = PQ[ root_ix & 1 ];
         xlo = fl_LSFCosTab_FIX_Q12[ k - 1 ];
         ylo = silk_LSHIFT( 1 - ( root_ix & 2 ), 12 );
      } else {
         k++;
         xlo = xhi;
         ylo = yhi;
         thr = 0;
         if( k > LSF_COS_TAB_SZ_FIX ) {
            i++;
            if( i > MAX_ITERATIONS_A2NLSF_FIX ) {
               NLSF[ 0 ] = (fl_i16)silk_DIV32_16( 1 << 15, d + 1 );
               for( k = 1; k < d; k++ ) {
                  NLSF[ k ] = (fl_i16)silk_ADD16( NLSF[ k-1 ], NLSF[ 0 ] );
               }
               return;
            }
            silk_bwexpander_32( a_Q16, d, 65536 - silk_LSHIFT( 1, i ) );
            silk_A2NLSF_init( a_Q16, P, Q, dd );
            p = P;
            xlo = fl_LSFCosTab_FIX_Q12[ 0 ];
            ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
            if( ylo < 0 ) {
               NLSF[ 0 ] = 0;
               p = Q;
               ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
               root_ix = 1;
            } else {
               root_ix = 0;
            }
            k = 1;
         }
      }
   }
}

static void silk_NLSF_VQ_weights_laroia(
   fl_i16 *pNLSFW_Q_OUT,
   const fl_i16 *pNLSF_Q15,
   const int D
)
{
   int k;
   fl_i32 tmp1_int, tmp2_int;
   celt_assert( D > 0 );
   celt_assert( ( D & 1 ) == 0 );
   tmp1_int = silk_max_int( pNLSF_Q15[ 0 ], 1 );
   tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
   tmp2_int = silk_max_int( pNLSF_Q15[ 1 ] - pNLSF_Q15[ 0 ], 1 );
   tmp2_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp2_int );
   pNLSFW_Q_OUT[ 0 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
   silk_assert( pNLSFW_Q_OUT[ 0 ] > 0 );
   for( k = 1; k < D - 1; k += 2 ) {
      tmp1_int = silk_max_int( pNLSF_Q15[ k + 1 ] - pNLSF_Q15[ k ], 1 );
      tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
      pNLSFW_Q_OUT[ k ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
      silk_assert( pNLSFW_Q_OUT[ k ] > 0 );
      tmp2_int = silk_max_int( pNLSF_Q15[ k + 2 ] - pNLSF_Q15[ k + 1 ], 1 );
      tmp2_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp2_int );
      pNLSFW_Q_OUT[ k + 1 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
      silk_assert( pNLSFW_Q_OUT[ k + 1 ] > 0 );
   }
   tmp1_int = silk_max_int( ( 1 << 15 ) - pNLSF_Q15[ D - 1 ], 1 );
   tmp1_int = silk_DIV32_16( (fl_i32)1 << ( 15 + NLSF_W_Q ), tmp1_int );
   pNLSFW_Q_OUT[ D - 1 ] = (fl_i16)silk_min_int( tmp1_int + tmp2_int, silk_int16_MAX );
   silk_assert( pNLSFW_Q_OUT[ D - 1 ] > 0 );
}

static void silk_NLSF_VQ(
   fl_i32 err_Q24[],
   const fl_i16 in_Q15[],
   const fl_u8 pCB_Q8[],
   const fl_i16 pWght_Q9[],
   const int K,
   const int LPC_order
)
{
   int i, m;
   fl_i32 diff_Q15, diffw_Q24, sum_error_Q24, pred_Q24;
   const fl_i16 *w_Q9_ptr;
   const fl_u8 *cb_Q8_ptr;
   celt_assert( ( LPC_order & 1 ) == 0 );
   cb_Q8_ptr = pCB_Q8;
   w_Q9_ptr = pWght_Q9;
   for( i = 0; i < K; i++ ) {
      sum_error_Q24 = 0;
      pred_Q24 = 0;
      for( m = LPC_order-2; m >= 0; m -= 2 ) {
         diff_Q15 = silk_SUB_LSHIFT32( in_Q15[ m + 1 ], (fl_i32)cb_Q8_ptr[ m + 1 ], 7 );
         diffw_Q24 = silk_SMULBB( diff_Q15, w_Q9_ptr[ m + 1 ] );
         sum_error_Q24 = silk_ADD32( sum_error_Q24, silk_abs( silk_SUB_RSHIFT32( diffw_Q24, pred_Q24, 1 ) ) );
         pred_Q24 = diffw_Q24;
         diff_Q15 = silk_SUB_LSHIFT32( in_Q15[ m ], (fl_i32)cb_Q8_ptr[ m ], 7 );
         diffw_Q24 = silk_SMULBB( diff_Q15, w_Q9_ptr[ m ] );
         sum_error_Q24 = silk_ADD32( sum_error_Q24, silk_abs( silk_SUB_RSHIFT32( diffw_Q24, pred_Q24, 1 ) ) );
         pred_Q24 = diffw_Q24;
         silk_assert( sum_error_Q24 >= 0 );
      }
      err_Q24[ i ] = sum_error_Q24;
      cb_Q8_ptr += LPC_order;
      w_Q9_ptr += LPC_order;
   }
}

static fl_i32 silk_NLSF_del_dec_quant(
   fl_i8 indices[],
   const fl_i16 x_Q10[],
   const fl_i16 w_Q5[],
   const fl_u8 pred_coef_Q8[],
   const fl_i16 ec_ix[],
   const fl_u8 ec_rates_Q5[],
   const int quant_step_size_Q16,
   const fl_i16 inv_quant_step_size_Q6,
   const fl_i32 mu_Q20,
   const fl_i16 order
)
{
   int i, j, nStates, ind_tmp, ind_min_max, ind_max_min, in_Q10, res_Q10;
   int pred_Q10, diff_Q10, rate0_Q5, rate1_Q5;
   fl_i16 out0_Q10, out1_Q10;
   fl_i32 RD_tmp_Q25, min_Q25, min_max_Q25, max_min_Q25;
   int ind_sort[ NLSF_QUANT_DEL_DEC_STATES ];
   fl_i8 ind[ NLSF_QUANT_DEL_DEC_STATES ][ MAX_LPC_ORDER ];
   fl_i16 prev_out_Q10[ 2 * NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_Q25[ 2 * NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_min_Q25[ NLSF_QUANT_DEL_DEC_STATES ];
   fl_i32 RD_max_Q25[ NLSF_QUANT_DEL_DEC_STATES ];
   const fl_u8 *rates_Q5;
   int out0_Q10_table[2 * NLSF_QUANT_MAX_AMPLITUDE_EXT];
   int out1_Q10_table[2 * NLSF_QUANT_MAX_AMPLITUDE_EXT];
   for (i = -NLSF_QUANT_MAX_AMPLITUDE_EXT; i <= NLSF_QUANT_MAX_AMPLITUDE_EXT-1; i++)
   {
      out0_Q10 = silk_LSHIFT( i, 10 );
      out1_Q10 = silk_ADD16( out0_Q10, 1024 );
      if( i > 0 ) {
         out0_Q10 = silk_SUB16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
         out1_Q10 = silk_SUB16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else if( i == 0 ) {
         out1_Q10 = silk_SUB16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else if( i == -1 ) {
         out0_Q10 = silk_ADD16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      } else {
         out0_Q10 = silk_ADD16( out0_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
         out1_Q10 = silk_ADD16( out1_Q10, SILK_FIX_CONST( NLSF_QUANT_LEVEL_ADJ, 10 ) );
      }
      out0_Q10_table[ i + NLSF_QUANT_MAX_AMPLITUDE_EXT ] = silk_RSHIFT( silk_SMULBB( out0_Q10, quant_step_size_Q16 ), 16 );
      out1_Q10_table[ i + NLSF_QUANT_MAX_AMPLITUDE_EXT ] = silk_RSHIFT( silk_SMULBB( out1_Q10, quant_step_size_Q16 ), 16 );
   }
   silk_assert( (NLSF_QUANT_DEL_DEC_STATES & (NLSF_QUANT_DEL_DEC_STATES-1)) == 0 );
   nStates = 1;
   RD_Q25[ 0 ] = 0;
   prev_out_Q10[ 0 ] = 0;
   for( i = order - 1; i >= 0; i-- ) {
      rates_Q5 = &ec_rates_Q5[ ec_ix[ i ] ];
      in_Q10 = x_Q10[ i ];
      for( j = 0; j < nStates; j++ ) {
         pred_Q10 = silk_RSHIFT( silk_SMULBB( (fl_i16)pred_coef_Q8[ i ], prev_out_Q10[ j ] ), 8 );
         res_Q10 = silk_SUB16( in_Q10, pred_Q10 );
         ind_tmp = silk_RSHIFT( silk_SMULBB( inv_quant_step_size_Q6, res_Q10 ), 16 );
         ind_tmp = silk_LIMIT( ind_tmp, -NLSF_QUANT_MAX_AMPLITUDE_EXT, NLSF_QUANT_MAX_AMPLITUDE_EXT-1 );
         ind[ j ][ i ] = (fl_i8)ind_tmp;
         out0_Q10 = out0_Q10_table[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE_EXT ];
         out1_Q10 = out1_Q10_table[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE_EXT ];
         out0_Q10 = silk_ADD16( out0_Q10, pred_Q10 );
         out1_Q10 = silk_ADD16( out1_Q10, pred_Q10 );
         prev_out_Q10[ j ] = out0_Q10;
         prev_out_Q10[ j + nStates ] = out1_Q10;
         if( ind_tmp + 1 >= NLSF_QUANT_MAX_AMPLITUDE ) {
            if( ind_tmp + 1 == NLSF_QUANT_MAX_AMPLITUDE ) {
               rate0_Q5 = rates_Q5[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE ];
               rate1_Q5 = 280;
            } else {
               rate0_Q5 = silk_SMLABB( 280 - 43 * NLSF_QUANT_MAX_AMPLITUDE, 43, ind_tmp );
               rate1_Q5 = silk_ADD16( rate0_Q5, 43 );
            }
         } else if( ind_tmp <= -NLSF_QUANT_MAX_AMPLITUDE ) {
            if( ind_tmp == -NLSF_QUANT_MAX_AMPLITUDE ) {
               rate0_Q5 = 280;
               rate1_Q5 = rates_Q5[ ind_tmp + 1 + NLSF_QUANT_MAX_AMPLITUDE ];
            } else {
               rate0_Q5 = silk_SMLABB( 280 - 43 * NLSF_QUANT_MAX_AMPLITUDE, -43, ind_tmp );
               rate1_Q5 = silk_SUB16( rate0_Q5, 43 );
            }
         } else {
            rate0_Q5 = rates_Q5[ ind_tmp + NLSF_QUANT_MAX_AMPLITUDE ];
            rate1_Q5 = rates_Q5[ ind_tmp + 1 + NLSF_QUANT_MAX_AMPLITUDE ];
         }
         RD_tmp_Q25 = RD_Q25[ j ];
         diff_Q10 = silk_SUB16( in_Q10, out0_Q10 );
         RD_Q25[ j ] = silk_SMLABB( silk_MLA( RD_tmp_Q25, silk_SMULBB( diff_Q10, diff_Q10 ), w_Q5[ i ] ), mu_Q20, rate0_Q5 );
         diff_Q10 = silk_SUB16( in_Q10, out1_Q10 );
         RD_Q25[ j + nStates ] = silk_SMLABB( silk_MLA( RD_tmp_Q25, silk_SMULBB( diff_Q10, diff_Q10 ), w_Q5[ i ] ), mu_Q20, rate1_Q5 );
      }
      if( nStates <= NLSF_QUANT_DEL_DEC_STATES/2 ) {
         for( j = 0; j < nStates; j++ ) {
            ind[ j + nStates ][ i ] = ind[ j ][ i ] + 1;
         }
         nStates = silk_LSHIFT( nStates, 1 );
         for( j = nStates; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            ind[ j ][ i ] = ind[ j - nStates ][ i ];
         }
      } else {
         for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            if( RD_Q25[ j ] > RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ] ) {
               RD_max_Q25[ j ] = RD_Q25[ j ];
               RD_min_Q25[ j ] = RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ];
               RD_Q25[ j ] = RD_min_Q25[ j ];
               RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ] = RD_max_Q25[ j ];
               out0_Q10 = prev_out_Q10[ j ];
               prev_out_Q10[ j ] = prev_out_Q10[ j + NLSF_QUANT_DEL_DEC_STATES ];
               prev_out_Q10[ j + NLSF_QUANT_DEL_DEC_STATES ] = out0_Q10;
               ind_sort[ j ] = j + NLSF_QUANT_DEL_DEC_STATES;
            } else {
               RD_min_Q25[ j ] = RD_Q25[ j ];
               RD_max_Q25[ j ] = RD_Q25[ j + NLSF_QUANT_DEL_DEC_STATES ];
               ind_sort[ j ] = j;
            }
         }
         while( 1 ) {
            min_max_Q25 = silk_int32_MAX;
            max_min_Q25 = 0;
            ind_min_max = 0;
            ind_max_min = 0;
            for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
               if( min_max_Q25 > RD_max_Q25[ j ] ) {
                  min_max_Q25 = RD_max_Q25[ j ];
                  ind_min_max = j;
               }
               if( max_min_Q25 < RD_min_Q25[ j ] ) {
                  max_min_Q25 = RD_min_Q25[ j ];
                  ind_max_min = j;
               }
            }
            if( min_max_Q25 >= max_min_Q25 ) {
               break;
            }
            ind_sort[ ind_max_min ] = ind_sort[ ind_min_max ] ^ NLSF_QUANT_DEL_DEC_STATES;
            RD_Q25[ ind_max_min ] = RD_Q25[ ind_min_max + NLSF_QUANT_DEL_DEC_STATES ];
            prev_out_Q10[ ind_max_min ] = prev_out_Q10[ ind_min_max + NLSF_QUANT_DEL_DEC_STATES ];
            RD_min_Q25[ ind_max_min ] = 0;
            RD_max_Q25[ ind_min_max ] = silk_int32_MAX;
            silk_memcpy( ind[ ind_max_min ], ind[ ind_min_max ], MAX_LPC_ORDER * sizeof( fl_i8 ) );
         }
         for( j = 0; j < NLSF_QUANT_DEL_DEC_STATES; j++ ) {
            ind[ j ][ i ] += silk_RSHIFT( ind_sort[ j ], NLSF_QUANT_DEL_DEC_STATES_LOG2 );
         }
      }
   }
   ind_tmp = 0;
   min_Q25 = silk_int32_MAX;
   for( j = 0; j < 2 * NLSF_QUANT_DEL_DEC_STATES; j++ ) {
      if( min_Q25 > RD_Q25[ j ] ) {
         min_Q25 = RD_Q25[ j ];
         ind_tmp = j;
      }
   }
   for( j = 0; j < order; j++ ) {
      indices[ j ] = ind[ ind_tmp & ( NLSF_QUANT_DEL_DEC_STATES - 1 ) ][ j ];
      silk_assert( indices[ j ] >= -NLSF_QUANT_MAX_AMPLITUDE_EXT );
      silk_assert( indices[ j ] <= NLSF_QUANT_MAX_AMPLITUDE_EXT );
   }
   indices[ 0 ] += silk_RSHIFT( ind_tmp, NLSF_QUANT_DEL_DEC_STATES_LOG2 );
   silk_assert( indices[ 0 ] <= NLSF_QUANT_MAX_AMPLITUDE_EXT );
   silk_assert( min_Q25 >= 0 );
   return min_Q25;
}

static fl_i32 silk_NLSF_encode(
       fl_i8 *NLSFIndices,
       fl_i16 *pNLSF_Q15,
   const fl_nlsf_cb *psNLSF_CB,
   const fl_i16 *pW_Q2,
   const int NLSF_mu_Q20,
   const int nSurvivors,
   const int signalType
)
{
   int i, s, ind1, bestIndex, prob_Q8, bits_q7;
   fl_i32 W_tmp_Q9, ret;
   VARDECL( fl_i32, err_Q24 );
   VARDECL( fl_i32, RD_Q25 );
   VARDECL( int, tempIndices1 );
   VARDECL( fl_i8, tempIndices2 );
   fl_i16 res_Q10[ MAX_LPC_ORDER ];
   fl_i16 NLSF_tmp_Q15[ MAX_LPC_ORDER ];
   fl_i16 W_adj_Q5[ MAX_LPC_ORDER ];
   fl_u8 pred_Q8[ MAX_LPC_ORDER ];
   fl_i16 ec_ix[ MAX_LPC_ORDER ];
   const fl_u8 *pCB_element, *iCDF_ptr;
   const fl_i16 *pCB_Wght_Q9;
   SAVE_STACK;
   celt_assert( signalType >= 0 && signalType <= 2 );
   silk_assert( NLSF_mu_Q20 <= 32767 && NLSF_mu_Q20 >= 0 );
   silk_NLSF_stabilize( pNLSF_Q15, psNLSF_CB->deltaMin_Q15, psNLSF_CB->order );
   ALLOC( err_Q24, psNLSF_CB->nVectors, fl_i32 );
   silk_NLSF_VQ( err_Q24, pNLSF_Q15, psNLSF_CB->CB1_NLSF_Q8, psNLSF_CB->CB1_Wght_Q9, psNLSF_CB->nVectors, psNLSF_CB->order );
   ALLOC( tempIndices1, nSurvivors, int );
   silk_insertion_sort_increasing( err_Q24, tempIndices1, psNLSF_CB->nVectors, nSurvivors );
   ALLOC( RD_Q25, nSurvivors, fl_i32 );
   ALLOC( tempIndices2, nSurvivors * MAX_LPC_ORDER, fl_i8 );
   for( s = 0; s < nSurvivors; s++ ) {
      ind1 = tempIndices1[ s ];
      pCB_element = &psNLSF_CB->CB1_NLSF_Q8[ ind1 * psNLSF_CB->order ];
      pCB_Wght_Q9 = &psNLSF_CB->CB1_Wght_Q9[ ind1 * psNLSF_CB->order ];
      for( i = 0; i < psNLSF_CB->order; i++ ) {
         NLSF_tmp_Q15[ i ] = silk_LSHIFT16( (fl_i16)pCB_element[ i ], 7 );
         W_tmp_Q9 = pCB_Wght_Q9[ i ];
         res_Q10[ i ] = (fl_i16)silk_RSHIFT( silk_SMULBB( pNLSF_Q15[ i ] - NLSF_tmp_Q15[ i ], W_tmp_Q9 ), 14 );
         W_adj_Q5[ i ] = silk_DIV32_varQ( (fl_i32)pW_Q2[ i ], silk_SMULBB( W_tmp_Q9, W_tmp_Q9 ), 21 );
      }
      silk_NLSF_unpack( ec_ix, pred_Q8, psNLSF_CB, ind1 );
      RD_Q25[ s ] = silk_NLSF_del_dec_quant( &tempIndices2[ s * MAX_LPC_ORDER ], res_Q10, W_adj_Q5, pred_Q8, ec_ix,
         psNLSF_CB->ec_Rates_Q5, psNLSF_CB->quantStepSize_Q16, psNLSF_CB->invQuantStepSize_Q6, NLSF_mu_Q20, psNLSF_CB->order );
      iCDF_ptr = &psNLSF_CB->CB1_iCDF[ ( signalType >> 1 ) * psNLSF_CB->nVectors ];
      if( ind1 == 0 ) {
         prob_Q8 = 256 - iCDF_ptr[ ind1 ];
      } else {
         prob_Q8 = iCDF_ptr[ ind1 - 1 ] - iCDF_ptr[ ind1 ];
      }
      bits_q7 = ( 8 << 7 ) - silk_lin2log( prob_Q8 );
      RD_Q25[ s ] = silk_SMLABB( RD_Q25[ s ], bits_q7, silk_RSHIFT( NLSF_mu_Q20, 2 ) );
   }
   silk_insertion_sort_increasing( RD_Q25, &bestIndex, nSurvivors, 1 );
   NLSFIndices[ 0 ] = (fl_i8)tempIndices1[ bestIndex ];
   silk_memcpy( &NLSFIndices[ 1 ], &tempIndices2[ bestIndex * MAX_LPC_ORDER ], psNLSF_CB->order * sizeof( fl_i8 ) );
   silk_NLSF_decode( pNLSF_Q15, NLSFIndices, psNLSF_CB );
   ret = RD_Q25[ 0 ];
   RESTORE_STACK;
   return ret;
}

static void silk_interpolate(
   fl_i16 xi[ MAX_LPC_ORDER ],
   const fl_i16 x0[ MAX_LPC_ORDER ],
   const fl_i16 x1[ MAX_LPC_ORDER ],
   const int ifact_Q2,
   const int d
)
{
   int i;
   celt_assert( ifact_Q2 >= 0 );
   celt_assert( ifact_Q2 <= 4 );
   for( i = 0; i < d; i++ ) {
      xi[ i ] = (fl_i16)silk_ADD_RSHIFT( x0[ i ], silk_SMULBB( x1[ i ] - x0[ i ], ifact_Q2 ), 2 );
   }
}

static void silk_process_NLSFs(
   silk_encoder_state *psEncC,
   fl_i16 PredCoef_Q12[ 2 ][ MAX_LPC_ORDER ],
   fl_i16 pNLSF_Q15[ MAX_LPC_ORDER ],
   const fl_i16 prev_NLSFq_Q15[ MAX_LPC_ORDER ]
)
{
   int i, doInterpolate;
   int NLSF_mu_Q20;
   fl_i16 i_sqr_Q15;
   fl_i16 pNLSF0_temp_Q15[ MAX_LPC_ORDER ];
   fl_i16 pNLSFW_QW[ MAX_LPC_ORDER ];
   fl_i16 pNLSFW0_temp_QW[ MAX_LPC_ORDER ];
   silk_assert( psEncC->speech_activity_Q8 >= 0 );
   silk_assert( psEncC->speech_activity_Q8 <= SILK_FIX_CONST( 1.0, 8 ) );
   celt_assert( psEncC->useInterpolatedNLSFs == 1 || psEncC->indices.NLSFInterpCoef_Q2 == ( 1 << 2 ) );
   NLSF_mu_Q20 = silk_SMLAWB( SILK_FIX_CONST( 0.003, 20 ), SILK_FIX_CONST( -0.001, 28 ), psEncC->speech_activity_Q8 );
   if( psEncC->nb_subfr == 2 ) {
      NLSF_mu_Q20 = silk_ADD_RSHIFT( NLSF_mu_Q20, NLSF_mu_Q20, 1 );
   }
   celt_assert( NLSF_mu_Q20 > 0 );
   silk_assert( NLSF_mu_Q20 <= SILK_FIX_CONST( 0.005, 20 ) );
   silk_NLSF_VQ_weights_laroia( pNLSFW_QW, pNLSF_Q15, psEncC->predictLPCOrder );
   doInterpolate = ( psEncC->useInterpolatedNLSFs == 1 ) && ( psEncC->indices.NLSFInterpCoef_Q2 < 4 );
   if( doInterpolate ) {
      silk_interpolate( pNLSF0_temp_Q15, prev_NLSFq_Q15, pNLSF_Q15,
         psEncC->indices.NLSFInterpCoef_Q2, psEncC->predictLPCOrder );
      silk_NLSF_VQ_weights_laroia( pNLSFW0_temp_QW, pNLSF0_temp_Q15, psEncC->predictLPCOrder );
      i_sqr_Q15 = silk_LSHIFT( silk_SMULBB( psEncC->indices.NLSFInterpCoef_Q2, psEncC->indices.NLSFInterpCoef_Q2 ), 11 );
      for( i = 0; i < psEncC->predictLPCOrder; i++ ) {
         pNLSFW_QW[ i ] = silk_ADD16( silk_RSHIFT( pNLSFW_QW[ i ], 1 ), silk_RSHIFT(
             silk_SMULBB( pNLSFW0_temp_QW[ i ], i_sqr_Q15 ), 16) );
         silk_assert( pNLSFW_QW[ i ] >= 1 );
      }
   }
   silk_NLSF_encode( psEncC->indices.NLSFIndices, pNLSF_Q15, psEncC->psNLSF_CB, pNLSFW_QW,
      NLSF_mu_Q20, psEncC->NLSF_MSVQ_Survivors, psEncC->indices.signalType );
   silk_NLSF2A( PredCoef_Q12[ 1 ], pNLSF_Q15, psEncC->predictLPCOrder, psEncC->arch );
   if( doInterpolate ) {
      silk_interpolate( pNLSF0_temp_Q15, prev_NLSFq_Q15, pNLSF_Q15,
         psEncC->indices.NLSFInterpCoef_Q2, psEncC->predictLPCOrder );
      silk_NLSF2A( PredCoef_Q12[ 0 ], pNLSF0_temp_Q15, psEncC->predictLPCOrder, psEncC->arch );
   } else {
      celt_assert( psEncC->predictLPCOrder <= MAX_LPC_ORDER );
      silk_memcpy( PredCoef_Q12[ 0 ], PredCoef_Q12[ 1 ], psEncC->predictLPCOrder * sizeof( fl_i16 ) );
   }
}

static void silk_VQ_WMat_EC_c(
   fl_i8 *ind,
   fl_i32 *res_nrg_Q15,
   fl_i32 *rate_dist_Q8,
   int *gain_Q7,
   const fl_i32 *XX_Q17,
   const fl_i32 *xX_Q17,
   const fl_i8 *cb_Q7,
   const fl_u8 *cb_gain_Q7,
   const fl_u8 *cl_Q5,
   const int subfr_len,
   const fl_i32 max_gain_Q7,
   const int L
)
{
   int k, gain_tmp_Q7;
   const fl_i8 *cb_row_Q7;
   fl_i32 neg_xX_Q24[ 5 ];
   fl_i32 sum1_Q15, sum2_Q24;
   fl_i32 bits_res_Q8, bits_tot_Q8;
   neg_xX_Q24[ 0 ] = -silk_LSHIFT32( xX_Q17[ 0 ], 7 );
   neg_xX_Q24[ 1 ] = -silk_LSHIFT32( xX_Q17[ 1 ], 7 );
   neg_xX_Q24[ 2 ] = -silk_LSHIFT32( xX_Q17[ 2 ], 7 );
   neg_xX_Q24[ 3 ] = -silk_LSHIFT32( xX_Q17[ 3 ], 7 );
   neg_xX_Q24[ 4 ] = -silk_LSHIFT32( xX_Q17[ 4 ], 7 );
   *rate_dist_Q8 = silk_int32_MAX;
   *res_nrg_Q15 = silk_int32_MAX;
   cb_row_Q7 = cb_Q7;
   *ind = 0;
   for( k = 0; k < L; k++ ) {
      fl_i32 penalty;
      gain_tmp_Q7 = cb_gain_Q7[k];
      sum1_Q15 = SILK_FIX_CONST( 1.001, 15 );
      penalty = silk_LSHIFT32( silk_max( silk_SUB32( gain_tmp_Q7, max_gain_Q7 ), 0 ), 11 );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 0 ], XX_Q17[ 1 ], cb_row_Q7[ 1 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 2 ], cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 3 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 4 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 0 ], cb_row_Q7[ 0 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 0 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 1 ], XX_Q17[ 7 ], cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 8 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 9 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 6 ], cb_row_Q7[ 1 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 1 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 2 ], XX_Q17[ 13 ], cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 14 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 12 ], cb_row_Q7[ 2 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 2 ] );
      sum2_Q24 = silk_MLA( neg_xX_Q24[ 3 ], XX_Q17[ 19 ], cb_row_Q7[ 4 ] );
      sum2_Q24 = silk_LSHIFT32( sum2_Q24, 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 18 ], cb_row_Q7[ 3 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 3 ] );
      sum2_Q24 = silk_LSHIFT32( neg_xX_Q24[ 4 ], 1 );
      sum2_Q24 = silk_MLA( sum2_Q24, XX_Q17[ 24 ], cb_row_Q7[ 4 ] );
      sum1_Q15 = silk_SMLAWB( sum1_Q15, sum2_Q24, cb_row_Q7[ 4 ] );
      if( sum1_Q15 >= 0 ) {
         bits_res_Q8 = silk_SMULBB( subfr_len, silk_lin2log( sum1_Q15 + penalty) - (15 << 7) );
         bits_tot_Q8 = silk_ADD_LSHIFT32( bits_res_Q8, cl_Q5[ k ], 3-1 );
         if( bits_tot_Q8 <= *rate_dist_Q8 ) {
            *rate_dist_Q8 = bits_tot_Q8;
            *res_nrg_Q15 = sum1_Q15 + penalty;
            *ind = (fl_i8)k;
            *gain_Q7 = gain_tmp_Q7;
         }
      }
      cb_row_Q7 += LTP_ORDER;
   }
}

static void silk_quant_LTP_gains(
   fl_i16 B_Q14[ MAX_NB_SUBFR * LTP_ORDER ],
   fl_i8 cbk_index[ MAX_NB_SUBFR ],
   fl_i8 *periodicity_index,
   fl_i32 *sum_log_gain_Q7,
   int *pred_gain_dB_Q7,
   const fl_i32 XX_Q17[ MAX_NB_SUBFR*LTP_ORDER*LTP_ORDER ],
   const fl_i32 xX_Q17[ MAX_NB_SUBFR*LTP_ORDER ],
   const int subfr_len,
   const int nb_subfr,
   int arch
)
{
   int j, k, cbk_size;
   fl_i8 temp_idx[ MAX_NB_SUBFR ];
   const fl_u8 *cl_ptr_Q5;
   const fl_i8 *cbk_ptr_Q7;
   const fl_u8 *cbk_gain_ptr_Q7;
   const fl_i32 *XX_Q17_ptr, *xX_Q17_ptr;
   fl_i32 res_nrg_Q15_subfr, res_nrg_Q15, rate_dist_Q7_subfr, rate_dist_Q7, min_rate_dist_Q7;
   fl_i32 sum_log_gain_tmp_Q7, best_sum_log_gain_Q7, max_gain_Q7;
   int gain_Q7;
   min_rate_dist_Q7 = silk_int32_MAX;
   best_sum_log_gain_Q7 = 0;
   for( k = 0; k < 3; k++ ) {
      fl_i32 gain_safety = SILK_FIX_CONST( 0.4, 7 );
      cl_ptr_Q5 = fl_LTP_gain_BITS_Q5_ptrs[ k ];
      cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[ k ];
      cbk_gain_ptr_Q7 = fl_LTP_vq_gain_ptrs_Q7[ k ];
      cbk_size = fl_LTP_vq_sizes[ k ];
      XX_Q17_ptr = XX_Q17;
      xX_Q17_ptr = xX_Q17;
      res_nrg_Q15 = 0;
      rate_dist_Q7 = 0;
      sum_log_gain_tmp_Q7 = *sum_log_gain_Q7;
      for( j = 0; j < nb_subfr; j++ ) {
         max_gain_Q7 = silk_log2lin( ( SILK_FIX_CONST( MAX_SUM_LOG_GAIN_DB / 6.0, 7 ) - sum_log_gain_tmp_Q7 )
                              + SILK_FIX_CONST( 7, 7 ) ) - gain_safety;
         silk_VQ_WMat_EC(
            &temp_idx[ j ],
            &res_nrg_Q15_subfr,
            &rate_dist_Q7_subfr,
            &gain_Q7,
            XX_Q17_ptr,
            xX_Q17_ptr,
            cbk_ptr_Q7,
            cbk_gain_ptr_Q7,
            cl_ptr_Q5,
            subfr_len,
            max_gain_Q7,
            cbk_size,
            arch
         );
         res_nrg_Q15 = silk_ADD_POS_SAT32( res_nrg_Q15, res_nrg_Q15_subfr );
         rate_dist_Q7 = silk_ADD_POS_SAT32( rate_dist_Q7, rate_dist_Q7_subfr );
         sum_log_gain_tmp_Q7 = silk_max(0, sum_log_gain_tmp_Q7
                        + silk_lin2log( gain_safety + gain_Q7 ) - SILK_FIX_CONST( 7, 7 ));
         XX_Q17_ptr += LTP_ORDER * LTP_ORDER;
         xX_Q17_ptr += LTP_ORDER;
      }
      if( rate_dist_Q7 <= min_rate_dist_Q7 ) {
         min_rate_dist_Q7 = rate_dist_Q7;
         *periodicity_index = (fl_i8)k;
         silk_memcpy( cbk_index, temp_idx, nb_subfr * sizeof( fl_i8 ) );
         best_sum_log_gain_Q7 = sum_log_gain_tmp_Q7;
      }
   }
   cbk_ptr_Q7 = fl_LTP_vq_ptrs_Q7[ *periodicity_index ];
   for( j = 0; j < nb_subfr; j++ ) {
      for( k = 0; k < LTP_ORDER; k++ ) {
         B_Q14[ j * LTP_ORDER + k ] = silk_LSHIFT( cbk_ptr_Q7[ cbk_index[ j ] * LTP_ORDER + k ], 7 );
      }
   }
   if( nb_subfr == 2 ) {
      res_nrg_Q15 = silk_RSHIFT32( res_nrg_Q15, 1 );
   } else {
      res_nrg_Q15 = silk_RSHIFT32( res_nrg_Q15, 2 );
   }
   *sum_log_gain_Q7 = best_sum_log_gain_Q7;
   *pred_gain_dB_Q7 = (int)silk_SMULBB( -3, silk_lin2log( res_nrg_Q15 ) - ( 15 << 7 ) );
}

static void silk_encode_indices(
   silk_encoder_state *psEncC,
   ec_ctx *psRangeEnc,
   int FrameIndex,
   int encode_LBRR,
   int condCoding
)
{
   int i, k, typeOffset;
   int encode_absolute_lagIndex, delta_lagIndex;
   fl_i16 ec_ix[ MAX_LPC_ORDER ];
   fl_u8 pred_Q8[ MAX_LPC_ORDER ];
   const SideInfoIndices *psIndices;
   if( encode_LBRR ) {
      psIndices = &psEncC->indices_LBRR[ FrameIndex ];
   } else {
      psIndices = &psEncC->indices;
   }
   typeOffset = 2 * psIndices->signalType + psIndices->quantOffsetType;
   celt_assert( typeOffset >= 0 && typeOffset < 6 );
   celt_assert( encode_LBRR == 0 || typeOffset >= 2 );
   if( encode_LBRR || typeOffset >= 2 ) {
      ec_enc_icdf( psRangeEnc, typeOffset - 2, fl_type_offset_VAD_iCDF, 8 );
   } else {
      ec_enc_icdf( psRangeEnc, typeOffset, fl_type_offset_no_VAD_iCDF, 8 );
   }
   if( condCoding == CODE_CONDITIONALLY ) {
      silk_assert( psIndices->GainsIndices[ 0 ] >= 0 && psIndices->GainsIndices[ 0 ] < MAX_DELTA_GAIN_QUANT - MIN_DELTA_GAIN_QUANT + 1 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ 0 ], fl_delta_gain_iCDF, 8 );
   } else {
      silk_assert( psIndices->GainsIndices[ 0 ] >= 0 && psIndices->GainsIndices[ 0 ] < N_LEVELS_QGAIN );
      ec_enc_icdf( psRangeEnc, silk_RSHIFT( psIndices->GainsIndices[ 0 ], 3 ), fl_gain_iCDF[ psIndices->signalType ], 8 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ 0 ] & 7, fl_uniform8_iCDF, 8 );
   }
   for( i = 1; i < psEncC->nb_subfr; i++ ) {
      silk_assert( psIndices->GainsIndices[ i ] >= 0 && psIndices->GainsIndices[ i ] < MAX_DELTA_GAIN_QUANT - MIN_DELTA_GAIN_QUANT + 1 );
      ec_enc_icdf( psRangeEnc, psIndices->GainsIndices[ i ], fl_delta_gain_iCDF, 8 );
   }
   ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ 0 ], &psEncC->psNLSF_CB->CB1_iCDF[ ( psIndices->signalType >> 1 ) * psEncC->psNLSF_CB->nVectors ], 8 );
   silk_NLSF_unpack( ec_ix, pred_Q8, psEncC->psNLSF_CB, psIndices->NLSFIndices[ 0 ] );
   celt_assert( psEncC->psNLSF_CB->order == psEncC->predictLPCOrder );
   for( i = 0; i < psEncC->psNLSF_CB->order; i++ ) {
      if( psIndices->NLSFIndices[ i+1 ] >= NLSF_QUANT_MAX_AMPLITUDE ) {
         ec_enc_icdf( psRangeEnc, 2 * NLSF_QUANT_MAX_AMPLITUDE, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
         ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ i+1 ] - NLSF_QUANT_MAX_AMPLITUDE, fl_NLSF_EXT_iCDF, 8 );
      } else if( psIndices->NLSFIndices[ i+1 ] <= -NLSF_QUANT_MAX_AMPLITUDE ) {
         ec_enc_icdf( psRangeEnc, 0, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
         ec_enc_icdf( psRangeEnc, -psIndices->NLSFIndices[ i+1 ] - NLSF_QUANT_MAX_AMPLITUDE, fl_NLSF_EXT_iCDF, 8 );
      } else {
         ec_enc_icdf( psRangeEnc, psIndices->NLSFIndices[ i+1 ] + NLSF_QUANT_MAX_AMPLITUDE, &psEncC->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 );
      }
   }
   if( psEncC->nb_subfr == MAX_NB_SUBFR ) {
      silk_assert( psIndices->NLSFInterpCoef_Q2 >= 0 && psIndices->NLSFInterpCoef_Q2 < 5 );
      ec_enc_icdf( psRangeEnc, psIndices->NLSFInterpCoef_Q2, fl_NLSF_interpolation_factor_iCDF, 8 );
   }
   if( psIndices->signalType == TYPE_VOICED )
   {
      encode_absolute_lagIndex = 1;
      if( condCoding == CODE_CONDITIONALLY && psEncC->ec_prevSignalType == TYPE_VOICED ) {
         delta_lagIndex = psIndices->lagIndex - psEncC->ec_prevLagIndex;
         if( delta_lagIndex < -8 || delta_lagIndex > 11 ) {
            delta_lagIndex = 0;
         } else {
            delta_lagIndex = delta_lagIndex + 9;
            encode_absolute_lagIndex = 0;
         }
         silk_assert( delta_lagIndex >= 0 && delta_lagIndex < 21 );
         ec_enc_icdf( psRangeEnc, delta_lagIndex, fl_pitch_delta_iCDF, 8 );
      }
      if( encode_absolute_lagIndex ) {
         fl_i32 pitch_high_bits, pitch_low_bits;
         pitch_high_bits = silk_DIV32_16( psIndices->lagIndex, silk_RSHIFT( psEncC->fs_kHz, 1 ) );
         pitch_low_bits = psIndices->lagIndex - silk_SMULBB( pitch_high_bits, silk_RSHIFT( psEncC->fs_kHz, 1 ) );
         silk_assert( pitch_low_bits < psEncC->fs_kHz / 2 );
         silk_assert( pitch_high_bits < 32 );
         ec_enc_icdf( psRangeEnc, pitch_high_bits, fl_pitch_lag_iCDF, 8 );
         ec_enc_icdf( psRangeEnc, pitch_low_bits, psEncC->pitch_lag_low_bits_iCDF, 8 );
      }
      psEncC->ec_prevLagIndex = psIndices->lagIndex;
      silk_assert( psIndices->contourIndex >= 0 );
      silk_assert( ( psIndices->contourIndex < 34 && psEncC->fs_kHz > 8 && psEncC->nb_subfr == 4 ) ||
               ( psIndices->contourIndex < 11 && psEncC->fs_kHz == 8 && psEncC->nb_subfr == 4 ) ||
               ( psIndices->contourIndex < 12 && psEncC->fs_kHz > 8 && psEncC->nb_subfr == 2 ) ||
               ( psIndices->contourIndex < 3 && psEncC->fs_kHz == 8 && psEncC->nb_subfr == 2 ) );
      ec_enc_icdf( psRangeEnc, psIndices->contourIndex, psEncC->pitch_contour_iCDF, 8 );
      silk_assert( psIndices->PERIndex >= 0 && psIndices->PERIndex < 3 );
      ec_enc_icdf( psRangeEnc, psIndices->PERIndex, fl_LTP_per_index_iCDF, 8 );
      for( k = 0; k < psEncC->nb_subfr; k++ ) {
         silk_assert( psIndices->LTPIndex[ k ] >= 0 && psIndices->LTPIndex[ k ] < ( 8 << psIndices->PERIndex ) );
         ec_enc_icdf( psRangeEnc, psIndices->LTPIndex[ k ], fl_LTP_gain_iCDF_ptrs[ psIndices->PERIndex ], 8 );
      }
      if( condCoding == CODE_INDEPENDENTLY ) {
         silk_assert( psIndices->LTP_scaleIndex >= 0 && psIndices->LTP_scaleIndex < 3 );
         ec_enc_icdf( psRangeEnc, psIndices->LTP_scaleIndex, fl_LTPscale_iCDF, 8 );
      }
      silk_assert( !condCoding || psIndices->LTP_scaleIndex == 0 );
   }
   psEncC->ec_prevSignalType = psIndices->signalType;
   silk_assert( psIndices->Seed >= 0 && psIndices->Seed < 4 );
   ec_enc_icdf( psRangeEnc, psIndices->Seed, fl_uniform4_iCDF, 8 );
}

static inline int combine_and_check(
   int *pulses_comb,
   const int *pulses_in,
   int max_pulses,
   int len
)
{
   int k, sum;
   for( k = 0; k < len; k++ ) {
      sum = pulses_in[ 2 * k ] + pulses_in[ 2 * k + 1 ];
      if( sum > max_pulses ) {
         return 1;
      }
      pulses_comb[ k ] = sum;
   }
   return 0;
}

static void silk_encode_pulses(
   ec_ctx *psRangeEnc,
   const int signalType,
   const int quantOffsetType,
   fl_i8 pulses[],
   const int frame_length
)
{
   int i, k, j, iter, bit, nLS, scale_down, RateLevelIndex = 0;
   fl_i32 abs_q, minSumBits_Q5, sumBits_Q5;
   VARDECL( int, abs_pulses );
   VARDECL( int, sum_pulses );
   VARDECL( int, nRshifts );
   int pulses_comb[ 8 ];
   int *abs_pulses_ptr;
   const fl_i8 *pulses_ptr;
   const fl_u8 *cdf_ptr;
   const fl_u8 *nBits_ptr;
   SAVE_STACK;
   silk_memset( pulses_comb, 0, 8 * sizeof( int ) );
   silk_assert( 1 << LOG2_SHELL_CODEC_FRAME_LENGTH == SHELL_CODEC_FRAME_LENGTH );
   iter = silk_RSHIFT( frame_length, LOG2_SHELL_CODEC_FRAME_LENGTH );
   if( iter * SHELL_CODEC_FRAME_LENGTH < frame_length ) {
      celt_assert( frame_length == 12 * 10 );
      iter++;
      silk_memset( &pulses[ frame_length ], 0, SHELL_CODEC_FRAME_LENGTH * sizeof(fl_i8));
   }
   ALLOC( abs_pulses, iter * SHELL_CODEC_FRAME_LENGTH, int );
   silk_assert( !( SHELL_CODEC_FRAME_LENGTH & 3 ) );
   for( i = 0; i < iter * SHELL_CODEC_FRAME_LENGTH; i+=4 ) {
      abs_pulses[i+0] = ( int )silk_abs( pulses[ i + 0 ] );
      abs_pulses[i+1] = ( int )silk_abs( pulses[ i + 1 ] );
      abs_pulses[i+2] = ( int )silk_abs( pulses[ i + 2 ] );
      abs_pulses[i+3] = ( int )silk_abs( pulses[ i + 3 ] );
   }
   ALLOC( sum_pulses, iter, int );
   ALLOC( nRshifts, iter, int );
   abs_pulses_ptr = abs_pulses;
   for( i = 0; i < iter; i++ ) {
      nRshifts[ i ] = 0;
      while( 1 ) {
         scale_down = combine_and_check( pulses_comb, abs_pulses_ptr, fl_max_pulses_table[ 0 ], 8 );
         scale_down += combine_and_check( pulses_comb, pulses_comb, fl_max_pulses_table[ 1 ], 4 );
         scale_down += combine_and_check( pulses_comb, pulses_comb, fl_max_pulses_table[ 2 ], 2 );
         scale_down += combine_and_check( &sum_pulses[ i ], pulses_comb, fl_max_pulses_table[ 3 ], 1 );
         if( scale_down ) {
            nRshifts[ i ]++;
            for( k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++ ) {
               abs_pulses_ptr[ k ] = silk_RSHIFT( abs_pulses_ptr[ k ], 1 );
            }
         } else {
            break;
         }
      }
      abs_pulses_ptr += SHELL_CODEC_FRAME_LENGTH;
   }
   minSumBits_Q5 = silk_int32_MAX;
   for( k = 0; k < N_RATE_LEVELS - 1; k++ ) {
      nBits_ptr = fl_pulses_per_block_BITS_Q5[ k ];
      sumBits_Q5 = fl_rate_levels_BITS_Q5[ signalType >> 1 ][ k ];
      for( i = 0; i < iter; i++ ) {
         if( nRshifts[ i ] > 0 ) {
            sumBits_Q5 += nBits_ptr[ SILK_MAX_PULSES + 1 ];
         } else {
            sumBits_Q5 += nBits_ptr[ sum_pulses[ i ] ];
         }
      }
      if( sumBits_Q5 < minSumBits_Q5 ) {
         minSumBits_Q5 = sumBits_Q5;
         RateLevelIndex = k;
      }
   }
   ec_enc_icdf( psRangeEnc, RateLevelIndex, fl_rate_levels_iCDF[ signalType >> 1 ], 8 );
   cdf_ptr = fl_pulses_per_block_iCDF[ RateLevelIndex ];
   for( i = 0; i < iter; i++ ) {
      if( nRshifts[ i ] == 0 ) {
         ec_enc_icdf( psRangeEnc, sum_pulses[ i ], cdf_ptr, 8 );
      } else {
         ec_enc_icdf( psRangeEnc, SILK_MAX_PULSES + 1, cdf_ptr, 8 );
         for( k = 0; k < nRshifts[ i ] - 1; k++ ) {
            ec_enc_icdf( psRangeEnc, SILK_MAX_PULSES + 1, fl_pulses_per_block_iCDF[ N_RATE_LEVELS - 1 ], 8 );
         }
         ec_enc_icdf( psRangeEnc, sum_pulses[ i ], fl_pulses_per_block_iCDF[ N_RATE_LEVELS - 1 ], 8 );
      }
   }
   for( i = 0; i < iter; i++ ) {
      if( sum_pulses[ i ] > 0 ) {
         silk_shell_encoder( psRangeEnc, &abs_pulses[ i * SHELL_CODEC_FRAME_LENGTH ] );
      }
   }
   for( i = 0; i < iter; i++ ) {
      if( nRshifts[ i ] > 0 ) {
         pulses_ptr = &pulses[ i * SHELL_CODEC_FRAME_LENGTH ];
         nLS = nRshifts[ i ] - 1;
         for( k = 0; k < SHELL_CODEC_FRAME_LENGTH; k++ ) {
            abs_q = (fl_i8)silk_abs( pulses_ptr[ k ] );
            for( j = nLS; j > 0; j-- ) {
               bit = silk_RSHIFT( abs_q, j ) & 1;
               ec_enc_icdf( psRangeEnc, bit, fl_lsb_iCDF, 8 );
            }
            bit = abs_q & 1;
            ec_enc_icdf( psRangeEnc, bit, fl_lsb_iCDF, 8 );
         }
      }
   }
   silk_encode_signs( psRangeEnc, pulses, frame_length, signalType, quantOffsetType, sum_pulses );
   RESTORE_STACK;
}

static inline fl_i32 silk_noise_shape_quantizer_short_prediction_c(const fl_i32 *buf32, const fl_i16 *coef16, int order)
{
   fl_i32 out;
   silk_assert( order == 10 || order == 16 );
   out = silk_RSHIFT( order, 1 );
   out = silk_SMLAWB( out, buf32[ 0 ], coef16[ 0 ] );
   out = silk_SMLAWB( out, buf32[ -1 ], coef16[ 1 ] );
   out = silk_SMLAWB( out, buf32[ -2 ], coef16[ 2 ] );
   out = silk_SMLAWB( out, buf32[ -3 ], coef16[ 3 ] );
   out = silk_SMLAWB( out, buf32[ -4 ], coef16[ 4 ] );
   out = silk_SMLAWB( out, buf32[ -5 ], coef16[ 5 ] );
   out = silk_SMLAWB( out, buf32[ -6 ], coef16[ 6 ] );
   out = silk_SMLAWB( out, buf32[ -7 ], coef16[ 7 ] );
   out = silk_SMLAWB( out, buf32[ -8 ], coef16[ 8 ] );
   out = silk_SMLAWB( out, buf32[ -9 ], coef16[ 9 ] );
   if( order == 16 )
   {
      out = silk_SMLAWB( out, buf32[ -10 ], coef16[ 10 ] );
      out = silk_SMLAWB( out, buf32[ -11 ], coef16[ 11 ] );
      out = silk_SMLAWB( out, buf32[ -12 ], coef16[ 12 ] );
      out = silk_SMLAWB( out, buf32[ -13 ], coef16[ 13 ] );
      out = silk_SMLAWB( out, buf32[ -14 ], coef16[ 14 ] );
      out = silk_SMLAWB( out, buf32[ -15 ], coef16[ 15 ] );
   }
   return out;
}

static inline fl_i32 silk_NSQ_noise_shape_feedback_loop_c(const fl_i32 *data0, fl_i32 *data1, const fl_i16 *coef, int order)
{
   fl_i32 out;
   fl_i32 tmp1, tmp2;
   int j;
   tmp2 = data0[0];
   tmp1 = data1[0];
   data1[0] = tmp2;
   out = silk_RSHIFT(order, 1);
   out = silk_SMLAWB(out, tmp2, coef[0]);
   for (j = 2; j < order; j += 2) {
      tmp2 = data1[j - 1];
      data1[j - 1] = tmp1;
      out = silk_SMLAWB(out, tmp1, coef[j - 1]);
      tmp1 = data1[j + 0];
      data1[j + 0] = tmp2;
      out = silk_SMLAWB(out, tmp2, coef[j]);
   }
   data1[order - 1] = tmp1;
   out = silk_SMLAWB(out, tmp1, coef[order - 1]);
   out = silk_LSHIFT32( out, 1 );
   return out;
}

static void silk_NSQ_c
(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
)
{
   int k, lag, start_idx, LSF_interpolation_flag;
   const fl_i16 *A_Q12, *B_Q14, *AR_shp_Q13;
   fl_i16 *pxq;
   VARDECL( fl_i32, sLTP_Q15 );
   VARDECL( fl_i16, sLTP );
   fl_i32 HarmShapeFIRPacked_Q14;
   int offset_Q10;
   VARDECL( fl_i32, x_sc_Q10 );
   SAVE_STACK;
   NSQ->rand_seed = psIndices->Seed;
   lag = NSQ->lagPrev;
   silk_assert( NSQ->prev_gain_Q16 != 0 );
   offset_Q10 = fl_Quantization_Offsets_Q10[ psIndices->signalType >> 1 ][ psIndices->quantOffsetType ];
   if( psIndices->NLSFInterpCoef_Q2 == 4 ) {
      LSF_interpolation_flag = 0;
   } else {
      LSF_interpolation_flag = 1;
   }
   ALLOC( sLTP_Q15, psEncC->ltp_mem_length + psEncC->frame_length, fl_i32 );
   ALLOC( sLTP, psEncC->ltp_mem_length + psEncC->frame_length, fl_i16 );
   ALLOC( x_sc_Q10, psEncC->subfr_length, fl_i32 );
   NSQ->sLTP_shp_buf_idx = psEncC->ltp_mem_length;
   NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
   pxq = &NSQ->xq[ psEncC->ltp_mem_length ];
   for( k = 0; k < psEncC->nb_subfr; k++ ) {
      A_Q12 = &PredCoef_Q12[ (( k >> 1 ) | ( 1 - LSF_interpolation_flag )) * MAX_LPC_ORDER ];
      B_Q14 = &LTPCoef_Q14[ k * LTP_ORDER ];
      AR_shp_Q13 = &AR_Q13[ k * MAX_SHAPE_LPC_ORDER ];
      silk_assert( HarmShapeGain_Q14[ k ] >= 0 );
      HarmShapeFIRPacked_Q14 = silk_RSHIFT( HarmShapeGain_Q14[ k ], 2 );
      HarmShapeFIRPacked_Q14 |= silk_LSHIFT( (fl_i32)silk_RSHIFT( HarmShapeGain_Q14[ k ], 1 ), 16 );
      NSQ->rewhite_flag = 0;
      if( psIndices->signalType == TYPE_VOICED ) {
         lag = pitchL[ k ];
         if( ( k & ( 3 - silk_LSHIFT( LSF_interpolation_flag, 1 ) ) ) == 0 ) {
            start_idx = psEncC->ltp_mem_length - lag - psEncC->predictLPCOrder - LTP_ORDER / 2;
            celt_assert( start_idx > 0 );
            silk_LPC_analysis_filter( &sLTP[ start_idx ], &NSQ->xq[ start_idx + k * psEncC->subfr_length ],
               A_Q12, psEncC->ltp_mem_length - start_idx, psEncC->predictLPCOrder, psEncC->arch );
            NSQ->rewhite_flag = 1;
            NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
         }
      }
      silk_nsq_scale_states( psEncC, NSQ, x16, x_sc_Q10, sLTP, sLTP_Q15, k, LTP_scale_Q14, Gains_Q16, pitchL, psIndices->signalType );
      silk_noise_shape_quantizer( NSQ, psIndices->signalType, x_sc_Q10, pulses, pxq, sLTP_Q15, A_Q12, B_Q14,
         AR_shp_Q13, lag, HarmShapeFIRPacked_Q14, Tilt_Q14[ k ], LF_shp_Q14[ k ], Gains_Q16[ k ], Lambda_Q10,
         offset_Q10, psEncC->subfr_length, psEncC->shapingLPCOrder, psEncC->predictLPCOrder, psEncC->arch );
      x16 += psEncC->subfr_length;
      pulses += psEncC->subfr_length;
      pxq += psEncC->subfr_length;
   }
   NSQ->lagPrev = pitchL[ psEncC->nb_subfr - 1 ];
   silk_memmove( NSQ->xq, &NSQ->xq[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i16 ) );
   silk_memmove( NSQ->sLTP_shp_Q14, &NSQ->sLTP_shp_Q14[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static void silk_noise_shape_quantizer(
   silk_nsq_state *NSQ,
   int signalType,
   const fl_i32 x_sc_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int shapingLPCOrder,
   int predictLPCOrder,
   int arch
)
{
   int i;
   fl_i32 LTP_pred_Q13, LPC_pred_Q10, n_AR_Q12, n_LTP_Q13;
   fl_i32 n_LF_Q12, r_Q10, rr_Q10, q1_Q0, q1_Q10, q2_Q10, rd1_Q20, rd2_Q20;
   fl_i32 exc_Q14, LPC_exc_Q14, xq_Q14, Gain_Q10;
   fl_i32 tmp1, tmp2, sLF_AR_shp_Q14;
   fl_i32 *psLPC_Q14, *shp_lag_ptr, *pred_lag_ptr;
   fl_i32 a_Q12_arch[MAX_LPC_ORDER];
   shp_lag_ptr = &NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - lag + HARM_SHAPE_FIR_TAPS / 2 ];
   pred_lag_ptr = &sLTP_Q15[ NSQ->sLTP_buf_idx - lag + LTP_ORDER / 2 ];
   Gain_Q10 = silk_RSHIFT( Gain_Q16, 6 );
   psLPC_Q14 = &NSQ->sLPC_Q14[ NSQ_LPC_BUF_LENGTH - 1 ];
   silk_short_prediction_create_arch_coef(a_Q12_arch, a_Q12, predictLPCOrder);
   for( i = 0; i < length; i++ ) {
      NSQ->rand_seed = silk_RAND( NSQ->rand_seed );
      LPC_pred_Q10 = silk_noise_shape_quantizer_short_prediction(psLPC_Q14, a_Q12, a_Q12_arch, predictLPCOrder, arch);
      if( signalType == TYPE_VOICED ) {
         LTP_pred_Q13 = 2;
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ 0 ], b_Q14[ 0 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -1 ], b_Q14[ 1 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -2 ], b_Q14[ 2 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -3 ], b_Q14[ 3 ] );
         LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -4 ], b_Q14[ 4 ] );
         pred_lag_ptr++;
      } else {
         LTP_pred_Q13 = 0;
      }
      celt_assert( ( shapingLPCOrder & 1 ) == 0 );
      n_AR_Q12 = silk_NSQ_noise_shape_feedback_loop(&NSQ->sDiff_shp_Q14, NSQ->sAR2_Q14, AR_shp_Q13, shapingLPCOrder, arch);
      n_AR_Q12 = silk_SMLAWB( n_AR_Q12, NSQ->sLF_AR_shp_Q14, Tilt_Q14 );
      n_LF_Q12 = silk_SMULWB( NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - 1 ], LF_shp_Q14 );
      n_LF_Q12 = silk_SMLAWT( n_LF_Q12, NSQ->sLF_AR_shp_Q14, LF_shp_Q14 );
      celt_assert( lag > 0 || signalType != TYPE_VOICED );
      tmp1 = silk_SUB32_ovflw( silk_LSHIFT32( LPC_pred_Q10, 2 ), n_AR_Q12 );
      tmp1 = silk_SUB32_ovflw( tmp1, n_LF_Q12 );
      if( lag > 0 ) {
         n_LTP_Q13 = silk_SMULWB( silk_ADD_SAT32( shp_lag_ptr[ 0 ], shp_lag_ptr[ -2 ] ), HarmShapeFIRPacked_Q14 );
         n_LTP_Q13 = silk_SMLAWT( n_LTP_Q13, shp_lag_ptr[ -1 ], HarmShapeFIRPacked_Q14 );
         n_LTP_Q13 = silk_LSHIFT( n_LTP_Q13, 1 );
         shp_lag_ptr++;
         tmp2 = silk_SUB32( LTP_pred_Q13, n_LTP_Q13 );
         tmp1 = silk_ADD32_ovflw( tmp2, silk_LSHIFT32( tmp1, 1 ) );
         tmp1 = silk_RSHIFT_ROUND( tmp1, 3 );
      } else {
         tmp1 = silk_RSHIFT_ROUND( tmp1, 2 );
      }
      r_Q10 = silk_SUB32( x_sc_Q10[ i ], tmp1 );
      if( NSQ->rand_seed < 0 ) {
         r_Q10 = -r_Q10;
      }
      r_Q10 = silk_LIMIT_32( r_Q10, -(31 << 10), 30 << 10 );
      q1_Q10 = silk_SUB32( r_Q10, offset_Q10 );
      q1_Q0 = silk_RSHIFT( q1_Q10, 10 );
      if (Lambda_Q10 > 2048) {
         int rdo_offset = Lambda_Q10/2 - 512;
         if (q1_Q10 > rdo_offset) {
            q1_Q0 = silk_RSHIFT( q1_Q10 - rdo_offset, 10 );
         } else if (q1_Q10 < -rdo_offset) {
            q1_Q0 = silk_RSHIFT( q1_Q10 + rdo_offset, 10 );
         } else if (q1_Q10 < 0) {
            q1_Q0 = -1;
         } else {
            q1_Q0 = 0;
         }
      }
      if( q1_Q0 > 0 ) {
         q1_Q10 = silk_SUB32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
         q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
         q2_Q10 = silk_ADD32( q1_Q10, 1024 );
         rd1_Q20 = silk_SMULBB( q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else if( q1_Q0 == 0 ) {
         q1_Q10 = offset_Q10;
         q2_Q10 = silk_ADD32( q1_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
         rd1_Q20 = silk_SMULBB( q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else if( q1_Q0 == -1 ) {
         q2_Q10 = offset_Q10;
         q1_Q10 = silk_SUB32( q2_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
         rd1_Q20 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( q2_Q10, Lambda_Q10 );
      } else {
         q1_Q10 = silk_ADD32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
         q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
         q2_Q10 = silk_ADD32( q1_Q10, 1024 );
         rd1_Q20 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
         rd2_Q20 = silk_SMULBB( -q2_Q10, Lambda_Q10 );
      }
      rr_Q10 = silk_SUB32( r_Q10, q1_Q10 );
      rd1_Q20 = silk_SMLABB( rd1_Q20, rr_Q10, rr_Q10 );
      rr_Q10 = silk_SUB32( r_Q10, q2_Q10 );
      rd2_Q20 = silk_SMLABB( rd2_Q20, rr_Q10, rr_Q10 );
      if( rd2_Q20 < rd1_Q20 ) {
         q1_Q10 = q2_Q10;
      }
      pulses[ i ] = (fl_i8)silk_RSHIFT_ROUND( q1_Q10, 10 );
      exc_Q14 = silk_LSHIFT( q1_Q10, 4 );
      if ( NSQ->rand_seed < 0 ) {
        exc_Q14 = -exc_Q14;
      }
      LPC_exc_Q14 = silk_ADD_LSHIFT32( exc_Q14, LTP_pred_Q13, 1 );
      xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, silk_LSHIFT32( LPC_pred_Q10, 4 ) );
      xq[ i ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( silk_SMULWW( xq_Q14, Gain_Q10 ), 8 ) );
      psLPC_Q14++;
      *psLPC_Q14 = xq_Q14;
      NSQ->sDiff_shp_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_sc_Q10[ i ], 4 ) );
      sLF_AR_shp_Q14 = silk_SUB32_ovflw( NSQ->sDiff_shp_Q14, silk_LSHIFT32( n_AR_Q12, 2 ) );
      NSQ->sLF_AR_shp_Q14 = sLF_AR_shp_Q14;
      NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx ] = silk_SUB32_ovflw(sLF_AR_shp_Q14, silk_LSHIFT32(n_LF_Q12, 2));
      sLTP_Q15[ NSQ->sLTP_buf_idx ] = silk_LSHIFT( LPC_exc_Q14, 1 );
      NSQ->sLTP_shp_buf_idx++;
      NSQ->sLTP_buf_idx++;
      NSQ->rand_seed = silk_ADD32_ovflw( NSQ->rand_seed, pulses[ i ] );
   }
   silk_memcpy( NSQ->sLPC_Q14, &NSQ->sLPC_Q14[ length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
}

static inline void silk_nsq_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type
)
{
   int i, lag;
   fl_i32 gain_adj_Q16, inv_gain_Q31, inv_gain_Q26;
   lag = pitchL[ subfr ];
   inv_gain_Q31 = silk_INVERSE32_varQ( silk_max( Gains_Q16[ subfr ], 1 ), 47 );
   silk_assert( inv_gain_Q31 != 0 );
   inv_gain_Q26 = silk_RSHIFT_ROUND( inv_gain_Q31, 5 );
   for( i = 0; i < psEncC->subfr_length; i++ ) {
      x_sc_Q10[ i ] = silk_SMULWW( x16[ i ], inv_gain_Q26 );
   }
   if( NSQ->rewhite_flag ) {
      if( subfr == 0 ) {
         inv_gain_Q31 = silk_LSHIFT( silk_SMULWB( inv_gain_Q31, LTP_scale_Q14 ), 2 );
      }
      for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
         silk_assert( i < MAX_FRAME_LENGTH );
         sLTP_Q15[ i ] = silk_SMULWB( inv_gain_Q31, sLTP[ i ] );
      }
   }
   if( Gains_Q16[ subfr ] != NSQ->prev_gain_Q16 ) {
      gain_adj_Q16 = silk_DIV32_varQ( NSQ->prev_gain_Q16, Gains_Q16[ subfr ], 16 );
      for( i = NSQ->sLTP_shp_buf_idx - psEncC->ltp_mem_length; i < NSQ->sLTP_shp_buf_idx; i++ ) {
         NSQ->sLTP_shp_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLTP_shp_Q14[ i ] );
      }
      if( signal_type == TYPE_VOICED && NSQ->rewhite_flag == 0 ) {
         for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
            sLTP_Q15[ i ] = silk_SMULWW( gain_adj_Q16, sLTP_Q15[ i ] );
         }
      }
      NSQ->sLF_AR_shp_Q14 = silk_SMULWW( gain_adj_Q16, NSQ->sLF_AR_shp_Q14 );
      NSQ->sDiff_shp_Q14 = silk_SMULWW( gain_adj_Q16, NSQ->sDiff_shp_Q14 );
      for( i = 0; i < NSQ_LPC_BUF_LENGTH; i++ ) {
         NSQ->sLPC_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLPC_Q14[ i ] );
      }
      for( i = 0; i < MAX_SHAPE_LPC_ORDER; i++ ) {
         NSQ->sAR2_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sAR2_Q14[ i ] );
      }
      NSQ->prev_gain_Q16 = Gains_Q16[ subfr ];
   }
}

static void silk_NSQ_del_dec_c(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   SideInfoIndices *psIndices,
   const fl_i16 x16[],
   fl_i8 pulses[],
   const fl_i16 *PredCoef_Q12,
   const fl_i16 LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],
   const fl_i16 AR_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ],
   const int HarmShapeGain_Q14[ MAX_NB_SUBFR ],
   const int Tilt_Q14[ MAX_NB_SUBFR ],
   const fl_i32 LF_shp_Q14[ MAX_NB_SUBFR ],
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int Lambda_Q10,
   const int LTP_scale_Q14
)
{
   int i, k, lag, start_idx, LSF_interpolation_flag, Winner_ind, subfr;
   int last_smple_idx, smpl_buf_idx, decisionDelay;
   const fl_i16 *A_Q12, *B_Q14, *AR_shp_Q13;
   fl_i16 *pxq;
   VARDECL( fl_i32, sLTP_Q15 );
   VARDECL( fl_i16, sLTP );
   fl_i32 HarmShapeFIRPacked_Q14;
   int offset_Q10;
   fl_i32 RDmin_Q10, Gain_Q10;
   VARDECL( fl_i32, x_sc_Q10 );
   VARDECL( fl_i32, delayedGain_Q10 );
   VARDECL( NSQ_del_dec_struct, psDelDec );
   NSQ_del_dec_struct *psDD;
   SAVE_STACK;
   lag = NSQ->lagPrev;
   silk_assert( NSQ->prev_gain_Q16 != 0 );
   ALLOC( psDelDec, psEncC->nStatesDelayedDecision, NSQ_del_dec_struct );
   silk_memset( psDelDec, 0, psEncC->nStatesDelayedDecision * sizeof( NSQ_del_dec_struct ) );
   for( k = 0; k < psEncC->nStatesDelayedDecision; k++ ) {
      psDD = &psDelDec[ k ];
      psDD->Seed = ( k + psIndices->Seed ) & 3;
      psDD->SeedInit = psDD->Seed;
      psDD->RD_Q10 = 0;
      psDD->LF_AR_Q14 = NSQ->sLF_AR_shp_Q14;
      psDD->Diff_Q14 = NSQ->sDiff_shp_Q14;
      psDD->Shape_Q14[ 0 ] = NSQ->sLTP_shp_Q14[ psEncC->ltp_mem_length - 1 ];
      silk_memcpy( psDD->sLPC_Q14, NSQ->sLPC_Q14, NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
      silk_memcpy( psDD->sAR2_Q14, NSQ->sAR2_Q14, sizeof( NSQ->sAR2_Q14 ) );
   }
   offset_Q10 = fl_Quantization_Offsets_Q10[ psIndices->signalType >> 1 ][ psIndices->quantOffsetType ];
   smpl_buf_idx = 0;
   decisionDelay = silk_min_int( DECISION_DELAY, psEncC->subfr_length );
   if( psIndices->signalType == TYPE_VOICED ) {
      for( k = 0; k < psEncC->nb_subfr; k++ ) {
         decisionDelay = silk_min_int( decisionDelay, pitchL[ k ] - LTP_ORDER / 2 - 1 );
      }
   } else {
      if( lag > 0 ) {
         decisionDelay = silk_min_int( decisionDelay, lag - LTP_ORDER / 2 - 1 );
      }
   }
   if( psIndices->NLSFInterpCoef_Q2 == 4 ) {
      LSF_interpolation_flag = 0;
   } else {
      LSF_interpolation_flag = 1;
   }
   ALLOC( sLTP_Q15, psEncC->ltp_mem_length + psEncC->frame_length, fl_i32 );
   ALLOC( sLTP, psEncC->ltp_mem_length + psEncC->frame_length, fl_i16 );
   ALLOC( x_sc_Q10, psEncC->subfr_length, fl_i32 );
   ALLOC( delayedGain_Q10, DECISION_DELAY, fl_i32 );
   pxq = &NSQ->xq[ psEncC->ltp_mem_length ];
   NSQ->sLTP_shp_buf_idx = psEncC->ltp_mem_length;
   NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
   subfr = 0;
   for( k = 0; k < psEncC->nb_subfr; k++ ) {
      A_Q12 = &PredCoef_Q12[ ( ( k >> 1 ) | ( 1 - LSF_interpolation_flag ) ) * MAX_LPC_ORDER ];
      B_Q14 = &LTPCoef_Q14[ k * LTP_ORDER ];
      AR_shp_Q13 = &AR_Q13[ k * MAX_SHAPE_LPC_ORDER ];
      silk_assert( HarmShapeGain_Q14[ k ] >= 0 );
      HarmShapeFIRPacked_Q14 = silk_RSHIFT( HarmShapeGain_Q14[ k ], 2 );
      HarmShapeFIRPacked_Q14 |= silk_LSHIFT( (fl_i32)silk_RSHIFT( HarmShapeGain_Q14[ k ], 1 ), 16 );
      NSQ->rewhite_flag = 0;
      if( psIndices->signalType == TYPE_VOICED ) {
         lag = pitchL[ k ];
         if( ( k & ( 3 - silk_LSHIFT( LSF_interpolation_flag, 1 ) ) ) == 0 ) {
            if( k == 2 ) {
               RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
               Winner_ind = 0;
               for( i = 1; i < psEncC->nStatesDelayedDecision; i++ ) {
                  if( psDelDec[ i ].RD_Q10 < RDmin_Q10 ) {
                     RDmin_Q10 = psDelDec[ i ].RD_Q10;
                     Winner_ind = i;
                  }
               }
               for( i = 0; i < psEncC->nStatesDelayedDecision; i++ ) {
                  if( i != Winner_ind ) {
                     psDelDec[ i ].RD_Q10 += ( silk_int32_MAX >> 4 );
                     silk_assert( psDelDec[ i ].RD_Q10 >= 0 );
                  }
               }
               psDD = &psDelDec[ Winner_ind ];
               last_smple_idx = smpl_buf_idx + decisionDelay;
               for( i = 0; i < decisionDelay; i++ ) {
                  last_smple_idx = ( last_smple_idx - 1 ) % DECISION_DELAY;
                  if( last_smple_idx < 0 ) last_smple_idx += DECISION_DELAY;
                  pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
                  pxq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
                     silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], Gains_Q16[ 1 ] ), 14 ) );
                  NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q14[ last_smple_idx ];
               }
               subfr = 0;
            }
            start_idx = psEncC->ltp_mem_length - lag - psEncC->predictLPCOrder - LTP_ORDER / 2;
            celt_assert( start_idx > 0 );
            silk_LPC_analysis_filter( &sLTP[ start_idx ], &NSQ->xq[ start_idx + k * psEncC->subfr_length ],
               A_Q12, psEncC->ltp_mem_length - start_idx, psEncC->predictLPCOrder, psEncC->arch );
            NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
            NSQ->rewhite_flag = 1;
         }
      }
      silk_nsq_del_dec_scale_states( psEncC, NSQ, psDelDec, x16, x_sc_Q10, sLTP, sLTP_Q15, k,
         psEncC->nStatesDelayedDecision, LTP_scale_Q14, Gains_Q16, pitchL, psIndices->signalType, decisionDelay );
      silk_noise_shape_quantizer_del_dec( NSQ, psDelDec, psIndices->signalType, x_sc_Q10, pulses, pxq, sLTP_Q15,
         delayedGain_Q10, A_Q12, B_Q14, AR_shp_Q13, lag, HarmShapeFIRPacked_Q14, Tilt_Q14[ k ], LF_shp_Q14[ k ],
         Gains_Q16[ k ], Lambda_Q10, offset_Q10, psEncC->subfr_length, subfr++, psEncC->shapingLPCOrder,
         psEncC->predictLPCOrder, psEncC->warping_Q16, psEncC->nStatesDelayedDecision, &smpl_buf_idx, decisionDelay, psEncC->arch );
      x16 += psEncC->subfr_length;
      pulses += psEncC->subfr_length;
      pxq += psEncC->subfr_length;
   }
   RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
   Winner_ind = 0;
   for( k = 1; k < psEncC->nStatesDelayedDecision; k++ ) {
      if( psDelDec[ k ].RD_Q10 < RDmin_Q10 ) {
         RDmin_Q10 = psDelDec[ k ].RD_Q10;
         Winner_ind = k;
      }
   }
   psDD = &psDelDec[ Winner_ind ];
   psIndices->Seed = psDD->SeedInit;
   last_smple_idx = smpl_buf_idx + decisionDelay;
   Gain_Q10 = silk_RSHIFT32( Gains_Q16[ psEncC->nb_subfr - 1 ], 6 );
   for( i = 0; i < decisionDelay; i++ ) {
      last_smple_idx = ( last_smple_idx - 1 ) % DECISION_DELAY;
      if( last_smple_idx < 0 ) last_smple_idx += DECISION_DELAY;
      pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
      pxq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
         silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], Gain_Q10 ), 8 ) );
      NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q14[ last_smple_idx ];
   }
   silk_memcpy( NSQ->sLPC_Q14, &psDD->sLPC_Q14[ psEncC->subfr_length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
   silk_memcpy( NSQ->sAR2_Q14, psDD->sAR2_Q14, sizeof( psDD->sAR2_Q14 ) );
   NSQ->sLF_AR_shp_Q14 = psDD->LF_AR_Q14;
   NSQ->sDiff_shp_Q14 = psDD->Diff_Q14;
   NSQ->lagPrev = pitchL[ psEncC->nb_subfr - 1 ];
   silk_memmove( NSQ->xq, &NSQ->xq[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i16 ) );
   silk_memmove( NSQ->sLTP_shp_Q14, &NSQ->sLTP_shp_Q14[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static inline void silk_noise_shape_quantizer_del_dec(
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   int signalType,
   const fl_i32 x_Q10[],
   fl_i8 pulses[],
   fl_i16 xq[],
   fl_i32 sLTP_Q15[],
   fl_i32 delayedGain_Q10[],
   const fl_i16 a_Q12[],
   const fl_i16 b_Q14[],
   const fl_i16 AR_shp_Q13[],
   int lag,
   fl_i32 HarmShapeFIRPacked_Q14,
   int Tilt_Q14,
   fl_i32 LF_shp_Q14,
   fl_i32 Gain_Q16,
   int Lambda_Q10,
   int offset_Q10,
   int length,
   int subfr,
   int shapingLPCOrder,
   int predictLPCOrder,
   int warping_Q16,
   int nStatesDelayedDecision,
   int *smpl_buf_idx,
   int decisionDelay,
   int arch
)
{
   int i, j, k, Winner_ind, RDmin_ind, RDmax_ind, last_smple_idx;
   fl_i32 Winner_rand_state;
   fl_i32 LTP_pred_Q14, LPC_pred_Q14, n_AR_Q14, n_LTP_Q14;
   fl_i32 n_LF_Q14, r_Q10, rr_Q10, rd1_Q10, rd2_Q10, RDmin_Q10, RDmax_Q10;
   fl_i32 q1_Q0, q1_Q10, q2_Q10, exc_Q14, LPC_exc_Q14, xq_Q14, Gain_Q10;
   fl_i32 tmp1, tmp2, sLF_AR_shp_Q14;
   fl_i32 *pred_lag_ptr, *shp_lag_ptr, *psLPC_Q14;
   fl_i32 a_Q12_arch[MAX_LPC_ORDER];
   VARDECL( NSQ_sample_pair, psSampleState );
   NSQ_del_dec_struct *psDD;
   NSQ_sample_struct *psSS;
   SAVE_STACK;
   celt_assert( nStatesDelayedDecision > 0 );
   ALLOC( psSampleState, nStatesDelayedDecision, NSQ_sample_pair );
   shp_lag_ptr = &NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - lag + HARM_SHAPE_FIR_TAPS / 2 ];
   pred_lag_ptr = &sLTP_Q15[ NSQ->sLTP_buf_idx - lag + LTP_ORDER / 2 ];
   Gain_Q10 = silk_RSHIFT( Gain_Q16, 6 );
   silk_short_prediction_create_arch_coef(a_Q12_arch, a_Q12, predictLPCOrder);
   for( i = 0; i < length; i++ ) {
      if( signalType == TYPE_VOICED ) {
         LTP_pred_Q14 = 2;
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ 0 ], b_Q14[ 0 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -1 ], b_Q14[ 1 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -2 ], b_Q14[ 2 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -3 ], b_Q14[ 3 ] );
         LTP_pred_Q14 = silk_SMLAWB( LTP_pred_Q14, pred_lag_ptr[ -4 ], b_Q14[ 4 ] );
         LTP_pred_Q14 = silk_LSHIFT( LTP_pred_Q14, 1 );
         pred_lag_ptr++;
      } else {
         LTP_pred_Q14 = 0;
      }
      if( lag > 0 ) {
         n_LTP_Q14 = silk_SMULWB( silk_ADD_SAT32( shp_lag_ptr[ 0 ], shp_lag_ptr[ -2 ] ), HarmShapeFIRPacked_Q14 );
         n_LTP_Q14 = silk_SMLAWT( n_LTP_Q14, shp_lag_ptr[ -1 ], HarmShapeFIRPacked_Q14 );
         n_LTP_Q14 = silk_SUB_LSHIFT32( LTP_pred_Q14, n_LTP_Q14, 2 );
         shp_lag_ptr++;
      } else {
         n_LTP_Q14 = 0;
      }
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psSS = psSampleState[ k ];
         psDD->Seed = silk_RAND( psDD->Seed );
         psLPC_Q14 = &psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH - 1 + i ];
         LPC_pred_Q14 = silk_noise_shape_quantizer_short_prediction(psLPC_Q14, a_Q12, a_Q12_arch, predictLPCOrder, arch);
         LPC_pred_Q14 = silk_LSHIFT( LPC_pred_Q14, 4 );
         celt_assert( ( shapingLPCOrder & 1 ) == 0 );
         tmp2 = silk_SMLAWB( psDD->Diff_Q14, psDD->sAR2_Q14[ 0 ], warping_Q16 );
         tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ 0 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ 1 ], tmp2), warping_Q16 );
         psDD->sAR2_Q14[ 0 ] = tmp2;
         n_AR_Q14 = silk_RSHIFT( shapingLPCOrder, 1 );
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp2, AR_shp_Q13[ 0 ] );
         for( j = 2; j < shapingLPCOrder; j += 2 ) {
            tmp2 = silk_SMLAWB( psDD->sAR2_Q14[ j - 1 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ j + 0 ], tmp1), warping_Q16 );
            psDD->sAR2_Q14[ j - 1 ] = tmp1;
            n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp1, AR_shp_Q13[ j - 1 ] );
            tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ j + 0 ], silk_SUB32_ovflw(psDD->sAR2_Q14[ j + 1 ], tmp2), warping_Q16 );
            psDD->sAR2_Q14[ j + 0 ] = tmp2;
            n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp2, AR_shp_Q13[ j ] );
         }
         psDD->sAR2_Q14[ shapingLPCOrder - 1 ] = tmp1;
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, tmp1, AR_shp_Q13[ shapingLPCOrder - 1 ] );
         n_AR_Q14 = silk_LSHIFT( n_AR_Q14, 1 );
         n_AR_Q14 = silk_SMLAWB( n_AR_Q14, psDD->LF_AR_Q14, Tilt_Q14 );
         n_AR_Q14 = silk_LSHIFT( n_AR_Q14, 2 );
         n_LF_Q14 = silk_SMULWB( psDD->Shape_Q14[ *smpl_buf_idx ], LF_shp_Q14 );
         n_LF_Q14 = silk_SMLAWT( n_LF_Q14, psDD->LF_AR_Q14, LF_shp_Q14 );
         n_LF_Q14 = silk_LSHIFT( n_LF_Q14, 2 );
         tmp1 = silk_ADD_SAT32( n_AR_Q14, n_LF_Q14 );
         tmp2 = silk_ADD32_ovflw( n_LTP_Q14, LPC_pred_Q14 );
         tmp1 = silk_SUB_SAT32( tmp2, tmp1 );
         tmp1 = silk_RSHIFT_ROUND( tmp1, 4 );
         r_Q10 = silk_SUB32( x_Q10[ i ], tmp1 );
         if ( psDD->Seed < 0 ) {
            r_Q10 = -r_Q10;
         }
         r_Q10 = silk_LIMIT_32( r_Q10, -(31 << 10), 30 << 10 );
         q1_Q10 = silk_SUB32( r_Q10, offset_Q10 );
         q1_Q0 = silk_RSHIFT( q1_Q10, 10 );
         if (Lambda_Q10 > 2048) {
            int rdo_offset = Lambda_Q10/2 - 512;
            if (q1_Q10 > rdo_offset) {
               q1_Q0 = silk_RSHIFT( q1_Q10 - rdo_offset, 10 );
            } else if (q1_Q10 < -rdo_offset) {
               q1_Q0 = silk_RSHIFT( q1_Q10 + rdo_offset, 10 );
            } else if (q1_Q10 < 0) {
               q1_Q0 = -1;
            } else {
               q1_Q0 = 0;
            }
         }
         if( q1_Q0 > 0 ) {
            q1_Q10 = silk_SUB32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
            q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
            q2_Q10 = silk_ADD32( q1_Q10, 1024 );
            rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else if( q1_Q0 == 0 ) {
            q1_Q10 = offset_Q10;
            q2_Q10 = silk_ADD32( q1_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
            rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else if( q1_Q0 == -1 ) {
            q2_Q10 = offset_Q10;
            q1_Q10 = silk_SUB32( q2_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
            rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
         } else {
            q1_Q10 = silk_ADD32( silk_LSHIFT( q1_Q0, 10 ), QUANT_LEVEL_ADJUST_Q10 );
            q1_Q10 = silk_ADD32( q1_Q10, offset_Q10 );
            q2_Q10 = silk_ADD32( q1_Q10, 1024 );
            rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
            rd2_Q10 = silk_SMULBB( -q2_Q10, Lambda_Q10 );
         }
         rr_Q10 = silk_SUB32( r_Q10, q1_Q10 );
         rd1_Q10 = silk_RSHIFT( silk_SMLABB( rd1_Q10, rr_Q10, rr_Q10 ), 10 );
         rr_Q10 = silk_SUB32( r_Q10, q2_Q10 );
         rd2_Q10 = silk_RSHIFT( silk_SMLABB( rd2_Q10, rr_Q10, rr_Q10 ), 10 );
         if( rd1_Q10 < rd2_Q10 ) {
            psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
            psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
            psSS[ 0 ].Q_Q10 = q1_Q10;
            psSS[ 1 ].Q_Q10 = q2_Q10;
         } else {
            psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
            psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
            psSS[ 0 ].Q_Q10 = q2_Q10;
            psSS[ 1 ].Q_Q10 = q1_Q10;
         }
         exc_Q14 = silk_LSHIFT32( psSS[ 0 ].Q_Q10, 4 );
         if ( psDD->Seed < 0 ) {
            exc_Q14 = -exc_Q14;
         }
         LPC_exc_Q14 = silk_ADD32( exc_Q14, LTP_pred_Q14 );
         xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, LPC_pred_Q14 );
         psSS[ 0 ].Diff_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_Q10[ i ], 4 ) );
         sLF_AR_shp_Q14 = silk_SUB32_ovflw( psSS[ 0 ].Diff_Q14, n_AR_Q14 );
         psSS[ 0 ].sLTP_shp_Q14 = silk_SUB_SAT32( sLF_AR_shp_Q14, n_LF_Q14 );
         psSS[ 0 ].LF_AR_Q14 = sLF_AR_shp_Q14;
         psSS[ 0 ].LPC_exc_Q14 = LPC_exc_Q14;
         psSS[ 0 ].xq_Q14 = xq_Q14;
         exc_Q14 = silk_LSHIFT32( psSS[ 1 ].Q_Q10, 4 );
         if ( psDD->Seed < 0 ) {
            exc_Q14 = -exc_Q14;
         }
         LPC_exc_Q14 = silk_ADD32( exc_Q14, LTP_pred_Q14 );
         xq_Q14 = silk_ADD32_ovflw( LPC_exc_Q14, LPC_pred_Q14 );
         psSS[ 1 ].Diff_Q14 = silk_SUB32_ovflw( xq_Q14, silk_LSHIFT32( x_Q10[ i ], 4 ) );
         sLF_AR_shp_Q14 = silk_SUB32_ovflw( psSS[ 1 ].Diff_Q14, n_AR_Q14 );
         psSS[ 1 ].sLTP_shp_Q14 = silk_SUB_SAT32( sLF_AR_shp_Q14, n_LF_Q14 );
         psSS[ 1 ].LF_AR_Q14 = sLF_AR_shp_Q14;
         psSS[ 1 ].LPC_exc_Q14 = LPC_exc_Q14;
         psSS[ 1 ].xq_Q14 = xq_Q14;
      }
      *smpl_buf_idx = ( *smpl_buf_idx - 1 ) % DECISION_DELAY;
      if( *smpl_buf_idx < 0 ) *smpl_buf_idx += DECISION_DELAY;
      last_smple_idx = ( *smpl_buf_idx + decisionDelay ) % DECISION_DELAY;
      RDmin_Q10 = psSampleState[ 0 ][ 0 ].RD_Q10;
      Winner_ind = 0;
      for( k = 1; k < nStatesDelayedDecision; k++ ) {
         if( psSampleState[ k ][ 0 ].RD_Q10 < RDmin_Q10 ) {
            RDmin_Q10 = psSampleState[ k ][ 0 ].RD_Q10;
            Winner_ind = k;
         }
      }
      Winner_rand_state = psDelDec[ Winner_ind ].RandState[ last_smple_idx ];
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         if( psDelDec[ k ].RandState[ last_smple_idx ] != Winner_rand_state ) {
            psSampleState[ k ][ 0 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 0 ].RD_Q10, silk_int32_MAX >> 4 );
            psSampleState[ k ][ 1 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 1 ].RD_Q10, silk_int32_MAX >> 4 );
            silk_assert( psSampleState[ k ][ 0 ].RD_Q10 >= 0 );
         }
      }
      RDmax_Q10 = psSampleState[ 0 ][ 0 ].RD_Q10;
      RDmin_Q10 = psSampleState[ 0 ][ 1 ].RD_Q10;
      RDmax_ind = 0;
      RDmin_ind = 0;
      for( k = 1; k < nStatesDelayedDecision; k++ ) {
         if( psSampleState[ k ][ 0 ].RD_Q10 > RDmax_Q10 ) {
            RDmax_Q10 = psSampleState[ k ][ 0 ].RD_Q10;
            RDmax_ind = k;
         }
         if( psSampleState[ k ][ 1 ].RD_Q10 < RDmin_Q10 ) {
            RDmin_Q10 = psSampleState[ k ][ 1 ].RD_Q10;
            RDmin_ind = k;
         }
      }
      if( RDmin_Q10 < RDmax_Q10 ) {
         silk_memcpy( ( (fl_i32 *)&psDelDec[ RDmax_ind ] ) + i,
                  ( (fl_i32 *)&psDelDec[ RDmin_ind ] ) + i, sizeof( NSQ_del_dec_struct ) - i * sizeof( fl_i32) );
         silk_memcpy( &psSampleState[ RDmax_ind ][ 0 ], &psSampleState[ RDmin_ind ][ 1 ], sizeof( NSQ_sample_struct ) );
      }
      psDD = &psDelDec[ Winner_ind ];
      if( subfr > 0 || i >= decisionDelay ) {
         pulses[ i - decisionDelay ] = (fl_i8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
         xq[ i - decisionDelay ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND(
            silk_SMULWW( psDD->Xq_Q14[ last_smple_idx ], delayedGain_Q10[ last_smple_idx ] ), 8 ) );
         NSQ->sLTP_shp_Q14[ NSQ->sLTP_shp_buf_idx - decisionDelay ] = psDD->Shape_Q14[ last_smple_idx ];
         sLTP_Q15[ NSQ->sLTP_buf_idx - decisionDelay ] = psDD->Pred_Q15[ last_smple_idx ];
      }
      NSQ->sLTP_shp_buf_idx++;
      NSQ->sLTP_buf_idx++;
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psSS = &psSampleState[ k ][ 0 ];
         psDD->LF_AR_Q14 = psSS->LF_AR_Q14;
         psDD->Diff_Q14 = psSS->Diff_Q14;
         psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH + i ] = psSS->xq_Q14;
         psDD->Xq_Q14[ *smpl_buf_idx ] = psSS->xq_Q14;
         psDD->Q_Q10[ *smpl_buf_idx ] = psSS->Q_Q10;
         psDD->Pred_Q15[ *smpl_buf_idx ] = silk_LSHIFT32( psSS->LPC_exc_Q14, 1 );
         psDD->Shape_Q14[ *smpl_buf_idx ] = psSS->sLTP_shp_Q14;
         psDD->Seed = silk_ADD32_ovflw( psDD->Seed, silk_RSHIFT_ROUND( psSS->Q_Q10, 10 ) );
         psDD->RandState[ *smpl_buf_idx ] = psDD->Seed;
         psDD->RD_Q10 = psSS->RD_Q10;
      }
      delayedGain_Q10[ *smpl_buf_idx ] = Gain_Q10;
   }
   for( k = 0; k < nStatesDelayedDecision; k++ ) {
      psDD = &psDelDec[ k ];
      silk_memcpy( psDD->sLPC_Q14, &psDD->sLPC_Q14[ length ], NSQ_LPC_BUF_LENGTH * sizeof( fl_i32 ) );
   }
   RESTORE_STACK;
}

static inline void silk_nsq_del_dec_scale_states(
   const silk_encoder_state *psEncC,
   silk_nsq_state *NSQ,
   NSQ_del_dec_struct psDelDec[],
   const fl_i16 x16[],
   fl_i32 x_sc_Q10[],
   const fl_i16 sLTP[],
   fl_i32 sLTP_Q15[],
   int subfr,
   int nStatesDelayedDecision,
   const int LTP_scale_Q14,
   const fl_i32 Gains_Q16[ MAX_NB_SUBFR ],
   const int pitchL[ MAX_NB_SUBFR ],
   const int signal_type,
   const int decisionDelay
)
{
   int i, k, lag;
   fl_i32 gain_adj_Q16, inv_gain_Q31, inv_gain_Q26;
   NSQ_del_dec_struct *psDD;
   lag = pitchL[ subfr ];
   inv_gain_Q31 = silk_INVERSE32_varQ( silk_max( Gains_Q16[ subfr ], 1 ), 47 );
   silk_assert( inv_gain_Q31 != 0 );
   inv_gain_Q26 = silk_RSHIFT_ROUND( inv_gain_Q31, 5 );
   for( i = 0; i < psEncC->subfr_length; i++ ) {
      x_sc_Q10[ i ] = silk_SMULWW( x16[ i ], inv_gain_Q26 );
   }
   if( NSQ->rewhite_flag ) {
      if( subfr == 0 ) {
         inv_gain_Q31 = silk_LSHIFT( silk_SMULWB( inv_gain_Q31, LTP_scale_Q14 ), 2 );
      }
      for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
         silk_assert( i < MAX_FRAME_LENGTH );
         sLTP_Q15[ i ] = silk_SMULWB( inv_gain_Q31, sLTP[ i ] );
      }
   }
   if( Gains_Q16[ subfr ] != NSQ->prev_gain_Q16 ) {
      gain_adj_Q16 = silk_DIV32_varQ( NSQ->prev_gain_Q16, Gains_Q16[ subfr ], 16 );
      for( i = NSQ->sLTP_shp_buf_idx - psEncC->ltp_mem_length; i < NSQ->sLTP_shp_buf_idx; i++ ) {
         NSQ->sLTP_shp_Q14[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLTP_shp_Q14[ i ] );
      }
      if( signal_type == TYPE_VOICED && NSQ->rewhite_flag == 0 ) {
         for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx - decisionDelay; i++ ) {
            sLTP_Q15[ i ] = silk_SMULWW( gain_adj_Q16, sLTP_Q15[ i ] );
         }
      }
      for( k = 0; k < nStatesDelayedDecision; k++ ) {
         psDD = &psDelDec[ k ];
         psDD->LF_AR_Q14 = silk_SMULWW( gain_adj_Q16, psDD->LF_AR_Q14 );
         psDD->Diff_Q14 = silk_SMULWW( gain_adj_Q16, psDD->Diff_Q14 );
         for( i = 0; i < NSQ_LPC_BUF_LENGTH; i++ ) {
            psDD->sLPC_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sLPC_Q14[ i ] );
         }
         for( i = 0; i < MAX_SHAPE_LPC_ORDER; i++ ) {
            psDD->sAR2_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sAR2_Q14[ i ] );
         }
         for( i = 0; i < DECISION_DELAY; i++ ) {
            psDD->Pred_Q15[ i ] = silk_SMULWW( gain_adj_Q16, psDD->Pred_Q15[ i ] );
            psDD->Shape_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->Shape_Q14[ i ] );
         }
      }
      NSQ->prev_gain_Q16 = Gains_Q16[ subfr ];
   }
}

static void silk_resampler_down2(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
)
{
   fl_i32 k, len2 = silk_RSHIFT32( inLen, 1 );
   fl_i32 in32, out32, Y, X;
   celt_assert( silk_resampler_down2_0 > 0 );
   celt_assert( silk_resampler_down2_1 < 0 );
   for( k = 0; k < len2; k++ ) {
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k ], 10 );
      Y = silk_SUB32( in32, S[ 0 ] );
      X = silk_SMLAWB( Y, Y, silk_resampler_down2_1 );
      out32 = silk_ADD32( S[ 0 ], X );
      S[ 0 ] = silk_ADD32( in32, X );
      in32 = silk_LSHIFT( (fl_i32)in[ 2 * k + 1 ], 10 );
      Y = silk_SUB32( in32, S[ 1 ] );
      X = silk_SMULWB( Y, silk_resampler_down2_0 );
      out32 = silk_ADD32( out32, S[ 1 ] );
      out32 = silk_ADD32( out32, X );
      S[ 1 ] = silk_ADD32( in32, X );
      out[ k ] = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( out32, 11 ) );
   }
}

static void silk_resampler_down2_3(
   fl_i32 *S,
   fl_i16 *out,
   const fl_i16 *in,
   fl_i32 inLen
)
{
   fl_i32 nSamplesIn, counter, res_Q6;
   VARDECL( fl_i32, buf );
   fl_i32 *buf_ptr;
   SAVE_STACK;
   ALLOC( buf, RESAMPLER_MAX_BATCH_SIZE_IN + ORDER_FIR, fl_i32 );
   silk_memcpy( buf, S, ORDER_FIR * sizeof( fl_i32 ) );
   while( 1 ) {
      nSamplesIn = silk_min( inLen, RESAMPLER_MAX_BATCH_SIZE_IN );
      silk_resampler_private_AR2( &S[ ORDER_FIR ], &buf[ ORDER_FIR ], in,
         fl_Resampler_2_3_COEFS_LQ, nSamplesIn );
      buf_ptr = buf;
      counter = nSamplesIn;
      while( counter > 2 ) {
         res_Q6 = silk_SMULWB( buf_ptr[ 0 ], fl_Resampler_2_3_COEFS_LQ[ 2 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 1 ], fl_Resampler_2_3_COEFS_LQ[ 3 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 2 ], fl_Resampler_2_3_COEFS_LQ[ 5 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 3 ], fl_Resampler_2_3_COEFS_LQ[ 4 ] );
         *out++ = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( res_Q6, 6 ) );
         res_Q6 = silk_SMULWB( buf_ptr[ 1 ], fl_Resampler_2_3_COEFS_LQ[ 4 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 2 ], fl_Resampler_2_3_COEFS_LQ[ 5 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 3 ], fl_Resampler_2_3_COEFS_LQ[ 3 ] );
         res_Q6 = silk_SMLAWB( res_Q6, buf_ptr[ 4 ], fl_Resampler_2_3_COEFS_LQ[ 2 ] );
         *out++ = (fl_i16)silk_SAT16( silk_RSHIFT_ROUND( res_Q6, 6 ) );
         buf_ptr += 3;
         counter -= 3;
      }
      in += nSamplesIn;
      inLen -= nSamplesIn;
      if( inLen > 0 ) {
         silk_memcpy( buf, &buf[ nSamplesIn ], ORDER_FIR * sizeof( fl_i32 ) );
      } else {
         break;
      }
   }
   silk_memcpy( S, &buf[ nSamplesIn ], ORDER_FIR * sizeof( fl_i32 ) );
   RESTORE_STACK;
}

static int silk_Get_Encoder_Size(
   int *encSizeBytes,
   int channels
)
{
   int ret = SILK_NO_ERROR;
   *encSizeBytes = sizeof( fl_silk_encoder );
   if ( channels == 1 ) {
      *encSizeBytes -= sizeof( silk_encoder_state_Fxx );
   }
   return ret;
}

static int silk_InitEncoder(
   void *encState,
   int channels,
   int arch,
   fl_silk_ctrl *encStatus
)
{
   fl_silk_encoder *psEnc;
   int n, ret = SILK_NO_ERROR;
   psEnc = (fl_silk_encoder *)encState;
   silk_memset( psEnc, 0, sizeof( fl_silk_encoder ) - (channels==1)*sizeof( silk_encoder_state_Fxx ) );
   for( n = 0; n < channels; n++ ) {
      if( ret += silk_init_encoder( &psEnc->state_Fxx[ n ], arch ) ) {
         celt_assert( 0 );
      }
   }
   psEnc->nChannelsAPI = 1;
   psEnc->nChannelsInternal = 1;
   if( ret += silk_QueryEncoder( encState, encStatus ) ) {
      celt_assert( 0 );
   }
   return ret;
}

static int silk_QueryEncoder(
   const void *encState,
   fl_silk_ctrl *encStatus
)
{
   int ret = SILK_NO_ERROR;
   silk_encoder_state_Fxx *state_Fxx;
   fl_silk_encoder *psEnc = (fl_silk_encoder *)encState;
   state_Fxx = psEnc->state_Fxx;
   encStatus->nChannelsAPI = psEnc->nChannelsAPI;
   encStatus->nChannelsInternal = psEnc->nChannelsInternal;
   encStatus->API_sampleRate = state_Fxx[ 0 ].sCmn.API_fs_Hz;
   encStatus->maxInternalSampleRate = state_Fxx[ 0 ].sCmn.maxInternal_fs_Hz;
   encStatus->minInternalSampleRate = state_Fxx[ 0 ].sCmn.minInternal_fs_Hz;
   encStatus->desiredInternalSampleRate = state_Fxx[ 0 ].sCmn.desiredInternal_fs_Hz;
   encStatus->payloadSize_ms = state_Fxx[ 0 ].sCmn.PacketSize_ms;
   encStatus->bitRate = state_Fxx[ 0 ].sCmn.TargetRate_bps;
   encStatus->packetLossPercentage = state_Fxx[ 0 ].sCmn.PacketLoss_perc;
   encStatus->complexity = state_Fxx[ 0 ].sCmn.Complexity;
   encStatus->useInBandFEC = state_Fxx[ 0 ].sCmn.useInBandFEC;
   encStatus->useDTX = state_Fxx[ 0 ].sCmn.useDTX;
   encStatus->useCBR = state_Fxx[ 0 ].sCmn.useCBR;
   encStatus->internalSampleRate = silk_SMULBB( state_Fxx[ 0 ].sCmn.fs_kHz, 1000 );
   encStatus->allowBandwidthSwitch = state_Fxx[ 0 ].sCmn.allow_bandwidth_switch;
   encStatus->inWBmodeWithoutVariableLP = state_Fxx[ 0 ].sCmn.fs_kHz == 16 && state_Fxx[ 0 ].sCmn.sLP.mode == 0;
   return ret;
}

static int silk_Encode(
   void *encState,
   fl_silk_ctrl *encControl,
   const opus_res *samplesIn,
   int nSamplesIn,
   ec_ctx *psRangeEnc,
   fl_i32 *nBytesOut,
   const int prefillFlag,
   int activity
)
{
   int n, i, nBits, flags, tmp_payloadSize_ms = 0, tmp_complexity = 0, ret = 0;
   int nSamplesToBuffer, nSamplesToBufferMax, nBlocksOf10ms;
   int nSamplesFromInput = 0, nSamplesFromInputMax;
   int speech_act_thr_for_switch_Q8;
   fl_i32 TargetRate_bps, MStargetRates_bps[ 2 ], channelRate_bps, LBRR_symbol, sum;
   fl_silk_encoder *psEnc = ( fl_silk_encoder * )encState;
   VARDECL( fl_i16, buf );
   int transition, curr_block, tot_blocks;
   SAVE_STACK;
   celt_assert( encControl->nChannelsAPI >= encControl->nChannelsInternal && encControl->nChannelsAPI >= psEnc->nChannelsInternal );
   if (encControl->reducedDependency)
   {
     for( n = 0; n < encControl->nChannelsAPI; n++ ) {
        psEnc->state_Fxx[ n ].sCmn.first_frame_after_reset = 1;
     }
   }
   for( n = 0; n < encControl->nChannelsAPI; n++ ) {
      psEnc->state_Fxx[ n ].sCmn.nFramesEncoded = 0;
   }
   if( ( ret = check_control_input( encControl ) ) != 0 ) {
      celt_assert( 0 );
      RESTORE_STACK;
      return ret;
   }
   encControl->switchReady = 0;
   if( encControl->nChannelsInternal > psEnc->nChannelsInternal ) {
      ret += silk_init_encoder( &psEnc->state_Fxx[ 1 ], psEnc->state_Fxx[ 0 ].sCmn.arch );
      silk_memset( psEnc->sStereo.pred_prev_Q13, 0, sizeof( psEnc->sStereo.pred_prev_Q13 ) );
      silk_memset( psEnc->sStereo.sSide, 0, sizeof( psEnc->sStereo.sSide ) );
      psEnc->sStereo.mid_side_amp_Q0[ 0 ] = 0;
      psEnc->sStereo.mid_side_amp_Q0[ 1 ] = 1;
      psEnc->sStereo.mid_side_amp_Q0[ 2 ] = 0;
      psEnc->sStereo.mid_side_amp_Q0[ 3 ] = 1;
      psEnc->sStereo.width_prev_Q14 = 0;
      psEnc->sStereo.smth_width_Q14 = SILK_FIX_CONST( 1, 14 );
      if( psEnc->nChannelsAPI == 2 ) {
         silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state, &psEnc->state_Fxx[ 0 ].sCmn.resampler_state, sizeof( silk_resampler_state ) );
         silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.In_HP_State, &psEnc->state_Fxx[ 0 ].sCmn.In_HP_State, sizeof( psEnc->state_Fxx[ 1 ].sCmn.In_HP_State ) );
      }
   }
   transition = (encControl->payloadSize_ms != psEnc->state_Fxx[ 0 ].sCmn.PacketSize_ms) || (psEnc->nChannelsInternal != encControl->nChannelsInternal);
   psEnc->nChannelsAPI = encControl->nChannelsAPI;
   psEnc->nChannelsInternal = encControl->nChannelsInternal;
   nBlocksOf10ms = silk_DIV32( 100 * nSamplesIn, encControl->API_sampleRate );
   tot_blocks = ( nBlocksOf10ms > 1 ) ? nBlocksOf10ms >> 1 : 1;
   curr_block = 0;
   if( prefillFlag ) {
      silk_LP_state save_LP;
      if( nBlocksOf10ms != 1 ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
      if ( prefillFlag == 2 ) {
         save_LP = psEnc->state_Fxx[ 0 ].sCmn.sLP;
         save_LP.saved_fs_kHz = psEnc->state_Fxx[ 0 ].sCmn.fs_kHz;
      }
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         ret = silk_init_encoder( &psEnc->state_Fxx[ n ], psEnc->state_Fxx[ n ].sCmn.arch );
         if ( prefillFlag == 2 ) {
            psEnc->state_Fxx[ n ].sCmn.sLP = save_LP;
         }
         celt_assert( !ret );
      }
      tmp_payloadSize_ms = encControl->payloadSize_ms;
      encControl->payloadSize_ms = 10;
      tmp_complexity = encControl->complexity;
      encControl->complexity = 0;
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
         psEnc->state_Fxx[ n ].sCmn.prefillFlag = 1;
      }
   } else {
      if( nBlocksOf10ms * encControl->API_sampleRate != 100 * nSamplesIn || nSamplesIn < 0 ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
      if( 1000 * (fl_i32)nSamplesIn > encControl->payloadSize_ms * encControl->API_sampleRate ) {
         celt_assert( 0 );
         RESTORE_STACK;
         return SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES;
      }
   }
   for( n = 0; n < encControl->nChannelsInternal; n++ ) {
      int force_fs_kHz = (n==1) ? psEnc->state_Fxx[0].sCmn.fs_kHz : 0;
      if( ( ret = silk_control_encoder( &psEnc->state_Fxx[ n ], encControl, psEnc->allowBandwidthSwitch, n, force_fs_kHz ) ) != 0 ) {
         silk_assert( 0 );
         RESTORE_STACK;
         return ret;
      }
      if( psEnc->state_Fxx[n].sCmn.first_frame_after_reset || transition ) {
         for( i = 0; i < psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket; i++ ) {
            psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ] = 0;
         }
      }
      psEnc->state_Fxx[ n ].sCmn.inDTX = psEnc->state_Fxx[ n ].sCmn.useDTX;
   }
   celt_assert( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 0 ].sCmn.fs_kHz == psEnc->state_Fxx[ 1 ].sCmn.fs_kHz );
   nSamplesToBufferMax =
      10 * nBlocksOf10ms * psEnc->state_Fxx[ 0 ].sCmn.fs_kHz;
   nSamplesFromInputMax =
      silk_DIV32_16( nSamplesToBufferMax *
                    psEnc->state_Fxx[ 0 ].sCmn.API_fs_Hz,
                 psEnc->state_Fxx[ 0 ].sCmn.fs_kHz * 1000 );
   ALLOC( buf, nSamplesFromInputMax, fl_i16 );
   while( 1 ) {
      int curr_nBitsUsedLBRR = 0;
      nSamplesToBuffer = psEnc->state_Fxx[ 0 ].sCmn.frame_length - psEnc->state_Fxx[ 0 ].sCmn.inputBufIx;
      nSamplesToBuffer = silk_min( nSamplesToBuffer, nSamplesToBufferMax );
      nSamplesFromInput = silk_DIV32_16( nSamplesToBuffer * psEnc->state_Fxx[ 0 ].sCmn.API_fs_Hz, psEnc->state_Fxx[ 0 ].sCmn.fs_kHz * 1000 );
      if( encControl->nChannelsAPI == 2 && encControl->nChannelsInternal == 2 ) {
         int id = psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded;
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[ n ] = RES2INT16(samplesIn[ 2 * n ]);
         }
         if( psEnc->nPrevChannelsInternal == 1 && id==0 ) {
           silk_memcpy( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state, &psEnc->state_Fxx[ 0 ].sCmn.resampler_state, sizeof(psEnc->state_Fxx[ 1 ].sCmn.resampler_state));
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
         nSamplesToBuffer = psEnc->state_Fxx[ 1 ].sCmn.frame_length - psEnc->state_Fxx[ 1 ].sCmn.inputBufIx;
         nSamplesToBuffer = silk_min( nSamplesToBuffer, 10 * nBlocksOf10ms * psEnc->state_Fxx[ 1 ].sCmn.fs_kHz );
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[ n ] = RES2INT16(samplesIn[ 2 * n + 1 ]);
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 1 ].sCmn.inputBufIx += nSamplesToBuffer;
      } else if( encControl->nChannelsAPI == 2 && encControl->nChannelsInternal == 1 ) {
         for( n = 0; n < nSamplesFromInput; n++ ) {
            sum = RES2INT16(samplesIn[ 2 * n ] + samplesIn[ 2 * n + 1 ]);
            buf[ n ] = (fl_i16)silk_RSHIFT_ROUND( sum, 1 );
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         if( psEnc->nPrevChannelsInternal == 2 && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == 0 ) {
           ret += silk_resampler( &psEnc->state_Fxx[ 1 ].sCmn.resampler_state,
              &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
           for( n = 0; n < psEnc->state_Fxx[ 0 ].sCmn.frame_length; n++ ) {
             psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx+n+2 ] =
                  silk_RSHIFT(psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx+n+2 ]
                         + psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ psEnc->state_Fxx[ 1 ].sCmn.inputBufIx+n+2 ], 1);
           }
         }
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
      } else {
         celt_assert( encControl->nChannelsAPI == 1 && encControl->nChannelsInternal == 1 );
         for( n = 0; n < nSamplesFromInput; n++ ) {
            buf[n] = RES2INT16(samplesIn[n]);
         }
         ret += silk_resampler( &psEnc->state_Fxx[ 0 ].sCmn.resampler_state,
            &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.inputBufIx + 2 ], buf, nSamplesFromInput );
         psEnc->state_Fxx[ 0 ].sCmn.inputBufIx += nSamplesToBuffer;
      }
      samplesIn += nSamplesFromInput * encControl->nChannelsAPI;
      nSamplesIn -= nSamplesFromInput;
      psEnc->allowBandwidthSwitch = 0;
      if( psEnc->state_Fxx[ 0 ].sCmn.inputBufIx >= psEnc->state_Fxx[ 0 ].sCmn.frame_length ) {
         celt_assert( psEnc->state_Fxx[ 0 ].sCmn.inputBufIx == psEnc->state_Fxx[ 0 ].sCmn.frame_length );
         celt_assert( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 1 ].sCmn.inputBufIx == psEnc->state_Fxx[ 1 ].sCmn.frame_length );
         if( psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == 0 && !prefillFlag ) {
            fl_u8 iCDF[ 2 ] = { 0, 0 };
            iCDF[ 0 ] = 256 - silk_RSHIFT( 256, ( psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket + 1 ) * encControl->nChannelsInternal );
            ec_enc_icdf( psRangeEnc, 0, iCDF, 8 );
            curr_nBitsUsedLBRR = ec_tell( psRangeEnc );
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               LBRR_symbol = 0;
               for( i = 0; i < psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket; i++ ) {
                  LBRR_symbol |= silk_LSHIFT( psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ], i );
               }
               psEnc->state_Fxx[ n ].sCmn.LBRR_flag = LBRR_symbol > 0 ? 1 : 0;
               if( LBRR_symbol && psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket > 1 ) {
                  ec_enc_icdf( psRangeEnc, LBRR_symbol - 1, fl_LBRR_flags_iCDF_ptr[ psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket - 2 ], 8 );
               }
            }
            for( i = 0; i < psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket; i++ ) {
               for( n = 0; n < encControl->nChannelsInternal; n++ ) {
                  if( psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i ] ) {
                     int condCoding;
                     if( encControl->nChannelsInternal == 2 && n == 0 ) {
                        silk_stereo_encode_pred( psRangeEnc, psEnc->sStereo.predIx[ i ] );
                        if( psEnc->state_Fxx[ 1 ].sCmn.LBRR_flags[ i ] == 0 ) {
                           silk_stereo_encode_mid_only( psRangeEnc, psEnc->sStereo.mid_only_flags[ i ] );
                        }
                     }
                     if( i > 0 && psEnc->state_Fxx[ n ].sCmn.LBRR_flags[ i - 1 ] ) {
                        condCoding = CODE_CONDITIONALLY;
                     } else {
                        condCoding = CODE_INDEPENDENTLY;
                     }
                     silk_encode_indices( &psEnc->state_Fxx[ n ].sCmn, psRangeEnc, i, 1, condCoding );
                     silk_encode_pulses( psRangeEnc, psEnc->state_Fxx[ n ].sCmn.indices_LBRR[i].signalType, psEnc->state_Fxx[ n ].sCmn.indices_LBRR[i].quantOffsetType,
                        psEnc->state_Fxx[ n ].sCmn.pulses_LBRR[ i ], psEnc->state_Fxx[ n ].sCmn.frame_length );
                  }
               }
            }
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               silk_memset( psEnc->state_Fxx[ n ].sCmn.LBRR_flags, 0, sizeof( psEnc->state_Fxx[ n ].sCmn.LBRR_flags ) );
            }
            curr_nBitsUsedLBRR = ec_tell( psRangeEnc ) - curr_nBitsUsedLBRR;
         }
         silk_HP_variable_cutoff( psEnc->state_Fxx );
         nBits = silk_DIV32_16( silk_MUL( encControl->bitRate, encControl->payloadSize_ms ), 1000 );
         if( !prefillFlag ) {
            if ( curr_nBitsUsedLBRR < 10 ) {
               psEnc->nBitsUsedLBRR = 0;
            } else if ( psEnc->nBitsUsedLBRR < 10) {
               psEnc->nBitsUsedLBRR = curr_nBitsUsedLBRR;
            } else {
               psEnc->nBitsUsedLBRR = ( psEnc->nBitsUsedLBRR + curr_nBitsUsedLBRR ) / 2;
            }
            nBits -= psEnc->nBitsUsedLBRR;
         }
         nBits = silk_DIV32_16( nBits, psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket );
         if( encControl->payloadSize_ms == 10 ) {
            TargetRate_bps = silk_SMULBB( nBits, 100 );
         } else {
            TargetRate_bps = silk_SMULBB( nBits, 50 );
         }
         TargetRate_bps -= silk_DIV32_16( silk_MUL( psEnc->nBitsExceeded, 1000 ), BITRESERVOIR_DECAY_TIME_MS );
         if( !prefillFlag && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded > 0 ) {
            fl_i32 bitsBalance = ec_tell( psRangeEnc ) - psEnc->nBitsUsedLBRR - nBits * psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded;
            TargetRate_bps -= silk_DIV32_16( silk_MUL( bitsBalance, 1000 ), BITRESERVOIR_DECAY_TIME_MS );
         }
         TargetRate_bps = silk_LIMIT( TargetRate_bps, encControl->bitRate, 5000 );
         if( encControl->nChannelsInternal == 2 ) {
            silk_stereo_LR_to_MS( &psEnc->sStereo, &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ 2 ], &psEnc->state_Fxx[ 1 ].sCmn.inputBuf[ 2 ],
               psEnc->sStereo.predIx[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ], &psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ],
               MStargetRates_bps, TargetRate_bps, psEnc->state_Fxx[ 0 ].sCmn.speech_activity_Q8, encControl->toMono,
               psEnc->state_Fxx[ 0 ].sCmn.fs_kHz, psEnc->state_Fxx[ 0 ].sCmn.frame_length );
            if( psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] == 0 ) {
               if( psEnc->prev_decode_only_middle == 1 ) {
                  silk_memset( &psEnc->state_Fxx[ 1 ].sShape, 0, sizeof( psEnc->state_Fxx[ 1 ].sShape ) );
                  silk_memset( &psEnc->state_Fxx[ 1 ].sCmn.sNSQ, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.sNSQ ) );
                  silk_memset( psEnc->state_Fxx[ 1 ].sCmn.prev_NLSFq_Q15, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.prev_NLSFq_Q15 ) );
                  silk_memset( &psEnc->state_Fxx[ 1 ].sCmn.sLP.In_LP_State, 0, sizeof( psEnc->state_Fxx[ 1 ].sCmn.sLP.In_LP_State ) );
                  psEnc->state_Fxx[ 1 ].sCmn.prevLag = 100;
                  psEnc->state_Fxx[ 1 ].sCmn.sNSQ.lagPrev = 100;
                  psEnc->state_Fxx[ 1 ].sShape.LastGainIndex = 10;
                  psEnc->state_Fxx[ 1 ].sCmn.prevSignalType = TYPE_NO_VOICE_ACTIVITY;
                  psEnc->state_Fxx[ 1 ].sCmn.sNSQ.prev_gain_Q16 = 65536;
                  psEnc->state_Fxx[ 1 ].sCmn.first_frame_after_reset = 1;
               }
               silk_encode_do_VAD_Fxx( &psEnc->state_Fxx[ 1 ], activity );
            } else {
               psEnc->state_Fxx[ 1 ].sCmn.VAD_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] = 0;
            }
            if( !prefillFlag ) {
               silk_stereo_encode_pred( psRangeEnc, psEnc->sStereo.predIx[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] );
               if( psEnc->state_Fxx[ 1 ].sCmn.VAD_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] == 0 ) {
                  silk_stereo_encode_mid_only( psRangeEnc, psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded ] );
               }
            }
         } else {
            silk_memcpy( psEnc->state_Fxx[ 0 ].sCmn.inputBuf, psEnc->sStereo.sMid, 2 * sizeof( fl_i16 ) );
            silk_memcpy( psEnc->sStereo.sMid, &psEnc->state_Fxx[ 0 ].sCmn.inputBuf[ psEnc->state_Fxx[ 0 ].sCmn.frame_length ], 2 * sizeof( fl_i16 ) );
         }
         silk_encode_do_VAD_Fxx( &psEnc->state_Fxx[ 0 ], activity );
         for( n = 0; n < encControl->nChannelsInternal; n++ ) {
            int maxBits, useCBR;
            maxBits = encControl->maxBits;
            if( tot_blocks == 2 && curr_block == 0 ) {
               maxBits = maxBits * 3 / 5;
            } else if( tot_blocks == 3 ) {
               if( curr_block == 0 ) {
                  maxBits = maxBits * 2 / 5;
               } else if( curr_block == 1 ) {
                  maxBits = maxBits * 3 / 4;
               }
            }
            useCBR = encControl->useCBR && curr_block == tot_blocks - 1;
            if( encControl->nChannelsInternal == 1 ) {
               channelRate_bps = TargetRate_bps;
            } else {
               channelRate_bps = MStargetRates_bps[ n ];
               if( n == 0 && MStargetRates_bps[ 1 ] > 0 ) {
                  useCBR = 0;
                  maxBits -= encControl->maxBits / ( tot_blocks * 2 );
               }
            }
            if( channelRate_bps > 0 ) {
               int condCoding;
               silk_control_SNR( &psEnc->state_Fxx[ n ].sCmn, channelRate_bps );
               if( psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded - n <= 0 ) {
                  condCoding = CODE_INDEPENDENTLY;
               } else if( n > 0 && psEnc->prev_decode_only_middle ) {
                  condCoding = CODE_INDEPENDENTLY_NO_LTP_SCALING;
               } else {
                  condCoding = CODE_CONDITIONALLY;
               }
               if( ( ret = silk_encode_frame_Fxx( &psEnc->state_Fxx[ n ], nBytesOut, psRangeEnc, condCoding, maxBits, useCBR ) ) != 0 ) {
                  silk_assert( 0 );
               }
            }
            psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
            psEnc->state_Fxx[ n ].sCmn.inputBufIx = 0;
            psEnc->state_Fxx[ n ].sCmn.nFramesEncoded++;
         }
         psEnc->prev_decode_only_middle = psEnc->sStereo.mid_only_flags[ psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded - 1 ];
         if( *nBytesOut > 0 && psEnc->state_Fxx[ 0 ].sCmn.nFramesEncoded == psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket) {
            flags = 0;
            for( n = 0; n < encControl->nChannelsInternal; n++ ) {
               for( i = 0; i < psEnc->state_Fxx[ n ].sCmn.nFramesPerPacket; i++ ) {
                  flags = silk_LSHIFT( flags, 1 );
                  flags |= psEnc->state_Fxx[ n ].sCmn.VAD_flags[ i ];
               }
               flags = silk_LSHIFT( flags, 1 );
               flags |= psEnc->state_Fxx[ n ].sCmn.LBRR_flag;
            }
            if( !prefillFlag ) {
               ec_enc_patch_initial_bits( psRangeEnc, flags, ( psEnc->state_Fxx[ 0 ].sCmn.nFramesPerPacket + 1 ) * encControl->nChannelsInternal );
            }
            if( psEnc->state_Fxx[ 0 ].sCmn.inDTX && ( encControl->nChannelsInternal == 1 || psEnc->state_Fxx[ 1 ].sCmn.inDTX ) ) {
               *nBytesOut = 0;
            }
            psEnc->nBitsExceeded += *nBytesOut * 8;
            psEnc->nBitsExceeded -= silk_DIV32_16( silk_MUL( encControl->bitRate, encControl->payloadSize_ms ), 1000 );
            psEnc->nBitsExceeded = silk_LIMIT( psEnc->nBitsExceeded, 0, 10000 );
            speech_act_thr_for_switch_Q8 = silk_SMLAWB( SILK_FIX_CONST( SPEECH_ACTIVITY_DTX_THRES, 8 ),
               SILK_FIX_CONST( ( 1 - SPEECH_ACTIVITY_DTX_THRES ) / MAX_BANDWIDTH_SWITCH_DELAY_MS, 16 + 8 ), psEnc->timeSinceSwitchAllowed_ms );
            if( psEnc->state_Fxx[ 0 ].sCmn.speech_activity_Q8 < speech_act_thr_for_switch_Q8 ) {
               psEnc->allowBandwidthSwitch = 1;
               psEnc->timeSinceSwitchAllowed_ms = 0;
            } else {
               psEnc->allowBandwidthSwitch = 0;
               psEnc->timeSinceSwitchAllowed_ms += encControl->payloadSize_ms;
            }
         }
         if( nSamplesIn == 0 ) {
            break;
         }
      } else {
         break;
      }
      curr_block++;
   }
   psEnc->nPrevChannelsInternal = encControl->nChannelsInternal;
   encControl->allowBandwidthSwitch = psEnc->allowBandwidthSwitch;
   encControl->inWBmodeWithoutVariableLP = psEnc->state_Fxx[ 0 ].sCmn.fs_kHz == 16 && psEnc->state_Fxx[ 0 ].sCmn.sLP.mode == 0;
   encControl->internalSampleRate = silk_SMULBB( psEnc->state_Fxx[ 0 ].sCmn.fs_kHz, 1000 );
   encControl->stereoWidth_Q14 = encControl->toMono ? 0 : psEnc->sStereo.smth_width_Q14;
   if( prefillFlag ) {
      encControl->payloadSize_ms = tmp_payloadSize_ms;
      encControl->complexity = tmp_complexity;
      for( n = 0; n < encControl->nChannelsInternal; n++ ) {
         psEnc->state_Fxx[ n ].sCmn.controlled_since_last_payload = 0;
         psEnc->state_Fxx[ n ].sCmn.prefillFlag = 0;
      }
   }
   encControl->signalType = psEnc->state_Fxx[0].sCmn.indices.signalType;
   encControl->offset = fl_Quantization_Offsets_Q10
                  [ psEnc->state_Fxx[0].sCmn.indices.signalType >> 1 ]
                  [ psEnc->state_Fxx[0].sCmn.indices.quantOffsetType ];
   RESTORE_STACK;
   return ret;
}
