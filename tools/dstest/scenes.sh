#!/bin/bash
# All non-level scenes. LOG=/tmp/scenes.log
LOG=${LOG:-/tmp/scenes.log}; : > $LOG
BOOT="mode:07 90 10:start mode:08 40 10:start mode:0a 40 10:a mode:14 900 10:b mode:0e 90"
INLV="$BOOT 10:left 120 10:b mode:14 60"
S=/home/claude/sweep_scene.sh
$S title $LOG "40" 8 60 &
$S demo $LOG "mode:07 200" 6 120
wait
for N in 0 1 2 3 4 5 6; do
  $S map$N $LOG "$INLV pin:1f11,0$N pin:100,0b 1 unpin:100 mode:0e unpin:1f11 60" 4 40 right &
  [ $((N % 2)) = 1 ] && wait
done
wait
for N in 1 2 3 4 5 6 7; do
  $S castle$N $LOG "$INLV pin:13c6,0$N pin:100,18 1 unpin:100 unpin:13c6" 6 300 &
  [ $((N % 2)) = 0 ] && wait
done
wait
$S ending $LOG "$INLV pin:13c6,08 pin:100,18 1 unpin:100 unpin:13c6" 24 900
echo DONE >> $LOG
