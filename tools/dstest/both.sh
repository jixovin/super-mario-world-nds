#!/bin/bash
# both.sh LEVEL: compare the C port with the original code every frame (RM_BOTH)
L=$1; S=/home/claude/ex/out/dp_$L/s.txt; D=/tmp/both/$L; mkdir -p $D
SMW_COMPARE=1 THEIRS=/home/claude/ref/smw/smw.sfc WAV=$D/w.wav timeout 300 /home/claude/smwds/tools/hostrun/hostrun /tmp/emu/sd/smw_assets.dat $D $S > $D/log 2>&1
rm -f $D/w.wav $D/*.ppm
n=$(grep -c "Verify failure" $D/log); first=$(grep -m1 "compare failed" $D/log | cut -d: -f1)
echo "$L failures=$n first=$first $(grep -m1 -A3 'compare failed' $D/log | tail -3 | tr '\n' ' ')"
