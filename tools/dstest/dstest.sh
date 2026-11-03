#!/bin/bash
# dstest.sh outdir steps...  : run $W/test.nds (test build) in melonDS.
HERE=$(cd "$(dirname "$0")" && pwd); W=${SMW_TEST_DIR:-/tmp/emu}  # test files: test.nds/.map, sd/smw_assets.dat
OUT=$1; shift
rm -rf "$OUT"; mkdir -p "$OUT"
[ -n "$KEEP_SAVE" ] || rm -rf $W/sd/_nds
export MAP=${MAP:-$W/test.map}; NDS=${NDS:-$W/test.nds}
export GRAM=$(grep -E "^\s+0x[0-9a-f]+\s+g_ram$" $MAP | awk '{print $1}')
SDDIR=$W/sd $HERE/melonds_headless $NDS "$OUT" "$@" 2>&1 | grep -vE "unmapping|^$|incomplete SD|Secure area|now booting"
