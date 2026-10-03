#!/usr/bin/env bash
# Downloads Freedoom (free, BSD-licensed Doom-compatible game data) and checks it.
set -euo pipefail

version=0.13.0
url="https://github.com/freedoom/freedoom/releases/download/v$version/freedoom-$version.zip"
sha256=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59
out=${1:-wads}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

mkdir -p "$out"
wget -q -O "$tmp/freedoom.zip" "$url"
echo "$sha256  $tmp/freedoom.zip" | sha256sum --check --quiet
unzip -q -o "$tmp/freedoom.zip" -d "$tmp"
mv "$tmp/freedoom-$version/freedoom1.wad" "$tmp/freedoom-$version/freedoom2.wad" "$out/"
mv "$tmp/freedoom-$version/COPYING.txt" "$out/FREEDOOM-COPYING.txt"
mv "$tmp/freedoom-$version/CREDITS.txt" "$out/FREEDOOM-CREDITS.txt"
