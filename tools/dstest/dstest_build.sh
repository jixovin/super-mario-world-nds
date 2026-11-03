#!/bin/bash
# Build the SMW_NDS_TEST variant to $SMW_TEST_DIR/test.nds (+ .map), leaving no objects behind for the release build.
HERE=$(cd "$(dirname "$0")" && pwd); W=${SMW_TEST_DIR:-/tmp/emu}  # test files: test.nds/.map, sd/smw_assets.dat
cd "$HERE/../../nds" && touch source/*.c && make EXTRA_DEFS=-DSMW_NDS_TEST && cp smw-nds.nds $W/test.nds && cp build/smw-nds.map $W/test.map && touch source/*.c
