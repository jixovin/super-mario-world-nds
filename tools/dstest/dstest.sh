#!/bin/bash
# dstest.sh outdir steps...  : run /tmp/emu/test.nds (test build) in melonDS.
OUT=$1; shift
rm -rf "$OUT"; mkdir -p "$OUT"
[ -n "$KEEP_SAVE" ] || rm -rf /tmp/emu/sd/_nds
export MAP=/tmp/emu/test.map
export GRAM=$(grep -E "^\s+0x[0-9a-f]+\s+g_ram$" $MAP | awk '{print $1}')
SDDIR=/tmp/emu/sd /tmp/emu/melonds_headless /tmp/emu/test.nds "$OUT" "$@" 2>&1 | grep -vE "unmapping|^$|incomplete SD|Secure area|now booting"
