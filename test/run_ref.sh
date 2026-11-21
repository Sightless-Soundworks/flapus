#!/bin/sh
# Re-encodes ../../mand.wav with the settings of the reference files and compares the audio.
mkdir -p out
enc() { ../flapus.exe -s -f "$@"; }
enc --bitrate 1 ../../mand.wav out/mand5.opus
enc --bitrate 24 ../../mand.wav out/mand24.opus
enc ../../mand.wav out/mand96.opus
enc --bitrate 1000 ../../mand.wav out/mand510.opus
for b in 5 24 96 510; do
   echo "mand$b: $(python ../tools/oggcmp.py out/mand$b.opus ../../mand$b.opus)"
done
