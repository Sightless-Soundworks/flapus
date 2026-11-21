/* ---- File conversion and in-memory audio ---- */

FLDEF void fl_options_init(fl_options *opt) {
   memset(opt, 0, sizeof(*opt));
   opt->dither = 1;
   opt->flac_level = 5;
   opt->threads = 1;
   opt->complexity = 10;
   opt->frame_ms_x10 = 200;
   opt->vbr = 1;
   opt->quality = -1;
   opt->flac_seektable = 1;
   opt->serialno = -1;
}

FLDEF const char *fl_error_text(int err) {
   switch (err) {
   case FL_OK: return "no error";
   case FL_ERR_IO: return "cannot read or write a file";
   case FL_ERR_BAD_FILE: return "invalid or corrupt input file";
   case FL_ERR_UNSUPPORTED: return "unsupported input format or output combination";
   case FL_ERR_ALLOC: return "out of memory";
   case FL_ERR_BAD_ARG: return "invalid option value or unknown output format";
   case FL_ERR_CHECKSUM: return "MD5 mismatch: the FLAC input is damaged";
   case FL_ERR_INTERNAL: return "internal FLAC encoder check failed";
   default: return "unknown error";
   }
}

/* Detects the input format from the first bytes of the file (they stay in the buffer). */
static int fl_detect_format(fl_inbuf *in) {
   const unsigned char *b;
   size_t n, i;
   if (fl_inbuf_need(in, 4096) < 0) return FL_ERR_ALLOC;
   b = in->buf+in->pos;
   n = FL_MIN(in->len-in->pos, 4096);
   if (fl_is_wav(b, n)) return FL_FORMAT_WAV;
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

/* A sink for format writing to path or (path NULL) io; it owns the output. */
static fl_sink *fl_create_sink(int format, const char *path, const fl_io *io, const fl_options *opt, int *err) {
   fl_out *out;
   fl_sink *s = NULL;
#ifndef FLAPUS_HAVE_ENCODER
   if (format == FL_FORMAT_OPUS) {
      *err = FL_ERR_UNSUPPORTED;
      return NULL;
   }
#endif
   if (format < FL_FORMAT_WAV || format > FL_FORMAT_OPUS) {
      *err = FL_ERR_BAD_ARG;
      return NULL;
   }
   /* WAV output is read back when it has to become RF64 at the end */
   if (!(out = path ? fl_out_file(path, format == FL_FORMAT_WAV) : fl_out_from_io(io))) {
      *err = FL_ERR_IO;
      return NULL;
   }
   if (format == FL_FORMAT_WAV) s = fl_wav_sink_create(out, err);
   if (format == FL_FORMAT_FLAC) s = fl_flac_sink_create(out, opt, err);
#ifdef FLAPUS_HAVE_ENCODER
   if (format == FL_FORMAT_OPUS) s = fl_opus_sink_create(out, opt, err);
#endif
   if (!s) fl_out_close(out);
   return s;
}

/* Sink wrapper that reports progress to opt->progress. */
typedef struct {
   fl_sink base;
   fl_sink *inner;
   const fl_options *opt;
   fl_i64 total, done;
   double pcm_bytes;  /* input PCM bytes per frame */
   fl_i64 in_size;    /* FLAC or Opus input: its file size (0 = unknown); -1: WAV input */
} fl_progress_sink;

static void fl_progress_report(fl_progress_sink *p, int final) {
   fl_sink *in = p->inner;
   double done = final ? 1.0 : p->total > 0 ? FL_MIN(1.0, (double)p->done/p->total) : -1.0, ratio = -1.0;
   if (in->out_frames > 0) {
      if (p->in_size < 0) ratio = in->out_bytes/(in->out_frames*p->pcm_bytes);
      else if (p->in_size > 0 && (final || p->total > 0))
         ratio = in->out_bytes/(p->in_size*(final ? 1.0 : FL_MIN(1.0, (double)in->out_frames/p->total)));
   }
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

static fl_sink *fl_progress_wrap(fl_sink *inner, const fl_options *opt, fl_i64 in_size, int *err) {
   fl_progress_sink *p;
   if (!opt->progress) return inner;
   if (!(p = (fl_progress_sink*)calloc(1, sizeof(*p)))) {
      inner->destroy(inner);
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   p->inner = inner;
   p->opt = opt;
   p->in_size = in_size;
   p->base.start = fl_progress_start;
   p->base.write = fl_progress_write;
   p->base.finish = fl_progress_finish;
   p->base.destroy = fl_progress_destroy;
   return &p->base;
}

/* Sink wrapper that gives WAV and FLAC input the sample format of opt->bits and
   opt->as_float, converted the way Opus output is (fl_emit_chunk). */
typedef struct {
   fl_sink base;
   fl_sink *inner;
   int borrowed;                /* the caller destroys inner */
   int bits, as_float, dither;  /* requested format */
   int in_bits, in_float, C;    /* source format; in_bits 0: passed on unchanged */
   float *clipmem, *f, *g;
   fl_shapestate *ss;           /* one per 8 channels */
   fl_i32 *v;
   short *s;
   int cap;
} fl_format_sink;

static int fl_format_start(fl_sink *s, const fl_format *f) {
   fl_format_sink *p = (fl_format_sink*)s;
   fl_format g = *f;
   int i, groups = (f->channels+7)/8;
   if (p->as_float ? !f->is_float : p->bits && (f->is_float || p->bits != f->bits)) {
      p->in_bits = f->bits;
      p->in_float = f->is_float;
      p->C = f->channels;
      g.bits = g.container_bits = p->as_float ? 32 : p->bits;
      g.is_float = p->as_float;
      p->clipmem = (float*)calloc(f->channels, sizeof(float));
      p->ss = (fl_shapestate*)calloc(groups, sizeof(fl_shapestate));
      if (!p->clipmem || !p->ss) return FL_ERR_ALLOC;
      for (i = 0; i < groups; i++) p->ss[i].fs = f->rate;
   }
   return p->inner->start(p->inner, &g);
}

static int fl_format_put(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_format_sink *p = (fl_format_sink*)s;
   int i, c, k, C = p->C, N = n*C;
   if (!p->in_bits) return p->inner->write(p->inner, pcm, n);
   if (N > p->cap) {
      free(p->f);
      free(p->g);
      free(p->v);
      free(p->s);
      p->f = (float*)malloc(sizeof(float)*N);
      p->g = (float*)malloc(sizeof(float)*N);
      p->v = (fl_i32*)malloc(sizeof(fl_i32)*N);
      p->s = (short*)malloc(sizeof(short)*N);
      p->cap = p->f && p->g && p->v && p->s ? N : 0;
      if (!p->cap) return FL_ERR_ALLOC;
   }
   if (!p->in_float && !p->as_float && p->bits < p->in_bits && p->bits == 16 && p->dither) {
      /* no dither where the dropped bits are zero (16-bit audio in a wider file) */
      fl_i32 any = 0, mask = (fl_i32)(((fl_i64)1<<(p->in_bits-16))-1);
      for (i = 0; i < N; i++) any |= pcm[i]&mask;
      if (!any) {
         for (i = 0; i < N; i++) p->v[i] = (fl_i32)((fl_i64)pcm[i]>>(p->in_bits-16));
         return p->inner->write(p->inner, p->v, n);
      }
   }
   if (!p->in_float && !p->as_float && (p->bits > p->in_bits || p->bits > 16 || !p->dither)) {
      /* integer to integer: exact shift up, or rounding (ties to even) and clipping */
      int d = p->in_bits-p->bits;
      fl_i64 hi = ((fl_i64)1<<(p->bits-1))-1;
      for (i = 0; i < N; i++) {
         fl_i64 x = pcm[i];
         if (d <= 0) x *= (fl_i64)1<<-d;
         else {
            fl_i64 q = x>>d, r = x-q*((fl_i64)1<<d), half = (fl_i64)1<<(d-1);
            x = q+(r > half || (r == half && (q&1)));
         }
         p->v[i] = (fl_i32)FL_MIN(x, hi);
      }
      return p->inner->write(p->inner, p->v, n);
   }
   if (p->in_float) memcpy(p->f, pcm, sizeof(float)*N);
   else {
      float scale = 1.0f/(float)((fl_i64)1<<(p->in_bits-1));
      for (i = 0; i < N; i++) p->f[i] = (float)pcm[i]*scale;
   }
   if (p->as_float) return p->inner->write(p->inner, (const fl_i32*)p->f, n);
   fl_pcm_soft_clip(p->f, n, C, p->clipmem);
   if (p->bits > 16) {
      double scale = p->bits == 32 ? 2147483648.0 : 8388608.0;
      for (i = 0; i < N; i++) p->v[i] = (fl_i32)lrint(FL_MAX(-scale, FL_MIN(p->f[i]*scale, scale-1)));
   } else if (!p->dither) {
      for (i = 0; i < N; i++) p->v[i] = fl_float2int(FL_MAX(-32768, FL_MIN(p->f[i]*32768.f, 32767)));
   } else {
      /* opusdec's shaped dither, 8 channels at a time */
      for (c = 0; c < C; c += 8) {
         int m = FL_MIN(8, C-c);
         for (i = 0; i < n; i++)
            for (k = 0; k < m; k++) p->g[i*m+k] = p->f[i*C+c+k];
         fl_shape_dither_toshort(&p->ss[c/8], p->s, p->g, n, m);
         for (i = 0; i < n; i++)
            for (k = 0; k < m; k++) p->v[i*C+c+k] = p->s[i*m+k];
      }
   }
   return p->inner->write(p->inner, p->v, n);
}

/* The output counters are passed on for the ratio. */
static int fl_format_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_format_sink *p = (fl_format_sink*)s;
   int ret = fl_format_put(s, pcm, n);
   p->base.out_bytes = p->inner->out_bytes;
   p->base.out_frames = p->inner->out_frames;
   return ret;
}

static int fl_format_finish(fl_sink *s) {
   fl_format_sink *p = (fl_format_sink*)s;
   int ret = p->inner->finish(p->inner);
   p->base.out_bytes = p->inner->out_bytes;
   p->base.out_frames = p->inner->out_frames;
   return ret;
}

static void fl_format_destroy(fl_sink *s) {
   fl_format_sink *p = (fl_format_sink*)s;
   if (!p->borrowed) p->inner->destroy(p->inner);
   free(p->clipmem);
   free(p->ss);
   free(p->f);
   free(p->g);
   free(p->v);
   free(p->s);
   free(p);
}

/* Wraps inner when opt asks for a sample format (WAV and FLAC input only). */
static fl_sink *fl_format_wrap(fl_sink *inner, const fl_options *opt, int borrowed, int *err) {
   fl_format_sink *p;
   if (!opt->bits && !opt->as_float) return inner;
   if (!(p = (fl_format_sink*)calloc(1, sizeof(*p)))) {
      if (!borrowed) inner->destroy(inner);
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   p->inner = inner;
   p->borrowed = borrowed;
   p->bits = opt->bits;
   p->as_float = opt->as_float;
   p->dither = opt->dither;
   p->base.start = fl_format_start;
   p->base.write = fl_format_write;
   p->base.finish = fl_format_finish;
   p->base.destroy = fl_format_destroy;
   return &p->base;
}

/* Opens the source for format (from fl_detect_format). */
static int fl_source_open(int format, fl_inbuf *in, const fl_options *opt, fl_source **src) {
   if (format == FL_FORMAT_WAV) return fl_wav_open(in, src);
   if (format == FL_FORMAT_FLAC) return fl_flac_open(in, src);
   return fl_opus_open(in, opt, src);
}

/* Decodes the whole input into sink: start, write, finish. */
static int fl_run_source(int format, fl_inbuf *in, const fl_options *opt, fl_sink *sink) {
   fl_source *src;
   int ret = fl_source_open(format, in, opt, &src);
   if (ret != FL_OK) return ret;
   ret = sink->start(sink, &src->fmt);
   while (ret == FL_OK && (ret = src->step(src, sink)) > 0) ret = FL_OK;
   if (ret == FL_OK) ret = sink->finish(sink);
   if (ret == FL_OK) ret = src->end_status;
   src->close(src);
   return ret;
}

/* Output goes to a temporary file next to out_path, which replaces out_path only when
   the conversion succeeds. So a failed conversion leaves an existing file alone, and the
   input may be the output file itself. */
static int fl_finish_output(const char *tmp, const char *out_path, int ret) {
   if (!tmp) return ret;
   /* a damaged FLAC input still gives complete output */
   if ((ret == FL_OK || ret == FL_ERR_CHECKSUM) && fl_rename_over(tmp, out_path) == 0) return ret;
   fl_remove(tmp);
   return ret == FL_OK || ret == FL_ERR_CHECKSUM ? FL_ERR_IO : ret;
}

/* Times of a file, for fl_options.keep_time. */
typedef struct {
#ifdef _WIN32
   FILETIME created, accessed, modified;
#elif defined(__unix__) || defined(__APPLE__)
   struct stat st;
#endif
   int ok;
} fl_times;

#ifdef _WIN32
static HANDLE fl_open_handle(const char *path, DWORD access) {
   wchar_t *w = fl_widen(path);
   DWORD share = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
   HANDLE h;
   if (w) {
      h = CreateFileW(w, access, share, NULL, OPEN_EXISTING, 0, NULL);
      free(w);
   } else h = CreateFileA(path, access, share, NULL, OPEN_EXISTING, 0, NULL);
   return h;
}
#endif

static void fl_get_times(const char *path, fl_times *t) {
   memset(t, 0, sizeof(*t));
#ifdef _WIN32
   {
      HANDLE h = fl_open_handle(path, FILE_READ_ATTRIBUTES);
      if (h == INVALID_HANDLE_VALUE) return;
      t->ok = GetFileTime(h, &t->created, &t->accessed, &t->modified) != 0;
      CloseHandle(h);
   }
#elif defined(__unix__) || defined(__APPLE__)
   t->ok = stat(path, &t->st) == 0 && S_ISREG(t->st.st_mode);
#else
   (void)path;
#endif
}

/* Size of the file at path, or 0 if unknown. */
static fl_i64 fl_file_size(const char *path) {
#ifdef _WIN32
   BY_HANDLE_FILE_INFORMATION info;
   HANDLE h = fl_open_handle(path, FILE_READ_ATTRIBUTES);
   int ok;
   if (h == INVALID_HANDLE_VALUE) return 0;
   ok = GetFileInformationByHandle(h, &info) != 0;
   CloseHandle(h);
   return ok ? (fl_i64)((fl_u64)info.nFileSizeHigh<<32|info.nFileSizeLow) : 0;
#elif defined(__unix__) || defined(__APPLE__)
   struct stat st;
   return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (fl_i64)st.st_size : 0;
#else
   (void)path;
   return 0;
#endif
}

/* Gives path the access and modification times of t, and with created also its
   creation time (Windows and macOS; Linux cannot set it). */
static void fl_set_times(const char *path, const fl_times *t, int created) {
   if (!t->ok) return;
#ifdef _WIN32
   {
      HANDLE h = fl_open_handle(path, FILE_WRITE_ATTRIBUTES);
      if (h == INVALID_HANDLE_VALUE) return;
      SetFileTime(h, created ? &t->created : NULL, &t->accessed, &t->modified);
      CloseHandle(h);
   }
#elif defined(__unix__) || defined(__APPLE__)
#if defined(UTIME_OMIT) && defined(__APPLE__)
   {
      struct timespec ts[2];
      ts[0] = t->st.st_atimespec;
      ts[1] = t->st.st_mtimespec;
      utimensat(AT_FDCWD, path, ts, 0);
   }
#elif defined(UTIME_OMIT)
   {
      struct timespec ts[2];
      ts[0] = t->st.st_atim;
      ts[1] = t->st.st_mtim;
      utimensat(AT_FDCWD, path, ts, 0);
   }
#else
   {
      /* without nanosecond support: whole seconds */
      struct utimbuf u;
      u.actime = t->st.st_atime;
      u.modtime = t->st.st_mtime;
      utime(path, &u);
   }
#endif
#ifdef __APPLE__
   if (created) {
      struct attrlist al;
      struct timespec ts = t->st.st_birthtimespec;
      memset(&al, 0, sizeof(al));
      al.bitmapcount = ATTR_BIT_MAP_COUNT;
      al.commonattr = ATTR_CMN_CRTIME;
      setattrlist(path, &al, &ts, sizeof(ts), 0);
   }
#else
   (void)created;
#endif
#else
   (void)path;
   (void)created;
#endif
}

/* Converts in into a sink for out_path or (out_path NULL) out_io. in_size: the input's size
   (0 = unknown), for the ratio of FLAC and Opus input. same: set to 1 when input and output
   are the same format (FLAC or Opus). */
static int fl_convert(fl_inbuf *in, fl_i64 in_size, const char *out_path, const fl_io *out_io, const fl_options *opt_in,
                      int *same) {
   fl_options opt;
   fl_sink *sink;
   char *tmp = NULL;
   int in_fmt, out_fmt, ret, err;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if ((ret = fl_check_options(&opt)) != FL_OK || (ret = in_fmt = fl_detect_format(in)) < 0) goto done;
   out_fmt = ret = opt.format ? opt.format : out_path ? fl_format_from_name(out_path) : FL_ERR_BAD_ARG;
   if (ret < 0) goto done;
   if (same) *same = in_fmt == out_fmt && in_fmt != FL_FORMAT_WAV;
   if (out_path && !fl_is_std(out_path) && !(tmp = fl_temp_path(out_path))) {
      ret = FL_ERR_IO;
      goto done;
   }
   if (!(sink = fl_create_sink(out_fmt, tmp ? tmp : out_path, out_io, &opt, &err)) ||
       (in_fmt != FL_FORMAT_OPUS && out_fmt != FL_FORMAT_OPUS && !(sink = fl_format_wrap(sink, &opt, 0, &err))) ||
       !(sink = fl_progress_wrap(sink, &opt, in_fmt == FL_FORMAT_WAV ? -1 : in_size, &err))) {
      ret = err;
   } else {
      ret = fl_run_source(in_fmt, in, &opt, sink);
      sink->destroy(sink);
   }
done:
   fl_inbuf_close(in);  /* before the rename, which needs the input closed on Windows */
   if (tmp) {
      ret = fl_finish_output(tmp, out_path, ret);
      free(tmp);
   }
   return ret;
}

FLDEF int fl_convert_file(const char *in_path, const char *out_path, const fl_options *opt) {
   fl_inbuf in;
   fl_times t;
   int ret, same = 0;
   if (opt && (ret = fl_check_options(opt)) != FL_OK) return ret;
   /* before the conversion, which may replace the input */
   memset(&t, 0, sizeof(t));
   if (opt && opt->keep_time && !fl_is_std(in_path) && !fl_is_std(out_path)) fl_get_times(in_path, &t);
   if ((ret = fl_inbuf_open(&in, in_path)) != FL_OK) return ret;
   ret = fl_convert(&in, opt && opt->progress && !fl_is_std(in_path) ? fl_file_size(in_path) : 0, out_path, NULL, opt, &same);
   if (ret == FL_OK || ret == FL_ERR_CHECKSUM) fl_set_times(out_path, &t, same);
   return ret;
}

FLDEF int fl_convert_io(const fl_io *in_io, const fl_io *out_io, const fl_options *opt) {
   fl_inbuf in;
   if (!in_io || !out_io || !opt || !opt->format) return FL_ERR_BAD_ARG;
   fl_inbuf_open_io(&in, in_io);
   return fl_convert(&in, 0, NULL, out_io, opt, NULL);
}

static int fl_read_all(fl_inbuf *in, const fl_options *opt_in, fl_audio *audio) {
   fl_options opt;
   fl_mem_sink m;
   fl_sink *sink;
   int fmt, ret;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   memset(audio, 0, sizeof(*audio));
   if ((ret = fl_check_options(&opt)) == FL_OK && (fmt = ret = fl_detect_format(in)) >= 0) {
      memset(&m, 0, sizeof(m));
      m.base.start = fl_mem_start;
      m.base.write = fl_mem_write;
      m.base.finish = fl_mem_finish;
      m.base.destroy = fl_mem_destroy;
      m.a = audio;
      sink = fmt == FL_FORMAT_OPUS ? &m.base : fl_format_wrap(&m.base, &opt, 1, &ret);
      if (sink) ret = fl_run_source(fmt, in, &opt, sink);
      if (sink && sink != &m.base) sink->destroy(sink);
      if (ret != FL_OK && ret != FL_ERR_CHECKSUM) fl_audio_free(audio);
   }
   fl_inbuf_close(in);
   return ret;
}

FLDEF int fl_read_audio(const char *path, const fl_options *opt, fl_audio *audio) {
   fl_inbuf in;
   int ret;
   memset(audio, 0, sizeof(*audio));
   if ((ret = fl_inbuf_open(&in, path)) != FL_OK) return ret;
   return fl_read_all(&in, opt, audio);
}

FLDEF int fl_read_audio_io(const fl_io *io, const fl_options *opt, fl_audio *audio) {
   fl_inbuf in;
   memset(audio, 0, sizeof(*audio));
   if (!io) return FL_ERR_BAD_ARG;
   fl_inbuf_open_io(&in, io);
   return fl_read_all(&in, opt, audio);
}

FLDEF void fl_audio_free(fl_audio *audio) {
   free(audio->samples);
   memset(audio, 0, sizeof(*audio));
}

/* ---- Step-by-step writing ---- */
struct fl_writer {
   fl_sink *sink;
   char *tmp, *path;
   int channels, err;
};

static fl_writer *fl_writer_create(const char *path, const fl_io *io, const fl_info *info, const fl_options *opt_in, int *error) {
   fl_options opt;
   fl_writer *w;
   fl_format f;
   int fmt, err = FL_ERR_BAD_ARG;
   if (error) *error = FL_ERR_BAD_ARG;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if (!info || fl_check_options(&opt) != FL_OK) return NULL;
   if (info->channels < 1 || info->channels > 255 || info->rate < 1 || info->rate > FL_MAX_RATE ||
       (info->is_float ? info->bits != 32 : info->bits < 1 || info->bits > 32)) return NULL;
   fmt = opt.format ? opt.format : path ? fl_format_from_name(path) : FL_ERR_BAD_ARG;
   if (fmt < 0) return NULL;
   if (!(w = (fl_writer*)calloc(1, sizeof(*w)))) {
      if (error) *error = FL_ERR_ALLOC;
      return NULL;
   }
   w->channels = info->channels;
   if (path && !fl_is_std(path)) {
      w->tmp = fl_temp_path(path);
      w->path = (char*)malloc(strlen(path)+1);
      if (!w->tmp || !w->path) {
         err = FL_ERR_ALLOC;
         goto fail;
      }
      strcpy(w->path, path);
   }
   if (!(w->sink = fl_create_sink(fmt, w->tmp ? w->tmp : path, io, &opt, &err)) ||
       (fmt != FL_FORMAT_OPUS && !(w->sink = fl_format_wrap(w->sink, &opt, 0, &err)))) goto fail;
   fl_format_init(&f);
   f.channels = info->channels;
   f.rate = info->rate;
   f.bits = f.container_bits = info->bits;
   f.is_float = info->is_float;
   f.frames = info->frames;
   if ((err = w->sink->start(w->sink, &f)) != FL_OK) goto fail;
   if (error) *error = FL_OK;
   return w;
fail:
   if (error) *error = err;
   w->err = err;
   fl_writer_close(w);
   return NULL;
}

FLDEF fl_writer *fl_writer_open(const char *path, const fl_info *info, const fl_options *opt, int *error) {
   if (!path) {
      if (error) *error = FL_ERR_BAD_ARG;
      return NULL;
   }
   return fl_writer_create(path, NULL, info, opt, error);
}

FLDEF fl_writer *fl_writer_open_io(const fl_io *out, const fl_info *info, const fl_options *opt, int *error) {
   if (!out || !opt || !opt->format) {
      if (error) *error = FL_ERR_BAD_ARG;
      return NULL;
   }
   return fl_writer_create(NULL, out, info, opt, error);
}

FLDEF int fl_writer_write(fl_writer *w, const int *pcm, int frames) {
   while (w->err == FL_OK && frames > 0) {
      int n = FL_MIN(frames, 4096);
      w->err = w->sink->write(w->sink, pcm, n);
      pcm += (size_t)n*w->channels;
      frames -= n;
   }
   return w->err;
}

FLDEF int fl_writer_close(fl_writer *w) {
   int ret;
   if (!w) return FL_ERR_BAD_ARG;
   ret = w->err;
   if (w->sink) {
      if (ret == FL_OK) ret = w->sink->finish(w->sink);
      w->sink->destroy(w->sink);
   }
   if (w->tmp) {
      if (w->path) ret = fl_finish_output(w->tmp, w->path, ret);
      else fl_remove(w->tmp);
   }
   free(w->tmp);
   free(w->path);
   free(w);
   return ret;
}

static int fl_write_all(fl_writer *w, const fl_audio *a, int err) {
   long long pos = 0;
   if (!w) return err;
   while (pos < a->frames && fl_writer_write(w, a->samples+(size_t)pos*a->channels, (int)FL_MIN(a->frames-pos, 1<<20)) == FL_OK)
      pos += FL_MIN(a->frames-pos, 1<<20);
   return fl_writer_close(w);
}

static int fl_audio_info(const fl_audio *a, fl_info *info) {
   if (!a || a->channels < 1 || a->bits < 1 || a->bits > 32 || a->rate < 1 || a->frames < 0) return FL_ERR_BAD_ARG;
   info->channels = a->channels;
   info->rate = a->rate;
   info->bits = a->is_float ? 32 : a->bits;
   info->is_float = a->is_float;
   info->frames = a->frames;
   return FL_OK;
}

FLDEF int fl_write_audio(const char *path, const fl_audio *a, const fl_options *opt) {
   fl_info info;
   fl_writer *w;
   int err;
   if ((err = fl_audio_info(a, &info)) != FL_OK) return err;
   /* err is read after the open sets it (argument order is unspecified) */
   w = fl_writer_open(path, &info, opt, &err);
   return fl_write_all(w, a, err);
}

FLDEF int fl_write_audio_io(const fl_io *out, const fl_audio *a, const fl_options *opt) {
   fl_info info;
   fl_writer *w;
   int err;
   if ((err = fl_audio_info(a, &info)) != FL_OK) return err;
   w = fl_writer_open_io(out, &info, opt, &err);
   return fl_write_all(w, a, err);
}

/* ---- Step-by-step reading ---- */

/* Sink that collects decoded frames for fl_reader_read(). */
typedef struct {
   fl_sink base;
   fl_i32 *buf;
   size_t cap, len, pos;  /* in samples */
   int C;
} fl_fifo_sink;

static int fl_fifo_write(fl_sink *s, const fl_i32 *pcm, int n) {
   fl_fifo_sink *q = (fl_fifo_sink*)s;
   size_t need = (size_t)n*q->C;
   if (q->pos > 0 && q->len+need > q->cap) {
      memmove(q->buf, q->buf+q->pos, sizeof(fl_i32)*(q->len-q->pos));
      q->len -= q->pos;
      q->pos = 0;
   }
   if (q->len+need > q->cap) {
      size_t cap = FL_MAX(2*q->cap, q->len+need);
      fl_i32 *b = (fl_i32*)realloc(q->buf, sizeof(fl_i32)*cap);
      if (!b) return FL_ERR_ALLOC;
      q->buf = b;
      q->cap = cap;
   }
   memcpy(q->buf+q->len, pcm, sizeof(fl_i32)*need);
   q->len += need;
   return FL_OK;
}

static int fl_fifo_start(fl_sink *s, const fl_format *f) {
   (void)s;
   (void)f;
   return FL_OK;
}

struct fl_reader {
   fl_inbuf in;
   fl_source *src;
   fl_fifo_sink q;
   fl_sink *sink;     /* &q, or a fl_format_sink around it */
   fl_i64 pos, drop;  /* frames handed out so far; frames still to drop after a seek */
   int done, status;  /* end reached; FL_ERR_CHECKSUM still to report */
};

static fl_reader *fl_reader_create(fl_reader *r, const fl_options *opt_in, int *error) {
   fl_options opt;
   int fmt = 0, ret;
   if (opt_in) opt = *opt_in;
   else fl_options_init(&opt);
   if ((ret = fl_check_options(&opt)) == FL_OK && (fmt = ret = fl_detect_format(&r->in)) >= 0)
      ret = fl_source_open(fmt, &r->in, &opt, &r->src);
   if (ret >= 0) {
      r->q.C = r->src->fmt.channels;
      r->q.base.start = fl_fifo_start;
      r->q.base.write = fl_fifo_write;
      r->sink = fmt == FL_FORMAT_OPUS ? &r->q.base : fl_format_wrap(&r->q.base, &opt, 1, &ret);
      if (r->sink && (ret = r->sink->start(r->sink, &r->src->fmt)) == FL_OK) {
         if (error) *error = FL_OK;
         return r;
      }
   }
   if (error) *error = ret;
   fl_reader_close(r);
   return NULL;
}

FLDEF fl_reader *fl_reader_open(const char *path, const fl_options *opt, int *error) {
   fl_reader *r = (fl_reader*)calloc(1, sizeof(*r));
   int ret = r ? fl_inbuf_open(&r->in, path) : FL_ERR_ALLOC;
   if (ret != FL_OK) {
      if (error) *error = ret;
      free(r);
      return NULL;
   }
   return fl_reader_create(r, opt, error);
}

FLDEF fl_reader *fl_reader_open_io(const fl_io *in, const fl_options *opt, int *error) {
   fl_reader *r = in ? (fl_reader*)calloc(1, sizeof(*r)) : NULL;
   if (!r) {
      if (error) *error = in ? FL_ERR_ALLOC : FL_ERR_BAD_ARG;
      return NULL;
   }
   fl_inbuf_open_io(&r->in, in);
   return fl_reader_create(r, opt, error);
}

FLDEF fl_reader *fl_reader_open_memory(const void *data, size_t size, const fl_options *opt, int *error) {
   fl_reader *r = data ? (fl_reader*)calloc(1, sizeof(*r)) : NULL;
   if (!r) {
      if (error) *error = data ? FL_ERR_ALLOC : FL_ERR_BAD_ARG;
      return NULL;
   }
   fl_inbuf_open_memory(&r->in, data, size);
   return fl_reader_create(r, opt, error);
}

FLDEF void fl_reader_info(const fl_reader *r, fl_info *info) {
   const fl_format *f = &r->src->fmt;
   const fl_format_sink *p = (const fl_format_sink*)r->sink;
   info->channels = f->channels;
   info->rate = f->rate;
   info->bits = f->bits;
   info->is_float = f->is_float;
   if (r->sink != &r->q.base && p->in_bits) {
      info->bits = p->as_float ? 32 : p->bits;
      info->is_float = p->as_float;
   }
   info->frames = f->frames;
}

FLDEF int fl_reader_tag_count(const fl_reader *r) { return r->src->fmt.ntags; }

FLDEF const char *fl_reader_tag(const fl_reader *r, int index) {
   return index >= 0 && index < r->src->fmt.ntags ? r->src->fmt.tags[index] : NULL;
}

FLDEF int fl_reader_read(fl_reader *r, int *pcm, int max_frames) {
   fl_fifo_sink *q = &r->q;
   int C = q->C;
   if (max_frames <= 0) return 0;
   for (;;) {
      size_t have = (q->len-q->pos)/C;
      int ret;
      if (have > 0 && r->drop > 0) {
         size_t k = (size_t)FL_MIN((fl_i64)have, r->drop);
         q->pos += k*C;
         r->drop -= k;
         continue;
      }
      if (have > 0) {
         int n = (int)FL_MIN(have, (size_t)max_frames);
         memcpy(pcm, q->buf+q->pos, sizeof(fl_i32)*n*C);
         q->pos += (size_t)n*C;
         r->pos += n;
         return n;
      }
      q->pos = q->len = 0;
      if (r->done) {
         ret = r->status;
         r->status = FL_OK;
         return ret;
      }
      if ((ret = r->src->step(r->src, r->sink)) < 0) return ret;
      if (ret == 0) {
         r->done = 1;
         r->status = r->src->end_status;
      }
   }
}

FLDEF int fl_reader_seek(fl_reader *r, long long frame) {
   fl_i64 at;
   int ret;
   if (frame < 0) return FL_ERR_BAD_ARG;
   ret = r->src->seek ? r->src->seek(r->src, frame, &at) : FL_ERR_UNSUPPORTED;
   if (ret == FL_OK) {
      r->q.pos = r->q.len = 0;
      r->pos = frame;
      r->drop = frame-at;
      r->done = 0;
      r->status = FL_OK;
      return FL_OK;
   }
   /* input that cannot seek: forward by dropping frames */
   if (ret != FL_ERR_UNSUPPORTED || frame < r->pos) return ret;
   r->drop += frame-r->pos;
   r->pos = frame;
   return FL_OK;
}

FLDEF void fl_reader_close(fl_reader *r) {
   if (!r) return;
   if (r->sink && r->sink != &r->q.base) r->sink->destroy(r->sink);
   if (r->src) r->src->close(r->src);
   fl_inbuf_close(&r->in);
   free(r->q.buf);
   free(r);
}
