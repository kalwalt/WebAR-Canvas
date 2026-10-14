#!/bin/sh
# Builds and runs the Node test runner of ARNFTTracker (target nft_tests).
#
# Run it inside the emscripten/emsdk image after scripts/build-host-tools.sh, as
# `node build-docker.js --test` and the CI do. Exits with the number of failed checks.
#
# Usage: scripts/test-wasm.sh [jobs]
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)

sh "$root/scripts/build-wasm.sh" "${1:-$(nproc)}" nft_tests
node "$root/build-wasm/nft_tests.js"
