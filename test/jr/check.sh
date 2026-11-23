#!/bin/sh
# test/jr/check.sh ROMDIR [OUT] -- the game itself, from your own MAME set
# (ROMDIR/dkongjr.zip; it's read, never copied), against MAME. The kit's
# test/run.sh can't run the game: its model allows a list one TV frame, and
# these run back to back, a little longer (src/jrcv.h).
#
#   1. the board: MAME's dkongjr and src/jr.c play test/jr/play1.txt (a coin,
#      a start, then 6000 frames of a random stick and jump); the Z80's RAM
#      and the 8035 must match at every vertical blank
#   2. the plugin's lists: test/jr/jrlists makes 3000 as the cart does
#      (src/jrplay.c), checking the picture drawn a part at a time against
#      a whole drawing, then tools/mame-replay.sh plays them to the SDK's
#      kernel on MAME's ColecoVision, whose picture must be the model's
#   3. how loud: the replay's sound next to the arcade's (tools/wavlevel.py)
#
# MAME runs headless (no window, no sound, a time limit), as the kit's
# scripts run it. Needs mame and, for step 2, the ColecoVision BIOS:
# MAME_ROMPATH, as for test/run.sh. Output in OUT (default
# $TMPDIR/dkongjr-check).
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
roms=$(cd "${1:?usage: test/jr/check.sh ROMDIR [OUT]}" && pwd)
zip=$roms/dkongjr.zip
[ -f "$zip" ] || { echo "no $zip"; exit 1; }
out=${2:-${TMPDIR:-/tmp}/dkongjr-check}
rm -rf "$out"; mkdir -p "$out/m" "$out/jr" "$out/cv"
script=$(cat "$here/play1.txt")
status=0

"$here/build.sh" "$out/jrhost"
"$here/build-lists.sh" "$out/jrlists"

echo "1. the board against MAME's dkongjr, 6000 frames"
JR_WAV=$out/m/arcade.wav "$here/mame.sh" "$roms" "$out/m" 6000 "$script" 3000
"$out/jrhost" "$zip" "$out/jr" 6000 --script "$script" --holds "$out/m/holds.txt" >"$out/jr/log.txt"
python3 -I "$here/cmpdump.py" "$out/jr/dump.bin" "$out/m/dump.bin" >"$out/jr/cmp.txt" || status=1
sed 's/^/   /' "$out/jr/cmp.txt"

echo "2. 3000 lists, and the SDK's kernel on MAME's ColecoVision"
"$out/jrlists" "$zip" "$out/cv" 3000 --script "$script" --snaps 3000 >"$out/cv/log.txt"
sed 's/^/   /' "$out/cv/log.txt"
grep -q ' 0 unlike a whole drawing, 0 with the HUD unlike, 0 converted unlike' "$out/cv/log.txt" || status=1
MAME_WAV=$out/cv/replay.wav "$top/tools/mame-replay.sh" "$out/cv/lists.bin" "$out/cv" 60 >"$out/cv/mame.log" 2>&1 || status=1
grep 'lists run' "$out/cv/mame.log" | sed 's/^/   /'
python3 -I "$top/tools/png256.py" same "$out/cv/cv-3000.png" "$out/cv/mame-256x192.png" >"$out/cv/same.txt" || status=1
sed 's/^/   /' "$out/cv/same.txt"

echo "3. how loud (dBFS; an ordinary cartridge is about -22, docs/testing.md)"
python3 -I "$top/tools/wavlevel.py" "$out/m/arcade.wav" "$out/cv/replay.wav" | sed 's/^/   /'

[ $status = 0 ] && echo "all checks passed ($out)" || echo "FAILED ($out)"
exit $status
