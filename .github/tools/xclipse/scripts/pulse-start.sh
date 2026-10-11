#!/data/data/com.termux/files/usr/bin/bash
# Termux: start PulseAudio with Android output (AAudio; PULSE_SINK_MODULE=module-sles-sink as a
# fallback) and a TCP listener on 127.0.0.1:4713 for the Debian container (PULSE_SERVER=127.0.0.1).
# Checks go through TCP: a plain `pactl` autospawns a daemon with the default config (dummy output
# only), and `pulseaudio --check`/`pgrep -x` don't see the daemon (it runs as linker64).
# timeout: a hung daemon (e.g. after an audio route change) would block pactl forever.
up() { PULSE_SERVER=127.0.0.1 timeout 3 pactl info >/dev/null 2>&1; }
if up; then echo "pulse up"; exit 0; fi
pkill -9 -f "[b]in/pulseaudio"; sleep 1  # they ignore SIGTERM
rm -f $PREFIX/tmp/pulse-*/native $PREFIX/tmp/pulse-*/pid
pulseaudio --start --exit-idle-time=-1 -n \
   --load="${PULSE_SINK_MODULE:-module-aaudio-sink}" \
   --load="module-native-protocol-unix" \
   --load="module-native-protocol-tcp auth-ip-acl=127.0.0.1 auth-anonymous=1" >$HOME/pulse.log 2>&1
for i in 1 2 3 4 5; do sleep 1; up && break; done
if up; then
   echo "pulse up"; PULSE_SERVER=127.0.0.1 timeout 3 pactl list short sinks
else
   echo "pulse failed"; tail -5 $HOME/pulse.log; exit 1
fi
