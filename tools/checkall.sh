#!/bin/sh
# checkall.sh - check.sh, then swcompare over every recording in a folder.
# Run inside WSL/Linux:
#
#   sh tools/checkall.sh CAPTUREDIR ROMDIR [swcompare options]
#
# Frames that match are summed up; of the others the first few are shown.
cd "$(dirname "$0")/.." || exit 1
dir=$1
roms=$2
shift 2
SWVP_ROMS="$roms/starwars.zip" sh tools/check.sh || exit 1
for cap in "$dir"/*.swcap; do
    echo "== $(basename "$cap")"
    ./build-linux/swcompare "$cap" --roms "$roms" "$@" | grep -v "frames, 0 differ"
done
