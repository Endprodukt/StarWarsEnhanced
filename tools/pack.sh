#!/bin/sh
# pack.sh - make the cabinet's package of the game: dist/starwarsvp-<version>.vpk
# (external/VectorPie/sdk/docs/07-packages.md). Run from the project's folder
# in WSL or on Linux, after the Pi build (`cmake --build --preset pi-clang`):
# the package is staged where files keep their modes, which a Windows folder
# does not. On the cabinet the file goes to /persistent/games/packages/ (or
# onto a USB drive, in its games/ folder); the menu installs it from there:
# Settings -> GAMES -> INSTALL GAMES.
set -eu
cd "$(dirname "$0")/.."
[ -f build-pi/starwarsvp ] || { echo "pack.sh: no build-pi/starwarsvp (build for the Pi first)" >&2; exit 1; }
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/starwarsvp"
cp build-pi/starwarsvp package/game.ini package/starwarsvp.ini package/marquee.png "$stage/starwarsvp/"
chmod 755 "$stage/starwarsvp/starwarsvp"
chmod 644 "$stage/starwarsvp/game.ini" "$stage/starwarsvp/starwarsvp.ini" "$stage/starwarsvp/marquee.png"
# (a Windows checkout may give the SDK's script and the manifest CR LF line ends)
tr -d '\r' < package/game.ini > "$stage/starwarsvp/game.ini"
tr -d '\r' < external/VectorPie/sdk/tools/vp-pack/vp-pack.sh > "$stage/vp-pack.sh"
sh "$stage/vp-pack.sh" "$stage/starwarsvp" dist
