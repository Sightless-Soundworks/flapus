static int silk_encode_frame_FLP(silk_encoder_state_FLP *psEnc, opus_int32 *pnBytesOut, ec_enc *psRangeEnc,
      int condCoding, int maxBits, int useCBR);
static void silk_encode_do_VAD_FLP(silk_encoder_state_FLP *psEnc, int activity);
