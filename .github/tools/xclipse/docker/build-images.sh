#!/bin/bash
# Build the Debian trixie arm64 cross-compile images:
#   mesa-cross-arm64     - RADV / Vulkan-only Mesa builds, test programs
#   mesa-cross-arm64-gl  - + GL/EGL/GLX deps and CMake (Mesa with Zink/radeonsi, Vulkan CTS)
DOCKER=${DOCKER:-docker}  # e.g. DOCKER="sudo docker" if you are not in the docker group
set -e
cd "$(dirname "$(readlink -f "$0")")"
$DOCKER build -t mesa-cross-arm64 -f Dockerfile.radv .
$DOCKER build -t mesa-cross-arm64-gl -f Dockerfile.gl .
