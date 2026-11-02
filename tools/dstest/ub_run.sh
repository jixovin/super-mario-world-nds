#!/bin/bash
S=$1; n=$(basename $S .txt); D=/tmp/ub/$n; mkdir -p $D
WAV=$D/w.wav timeout 300 /tmp/hr_ub /tmp/emu/sd/smw_assets.dat $D $S > $D/log 2>&1
rm -f $D/w.wav $D/*.ppm
grep "runtime error" $D/log | grep -v "snes/\|common_rtl.c:542" | sed "s|^|$n: |"
