/* ---- Tonality/music analysis (analysis.c + mlp.c, x87 rounding) ---- */
#include "tables_mlp.c"
#define NB_FRAMES 8
#define NB_TBANDS 18
#define ANALYSIS_BUF_SIZE 720
#define ANALYSIS_COUNT_MAX 10000
#define DETECT_SIZE 100
#define MAX_NEURONS 32
#define NB_TONAL_SKIP_BANDS 9

typedef struct {
   int Fs;
   /* reset from here */
   float angle[240], d_angle[240], d2_angle[240];
   float inmem[ANALYSIS_BUF_SIZE];
   int mem_fill;
   float prev_band_tonality[NB_TBANDS], prev_tonality;
   int prev_bandwidth;
   float E[NB_FRAMES][NB_TBANDS], logE[NB_FRAMES][NB_TBANDS];
   float lowE[NB_TBANDS], highE[NB_TBANDS], meanE[NB_TBANDS+1];
   float mem[32], cmean[8], std[9], Etracker, lowECount;
   int E_count, count, analysis_offset, write_pos, read_pos, read_subframe;
   float hp_ener_accum;
   int initialized;
   float rnn_state[MAX_NEURONS];
   float downmix_state[3];
   fl_analysis_info info[DETECT_SIZE];
} fl_tonality_state;

static const float fl_dct_table[128] = {
   0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f,
   0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f, 0.250000f,
   0.351851f, 0.338330f, 0.311806f, 0.273300f, 0.224292f, 0.166664f, 0.102631f, 0.034654f,
   -0.034654f, -0.102631f, -0.166664f, -0.224292f, -0.273300f, -0.311806f, -0.338330f, -0.351851f,
   0.346760f, 0.293969f, 0.196424f, 0.068975f, -0.068975f, -0.196424f, -0.293969f, -0.346760f,
   -0.346760f, -0.293969f, -0.196424f, -0.068975f, 0.068975f, 0.196424f, 0.293969f, 0.346760f,
   0.338330f, 0.224292f, 0.034654f, -0.166664f, -0.311806f, -0.351851f, -0.273300f, -0.102631f,
   0.102631f, 0.273300f, 0.351851f, 0.311806f, 0.166664f, -0.034654f, -0.224292f, -0.338330f,
   0.326641f, 0.135299f, -0.135299f, -0.326641f, -0.326641f, -0.135299f, 0.135299f, 0.326641f,
   0.326641f, 0.135299f, -0.135299f, -0.326641f, -0.326641f, -0.135299f, 0.135299f, 0.326641f,
   0.311806f, 0.034654f, -0.273300f, -0.338330f, -0.102631f, 0.224292f, 0.351851f, 0.166664f,
   -0.166664f, -0.351851f, -0.224292f, 0.102631f, 0.338330f, 0.273300f, -0.034654f, -0.311806f,
   0.293969f, -0.068975f, -0.346760f, -0.196424f, 0.196424f, 0.346760f, 0.068975f, -0.293969f,
   -0.293969f, 0.068975f, 0.346760f, 0.196424f, -0.196424f, -0.346760f, -0.068975f, 0.293969f,
   0.273300f, -0.166664f, -0.338330f, 0.034654f, 0.351851f, 0.102631f, -0.311806f, -0.224292f,
   0.224292f, 0.311806f, -0.102631f, -0.351851f, -0.034654f, 0.338330f, 0.166664f, -0.273300f};
static const float fl_analysis_window[240] = {
   0.000043f, 0.000171f, 0.000385f, 0.000685f, 0.001071f, 0.001541f, 0.002098f, 0.002739f,
   0.003466f, 0.004278f, 0.005174f, 0.006156f, 0.007222f, 0.008373f, 0.009607f, 0.010926f,
   0.012329f, 0.013815f, 0.015385f, 0.017037f, 0.018772f, 0.020590f, 0.022490f, 0.024472f,
   0.026535f, 0.028679f, 0.030904f, 0.033210f, 0.035595f, 0.038060f, 0.040604f, 0.043227f,
   0.045928f, 0.048707f, 0.051564f, 0.054497f, 0.057506f, 0.060591f, 0.063752f, 0.066987f,
   0.070297f, 0.073680f, 0.077136f, 0.080665f, 0.084265f, 0.087937f, 0.091679f, 0.095492f,
   0.099373f, 0.103323f, 0.107342f, 0.111427f, 0.115579f, 0.119797f, 0.124080f, 0.128428f,
   0.132839f, 0.137313f, 0.141849f, 0.146447f, 0.151105f, 0.155823f, 0.160600f, 0.165435f,
   0.170327f, 0.175276f, 0.180280f, 0.185340f, 0.190453f, 0.195619f, 0.200838f, 0.206107f,
   0.211427f, 0.216797f, 0.222215f, 0.227680f, 0.233193f, 0.238751f, 0.244353f, 0.250000f,
   0.255689f, 0.261421f, 0.267193f, 0.273005f, 0.278856f, 0.284744f, 0.290670f, 0.296632f,
   0.302628f, 0.308658f, 0.314721f, 0.320816f, 0.326941f, 0.333097f, 0.339280f, 0.345492f,
   0.351729f, 0.357992f, 0.364280f, 0.370590f, 0.376923f, 0.383277f, 0.389651f, 0.396044f,
   0.402455f, 0.408882f, 0.415325f, 0.421783f, 0.428254f, 0.434737f, 0.441231f, 0.447736f,
   0.454249f, 0.460770f, 0.467298f, 0.473832f, 0.480370f, 0.486912f, 0.493455f, 0.500000f,
   0.506545f, 0.513088f, 0.519630f, 0.526168f, 0.532702f, 0.539230f, 0.545751f, 0.552264f,
   0.558769f, 0.565263f, 0.571746f, 0.578217f, 0.584675f, 0.591118f, 0.597545f, 0.603956f,
   0.610349f, 0.616723f, 0.623077f, 0.629410f, 0.635720f, 0.642008f, 0.648271f, 0.654508f,
   0.660720f, 0.666903f, 0.673059f, 0.679184f, 0.685279f, 0.691342f, 0.697372f, 0.703368f,
   0.709330f, 0.715256f, 0.721144f, 0.726995f, 0.732807f, 0.738579f, 0.744311f, 0.750000f,
   0.755647f, 0.761249f, 0.766807f, 0.772320f, 0.777785f, 0.783203f, 0.788573f, 0.793893f,
   0.799162f, 0.804381f, 0.809547f, 0.814660f, 0.819720f, 0.824724f, 0.829673f, 0.834565f,
   0.839400f, 0.844177f, 0.848895f, 0.853553f, 0.858151f, 0.862687f, 0.867161f, 0.871572f,
   0.875920f, 0.880203f, 0.884421f, 0.888573f, 0.892658f, 0.896677f, 0.900627f, 0.904508f,
   0.908321f, 0.912063f, 0.915735f, 0.919335f, 0.922864f, 0.926320f, 0.929703f, 0.933013f,
   0.936248f, 0.939409f, 0.942494f, 0.945503f, 0.948436f, 0.951293f, 0.954072f, 0.956773f,
   0.959396f, 0.961940f, 0.964405f, 0.966790f, 0.969096f, 0.971321f, 0.973465f, 0.975528f,
   0.977510f, 0.979410f, 0.981228f, 0.982963f, 0.984615f, 0.986185f, 0.987671f, 0.989074f,
   0.990393f, 0.991627f, 0.992778f, 0.993844f, 0.994826f, 0.995722f, 0.996534f, 0.997261f,
   0.997902f, 0.998459f, 0.998929f, 0.999315f, 0.999615f, 0.999829f, 0.999957f, 1.000000f};
static const int fl_tbands[NB_TBANDS+1] = {4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 136, 160, 192, 240};
static const float fl_std_feature_bias[9] = {5.684947f, 3.475288f, 1.770634f, 1.599784f, 3.773215f, 2.163313f,
   1.260756f, 1.116868f, 1.918795f};

/* MLP (int8 weights, scale 1/128). */
static xf fl_tansig_approx(float x) {
   xf X2 = XF(x)*x, num, den;
   num = (XF(0.60863042f)*X2+96.39235687f)*X2+952.52801514f;
   den = (XF(11.88600922f)*X2+413.36801147f)*X2+952.72399902f;
   num = (num*x)/den;
   return 1.f < num ? XF(1.f) : (-1.f > num ? XF(-1.f) : num);
}
static void fl_gemm_accum(float *out, const fl_i8 *w, int rows, int cols, int col_stride, const float *x) {
   int i, j;
   for (i = 0; i < rows; i++)
      for (j = 0; j < cols; j++) out[i] = (float)(XF(w[j*col_stride+i])*x[j]+out[i]);
}
static void fl_mlp_dense(const fl_i8 *bias, const fl_i8 *weights, int M, int N, int sigmoid, float *output,
      const float *input) {
   int i;
   for (i = 0; i < N; i++) output[i] = bias[i];
   fl_gemm_accum(output, weights, N, M, N, input);
   for (i = 0; i < N; i++) output[i] *= 1.f/128;
   for (i = 0; i < N; i++)
      output[i] = sigmoid ? (float)(fl_tansig_approx(.5f*output[i])*.5f+.5f) : (float)fl_tansig_approx(output[i]);
}
static void fl_mlp_gru(float *state, const float *input) {
   const int M = 32, N = 24, stride = 3*24;
   const fl_i8 *bias = fl_mlp_layer1_bias, *iw = fl_mlp_layer1_weights, *rw = fl_mlp_layer1_recur_weights;
   int i;
   float tmp[MAX_NEURONS], z[MAX_NEURONS], r[MAX_NEURONS], h[MAX_NEURONS];
   for (i = 0; i < N; i++) z[i] = bias[i];
   fl_gemm_accum(z, iw, N, M, stride, input);
   fl_gemm_accum(z, rw, N, N, stride, state);
   for (i = 0; i < N; i++) z[i] = (float)(fl_tansig_approx(.5f*((1.f/128)*z[i]))*.5f+.5f);
   for (i = 0; i < N; i++) r[i] = bias[N+i];
   fl_gemm_accum(r, &iw[N], N, M, stride, input);
   fl_gemm_accum(r, &rw[N], N, N, stride, state);
   for (i = 0; i < N; i++) r[i] = (float)(fl_tansig_approx(.5f*((1.f/128)*r[i]))*.5f+.5f);
   for (i = 0; i < N; i++) h[i] = bias[2*N+i];
   for (i = 0; i < N; i++) tmp[i] = state[i]*r[i];
   fl_gemm_accum(h, &iw[2*N], N, M, stride, input);
   fl_gemm_accum(h, &rw[2*N], N, N, stride, tmp);
   for (i = 0; i < N; i++) {
      float zs = z[i]*state[i], omz = 1-z[i];
      h[i] = (float)(fl_tansig_approx((1.f/128)*h[i])*omz+zs);
   }
   for (i = 0; i < N; i++) state[i] = h[i];
}

static xf fl_fast_atan2f(float y, float x) {
   const float cA = 0.43157974f, cB = 0.67848403f, cC = 0.08595542f, cE = (float)3.141592653589793/2;
   xf x2 = XF(x)*x, y2 = XF(y)*y;
   if (x2+y2 < 1e-18f) return 0;
   if (x2 < y2) {
      xf den = (y2+cB*x2)*(y2+cC*x2);
      return ((XF(-x)*y)*(y2+cA*x2))/den+(y < 0 ? -cE : cE);
   } else {
      xf den = (x2+cB*y2)*(x2+cC*y2), xy = XF(x)*y;
      return ((xy*(x2+cA*y2))/den+(y < 0 ? -cE : cE))-(xy < 0 ? -cE : cE);
   }
}

static xf fl_resampler_down2_hp(float *S, float *out, const float *in, int inLen) {
   int k, len2 = inLen/2;
   xf hp_ener = 0;
   for (k = 0; k < len2; k++) {
      float in32 = in[2*k], in32b;
      xf Y, X, out32, out32_hp;
      Y = XF(in32)-S[0];
      X = XF(0.6074371f)*Y;
      out32 = S[0]+X;
      S[0] = (float)(in32+X);
      out32_hp = out32;
      in32b = in[2*k+1];
      Y = XF(in32b)-S[1];
      X = XF(0.15063f)*Y;
      out32 = out32+S[1];
      out32 = out32+X;
      S[1] = (float)(in32b+X);
      Y = XF(-in32b)-S[2];
      X = XF(0.15063f)*Y;
      out32_hp = out32_hp+S[2];
      out32_hp = out32_hp+X;
      S[2] = (float)(X-in32b);
      hp_ener += out32_hp*out32_hp;
      out[k] = (float)(out32*.5f);
   }
   return hp_ener;
}

/* downmix_float + down2_hp for 48 kHz input. */
static xf fl_downmix_and_resample(const float *x, float *y, float S[3], int subframe, int offset, int c1, int c2, int C) {
   float tmp[2*ANALYSIS_BUF_SIZE];
   int j;
   if (subframe == 0) return 0;
   subframe *= 2;
   offset *= 2;
   for (j = 0; j < subframe; j++) tmp[j] = x[(j+offset)*C+c1]*32768.f;
   if (c2 > -1)
      for (j = 0; j < subframe; j++) tmp[j] = tmp[j]+x[(j+offset)*C+c2]*32768.f;
   for (j = 0; j < subframe; j++) {
      if (tmp[j] < -65536.f) tmp[j] = -65536.f;
      if (tmp[j] > 65536.f) tmp[j] = 65536.f;
      if (tmp[j] != tmp[j]) tmp[j] = 0;
   }
   if (c2 > -1)
      for (j = 0; j < subframe; j++) tmp[j] = .5f*tmp[j];
   return fl_resampler_down2_hp(S, y, tmp, subframe)*XF(1.f/32768/32768);
}

static void fl_tonality_reset(fl_tonality_state *t) {
   memset(&t->angle, 0, sizeof(*t)-offsetof(fl_tonality_state, angle));
}

static void fl_tonality_analysis(fl_tonality_state *tonal, const float *x, int len, int offset, int c1, int c2,
      int C, int lsb_depth) {
   int i, b, N = 480, N2 = 240, remaining, bandwidth = 0, is_masked[NB_TBANDS+1];
   float *A = tonal->angle, *dA = tonal->d_angle, *d2A = tonal->d2_angle;
   float tonality[240], noisiness[240], tonality2[240], band_tonality[NB_TBANDS], logE[NB_TBANDS], BFCC[8];
   float features[25], midE[8], band_log2[NB_TBANDS+1], leakage_from[NB_TBANDS+1], leakage_to[NB_TBANDS+1];
   float layer_out[MAX_NEURONS], frame_probs[2], alpha, alphaE, alphaE2, hp_ener, spec_variability;
   float frame_noisiness_f = 0, frame_loudness = 0, frame_stationarity = 0, relativeE = 0, slope = 0;
   float frame_tonality = 0, max_frame_tonality = 0, noise_floor, one_m_alphaE2;
   fl_cpx in[480], out[480];
   fl_analysis_info *info;
   if (!tonal->initialized) {
      tonal->mem_fill = 240;
      tonal->initialized = 1;
   }
   alpha = (float)(1.0L/IMIN(10, 1+tonal->count));
   alphaE = (float)(1.0L/IMIN(25, 1+tonal->count));
   alphaE2 = (float)(1.0L/IMIN(100, 1+tonal->count));
   if (tonal->count <= 1) alphaE2 = 1;
   len /= 2;
   offset /= 2;
   hp_ener = (float)(tonal->hp_ener_accum+fl_downmix_and_resample(x, &tonal->inmem[tonal->mem_fill],
         tonal->downmix_state, IMIN(len, ANALYSIS_BUF_SIZE-tonal->mem_fill), offset, c1, c2, C));
   tonal->hp_ener_accum = hp_ener;
   if (tonal->mem_fill+len < ANALYSIS_BUF_SIZE) {
      tonal->mem_fill += len;
      return;
   }
   info = &tonal->info[tonal->write_pos++];
   if (tonal->write_pos >= DETECT_SIZE) tonal->write_pos -= DETECT_SIZE;
   {
      int is_silence = fl_e_maxabs(tonal->inmem, ANALYSIS_BUF_SIZE) <= XF(1)/(1<<lsb_depth);
      for (i = 0; i < N2; i++) {
         float w = fl_analysis_window[i];
         in[i].r = w*tonal->inmem[i];
         in[i].i = w*tonal->inmem[N2+i];
         in[N-i-1].r = w*tonal->inmem[N-i-1];
         in[N-i-1].i = w*tonal->inmem[N+N2-i-1];
      }
      memmove(tonal->inmem, tonal->inmem+ANALYSIS_BUF_SIZE-240, 240*sizeof(float));
      remaining = len-(ANALYSIS_BUF_SIZE-tonal->mem_fill);
      tonal->hp_ener_accum = (float)fl_downmix_and_resample(x, &tonal->inmem[240], tonal->downmix_state, remaining,
            offset+ANALYSIS_BUF_SIZE-tonal->mem_fill, c1, c2, C);
      tonal->mem_fill = 240+remaining;
      if (is_silence) {
         int prev_pos = tonal->write_pos-2;
         if (prev_pos < 0) prev_pos += DETECT_SIZE;
         memcpy(info, &tonal->info[prev_pos], sizeof(*info));
         return;
      }
   }
   fl_e_fft(&fl_fft_states[0], in, out);
   if (out[0].r != out[0].r) {
      info->valid = 0;
      return;
   }
   for (i = 1; i < N2; i++) {
      float X1r, X2r, X1i, X2i, angle_f, d_angle_f, d2_angle_f;
      xf angle, d_angle, angle2, d_angle2, d2_angle2, mod1, mod2, avg_mod, K = 62341.81640625f;
      X1r = out[i].r+out[N-i].r;
      X1i = out[i].i-out[N-i].i;
      X2r = out[i].i+out[N-i].i;
      X2i = out[N-i].r-out[i].r;
      angle = XF((float)(.5f/3.141592653589793))*fl_fast_atan2f(X1i, X1r);
      angle_f = (float)angle;
      d_angle = angle-A[i];
      d_angle_f = (float)d_angle;
      d2_angle_f = (float)(d_angle-dA[i]);
      angle2 = XF((float)(.5f/3.141592653589793))*fl_fast_atan2f(X2i, X2r);
      d_angle2 = angle2-angle_f;
      d2_angle2 = d_angle2-d_angle_f;
      mod1 = XF(d2_angle_f)-(int)lrintf(d2_angle_f);
      mod2 = d2_angle2-(int)lrintf((float)d2_angle2);
      noisiness[i] = (float)(fabsl(mod1)+fabsl(mod2));
      mod1 = mod1*mod1;
      mod1 = mod1*mod1;
      mod2 = mod2*mod2;
      mod2 = mod2*mod2;
      avg_mod = ((d2A[i]+mod1)+2*mod2)*.25f;
      tonality[i] = (float)(1.0L/(1+K*avg_mod)-.015f);
      tonality2[i] = (float)(1.0L/(1+K*mod2)-.015f);
      A[i] = (float)angle2;
      dA[i] = (float)d_angle2;
      d2A[i] = (float)mod2;
   }
   for (i = 2; i < N2-1; i++) {
      float tt = FL_MIN(tonality2[i], FL_MAX(tonality2[i-1], tonality2[i+1]));
      xf a = XF(tt)-.1f;
      tonality[i] = (float)((tonality[i] > a ? XF(tonality[i]) : a)*.9f);
   }
   info->activity = 0;
   if (!tonal->count)
      for (b = 0; b < NB_TBANDS; b++) {
         tonal->lowE[b] = 1e10;
         tonal->highE[b] = -1e10;
      }
   {
      xf E, X1r = XF(out[0].r)*2, X2r = XF(out[0].i)*2;
      E = X1r*X1r+X2r*X2r;
      for (i = 1; i < 4; i++)
         E += ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
      E = E*XF(1.f/32768/32768);
      band_log2[0] = (float)(XF((float)fl_m_log((double)(E+1e-10f)))*(.5f*1.442695f));
   }
   for (b = 0; b < NB_TBANDS; b++) {
      float E = 0, tE = 0, Eeps, L1 = 0, L2 = 0, arg;
      xf nE = 0, stationarity, bt, s, ft;
      for (i = fl_tbands[b]; i < fl_tbands[b+1]; i++) {
         xf binE = ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
         xf t = tonality[i];
         binE = binE*XF(1.f/32768/32768);
         E = (float)(E+binE);
         t = 0 > t ? 0 : t;
         tE = (float)(binE*t+tE);
         nE += (binE*2)*(XF(.5f)-noisiness[i]);
      }
      if (!(E < 1e9f) || E != E) {
         info->valid = 0;
         return;
      }
      tonal->E[tonal->E_count][b] = E;
      {
         xf e = XF(E)+1e-15f;
         Eeps = (float)e;
         frame_noisiness_f = (float)(frame_noisiness_f+nE/e);
      }
      arg = (float)(XF(E)+1e-10f);
      frame_loudness = (float)(frame_loudness+fl_m_sqrtf(arg));
      logE[b] = (float)fl_m_log((double)arg);
      band_log2[b+1] = (float)(XF(logE[b])*(.5f*1.442695f));
      tonal->logE[tonal->E_count][b] = logE[b];
      if (tonal->count == 0) tonal->highE[b] = tonal->lowE[b] = logE[b];
      {
         /* highE may stay in a register (unrounded) after the -0.01 update */
         xf Hx = tonal->highE[b], L = logE[b];
         if (tonal->highE[b] > XF(tonal->lowE[b])+7.5) {
            if (XF(tonal->highE[b])-L > L-tonal->lowE[b]) {
               Hx = XF(tonal->highE[b])-.01f;
               tonal->highE[b] = (float)Hx;
            } else tonal->lowE[b] = (float)(XF(tonal->lowE[b])+.01f);
         }
         if (L > Hx) {
            xf a = L-15;
            tonal->highE[b] = logE[b];
            tonal->lowE[b] = (float)(a > tonal->lowE[b] ? a : XF(tonal->lowE[b]));
            Hx = L;
         } else if (L < tonal->lowE[b]) {
            xf a = L+15;
            tonal->lowE[b] = logE[b];
            Hx = Hx > a ? a : Hx;
            tonal->highE[b] = (float)Hx;
         }
         relativeE = (float)((L-tonal->lowE[b])/((Hx-tonal->lowE[b])+1e-5f)+relativeE);
      }
      for (i = 0; i < NB_FRAMES-1; i++) {
         L1 = (float)(fl_m_sqrtf(tonal->E[i][b])+L1);
         L2 = L2+tonal->E[i][b];
      }
      L1 = (float)(fl_m_sqrtf(tonal->E[i][b])+L1);
      /* the last L2 term stays in a register */
      s = (float)fl_m_sqrt((double)((XF(L2)+tonal->E[i][b])*8.f+1e-15));
      stationarity = XF(L1)/s;
      stationarity = .99f < stationarity ? XF(.99f) : stationarity;
      stationarity = stationarity*stationarity;
      stationarity = stationarity*stationarity;
      frame_stationarity = (float)(frame_stationarity+stationarity);
      {
         xf a = XF(tE)/Eeps, c = stationarity*tonal->prev_band_tonality[b];
         bt = a > c ? a : c;
      }
      band_tonality[b] = (float)bt;
      ft = frame_tonality+bt;
      if (b >= NB_TBANDS-NB_TONAL_SKIP_BANDS) ft = ft-band_tonality[b-NB_TBANDS+NB_TONAL_SKIP_BANDS];
      frame_tonality = (float)ft;
      {
         xf m = (XF(.03f)*(b-NB_TBANDS)+1)*frame_tonality;
         max_frame_tonality = (float)(max_frame_tonality > m ? XF(max_frame_tonality) : m);
      }
      slope = (float)(bt*(b-8)+slope);
      tonal->prev_band_tonality[b] = (float)bt;
   }
   leakage_from[0] = band_log2[0];
   leakage_to[0] = band_log2[0]-2.5f;
   for (b = 1; b < NB_TBANDS+1; b++) {
      xf leak_slope = XF(fl_tbands[b]-fl_tbands[b-1])*2*.25f, a, c;
      a = leakage_from[b-1]+leak_slope;
      leakage_from[b] = (float)(a < band_log2[b] ? a : XF(band_log2[b]));
      a = leakage_to[b-1]-leak_slope;
      c = XF(band_log2[b])-2.5f;
      leakage_to[b] = (float)(a > c ? a : c);
   }
   for (b = NB_TBANDS-2; b >= 0; b--) {
      xf leak_slope = XF(fl_tbands[b+1]-fl_tbands[b])*2*.25f, a;
      a = leakage_from[b+1]+leak_slope;
      leakage_from[b] = (float)(a < leakage_from[b] ? a : XF(leakage_from[b]));
      a = leakage_to[b+1]-leak_slope;
      leakage_to[b] = (float)(a > leakage_to[b] ? a : XF(leakage_to[b]));
   }
   for (b = 0; b < NB_TBANDS+1; b++) {
      xf a = XF(leakage_to[b])-band_log2[b], c = XF(band_log2[b])-(XF(2.5f)+leakage_from[b]);
      xf boost = (0 > a ? 0 : a)+(0 > c ? 0 : c);
      info->leak_boost[b] = (unsigned char)IMIN(255, (int)floor((double)(boost*64.f+.5f)));
   }
   for (; b < LEAK_BANDS; b++) info->leak_boost[b] = 0;
   {
      xf sv = 0;
      for (i = 0; i < NB_FRAMES; i++) {
         int j;
         xf mindist = 1e15f;
         for (j = 0; j < NB_FRAMES; j++) {
            int k;
            xf dist = 0;
            for (k = 0; k < NB_TBANDS; k++) {
               xf t = XF(tonal->logE[i][k])-tonal->logE[j][k];
               dist += t*t;
            }
            if (j != i) mindist = mindist < dist ? mindist : dist;
         }
         sv += mindist;
      }
      spec_variability = (float)fl_m_sqrtf((float)((sv*.125f)/18.f));
   }
   {
      xf nf = XF(5.7e-4f)/(1<<IMAX(0, lsb_depth-8)), maxE = 0, below = 0, above = 0, bmask = 0;
      noise_floor = (float)(nf*nf);
      one_m_alphaE2 = 1-alphaE2;
      for (b = 0; b < NB_TBANDS; b++) {
         int band_start = fl_tbands[b], band_end = fl_tbands[b+1];
         xf E = 0, M, Em;
         for (i = band_start; i < band_end; i++)
            E += ((XF(out[i].r)*out[i].r+XF(out[N-i].r)*out[N-i].r)+XF(out[i].i)*out[i].i)+XF(out[N-i].i)*out[N-i].i;
         E = E*XF(1.f/32768/32768);
         maxE = maxE > E ? maxE : E;
         if (band_start < 64) below += E;
         else above += E;
         M = XF(one_m_alphaE2)*tonal->meanE[b];
         M = M > E ? M : E;
         tonal->meanE[b] = (float)M;
         Em = E > M ? E : M;
         if (E*1e9f > maxE && (Em > (XF(noise_floor)*3)*(band_end-band_start) || E > XF(noise_floor)*(band_end-band_start)))
            bandwidth = b+1;
         is_masked[b] = E < (tonal->prev_bandwidth >= b+1 ? XF(.01f) : XF(.05f))*bmask;
         bmask = XF(.05f)*bmask;
         bmask = bmask > E ? bmask : E;
      }
      {
         xf E = XF(hp_ener)*(1.f/(60*60)), M, Em, nr = tonal->prev_bandwidth == 20 ? 10.f : 30.f;
         above += E;
         M = XF(one_m_alphaE2)*tonal->meanE[b];
         M = M > E ? M : E;
         tonal->meanE[b] = (float)M;
         Em = E > M ? E : M;
         if (Em > ((XF(3)*nr)*noise_floor)*160 || E > (nr*noise_floor)*160) bandwidth = 20;
         is_masked[b] = E < (tonal->prev_bandwidth == 20 ? XF(.01f) : XF(.05f))*bmask;
      }
      if (above > below) info->max_pitch_ratio = (float)(below/above);
      else info->max_pitch_ratio = 1;
   }
   if (bandwidth == 20 && is_masked[NB_TBANDS]) bandwidth -= 2;
   else if (bandwidth > 0 && bandwidth <= NB_TBANDS && is_masked[bandwidth-1]) bandwidth--;
   if (tonal->count <= 2) bandwidth = 20;
   {
      xf fl = XF((float)fl_m_log10((double)frame_loudness))*20, et = XF(tonal->Etracker)-.003f, lc;
      et = et > fl ? et : fl;
      tonal->Etracker = (float)et;
      lc = (1-XF(alphaE))*tonal->lowECount;
      if (fl < et-30) lc = lc+alphaE;
      tonal->lowECount = (float)lc;
   }
   for (i = 0; i < 8; i++) {
      xf sum = 0;
      for (b = 0; b < 16; b++) sum += XF(fl_dct_table[i*16+b])*logE[b];
      BFCC[i] = (float)sum;
   }
   for (i = 0; i < 8; i++) {
      xf sum = 0;
      for (b = 0; b < 16; b++) sum += (XF(fl_dct_table[i*16+b])*.5f)*(XF(tonal->highE[b])+tonal->lowE[b]);
      midE[i] = (float)sum;
   }
   {
      float fs_f, act_f, fn_f, ft_f, sl_f;
      xf fs = XF(frame_stationarity)/NB_TBANDS, re = XF(relativeE)/NB_TBANDS, fn, ft;
      if (tonal->count < 10) re = .5f;
      fn = XF(frame_noisiness_f)/NB_TBANDS;
      fn_f = (float)fn;
      act_f = (float)(fn+(1-fn)*re);
      info->activity = act_f;
      ft = XF(max_frame_tonality)/(NB_TBANDS-NB_TONAL_SKIP_BANDS);
      {
         xf p = XF(.8f)*tonal->prev_tonality;
         ft = ft > p ? ft : p;
      }
      ft_f = (float)ft;
      tonal->prev_tonality = ft_f;
      sl_f = slope*(1.f/64);
      info->tonality_slope = sl_f;
      tonal->E_count = (tonal->E_count+1)%NB_FRAMES;
      tonal->count = IMIN(tonal->count+1, ANALYSIS_COUNT_MAX);
      info->tonality = ft_f;
      fs_f = (float)fs;
      for (i = 0; i < 4; i++)
         features[i] = (float)((((XF(-0.12299f)*(XF(BFCC[i])+tonal->mem[i+24]))+XF(0.49195f)*(XF(tonal->mem[i])+tonal->mem[i+16]))
               +XF(0.69693f)*tonal->mem[i+8])-XF(1.4349f)*tonal->cmean[i]);
      for (i = 0; i < 4; i++) tonal->cmean[i] = (float)((1-XF(alpha))*tonal->cmean[i]+XF(alpha)*BFCC[i]);
      for (i = 0; i < 4; i++)
         features[4+i] = (float)(XF(0.63246f)*(XF(BFCC[i])-tonal->mem[i+24])+XF(0.31623f)*(XF(tonal->mem[i])-tonal->mem[i+16]));
      for (i = 0; i < 3; i++)
         features[8+i] = (float)((XF(0.53452f)*(XF(BFCC[i])+tonal->mem[i+24])-XF(0.26726f)*(XF(tonal->mem[i])+tonal->mem[i+16]))
               -XF(0.53452f)*tonal->mem[i+8]);
      if (tonal->count > 5)
         for (i = 0; i < 9; i++)
            tonal->std[i] = (float)((1-XF(alpha))*tonal->std[i]+(XF(alpha)*features[i])*features[i]);
      for (i = 0; i < 4; i++) features[i] = BFCC[i]-midE[i];
      for (i = 0; i < 8; i++) {
         tonal->mem[i+24] = tonal->mem[i+16];
         tonal->mem[i+16] = tonal->mem[i+8];
         tonal->mem[i+8] = tonal->mem[i];
         tonal->mem[i] = BFCC[i];
      }
      for (i = 0; i < 9; i++) features[11+i] = (float)(fl_m_sqrtf(tonal->std[i])-fl_std_feature_bias[i]);
      features[18] = spec_variability-0.78f;
      features[20] = ft_f-0.154723f;
      features[21] = act_f-0.724643f;
      features[22] = fs_f-0.743717f;
      features[23] = sl_f+0.069216f;
      features[24] = tonal->lowECount-0.067930f;
      fl_mlp_dense(fl_mlp_layer0_bias, fl_mlp_layer0_weights, 25, 32, 0, layer_out, features);
      fl_mlp_gru(tonal->rnn_state, layer_out);
      fl_mlp_dense(fl_mlp_layer2_bias, fl_mlp_layer2_weights, 24, 2, 1, frame_probs, tonal->rnn_state);
      info->activity_probability = frame_probs[1];
      info->music_prob = frame_probs[0];
      info->bandwidth = bandwidth;
      tonal->prev_bandwidth = bandwidth;
      info->noisiness = fn_f;
      info->valid = 1;
   }
}

static void fl_tonality_get_info(fl_tonality_state *tonal, fl_analysis_info *info_out, int len) {
   int pos, curr_lookahead, tonality_count, i, pos0, mpos, vpos, bandwidth_span;
   xf tonality_max, tonality_avg, prob_avg, prob_count, prob_min, prob_max, mp;
   float vad_prob;
   pos = tonal->read_pos;
   curr_lookahead = tonal->write_pos-tonal->read_pos;
   if (curr_lookahead < 0) curr_lookahead += DETECT_SIZE;
   tonal->read_subframe += len/(tonal->Fs/400);
   while (tonal->read_subframe >= 8) {
      tonal->read_subframe -= 8;
      tonal->read_pos++;
   }
   if (tonal->read_pos >= DETECT_SIZE) tonal->read_pos -= DETECT_SIZE;
   if (len > tonal->Fs/50 && pos != tonal->write_pos) {
      pos++;
      if (pos == DETECT_SIZE) pos = 0;
   }
   if (pos == tonal->write_pos) pos--;
   if (pos < 0) pos = DETECT_SIZE-1;
   pos0 = pos;
   *info_out = tonal->info[pos];
   if (!info_out->valid) return;
   tonality_max = tonality_avg = info_out->tonality;
   tonality_count = 1;
   bandwidth_span = 6;
   for (i = 0; i < 3; i++) {
      pos++;
      if (pos == DETECT_SIZE) pos = 0;
      if (pos == tonal->write_pos) break;
      tonality_max = tonality_max > tonal->info[pos].tonality ? tonality_max : XF(tonal->info[pos].tonality);
      tonality_avg += tonal->info[pos].tonality;
      tonality_count++;
      info_out->bandwidth = IMAX(info_out->bandwidth, tonal->info[pos].bandwidth);
      bandwidth_span--;
   }
   pos = pos0;
   for (i = 0; i < bandwidth_span; i++) {
      pos--;
      if (pos < 0) pos = DETECT_SIZE-1;
      if (pos == tonal->write_pos) break;
      info_out->bandwidth = IMAX(info_out->bandwidth, tonal->info[pos].bandwidth);
   }
   {
      xf a = tonality_avg/tonality_count, c = tonality_max-.2f;
      info_out->tonality = (float)(a > c ? a : c);
   }
   mpos = vpos = pos0;
   if (curr_lookahead > 15) {
      mpos += 5;
      if (mpos >= DETECT_SIZE) mpos -= DETECT_SIZE;
      vpos += 1;
      if (vpos >= DETECT_SIZE) vpos -= DETECT_SIZE;
   }
   prob_min = 1;
   prob_max = 0;
   vad_prob = tonal->info[vpos].activity_probability;
   prob_count = FL_MAX(.1f, vad_prob);
   prob_avg = prob_count*tonal->info[mpos].music_prob;
   while (1) {
      float pos_vad;
      xf a;
      mpos++;
      if (mpos == DETECT_SIZE) mpos = 0;
      if (mpos == tonal->write_pos) break;
      vpos++;
      if (vpos == DETECT_SIZE) vpos = 0;
      if (vpos == tonal->write_pos) break;
      pos_vad = tonal->info[vpos].activity_probability;
      a = (prob_avg-(XF(vad_prob)-pos_vad)*10.f)/prob_count;
      prob_min = a < prob_min ? a : prob_min;
      a = (prob_avg+(XF(vad_prob)-pos_vad)*10.f)/prob_count;
      prob_max = a > prob_max ? a : prob_max;
      prob_count += FL_MAX(.1f, pos_vad);
      prob_avg += XF(FL_MAX(.1f, pos_vad))*tonal->info[mpos].music_prob;
   }
   mp = prob_avg/prob_count;
   info_out->music_prob = (float)mp;
   prob_min = mp < prob_min ? mp : prob_min;
   prob_max = mp > prob_max ? mp : prob_max;
   prob_min = prob_min > 0 ? prob_min : 0;
   prob_max = prob_max < 1 ? prob_max : 1;
   if (curr_lookahead < 10) {
      xf pmin = prob_min, pmax = prob_max, k;
      pos = pos0;
      for (i = 0; i < IMIN(tonal->count-1, 15); i++) {
         pos--;
         if (pos < 0) pos = DETECT_SIZE-1;
         pmin = pmin < tonal->info[pos].music_prob ? pmin : XF(tonal->info[pos].music_prob);
         pmax = pmax > tonal->info[pos].music_prob ? pmax : XF(tonal->info[pos].music_prob);
      }
      pmin = pmin-XF(.1f)*vad_prob;
      pmin = 0 > pmin ? 0 : pmin;
      pmax = pmax+XF(.1f)*vad_prob;
      pmax = 1 < pmax ? 1 : pmax;
      k = 1-XF(.1f)*curr_lookahead;
      prob_min += k*(pmin-prob_min);
      prob_max += k*(pmax-prob_max);
   }
   info_out->music_prob_min = (float)prob_min;
   info_out->music_prob_max = (float)prob_max;
}

static void fl_run_analysis(fl_tonality_state *analysis, const float *pcm, int analysis_frame_size, int frame_size,
      int c1, int c2, int C, int lsb_depth, fl_analysis_info *analysis_info) {
   int offset, pcm_len;
   const int Fs = 48000;
   analysis_frame_size -= analysis_frame_size&1;
   if (pcm != NULL) {
      analysis_frame_size = IMIN((DETECT_SIZE-5)*Fs/50, analysis_frame_size);
      pcm_len = analysis_frame_size-analysis->analysis_offset;
      offset = analysis->analysis_offset;
      while (pcm_len > 0) {
         fl_tonality_analysis(analysis, pcm, IMIN(Fs/50, pcm_len), offset, c1, c2, C, lsb_depth);
         offset += Fs/50;
         pcm_len -= Fs/50;
      }
      analysis->analysis_offset = analysis_frame_size;
      analysis->analysis_offset -= frame_size;
   }
   fl_tonality_get_info(analysis, analysis_info, frame_size);
}
