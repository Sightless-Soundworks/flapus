/* Command line front end for flapus.h: converts between WAV, FLAC and Ogg Opus. */
#define FLAPUS_IMPLEMENTATION
#include "flapus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(void) {
   fprintf(stderr,
      "flapus - Opus and FLAC encoder/decoder\n"
      "Usage: flapus [options] input output\n"
      "  The input format (WAV, FLAC, Ogg FLAC, Ogg Opus) is detected from the file.\n"
      "  The output format follows the extension: .wav, .flac or .opus.\n"
      "  Use - for stdin or stdout (stdout needs --format).\n"
      "General:\n"
      "      --format F       output format: wav, flac or opus (overrides the extension)\n"
      "  -s, --silent         no progress display\n"
      "  -V, --version        show the version and how this build was made\n"
      "      --delete-input-file  delete the input file after a successful conversion\n"
      "FLAC output:\n"
      "      --level N        0 (fastest) .. 8 (smallest), default 5\n"
      "      --lax            allow streams outside the FLAC subset (even smaller, but\n"
      "                       not every hardware player supports them)\n"
      "      --threads N      encoder threads (default 0 = one per CPU)\n"
      "Opus input:\n"
      "      --rate N         output sample rate (default: original input rate)\n"
      "      --no-dither      plain rounding instead of shaped dither\n"
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

int main(int argc, char **argv) {
   fl_options opt;
   static char opts[256];
   progress_state ps;
   int i, ret, last = argc-2, silent = 0, delete_input = 0, to_stdout;
   if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
      version();
      return 0;
   }
   if (argc < 3) {
      usage();
      return 1;
   }
   fl_options_init(&opt);
   opt.threads = 0;  /* the command line tool uses every CPU (the library default is 1) */
   for (i = 1; i < last; i++) {
      const char *a = argv[i], *v = i+1 < last ? argv[i+1] : NULL;
      if (strcmp(a, "-s") == 0 || strcmp(a, "--silent") == 0) {
         silent = 1;
      } else if (strcmp(a, "--delete-input-file") == 0) {
         delete_input = 1;
      } else if (strcmp(a, "--format") == 0 && v) {
         if (strcmp(v, "wav") == 0) opt.format = FL_FORMAT_WAV;
         else if (strcmp(v, "flac") == 0) opt.format = FL_FORMAT_FLAC;
         else if (strcmp(v, "opus") == 0) opt.format = FL_FORMAT_OPUS;
         else {
            usage();
            return 1;
         }
         i++;
      } else if (strcmp(a, "--level") == 0 && v) {
         opt.flac_level = atoi(v);
         if (opt.flac_level < 0 || opt.flac_level > 8) {
            usage();
            return 1;
         }
         i++;
      } else if (strcmp(a, "--lax") == 0) {
         opt.flac_lax = 1;
      } else if (strcmp(a, "--threads") == 0 && v) {
         opt.threads = atoi(v);
         i++;
      } else if (strcmp(a, "--rate") == 0 && v) {
         opt.rate = atoi(v);
         i++;
      } else if (strcmp(a, "--no-dither") == 0) {
         opt.dither = 0;
      } else if (strcmp(a, "--bitrate") == 0 && v) {
         opt.bitrate = (int)(atof(v)*1000);
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--comp") == 0 && v) {
         opt.complexity = atoi(v);
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--framesize") == 0 && v) {
         opt.frame_ms_x10 = (int)(atof(v)*10+.5);
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--vbr") == 0 || strcmp(a, "--cvbr") == 0 || strcmp(a, "--hard-cbr") == 0) {
         opt.vbr = a[2] != 'h';
         opt.cvbr = a[2] == 'c';
         note(opts, sizeof(opts), a, NULL);
      } else if (strcmp(a, "--quality") == 0 && v) {
         opt.quality = atof(v);
         if (!(opt.quality >= 0) || opt.quality > 10) {
            usage();
            return 1;
         }
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--expect-loss") == 0 && v) {
         opt.expect_loss = atoi(v);
         if (opt.expect_loss < 0 || opt.expect_loss > 100) {
            usage();
            return 1;
         }
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--music") == 0 || strcmp(a, "--speech") == 0) {
         opt.signal = a[2] == 'm' ? 1 : 2;
         note(opts, sizeof(opts), a, NULL);
      } else if (strcmp(a, "--no-phase-inv") == 0) {
         opt.no_phase_inv = 1;
         note(opts, sizeof(opts), a, NULL);
      } else if (strcmp(a, "--downmix-mono") == 0 || strcmp(a, "--downmix-stereo") == 0) {
         opt.downmix = a[10] == 'm' ? 1 : 2;
         note(opts, sizeof(opts), a, NULL);
      } else if (strcmp(a, "--padding") == 0 && v) {
         char *end;
         long n = strtol(v, &end, 10);
         if (*end || n < 0 || n > 65536) {
            usage();
            return 1;
         }
         opt.padding = (int)n;
         note(opts, sizeof(opts), a, v);
         i++;
      } else if (strcmp(a, "--serial") == 0 && v) {
         char *end;
         long long n = strtoll(v, &end, 10);
         if (*end || n < 0 || n > 4294967295LL) {
            usage();
            return 1;
         }
         opt.serialno = n;
         note(opts, sizeof(opts), a, v);
         i++;
      } else {
         usage();
         return 1;
      }
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
