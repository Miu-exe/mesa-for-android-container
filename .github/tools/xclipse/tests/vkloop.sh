#!/bin/bash
# Run inside Debian: repeat a Vulkan test program to look for startup hangs. Usage: vkloop.sh <runs> <cmd...>
N=$1; shift; LOG=/root/vk-loop.log
export VK_DRIVER_FILES=${VK_DRIVER_FILES:-/opt/radv/radv_icd.json}
cd /tmp
for i in $(seq 1 $N); do
   echo "$(date +%T) start ${DYN:+DYN }$* #$i" >> $LOG; sync
   out=$(timeout 20 "$@" 2>&1 | tail -1); rc=$?
   echo "$(date +%T) done  #$i $out" >> $LOG; sync
done
