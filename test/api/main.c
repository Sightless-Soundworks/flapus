/* Uses only the public API: Opus packet round trip, FLAC in-memory round trip, file conversion,
   step-by-step reading and writing with seeks, custom I/O and FLAC frames. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../flapus.h"

/* Custom I/O over a growable memory buffer. */
typedef struct {
   unsigned char *p;
   size_t len, cap, pos;
} membuf;

static size_t mem_read(void *u, void *buf, size_t n) {
   membuf *m = (membuf*)u;
   if (n > m->len-m->pos) n = m->len-m->pos;
   memcpy(buf, m->p+m->pos, n);
   m->pos += n;
   return n;
}

static size_t mem_write(void *u, const void *buf, size_t n) {
   membuf *m = (membuf*)u;
   if (m->pos+n > m->cap) {
      m->cap = 2*(m->pos+n);
      m->p = (unsigned char*)realloc(m->p, m->cap);
   }
   memcpy(m->p+m->pos, buf, n);
   m->pos += n;
   if (m->pos > m->len) m->len = m->pos;
   return n;
}

static int mem_seek(void *u, long long off) {
   membuf *m = (membuf*)u;
   if (off < 0 || (size_t)off > m->len) return -1;
   m->pos = (size_t)off;
   return 0;
}

static int load(const char *path, membuf *m) {
   FILE *f = fopen(path, "rb");
   memset(m, 0, sizeof(*m));
   if (!f) return 0;
   fseek(f, 0, SEEK_END);
   m->len = m->cap = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   m->p = (unsigned char*)malloc(m->len+1);
   m->len = fread(m->p, 1, m->len, f);
   fclose(f);
   return 1;
}

/* Frames [at, at+n) from r compared with the reference: max difference, or -1 if short. */
static double compare(fl_reader *r, const fl_audio *ref, long long at, int n) {
   int *buf = (int*)malloc(sizeof(int)*n*ref->channels), got = 0, k;
   double worst = 0;
   while (got < n) {
      k = fl_reader_read(r, buf+(size_t)got*ref->channels, n-got);
      if (k <= 0) break;
      got += k;
   }
   if (got < n && at+got < ref->frames) worst = -1;
   for (k = 0; worst >= 0 && k < got*ref->channels; k++) {
      double d = fabs((double)buf[k]-ref->samples[at*ref->channels+k]);
      if (d > worst) worst = d;
   }
   free(buf);
   return worst;
}

/* Reading in chunks and after seeks must give the reference samples (Opus: close to them). */
static int test_reader(const char *path, int exact) {
   static const long long pos[6] = {0, 12345, 150000, 3000, 199000, 70001};
   fl_audio ref;
   fl_reader *r;
   fl_info info;
   membuf m;
   fl_io io;
   int err, i, mode, fails = 0;
   if (fl_read_audio(path, NULL, &ref) != FL_OK) return printf("%s: read error\n", path), 1;
   load(path, &m);
   for (mode = 0; mode < 3; mode++) {
      io.user = &m;
      io.read = mem_read;
      io.write = NULL;
      io.seek = mem_seek;
      m.pos = 0;
      r = mode == 0 ? fl_reader_open(path, NULL, &err) : mode == 1 ? fl_reader_open_memory(m.p, m.len, NULL, &err) :
          fl_reader_open_io(&io, NULL, &err);
      if (!r) return printf("%s: open error %d\n", path, err), 1;
      fl_reader_info(r, &info);
      if (info.channels != ref.channels || info.rate != ref.rate || info.bits != ref.bits) fails++;
      if (compare(r, &ref, 0, (int)ref.frames) != 0) fails++;  /* everything in one go */
      for (i = 0; i < 6; i++) {
         double d;
         if (fl_reader_seek(r, pos[i]) != FL_OK) {
            fails++;
            continue;
         }
         d = compare(r, &ref, pos[i], 1000);
         /* Opus restarts the decoder 80 ms early: allow a small difference in 16-bit units */
         if (d < 0 || d > (exact ? 0 : 64)) {
            printf("%s: mode %d seek %lld differs by %g\n", path, mode, pos[i], d);
            fails++;
         }
      }
      fl_reader_close(r);
   }
   printf("reader %-16s %s\n", path, fails ? "FAILED" : "ok");
   free(m.p);
   fl_audio_free(&ref);
   return fails > 0;
}

static int same_files(const char *a, const char *b) {
   membuf x, y;
   int same = load(a, &x) && load(b, &y) && x.len == y.len && memcmp(x.p, y.p, x.len) == 0;
   free(x.p);
   free(y.p);
   return same;
}

static int test_stream(void) {
   fl_audio a, b;
   fl_options opt;
   fl_info info;
   fl_writer *w;
   membuf in, out;
   fl_io iio, oio;
   long long k;
   int err, fails = 0, fmt;
   const char *names[3] = {"api_s.wav", "api_s.flac", "api_s.opus"};
   a.channels = 2;
   a.rate = 44100;
   a.bits = 16;
   a.is_float = 0;
   a.frames = 200000;
   a.samples = (int*)malloc(sizeof(int)*a.frames*2);
   for (k = 0; k < a.frames*2; k++)
      a.samples[k] = (int)(12000*sin(k*.0123+.002*k*(k&1))+(rand()%200-100));
   fl_options_init(&opt);
   opt.flac_level = 1;
   opt.serialno = 7;
   for (fmt = 0; fmt < 3; fmt++) {
#ifndef FLAPUS_HAVE_ENCODER
      if (fmt == 2) break;
#endif
      /* fl_writer in odd chunks gives the same file as fl_write_audio */
      if ((err = fl_write_audio(names[fmt], &a, &opt)) != FL_OK) return printf("write error %d\n", err), 1;
      info.channels = a.channels;
      info.rate = a.rate;
      info.bits = a.bits;
      info.is_float = 0;
      info.frames = a.frames;
      if (!(w = fl_writer_open("api_w.tmp", &info, &opt, &err))) {
         /* no extension: the format must come from opt */
         if (err != FL_ERR_BAD_ARG) fails++;
      }
      opt.format = fmt+1;
      if (!(w = fl_writer_open("api_w.tmp", &info, &opt, &err))) return printf("writer error %d\n", err), 1;
      for (k = 0; k < a.frames; k += 777) fl_writer_write(w, a.samples+k*2, (int)(a.frames-k < 777 ? a.frames-k : 777));
      if ((err = fl_writer_close(w)) != FL_OK || !same_files("api_w.tmp", names[fmt])) {
         printf("writer %s: %s\n", names[fmt], err ? "error" : "DIFFERENT");
         fails++;
      }
      /* fl_convert_io between seekable memory buffers gives the same file as fl_convert_file */
      load(names[fmt], &in);
      memset(&out, 0, sizeof(out));
      iio.user = &in;
      iio.read = mem_read;
      iio.write = NULL;
      iio.seek = mem_seek;
      oio.user = &out;
      oio.read = mem_read;
      oio.write = mem_write;
      oio.seek = mem_seek;
      opt.format = FL_FORMAT_FLAC;
      if ((err = fl_convert_io(&iio, &oio, &opt)) != FL_OK || fl_convert_file(names[fmt], "api_c.flac", &opt) != FL_OK) {
         printf("convert_io %s: error %d\n", names[fmt], err);
         fails++;
      } else {
         FILE *f = fopen("api_m.flac", "wb");
         fwrite(out.p, 1, out.len, f);
         fclose(f);
         if (!same_files("api_m.flac", "api_c.flac")) {
            printf("convert_io %s: DIFFERENT\n", names[fmt]);
            fails++;
         }
      }
      opt.format = 0;
      free(in.p);
      free(out.p);
      remove("api_w.tmp");
      remove("api_m.flac");
      fails += test_reader(names[fmt], fmt < 2);
   }
   fails += test_reader("api_c.flac", 1);
   /* FLAC frames: varying block sizes, the STREAMINFO of the encoder, both decoders; the
      encoder verifies its frames */
   {
      static const int sizes[6] = {4096, 1000, 4608, 17, 16384, 300};
      fl_options fopt;
      fl_flac_frame_encoder *e;
      fl_flac_frame_decoder *d;
      unsigned char *buf = (unsigned char*)malloc(1<<22), info34[34];
      int *pcm = (int*)malloc(sizeof(int)*65536*2), used, n, i;
      size_t len = 42;
      int bad = 0;
      FILE *f;
      fl_options_init(&fopt);
      fopt.flac_verify = 1;
      e = fl_flac_frame_encoder_create(2, 44100, 16, &fopt, &err);
      if (!e) return printf("frame encoder error %d\n", err), 1;
      k = 0;
      for (i = 0; k < a.frames; i++) {
         n = (int)(a.frames-k < sizes[i%6] ? a.frames-k : sizes[i%6]);
         if ((err = fl_flac_frame_encode(e, a.samples+k*2, n, buf+len, (1<<22)-(int)len)) < 0) return printf("frame error %d\n", err), 1;
         len += err;
         k += n;
      }
      fl_flac_frame_encoder_streaminfo(e, info34);
      fl_flac_frame_encoder_destroy(e);
      memcpy(buf, "fLaC\x80\0\0\x22", 8);
      memcpy(buf+8, info34, 34);
      f = fopen("api_f.flac", "wb");
      fwrite(buf, 1, len, f);
      fclose(f);
      if ((err = fl_read_audio("api_f.flac", NULL, &b)) != FL_OK || b.frames != a.frames ||
          memcmp(a.samples, b.samples, sizeof(int)*a.frames*2) != 0) {
         printf("FLAC frames: %s\n", err ? "read error (MD5?)" : "DIFFERENT");
         bad++;
      }
      fl_audio_free(&b);
      if (!(d = fl_flac_frame_decoder_create(info34, &err))) return printf("frame decoder error %d\n", err), 1;
      for (k = 0, i = 42; (size_t)i < len; i += used, k += n)
         if ((n = fl_flac_frame_decode(d, buf+i, (int)len-i, pcm, 65536, &used)) <= 0 ||
             memcmp(pcm, a.samples+k*2, sizeof(int)*n*2) != 0) {
            bad++;
            break;
         }
      printf("FLAC frames: %s\n", bad ? "FAILED" : "ok");
      fails += bad;
      fl_flac_frame_decoder_destroy(d);
      free(buf);
      free(pcm);
      remove("api_f.flac");
   }
   remove("api_c.flac");
   for (fmt = 0; fmt < 3; fmt++) remove(names[fmt]);
   free(a.samples);
   return fails;
}

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
      /* 18 bits are outside the subset: refused without flac_lax */
      a.bits = 18;
      a.frames = 1000;
      a.samples = (int*)calloc((size_t)a.frames*2, sizeof(int));
      err = fl_write_audio("api_test18.flac", &a, &opt);
      opt.flac_lax = 1;
      if (err != FL_ERR_NOT_SUBSET || fl_write_audio("api_test18.flac", &a, &opt) != FL_OK) return printf("FLAC 18-bit: FAILED\n"), 1;
      printf("FLAC 18-bit: refused without lax, ok with lax\n");
      opt.flac_lax = 0;
      free(a.samples);
      remove("api_test18.flac");
#ifdef FLAPUS_HAVE_ENCODER
      if ((err = fl_convert_file("api_test.flac", "api_test.opus", NULL)) != FL_OK) return printf("convert error %d\n", err), 1;
      if ((err = fl_read_audio("api_test.opus", NULL, &b)) != FL_OK) return printf("Opus read error %d\n", err), 1;
      printf("FLAC -> Opus -> memory: %lld frames, %d channels, %d Hz, %d-bit\n", b.frames, b.channels, b.rate, b.bits);
      fl_audio_free(&b);
      remove("api_test.opus");
#endif
      remove("api_test.flac");
   }
   if (test_stream()) return printf("stream tests FAILED\n"), 1;
   return 0;
}
