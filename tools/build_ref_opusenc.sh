#!/bin/sh
# Builds the reference opusenc that the Opus encoder is checked against (test/grid.sh):
# opus-tools (git master, Nov 2026) with libopusenc 0.3 and libopus 1.6.1, all plain C
# (no intrinsics in libopus, no run-time CPU detection, no fused multiply-add), with
# libm's log, log10, exp, pow, sin, cos and acos replaced by src/math.c, as in flapus.
#
#   sh tools/build_ref_opusenc.sh <opus> <libopusenc> <opus-tools> <libogg> <out dir>
#
# The arguments are the source trees (as released, or git checkouts); the result is
# <out dir>/opusenc. Linux or another system with GNU ld (for --wrap).
set -e
OPUS=$(cd "$1" && pwd); OPE=$(cd "$2" && pwd); TOOLS=$(cd "$3" && pwd); OGG=$(cd "$4" && pwd)
mkdir -p "$5"; OUT=$(cd "$5" && pwd); SRC=$(cd "$(dirname "$0")/../src" && pwd)
CC=${CC:-gcc}
F="-O2 -ffp-contract=off -DUSE_ALLOCA -DHAVE_ALLOCA_H -DHAVE_LRINTF -DHAVE_LRINT"
mkdir -p "$OUT/obj" "$OUT/inc/ogg"
cat > "$OUT/inc/ogg/config_types.h" <<'H'
#include <stdint.h>
typedef int16_t ogg_int16_t; typedef uint16_t ogg_uint16_t; typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t; typedef int64_t ogg_int64_t; typedef uint64_t ogg_uint64_t;
H
cat > "$OUT/math_wrap.c" <<H
#include <string.h>
#include <math.h>
typedef int fl_i32; typedef unsigned fl_u32; typedef long long fl_i64; typedef unsigned long long fl_u64;
#define FLAPUS_HAVE_ENCODER
#include "$SRC/math.c"
double __wrap_log(double x) { return fl_log(x); }
double __wrap_log10(double x) { return fl_log10(x); }
double __wrap_exp(double x) { return fl_exp(x); }
double __wrap_pow(double x, double y) { return fl_pow(x, y); }
double __wrap_sin(double x) { return fl_sin(x); }
double __wrap_cos(double x) { return fl_cos(x); }
double __wrap_acos(double x) { return fl_acos(x); }
H
# libopus: the plain C sources of a float build
SRCS=$(cd "$OPUS" && awk 'FNR == 1 { g = "" } { gsub(/\\/, "") }
  /^[A-Z_0-9]+ *=/ { g = $1; next } NF == 0 { g = "" }
  g ~ /^(CELT_SOURCES|SILK_SOURCES|SILK_SOURCES_FLOAT|OPUS_SOURCES|OPUS_SOURCES_FLOAT)$/ { for (i = 1; i <= NF; i++) print $i }
  ' celt_sources.mk silk_sources.mk opus_sources.mk)
for f in $SRCS; do
  (cd "$OPUS" && $CC $F -DOPUS_BUILD -DPACKAGE_VERSION='"1.6.1"' -Iinclude -Icelt -Isilk -Isilk/float -c $f -o "$OUT/obj/opus_$(echo $f | tr / _).o")
done
for f in "$OPE"/src/*.c; do
  $CC $F -DPACKAGE_NAME='"libopusenc"' -DPACKAGE_VERSION='"0.3"' -DOUTSIDE_SPEEX -DRANDOM_PREFIX=libopusenc \
      -DRESAMPLE_FULL_SINC_TABLE -I"$OPE/include" -I"$OPUS/include" -c "$f" -o "$OUT/obj/ope_$(basename $f .c).o"
done
for f in bitwise framing; do $CC -O2 -I"$OUT/inc" -I"$OGG/include" -c "$OGG/src/$f.c" -o "$OUT/obj/ogg_$f.o"; done
# opusenc's own sources, as Makefile.am lists them (WAV input only: no libFLAC)
for f in $(sed -n 's/^opusenc_SOURCES *= *//p' "$TOOLS/Makefile.am"); do
  case $f in win32/*) continue;; esac
  $CC $F -DPACKAGE_NAME='"opus-tools"' -DPACKAGE_VERSION='"git"' -I"$OUT/inc" -I"$OGG/include" -I"$OPUS/include" \
      -I"$OPE/include" -c "$TOOLS/$f" -o "$OUT/obj/tools_$(basename $f .c).o"
done
$CC -O2 -ffp-contract=off -c "$OUT/math_wrap.c" -o "$OUT/obj/math_wrap.o"
$CC -o "$OUT/opusenc" "$OUT"/obj/*.o -Wl,--wrap=log,--wrap=log10,--wrap=exp,--wrap=pow,--wrap=sin,--wrap=cos,--wrap=acos -lm
echo "$OUT/opusenc"
