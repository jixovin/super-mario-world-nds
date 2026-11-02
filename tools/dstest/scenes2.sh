#!/bin/bash
# Special scenes reached by gameplay events. LOG=/tmp/scenes4.log
LOG=${LOG:-/tmp/scenes4.log}; : > $LOG
S=/home/claude/sweep_scene.sh
BOOT="mode:07 90 10:start mode:08 40 10:start mode:0a 40 10:a mode:14 900 10:b mode:0e 90"
WARP105="$BOOT set:g_smw_nds_force_level,$((0x8105)),2 10:b mode:14:1500 30"
# bonus game: exit with the bonus flag set
$S bonus $LOG "$WARP105 pin:1425,01 pin:141a,01 pin:100,0f 1 unpin:100 unpin:141a mode:14 unpin:1425" 6 120 &
# Yoshi wings area
$S wings $LOG "$WARP105 pin:1b95,01 pin:141a,01 pin:100,0f 1 unpin:100 unpin:141a mode:14 unpin:1b95" 6 120
wait
# goal tape + score tally in YI1 (star power, run right)
$S goal $LOG "$BOOT 10:left 120 10:b mode:14 60 pin:1490,ff 1800:right+y" 8 60 &
# Yellow Switch Palace: switch + message + save prompt (bot route)
$S ysp $LOG "$(tr '\n' ' ' < /home/claude/route_ysp_save.txt | sed 's/ shot / /g; s/ shot$//')" 1 30
wait
echo DONE >> $LOG
