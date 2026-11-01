/* ---- SILK encoder: interface used by the Opus encoder ---- */
struct fl_silk_enc { fl_silk_encoder s; };
static fl_silk_enc *fl_silk_create(void) { return (fl_silk_enc*)calloc(1, sizeof(fl_silk_enc)); }
static int fl_silk_init(fl_silk_enc *st, int channels, fl_silk_ctrl *status) {
   return silk_InitEncoder(&st->s, channels, 0, status);
}
static int fl_silk_encode(fl_silk_enc *st, fl_silk_ctrl *ctrl, const float *pcm, int nSamples, ec_ctx *enc,
      fl_i32 *nBytes, int prefillFlag, int activity) {
   return silk_Encode(&st->s, ctrl, pcm, nSamples, enc, nBytes, prefillFlag, activity);
}
static int fl_silk_hp_smth1(fl_silk_enc *st) { return st->s.state_Fxx[0].sCmn.variable_HP_smth1_Q15; }
