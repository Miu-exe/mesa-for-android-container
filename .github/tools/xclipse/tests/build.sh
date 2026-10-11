#!/bin/bash
# Build the Vulkan test programs.
#   ./build.sh glibc     -> out/glibc/*   (Debian arm64; run inside the proot container, uses RADV)
#   ./build.sh android   -> out/android/* (Termux/Android; run from Termux, uses Samsung's driver)
# glibc needs the mesa-cross-arm64 docker image; android needs NDK=/path/to/android-ndk (r27c tested).
# Shaders are compiled to SPIR-V headers with glslangValidator inside the docker image.
DOCKER=${DOCKER:-docker}  # e.g. DOCKER="sudo docker" if you are not in the docker group
set -e
cd "$(dirname "$(readlink -f "$0")")"
UAPI=$(cd ../../../../include/drm-uapi && pwd)
CAPTURE=$(cd ../capture && pwd)
TESTS="vkcomp vkfill vksample vktexel vktri vkdiscard vkocc vkprim vkvbo"
mkdir -p out/gen
$DOCKER run --rm -u "$(id -u):$(id -g)" -v "$PWD":/t -w /t mesa-cross-arm64 bash -c '
  for f in *.comp *.vert *.frag; do
    n=$(echo "$f" | sed "s/\.comp$//; s/\./_/")
    glslangValidator -V --target-env vulkan1.1 -o out/gen/$n.spv "$f" >/dev/null
    python3 -c "import sys; d=open(\"out/gen/$n.spv\",\"rb\").read(); print(\"static const uint32_t ${n}_spv[] = {\" + \",\".join(str(int.from_bytes(d[i:i+4],\"little\")) for i in range(0,len(d),4)) + \"};\")" > out/gen/$n.h
  done'
case "${1:-glibc}" in
glibc)
  mkdir -p out/glibc
  $DOCKER run --rm -u "$(id -u):$(id -g)" -v "$PWD":/t -v "$UAPI":/uapi:ro -v "$CAPTURE":/capture:ro -w /t mesa-cross-arm64 bash -c "
    for t in $TESTS; do aarch64-linux-gnu-gcc -O1 -DWITH_IBDUMP -Iout/gen -I/uapi -o out/glibc/\$t \$t.c /capture/ibdump.c -lvulkan; done"
  ls out/glibc ;;
android)
  : "${NDK:?set NDK=/path/to/android-ndk}"
  CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
  mkdir -p out/android
  for t in $TESTS; do $CC -O1 -DWITH_IBDUMP -Iout/gen -I"$UAPI" -o out/android/$t $t.c "$CAPTURE/ibdump.c" -lvulkan; done
  $CC -O1 -I"$UAPI" -o out/android/drmtrace "$CAPTURE/drmtrace.c"
  $CC -O1 -shared -fPIC -DIBDUMP_PRELOAD -I"$UAPI" -o out/android/libibdump.so "$CAPTURE/ibdump.c"
  ls out/android ;;
*) echo "usage: $0 glibc|android"; exit 1 ;;
esac
