#!/usr/bin/env bash
# Downloads id's freely distributable shareware IWAD (Doom 1.9, episode 1) and checks it.
set -euo pipefail

url='https://archive.org/download/doomworld/doomworld.tar/doomworld%2Fports%2Fshareware_doom_iwad.zip'
sha1=5b2e249b9c5133ec987b3ea77596381dc0d6bc1d
out=${1:-wads/DOOM1.WAD}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

mkdir -p "$(dirname "$out")"
wget -q -O "$tmp/shareware.zip" "$url"
unzip -q -o "$tmp/shareware.zip" -d "$tmp"
echo "$sha1  $tmp/DOOM1.WAD" | sha1sum --check --quiet
mv "$tmp/DOOM1.WAD" "$out"
