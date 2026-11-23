#!/bin/sh
# test/jr/build-lists.sh OUT -- the plugin's game loop (src/jrplay.c) and test/jr/jrlists.c for the Mac
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
sdk=$top/sdk/firmware/revc/sdk
cc -std=c11 -O2 -g -Wall -Wextra -Wno-unused-function -I"$top/src" -I"$sdk" \
    "$top/src/jr.c" "$top/src/jrroms.c" "$top/src/jrsnd.c" "$top/src/jrcv.c" "$top/src/jrdraw.c" "$top/src/jrplay.c" \
    "$sdk/cvscale.c" "$sdk/swzip.c" "$here/jrlists.c" -lz -lm -o "${1:?usage: build-lists.sh OUT}"
