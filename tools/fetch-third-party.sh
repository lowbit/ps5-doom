#!/usr/bin/env bash
# Downloads the libraries built into the launcher and checks them: libarchive, xz's liblzma and zlib
# for the importer, qrcodegen for the address shown on the upload screen.
set -euo pipefail

out=${1:-.deps/third-party}
mkdir -p "$out"

fetch() {
  local name=$1 sha256=$2
  shift 2
  if [ -f "$out/$name/.complete" ]; then
    return
  fi
  local tmp
  tmp=$(mktemp -d)
  for url in "$@"; do
    if wget -q -O "$tmp/source.tar.gz" "$url" &&
       echo "$sha256  $tmp/source.tar.gz" | sha256sum --check --quiet; then
      rm -rf "${out:?}/$name"
      tar -xzf "$tmp/source.tar.gz" -C "$out"
      touch "$out/$name/.complete"
      rm -rf -- "$tmp"
      return
    fi
  done
  rm -rf -- "$tmp"
  echo "could not fetch $name" >&2
  exit 1
}

fetch libarchive-3.8.9 f5a6539059cf5e597dbeda37bfa4874b1e8dea063c8d93bf85a2b44af90a5bd4 \
  https://www.libarchive.org/downloads/libarchive-3.8.9.tar.gz \
  https://github.com/libarchive/libarchive/releases/download/v3.8.9/libarchive-3.8.9.tar.gz
fetch xz-5.8.4 0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9 \
  https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz \
  https://downloads.sourceforge.net/project/lzmautils/xz-5.8.4.tar.gz
fetch zlib-1.3.2 bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16 \
  https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz \
  https://zlib.net/zlib-1.3.2.tar.gz
fetch QR-Code-generator-1.8.0 2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60   https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz   https://codeload.github.com/nayuki/QR-Code-generator/tar.gz/refs/tags/v1.8.0
