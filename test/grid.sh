#!/bin/sh
for b in 6 8 12 16 20 24 32 48 64 96 128 256; do sh run_cmp.sh st44.wav --bitrate $b; done
for b in 6 12 24 64; do for f in 10 40 60; do sh run_cmp.sh st44.wav --bitrate $b --framesize $f; done; done
for b in 32 128; do for f in 2.5 5; do sh run_cmp.sh st44.wav --bitrate $b --framesize $f; done; done
for w in mono44.wav mono16.wav mono8.wav st48.wav; do for b in 8 12 16 24 32 64; do sh run_cmp.sh $w --bitrate $b; done; done
for b in 16 32 96; do sh run_cmp.sh st44.wav --bitrate $b --cvbr; sh run_cmp.sh st44.wav --bitrate $b --hard-cbr; done
for c in 0 3 6; do sh run_cmp.sh st44.wav --bitrate 16 --comp $c; sh run_cmp.sh mono16.wav --bitrate 64 --comp $c; done
sh run_cmp.sh mono16.wav --bitrate 12 --framesize 40 --hard-cbr
sh run_cmp.sh st44.wav --bitrate 24 --framesize 60 --cvbr
# frame sizes beyond 60 ms
for f in 80 100 120; do sh run_cmp.sh st44.wav --bitrate 32 --framesize $f; sh run_cmp.sh mono16.wav --bitrate 12 --framesize $f; done
# input rates and sample formats (resampler, 8/24-bit)
for f in ext/st8000.wav ext/st11025.wav ext/st22050.wav ext/st32000.wav ext/st96000.wav ext/mo12000.wav ext/mo24000.wav ext/u8.wav ext/s24.wav; do sh run_cmp.sh $f --bitrate 48; done
# encoder options
for a in "--music --bitrate 12" "--speech --bitrate 24" "--expect-loss 10 --bitrate 16" "--expect-loss 25 --bitrate 48" "--no-phase-inv --bitrate 48" "--downmix-mono --bitrate 24"; do sh run_cmp.sh st44.wav $a; done
# surround downmix
for c in 3 6 8; do sh run_cmp.sh ext/ch$c.wav --bitrate 32; sh run_cmp.sh ext/ch$c.wav --downmix-mono --bitrate 40; done
# surround and discrete multistream
for c in 3 4 5 6 7 8 10 12; do sh run_cmp.sh ext/ch$c.wav; done
# more than 8 channels: downmix to mono only; bitrates above 256 kbit/s per channel
for a in "ext/ch10.wav --downmix-stereo --bitrate 64" "ext/ch12.wav --bitrate 96" "st44.wav --bitrate 600" "mono16.wav --bitrate 400"; do sh run_cmp.sh $a; done
for a in "ext/ch6.wav --bitrate 192 --hard-cbr" "ext/ch6.wav --bitrate 160 --cvbr" "ext/ch6.wav --bitrate 256 --framesize 10" "ext/ch6.wav --bitrate 200 --framesize 60" "ext/ch6.wav --bitrate 300 --framesize 2.5" "ext/ch8.wav --bitrate 100 --framesize 40 --hard-cbr" "ext/ch6.wav --bitrate 40 --speech" "ext/ch3.wav --bitrate 30 --speech" "ext/ch6_16000.wav --bitrate 60 --speech" "ext/ch8_8000.wav --bitrate 90 --hard-cbr" "ext/ch6.wav --bitrate 1500"; do sh run_cmp.sh $a; done
echo DONE
