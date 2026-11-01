/* ---- Opus encoder (opus_encoder.c) and single-stream multistream wrapper ---- */
#define FL_MODE_SILK_ONLY 1000
#define FL_MODE_HYBRID 1001
#define FL_MODE_CELT_ONLY 1002
#define FL_BW_NB 1101
#define FL_BW_MB 1102
#define FL_BW_WB 1103
#define FL_BW_SWB 1104
#define FL_BW_FB 1105
#define FL_AUTO -1000
#define FL_BITRATE_MAX -1
#define FL_SIGNAL_VOICE 3001
#define FL_SIGNAL_MUSIC 3002
#define FL_MAX_ENCODER_BUFFER 480
#define FL_VAD_NO_DECISION -1

/* SILK encoder control (silk_EncControlStruct subset used by the Opus layer). */
typedef struct {
   int nChannelsAPI, nChannelsInternal, API_sampleRate, maxInternalSampleRate, minInternalSampleRate;
   int desiredInternalSampleRate, payloadSize_ms, bitRate, packetLossPercentage, complexity, useInBandFEC;
   int LBRR_coded, useDTX, useCBR, maxBits, toMono, opusCanSwitch, reducedDependency, internalSampleRate;
   int allowBandwidthSwitch, inWBmodeWithoutVariableLP, stereoWidth_Q14, switchReady, signalType, offset;
} fl_silk_ctrl;

typedef struct fl_silk_enc fl_silk_enc;
static int fl_silk_init(fl_silk_enc *st, int channels, fl_silk_ctrl *status);
static int fl_silk_encode(fl_silk_enc *st, fl_silk_ctrl *ctrl, const float *pcm, int nSamples, ec_ctx *enc,
      fl_i32 *nBytes, int prefillFlag, int activity);
static int fl_silk_hp_smth1(fl_silk_enc *st);
static fl_silk_enc *fl_silk_create(void);

typedef struct { float XX, XY, YY, smoothed_width, max_follower; } fl_width_state;

typedef struct {
   fl_silk_enc *silk;
   fl_celt_enc celt;
   fl_silk_ctrl silk_mode;
   int channels, delay_compensation, force_channels, signal_type, user_bandwidth, max_bandwidth;
   int user_forced_mode, voice_ratio, use_vbr, vbr_constraint, variable_duration;
   fl_i32 bitrate_bps, user_bitrate_bps;
   int lsb_depth, encoder_buffer, use_dtx;
   int lfe;                      /* LFE stream of a surround encoder */
   const float *energy_masking;  /* surround masking (21 bands per channel), NULL if none */
   fl_tonality_state analysis;
   /* reset from here */
   int stream_channels;
   fl_i16 hybrid_stereo_width_Q14;
   fl_i32 variable_HP_smth2_Q15;
   float prev_HB_gain, hp_mem[4];
   int mode, prev_mode, prev_channels, prev_framesize, bandwidth, auto_bandwidth, silk_bw_switch, first;
   fl_width_state width_mem;
   int detected_bandwidth, nb_no_activity_ms_Q1;
   float peak_signal_energy;
   int nonfinal_frame;
   fl_u32 rangeFinal;
   float delay_buffer[FL_MAX_ENCODER_BUFFER*2];
} fl_opus_enc;

static void fl_opus_enc_init(fl_opus_enc *st, int channels) {
   fl_silk_enc *silk = st->silk;
   memset(st, 0, sizeof(*st));
   st->silk = silk;
   st->stream_channels = st->channels = channels;
   fl_silk_init(st->silk, channels, &st->silk_mode);
   st->silk_mode.nChannelsAPI = channels;
   st->silk_mode.nChannelsInternal = channels;
   st->silk_mode.API_sampleRate = 48000;
   st->silk_mode.maxInternalSampleRate = 16000;
   st->silk_mode.minInternalSampleRate = 8000;
   st->silk_mode.desiredInternalSampleRate = 16000;
   st->silk_mode.payloadSize_ms = 20;
   st->silk_mode.bitRate = 25000;
   st->silk_mode.packetLossPercentage = 0;
   st->silk_mode.complexity = 9;
   st->silk_mode.useInBandFEC = 0;
   st->silk_mode.useDTX = 0;
   st->silk_mode.useCBR = 0;
   st->silk_mode.reducedDependency = 0;
   fl_celt_enc_init(&st->celt, channels);
   st->celt.complexity = 9;
   st->use_vbr = 1;
   st->vbr_constraint = 1;
   st->user_bitrate_bps = FL_AUTO;
   st->bitrate_bps = 3000+48000*channels;
   st->signal_type = FL_AUTO;
   st->user_bandwidth = FL_AUTO;
   st->max_bandwidth = FL_BW_FB;
   st->force_channels = FL_AUTO;
   st->user_forced_mode = FL_AUTO;
   st->voice_ratio = -1;
   st->encoder_buffer = 480;
   st->lsb_depth = 24;
   st->variable_duration = 5000;
   st->delay_compensation = 192;
   st->hybrid_stereo_width_Q14 = 1<<14;
   st->prev_HB_gain = 1;
   st->variable_HP_smth2_Q15 = silk_LSHIFT(silk_lin2log(60), 8);
   st->first = 1;
   st->mode = FL_MODE_HYBRID;
   st->bandwidth = FL_BW_FB;
   st->analysis.Fs = 48000;
   fl_tonality_reset(&st->analysis);
}

static unsigned char fl_gen_toc(int mode, int framerate, int bandwidth, int channels) {
   int period = 0;
   unsigned char toc;
   while (framerate < 400) {
      framerate <<= 1;
      period++;
   }
   if (mode == FL_MODE_SILK_ONLY) {
      toc = (bandwidth-FL_BW_NB)<<5;
      toc |= (period-2)<<3;
   } else if (mode == FL_MODE_CELT_ONLY) {
      int tmp = bandwidth-FL_BW_MB;
      if (tmp < 0) tmp = 0;
      toc = 0x80;
      toc |= tmp<<5;
      toc |= period<<3;
   } else {
      toc = 0x60;
      toc |= (bandwidth-FL_BW_SWB)<<4;
      toc |= (period-2)<<3;
   }
   toc |= (channels == 2)<<2;
   return toc;
}

static void fl_dc_reject(const float *in, float *out, float *hp_mem, int len, int channels) {
   int i;
   xf coef = XF(18.900001525878906f)/48000, coef2 = 1-coef; /* 6.3f*3 folded in float */
   if (channels == 2) {
      xf m0 = hp_mem[0], m2 = hp_mem[2];
      for (i = 0; i < len; i++) {
         float x0 = in[2*i], x1 = in[2*i+1];
         out[2*i] = (float)(x0-m0);
         out[2*i+1] = (float)(x1-m2);
         m0 = (coef*x0+1e-30f)+coef2*m0;
         m2 = (coef*x1+1e-30f)+coef2*m2;
      }
      hp_mem[0] = (float)m0;
      hp_mem[2] = (float)m2;
   } else {
      xf m0 = hp_mem[0];
      for (i = 0; i < len; i++) {
         float x = in[i];
         out[i] = (float)(x-m0);
         m0 = (coef*x+1e-30f)+coef2*m0;
      }
      hp_mem[0] = (float)m0;
   }
}

static void fl_stereo_fade(float *buf, int g1_q14, int g2_q14, int frame_size, int channels) {
   int i;
   xf g1 = 1-XF(1.f/16384)*g1_q14, g2 = 1-XF(1.f/16384)*g2_q14;
   for (i = 0; i < CELT_OVERLAP; i++) {
      xf w = XF(fl_window120[i])*fl_window120[i], g, diff;
      g = w*g2+(1-w)*g1;
      diff = (XF(buf[i*channels])-buf[i*channels+1])*.5f;
      diff = g*diff;
      buf[i*channels] = (float)(buf[i*channels]-diff);
      buf[i*channels+1] = (float)(buf[i*channels+1]+diff);
   }
   for (; i < frame_size; i++) {
      xf diff = (XF(buf[i*channels])-buf[i*channels+1])*.5f;
      diff = diff*g2;
      buf[i*channels] = (float)(buf[i*channels]-diff);
      buf[i*channels+1] = (float)(buf[i*channels+1]+diff);
   }
}

static void fl_gain_fade(const float *in, float *out, float g1, float g2, int overlap, int frame_size, int channels) {
   int i, c;
   for (i = 0; i < overlap; i++) {
      xf w = XF(fl_window120[i])*fl_window120[i], g = w*g2+(1-w)*g1;
      for (c = 0; c < channels; c++) out[i*channels+c] = (float)(g*in[i*channels+c]);
   }
   for (c = 0; c < channels; c++)
      for (i = overlap; i < frame_size; i++) out[i*channels+c] = g2*in[i*channels+c];
}

static float fl_compute_stereo_width(const float *pcm, int frame_size, fl_width_state *mem) {
   int i, frame_rate = 48000/frame_size;
   float short_alpha, xx_f, yy_f, xy = 0;
   xf xx = 0, yy = 0, XX, XY, YY;
   short_alpha = (float)(XF(25.f)/IMAX(50, frame_rate));
   for (i = 0; i < frame_size-3; i += 4) {
      xf pxx, pxy, pyy;
      float x = pcm[2*i], y = pcm[2*i+1];
      pxx = XF(x)*x;
      pxy = XF(x)*y;
      pyy = XF(y)*y;
      x = pcm[2*i+2]; y = pcm[2*i+3];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      x = pcm[2*i+4]; y = pcm[2*i+5];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      x = pcm[2*i+6]; y = pcm[2*i+7];
      pxx += XF(x)*x; pxy += XF(x)*y; pyy += XF(y)*y;
      xx += pxx;
      xy = (float)(xy+pxy);
      yy += pyy;
   }
   xx_f = (float)xx;
   yy_f = (float)yy;
   if (!(xx < 1e9f) || xx_f != xx_f || !(yy < 1e9f) || yy_f != yy_f) xy = xx_f = yy_f = 0;
   XX = (XF(xx_f)-mem->XX)*short_alpha+mem->XX;
   XY = (1-XF(short_alpha))*mem->XY+XF(xy)*short_alpha;
   YY = (XF(yy_f)-mem->YY)*short_alpha+mem->YY;
   XX = 0 > XX ? 0 : XX;
   XY = 0 > XY ? 0 : XY;
   YY = 0 > YY ? 0 : YY;
   mem->XX = (float)XX;
   mem->XY = (float)XY;
   mem->YY = (float)YY;
   if ((XX > YY ? XX : YY) > 8e-4f) {
      float sqrt_xx, sqrt_yy, qrrt_xx, ldiff;
      xf qrrt_yy, xym, corr, s, width, sw, mf;
      sqrt_xx = (float)fl_m_sqrtf(mem->XX);
      sqrt_yy = (float)fl_m_sqrtf(mem->YY);
      qrrt_xx = (float)fl_m_sqrtf(sqrt_xx);
      qrrt_yy = fl_m_sqrtf(sqrt_yy);
      xym = XF(sqrt_xx)*sqrt_yy;
      xym = mem->XY < xym ? XF(mem->XY) : xym;
      mem->XY = (float)xym;
      corr = xym/(XF(1e-15f)+XF(sqrt_xx)*sqrt_yy);
      ldiff = (float)(fabsl(XF(qrrt_xx)-qrrt_yy)/((XF(1e-15f)+qrrt_xx)+qrrt_yy));
      s = fl_m_sqrtf((float)(1-corr*corr));
      s = 1 < s ? 1 : s;
      width = s*ldiff;
      sw = mem->smoothed_width+(width-mem->smoothed_width)/frame_rate;
      mem->smoothed_width = (float)sw;
      mf = mem->max_follower-XF(.02f)/frame_rate;
      mem->max_follower = (float)(mf > sw ? mf : sw);
   }
   {
      xf r = XF(20.f)*mem->max_follower;
      return (float)(1 < r ? 1 : r);
   }
}

static int fl_compute_silk_rate_for_hybrid(int rate, int bandwidth, int frame20ms, int vbr, int fec, int channels) {
   static const int rate_table[][5] = {
      {0, 0, 0, 0, 0}, {12000, 10000, 10000, 11000, 11000}, {16000, 13500, 13500, 15000, 15000},
      {20000, 16000, 16000, 18000, 18000}, {24000, 18000, 18000, 21000, 21000},
      {32000, 22000, 22000, 28000, 28000}, {64000, 38000, 38000, 50000, 50000}};
   int entry, i, N = 7, silk_rate;
   rate /= channels;
   entry = 1+frame20ms+2*fec;
   for (i = 1; i < N; i++)
      if (rate_table[i][0] > rate) break;
   if (i == N) {
      silk_rate = rate_table[i-1][entry];
      silk_rate += (rate-rate_table[i-1][0])/2;
   } else {
      fl_i32 lo = rate_table[i-1][entry], hi = rate_table[i][entry], x0 = rate_table[i-1][0], x1 = rate_table[i][0];
      silk_rate = (lo*(x1-rate)+hi*(rate-x0))/(x1-x0);
   }
   if (!vbr) silk_rate += 100;
   if (bandwidth == FL_BW_SWB) silk_rate += 300;
   silk_rate *= channels;
   if (channels == 2 && rate >= 12000) silk_rate -= 1000;
   return silk_rate;
}

static fl_i32 fl_compute_equiv_rate(fl_i32 bitrate, int channels, int frame_rate, int vbr, int mode, int complexity, int loss) {
   fl_i32 equiv = bitrate;
   if (frame_rate > 50) equiv -= (40*channels+20)*(frame_rate-50);
   if (!vbr) equiv -= equiv/12;
   equiv = equiv*(90+complexity)/100;
   if (mode == FL_MODE_SILK_ONLY || mode == FL_MODE_HYBRID) {
      if (complexity < 2) equiv = equiv*4/5;
      equiv -= equiv*loss/(6*loss+10);
   } else if (mode == FL_MODE_CELT_ONLY) {
      if (complexity < 5) equiv = equiv*9/10;
   } else {
      equiv -= equiv*loss/(12*loss+20);
   }
   return equiv;
}

static xf fl_frame_energy(const float *pcm, int len) { return XF(fl_inner_prod(pcm, pcm, len))/len; }

static int fl_compute_redundancy_bytes(fl_i32 max_data_bytes, fl_i32 bitrate_bps, int frame_rate, int channels) {
   int redundancy_bytes_cap, redundancy_bytes, base_bits = 40*channels+20;
   fl_i32 redundancy_rate, available_bits;
   redundancy_rate = bitrate_bps+base_bits*(200-frame_rate);
   redundancy_rate = 3*redundancy_rate/2;
   redundancy_bytes = redundancy_rate/1600;
   available_bits = max_data_bytes*8-2*base_bits;
   redundancy_bytes_cap = (available_bits*240/(240+48000/frame_rate)+base_bits)/8;
   redundancy_bytes = IMIN(redundancy_bytes, redundancy_bytes_cap);
   if (redundancy_bytes > 4+8*channels) redundancy_bytes = IMIN(257, redundancy_bytes);
   else redundancy_bytes = 0;
   return redundancy_bytes;
}

/* Pads a single-frame packet to new_len bytes (opus_packet_pad for one frame). */
static int fl_packet_pad(unsigned char *data, int len, int new_len) {
   int pad, i;
   unsigned char frame[1276];
   if (len == new_len) return 0;
   if (len > new_len || (data[0]&3) != 0) return -1;
   memcpy(frame, data+1, len-1);
   pad = new_len-len-1;
   data[0] |= 3;
   data[1] = (unsigned char)(1|(pad > 0 ? 0x40 : 0));
   i = 2;
   if (pad > 0) {
      int nb255 = (pad-1)/255;
      memset(data+i, 255, nb255);
      i += nb255;
      data[i++] = (unsigned char)(pad-1-255*nb255);
   }
   memcpy(data+i, frame, len-1);
   memset(data+i+len-1, 0, new_len-(i+len-1));
   return 0;
}

static int fl_encode_size(int size, unsigned char *data) {
   if (size < 252) {
      data[0] = (unsigned char)size;
      return 1;
   }
   data[0] = (unsigned char)(252+(size&3));
   data[1] = (unsigned char)((size-data[0])>>2);
   return 2;
}

/* Writes frames as one packet; self_delimited adds the last frame size (multistream),
   pad fills maxlen (CBR). Returns the length or <0. */
static fl_i32 fl_repacketize(unsigned char toc, unsigned char *const *frames, const int *lens, int count,
      unsigned char *data, fl_i32 maxlen, int self_delimited, int pad) {
   fl_i32 total = self_delimited ? 1+(lens[count-1] >= 252) : 0;
   unsigned char *ptr = data;
   int k;
   toc &= 0xFC;
   if (count == 1) {
      total += lens[0]+1;
      if (total > maxlen) return -2;
      *ptr++ = toc;
   } else if (count == 2) {
      if (lens[1] == lens[0]) {
         total += 2*lens[0]+1;
         if (total > maxlen) return -2;
         *ptr++ = toc|1;
      } else {
         total += lens[0]+lens[1]+2+(lens[0] >= 252);
         if (total > maxlen) return -2;
         *ptr++ = toc|2;
         ptr += fl_encode_size(lens[0], ptr);
      }
   }
   if (count > 2 || (pad && total < maxlen)) {
      int vbr = 0;
      fl_i32 pad_amount;
      ptr = data;
      total = self_delimited ? 1+(lens[count-1] >= 252) : 0;
      for (k = 1; k < count; k++)
         if (lens[k] != lens[0]) vbr = 1;
      if (vbr) {
         total += 2;
         for (k = 0; k < count-1; k++) total += 1+(lens[k] >= 252)+lens[k];
         total += lens[count-1];
         if (total > maxlen) return -2;
         *ptr++ = toc|3;
         *ptr++ = (unsigned char)(count|0x80);
      } else {
         total += count*lens[0]+2;
         if (total > maxlen) return -2;
         *ptr++ = toc|3;
         *ptr++ = (unsigned char)count;
      }
      pad_amount = pad ? maxlen-total : 0;
      if (pad_amount != 0) {
         fl_i32 nb_255s = (pad_amount-1)/255;
         data[1] |= 0x40;
         if (total+nb_255s+1 > maxlen) return -2;
         for (k = 0; k < nb_255s; k++) *ptr++ = 255;
         *ptr++ = (unsigned char)(pad_amount-255*nb_255s-1);
         total += pad_amount;
      }
      if (vbr)
         for (k = 0; k < count-1; k++) ptr += fl_encode_size(lens[k], ptr);
   }
   if (self_delimited) ptr += fl_encode_size(lens[count-1], ptr);
   for (k = 0; k < count; k++) {
      memmove(ptr, frames[k], lens[k]);
      ptr += lens[k];
   }
   if (pad)
      while (ptr < data+maxlen) *ptr++ = 0;
   return total;
}

/* OPUS_SET_BITRATE on the CELT encoder: values <= 500 are rejected and leave it unchanged. */
static void fl_celt_set_bitrate(fl_celt_enc *celt, fl_i32 v) {
   if (v <= 500 && v != FL_BITRATE_MAX) return;
   celt->bitrate = IMIN(v, 750000*celt->channels);
}

static void fl_celt_set_analysis(fl_celt_enc *celt, const fl_analysis_info *info) {
   if (info) celt->analysis = *info;
}

static fl_i32 fl_opus_encode_frame(fl_opus_enc *st, const float *pcm, int frame_size, unsigned char *data,
      fl_i32 orig_max_data_bytes, fl_analysis_info *analysis_info, int is_silence, int redundancy, int celt_to_silk,
      int prefill, fl_i32 equiv_rate, int to_celt) {
   fl_celt_enc *celt = &st->celt;
   int i, ret = 0, max_data_bytes, bits_target, start_band = 0, redundancy_bytes = 0, nb_compr_bytes;
   int frame_rate, curr_bandwidth, total_buffer, activity = FL_VAD_NO_DECISION, hp_freq_smth1;
   fl_i32 nBytes;
   fl_u32 redundant_rng = 0;
   ec_ctx enc;
   float HB_gain;
   float pcm_buf[(FL_MAX_ENCODER_BUFFER+2880)*2], tmp_prefill[2*120];
   max_data_bytes = IMIN(orig_max_data_bytes, 1276);
   st->rangeFinal = 0;
   curr_bandwidth = st->bandwidth;
   total_buffer = st->delay_compensation;
   frame_rate = 48000/frame_size;
   if (is_silence) activity = !is_silence;
   else if (analysis_info->valid) {
      activity = analysis_info->activity_probability >= .1f;
      if (!activity) activity = st->peak_signal_energy < XF(316.23f)*fl_frame_energy(pcm, frame_size*st->channels);
   } else if (st->mode == FL_MODE_CELT_ONLY) {
      activity = st->peak_signal_energy < (fl_frame_energy(pcm, frame_size*st->channels)*.5f)*316.23f;
   }
   if (st->silk_bw_switch) {
      redundancy = 1;
      celt_to_silk = 1;
      st->silk_bw_switch = 0;
      prefill = 2;
   }
   if (st->mode == FL_MODE_CELT_ONLY) redundancy = 0;
   if (redundancy) {
      redundancy_bytes = fl_compute_redundancy_bytes(max_data_bytes, st->bitrate_bps, frame_rate, st->stream_channels);
      if (redundancy_bytes == 0) redundancy = 0;
   }
   bits_target = IMIN(8*(max_data_bytes-redundancy_bytes), st->bitrate_bps*6/(6*48000/frame_size))-8;
   data += 1;
   ec_enc_init(&enc, data, orig_max_data_bytes-1);
   memcpy(pcm_buf, &st->delay_buffer[(st->encoder_buffer-total_buffer)*st->channels], total_buffer*st->channels*sizeof(float));
   if (st->mode == FL_MODE_CELT_ONLY) hp_freq_smth1 = silk_LSHIFT(silk_lin2log(60), 8);
   else hp_freq_smth1 = fl_silk_hp_smth1(st->silk);
   st->variable_HP_smth2_Q15 = silk_SMLAWB(st->variable_HP_smth2_Q15, hp_freq_smth1-st->variable_HP_smth2_Q15,
         SILK_FIX_CONST(0.015, 16));
   fl_dc_reject(pcm, &pcm_buf[total_buffer*st->channels], st->hp_mem, frame_size, st->channels);
   {
      float sum = fl_inner_prod(&pcm_buf[total_buffer*st->channels], &pcm_buf[total_buffer*st->channels], frame_size*st->channels);
      if (!(sum < 1e9f) || sum != sum) {
         memset(&pcm_buf[total_buffer*st->channels], 0, frame_size*st->channels*sizeof(float));
         st->hp_mem[0] = st->hp_mem[1] = st->hp_mem[2] = st->hp_mem[3] = 0;
      }
   }
   HB_gain = 1;
   if (st->mode != FL_MODE_CELT_ONLY) {
      fl_i32 total_bitRate, celt_rate;
      const float *pcm_silk;
      total_bitRate = bits_target*(6*48000/frame_size)/6;
      if (st->mode == FL_MODE_HYBRID) {
         st->silk_mode.bitRate = fl_compute_silk_rate_for_hybrid(total_bitRate, curr_bandwidth, 48000 == 50*frame_size,
               st->use_vbr, st->silk_mode.LBRR_coded, st->stream_channels);
         if (!st->energy_masking) {
            celt_rate = total_bitRate-st->silk_mode.bitRate;
            HB_gain = (float)(1-XF(fl_e_exp2((float)(XF(-celt_rate)*(1.f/1024))))); /* SHR32 is a no-op in float */
         }
      } else {
         st->silk_mode.bitRate = total_bitRate;
      }
      if (st->energy_masking && st->use_vbr && !st->lfe) {
         /* surround masking for SILK; the sum stays in an x87 register */
         xf mask_sum = 0, masking_depth;
         fl_i32 rate_offset;
         int c, end = 17, srate = 16000;
         if (st->bandwidth == FL_BW_NB) {
            end = 13;
            srate = 8000;
         } else if (st->bandwidth == FL_BW_MB) {
            end = 15;
            srate = 12000;
         }
         for (c = 0; c < st->channels; c++)
            for (i = 0; i < end; i++) {
               float m = st->energy_masking[21*c+i];
               xf mask;
               m = m < .5f ? m : .5f;
               m = m > -2.f ? m : -2.f;
               mask = m;
               if (mask > 0) mask = XF(.5f)*mask;
               mask_sum += mask;
            }
         masking_depth = mask_sum/end*st->channels+XF(.2f);
         rate_offset = (fl_i32)(float)(masking_depth*srate);
         rate_offset = FL_MAX(rate_offset, -2*st->silk_mode.bitRate/3);
         if (st->bandwidth == FL_BW_SWB || st->bandwidth == FL_BW_FB) st->silk_mode.bitRate += 3*rate_offset/5;
         else st->silk_mode.bitRate += rate_offset;
      }
      st->silk_mode.payloadSize_ms = 1000*frame_size/48000;
      st->silk_mode.nChannelsAPI = st->channels;
      st->silk_mode.nChannelsInternal = st->stream_channels;
      if (curr_bandwidth == FL_BW_NB) st->silk_mode.desiredInternalSampleRate = 8000;
      else if (curr_bandwidth == FL_BW_MB) st->silk_mode.desiredInternalSampleRate = 12000;
      else st->silk_mode.desiredInternalSampleRate = 16000;
      st->silk_mode.minInternalSampleRate = st->mode == FL_MODE_HYBRID ? 16000 : 8000;
      st->silk_mode.maxInternalSampleRate = 16000;
      if (st->mode == FL_MODE_SILK_ONLY) {
         fl_i32 effective_max_rate = max_data_bytes*8*(6*48000/frame_size)/6;
         if (frame_rate > 50) effective_max_rate = effective_max_rate*2/3;
         if (effective_max_rate < 8000) {
            st->silk_mode.maxInternalSampleRate = 12000;
            st->silk_mode.desiredInternalSampleRate = IMIN(12000, st->silk_mode.desiredInternalSampleRate);
         }
         if (effective_max_rate < 7000) {
            st->silk_mode.maxInternalSampleRate = 8000;
            st->silk_mode.desiredInternalSampleRate = IMIN(8000, st->silk_mode.desiredInternalSampleRate);
         }
      }
      st->silk_mode.useCBR = !st->use_vbr;
      st->silk_mode.maxBits = (max_data_bytes-1)*8;
      if (redundancy && redundancy_bytes >= 2) {
         st->silk_mode.maxBits -= redundancy_bytes*8+1;
         if (st->mode == FL_MODE_HYBRID) st->silk_mode.maxBits -= 20;
      }
      if (st->silk_mode.useCBR) {
         if (st->mode == FL_MODE_HYBRID) {
            fl_i16 other_bits = (fl_i16)IMAX(0, st->silk_mode.maxBits-st->silk_mode.bitRate*frame_size/48000);
            st->silk_mode.maxBits = IMAX(0, st->silk_mode.maxBits-other_bits*3/4);
            st->silk_mode.useCBR = 0;
         }
      } else if (st->mode == FL_MODE_HYBRID) {
         fl_i32 maxBitRate = fl_compute_silk_rate_for_hybrid(st->silk_mode.maxBits*48000/frame_size, curr_bandwidth,
               48000 == 50*frame_size, st->use_vbr, st->silk_mode.LBRR_coded, st->stream_channels);
         st->silk_mode.maxBits = maxBitRate*6/(6*48000/frame_size);
      }
      if (prefill) {
         fl_i32 zero = 0;
         int prefill_offset = st->channels*(st->encoder_buffer-st->delay_compensation-48000/400);
         fl_gain_fade(st->delay_buffer+prefill_offset, st->delay_buffer+prefill_offset, 0, 1, CELT_OVERLAP, 48000/400, st->channels);
         memset(st->delay_buffer, 0, prefill_offset*sizeof(float));
         fl_silk_encode(st->silk, &st->silk_mode, st->delay_buffer, st->encoder_buffer, NULL, &zero, prefill, activity);
         st->silk_mode.opusCanSwitch = 0;
      }
      pcm_silk = pcm_buf+total_buffer*st->channels;
      ret = fl_silk_encode(st->silk, &st->silk_mode, pcm_silk, frame_size, &enc, &nBytes, 0, activity);
      if (ret) return -3;
      if (st->mode == FL_MODE_SILK_ONLY) {
         if (st->silk_mode.internalSampleRate == 8000) curr_bandwidth = FL_BW_NB;
         else if (st->silk_mode.internalSampleRate == 12000) curr_bandwidth = FL_BW_MB;
         else if (st->silk_mode.internalSampleRate == 16000) curr_bandwidth = FL_BW_WB;
      }
      st->silk_mode.opusCanSwitch = st->silk_mode.switchReady && !st->nonfinal_frame;
      if (activity == FL_VAD_NO_DECISION) activity = st->silk_mode.signalType != 0;
      if (nBytes == 0) {
         st->rangeFinal = 0;
         data[-1] = fl_gen_toc(st->mode, 48000/frame_size, curr_bandwidth, st->stream_channels);
         return 1;
      }
      if (st->silk_mode.opusCanSwitch) {
         redundancy_bytes = fl_compute_redundancy_bytes(max_data_bytes, st->bitrate_bps, frame_rate, st->stream_channels);
         redundancy = redundancy_bytes != 0;
         celt_to_silk = 0;
         st->silk_bw_switch = 1;
      }
   }
   {
      int endband = 21;
      switch (curr_bandwidth) {
      case FL_BW_NB: endband = 13; break;
      case FL_BW_MB: case FL_BW_WB: endband = 17; break;
      case FL_BW_SWB: endband = 19; break;
      case FL_BW_FB: endband = 21; break;
      }
      celt->end = endband;
      celt->stream_channels = st->stream_channels;
      celt->bitrate = FL_BITRATE_MAX;
   }
   if (st->mode != FL_MODE_SILK_ONLY) {
      int celt_pred = st->silk_mode.reducedDependency ? 0 : 2;
      celt->disable_pf = celt_pred <= 1;
      celt->force_intra = celt_pred == 0;
   }
   if (st->mode != FL_MODE_SILK_ONLY && st->mode != st->prev_mode && st->prev_mode > 0)
      memcpy(tmp_prefill, &st->delay_buffer[(st->encoder_buffer-total_buffer-48000/400)*st->channels],
            st->channels*48000/400*sizeof(float));
   if (st->channels*(st->encoder_buffer-(frame_size+total_buffer)) > 0) {
      memmove(st->delay_buffer, &st->delay_buffer[st->channels*frame_size],
            st->channels*(st->encoder_buffer-frame_size-total_buffer)*sizeof(float));
      memcpy(&st->delay_buffer[st->channels*(st->encoder_buffer-frame_size-total_buffer)], &pcm_buf[0],
            (frame_size+total_buffer)*st->channels*sizeof(float));
   } else {
      memcpy(st->delay_buffer, &pcm_buf[(frame_size+total_buffer-st->encoder_buffer)*st->channels],
            st->encoder_buffer*st->channels*sizeof(float));
   }
   if (st->prev_HB_gain < 1 || HB_gain < 1)
      fl_gain_fade(pcm_buf, pcm_buf, st->prev_HB_gain, HB_gain, CELT_OVERLAP, frame_size, st->channels);
   st->prev_HB_gain = HB_gain;
   if (st->mode != FL_MODE_HYBRID || st->stream_channels == 1) {
      if (equiv_rate > 32000) st->silk_mode.stereoWidth_Q14 = 16384;
      else if (equiv_rate < 16000) st->silk_mode.stereoWidth_Q14 = 0;
      else st->silk_mode.stereoWidth_Q14 = 16384-2048*(fl_i32)(32000-equiv_rate)/(equiv_rate-14000);
   }
   if (!st->energy_masking && st->channels == 2) {
      if (st->hybrid_stereo_width_Q14 < (1<<14) || st->silk_mode.stereoWidth_Q14 < (1<<14)) {
         fl_stereo_fade(pcm_buf, st->hybrid_stereo_width_Q14, (fl_i16)st->silk_mode.stereoWidth_Q14, frame_size, st->channels);
         st->hybrid_stereo_width_Q14 = (fl_i16)st->silk_mode.stereoWidth_Q14;
      }
   }
   if (st->mode != FL_MODE_CELT_ONLY && ec_tell(&enc)+17+20*(st->mode == FL_MODE_HYBRID) <= 8*(max_data_bytes-1)) {
      if (st->mode == FL_MODE_HYBRID) ec_enc_bit_logp(&enc, redundancy, 12);
      if (redundancy) {
         int max_redundancy;
         ec_enc_bit_logp(&enc, celt_to_silk, 1);
         if (st->mode == FL_MODE_HYBRID) max_redundancy = (max_data_bytes-1)-((ec_tell(&enc)+8+3+7)>>3);
         else max_redundancy = (max_data_bytes-1)-((ec_tell(&enc)+7)>>3);
         redundancy_bytes = IMIN(max_redundancy, redundancy_bytes);
         redundancy_bytes = IMIN(257, IMAX(2, redundancy_bytes));
         if (st->mode == FL_MODE_HYBRID) ec_enc_uint(&enc, redundancy_bytes-2, 256);
      }
   } else redundancy = 0;
   if (!redundancy) {
      st->silk_bw_switch = 0;
      redundancy_bytes = 0;
   }
   if (st->mode != FL_MODE_CELT_ONLY) start_band = 17;
   if (st->mode == FL_MODE_SILK_ONLY) {
      ret = (ec_tell(&enc)+7)>>3;
      ec_enc_done(&enc);
      nb_compr_bytes = ret;
   } else {
      nb_compr_bytes = (max_data_bytes-1)-redundancy_bytes;
      ec_enc_shrink(&enc, nb_compr_bytes);
   }
   if (redundancy || st->mode != FL_MODE_SILK_ONLY) fl_celt_set_analysis(celt, analysis_info);
   if (st->mode == FL_MODE_HYBRID) {
      celt->silk_signal_type = st->silk_mode.signalType;
      celt->silk_offset = st->silk_mode.offset;
   }
   if (redundancy && celt_to_silk) {
      int err;
      celt->start = 0;
      celt->vbr = 0;
      celt->bitrate = FL_BITRATE_MAX;
      err = fl_celt_encode(celt, pcm_buf, 48000/200, data+nb_compr_bytes, redundancy_bytes, NULL);
      if (err < 0) return -3;
      redundant_rng = celt->rng;
      fl_celt_enc_reset(celt);
   }
   celt->start = start_band;
   data[-1] = 0;
   if (st->mode != FL_MODE_SILK_ONLY) {
      celt->vbr = st->use_vbr;
      if (st->mode == FL_MODE_HYBRID) {
         if (st->use_vbr) {
            fl_celt_set_bitrate(celt, st->bitrate_bps-st->silk_mode.bitRate);
            celt->constrained_vbr = 0;
         }
      } else if (st->use_vbr) {
         celt->vbr = 1;
         celt->constrained_vbr = st->vbr_constraint;
         fl_celt_set_bitrate(celt, st->bitrate_bps);
      }
      if (st->mode != st->prev_mode && st->prev_mode > 0) {
         unsigned char dummy[2];
         fl_celt_enc_reset(celt);
         fl_celt_encode(celt, tmp_prefill, 48000/400, dummy, 2, NULL);
         celt->disable_pf = 1;
         celt->force_intra = 1;
      }
      if (ec_tell(&enc) <= 8*nb_compr_bytes) {
         ret = fl_celt_encode(celt, pcm_buf, frame_size, NULL, nb_compr_bytes, &enc);
         if (ret < 0) return -3;
         if (redundancy && celt_to_silk && st->mode == FL_MODE_HYBRID && nb_compr_bytes != ret) {
            memmove(data+ret, data+nb_compr_bytes, redundancy_bytes);
            nb_compr_bytes = ret+redundancy_bytes;
         }
      }
      st->rangeFinal = celt->rng;
   } else {
      st->rangeFinal = enc.rng;
   }
   if (redundancy && !celt_to_silk) {
      unsigned char dummy[2];
      int err, N2 = 48000/200, N4 = 48000/400;
      fl_celt_enc_reset(celt);
      celt->start = 0;
      celt->disable_pf = 1;
      celt->force_intra = 1;
      celt->vbr = 0;
      celt->bitrate = FL_BITRATE_MAX;
      if (st->mode == FL_MODE_HYBRID) {
         nb_compr_bytes = ret;
         ec_enc_shrink(&enc, nb_compr_bytes);
      }
      fl_celt_encode(celt, pcm_buf+st->channels*(frame_size-N2-N4), N4, dummy, 2, NULL);
      err = fl_celt_encode(celt, pcm_buf+st->channels*(frame_size-N2), N2, data+nb_compr_bytes, redundancy_bytes, NULL);
      if (err < 0) return -3;
      redundant_rng = celt->rng;
   }
   data--;
   data[0] |= fl_gen_toc(st->mode, 48000/frame_size, curr_bandwidth, st->stream_channels);
   st->rangeFinal ^= redundant_rng;
   st->prev_mode = to_celt ? FL_MODE_CELT_ONLY : st->mode;
   st->prev_channels = st->stream_channels;
   st->prev_framesize = frame_size;
   st->first = 0;
   st->nb_no_activity_ms_Q1 = 0;
   if (ec_tell(&enc) > (max_data_bytes-1)*8) {
      if (max_data_bytes < 2) return -2;
      data[1] = 0;
      ret = 1;
      st->rangeFinal = 0;
   } else if (st->mode == FL_MODE_SILK_ONLY && !redundancy) {
      while (ret > 2 && data[ret] == 0) ret--;
   }
   ret += 1+redundancy_bytes;
   if (!st->use_vbr) {
      if (fl_packet_pad(data, ret, orig_max_data_bytes) != 0) return -3;
      ret = orig_max_data_bytes;
   }
   (void)i;
   return ret;
}

static fl_i32 fl_opus_encode_native(fl_opus_enc *st, const float *pcm, int frame_size, unsigned char *data,
      fl_i32 out_data_bytes, int lsb_depth, const float *analysis_pcm, fl_i32 analysis_size, int c1, int c2,
      int analysis_channels) {
   int i, redundancy = 0, celt_to_silk = 0, to_celt = 0, voice_est, frame_rate, prefill = 0, is_silence;
   fl_i32 equiv_rate, max_rate, max_data_bytes, cbr_bytes = -1;
   float stereo_width;
   fl_analysis_info analysis_info;
   int analysis_read_pos_bak = -1, analysis_read_subframe_bak = -1;
   max_data_bytes = IMIN(1276*6, out_data_bytes);
   st->rangeFinal = 0;
   if (frame_size <= 0 || max_data_bytes <= 0) return FL_ERR_BAD_ARG;
   lsb_depth = IMIN(lsb_depth, st->lsb_depth);
   is_silence = fl_e_maxabs(pcm, frame_size*st->channels) <= XF(1)/(1<<lsb_depth);
   analysis_info.valid = 0;
   if (st->silk_mode.complexity >= 7) {
      analysis_read_pos_bak = st->analysis.read_pos;
      analysis_read_subframe_bak = st->analysis.read_subframe;
      fl_run_analysis(&st->analysis, analysis_pcm, analysis_size, frame_size, c1, c2, analysis_channels, lsb_depth,
            &analysis_info);
   } else if (st->analysis.initialized) {
      fl_tonality_reset(&st->analysis);
   }
   if (!is_silence) st->voice_ratio = -1;
   st->detected_bandwidth = 0;
   if (analysis_info.valid) {
      int analysis_bandwidth;
      if (st->signal_type == FL_AUTO) {
         float prob;
         if (st->prev_mode == 0) prob = analysis_info.music_prob;
         else if (st->prev_mode == FL_MODE_CELT_ONLY) prob = analysis_info.music_prob_max;
         else prob = analysis_info.music_prob_min;
         st->voice_ratio = (int)floor((double)((1-XF(prob))*100.f+.5f));
      }
      analysis_bandwidth = analysis_info.bandwidth;
      if (analysis_bandwidth <= 12) st->detected_bandwidth = FL_BW_NB;
      else if (analysis_bandwidth <= 14) st->detected_bandwidth = FL_BW_MB;
      else if (analysis_bandwidth <= 16) st->detected_bandwidth = FL_BW_WB;
      else if (analysis_bandwidth <= 18) st->detected_bandwidth = FL_BW_SWB;
      else st->detected_bandwidth = FL_BW_FB;
   }
   if (!analysis_info.valid || analysis_info.activity_probability > .1f) {
      if (!is_silence) {
         float a = (float)(XF(.999f)*st->peak_signal_energy);
         xf e = fl_frame_energy(pcm, frame_size*st->channels);
         st->peak_signal_energy = a > e ? a : (float)e;
      }
   }
   if (st->channels == 2 && st->force_channels != 1) stereo_width = fl_compute_stereo_width(pcm, frame_size, &st->width_mem);
   else stereo_width = 0;
   {
      fl_i32 max_bitrate = max_data_bytes*8*(6*48000/frame_size)/6, user_bitrate;
      if (st->user_bitrate_bps == FL_AUTO) user_bitrate = 60*48000/frame_size+48000*st->channels;
      else if (st->user_bitrate_bps == FL_BITRATE_MAX) user_bitrate = 1500000;
      else user_bitrate = st->user_bitrate_bps;
      st->bitrate_bps = IMIN(user_bitrate, max_bitrate);
   }
   frame_rate = 48000/frame_size;
   if (!st->use_vbr) {
      cbr_bytes = IMIN((st->bitrate_bps*6/(6*48000/frame_size)+4)/8, max_data_bytes);
      st->bitrate_bps = cbr_bytes*8*(6*48000/frame_size)/6;
      max_data_bytes = IMAX(1, cbr_bytes);
   }
   if (max_data_bytes < 3 || st->bitrate_bps < 3*frame_rate*8 || (frame_rate < 50 && (max_data_bytes*frame_rate < 300 || st->bitrate_bps < 2400))) {
      int tocmode = st->mode, bw = st->bandwidth == 0 ? FL_BW_NB : st->bandwidth, ret;
      if (tocmode == 0) tocmode = FL_MODE_SILK_ONLY;
      if (frame_rate > 100) tocmode = FL_MODE_CELT_ONLY;
      if (tocmode == FL_MODE_SILK_ONLY && bw > FL_BW_WB) bw = FL_BW_WB;
      else if (tocmode == FL_MODE_CELT_ONLY && bw == FL_BW_MB) bw = FL_BW_NB;
      else if (tocmode == FL_MODE_HYBRID && bw <= FL_BW_SWB) bw = FL_BW_SWB;
      data[0] = fl_gen_toc(tocmode, frame_rate, bw, st->stream_channels);
      ret = 1;
      if (!st->use_vbr) {
         if (fl_packet_pad(data, ret, max_data_bytes) != 0) return -3;
         ret = max_data_bytes;
      }
      return ret;
   }
   max_rate = max_data_bytes*8*(6*48000/frame_size)/6;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->channels, frame_rate, st->use_vbr, 0,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   if (st->signal_type == FL_SIGNAL_VOICE) voice_est = 127;
   else if (st->signal_type == FL_SIGNAL_MUSIC) voice_est = 0;
   else if (st->voice_ratio >= 0) voice_est = IMIN(st->voice_ratio*327>>8, 115);
   else voice_est = 48;
   if (st->force_channels != FL_AUTO && st->channels == 2) {
      st->stream_channels = st->force_channels;
   } else if (st->channels == 2) {
      fl_i32 stereo_threshold = 17000+((voice_est*voice_est*(19000-17000))>>14);
      if (st->stream_channels == 2) stereo_threshold -= 1000;
      else stereo_threshold += 1000;
      st->stream_channels = equiv_rate > stereo_threshold ? 2 : 1;
   } else st->stream_channels = st->channels;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->stream_channels, frame_rate, st->use_vbr, 0,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   st->silk_mode.useDTX = st->use_dtx && !(analysis_info.valid || is_silence);
   if (st->user_forced_mode == FL_AUTO) {
      fl_i32 mode_voice, mode_music, threshold;
      xf a = 1-XF(stereo_width);
      mode_voice = (fl_i32)(float)(a*64000+XF(stereo_width)*44000);
      mode_music = (fl_i32)(float)(a*10000+XF(stereo_width)*10000);
      threshold = mode_music+((voice_est*voice_est*(mode_voice-mode_music))>>14);
      if (st->prev_mode == FL_MODE_CELT_ONLY) threshold -= 4000;
      else if (st->prev_mode > 0) threshold += 4000;
      st->mode = equiv_rate >= threshold ? FL_MODE_CELT_ONLY : FL_MODE_SILK_ONLY;
      if (st->silk_mode.useInBandFEC && st->silk_mode.packetLossPercentage > (128-voice_est)>>4) st->mode = FL_MODE_SILK_ONLY;
      if (st->silk_mode.useDTX && voice_est > 100) st->mode = FL_MODE_SILK_ONLY;
      if (max_data_bytes < (frame_rate > 50 ? 9000 : 6000)*6/(6*48000/frame_size)/8) st->mode = FL_MODE_CELT_ONLY;
   } else st->mode = st->user_forced_mode;
   if (st->mode != FL_MODE_CELT_ONLY && frame_size < 48000/100) st->mode = FL_MODE_CELT_ONLY;
   if (st->lfe) st->mode = FL_MODE_CELT_ONLY;
   if (st->prev_mode > 0 && ((st->mode != FL_MODE_CELT_ONLY && st->prev_mode == FL_MODE_CELT_ONLY) ||
         (st->mode == FL_MODE_CELT_ONLY && st->prev_mode != FL_MODE_CELT_ONLY))) {
      redundancy = 1;
      celt_to_silk = st->mode != FL_MODE_CELT_ONLY;
      if (!celt_to_silk) {
         if (frame_size >= 48000/100) {
            st->mode = st->prev_mode;
            to_celt = 1;
         } else redundancy = 0;
      }
   }
   if (st->stream_channels == 1 && st->prev_channels == 2 && st->silk_mode.toMono == 0 &&
         st->mode != FL_MODE_CELT_ONLY && st->prev_mode != FL_MODE_CELT_ONLY) {
      st->silk_mode.toMono = 1;
      st->stream_channels = 2;
   } else st->silk_mode.toMono = 0;
   equiv_rate = fl_compute_equiv_rate(st->bitrate_bps, st->stream_channels, frame_rate, st->use_vbr, st->mode,
         st->silk_mode.complexity, st->silk_mode.packetLossPercentage);
   if (st->mode != FL_MODE_CELT_ONLY && st->prev_mode == FL_MODE_CELT_ONLY) {
      fl_silk_ctrl dummy;
      fl_silk_init(st->silk, st->channels, &dummy);
      prefill = 1;
   }
   if (st->mode == FL_MODE_CELT_ONLY || st->first || st->silk_mode.allowBandwidthSwitch) {
      static const fl_i32 voice_thr[8] = {9000, 700, 9000, 700, 13500, 1000, 14000, 2000};
      static const fl_i32 music_thr[8] = {9000, 700, 9000, 700, 11000, 1000, 12000, 2000};
      fl_i32 bandwidth_thresholds[8];
      int bandwidth = FL_BW_FB;
      for (i = 0; i < 8; i++)
         bandwidth_thresholds[i] = music_thr[i]+((voice_est*voice_est*(voice_thr[i]-music_thr[i]))>>14);
      do {
         int threshold = bandwidth_thresholds[2*(bandwidth-FL_BW_MB)], hysteresis = bandwidth_thresholds[2*(bandwidth-FL_BW_MB)+1];
         if (!st->first) {
            if (st->auto_bandwidth >= bandwidth) threshold -= hysteresis;
            else threshold += hysteresis;
         }
         if (equiv_rate >= threshold) break;
      } while (--bandwidth > FL_BW_NB);
      if (bandwidth == FL_BW_MB) bandwidth = FL_BW_WB;
      st->bandwidth = st->auto_bandwidth = bandwidth;
      if (!st->first && st->mode != FL_MODE_CELT_ONLY && !st->silk_mode.inWBmodeWithoutVariableLP && st->bandwidth > FL_BW_WB)
         st->bandwidth = FL_BW_WB;
   }
   if (st->bandwidth > st->max_bandwidth) st->bandwidth = st->max_bandwidth;
   if (st->user_bandwidth != FL_AUTO) st->bandwidth = st->user_bandwidth;
   if (st->mode != FL_MODE_CELT_ONLY && max_rate < 15000) st->bandwidth = IMIN(st->bandwidth, FL_BW_WB);
   if (st->detected_bandwidth && st->user_bandwidth == FL_AUTO) {
      int min_detected_bandwidth;
      if (equiv_rate <= 18000*st->stream_channels && st->mode == FL_MODE_CELT_ONLY) min_detected_bandwidth = FL_BW_NB;
      else if (equiv_rate <= 24000*st->stream_channels && st->mode == FL_MODE_CELT_ONLY) min_detected_bandwidth = FL_BW_MB;
      else if (equiv_rate <= 30000*st->stream_channels) min_detected_bandwidth = FL_BW_WB;
      else if (equiv_rate <= 44000*st->stream_channels) min_detected_bandwidth = FL_BW_SWB;
      else min_detected_bandwidth = FL_BW_FB;
      st->detected_bandwidth = IMAX(st->detected_bandwidth, min_detected_bandwidth);
      st->bandwidth = IMIN(st->bandwidth, st->detected_bandwidth);
   }
   st->silk_mode.LBRR_coded = 0;
   st->celt.lsb_depth = lsb_depth;
   if (st->mode == FL_MODE_CELT_ONLY && st->bandwidth == FL_BW_MB) st->bandwidth = FL_BW_WB;
   if (st->lfe) st->bandwidth = FL_BW_NB;
   {
      int curr_bandwidth = st->bandwidth;
      if (st->mode == FL_MODE_SILK_ONLY && curr_bandwidth > FL_BW_WB) st->mode = FL_MODE_HYBRID;
      if (st->mode == FL_MODE_HYBRID && curr_bandwidth <= FL_BW_WB) st->mode = FL_MODE_SILK_ONLY;
   }
   if ((frame_size > 48000/50 && st->mode != FL_MODE_SILK_ONLY) || frame_size > 3*48000/50) {
      /* Several frames repacketized into one packet. */
      int enc_frame_size, nb_frames, max_header_bytes, dtx_count = 0, bak_to_mono, nb = 0;
      fl_i32 repacketize_len, max_len_sum, tot_size = 0;
      unsigned char tmp_data[7680], *curr_data = tmp_data, *frames[6];
      int lens[6];
      if (st->mode == FL_MODE_SILK_ONLY) {
         if (frame_size == 2*48000/25) enc_frame_size = 48000/25;
         else if (frame_size == 3*48000/25) enc_frame_size = 3*48000/50;
         else enc_frame_size = 48000/50;
      } else enc_frame_size = 48000/50;
      nb_frames = frame_size/enc_frame_size;
      if (analysis_read_pos_bak != -1) {
         st->analysis.read_pos = analysis_read_pos_bak;
         st->analysis.read_subframe = analysis_read_subframe_bak;
      }
      max_header_bytes = nb_frames == 2 ? 3 : (2+(nb_frames-1)*2);
      if (st->use_vbr || st->user_bitrate_bps == FL_BITRATE_MAX) repacketize_len = out_data_bytes;
      else repacketize_len = IMIN(cbr_bytes, out_data_bytes);
      max_len_sum = nb_frames+repacketize_len-max_header_bytes;
      bak_to_mono = st->silk_mode.toMono;
      if (bak_to_mono) st->force_channels = 1;
      else st->prev_channels = st->stream_channels;
      for (i = 0; i < nb_frames; i++) {
         int first_frame, frame_to_celt, frame_redundancy, tmp_len;
         fl_i32 curr_max;
         first_frame = i == 0 || i == dtx_count;
         (void)first_frame;
         st->silk_mode.toMono = 0;
         st->nonfinal_frame = i < nb_frames-1;
         frame_to_celt = to_celt && i == nb_frames-1;
         frame_redundancy = redundancy && (frame_to_celt || (!to_celt && i == 0));
         curr_max = IMIN(st->bitrate_bps*6/(6*48000/enc_frame_size)/8, max_len_sum/nb_frames);
         curr_max = IMIN(max_len_sum-tot_size, curr_max);
         if (analysis_read_pos_bak != -1) fl_tonality_get_info(&st->analysis, &analysis_info, enc_frame_size);
         is_silence = fl_e_maxabs(pcm+i*(st->channels*enc_frame_size), enc_frame_size*st->channels) <= XF(1)/(1<<lsb_depth);
         tmp_len = fl_opus_encode_frame(st, pcm+i*(st->channels*enc_frame_size), enc_frame_size, curr_data, curr_max,
               &analysis_info, is_silence, frame_redundancy, celt_to_silk, prefill, equiv_rate, frame_to_celt);
         if (tmp_len < 0) return -3;
         if (tmp_len == 1) dtx_count++;
         /* opus_repacketizer_cat: strip the TOC of single-frame packets */
         if ((curr_data[0]&3) != 0) {
            /* padded (CBR) frame: code 3 with one frame and padding */
            int p = 2, pad = 0;
            if (curr_data[1]&0x40) {
               int b;
               do {
                  b = curr_data[p++];
                  pad += b == 255 ? 254 : b;
               } while (b == 255);
            }
            frames[nb] = curr_data+p;
            lens[nb] = tmp_len-p-pad;
         } else {
            frames[nb] = curr_data+1;
            lens[nb] = tmp_len-1;
         }
         if (nb == 0) data[0] = curr_data[0];
         nb++;
         tot_size += tmp_len;
         curr_data += tmp_len;
      }
      st->silk_mode.toMono = bak_to_mono;
      {
         int pad = !st->use_vbr && dtx_count != nb_frames;
         return fl_repacketize(data[0], frames, lens, nb, data, repacketize_len, 0, pad);
      }
   }
   (void)cbr_bytes;
   return fl_opus_encode_frame(st, pcm, frame_size, data, max_data_bytes, &analysis_info, is_silence, redundancy,
         celt_to_silk, prefill, equiv_rate, to_celt);
}

/* ---- Multistream encoder (libopus opus_multistream_encoder.c, float API) ---- */
struct fl_msenc {
   int channels, family, streams, coupled, lfe_stream, surround, variable_duration;
   fl_i32 bitrate_bps;
   unsigned char mapping[255];
   fl_opus_enc *enc;                 /* one encoder per stream */
   float *window_mem, *preemph_mem;  /* surround analysis state */
   float *bandSMR, *buf, bandLogE[42];
   unsigned char tmp[6*1275+12];
};

/* Stream layout of opus_multistream_surround_encoder_init() for families 0, 1 and 255. */
static int fl_msenc_layout(int channels, int family, int *streams, int *coupled, unsigned char *mapping) {
   static const unsigned char vorbis[8][10] = {
      {1, 0, 0}, {1, 1, 0, 1}, {2, 1, 0, 2, 1}, {2, 2, 0, 1, 2, 3}, {3, 2, 0, 4, 1, 2, 3},
      {4, 2, 0, 4, 1, 2, 3, 5}, {4, 3, 0, 4, 1, 2, 3, 5, 6}, {5, 3, 0, 6, 1, 2, 3, 4, 5, 7}};
   int i;
   if (channels < 1 || channels > 255) return FL_ERR_BAD_ARG;
   if (family == 0 && channels <= 2) {
      *streams = 1;
      *coupled = channels-1;
      for (i = 0; i < channels; i++) mapping[i] = (unsigned char)i;
   } else if (family == 1 && channels <= 8) {
      *streams = vorbis[channels-1][0];
      *coupled = vorbis[channels-1][1];
      for (i = 0; i < channels; i++) mapping[i] = vorbis[channels-1][2+i];
   } else if (family == 255) {
      *streams = channels;
      *coupled = 0;
      for (i = 0; i < channels; i++) mapping[i] = (unsigned char)i;
   } else return FL_ERR_UNSUPPORTED;
   return FL_OK;
}

static void fl_msenc_destroy(fl_msenc *st) {
   int s;
   if (!st) return;
   if (st->enc)
      for (s = 0; s < st->streams; s++) free(st->enc[s].silk);
   free(st->enc);
   free(st->window_mem);
   free(st->preemph_mem);
   free(st->bandSMR);
   free(st->buf);
   free(st);
}

static fl_msenc *fl_msenc_create(int channels, int family, int *err) {
   fl_msenc *st = (fl_msenc*)calloc(1, sizeof(*st));
   int s;
   *err = FL_ERR_ALLOC;
   if (!st) return NULL;
   *err = fl_msenc_layout(channels, family, &st->streams, &st->coupled, st->mapping);
   if (*err != FL_OK) {
      free(st);
      return NULL;
   }
   *err = FL_ERR_ALLOC;
   st->channels = channels;
   st->family = family;
   st->lfe_stream = family == 1 && channels >= 6 ? st->streams-1 : -1;
   st->surround = family == 1 && channels > 2;
   st->bitrate_bps = FL_AUTO;
   st->variable_duration = 5000;
   st->enc = (fl_opus_enc*)calloc(st->streams, sizeof(fl_opus_enc));
   st->bandSMR = (float*)calloc(21*channels, sizeof(float));
   st->buf = (float*)malloc(sizeof(float)*2*5760);
   if (st->surround) {
      st->window_mem = (float*)calloc(channels*CELT_OVERLAP, sizeof(float));
      st->preemph_mem = (float*)calloc(channels, sizeof(float));
   }
   if (!st->enc || !st->bandSMR || !st->buf || (st->surround && (!st->window_mem || !st->preemph_mem))) {
      fl_msenc_destroy(st);
      return NULL;
   }
   for (s = 0; s < st->streams; s++) {
      st->enc[s].silk = fl_silk_create();
      if (!st->enc[s].silk) {
         fl_msenc_destroy(st);
         return NULL;
      }
      fl_opus_enc_init(&st->enc[s], s < st->coupled ? 2 : 1);
      if (s == st->lfe_stream) st->enc[s].lfe = st->enc[s].celt.lfe = 1;
   }
   *err = FL_OK;
   return st;
}

static int fl_msenc_lookahead(fl_msenc *st) { (void)st; return 48000/400+192; }

/* The opusenc ctl sequence, forwarded to every stream encoder. */
static void fl_msenc_setup(fl_msenc *st, const fl_options *opt, int lsb_depth) {
   fl_i32 v = opt->bitrate;
   int s, duration;
   if (v != FL_AUTO && v != FL_BITRATE_MAX) v = IMIN(750000*st->channels, IMAX(500*st->channels, v));
   st->bitrate_bps = v;
   duration = opt->frame_ms_x10 == 25 ? 5001 : opt->frame_ms_x10 == 50 ? 5002 : opt->frame_ms_x10 == 100 ? 5003 :
         opt->frame_ms_x10 == 400 ? 5005 : opt->frame_ms_x10 == 600 ? 5006 : opt->frame_ms_x10 == 800 ? 5007 :
         opt->frame_ms_x10 == 1000 ? 5008 : opt->frame_ms_x10 == 1200 ? 5009 : 5004;
   st->variable_duration = duration;
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      e->use_vbr = opt->vbr;
      e->silk_mode.useCBR = 1-opt->vbr;
      if (opt->vbr) e->vbr_constraint = opt->cvbr;
      e->signal_type = opt->signal == 1 ? FL_SIGNAL_MUSIC : opt->signal == 2 ? FL_SIGNAL_VOICE : FL_AUTO;
      e->silk_mode.complexity = opt->complexity;
      e->celt.complexity = opt->complexity;
      e->silk_mode.packetLossPercentage = opt->expect_loss;
      e->celt.loss_rate = opt->expect_loss;
      e->celt.disable_inv = opt->no_phase_inv;
      e->lsb_depth = lsb_depth;
      e->variable_duration = duration;
   }
}

static int fl_msenc_get_duration(fl_msenc *st) { return st->variable_duration; }
static void fl_msenc_set_duration(fl_msenc *st, int request) {
   int s;
   st->variable_duration = request;
   for (s = 0; s < st->streams; s++) st->enc[s].variable_duration = request;
}

/* OPUS_SET_BITRATE on one stream encoder. */
static void fl_enc_set_bitrate(fl_opus_enc *e, fl_i32 v) {
   if (v != FL_AUTO && v != FL_BITRATE_MAX) {
      if (v <= 0) return;  /* rejected by the ctl */
      if (v <= 500) v = 500;
      else if (v > 750000*e->channels) v = 750000*e->channels;
   }
   e->user_bitrate_bps = v;
}

/* log2(2^a+2^b) approximation; 2*diff is rounded to float before floorf. */
static float fl_log_sum(float a, float b) {
   static const float diff_table[17] = {0.5000000f, 0.2924813f, 0.1609640f, 0.0849625f, 0.0437314f,
      0.0221971f, 0.0111839f, 0.0056136f, 0.0028123f};
   float mx, d2;
   xf diff;
   int low;
   if (a > b) {
      mx = a;
      diff = XF(a)-b;
   } else {
      mx = b;
      diff = XF(b)-a;
   }
   if (!(diff < 8.f)) return mx;
   d2 = (float)(diff+diff);
   low = (int)floorf(d2);
   return (float)((XF(diff_table[low])+mx)+(XF(d2)-low)*(XF(diff_table[low+1])-diff_table[low]));
}

/* surround_analysis(): per-channel band energies masked by the other channels. */
static void fl_surround_analysis(fl_msenc *st, const float *pcm, int frame_size) {
   int C = st->channels, c, i, LM, pos[8] = {0}, freq_size, nb_frames, frame;
   float *in, freq[960], bandE[21], tmpE[21], maskLogE[3][21], *bandLogE = st->bandSMR;
   xf channel_offset;
   for (LM = 0; LM < CELT_MAX_LM; LM++)
      if (CELT_SHORT_MDCT<<LM == frame_size) break;
   freq_size = CELT_SHORT_MDCT<<LM;
   nb_frames = frame_size/freq_size;
   in = (float*)malloc(sizeof(float)*(frame_size+CELT_OVERLAP));
   if (!in) return;
   /* channel_pos(): 0 don't mix, 1 left, 2 center, 3 right */
   if (C == 4) {
      pos[0] = 1; pos[1] = 3; pos[2] = 1; pos[3] = 3;
   } else if (C == 3 || C == 5 || C == 6) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 0;
   } else if (C == 7) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 2; pos[6] = 0;
   } else if (C == 8) {
      pos[0] = 1; pos[1] = 2; pos[2] = 3; pos[3] = 1; pos[4] = 3; pos[5] = 1; pos[6] = 3; pos[7] = 0;
   }
   for (c = 0; c < 3; c++)
      for (i = 0; i < 21; i++) maskLogE[c][i] = -28.f;
   for (c = 0; c < C; c++) {
      memcpy(in, st->window_mem+c*CELT_OVERLAP, CELT_OVERLAP*sizeof(float));
      fl_e_preemphasis(pcm+c, in+CELT_OVERLAP, frame_size, C, &st->preemph_mem[c], 0);
      {
         float sum = (float)fl_e_inner_prod(in, in, frame_size+CELT_OVERLAP);
         if (!(sum < 1e18f) || sum != sum) {
            memset(in, 0, (frame_size+CELT_OVERLAP)*sizeof(float));
            st->preemph_mem[c] = 0;
         }
      }
      memset(bandE, 0, sizeof(bandE));
      for (frame = 0; frame < nb_frames; frame++) {
         fl_e_mdct_forward(in+freq_size*frame, freq, CELT_OVERLAP, CELT_MAX_LM-LM, 1);
         fl_e_band_energies(freq, tmpE, 21, 1, LM);
         for (i = 0; i < 21; i++) bandE[i] = FL_MAX(bandE[i], tmpE[i]);
      }
      fl_e_amp2log2(21, 21, bandE, bandLogE+21*c, 1);
      /* spreading: -6 dB/band going up, -12 dB/band going down */
      for (i = 1; i < 21; i++) {
         xf v = XF(bandLogE[21*c+i-1])-1;
         if (XF(bandLogE[21*c+i]) < v) bandLogE[21*c+i] = (float)v;
      }
      for (i = 19; i >= 0; i--) {
         xf v = XF(bandLogE[21*c+i+1])-2;
         if (XF(bandLogE[21*c+i]) < v) bandLogE[21*c+i] = (float)v;
      }
      if (pos[c] == 1) {
         for (i = 0; i < 21; i++) maskLogE[0][i] = fl_log_sum(maskLogE[0][i], bandLogE[21*c+i]);
      } else if (pos[c] == 3) {
         for (i = 0; i < 21; i++) maskLogE[2][i] = fl_log_sum(maskLogE[2][i], bandLogE[21*c+i]);
      } else if (pos[c] == 2) {
         for (i = 0; i < 21; i++) {
            float b = (float)(XF(bandLogE[21*c+i])-XF(.5f));
            maskLogE[0][i] = fl_log_sum(maskLogE[0][i], b);
            maskLogE[2][i] = fl_log_sum(maskLogE[2][i], b);
         }
      }
      memcpy(st->window_mem+c*CELT_OVERLAP, in+frame_size, CELT_OVERLAP*sizeof(float));
   }
   free(in);
   for (i = 0; i < 21; i++) maskLogE[1][i] = FL_MIN(maskLogE[0][i], maskLogE[2][i]);
   channel_offset = XF(.5f)*fl_e_log2((float)(XF(2.f)/(C-1)));
   for (c = 0; c < 3; c++)
      for (i = 0; i < 21; i++) maskLogE[c][i] = (float)(maskLogE[c][i]+channel_offset);
   for (c = 0; c < C; c++) {
      if (pos[c] != 0) {
         const float *mask = maskLogE[pos[c]-1];
         for (i = 0; i < 21; i++) bandLogE[21*c+i] = bandLogE[21*c+i]-mask[i];
      } else {
         for (i = 0; i < 21; i++) bandLogE[21*c+i] = 0;
      }
   }
}

/* surround_rate_allocation(); returns the rate sum. */
static fl_i32 fl_msenc_rate_allocation(fl_msenc *st, fl_i32 *rate, int frame_size) {
   int i, nb_lfe = st->lfe_stream != -1, nb_coupled = st->coupled, nb_uncoupled = st->streams-nb_coupled-nb_lfe;
   int nb_normal = 2*nb_coupled+nb_uncoupled, total;
   fl_i32 channel_offset = 40*IMAX(50, 48000/frame_size), bitrate, lfe_offset, stream_offset, channel_rate, sum = 0;
   if (st->bitrate_bps == FL_AUTO) bitrate = nb_normal*(channel_offset+48000+10000)+8000*nb_lfe;
   else if (st->bitrate_bps == FL_BITRATE_MAX) bitrate = nb_normal*750000+nb_lfe*128000;
   else bitrate = st->bitrate_bps;
   lfe_offset = IMIN(bitrate/20, 3000)+15*IMAX(50, 48000/frame_size);
   stream_offset = (bitrate-channel_offset*nb_normal-lfe_offset*nb_lfe)/nb_normal/2;
   stream_offset = IMAX(0, IMIN(20000, stream_offset));
   total = (nb_uncoupled<<8)+512*nb_coupled+nb_lfe*32;
   channel_rate = (fl_i32)(256*(fl_i64)(bitrate-lfe_offset*nb_lfe-stream_offset*(nb_coupled+nb_uncoupled)-channel_offset*nb_normal)/total);
   for (i = 0; i < st->streams; i++) {
      if (i < st->coupled) rate[i] = 2*channel_offset+IMAX(0, stream_offset+(channel_rate*512>>8));
      else if (i != st->lfe_stream) rate[i] = channel_offset+IMAX(0, stream_offset+channel_rate);
      else rate[i] = IMAX(0, lfe_offset+(channel_rate*32>>8));
      rate[i] = IMAX(rate[i], 500);
      sum += rate[i];
   }
   return sum;
}

/* First channel mapped to stream index m (get_left/right/mono_channel). */
static int fl_msenc_channel(const fl_msenc *st, int m) {
   int c;
   for (c = 0; c < st->channels; c++)
      if (st->mapping[c] == m) return c;
   return -1;
}

static int fl_msenc_encode(fl_msenc *st, const float *pcm, int analysis_frame_size, unsigned char *data, fl_i32 max_bytes) {
   static const int sizes[9] = {120, 240, 480, 960, 1920, 2880, 3840, 4800, 5760};
   int s, i, frame_size, vbr = st->enc[0].use_vbr, C = st->channels;
   fl_i32 bitrates[256], rate_sum, smallest_packet, tot_size = 0;
   frame_size = sizes[st->variable_duration-5001];
   if (frame_size > analysis_frame_size) return FL_ERR_BAD_ARG;
   smallest_packet = st->streams*2-1;
   if (48000/frame_size == 10) smallest_packet += st->streams;
   if (max_bytes < smallest_packet) return FL_ERR_BAD_ARG;
   if (st->surround) fl_surround_analysis(st, pcm, frame_size);
   rate_sum = fl_msenc_rate_allocation(st, bitrates, frame_size);
   if (!vbr) {
      if (st->bitrate_bps == FL_AUTO) max_bytes = IMIN(max_bytes, (rate_sum*6/(6*48000/frame_size)+4)/8);
      else if (st->bitrate_bps != FL_BITRATE_MAX)
         max_bytes = IMIN(max_bytes, IMAX(smallest_packet, (st->bitrate_bps*6/(6*48000/frame_size)+4)/8));
   }
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      fl_enc_set_bitrate(e, bitrates[s]);
      if (st->surround) {
         fl_i32 equiv_rate = st->bitrate_bps;
         if (frame_size*50 < 48000) equiv_rate -= 60*(48000/frame_size-50)*C;
         if (equiv_rate > 10000*C) e->user_bandwidth = FL_BW_FB;
         else if (equiv_rate > 7000*C) e->user_bandwidth = FL_BW_SWB;
         else if (equiv_rate > 5000*C) e->user_bandwidth = FL_BW_WB;
         else e->user_bandwidth = FL_BW_NB;
         e->silk_mode.maxInternalSampleRate = e->user_bandwidth == FL_BW_NB ? 8000 : 16000;
         if (s < st->coupled) {
            /* keep the spatial image: stereo CELT on coupled streams */
            e->user_forced_mode = FL_MODE_CELT_ONLY;
            e->force_channels = 2;
         }
      }
   }
   for (s = 0; s < st->streams; s++) {
      fl_opus_enc *e = &st->enc[s];
      unsigned char *frames[48];
      int lens[48], c1, c2, len, count, offset, k;
      fl_i16 size[48];
      fl_i32 curr_max;
      if (s < st->coupled) {
         c1 = fl_msenc_channel(st, 2*s);
         c2 = fl_msenc_channel(st, 2*s+1);
         for (i = 0; i < frame_size; i++) {
            st->buf[2*i] = pcm[i*C+c1];
            st->buf[2*i+1] = pcm[i*C+c2];
         }
         if (st->surround)
            for (i = 0; i < 21; i++) {
               st->bandLogE[i] = st->bandSMR[21*c1+i];
               st->bandLogE[21+i] = st->bandSMR[21*c2+i];
            }
      } else {
         c1 = fl_msenc_channel(st, s+st->coupled);
         c2 = -1;
         for (i = 0; i < frame_size; i++) st->buf[i] = pcm[i*C+c1];
         if (st->surround)
            for (i = 0; i < 21; i++) st->bandLogE[i] = st->bandSMR[21*c1+i];
      }
      if (st->surround) e->energy_masking = e->celt.energy_mask = st->bandLogE;
      curr_max = max_bytes-tot_size;
      curr_max -= IMAX(0, 2*(st->streams-s-1)-1);
      if (48000/frame_size == 10) curr_max -= st->streams-s-1;
      curr_max = IMIN(curr_max, 6*1275+12);
      if (s != st->streams-1) curr_max -= curr_max > 253 ? 2 : 1;
      if (!vbr && s == st->streams-1) fl_enc_set_bitrate(e, curr_max*8*(6*48000/frame_size)/6);
      len = fl_opus_encode_native(e, st->buf, frame_size, st->tmp, curr_max, 24, pcm, analysis_frame_size, c1, c2, C);
      if (len < 0) return len;
      /* opus_repacketizer_cat + out_range_impl: self-delimited framing for all but the last stream */
      count = fl_packet_parse(st->tmp, len, 0, size, &offset, NULL);
      if (count < 0) return -3;
      frames[0] = st->tmp+offset;
      for (k = 0; k < count; k++) {
         lens[k] = size[k];
         if (k > 0) frames[k] = frames[k-1]+lens[k-1];
      }
      len = fl_repacketize(st->tmp[0], frames, lens, count, data, max_bytes-tot_size, s != st->streams-1,
            !vbr && s == st->streams-1);
      if (len < 0) return -3;
      data += len;
      tot_size += len;
   }
   return tot_size;
}
