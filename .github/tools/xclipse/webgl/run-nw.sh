#!/bin/bash
# Run inside Debian: NW.js app on Zink (OpenGL) on RADV. Usage: run-nw.sh <app dir> [args]
export DISPLAY=:0 XDG_RUNTIME_DIR=/tmp
export VK_DRIVER_FILES=${VK_DRIVER_FILES:-/opt/radv/radv_icd.json} RADV_DEBUG=${RADV_DEBUG-nogpl}
export LD_LIBRARY_PATH=/opt/mesa-xclipse/lib LIBGL_DRIVERS_PATH=/opt/mesa-xclipse/lib/dri
export MESA_LOADER_DRIVER_OVERRIDE=zink MESA_SHADER_CACHE_DIR=/root/.cache/mesa-xclipse-gl
NW=${NW:-/opt/nwjs/nwjs-v0.111.1-linux-arm64/nw}
APP=$1; shift
exec timeout ${T:-60} $NW --no-sandbox "$APP" "$@"
