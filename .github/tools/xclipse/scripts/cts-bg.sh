#!/data/data/com.termux/files/usr/bin/bash
# (Re)start the resumable CTS run inside Debian. Safe to run again after a reboot.
termux-wake-lock
pgrep -f "bash ./[c]ts-run.sh" >/dev/null && { echo "already running"; exit 0; }
nohup proot-distro login debian -- bash -c "cd /opt/cts && ./cts-run.sh \$(cat order.txt)" >> ~/cts-run.log 2>&1 &
echo started
