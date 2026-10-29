#!/bin/bash
# Build the SMW_NDS_TEST variant to /tmp/emu/test.nds (+ .map), leaving no objects behind for the release build.
cd /home/claude/smwds/nds && touch source/*.c && /home/claude/dsbuild.sh EXTRA_DEFS=-DSMW_NDS_TEST && cp smw-nds.nds /tmp/emu/test.nds && cp build/smw-nds.map /tmp/emu/test.map && touch source/*.c
