#!/bin/bash
# sweep_level.sh LEVEL [LOG]: warp into LEVEL (star power on), run right/jump,
# compare 6 DS pictures with the reference; append a summary line to LOG.
L=$1; LOG=${2:-/tmp/sweep.log}; OUT=/tmp/sw/$L
CAP="set:g_test_ref_on,1 4 set:g_test_pause,1 16 shot refshot set:g_test_pause,0 set:g_test_ref_on,0"
LATE=$(grep -E "^\s+0x[0-9a-f]+\s+g_test_late$" /tmp/emu/test.map | awk '{print $1}')
STEPS=""
for i in 1 2 3 4 5 6; do STEPS="$STEPS 90:right+y 25:right+y+b $CAP"; done
R=$(timeout 300 /home/claude/dstest.sh $OUT mode:07 90 10:start mode:08 40 10:start mode:0a 40 10:a mode:14 900 10:b mode:0e 90 \
  set:g_smw_nds_force_level,$((0x8000 | 0x$L)),2 10:b mode:14:1500 pin:1490,ff $STEPS mem:$LATE ram:100 2>&1)
late=$(echo "$R" | grep -o "\]=[0-9]*" | head -1 | cut -c3-)
mode=$(echo "$R" | grep -o "\[100\]=.." | tail -1)
cmp=$(python3 /home/claude/compare.py $OUT 2>/dev/null | grep -o "mismatch=[0-9.]*" | cut -d= -f2 | tr '\n' ' ')
echo "$L late=$late $mode mism: $cmp" >> $LOG
# keep only comparison images of mismatching pictures
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
