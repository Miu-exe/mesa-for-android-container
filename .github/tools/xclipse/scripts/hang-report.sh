#!/bin/bash
# After a hang reboot: pull an Android bugreport (no root needed) and print the end of the previous
# boot's kernel log (LAST KMSG): GPU, ACPM and ITMON messages before the watchdog reset.
#   ADB_SERIAL=<serial> hang-report.sh [out dir]      (default: ./hang-<time>)
S=${ADB_SERIAL:?set ADB_SERIAL to the adb serial of the phone}
OUT=${1:-hang-$(date +%m%d-%H%M)}
set -e
mkdir -p "$OUT"
adb -s $S bugreport "$OUT/br.zip" >/dev/null
mkdir -p "$OUT/x" && unzip -oq "$OUT/br.zip" -d "$OUT/x"
f=$(ls "$OUT"/x/dumpstate-*.txt | head -1)
sed -n '/^------ LAST KMSG/,/^------ .*was the duration of .LAST KMSG/p' "$f" > "$OUT/last_kmsg.txt"
echo "saved $OUT/last_kmsg.txt ($(wc -l < "$OUT/last_kmsg.txt") lines)"
# The reset reason, then the messages that matter before it.
grep -m3 "RESET CAUSE\|rst_stat\|WDTRESET" "$OUT/last_kmsg.txt" || true
grep -n "acpm_ipc timeout\|Target Address\|> Target \|Type   *:\|Error code\|sgpu\|amdgpu\|\*ERROR\|page fault\|WDTRESET right now" \
   "$OUT/last_kmsg.txt" | grep -v "MIN_REQUEST" | tail -25
