#!/bin/bash
# sweep_scene.sh NAME LOG "SETUP STEPS" COUNT INTERVAL [BUTTONS]: run setup, then
# COUNT captures INTERVAL frames apart; compare with the reference.
NAME=$1; LOG=$2; SETUP=$3; COUNT=$4; IV=$5; B=${6:-none}; OUT=/tmp/sc/$NAME
CAP="set:g_test_ref_on,1 4 set:g_test_pause,1 16 shot refshot set:g_test_pause,0 set:g_test_ref_on,0"
LATE=$(grep -E "^\s+0x[0-9a-f]+\s+g_test_late$" ${MAP:-/tmp/emu/test.map} | awk '{print $1}')
STEPS=""
for i in $(seq $COUNT); do STEPS="$STEPS $IV:$B $CAP"; done
R=$(timeout 1200 /home/claude/dstest.sh $OUT $SETUP $STEPS mem:$LATE ram:100 2>&1)
late=$(echo "$R" | grep -o "\]=[0-9]*" | head -1 | cut -c3-)
mode=$(echo "$R" | grep -o "\[100\]=.." | tail -1)
cmp=$(python3 /home/claude/compare.py $OUT 2>/dev/null | grep -o "mismatch=[0-9.]*" | cut -d= -f2 | tr '\n' ' ')
echo "$NAME late=$late $mode mism: $cmp" >> $LOG
python3 - "$OUT" <<'PY'
import sys, glob, os, re, subprocess
out = sys.argv[1]
res = subprocess.run(['python3', '/home/claude/compare.py', out], capture_output=True, text=True).stdout
keep = set()
for line in res.splitlines():
    m = re.match(r'(\S+)\.ppm crop=\d+ mismatch=([\d.]+)', line)
    if m and float(m.group(2)) > 0.3: keep.add(m.group(1) + '_cmp.png')
for f in glob.glob(out + '/*'):
    if os.path.basename(f) not in keep: os.remove(f)
PY
