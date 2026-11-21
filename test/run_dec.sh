#!/bin/sh
# Decodes the reference Opus files in the parent folder and compares with the reference WAVs.
# FLAPUS overrides the flapus binary (default ../flapus), PYTHON the Python interpreter.
FLAPUS=${FLAPUS:-../flapus}
PYTHON=${PYTHON:-$(command -v python3 || echo python)}
mkdir -p out
for b in 5 24 96 510; do
   for r in 44 48; do
      rate=48000
      [ $r = 44 ] && rate=44100
      $FLAPUS -s -f --rate $rate ../../mand$b.opus out/dec_$b-$r.wav
      echo "mand$b-$r: $($PYTHON ../tools/wavcmp.py out/dec_$b-$r.wav ../../mand$b-$r.wav)"
   done
done
