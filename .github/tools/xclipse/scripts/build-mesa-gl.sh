#!/bin/bash
# Cross-compile Mesa with RADV + Zink + radeonsi (no LLVM) and GLX/EGL/GBM into $PREFIX
# (default /opt/mesa-xclipse), and optionally install it on the phone (PHONE=<ssh destination>).
# A second build next to the first: PREFIX=/opt/<name> WORK=<another build directory>.
DOCKER=${DOCKER:-docker}  # e.g. DOCKER="sudo docker" if you are not in the docker group
set -eo pipefail
TOOLS=$(dirname "$(dirname "$(readlink -f "$0")")")
MESA=$(cd "$TOOLS/../../.." && pwd)
WORK=${WORK:-$HOME/mesa-xclipse-build}
PREFIX=${PREFIX:-/opt/mesa-xclipse}
mkdir -p "$WORK/ccache"
rsync -a --delete --exclude .git "$MESA/" "$WORK/src/"
cp "$TOOLS/docker/cross-aarch64.ini" "$WORK/"
$DOCKER run --rm -u "$(id -u):$(id -g)" -v "$WORK":/work -e CCACHE_DIR=/work/ccache -e HOME=/work -e PREFIX="$PREFIX" mesa-cross-arm64-gl bash -c '
  set -e
  if [ ! -f /work/build-gl/build.ninja ]; then
    cd /work/src && meson setup /work/build-gl --cross-file /work/cross-aarch64.ini --prefix="$PREFIX" \
      --libdir=lib -Dbuildtype=debugoptimized -Dvulkan-drivers=amd -Dgallium-drivers=zink,radeonsi \
      -Dplatforms=x11 -Dllvm=disabled -Dopengl=true -Dglx=dri -Degl=enabled -Dgles1=disabled -Dgles2=enabled \
      -Dgbm=enabled -Dglvnd=disabled -Dvulkan-layers= -Dvideo-codecs= -Dgallium-va=disabled \
      -Dgallium-mediafoundation=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dintel-rt=disabled \
      -Dmicrosoft-clc=disabled -Dbuild-tests=false
  fi
  ninja -C /work/build-gl
  rm -rf /work/gl-install && DESTDIR=/work/gl-install meson install -C /work/build-gl --no-rebuild >/dev/null'
if [ -n "${PHONE:-}" ]; then
  tar -C "$WORK/gl-install$(dirname "$PREFIX")" -czf - "$(basename "$PREFIX")" |
    ssh "$PHONE" "proot-distro login debian -- bash -c \"rm -rf $PREFIX && tar -xzf - -C $(dirname "$PREFIX") --no-same-owner\""
  echo "installed to $PHONE:$PREFIX"
fi
