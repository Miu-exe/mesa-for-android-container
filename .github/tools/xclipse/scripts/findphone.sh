#!/bin/bash
# Find the phone's Termux sshd on the LAN (its IP changes after every reboot) and update ~/.ssh/config.
ssh -o ConnectTimeout=4 -o BatchMode=yes phone true 2>/dev/null && exit 0
for try in 1 2 3; do
  NET=${NET:-192.168.0}   # the first three parts of the LAN's addresses
IP=$(for i in $(seq 2 254); do (timeout 2 bash -c "echo > /dev/tcp/$NET.$i/8022" 2>/dev/null && echo "$NET.$i") & done; wait)
  IP=$(echo "$IP" | head -1)
  if [ -n "$IP" ]; then
    sed -i "/^Host phone$/,/^Host /s/^    HostName .*/    HostName $IP/" ~/.ssh/config
    ssh-keyscan -p 8022 "$IP" >> ~/.ssh/known_hosts 2>/dev/null
    echo "phone at $IP"; exit 0
  fi
  sleep 20
done
exit 1
