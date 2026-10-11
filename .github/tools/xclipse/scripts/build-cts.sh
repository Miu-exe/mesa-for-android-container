#!/bin/bash
# Cross-compile the Vulkan CTS (deqp-vk) for the phone.
#   WORK=... ./build-cts.sh linux                 -> Debian arm64 (glibc) build, for RADV in the container
#   WORK=... NDK=/path/to/ndk ./build-cts.sh android -> native Android build, for Samsung's own driver:
#        ./deqp-vk --deqp-vk-library-path=/system/lib64/libvulkan.so ...
# The Android build uses the headless target with the NDK toolchain; bionic lacks glibc's
# __assert_fail and separate librt/libpthread, which a force-included shim and empty archives cover.
DOCKER=${DOCKER:-docker}  # e.g. DOCKER="sudo docker" if you are not in the docker group
set -eo pipefail
TOOLS=$(dirname "$(dirname "$(readlink -f "$0")")")
WORK=${WORK:-$HOME/mesa-xclipse-build}
TAG=${CTS_TAG:-vulkan-cts-1.4.6.2}
mkdir -p "$WORK/cts-ccache"
if [ ! -d "$WORK/cts" ]; then
  git clone -q --depth 1 -b "$TAG" https://github.com/KhronosGroup/VK-GL-CTS.git "$WORK/cts"
  (cd "$WORK/cts" && python3 external/fetch_sources.py)
fi
case "${1:-}" in
linux)
  cp "$TOOLS/docker/aarch64-toolchain.cmake" "$WORK/"
  $DOCKER run --rm -u "$(id -u):$(id -g)" -v "$WORK":/b -e CCACHE_DIR=/b/cts-ccache -e HOME=/b mesa-cross-arm64-gl bash -c '
    cmake -S /b/cts -B /b/cts-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/b/aarch64-toolchain.cmake \
      -DDEQP_TARGET=default >/dev/null && ninja -C /b/cts-linux deqp-vk'
  echo "built: $WORK/cts-linux/external/vulkancts/modules/vulkan/deqp-vk (+ ../vulkan data, mustpass in cts/external/vulkancts/mustpass)" ;;
android)
  : "${NDK:?set NDK=/path/to/android-ndk (r27c tested)}"
  mkdir -p "$WORK/bionic-stubs"
  AR=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar
  for l in rt pthread; do "$AR" rcs "$WORK/bionic-stubs/lib$l.a"; done
  cat > "$WORK/cts-bionic-shim.h" <<'SHIM'
/* Force-included into the Android (bionic) CTS build: glibc-only calls mapped onto bionic. */
#pragma once
#include <assert.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func)
{
   __assert2(file, (int)line, func, expr);
}
#ifdef __cplusplus
}
#endif
SHIM
  $DOCKER run --rm -u "$(id -u):$(id -g)" -v "$WORK":/b -v "$(readlink -f "$NDK")":/ndk:ro -e CCACHE_DIR=/b/cts-ccache \
    -e HOME=/b mesa-cross-arm64-gl bash -c '
    cmake -S /b/cts -B /b/cts-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/ndk/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static -DDEQP_TARGET=vulkan_headless \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      -DCMAKE_C_FLAGS="-include /b/cts-bionic-shim.h" -DCMAKE_CXX_FLAGS="-include /b/cts-bionic-shim.h" \
      -DCMAKE_EXE_LINKER_FLAGS="-L/b/bionic-stubs" >/dev/null && ninja -C /b/cts-android deqp-vk'
  mkdir -p "$WORK/cts-android-pkg"
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" -o "$WORK/cts-android-pkg/deqp-vk" \
    "$WORK/cts-android/external/vulkancts/modules/vulkan/deqp-vk"
  echo "built: $WORK/cts-android-pkg/deqp-vk (stripped)" ;;
*) echo "usage: $0 linux|android"; exit 1 ;;
esac
