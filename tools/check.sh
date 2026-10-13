#!/bin/sh
# check.sh - build (linux preset), run the tests, and compare the native
# game with a MAME recording if one is given. Run inside WSL/Linux from
# anywhere:
#
#   sh tools/check.sh [CAPTURE.swcap [ROMDIR [swcompare options]]]
#
# The recording comes from tools/mame/swcapture.lua (README.md).
cd "$(dirname "$0")/.." || exit 1
cmake --preset linux > /dev/null || exit 1
cmake --build --preset linux 2>&1 | grep -E "error|warning|FAILED" -A6
ctest --preset linux 2>&1 | tail -n 3
[ -n "$1" ] || exit 0
cap=$1
roms=${2:-roms}
[ $# -ge 2 ] && shift 2 || shift 1
./build-linux/swcompare "$cap" --roms "$roms" "$@"
