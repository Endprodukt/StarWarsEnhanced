#!/bin/sh
# pack.sh - make the cabinet's package of the game: dist/swenhanced-<version>.vpk
# (external/VectorPie/sdk/docs/07-packages.md). Run from the project's folder
# in WSL or on Linux, after the Pi build (`cmake --build --preset pi-clang`):
# the package is staged where files keep their modes, which a Windows folder
# does not. On the cabinet the file goes to /persistent/games/packages/ (or
# onto a USB drive, in its games/ folder); the menu installs it from there:
# Settings -> GAMES -> INSTALL GAMES.
set -eu
cd "$(dirname "$0")/.."
[ -f build-pi/swenhanced ] || { echo "pack.sh: no build-pi/swenhanced (build for the Pi first)" >&2; exit 1; }
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/swenhanced"
cp build-pi/swenhanced package/game.ini package/swenhanced.ini package/marquee.png "$stage/swenhanced/"
cp -r package/sounds "$stage/swenhanced/sounds"     # (the recordings that replace the board's sounds)
find "$stage/swenhanced/sounds" -type d -exec chmod 755 {} + -o -type f -exec chmod 644 {} +
# The owner's film music and film lines (package/sounds/music and voice, on the PC
# only, never on GitHub) only with WITH_MUSIC=1: a package to share has the board's
# own music and speech (PMSF2 is the collection's torpedo, it stays).
if [ "${WITH_MUSIC:-0}" != 1 ]; then
    [ -d "$stage/swenhanced/sounds/music" ] && find "$stage/swenhanced/sounds/music" -name '*.wav' ! -name 'PMSF2.wav' -delete
    rm -rf "$stage/swenhanced/sounds/voice"
fi
chmod 755 "$stage/swenhanced/swenhanced"
chmod 644 "$stage/swenhanced/game.ini" "$stage/swenhanced/swenhanced.ini" "$stage/swenhanced/marquee.png"
# (a Windows checkout may give the SDK's script and the manifest CR LF line ends)
tr -d '\r' < package/game.ini > "$stage/swenhanced/game.ini"
tr -d '\r' < external/VectorPie/sdk/tools/vp-pack/vp-pack.sh > "$stage/vp-pack.sh"
# (with the music the package is the owner's own: dist/private/, apart from the one to share)
out=dist
if [ "${WITH_MUSIC:-0}" = 1 ]; then out=dist/private; mkdir -p "$out"; fi
sh "$stage/vp-pack.sh" "$stage/swenhanced" "$out"
