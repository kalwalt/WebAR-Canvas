#!/bin/sh
# Configures and builds WebAR-Canvas for WebAssembly.
#
# Run it inside the emscripten/emsdk image, as build-docker.js and the CI do. The Filament
# renderer needs scripts/build-host-tools.sh first; the sokol renderer needs no host tools.
# Uses Ninja when it is installed, Unix Makefiles otherwise.
# The test targets (nft_tests, core_tests, kpm_worker_tests, kpm_worker_browser) are configured too,
# but only built when asked for.
#
# Usage: [WEBAR_RENDERER=filament|sokol] [BUILD_DIR=dir] scripts/build-wasm.sh [jobs] [target]
#   WEBAR_RENDERER  the rendering backend (default: filament)
#   BUILD_DIR       the build directory, relative to the repository unless absolute
#                   (default: build-wasm/, or build-wasm-<renderer>/ for another renderer)
#   target          WebAR-Canvas by default
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
jobs=${1:-$(nproc)}
target=${2:-WebAR-Canvas}
renderer=${WEBAR_RENDERER:-filament}
if [ "$renderer" = "filament" ]; then
  build_dir=${BUILD_DIR:-build-wasm}
else
  build_dir=${BUILD_DIR:-build-wasm-$renderer}
fi
case "$build_dir" in
  /*) ;;
  *) build_dir="$root/$build_dir" ;;
esac

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
fi

# matc is a Filament host tool: only the Filament renderer uses it.
matc_option=""
if [ "$renderer" = "filament" ]; then
  matc_option="-DMATC_EXEC=$root/third_party/filament/out/host/tools/matc/matc"
fi

# $matc_option is unquoted on purpose: empty for sokol, one word for Filament.
emcmake cmake -S "$root" -B "$build_dir" -G "$generator" \
  -DWEBGL=1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Emscripten \
  -DWEBAR_RENDERER="$renderer" \
  $matc_option \
  -DWEBAR_CANVAS_BUILD_TESTS=ON
cmake --build "$build_dir" -j "$jobs" --target "$target"
