#!/usr/bin/env bash
# Fetches and builds the external native-app packaging tools at a pinned commit:
# ps5-native-tool (PIE to PS5 module converter, FSELF signer), libc.prx and the PS5 payload SDK.
set -euo pipefail

repo=https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git
commit=b1315a9
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
dest="$root/.deps/native-app"

if [[ ! -d $dest/.git ]]; then
    git -c core.autocrlf=false clone -q "$repo" "$dest"
fi
git -C "$dest" -c advice.detachedHead=false checkout -q "$commit"

bash "$dest/tools/setup-native-dependencies.sh" >/dev/null
bash "$dest/tools/build-host-tools.sh" >/dev/null
[[ -f $dest/runtime/libc.prx ]] || bash "$dest/tools/rebuild-libc.sh" >/dev/null
(cd "$dest/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
