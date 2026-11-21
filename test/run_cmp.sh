#!/bin/sh
# Encodes with opusenc and with flapus, then compares the audio.
# Usage: run_cmp.sh <wav> <opusenc options...>   (OPUSENC overrides the reference path; FLAPUS
# the flapus binary, default ../flapus; PYTHON the Python interpreter)
REF=${OPUSENC:-opusenc}
FLAPUS=${FLAPUS:-../flapus}
PYTHON=${PYTHON:-$(command -v python3 || echo python)}
w=$1; shift
id=$(date +%s%N)
mkdir -p out
$REF --quiet "$@" $w out/ref_$id.opus 2>/dev/null
$FLAPUS -s -f "$@" $w out/mine_$id.opus 2>/dev/null
r=$($PYTHON ../tools/oggcmp.py out/mine_$id.opus out/ref_$id.opus 2>&1 | tail -1)
echo "$w $*: $r"
cp out/ref_$id.opus ref.opus 2>/dev/null; cp out/mine_$id.opus mine.opus 2>/dev/null
rm -f out/ref_$id.opus out/mine_$id.opus
