#!/bin/sh
# Configures and builds WebAR-Canvas for WebAssembly into build-wasm/.
#
# Run it inside the emscripten/emsdk image after scripts/build-host-tools.sh, as
# build-docker.js and the CI do. Uses Ninja when it is installed, Unix Makefiles otherwise.
# The test target (nft_tests) is configured too, but only built when asked for.
#
# Usage: scripts/build-wasm.sh [jobs] [target]   (target: WebAR-Canvas by default)
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
jobs=${1:-$(nproc)}
target=${2:-WebAR-Canvas}

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
fi

emcmake cmake -S "$root" -B "$root/build-wasm" -G "$generator" \
  -DWEBGL=1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Emscripten \
  -DMATC_EXEC="$root/third_party/filament/out/host/tools/matc/matc" \
  -DWEBAR_CANVAS_BUILD_TESTS=ON
cmake --build "$root/build-wasm" -j "$jobs" --target "$target"
