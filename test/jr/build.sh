#!/bin/sh
# test/jr/build.sh OUT -- the board (src/jr.c) and test/jr/jrhost.c for the Mac
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
sdk=$top/sdk/firmware/revc/sdk
cc -std=c11 -O2 -g -Wall -Wextra -Wno-unused-function -I"$top/src" -I"$sdk" \
    -DJR_SND_PARTS -include "$here/trace.h" -DZ80_TRACE=jr_z80_trace -DM48_TRACE=jr_m48_trace \
    "$top/src/jr.c" "$top/src/jrroms.c" "$top/src/jrsnd.c" "$sdk/swzip.c" "$here/jrhost.c" -lz -o "${1:?usage: build.sh OUT}"
