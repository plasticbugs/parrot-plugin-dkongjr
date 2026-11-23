#!/bin/sh
# test/jr/mame.sh ROMDIR OUT FRAMES [SCRIPT] [SNAPS] -- MAME's dkongjr, headless,
# dumped by dump.lua into OUT/dump.bin, snapshots as OUT/mame-F.png
# (MAME never opens a window: the kit's headless flags, and a time limit)
set -e
here=$(cd "$(dirname "$0")" && pwd)
roms=${1:?usage: mame.sh ROMDIR OUT FRAMES [SCRIPT] [SNAPS]}
out=$(mkdir -p "$2" && cd "$2" && pwd)
frames=${3:?}
secs=$(( frames / 60 + 30 ))
rm -rf "$out/snap"
JR_OUT=$out JR_FRAMES=$frames JR_SCRIPT=${4:-} JR_SNAPS=${5:-} SDL_VIDEODRIVER=dummy mame dkongjr -rompath "$roms" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run "$secs" \
    -snapshot_directory "$out/snap" -snapname "%i" \
    -cfg_directory "$out/cfg" -nvram_directory "$out/nvram" ${JR_WAV:+-wavwrite "$JR_WAV"} \
    -debug -debugger none -oslog \
    -autoboot_script "$here/dump.lua" >"$out/mame.log" 2>&1 || true
# the sprite copy's holds (for jrhost --holds)
grep -E '^[WR] [0-9]+$' "$out/mame.log" >"$out/holds.txt" || true
grep -E '^N [0-9]+$' "$out/mame.log" >"$out/nmi.txt" || true
# the snapshots come out numbered in order: name them by frame
i=0
for fr in $(echo "${5:-}" | tr ',' ' '); do
    src=$(printf '%s/snap/%04d.png' "$out" "$i")
    [ -f "$src" ] && mv "$src" "$out/mame-$fr.png"
    i=$((i + 1))
done
