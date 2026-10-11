#!/bin/bash
# Usage: ADB_SERIAL=<serial> phone-up.sh   (phone on USB, "Host phone" going through an adb forward)
# After a phone reboot (phone on USB, unlocked): start sshd in Termux via adb input, then test ssh.
# Retries: right after boot the phone is busy and typed input can get lost.
S=${ADB_SERIAL:?set ADB_SERIAL to the adb serial of the phone (adb devices)}
adb -s $S wait-for-device
until adb -s $S shell 'dumpsys window | grep -q "isKeyguardShowing=false"' 2>/dev/null; do echo "unlock the phone..."; sleep 5; done
for try in 1 2 3 4 5 6; do
   ssh -o BatchMode=yes -o ConnectTimeout=5 phone true 2>/dev/null && break
   adb -s $S shell monkey -p com.termux -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1; sleep 5
   adb -s $S shell "input text 'termux-wake-lock;sshd'" && adb -s $S shell input keyevent 66; sleep 5
done
ssh -o BatchMode=yes -o ConnectTimeout=8 phone 'echo "phone up: $(uptime)"'
