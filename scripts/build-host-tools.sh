#!/bin/sh
# Builds the Filament host tools (matc, resgen, cmgen, glslminifier) that the WebAssembly
# build of Filament and of WebAR-Canvas runs at build time.
#
# Run it inside the emscripten/emsdk image, as build-docker.js and the CI do. It uses emsdk's
# own clang as the native compiler, with GCC's libstdc++ (USE_STATIC_LIBCXX=OFF), so the tools
# link against the image's glibc and run in the same container as the WASM build.
#
# Filament records the tools' absolute paths in third_party/filament/ImportExecutables-Release.cmake,
# so the WASM build must see the repository at the same path as this script did.
#
# Does nothing when the tools are already built.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
filament="$root/third_party/filament"
host="$filament/out/host"
tools="matc resgen cmgen glslminifier"

missing=""
for tool in $tools; do
  if [ -f "$host/tools/$tool/$tool" ]; then
    chmod +x "$host/tools/$tool/$tool"
  else
    missing="$missing $tool"
  fi
done
if [ -z "$missing" ] && [ -f "$filament/ImportExecutables-Release.cmake" ]; then
  echo "[host-tools] already built in $host"
  exit 0
fi
echo "[host-tools] building:${missing:- (import file missing)}"

# With core.autocrlf=true (Windows checkouts) Filament's shell scripts get CRLF line endings
# and the static-library combine step fails with "/usr/bin/env: 'bash\r'".
combine="$filament/build/linux/combine-static-libs.sh"
if grep -q "$(printf '\r')" "$combine"; then
  echo "[host-tools] converting $combine to LF"
  sed -i 's/\r$//' "$combine"
fi

clang_dir="${EMSDK:-/emsdk}/upstream/bin"
mkdir -p "$host"
cd "$host"
CC="$clang_dir/clang" CXX="$clang_dir/clang++" cmake ../.. \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_STATIC_LIBCXX=OFF \
  -DFILAMENT_SUPPORTS_VULKAN=OFF \
  -DFILAMENT_SKIP_SAMPLES=ON \
  -DFILAMENT_SKIP_SDL2=ON
# shellcheck disable=SC2086 # $tools is a list of targets
cmake --build . -j "$(nproc)" --target $tools
