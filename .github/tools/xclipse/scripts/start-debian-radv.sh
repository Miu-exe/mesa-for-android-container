#!/data/data/com.termux/files/usr/bin/bash
# Start Termux:X11, then enter Debian with Vulkan on the Xclipse GPU through RADV (/opt/radv).
# Usage: ~/start-debian-radv.sh            -> Debian shell
#        ~/start-debian-radv.sh vkcube     -> run a command directly
if ! pgrep -f "[c]om.termux.x11" >/dev/null; then
   rm -f "$PREFIX/tmp/.X11-unix/X0" "$PREFIX/tmp/.X0-lock"
   termux-x11 :0 -ac >"$HOME/x11.log" 2>&1 &
   sleep 3
fi
am start -n com.termux.x11/com.termux.x11.MainActivity >/dev/null 2>&1
ENV="DISPLAY=:0 XDG_RUNTIME_DIR=/tmp VK_DRIVER_FILES=/opt/radv/radv_icd.json"
if [ $# -gt 0 ]; then
   exec proot-distro login debian --shared-tmp -- env $ENV "$@"
else
   exec proot-distro login debian --shared-tmp -- env $ENV bash -l
fi
