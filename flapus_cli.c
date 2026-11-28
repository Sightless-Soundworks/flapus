/* Command line front end for flapus.h: converts between WAV, FLAC and Ogg Opus. */
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* fprintf(stderr, ...), but to a Windows console as UTF-16, so that file names show
   correctly whatever its code page. */
static void eprintf(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
#ifdef _WIN32
   {
      HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
      DWORD mode, done;
      if (GetConsoleMode(h, &mode)) {
         char buf[4096];
         wchar_t w[4096];
         int n;
         vsnprintf(buf, sizeof(buf), fmt, ap);
         va_end(ap);
         buf[sizeof(buf)-1] = 0;
         /* UTF-8 (the arguments, see utf8_args), else the ANSI code page */
         if ((n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, -1, w, 4096)) <= 0)
            n = MultiByteToWideChar(CP_ACP, 0, buf, -1, w, 4096);
         fflush(stderr);
         if (n > 1) WriteConsoleW(h, w, (DWORD)(n-1), &done, NULL);
         return;
      }
   }
#endif
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static void usage(FILE *f) {
   fprintf(f,
      "flapus - Opus and FLAC encoder/decoder\n"
      "Usage: flapus [options] input [output]\n"
      "       flapus --help | --version\n"
      "  The input format (WAV, FLAC, Ogg FLAC, Ogg Opus) is detected from the file.\n"
      "  The output format follows the extension: .wav, .flac or .opus.\n"
      "  Without output, a FLAC or Opus file is converted onto itself (FLAC to FLAC,\n"
      "  Opus to Opus); WAV input needs an output.\n"
      "  Use - for stdin or stdout (stdout needs --format).\n"
      "  Options may also follow the file names; -- ends the options.\n"
      "General:\n"
      "      --format F       output format: wav, flac or opus (overrides the extension)\n"
      "      --search N       look for the start of the stream up to N KB into the input,\n"
      "                       such as an Opus stream in an exe (default 64)\n"
      "      --padding [N]    free bytes after the tags, so tags can be edited later:\n"
      "                       N, or without N 8192 for FLAC and 512 for Opus\n"
      "                       (default: none)\n"
      "      --remove-tags    leave out the input's tags and pictures\n"
      "      --no-timestamp   do not give the output the input's modification time (and,\n"
      "                       FLAC to FLAC or Opus to Opus, its creation time)\n"
      "  -f, --force          overwrite an existing output file\n"
      "  -s, --silent         no progress display\n"
      "  -v, --version        show the version and how this build was made\n"
      "      --delete-input-file  delete the input file after a successful conversion\n"
      "FLAC output:\n"
      "      --level N        0 (fastest) .. 8 (smallest), default 5\n"
      "  -V, --verify         decode every frame again and compare it with the input\n"
      "      --lax            allow streams outside the FLAC subset (even smaller, but\n"
      "                       not every hardware player supports them)\n"
      "      --threads N      encoder threads, at most 64 (default 0 = one per CPU)\n"
      "      --no-seektable   no seek table (default: a seek point every 10 s)\n"
      "Sample format (WAV and FLAC output, and Opus from Opus):\n"
      "      --bits N         16, 24 or 32 (default: 16 from Opus, else the input's)\n"
      "      --float          32-bit float samples (WAV, or Opus from Opus)\n"
      "      --no-dither      plain rounding instead of shaped dither to 16 bits\n"
      "Opus input:\n"
      "      --rate N         output sample rate (default: original input rate)\n"
#ifdef FLAPUS_HAVE_ENCODER
      "Opus output:\n"
      "      --bitrate N      target bitrate in kbit/s (default: automatic)\n"
      "      --quality N      0-10 relative to the automatic bitrate (default 6)\n"
      "      --vbr            variable bitrate (default)\n"
      "      --cvbr           constrained variable bitrate\n"
      "      --hard-cbr       constant bitrate\n"
      "      --music          tune low bitrates for music\n"
      "      --speech         tune low bitrates for speech\n"
      "      --comp N         complexity 0-10 (default 10)\n"
      "      --framesize N    2.5, 5, 10, 20 (default), 40, 60, 80, 100, 120 ms\n"
      "      --expect-loss N  expected packet loss in percent (default 0)\n"
      "      --downmix-mono   downmix to mono\n"
      "      --downmix-stereo downmix to stereo (if more than 2 channels)\n"
      "      --no-phase-inv   disable phase inversion for intensity stereo\n"

      "      --serial N       Ogg serial number (default: random)\n"
#endif
      );
}

/* --version: the version, whether the Opus encoder is built in, the target and the compiler. */
static void version(void) {
   printf("flapus %s\n", FLAPUS_VERSION);
#ifdef FLAPUS_HAVE_ENCODER
   printf("Opus encoder: libopus 1.6.1\n");
#else
   printf("Opus encoder: not included\n");
#endif
#if defined(__x86_64__) || defined(_M_X64)
   printf("x86-64");
#elif defined(__i386__) || defined(_M_IX86)
   printf("x86 (32-bit)");
#elif defined(__aarch64__) || defined(_M_ARM64)
   printf("ARM64");
#elif defined(__arm__) || defined(_M_ARM)
   printf("ARM (32-bit)");
#else
   printf("%d-bit", (int)sizeof(void*)*8);
#endif
#if defined(__clang__)
   printf(", Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
   printf(", GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
   printf(", MSVC %d\n", _MSC_VER);
#else
   printf("\n");
#endif
}

/* Appends an option to the ENCODER_OPTIONS tag text. */
static void note(char *opts, size_t size, const char *a, const char *b) {
   size_t len = strlen(opts);
   snprintf(opts+len, size-len, "%s%s%s%s", len ? " " : "", a, b ? " " : "", b ? b : "");
}

/* Progress line like flac: "input: 42% complete, ratio=0.541". */
typedef struct {
   const char *name;
   int percent, shown;
   clock_t last;
   double ratio;
} progress_state;

static void progress(void *user, double done, double ratio) {
   progress_state *p = (progress_state*)user;
   int percent = done >= 0 ? (int)(done*100) : -1;
   clock_t now = clock();
   p->ratio = ratio;
   if (done >= 1.0) return;  /* the final line is printed by main() */
   if (p->shown && percent == p->percent && now-p->last < CLOCKS_PER_SEC/2) return;
   p->percent = percent;
   p->last = now;
   p->shown = 1;
   if (percent >= 0 && ratio >= 0) eprintf("\r%s: %d%% complete, ratio=%.3f   ", p->name, percent, ratio);
   else if (percent >= 0) eprintf("\r%s: %d%% complete   ", p->name, percent);
   else if (ratio >= 0) eprintf("\r%s: ratio=%.3f   ", p->name, ratio);
   fflush(stderr);
}

/* Options without a value, and with one (--padding takes one only if it is a number). */
static const char *const flag_opts[] = {"-h", "--help", "-v", "--version", "-s", "--silent", "-f", "--force",
                                        "--delete-input-file", "--lax", "--no-seektable",
                                        "-V", "--verify", "--remove-tags", "--no-timestamp", "--no-dither", "--float",
                                        "--vbr", "--cvbr", "--hard-cbr", "--music", "--speech", "--no-phase-inv",
                                        "--downmix-mono", "--downmix-stereo", NULL};
static const char *const value_opts[] = {"--format", "--level", "--threads", "--rate", "--bits", "--bitrate", "--comp",
                                         "--framesize", "--quality", "--expect-loss", "--padding", "--serial", "--search",
                                         NULL};

/* How many arguments option a takes, 0 or 1 (v: the next argument or NULL), or -1 if a
   is not an option. */
static int option_args(const char *a, const char *v) {
   int k;
   if (strcmp(a, "--padding") == 0) return v && *v && strspn(v, "0123456789") == strlen(v);
   for (k = 0; flag_opts[k]; k++)
      if (strcmp(a, flag_opts[k]) == 0) return 0;
   for (k = 0; value_opts[k]; k++)
      if (strcmp(a, value_opts[k]) == 0) return 1;
   return -1;
}

/* Splits argv into the options with their values (optv, *nopt entries) and the paths, in
   any order. "--" ends the options; "-" is a path. Returns the number of paths (0-2), or -1
   after an error message. */
static int split_args(int argc, char **argv, const char **optv, int *nopt, const char **path) {
   int i, k, n = 0, only_paths = 0;
   *nopt = 0;
   for (i = 1; i < argc; i++) {
      const char *a = argv[i];
      if (only_paths || a[0] != '-' || a[1] == 0) {
         if (n == 2) {
            eprintf("Error: more than two file names: %s\n", a);
            return -1;
         }
         path[n++] = a;
      } else if (strcmp(a, "--") == 0) {
         only_paths = 1;
      } else if ((k = option_args(a, i+1 < argc ? argv[i+1] : NULL)) < 0) {
         eprintf("Error: unknown option %s (see flapus --help)\n", a);
         return -1;
      } else if (k && i+1 == argc) {
         eprintf("Error: %s needs a value\n", a);
         return -1;
      } else {
         optv[(*nopt)++] = a;
         if (k) optv[(*nopt)++] = argv[++i];
      }
   }
   return n;
}

/* Detected format of the input file (FL_FORMAT_* or FL_ERR_*), searched as opt->search
   says; *ogg: in an Ogg container, *at: where the stream starts. */
static int input_format(const char *path, const fl_options *opt, int *ogg, long long *at) {
   fl_inbuf in;
   int fmt;
   *ogg = 0;
   *at = 0;
   if ((fmt = fl_inbuf_open(&in, path)) != FL_OK) return fmt;
   fmt = fl_detect_format(&in, (fl_i64)opt->search<<10);
   *ogg = in.len-in.pos >= 4 && memcmp(in.buf+in.pos, "OggS", 4) == 0;
   *at = fl_inbuf_tell(&in);
   fl_inbuf_close(&in);
   return fmt;
}

/* Parses a whole number in [lo, hi]. */
static int get_int(const char *opt, const char *s, long lo, long hi, int *out) {
   char *end;
   long v = strtol(s, &end, 10);
   if (end == s || *end || v < lo || v > hi) {
      eprintf("Error: invalid value for %s: %s (%ld to %ld)\n", opt, s, lo, hi);
      return 0;
   }
   *out = (int)v;
   return 1;
}

/* Parses a number in [lo, hi]. */
static int get_num(const char *opt, const char *s, double lo, double hi, double *out) {
   char *end;
   double v = strtod(s, &end);
   if (end == s || *end || !(v >= lo && v <= hi)) {
      eprintf("Error: invalid value for %s: %s (%.10g to %.10g)\n", opt, s, lo, hi);
      return 0;
   }
   *out = v;
   return 1;
}

static int run(int argc, char **argv) {
   fl_options opt;
   static char opts[256];
   progress_state ps;
   int i, ret, paths, last, silent = 0, force = 0, delete_input = 0, padding_on = 0, to_stdout, in_place;
   const char *in_path, *out_path, *path[2], **optv;
   double d;
   if (argc < 2) {
      usage(stderr);
      return 1;
   }
   if (!(optv = (const char**)malloc(sizeof(*optv)*argc)) || (paths = split_args(argc, argv, optv, &last, path)) < 0) return 1;
   fl_options_init(&opt);
   opt.keep_time = 1;  /* like flac */
   opt.threads = 0;  /* the command line tool uses every CPU (the library default is 1) */
   for (i = 0; i < last; i++) {
      const char *a = optv[i], *v = i+1 < last ? optv[i+1] : NULL;
      int ok = 1, takes = 1;
      if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
         usage(stdout);
         return 0;
      } else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
         version();
         return 0;
      } else if (strcmp(a, "-s") == 0 || strcmp(a, "--silent") == 0) {
         silent = 1;
         takes = 0;
      } else if (strcmp(a, "-f") == 0 || strcmp(a, "--force") == 0) {
         force = 1;
         takes = 0;
      } else if (strcmp(a, "--delete-input-file") == 0) {
         delete_input = 1;
         takes = 0;
      } else if (strcmp(a, "--lax") == 0) {
         opt.flac_lax = 1;
         takes = 0;
      } else if (strcmp(a, "--no-seektable") == 0) {
         opt.flac_seektable = 0;
         takes = 0;
      } else if (strcmp(a, "-V") == 0 || strcmp(a, "--verify") == 0) {
         opt.flac_verify = 1;
         takes = 0;
      } else if (strcmp(a, "--no-timestamp") == 0) {
         opt.keep_time = 0;
         takes = 0;
      } else if (strcmp(a, "--remove-tags") == 0) {
         opt.remove_tags = 1;
         takes = 0;
      } else if (strcmp(a, "--padding") == 0 && (!v || !*v || strspn(v, "0123456789") != strlen(v))) {
         padding_on = 1;  /* without a number: the standard size */
         takes = 0;
      } else if (strcmp(a, "--no-dither") == 0) {
         opt.dither = 0;
         takes = 0;
      } else if (strcmp(a, "--float") == 0) {
         opt.as_float = 1;
         takes = 0;
      } else if (strcmp(a, "--vbr") == 0 || strcmp(a, "--cvbr") == 0 || strcmp(a, "--hard-cbr") == 0) {
         opt.vbr = a[2] != 'h';
         opt.cvbr = a[2] == 'c';
         note(opts, sizeof(opts), a, NULL);
         takes = 0;
      } else if (strcmp(a, "--music") == 0 || strcmp(a, "--speech") == 0) {
         opt.signal = a[2] == 'm' ? 1 : 2;
         note(opts, sizeof(opts), a, NULL);
         takes = 0;
      } else if (strcmp(a, "--no-phase-inv") == 0) {
         opt.no_phase_inv = 1;
         note(opts, sizeof(opts), a, NULL);
         takes = 0;
      } else if (strcmp(a, "--downmix-mono") == 0 || strcmp(a, "--downmix-stereo") == 0) {
         opt.downmix = a[10] == 'm' ? 1 : 2;
         note(opts, sizeof(opts), a, NULL);
         takes = 0;
      } else if (option_args(a, v) != 1) {
         eprintf("Error: unknown option %s (see flapus --help)\n", a);
         return 1;
      } else if (!v) {
         eprintf("Error: %s needs a value\n", a);
         return 1;
      } else if (strcmp(a, "--format") == 0) {
         if (strcmp(v, "wav") == 0) opt.format = FL_FORMAT_WAV;
         else if (strcmp(v, "flac") == 0) opt.format = FL_FORMAT_FLAC;
         else if (strcmp(v, "opus") == 0) opt.format = FL_FORMAT_OPUS;
         else {
            eprintf("Error: invalid value for --format: %s (wav, flac or opus)\n", v);
            ok = 0;
         }
      } else if (strcmp(a, "--level") == 0) {
         ok = get_int(a, v, 0, 8, &opt.flac_level);
      } else if (strcmp(a, "--threads") == 0) {
         ok = get_int(a, v, 0, 64, &opt.threads);
      } else if (strcmp(a, "--rate") == 0) {
         ok = get_int(a, v, 8000, 192000, &opt.rate);
      } else if (strcmp(a, "--bits") == 0) {
         if ((ok = get_int(a, v, 16, 32, &opt.bits)) && opt.bits != 16 && opt.bits != 24 && opt.bits != 32) {
            eprintf("Error: --bits must be 16, 24 or 32\n");
            ok = 0;
         }
      } else if (strcmp(a, "--bitrate") == 0) {
         if ((ok = get_num(a, v, 0.5, 255*1024, &d))) opt.bitrate = (int)(d*1000);
      } else if (strcmp(a, "--comp") == 0) {
         ok = get_int(a, v, 0, 10, &opt.complexity);
      } else if (strcmp(a, "--framesize") == 0) {
         static const double sizes[9] = {2.5, 5, 10, 20, 40, 60, 80, 100, 120};
         int k;
         if ((ok = get_num(a, v, 2.5, 120, &d))) {
            for (k = 0; k < 9 && d != sizes[k]; k++);
            if (k == 9) {
               eprintf("Error: --framesize must be 2.5, 5, 10, 20, 40, 60, 80, 100 or 120\n");
               ok = 0;
            }
            opt.frame_ms_x10 = (int)(d*10+.5);
         }
      } else if (strcmp(a, "--quality") == 0) {
         ok = get_num(a, v, 0, 10, &opt.quality);
      } else if (strcmp(a, "--expect-loss") == 0) {
         ok = get_int(a, v, 0, 100, &opt.expect_loss);
      } else if (strcmp(a, "--padding") == 0) {
         ok = get_int(a, v, 0, 0xFFFFFF, &opt.padding);
         padding_on = 0;  /* the last --padding counts */
      } else if (strcmp(a, "--search") == 0) {
         ok = get_int(a, v, 0, 1<<30, &opt.search);
      } else if (strcmp(a, "--serial") == 0) {
         if ((ok = get_num(a, v, 0, 4294967295.0, &d)) && d != floor(d)) {
            eprintf("Error: --serial must be a whole number\n");
            ok = 0;
         }
         opt.serialno = (long long)d;
      }
      if (!ok) return 1;
      if (takes) {
         /* as in opusenc, only options that shape the encoding */
         if (strcmp(a, "--level") != 0 && strcmp(a, "--threads") != 0 && strcmp(a, "--rate") != 0 &&
             strcmp(a, "--bits") != 0 && strcmp(a, "--format") != 0 && strcmp(a, "--padding") != 0 &&
             strcmp(a, "--serial") != 0 && strcmp(a, "--search") != 0)
            note(opts, sizeof(opts), a, v);
         i++;
      }
   }
   if (paths == 0) {
      eprintf("Error: no input file (see flapus --help)\n");
      return 1;
   }
   in_path = path[0];
   out_path = path[paths-1];
   if (opt.bitrate > 0 && opt.quality >= 0) {
      eprintf("Error: --bitrate and --quality cannot be combined\n");
      return 1;
   }
   if (paths == 1) {
      /* no output: FLAC to FLAC or Opus to Opus onto the input itself */
      int ogg = 0, fmt;
      long long at;
      if (fl_is_std(in_path)) {
         eprintf("Error: stdin input needs an output (- for stdout, with --format)\n");
         return 1;
      }
      if ((fmt = input_format(in_path, &opt, &ogg, &at)) == FL_FORMAT_WAV) {
         eprintf("Error: WAV input needs an output file\n");
         return 1;
      }
      if (fmt < 0) {
         eprintf("Error: %s\n", fl_error_text(fmt));
         return 1;
      }
      if (fmt == FL_FORMAT_FLAC && ogg) {
         eprintf("Error: Ogg FLAC input needs an output file (flapus writes native FLAC)\n");
         return 1;
      }
      if (opt.format && opt.format != fmt) {
         eprintf("Error: without an output file, --format must be the input's format\n");
         return 1;
      }
      opt.format = fmt;
      force = 1;  /* the existing output is the input itself */
   }
   if (padding_on) opt.padding = (opt.format ? opt.format : fl_format_from_name(out_path)) == FL_FORMAT_OPUS ? 512 : 8192;
   to_stdout = strcmp(out_path, "-") == 0;
   if (to_stdout && !opt.format) {
      eprintf("Error: output to stdout needs --format\n");
      return 1;
   }
   if (delete_input && strcmp(in_path, "-") == 0) {
      eprintf("Error: --delete-input-file needs an input file\n");
      return 1;
   }
   if (!to_stdout && !force && fl_file_exists(out_path) && !fl_is_device(out_path)) {
      eprintf("Error: %s exists (use -f to overwrite it)\n", out_path);
      return 1;
   }
   in_place = fl_same_file(in_path, out_path);
   if (in_place) {
      /* a stream inside another file (an exe) would replace the whole file */
      int ogg;
      long long at;
      if (input_format(in_path, &opt, &ogg, &at) >= 0 && at > 0) {
         eprintf("Error: the stream starts %lld bytes into %s, so it needs another output file\n", at, in_path);
         return 1;
      }
   }
   if (delete_input && in_place) {
      eprintf("Error: --delete-input-file would delete the output, which is the same file\n");
      return 1;
   }
   opt.comment_options = opts[0] ? opts : NULL;
   memset(&ps, 0, sizeof(ps));
   ps.name = strcmp(in_path, "-") == 0 ? "stdin" : in_path;
   ps.ratio = -1;
   if (!silent) {
      opt.progress = progress;
      opt.progress_user = &ps;
   }
   ret = fl_convert_file(in_path, out_path, &opt);
   if (!silent) {
      if (ret == FL_OK && ps.ratio >= 0 && !to_stdout)
         eprintf("\r%s: wrote %lld bytes, ratio=%.3f   \n", ps.name, (long long)fl_file_size(out_path), ps.ratio);
      else if (ret == FL_OK && ps.ratio >= 0) eprintf("\r%s: done, ratio=%.3f   \n", ps.name, ps.ratio);
      else if (ret == FL_OK) eprintf("\r%s: done              \n", ps.name);
      else if (ps.shown) eprintf("\n");
   }
   if (ret != FL_OK) {
      eprintf("Error: %s%s\n", fl_error_text(ret),
              ret == FL_ERR_TOO_LARGE ? "; --remove-tags leaves them out" :
              ret != FL_ERR_CHECKSUM ? "" : in_place ? " (the input file was left unchanged)" : " (output written anyway)");
      return 1;
   }
   /* only after success, like flac --delete-input-file */
   if (delete_input && fl_remove(in_path) != 0) {
      eprintf("Error: cannot delete %s\n", in_path);
      return 1;
   }
   return 0;
}

#ifdef _WIN32
/* argv as UTF-8 from the UTF-16 arguments (NULL if out of memory). flapus opens UTF-8 paths
   with _wfopen, so file names outside the ANSI code page work. */
static char **utf8_args(int argc, wchar_t **w) {
   char **a = (char**)calloc(argc+1, sizeof(char*));
   int i, n;
   for (i = 0; a && i < argc; i++) {
      n = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
      if (n <= 0 || !(a[i] = (char*)malloc(n))) return NULL;
      WideCharToMultiByte(CP_UTF8, 0, w[i], -1, a[i], n, NULL, NULL);
   }
   return a;
}
#endif

#ifdef _MSC_VER
int wmain(int argc, wchar_t **wargv) {
   char **argv = utf8_args(argc, wargv);
   if (!argv) {
      eprintf("Error: out of memory\n");
      return 1;
   }
   return run(argc, argv);
}
#elif defined(_WIN32)
/* MinGW: the UTF-16 arguments from msvcrt.dll's __wgetmainargs, looked up at run time so
   that nothing is imported; without it (another C library), the ANSI argv. */
int main(int argc, char **argv) {
   typedef int (__cdecl *wgetmainargs_fn)(int*, wchar_t***, wchar_t***, int, int*);
   HMODULE crt = GetModuleHandleA("msvcrt.dll");
   wgetmainargs_fn get = crt ? (wgetmainargs_fn)(void (*)(void))GetProcAddress(crt, "__wgetmainargs") : NULL;
   wchar_t **wargv, **wenv;
   int wargc, newmode = 0;
   char **u;
   if (get && get(&wargc, &wargv, &wenv, 0, &newmode) == 0 && wargc == argc && (u = utf8_args(wargc, wargv))) argv = u;
   return run(argc, argv);
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
