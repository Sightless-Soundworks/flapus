#!/bin/sh
# FLAC tests: encodes every edge case at several settings, validates with the reference
# flac decoder (flac -t), and compares decoded samples (flapus and flac -d) with the input.
# Cases outside the subset (bit depth or rate) must be refused without --lax.
# Usage: run_flac.sh [case dir]   (FLAC overrides the reference flac path, FLAPUS the flapus
# binary, default ../flapus, and PYTHON the Python interpreter). Exits with 1 on a failure.
FLAC=${FLAC:-flac}
FLAPUS=${FLAPUS:-../flapus}
PYTHON=${PYTHON:-$(command -v python3 || echo python)}
dir=${1:-flac_cases}
[ -d "$dir" ] || $PYTHON make_flac_cases.py "$dir" > /dev/null
mkdir -p out
fail=0
for w in "$dir"/*.wav; do
   b=$(basename "$w" .wav)
   for set in "--level 0" "--level 3" "--level 8" "--level 8 --lax"; do
      tag=$(echo "$set" | tr -d ' -')
      $FLAPUS -s -f -V $set "$w" out/$b.$tag.flac 2>/dev/null
      r=$?
      case "$b:$set" in
         v18:*--lax*|rate700000:*--lax*) ;;
         v18:*|rate700000:*)
            if [ $r = 0 ]; then echo "$b $set: not refused outside the subset"; fail=1; fi
            continue;;
      esac
      if [ $r != 0 ]; then
         echo "$b $set: encoder error $r"; fail=1; continue
      fi
      if ! $FLAC -t -s out/$b.$tag.flac 2>/dev/null; then
         echo "$b $set: flac -t FAILED"; fail=1
      fi
      $FLAPUS -s -f out/$b.$tag.flac out/$b.$tag.wav
      $FLAC -d -s -f -o out/$b.$tag.ref.wav out/$b.$tag.flac 2>/dev/null
      m=$($PYTHON ../tools/pcmcmp.py out/$b.$tag.wav "$w")
      r=$($PYTHON ../tools/pcmcmp.py out/$b.$tag.ref.wav "$w")
      if [ "$m" != IDENTICAL ] || [ "$r" != IDENTICAL ]; then
         echo "$b $set: flapus $m / flac $r"; fail=1
      fi
      rm -f out/$b.$tag.wav out/$b.$tag.ref.wav
   done
done
[ $fail = 0 ] && echo "all FLAC cases OK"
exit $fail
