#!/bin/bash
# Cross-compile RADV (arm64, Debian trixie) from this repo and optionally push it to the phone.
#
#   WORK=/path/to/builddir ./build-radv.sh          # build only
#   PHONE=phone WORK=... ./build-radv.sh            # build and install to Debian /opt/radv on the phone
#   VK_LAYERS=overlay ...                           # also build Mesa Vulkan layers (installed to /opt/radv/layers)
#
# PHONE is an ssh destination for Termux's sshd (e.g. a ~/.ssh/config alias with Port 8022).
# The install goes through `proot-distro login debian` so it lands inside the container.
DOCKER=${DOCKER:-docker}  # e.g. DOCKER="sudo docker" if you are not in the docker group
set -eo pipefail
TOOLS=$(dirname "$(dirname "$(readlink -f "$0")")")
MESA=$(cd "$TOOLS/../../.." && pwd)
WORK=${WORK:-$HOME/mesa-xclipse-build}
mkdir -p "$WORK/ccache"
rsync -a --delete --exclude .git "$MESA/" "$WORK/src/"
cp "$TOOLS/docker/cross-aarch64.ini" "$WORK/"
$DOCKER run --rm -u "$(id -u):$(id -g)" -v "$WORK":/work -e CCACHE_DIR=/work/ccache -e HOME=/work \
  -e VK_LAYERS="${VK_LAYERS:-}" mesa-cross-arm64 bash -c '
  set -e
  if [ ! -f /work/build-radv/build.ninja ]; then
    cd /work/src && meson setup /work/build-radv --cross-file /work/cross-aarch64.ini --prefix=/usr \
      --libdir=lib/aarch64-linux-gnu -Dbuildtype=debugoptimized -Dvulkan-drivers=amd -Dgallium-drivers= \
      -Dplatforms=x11 -Dllvm=disabled -Dopengl=false -Dglx=disabled -Degl=disabled -Dgles1=disabled \
      -Dgles2=disabled -Dgbm=disabled -Dvulkan-layers=$VK_LAYERS -Dvideo-codecs= -Dlibunwind=disabled \
      -Dvalgrind=disabled -Dintel-rt=disabled -Dmicrosoft-clc=disabled -Dbuild-tests=false
  fi
  meson configure /work/build-radv -Dvulkan-layers=$VK_LAYERS >/dev/null
  ninja -C /work/build-radv'
echo '{"ICD":{"api_version":"1.4.0","library_path":"/opt/radv/libvulkan_radeon.so"},"file_format_version":"1.0.1"}' > "$WORK/radv_icd.json"
echo "built: $WORK/build-radv/src/amd/vulkan/libvulkan_radeon.so"
# Mesa layers: library + a manifest pointing at /opt/radv/layers.
rm -rf "$WORK/layers" && mkdir -p "$WORK/layers"
for so in "$WORK"/build-radv/src/vulkan/*/libVkLayer_*.so; do
  [ -e "$so" ] || continue
  cp "$so" "$WORK/layers/"
  json=$(ls "$(dirname "$so")"/VkLayer_*.json 2>/dev/null | head -1)
  [ -n "$json" ] && sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"/opt/radv/layers/$(basename "$so")\"|" "$json" > "$WORK/layers/$(basename "$json")"
done
if [ -n "${PHONE:-}" ]; then
  tar -C "$WORK" -czf - radv_icd.json layers -C "$WORK/build-radv/src/amd/vulkan" libvulkan_radeon.so |
    ssh "$PHONE" 'proot-distro login debian -- bash -c "mkdir -p /opt/radv && tar -xzf - -C /opt/radv --no-same-owner"'
  echo "installed to $PHONE:/opt/radv (use VK_DRIVER_FILES=/opt/radv/radv_icd.json, VK_ADD_LAYER_PATH=/opt/radv/layers)"
fi
