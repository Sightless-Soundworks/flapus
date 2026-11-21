#!/bin/sh
# Decodes the reference Opus files in the parent folder and compares with the reference WAVs.
mkdir -p out
for b in 5 24 96 510; do
   for r in 44 48; do
      rate=48000
      [ $r = 44 ] && rate=44100
      ../flapus.exe -s -f --rate $rate ../../mand$b.opus out/dec_$b-$r.wav
      echo "mand$b-$r: $(python ../tools/wavcmp.py out/dec_$b-$r.wav ../../mand$b-$r.wav)"
   done
done
