#!/bin/sh
# Builds and runs the Node test runners: nft_tests (ARNFTTracker) and core_tests (src/core).
#
# Run it inside the emscripten/emsdk image after scripts/build-host-tools.sh, as
# `node build-docker.js --test` and the CI do. Exits with the number of failed checks.
#
# Usage: scripts/test-wasm.sh [jobs]
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)

jobs=${1:-$(nproc)}
sh "$root/scripts/build-wasm.sh" "$jobs" nft_tests
sh "$root/scripts/build-wasm.sh" "$jobs" core_tests

# Run both runners even if the first fails; the exit code is the sum of their failures.
failures=0
node "$root/build-wasm/nft_tests.js" || failures=$((failures + $?))
node "$root/build-wasm/core_tests.js" || failures=$((failures + $?))
exit "$failures"
