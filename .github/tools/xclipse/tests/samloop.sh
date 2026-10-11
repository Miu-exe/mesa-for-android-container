#!/data/data/com.termux/files/usr/bin/bash
# Control: Samsung driver (Android build of vktri) in a loop, results synced to ~/sam-loop.log
LOG=$HOME/sam-loop.log; cd $HOME
for i in $(seq 1 ${1:-50}); do
   echo "$(date +%T) start samsung vktri #$i" >> $LOG; sync
   out=$(timeout 20 $HOME/xt/bin/vktri 2>&1 | grep -c "pixel(32,12) = 110 115  30")
   echo "$(date +%T) done  #$i ok=$out" >> $LOG; sync
done
