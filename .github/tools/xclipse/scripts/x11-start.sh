#!/data/data/com.termux/files/usr/bin/bash
# Termux: start a fresh Termux:X11 server on :0 (if none runs) and bring its activity to the front.
# Open the activity *before* starting the server: until the activity connects, the server
# re-sends its start broadcast every second, and the app rejects the second one with
# "Termux:X11 already has an active X server connection" (it counts the server's own first,
# still pending broadcast), so the server exits.
# Don't run `pgrep -f`/`pkill -f` for the server's name in the same ssh command line as this
# script: they match the ssh shell itself.
if pgrep -f "termux-x11 com.termux.x11" >/dev/null; then
   am start -n com.termux.x11/com.termux.x11.MainActivity >/dev/null 2>&1
   exit 0
fi
rm -f $PREFIX/tmp/.X11-unix/X0 $PREFIX/tmp/.X0-lock
am start -n com.termux.x11/com.termux.x11.MainActivity >/dev/null 2>&1
sleep 2
nohup termux-x11 :0 -ac >$HOME/x11.log 2>&1 </dev/null &
for i in $(seq 10); do
   sleep 1
   [ -S $PREFIX/tmp/.X11-unix/X0 ] && pgrep -f "termux-x11 com.termux.x11" >/dev/null && { echo "X up"; exit 0; }
done
echo "X failed"; tail -5 $HOME/x11.log; exit 1
