#!/bin/sh
# FLAC tests: encodes every edge case at several settings, validates with the reference
# flac decoder (flac -t), and compares decoded samples (flapus and flac -d) with the input.
# Usage: run_flac.sh [case dir]   (FLAC overrides the reference flac path)
FLAC=${FLAC:-flac}
dir=${1:-flac_cases}
[ -d "$dir" ] || python make_flac_cases.py "$dir" > /dev/null
mkdir -p out
fail=0
for w in "$dir"/*.wav; do
   b=$(basename "$w" .wav)
   for set in "--level 0" "--level 3" "--level 8" "--level 8 --lax"; do
      tag=$(echo "$set" | tr -d ' -')
      ../flapus.exe -s $set "$w" out/$b.$tag.flac
      r=$?
      if [ $r != 0 ]; then
         echo "$b $set: encoder error $r"; fail=1; continue
      fi
      if ! $FLAC -t -s out/$b.$tag.flac 2>/dev/null; then
         echo "$b $set: flac -t FAILED"; fail=1
      fi
      ../flapus.exe -s out/$b.$tag.flac out/$b.$tag.wav
      $FLAC -d -s -f -o out/$b.$tag.ref.wav out/$b.$tag.flac 2>/dev/null
      m=$(python ../tools/pcmcmp.py out/$b.$tag.wav "$w")
      r=$(python ../tools/pcmcmp.py out/$b.$tag.ref.wav "$w")
      if [ "$m" != IDENTICAL ] || [ "$r" != IDENTICAL ]; then
         echo "$b $set: flapus $m / flac $r"; fail=1
      fi
      rm -f out/$b.$tag.wav out/$b.$tag.ref.wav
   done
done
[ $fail = 0 ] && echo "all FLAC cases OK"
