/* ---- Opus decoder (SILK/CELT/hybrid glue, RFC 6716 section 4.5) ---- */
#define MODE_SILK_ONLY 1000
#define MODE_HYBRID 1001
#define MODE_CELT_ONLY 1002
#define FL_BANDWIDTH_NARROWBAND 1101
#define FL_BANDWIDTH_MEDIUMBAND 1102
#define FL_BANDWIDTH_WIDEBAND 1103
#define FL_BANDWIDTH_SUPERWIDEBAND 1104
#define FL_BANDWIDTH_FULLBAND 1105

struct fl_opus_decoder {
   int channels;
   fl_i32 Fs;
   int decode_gain;
   silk_dec_control DecControl;
   int stream_channels, bandwidth, mode, prev_mode, frame_size, prev_redundancy, last_packet_duration;
   fl_u32 rangeFinal;
   silk_decoder silk;
   fl_celt_dec celt;
};

static int fl_packet_get_samples_per_frame(const unsigned char *data, fl_i32 Fs) {
   int audiosize;
   if (data[0]&0x80) {
      audiosize = ((data[0]>>3)&0x3);
      audiosize = (Fs<<audiosize)/400;
   } else if ((data[0]&0x60) == 0x60) {
      audiosize = (data[0]&0x08) ? Fs/50 : Fs/100;
   } else {
      audiosize = ((data[0]>>3)&0x3);
      if (audiosize == 3) audiosize = Fs*60/1000;
      else audiosize = (Fs<<audiosize)/100;
   }
   return audiosize;
}
static int fl_packet_get_mode(const unsigned char *data) {
   if (data[0]&0x80) return MODE_CELT_ONLY;
   if ((data[0]&0x60) == 0x60) return MODE_HYBRID;
   return MODE_SILK_ONLY;
}
static int fl_packet_get_bandwidth(const unsigned char *data) {
   int bandwidth;
   if (data[0]&0x80) {
      bandwidth = FL_BANDWIDTH_MEDIUMBAND+((data[0]>>5)&0x3);
      if (bandwidth == FL_BANDWIDTH_MEDIUMBAND) bandwidth = FL_BANDWIDTH_NARROWBAND;
   } else if ((data[0]&0x60) == 0x60) {
      bandwidth = (data[0]&0x10) ? FL_BANDWIDTH_FULLBAND : FL_BANDWIDTH_SUPERWIDEBAND;
   } else {
      bandwidth = FL_BANDWIDTH_NARROWBAND+((data[0]>>5)&0x3);
   }
   return bandwidth;
}
static int fl_parse_size(const unsigned char *data, fl_i32 len, fl_i16 *size) {
   if (len < 1) { *size = -1; return -1; }
   if (data[0] < 252) { *size = data[0]; return 1; }
   if (len < 2) { *size = -1; return -1; }
   *size = 4*data[1]+data[0];
   return 2;
}
/* Splits a packet into frames. self_delimited: multistream framing (RFC 6716 appendix B).
   packet_offset receives the total packet length including padding. Returns the frame count or <0. */
static int fl_packet_parse(const unsigned char *data, fl_i32 len, int self_delimited, fl_i16 size[48], int *payload_offset, fl_i32 *packet_offset) {
   int i, bytes, count, cbr, framesize;
   unsigned char ch, toc;
   fl_i32 last_size, pad = 0;
   const unsigned char *data0 = data;
   if (len <= 0) return -4;
   framesize = fl_packet_get_samples_per_frame(data, 48000);
   cbr = 0;
   toc = *data++;
   len--;
   last_size = len;
   switch (toc&0x3) {
   case 0:
      count = 1;
      break;
   case 1:
      count = 2;
      cbr = 1;
      if (!self_delimited) {
         if (len&0x1) return -4;
         last_size = len/2;
         size[0] = (fl_i16)last_size;
      }
      break;
   case 2:
      count = 2;
      bytes = fl_parse_size(data, len, size);
      len -= bytes;
      if (size[0] < 0 || size[0] > len) return -4;
      data += bytes;
      last_size = len-size[0];
      break;
   default:
      if (len < 1) return -4;
      ch = *data++;
      count = ch&0x3F;
      if (count <= 0 || framesize*(fl_i32)count > 5760) return -4;
      len--;
      if (ch&0x40) {
         int p;
         do {
            int tmp;
            if (len <= 0) return -4;
            p = *data++;
            len--;
            tmp = p == 255 ? 254 : p;
            len -= tmp;
            pad += tmp;
         } while (p == 255);
      }
      if (len < 0) return -4;
      cbr = !(ch&0x80);
      if (!cbr) {
         last_size = len;
         for (i = 0; i < count-1; i++) {
            bytes = fl_parse_size(data, len, size+i);
            len -= bytes;
            if (size[i] < 0 || size[i] > len) return -4;
            data += bytes;
            last_size -= bytes+size[i];
         }
         if (last_size < 0) return -4;
      } else if (!self_delimited) {
         last_size = len/count;
         if (last_size*count != len) return -4;
         for (i = 0; i < count-1; i++) size[i] = (fl_i16)last_size;
      }
      break;
   }
   if (self_delimited) {
      bytes = fl_parse_size(data, len, size+count-1);
      len -= bytes;
      if (size[count-1] < 0 || size[count-1] > len) return -4;
      data += bytes;
      if (cbr) {
         if (size[count-1]*count > len) return -4;
         for (i = 0; i < count-1; i++) size[i] = size[count-1];
      } else if (bytes+size[count-1] > last_size) return -4;
   } else {
      if (last_size > 1275) return -4;
      size[count-1] = (fl_i16)last_size;
   }
   if (payload_offset) *payload_offset = (int)(data-data0);
   for (i = 0; i < count; i++) data += size[i];
   if (packet_offset) *packet_offset = pad+(fl_i32)(data-data0);
   return count;
}

static void fl_smooth_fade(const float *in1, const float *in2, float *out, int overlap, int channels, fl_i32 Fs) {
   int i, c, inc = 48000/Fs;
   for (c = 0; c < channels; c++) {
      for (i = 0; i < overlap; i++) {
         float w = fl_window120[i*inc]*fl_window120[i*inc];
         out[i*channels+c] = w*in2[i*channels+c]+(1.0f-w)*in1[i*channels+c];
      }
   }
}

static void fl_opus_decoder_reset(struct fl_opus_decoder *st) {
   st->stream_channels = st->channels;
   st->bandwidth = st->mode = st->prev_mode = st->prev_redundancy = st->last_packet_duration = 0;
   st->rangeFinal = 0;
   fl_celt_dec_reset(&st->celt);
   silk_dec_reset_all(&st->silk);
   st->frame_size = st->Fs/400;
}

static int fl_opus_decoder_init_state(struct fl_opus_decoder *st, fl_i32 Fs, int channels) {
   if ((Fs != 48000 && Fs != 24000 && Fs != 16000 && Fs != 12000 && Fs != 8000) || (channels != 1 && channels != 2))
      return -1;
   memset(st, 0, sizeof(*st));
   st->stream_channels = st->channels = channels;
   st->Fs = Fs;
   st->DecControl.API_sampleRate = Fs;
   st->DecControl.nChannelsAPI = channels;
   silk_dec_init_all(&st->silk);
   fl_celt_dec_init(&st->celt, Fs, channels);
   st->prev_mode = 0;
   st->frame_size = Fs/400;
   return 0;
}

static int fl_decode_frame(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size, int decode_fec) {
   int i, silk_ret = 0, celt_ret = 0, audiosize, mode, bandwidth, transition = 0, start_band, redundancy = 0;
   int redundancy_bytes = 0, celt_to_silk = 0, c, F2_5, F5, F10, F20, celt_accum;
   fl_i32 silk_frame_size;
   ec_ctx dec;
   float pcm_transition_buf[960*2], *pcm_transition = NULL, redundant_audio[480];
   fl_u32 redundant_rng = 0;
   fl_celt_dec *celt = &st->celt;
   F20 = st->Fs/50;
   F10 = F20>>1;
   F5 = F10>>1;
   F2_5 = F5>>1;
   if (frame_size < F2_5) return -2;
   frame_size = IMIN(frame_size, st->Fs/25*3);
   if (len <= 1) {
      data = NULL;
      frame_size = IMIN(frame_size, st->frame_size);
   }
   if (data != NULL) {
      audiosize = st->frame_size;
      mode = st->mode;
      bandwidth = st->bandwidth;
      ec_dec_init(&dec, (unsigned char*)data, len);
   } else {
      audiosize = frame_size;
      mode = st->prev_redundancy ? MODE_CELT_ONLY : st->prev_mode;
      bandwidth = 0;
      if (mode == 0) {
         for (i = 0; i < audiosize*st->channels; i++) pcm[i] = 0;
         return audiosize;
      }
      if (audiosize > F20) {
         do {
            int ret = fl_decode_frame(st, NULL, 0, pcm, IMIN(audiosize, F20), 0);
            if (ret < 0) return ret;
            pcm += ret*st->channels;
            audiosize -= ret;
         } while (audiosize > 0);
         return frame_size;
      } else if (audiosize < F20) {
         if (audiosize > F10) audiosize = F10;
         else if (mode != MODE_SILK_ONLY && audiosize > F5 && audiosize < F10) audiosize = F5;
      }
   }
   celt_accum = (mode != MODE_CELT_ONLY);
   if (data != NULL && st->prev_mode > 0 && ((mode == MODE_CELT_ONLY && st->prev_mode != MODE_CELT_ONLY && !st->prev_redundancy)
         || (mode != MODE_CELT_ONLY && st->prev_mode == MODE_CELT_ONLY))) {
      transition = 1;
   }
   if (transition && mode == MODE_CELT_ONLY) {
      pcm_transition = pcm_transition_buf;
      fl_decode_frame(st, NULL, 0, pcm_transition, IMIN(F5, audiosize), 0);
   }
   if (audiosize > frame_size) return -1;
   frame_size = audiosize;
   if (mode != MODE_CELT_ONLY) {
      int lost_flag, decoded_samples, pcm_too_small;
      float *pcm_ptr, pcm_silk[960*2];
      pcm_too_small = (frame_size < F10);
      pcm_ptr = pcm_too_small ? pcm_silk : pcm;
      if (st->prev_mode == MODE_CELT_ONLY) silk_dec_reset_all(&st->silk);
      st->DecControl.payloadSize_ms = IMAX(10, 1000*audiosize/st->Fs);
      if (data != NULL) {
         st->DecControl.nChannelsInternal = st->stream_channels;
         if (mode == MODE_SILK_ONLY) {
            if (bandwidth == FL_BANDWIDTH_NARROWBAND) st->DecControl.internalSampleRate = 8000;
            else if (bandwidth == FL_BANDWIDTH_MEDIUMBAND) st->DecControl.internalSampleRate = 12000;
            else st->DecControl.internalSampleRate = 16000;
         } else st->DecControl.internalSampleRate = 16000;
      }
      lost_flag = data == NULL ? 1 : 2*!!decode_fec;
      decoded_samples = 0;
      do {
         int first_frame = decoded_samples == 0;
         silk_ret = silk_Decode(&st->silk, &st->DecControl, lost_flag, first_frame, &dec, pcm_ptr, &silk_frame_size);
         if (silk_ret) {
            if (lost_flag) {
               silk_frame_size = frame_size;
               for (i = 0; i < frame_size*st->channels; i++) pcm_ptr[i] = 0;
            } else return -3;
         }
         pcm_ptr += silk_frame_size*st->channels;
         decoded_samples += silk_frame_size;
      } while (decoded_samples < frame_size);
      if (pcm_too_small) FL_COPY(pcm, pcm_silk, frame_size*st->channels);
   }
   start_band = 0;
   if (!decode_fec && mode != MODE_CELT_ONLY && data != NULL && ec_tell(&dec)+17+20*(mode == MODE_HYBRID) <= 8*len) {
      if (mode == MODE_HYBRID) redundancy = ec_dec_bit_logp(&dec, 12);
      else redundancy = 1;
      if (redundancy) {
         celt_to_silk = ec_dec_bit_logp(&dec, 1);
         redundancy_bytes = mode == MODE_HYBRID ? (fl_i32)ec_dec_uint(&dec, 256)+2 : len-((ec_tell(&dec)+7)>>3);
         len -= redundancy_bytes;
         if (len*8 < ec_tell(&dec)) {
            len = 0;
            redundancy_bytes = 0;
            redundancy = 0;
         }
         dec.storage -= redundancy_bytes;
      }
   }
   if (mode != MODE_CELT_ONLY) start_band = 17;
   if (redundancy) transition = 0;
   if (transition && mode != MODE_CELT_ONLY) {
      pcm_transition = pcm_transition_buf;
      fl_decode_frame(st, NULL, 0, pcm_transition, IMIN(F5, audiosize), 0);
   }
   if (bandwidth) {
      int endband = 21;
      switch (bandwidth) {
      case FL_BANDWIDTH_NARROWBAND: endband = 13; break;
      case FL_BANDWIDTH_MEDIUMBAND:
      case FL_BANDWIDTH_WIDEBAND: endband = 17; break;
      case FL_BANDWIDTH_SUPERWIDEBAND: endband = 19; break;
      case FL_BANDWIDTH_FULLBAND: endband = 21; break;
      }
      celt->end = endband;
   }
   celt->stream_channels = st->stream_channels;
   if (redundancy && celt_to_silk) {
      celt->start = 0;
      fl_celt_decode(celt, data+len, redundancy_bytes, redundant_audio, F5, NULL, 0);
      redundant_rng = celt->rng;
   }
   celt->start = start_band;
   if (mode != MODE_SILK_ONLY) {
      int celt_frame_size = IMIN(F20, frame_size);
      if (mode != st->prev_mode && st->prev_mode > 0 && !st->prev_redundancy) fl_celt_dec_reset(celt);
      celt_ret = fl_celt_decode(celt, decode_fec ? NULL : data, len, pcm, celt_frame_size, &dec, celt_accum);
      st->rangeFinal = celt->rng;
   } else {
      unsigned char silence[2] = {0xFF, 0xFF};
      if (!celt_accum)
         for (i = 0; i < frame_size*st->channels; i++) pcm[i] = 0;
      if (st->prev_mode == MODE_HYBRID && !(redundancy && celt_to_silk && st->prev_redundancy)) {
         celt->start = 0;
         fl_celt_decode(celt, silence, 2, pcm, F2_5, NULL, celt_accum);
      }
      st->rangeFinal = dec.rng;
   }
   if (redundancy && !celt_to_silk) {
      fl_celt_dec_reset(celt);
      celt->start = 0;
      fl_celt_decode(celt, data+len, redundancy_bytes, redundant_audio, F5, NULL, 0);
      redundant_rng = celt->rng;
      fl_smooth_fade(pcm+st->channels*(frame_size-F2_5), redundant_audio+st->channels*F2_5,
            pcm+st->channels*(frame_size-F2_5), F2_5, st->channels, st->Fs);
   }
   if (redundancy && celt_to_silk && (st->prev_mode != MODE_SILK_ONLY || st->prev_redundancy)) {
      for (c = 0; c < st->channels; c++)
         for (i = 0; i < F2_5; i++) pcm[st->channels*i+c] = redundant_audio[st->channels*i+c];
      fl_smooth_fade(redundant_audio+st->channels*F2_5, pcm+st->channels*F2_5, pcm+st->channels*F2_5, F2_5, st->channels, st->Fs);
   }
   if (transition) {
      if (audiosize >= F5) {
         for (i = 0; i < st->channels*F2_5; i++) pcm[i] = pcm_transition[i];
         fl_smooth_fade(pcm_transition+st->channels*F2_5, pcm+st->channels*F2_5, pcm+st->channels*F2_5, F2_5, st->channels, st->Fs);
      } else {
         fl_smooth_fade(pcm_transition, pcm, pcm, F2_5, st->channels, st->Fs);
      }
   }
   if (st->decode_gain) {
      float gain = fl_celt_exp2(6.48814081e-4f*st->decode_gain);
      for (i = 0; i < frame_size*st->channels; i++) pcm[i] = pcm[i]*gain;
   }
   if (len <= 1) st->rangeFinal = 0;
   else st->rangeFinal ^= redundant_rng;
   st->prev_mode = mode;
   st->prev_redundancy = redundancy && !celt_to_silk;
   return celt_ret < 0 ? celt_ret : audiosize;
}

/* Decodes one packet (NULL for loss) into interleaved float. Returns samples per channel. */
static int fl_decode_native(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size, int self_delimited, fl_i32 *packet_offset) {
   int i, nb_samples, count, offset, packet_frame_size, packet_bandwidth, packet_mode, packet_stream_channels;
   fl_i16 size[48];
   if (frame_size <= 0) return -1;
   if (len == 0 || data == NULL) {
      int pcm_count = 0;
      if (frame_size%(st->Fs/400) != 0) return -1;
      do {
         int ret = fl_decode_frame(st, NULL, 0, pcm+pcm_count*st->channels, frame_size-pcm_count, 0);
         if (ret < 0) return ret;
         pcm_count += ret;
      } while (pcm_count < frame_size);
      st->last_packet_duration = pcm_count;
      return pcm_count;
   } else if (len < 0) return -1;
   packet_mode = fl_packet_get_mode(data);
   packet_bandwidth = fl_packet_get_bandwidth(data);
   packet_frame_size = fl_packet_get_samples_per_frame(data, st->Fs);
   packet_stream_channels = (data[0]&0x4) ? 2 : 1;
   count = fl_packet_parse(data, len, self_delimited, size, &offset, packet_offset);
   if (count < 0) return count;
   data += offset;
   if (count*packet_frame_size > frame_size) return -2;
   st->mode = packet_mode;
   st->bandwidth = packet_bandwidth;
   st->frame_size = packet_frame_size;
   st->stream_channels = packet_stream_channels;
   nb_samples = 0;
   for (i = 0; i < count; i++) {
      int ret = fl_decode_frame(st, data, size[i], pcm+nb_samples*st->channels, frame_size-nb_samples, 0);
      if (ret < 0) return ret;
      data += size[i];
      nb_samples += ret;
   }
   st->last_packet_duration = nb_samples;
   return nb_samples;
}

static int fl_opus_decode_float_internal(struct fl_opus_decoder *st, const unsigned char *data, fl_i32 len, float *pcm, int frame_size) {
   return fl_decode_native(st, data, len, pcm, frame_size, 0, NULL);
}

/* Soft clipping used by opusdec before 16-bit conversion. */
static void fl_pcm_soft_clip(float *_x, int N, int C, float *declip_mem) {
   int c, i;
   float *x;
   if (C < 1 || N < 1 || !_x || !declip_mem) return;
   for (i = 0; i < N*C; i++) {
      float v = FL_MAX(-2.0f, _x[i]);
      _x[i] = FL_MIN(2.0f, v);
   }
   for (c = 0; c < C; c++) {
      float a, x0;
      int curr;
      x = _x+c;
      a = declip_mem[c];
      for (i = 0; i < N; i++) {
         if (x[i*C]*a >= 0) break;
         x[i*C] = x[i*C]+a*x[i*C]*x[i*C];
      }
      curr = 0;
      x0 = x[0];
      while (1) {
         int start, end, special = 0, peak_pos;
         float maxval;
         for (i = curr; i < N; i++)
            if (x[i*C] > 1 || x[i*C] < -1) break;
         if (i == N) {
            a = 0;
            break;
         }
         peak_pos = i;
         start = end = i;
         maxval = (float)fabs(x[i*C]);
         while (start > 0 && x[i*C]*x[(start-1)*C] >= 0) start--;
         while (end < N && x[i*C]*x[end*C] >= 0) {
            if ((float)fabs(x[end*C]) > maxval) {
               maxval = (float)fabs(x[end*C]);
               peak_pos = end;
            }
            end++;
         }
         special = (start == 0 && x[i*C]*x[0] >= 0);
         a = (maxval-1)/(maxval*maxval);
         a += a*2.4e-7f;
         if (x[i*C] > 0) a = -a;
         for (i = start; i < end; i++) x[i*C] = x[i*C]+a*x[i*C]*x[i*C];
         if (special && peak_pos >= 2) {
            float delta, offset = x0-x[0];
            delta = offset/peak_pos;
            for (i = curr; i < peak_pos; i++) {
               offset -= delta;
               x[i*C] += offset;
               x[i*C] = FL_MAX(-1.f, FL_MIN(1.f, x[i*C]));
            }
         }
         curr = end;
         if (curr == N) break;
      }
      declip_mem[c] = a;
   }
}

/* ---- Public low-level decoder API ---- */
FLDEF fl_opus_decoder *fl_opus_decoder_create(int sample_rate, int channels, int *error) {
   fl_opus_decoder *st = (fl_opus_decoder*)malloc(sizeof(*st));
   if (!st) {
      if (error) *error = FL_ERR_ALLOC;
      return NULL;
   }
   if (fl_opus_decoder_init_state(st, sample_rate, channels) < 0) {
      free(st);
      if (error) *error = FL_ERR_BAD_ARG;
      return NULL;
   }
   if (error) *error = FL_OK;
   return st;
}
FLDEF void fl_opus_decoder_destroy(fl_opus_decoder *dec) { free(dec); }
FLDEF void fl_opus_decoder_reset_state(fl_opus_decoder *dec) { fl_opus_decoder_reset(dec); }
FLDEF void fl_opus_decoder_set_gain(fl_opus_decoder *dec, int gain_q8) {
   dec->decode_gain = gain_q8 < -32768 ? -32768 : (gain_q8 > 32767 ? 32767 : gain_q8);
}
FLDEF int fl_opus_decode_float(fl_opus_decoder *dec, const unsigned char *packet, int len, float *pcm, int max_frame_size) {
   return fl_opus_decode_float_internal(dec, packet, len, pcm, max_frame_size);
}

/* ---- Multistream decoder (RFC 7845 channel mapping, libopus opus_multistream_decoder.c) ---- */
struct fl_opus_ms_decoder {
   int channels, streams, coupled;
   fl_i32 Fs;
   unsigned char mapping[255];
   struct fl_opus_decoder **dec;
   float *buf;
};

FLDEF void fl_opus_ms_decoder_destroy(fl_opus_ms_decoder *st) {
   int s;
   if (!st) return;
   if (st->dec)
      for (s = 0; s < st->streams; s++) free(st->dec[s]);
   free(st->dec);
   free(st->buf);
   free(st);
}

FLDEF fl_opus_ms_decoder *fl_opus_ms_decoder_create(int sample_rate, int channels, int streams, int coupled, const unsigned char *mapping, int *error) {
   fl_opus_ms_decoder *st;
   int c, s;
   if (error) *error = FL_ERR_BAD_ARG;
   if (channels < 1 || channels > 255 || streams < 1 || coupled < 0 || coupled > streams || streams+coupled > 255) return NULL;
   for (c = 0; c < channels; c++)
      if (mapping[c] >= streams+coupled && mapping[c] != 255) return NULL;
   st = (fl_opus_ms_decoder*)calloc(1, sizeof(*st));
   if (error) *error = FL_ERR_ALLOC;
   if (!st) return NULL;
   st->channels = channels;
   st->streams = streams;
   st->coupled = coupled;
   st->Fs = sample_rate;
   memcpy(st->mapping, mapping, channels);
   st->dec = (struct fl_opus_decoder**)calloc(streams, sizeof(*st->dec));
   st->buf = (float*)malloc(sizeof(float)*2*(sample_rate/25*3));
   if (!st->dec || !st->buf) {
      fl_opus_ms_decoder_destroy(st);
      return NULL;
   }
   for (s = 0; s < streams; s++) {
      st->dec[s] = (struct fl_opus_decoder*)malloc(sizeof(struct fl_opus_decoder));
      if (!st->dec[s]) {
         fl_opus_ms_decoder_destroy(st);
         return NULL;
      }
      if (fl_opus_decoder_init_state(st->dec[s], sample_rate, s < coupled ? 2 : 1) < 0) {
         fl_opus_ms_decoder_destroy(st);
         if (error) *error = FL_ERR_BAD_ARG;
         return NULL;
      }
   }
   if (error) *error = FL_OK;
   return st;
}

FLDEF void fl_opus_ms_decoder_reset_state(fl_opus_ms_decoder *st) {
   int s;
   for (s = 0; s < st->streams; s++) fl_opus_decoder_reset(st->dec[s]);
}

FLDEF void fl_opus_ms_decoder_set_gain(fl_opus_ms_decoder *st, int gain_q8) {
   int s;
   for (s = 0; s < st->streams; s++) fl_opus_decoder_set_gain(st->dec[s], gain_q8);
}

/* Checks that all streams parse and carry the same duration; returns it. */
static int fl_ms_packet_validate(const unsigned char *data, fl_i32 len, int streams, fl_i32 Fs) {
   int s, samples = 0;
   for (s = 0; s < streams; s++) {
      fl_i16 size[48];
      fl_i32 packet_offset;
      int count, tmp;
      if (len <= 0) return -4;
      count = fl_packet_parse(data, len, s != streams-1, size, NULL, &packet_offset);
      if (count < 0) return count;
      tmp = count*fl_packet_get_samples_per_frame(data, Fs);
      if (s != 0 && samples != tmp) return -4;
      samples = tmp;
      data += packet_offset;
      len -= packet_offset;
   }
   return samples;
}

FLDEF int fl_opus_ms_decode_float(fl_opus_ms_decoder *st, const unsigned char *data, int len, float *pcm, int frame_size) {
   int s, c, i, do_plc = len == 0 || data == NULL;
   if (frame_size <= 0 || len < 0) return FL_ERR_BAD_ARG;
   frame_size = FL_MIN(frame_size, st->Fs/25*3);
   if (!do_plc) {
      int ret;
      if (len < 2*st->streams-1) return FL_ERR_BAD_FILE;
      ret = fl_ms_packet_validate(data, len, st->streams, st->Fs);
      if (ret < 0) return FL_ERR_BAD_FILE;
      if (ret > frame_size) return FL_ERR_BAD_ARG;
   }
   for (s = 0; s < st->streams; s++) {
      fl_i32 packet_offset = 0;
      int ret, sc = s < st->coupled ? 2 : 1;
      if (!do_plc && len <= 0) return FL_ERR_BAD_FILE;
      ret = fl_decode_native(st->dec[s], do_plc ? NULL : data, do_plc ? 0 : len, st->buf, frame_size, s != st->streams-1, &packet_offset);
      if (!do_plc) {
         data += packet_offset;
         len -= packet_offset;
      }
      if (ret <= 0) return ret < 0 ? FL_ERR_BAD_FILE : ret;
      frame_size = ret;
      /* route this stream's channel(s) to every output channel mapped to it */
      for (c = 0; c < st->channels; c++) {
         int m = st->mapping[c], src = -1;
         if (s < st->coupled && m < 2*st->coupled && m>>1 == s) src = m&1;
         else if (s >= st->coupled && m == s+st->coupled) src = 0;
         if (src < 0) continue;
         for (i = 0; i < frame_size; i++) pcm[i*st->channels+c] = st->buf[i*sc+src];
      }
   }
   for (c = 0; c < st->channels; c++)
      if (st->mapping[c] == 255)
         for (i = 0; i < frame_size; i++) pcm[i*st->channels+c] = 0;
   return frame_size;
}
