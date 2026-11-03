#!/bin/bash
# warp_ds.sh OUTDIR LEVEL [frames buttons] [more steps...]: enter LEVEL from the map in the
# test build, run, then save DS picture + reference and compare.
HERE=$(cd "$(dirname "$0")" && pwd); W=${SMW_TEST_DIR:-/tmp/emu}  # test files: test.nds/.map, sd/smw_assets.dat
OUT=$1; L=$2; N=${3:-120}; B=${4:-right}; shift 4 2>/dev/null
CAP="set:g_test_ref_on,1 4 set:g_test_pause,1 16 shot refshot set:g_test_pause,0 set:g_test_ref_on,0"
$HERE/dstest.sh $OUT mode:07 90 10:start mode:08 40 10:start mode:0a 40 10:a mode:14 900 10:b mode:0e 90 \
  set:g_smw_nds_force_level,$((0x8000 | 0x$L)),2 10:b mode:14:1500 $N:$B $CAP "$@" | tail -2
python3 $HERE/compare.py $OUT
