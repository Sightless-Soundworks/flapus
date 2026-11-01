/* ---- File conversion and in-memory audio ---- */

FLDEF void fl_options_init(fl_options *opt) {
   memset(opt, 0, sizeof(*opt));
   opt->dither = 1;
   opt->flac_level = 8;
   opt->threads = 1;
   opt->complexity = 10;
   opt->frame_ms_x10 = 200;
   opt->vbr = 1;
   opt->quality = -1;
   opt->padding = 512;
   opt->serialno = -1;
}

/* Detects the input format from the first bytes of the file. */
static int fl_detect_format(const char *path) {
   unsigned char b[4096];
   size_t n, i;
   FILE *f = fl_fopen(path, "rb");
   if (!f) return FL_ERR_IO;
   n = fread(b, 1, sizeof(b), f);
   fclose(f);
   if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b+8, "WAVE", 4) == 0) return FL_FORMAT_WAV;
   if (n >= 4 && (memcmp(b, "fLaC", 4) == 0 || memcmp(b, "ID3", 3) == 0)) return FL_FORMAT_FLAC;
   if (n >= 4 && memcmp(b, "OggS", 4) == 0) {
      /* look at the first packets of the BOS pages */
      for (i = 0; i+8 <= n; i++) {
         if (memcmp(b+i, "OpusHead", 8) == 0) return FL_FORMAT_OPUS;
         if (memcmp(b+i, "\177FLAC", 5) == 0) return FL_FORMAT_FLAC;
      }
   }
   return FL_ERR_UNSUPPORTED;
}

static int fl_ext_is(const char *path, const char *ext) {
   size_t n = strlen(path), e = strlen(ext), i;
   if (n < e) return 0;
   for (i = 0; i < e; i++) {
      int c = path[n-e+i];
      if (c >= 'A' && c <= 'Z') c += 32;
      if (c != ext[i]) return 0;
   }
   return 1;
}

static int fl_format_from_name(const char *path) {
   if (fl_ext_is(path, ".wav")) return FL_FORMAT_WAV;
   if (fl_ext_is(path, ".flac") || fl_ext_is(path, ".fla")) return FL_FORMAT_FLAC;
   if (fl_ext_is(path, ".opus") || fl_ext_is(path, ".ogg")) return FL_FORMAT_OPUS;
   return FL_ERR_BAD_ARG;
}

static fl_sink *fl_create_sink(int format, const char *path, const fl_options *opt, int *err) {
   *err = FL_ERR_BAD_ARG;
   if (format == FL_FORMAT_WAV) return fl_wav_sink_create(path, err);
   if (format == FL_FORMAT_FLAC) return fl_flac_sink_create(path, opt, err);
#ifdef FLAPUS_HAVE_ENCODER
   if (format == FL_FORMAT_OPUS) return fl_opus_sink_create(path, opt, err);
#else
   if (format == FL_FORMAT_OPUS) *err = FL_ERR_UNSUPPORTED;
#endif
   return NULL;
}

/* Sink wrapper that reports progress to opt->progress. */
typedef struct {
   fl_sink base;
   fl_sink *inner;
   const fl_options *opt;
   fl_i64 total, done;
   double pcm_bytes;  /* input PCM bytes per frame */
} fl_progress_sink;

static void fl_progress_report(fl_progress_sink *p, int final) {
   fl_sink *in = p->inner;
   double done = final ? 1.0 : p->total > 0 ? FL_MIN(1.0, (double)p->done/p->total) : -1.0, ratio = -1.0;
   if (in->out_frames > 0) ratio = in->out_bytes/(in->out_frames*p->pcm_bytes);
   p->opt->progress(p->opt->progress_user, done, ratio);
}

static int fl_progress_start(fl_sink *s, const fl_format *f) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->start(p->inner, f);
   p->total = f->frames;
   p->pcm_bytes = (double)f->channels*((f->bits+7)/8);
   if (ret == FL_OK) fl_progress_report(p, 0);
   return ret;
}

static int fl_progress_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->write(p->inner, pcm, n);
   p->done += n;
   if (ret == FL_OK) fl_progress_report(p, 0);
   return ret;
}

static int fl_progress_finish(fl_sink *s) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   int ret = p->inner->finish(p->inner);
   if (ret == FL_OK) fl_progress_report(p, 1);
   return ret;
}

static void fl_progress_destroy(fl_sink *s) {
   fl_progress_sink *p = (fl_progress_sink*)s;
   p->inner->destroy(p->inner);
   free(p);
}

static fl_sink *fl_progress_wrap(fl_sink *inner, const fl_options *opt, int *err) {
   fl_progress_sink *p;
   if (!opt->progress) return inner;
   if (!(p = (fl_progress_sink*)calloc(1, sizeof(*p)))) {
      inner->destroy(inner);
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   p->inner = inner;
   p->opt = opt;
   p->base.start = fl_progress_start;
   p->base.write = fl_progress_write;
   p->base.finish = fl_progress_finish;
   p->base.destroy = fl_progress_destroy;
   return &p->base;
}

static int fl_run_source(int format, const char *path, const fl_options *opt, fl_sink *sink) {
   if (format == FL_FORMAT_WAV) return fl_run_wav(path, sink);
   if (format == FL_FORMAT_FLAC) return fl_run_flac(path, sink);
   return fl_run_opus(path, opt->rate, opt->dither, sink);
}

FLDEF int fl_convert_file(const char *in_path, const char *out_path, const fl_options *opt_in) {
   fl_options opt;
   fl_sink *sink;
   int in_fmt, out_fmt, ret, err;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if (strcmp(in_path, out_path) == 0) return FL_ERR_BAD_ARG;
   in_fmt = fl_detect_format(in_path);
   if (in_fmt < 0) return in_fmt;
   out_fmt = opt.format ? opt.format : fl_format_from_name(out_path);
   if (out_fmt < 0) return out_fmt;
   if (!(sink = fl_create_sink(out_fmt, out_path, &opt, &err))) return err;
   if (!(sink = fl_progress_wrap(sink, &opt, &err))) {
      fl_remove(out_path);
      return err;
   }
   ret = fl_run_source(in_fmt, in_path, &opt, sink);
   sink->destroy(sink);
   /* a damaged FLAC input still gives complete output; anything else is removed */
   if (ret != FL_OK && ret != FL_ERR_CHECKSUM) fl_remove(out_path);
   return ret;
}

FLDEF int fl_read_audio(const char *path, const fl_options *opt_in, fl_audio *audio) {
   fl_options opt;
   fl_mem_sink m;
   int fmt, ret;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   memset(audio, 0, sizeof(*audio));
   fmt = fl_detect_format(path);
   if (fmt < 0) return fmt;
   memset(&m, 0, sizeof(m));
   m.base.start = fl_mem_start;
   m.base.write = fl_mem_write;
   m.base.finish = fl_mem_finish;
   m.base.destroy = fl_mem_destroy;
   m.a = audio;
   ret = fl_run_source(fmt, path, &opt, &m.base);
   if (ret != FL_OK && ret != FL_ERR_CHECKSUM) fl_audio_free(audio);
   return ret;
}

FLDEF int fl_write_audio(const char *path, const fl_audio *a, const fl_options *opt_in) {
   fl_options opt;
   fl_format f;
   fl_sink *sink;
   int out_fmt, ret, err;
   long long pos = 0;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   out_fmt = opt.format ? opt.format : fl_format_from_name(path);
   if (out_fmt < 0) return out_fmt;
   if (a->channels < 1 || a->bits < 1 || a->bits > 32 || a->rate < 1 || a->frames < 0) return FL_ERR_BAD_ARG;
   if (!(sink = fl_create_sink(out_fmt, path, &opt, &err))) return err;
   fl_format_init(&f);
   f.channels = a->channels;
   f.rate = a->rate;
   f.bits = f.container_bits = a->is_float ? 32 : a->bits;
   f.is_float = a->is_float;
   f.frames = a->frames;
   ret = sink->start(sink, &f);
   while (ret == FL_OK && pos < a->frames) {
      int n = a->frames-pos < 4096 ? (int)(a->frames-pos) : 4096;
      ret = sink->write(sink, a->samples+(size_t)pos*a->channels, n);
      pos += n;
   }
   if (ret == FL_OK) ret = sink->finish(sink);
   sink->destroy(sink);
   if (ret != FL_OK) fl_remove(path);
   return ret;
}

FLDEF void fl_audio_free(fl_audio *audio) {
   free(audio->samples);
   memset(audio, 0, sizeof(*audio));
}
