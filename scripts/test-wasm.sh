#!/bin/sh
# Builds and runs the Node test runners: nft_tests (ARNFTTracker), core_tests (src/core) and
# kpm_worker_tests (the KPM worker module, src/worker), and builds the worker module itself.
#
# Run it inside the emscripten/emsdk image, as `node build-docker.js --test` and the CI do
# (after scripts/build-host-tools.sh for the Filament renderer; sokol needs no host tools).
# Exits with the number of failed checks.
#
# Usage: [WEBAR_RENDERER=...] [BUILD_DIR=dir] [WEBAR_DEBUG_INFO=OFF|ON] scripts/test-wasm.sh [jobs]
# The runners do not use a renderer; BUILD_DIR (default build-wasm/) is shared with build-wasm.sh.
# Give it the same WEBAR_DEBUG_INFO as the app's build (build-docker.js does): it reconfigures the
# folder but builds only the test targets, so the app keeps the setting it was last linked with.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)

jobs=${1:-$(nproc)}
# The same directory as build-wasm.sh: per renderer, relative to the repository.
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
sh "$root/scripts/build-wasm.sh" "$jobs" nft_tests
sh "$root/scripts/build-wasm.sh" "$jobs" core_tests
sh "$root/scripts/build-wasm.sh" "$jobs" kpm_worker_tests
# The worker module itself, so a link failure shows here too (videoTest.kpm.js/.wasm).
sh "$root/scripts/build-wasm.sh" "$jobs" kpm_worker

# Run every runner even if one fails; the exit code is the sum of their failures.
failures=0
node "$build_dir/nft_tests.js" || failures=$((failures + $?))
node "$build_dir/core_tests.js" || failures=$((failures + $?))
node "$build_dir/kpm_worker_tests.js" || failures=$((failures + $?))
# Exit codes are taken mod 256: cap the total so failures never read as success.
[ "$failures" -gt 255 ] && failures=255
exit "$failures"
