#!/bin/sh
# test/jr/build-arm.sh OUT.elf -- the plugin's work for a list (src/jrplay.c)
# for the cart's Cortex-M33, with the pico-sdk's flags and the plugin's -O2,
# for test/jr/armcount.py
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
sdk=$top/sdk/firmware/revc/sdk
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp \
    -O2 -std=c11 -ffunction-sections -fdata-sections -I"$top/src" -I"$sdk" \
    "$top/src/jr.c" "$top/src/jrsnd.c" "$top/src/jrcv.c" "$top/src/jrdraw.c" "$top/src/jrplay.c" "$sdk/cvscale.c" "$here/arm/bench.c" \
    -nostartfiles -T "$here/arm/bench.ld" --specs=nano.specs -Wl,--gc-sections -lm -o "${1:?usage: build-arm.sh OUT.elf}"
