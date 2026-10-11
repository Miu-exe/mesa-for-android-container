#!/bin/bash
# Run inside Debian: repeat the WebGL test to measure how often the GPU hangs under a setting.
# Usage: loop.sh <runs> [label]   (RADV_DEBUG etc. from the environment, e.g. RADV_DEBUG=nogpl,nodcc)
# Every start and result is appended to /root/webgl-loop.log and synced before the next step, so
# after a hang (which reboots the phone) the last "start" line without a result is the culprit.
N=${1:-10}; LABEL=${2:-${RADV_DEBUG-nogpl}}
LOG=/root/webgl-loop.log
for i in $(seq 1 $N); do
   echo "$(date +%T) start $LABEL #$i" >> $LOG; sync
   /root/run-nw.sh /root/webgltest > /tmp/loop-run.log 2>&1
   rc=$?
   res=$(grep -o "XTEST \(raw webgl\|pixi draw\|stress fps\): [A-Z0-9.]*" /tmp/loop-run.log | sed 's/XTEST //' | tr '\n' ' ')
   echo "$(date +%T) done  $LABEL #$i rc=$rc $res" >> $LOG; sync
   sleep 2
done
