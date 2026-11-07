#!/bin/sh
# FLAC -> Opus: compares opusenc reading a FLAC file with flapus converting the same file
# (audio packets and OpusHead, including the ReplayGain -> output gain rule).
# Usage: run_flac_opus.sh   (OPUSENC / FLAC / METAFLAC override the reference tools)
OPUSENC=${OPUSENC:-opusenc}
FLAC=${FLAC:-flac}
METAFLAC=${METAFLAC:-metaflac}
mkdir -p out
check() {  # check <flac file> <opusenc options...>
   f=$1; shift
   $OPUSENC --quiet "$@" "$f" out/fo_ref.opus 2>/dev/null
   ../flapus.exe -s "$@" "$f" out/fo_mine.opus
   echo "$(basename "$f") $*: $(python ../tools/oggcmp.py out/fo_mine.opus out/fo_ref.opus | tail -1)"
}
for w in st44.wav mono16.wav ext/s24.wav ext/u8.wav ext/ch3.wav ext/ch6.wav ext/ch8.wav ext/st96000.wav; do
   b=$(basename $w .wav)
   $FLAC -s -f --channel-map=none -o out/$b.ref.flac $w 2>/dev/null
   ../flapus.exe -s --level 4 $w out/$b.mine.flac
   check out/$b.ref.flac
   check out/$b.mine.flac --bitrate 48
done
# ReplayGain tags become the OpusHead output gain and R128_TRACK_GAIN like in opusenc
cp out/st44.ref.flac out/rg.flac
$METAFLAC --set-tag="REPLAYGAIN_TRACK_GAIN=-6.37 dB" --set-tag="REPLAYGAIN_ALBUM_GAIN=+2.11 dB" out/rg.flac
check out/rg.flac
cp out/st44.ref.flac out/rg2.flac
$METAFLAC --set-tag="REPLAYGAIN_ALBUM_GAIN=-1.234 dB" --set-tag="REPLAYGAIN_REFERENCE_LOUDNESS=85.0 dB" out/rg2.flac
check out/rg2.flac
