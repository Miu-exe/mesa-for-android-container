#!/data/data/com.termux/files/usr/bin/bash
# Capture the GPU command streams Samsung's own Vulkan driver submits for given CTS cases, as the
# known-good reference for cases that hang or fail on RADV. Runs in Termux with the Android build
# of deqp-vk (~/cts-android) and libibdump.so (tests/build.sh android).
#
#   capture-reference.sh dEQP-VK.api.smoke.triangle [more cases or globs...]
#
# Output: ~/cts-android/ref/<case>/ with cs.log, ib_NNNNNN.bin, shader_*.bin, bo_*.bin and the qpa.
set -u
CTS=${CTS:-$HOME/cts-android}
IBDUMP_LIB=${IBDUMP_LIB:-$HOME/xt/bin/libibdump.so}
for c in "$@"; do
   out=$CTS/ref/$c
   rm -rf "$out" && mkdir -p "$out"
   (cd "$CTS" && LD_PRELOAD="$IBDUMP_LIB ${LD_PRELOAD:-}" IBDUMP=cs.log IBDUMP_DIR="$out" IBDUMP_BOS=1 \
      ./deqp-vk --deqp-vk-library-path=/system/lib64/libvulkan.so --deqp-case="$c" \
      --deqp-log-filename="$out/result.qpa" --deqp-log-images=disable >/dev/null 2>&1)
   res=$(grep -o 'StatusCode="[^"]*"' "$out/result.qpa" 2>/dev/null | sort | uniq -c | tr -s ' ' | tr '\n' ' ')
   echo "$c: $(grep -c '^CS' "$out/cs.log" 2>/dev/null) submissions, $(ls "$out" | grep -c '^shader_') shaders, $res"
done
