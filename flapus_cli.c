/* Command line front end for flapus.h: converts between WAV, FLAC and Ogg Opus. */
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *f) {
   fprintf(f,
      "flapus - Opus and FLAC encoder/decoder\n"
      "Usage: flapus [options] input output\n"
      "       flapus --help | --version\n"
      "  The input format (WAV, FLAC, Ogg FLAC, Ogg Opus) is detected from the file.\n"
      "  The output format follows the extension: .wav, .flac or .opus.\n"
      "  Use - for stdin or stdout (stdout needs --format).\n"
      "General:\n"
      "      --format F       output format: wav, flac or opus (overrides the extension)\n"
      "  -f, --force          overwrite an existing output file\n"
      "  -s, --silent         no progress display\n"
      "  -V, --version        show the version and how this build was made\n"
      "      --delete-input-file  delete the input file after a successful conversion\n"
      "FLAC output:\n"
      "      --level N        0 (fastest) .. 8 (smallest), default 5\n"
      "      --lax            allow streams outside the FLAC subset (even smaller, but\n"
      "                       not every hardware player supports them)\n"
      "      --threads N      encoder threads (default 0 = one per CPU)\n"
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
      "      --padding N      free bytes after the tags (default 512, 0 = none)\n"
      "      --serial N       Ogg serial number (default: random)\n"
#endif
      );
}

/* Opens path for querying its identity (Windows). */
#ifdef _WIN32
static HANDLE fl_open_attr(const char *path) {
   wchar_t *w = fl_widen(path);
   HANDLE h;
   DWORD share = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
   if (w) {
      h = CreateFileW(w, FILE_READ_ATTRIBUTES, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      free(w);
   } else h = CreateFileA(path, FILE_READ_ATTRIBUTES, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
   return h;
}
#endif

/* 1 if a and b name the same existing file (also through links or another spelling). */
static int fl_same_file(const char *a, const char *b) {
   if (fl_is_std(a) || fl_is_std(b)) return 0;
#ifdef _WIN32
   {
      HANDLE ha = fl_open_attr(a), hb = fl_open_attr(b);
      BY_HANDLE_FILE_INFORMATION ia, ib;
      int same = ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE &&
                 GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib) &&
                 ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
                 ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
      if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
      if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
      return same;
   }
#elif defined(__unix__) || defined(__APPLE__)
   {
      struct stat sa, sb;
      return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
   }
#else
   return strcmp(a, b) == 0;
#endif
}

/* --version: the version, how the Opus encoder was built, and the target. */
static void version(void) {
   printf("flapus %s\n", FLAPUS_VERSION);
#if defined(FLAPUS_HAVE_ENCODER) && defined(FLAPUS_PORTABLE_MATH)
   printf("Opus encoder: portable math (the same output on every x86 CPU)\n");
#elif defined(FLAPUS_HAVE_ENCODER)
   printf("Opus encoder: native CPU instructions (output can differ between Intel and AMD)\n");
#else
   printf("Opus encoder: not included\n");
#endif
   printf("%s, GCC %d.%d.%d\n", sizeof(void*) == 8 ? "x86-64" : "x86 (32-bit)",
          __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
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
   if (percent >= 0 && ratio >= 0) fprintf(stderr, "\r%s: %d%% complete, ratio=%.3f   ", p->name, percent, ratio);
   else if (percent >= 0) fprintf(stderr, "\r%s: %d%% complete   ", p->name, percent);
   else if (ratio >= 0) fprintf(stderr, "\r%s: ratio=%.3f   ", p->name, ratio);
   fflush(stderr);
}

static long long file_size(const char *path) {
   FILE *f = fopen(path, "rb");
   long long n = -1;
   if (f) {
      fseek(f, 0, SEEK_END);
      n = ftell(f);
      fclose(f);
   }
   return n;
}

/* Parses a whole number in [lo, hi]. */
static int get_int(const char *opt, const char *s, long lo, long hi, int *out) {
   char *end;
   long v = strtol(s, &end, 10);
   if (end == s || *end || v < lo || v > hi) {
      fprintf(stderr, "Error: invalid value for %s: %s (%ld to %ld)\n", opt, s, lo, hi);
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
      fprintf(stderr, "Error: invalid value for %s: %s (%.10g to %.10g)\n", opt, s, lo, hi);
      return 0;
   }
   *out = v;
   return 1;
}

int main(int argc, char **argv) {
   fl_options opt;
   static char opts[256];
   progress_state ps;
   int i, ret, last = argc-2, silent = 0, force = 0, delete_input = 0, to_stdout;
   double d;
   if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
      version();
      return 0;
   }
   if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
      usage(stdout);
      return 0;
   }
   if (argc < 3) {
      usage(stderr);
      return 1;
   }
   fl_options_init(&opt);
   opt.threads = 0;  /* the command line tool uses every CPU (the library default is 1) */
   for (i = 1; i < last; i++) {
      const char *a = argv[i], *v = i+1 < last ? argv[i+1] : NULL;
      int ok = 1, takes = 1;
      if (strcmp(a, "-s") == 0 || strcmp(a, "--silent") == 0) {
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
      } else if (strcmp(a, "--format") != 0 && strcmp(a, "--level") != 0 && strcmp(a, "--threads") != 0 &&
                 strcmp(a, "--rate") != 0 && strcmp(a, "--bits") != 0 && strcmp(a, "--bitrate") != 0 && strcmp(a, "--comp") != 0 &&
                 strcmp(a, "--framesize") != 0 && strcmp(a, "--quality") != 0 && strcmp(a, "--expect-loss") != 0 &&
                 strcmp(a, "--padding") != 0 && strcmp(a, "--serial") != 0) {
         fprintf(stderr, "Error: unknown option %s (see flapus --help)\n", a);
         return 1;
      } else if (!v) {
         fprintf(stderr, "Error: %s needs a value\n", a);
         return 1;
      } else if (strcmp(a, "--format") == 0) {
         if (strcmp(v, "wav") == 0) opt.format = FL_FORMAT_WAV;
         else if (strcmp(v, "flac") == 0) opt.format = FL_FORMAT_FLAC;
         else if (strcmp(v, "opus") == 0) opt.format = FL_FORMAT_OPUS;
         else {
            fprintf(stderr, "Error: invalid value for --format: %s (wav, flac or opus)\n", v);
            ok = 0;
         }
      } else if (strcmp(a, "--level") == 0) {
         ok = get_int(a, v, 0, 8, &opt.flac_level);
      } else if (strcmp(a, "--threads") == 0) {
         ok = get_int(a, v, 0, 1024, &opt.threads);
      } else if (strcmp(a, "--rate") == 0) {
         ok = get_int(a, v, 8000, 192000, &opt.rate);
      } else if (strcmp(a, "--bits") == 0) {
         if ((ok = get_int(a, v, 16, 32, &opt.bits)) && opt.bits != 16 && opt.bits != 24 && opt.bits != 32) {
            fprintf(stderr, "Error: --bits must be 16, 24 or 32\n");
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
               fprintf(stderr, "Error: --framesize must be 2.5, 5, 10, 20, 40, 60, 80, 100 or 120\n");
               ok = 0;
            }
            opt.frame_ms_x10 = (int)(d*10+.5);
         }
      } else if (strcmp(a, "--quality") == 0) {
         ok = get_num(a, v, 0, 10, &opt.quality);
      } else if (strcmp(a, "--expect-loss") == 0) {
         ok = get_int(a, v, 0, 100, &opt.expect_loss);
      } else if (strcmp(a, "--padding") == 0) {
         ok = get_int(a, v, 0, 65536, &opt.padding);
      } else if (strcmp(a, "--serial") == 0) {
         if ((ok = get_num(a, v, 0, 4294967295.0, &d)) && d != floor(d)) {
            fprintf(stderr, "Error: --serial must be a whole number\n");
            ok = 0;
         }
         opt.serialno = (long long)d;
      }
      if (!ok) return 1;
      if (takes) {
         if (strcmp(a, "--level") != 0 && strcmp(a, "--threads") != 0 && strcmp(a, "--rate") != 0 &&
             strcmp(a, "--bits") != 0 && strcmp(a, "--format") != 0)
            note(opts, sizeof(opts), a, v);
         i++;
      }
   }
   if (opt.bitrate > 0 && opt.quality >= 0) {
      fprintf(stderr, "Error: --bitrate and --quality cannot be combined\n");
      return 1;
   }
   to_stdout = strcmp(argv[argc-1], "-") == 0;
   if (to_stdout && !opt.format) {
      fprintf(stderr, "Error: output to stdout needs --format\n");
      return 1;
   }
   if (delete_input && strcmp(argv[argc-2], "-") == 0) {
      fprintf(stderr, "Error: --delete-input-file needs an input file\n");
      return 1;
   }
   if (!to_stdout && !force && fl_file_exists(argv[argc-1])) {
      fprintf(stderr, "Error: %s exists (use -f to overwrite it)\n", argv[argc-1]);
      return 1;
   }
   if (delete_input && fl_same_file(argv[argc-2], argv[argc-1])) {
      fprintf(stderr, "Error: --delete-input-file would delete the output, which is the same file\n");
      return 1;
   }
   opt.comment_options = opts[0] ? opts : NULL;
   memset(&ps, 0, sizeof(ps));
   ps.name = strcmp(argv[argc-2], "-") == 0 ? "stdin" : argv[argc-2];
   ps.ratio = -1;
   if (!silent) {
      opt.progress = progress;
      opt.progress_user = &ps;
   }
   ret = fl_convert_file(argv[argc-2], argv[argc-1], &opt);
   if (!silent) {
      if (ret == FL_OK && ps.ratio >= 0 && !to_stdout)
         fprintf(stderr, "\r%s: wrote %lld bytes, ratio=%.3f   \n", ps.name, file_size(argv[argc-1]), ps.ratio);
      else if (ret == FL_OK && ps.ratio >= 0) fprintf(stderr, "\r%s: done, ratio=%.3f   \n", ps.name, ps.ratio);
      else if (ret == FL_OK) fprintf(stderr, "\r%s: done              \n", ps.name);
      else if (ps.shown) fprintf(stderr, "\n");
   }
   if (ret != FL_OK) {
      fprintf(stderr, "Error: %s%s\n", fl_error_text(ret), ret == FL_ERR_CHECKSUM ? " (output written anyway)" : "");
      return 1;
   }
   /* only after success, like flac --delete-input-file */
   if (delete_input && remove(argv[argc-2]) != 0) {
      fprintf(stderr, "Error: cannot delete %s\n", argv[argc-2]);
      return 1;
   }
   return 0;
}
