#!/bin/sh
# Configures and builds WebAR-Canvas for WebAssembly into build-wasm/.
#
# Run it inside the emscripten/emsdk image after scripts/build-host-tools.sh, as
# build-docker.js and the CI do. Uses Ninja when it is installed, Unix Makefiles otherwise.
#
# Usage: scripts/build-wasm.sh [jobs]
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
jobs=${1:-$(nproc)}

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
fi

emcmake cmake -S "$root" -B "$root/build-wasm" -G "$generator" \
  -DWEBGL=1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Emscripten \
  -DMATC_EXEC="$root/third_party/filament/out/host/tools/matc/matc"
cmake --build "$root/build-wasm" -j "$jobs" --target WebAR-Canvas
