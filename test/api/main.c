/* Uses only the public API: Opus packet round trip, FLAC in-memory round trip, file conversion. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../flapus.h"

int main(void) {
   int C, err, n, i, f, streams, coupled, family;
   for (C = 1; C <= 8; C += (C < 2 ? 1 : 3)) {
      fl_opus_encoder *enc = fl_opus_encoder_create(C, NULL, &err);
      unsigned char mapping[255], packet[7664*8];
      fl_opus_ms_decoder *dec;
      float *pcm, *out;
      double sig = 0, noise = 0;
      int fs, delay;
      if (!enc) return printf("create failed %d\n", err), 1;
      fs = fl_opus_encoder_frame_size(enc);
      delay = fl_opus_encoder_lookahead(enc);
      fl_opus_encoder_layout(enc, &family, &streams, &coupled, mapping);
      dec = fl_opus_ms_decoder_create(48000, C, streams, coupled, mapping, &err);
      pcm = (float*)malloc(sizeof(float)*fs*C*100);
      out = (float*)malloc(sizeof(float)*fs*C*100);
      for (i = 0; i < fs*100; i++)
         for (n = 0; n < C; n++) pcm[i*C+n] = (float)(.3*sin(i*(.01+.003*n)));
      for (f = 0; f < 100; f++) {
         int len = fl_opus_encode_float(enc, pcm+f*fs*C, packet, sizeof(packet));
         if (len < 0 || fl_opus_ms_decode_float(dec, packet, len, out+f*fs*C, fs) != fs) return printf("codec error\n"), 1;
      }
      for (i = 10*fs; i < 100*fs-delay; i++)
         for (n = 0; n < C; n++) {
            double d = out[(i+delay)*C+n]-pcm[i*C+n];
            sig += pcm[i*C+n]*pcm[i*C+n];
            noise += d*d;
         }
      printf("channels %d family %d streams %d coupled %d frame %d delay %d SNR %.1f dB\n", C, family, streams, coupled, fs, delay,
         10*log10(sig/noise));
      fl_opus_encoder_destroy(enc);
      fl_opus_ms_decoder_destroy(dec);
      free(pcm);
      free(out);
   }
   {
      /* FLAC: memory -> file -> memory must give the same samples; then FLAC -> Opus -> memory */
      fl_audio a, b;
      fl_options opt;
      int bits;
      fl_options_init(&opt);
      opt.flac_level = 5;
      for (bits = 8; bits <= 24; bits += 4) {
         long long k;
         a.channels = 2;
         a.rate = 44100;
         a.bits = bits;
         a.is_float = 0;
         a.frames = 30000;
         a.samples = (int*)malloc(sizeof(int)*a.frames*2);
         for (k = 0; k < a.frames*2; k++)
            a.samples[k] = (int)(((1<<(bits-1))-1)*(.5*sin(k*.0123)+.01*(rand()%100-50)/50.0));
         if ((err = fl_write_audio("api_test.flac", &a, &opt)) != FL_OK) return printf("FLAC write error %d\n", err), 1;
         if ((err = fl_read_audio("api_test.flac", NULL, &b)) != FL_OK) return printf("FLAC read error %d\n", err), 1;
         for (k = 0; k < a.frames*2 && b.frames == a.frames && a.samples[k] == b.samples[k]; k++);
         printf("FLAC %2d-bit round trip: %s\n", bits, k == a.frames*2 && b.bits == bits ? "identical" : "DIFFERENT");
         free(a.samples);
         fl_audio_free(&b);
      }
#ifdef FLAPUS_HAVE_ENCODER
      if ((err = fl_convert_file("api_test.flac", "api_test.opus", NULL)) != FL_OK) return printf("convert error %d\n", err), 1;
      if ((err = fl_read_audio("api_test.opus", NULL, &b)) != FL_OK) return printf("Opus read error %d\n", err), 1;
      printf("FLAC -> Opus -> memory: %lld frames, %d channels, %d Hz, %d-bit\n", b.frames, b.channels, b.rate, b.bits);
      fl_audio_free(&b);
      remove("api_test.opus");
#endif
      remove("api_test.flac");
   }
   return 0;
}
