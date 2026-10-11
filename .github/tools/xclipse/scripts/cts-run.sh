#!/bin/bash
# Resumable Vulkan CTS runner for a phone whose GPU hangs reboot the whole device.
#
# Usage: cts-run.sh <group.txt> [group.txt...]   (paths relative to $CTS/vk-default)
#
# RADV in the Debian container (defaults):  cts-run.sh memory.txt
# Samsung's driver, Android build in Termux (oracle for the same cases):
#   CTS=~/cts-android OUT=~/cts-results-samsung \
#   DEQP_EXTRA="--deqp-vk-library-path=/system/lib64/libvulkan.so" cts-run.sh memory.txt
# Results: $OUT/results.csv  (test,status)   Status: Pass/Fail/NotSupported/QualityWarning/
#          CompatibilityWarning/Crash/Timeout/Hang/...
#
# Chunks of $CHUNK tests run $JOBS at a time. A "running" marker is synced to disk before a chunk
# starts; if the phone reboots mid-chunk, that chunk is re-run one test at a time with a sync before
# every test, which pins down the test that took the GPU down (recorded as Hang and skipped).
#
# Flight recorder: in that one-test-at-a-time mode RADV_DEBUG=dumpibs is enabled and its output is
# written with O_SYNC to $OUT/flight/<case>.txt, so after the reboot RADV's command streams for the
# hanging case are still on disk. Files of cases that finish normally are deleted.
set -u
CTS=${CTS:-/opt/cts}
OUT=${OUT:-$HOME/cts-results}
CHUNK=${CHUNK:-200}
JOBS=${JOBS:-3}
TEST_TIMEOUT=${TEST_TIMEOUT:-120}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp}
# RADV in the container unless another driver is selected (DEQP_EXTRA with a library path).
if [ -z "${DEQP_EXTRA:-}" ] && [ -f /opt/radv/radv_icd.json ]; then
   export VK_DRIVER_FILES=${VK_DRIVER_FILES:-/opt/radv/radv_icd.json}
fi
mkdir -p "$OUT/chunks" "$OUT/running" "$OUT/done" "$OUT/flight"
touch "$OUT/results.csv"

DEQP_ARGS=(--deqp-log-images=disable --deqp-log-shader-sources=disable --deqp-surface-type=pbuffer
           --deqp-shadercache=disable ${DEQP_EXTRA:-})

# Append results for every finished case in a qpa log.
parse_qpa() {
   awk '
      /^#beginTestCaseResult / { name = $2 }
      /<Result StatusCode="/ { match($0, /StatusCode="[^"]*"/); st = substr($0, RSTART + 12, RLENGTH - 13) }
      /^#endTestCaseResult/ { if (name != "") print name "," st; name = "" }
      /^#terminateTestCaseResult / { if (name != "") print name "," $2; name = "" }
   ' "$1"
}

# Name of a case that was started but never finished in a qpa log (crash), if any.
unfinished_case() {
   awk '/^#beginTestCaseResult / { name = $2 } /^#(end|terminate)TestCaseResult/ { name = "" }
        END { if (name != "") print name }' "$1"
}

record() { # record <name> <status>
   echo "$1,$2" >> "$OUT/results.csv"
   sync "$OUT/results.csv" 2>/dev/null || sync
}

# Run the given case list file in one deqp-vk process; loop until every case has a result.
run_list() {
   local list=$1 id=$2 left=$OUT/chunks/$id.left qpa=$OUT/chunks/$id.qpa
   cp "$list" "$left"
   while [ -s "$left" ]; do
      rm -f "$qpa"
      local rc
      if [ -n "${FLIGHT:-}" ]; then
         # IB dumps go to stderr; dd with oflag=sync puts every write on disk before the next.
         (cd $CTS && RADV_DEBUG=dumpibs timeout -s KILL $((TEST_TIMEOUT * 4)) \
            ./deqp-vk "${DEQP_ARGS[@]}" --deqp-caselist-file="$left" --deqp-log-filename="$qpa" 2>&1 >/dev/null |
            dd of="$FLIGHT" oflag=sync status=none; exit ${PIPESTATUS[0]})
         rc=$?
      else
         (cd $CTS && timeout -s KILL $((TEST_TIMEOUT * $(wc -l < "$left") / 4 + TEST_TIMEOUT)) \
            ./deqp-vk "${DEQP_ARGS[@]}" --deqp-caselist-file="$left" --deqp-log-filename="$qpa" >/dev/null 2>&1)
         rc=$?
      fi
      [ -f "$qpa" ] && parse_qpa "$qpa" >> "$OUT/results.csv"
      sync "$OUT/results.csv" 2>/dev/null || sync   # results must survive a GPU hang that reboots the phone
      local bad=""
      [ -f "$qpa" ] && bad=$(unfinished_case "$qpa")
      if [ -n "$bad" ]; then
         record "$bad" "$([ $rc = 137 ] && echo Timeout || echo Crash)"
      fi
      # Remove cases that now have a result.
      cut -d, -f1 "$OUT/results.csv" | sort -u > "$OUT/chunks/$id.have" 2>/dev/null
      sort "$left" | comm -23 - "$OUT/chunks/$id.have" > "$left.new"
      if [ -z "$bad" ] && [ -s "$left.new" ] && cmp -s <(sort "$left") "$left.new"; then
         # No progress and no identifiable culprit: mark the first case and move on.
         record "$(head -1 "$left.new")" "NoResult"
         tail -n +2 "$left.new" > "$left.new2" && mv "$left.new2" "$left.new"
      fi
      mv "$left.new" "$left"
   done
   rm -f "$left" "$qpa" "$OUT/chunks/$id.have"
}

# Careful mode after a reboot: one process per case, synced marker before each.
run_careful() {
   local list=$1 id=$2
   cut -d, -f1 "$OUT/results.csv" | sort -u > "$OUT/chunks/$id.have"
   sort "$list" | comm -23 - "$OUT/chunks/$id.have" | while read -r t; do
      if [ -f "$OUT/running/$id.case" ] && [ "$(cat "$OUT/running/$id.case")" = "$t" ]; then
         record "$t" "Hang"      # this exact case was running when the phone went down
         echo "[$(date +%T)] Hang: $t (RADV IBs: $OUT/flight/$t.txt)"
         continue
      fi
      echo "$t" > "$OUT/running/$id.case"; sync
      echo "$t" > "$OUT/chunks/$id.one"
      FLIGHT="$OUT/flight/$t.txt" run_list "$OUT/chunks/$id.one" "$id.one"
      rm -f "$OUT/flight/$t.txt"   # finished normally: nothing to keep
   done
   rm -f "$OUT/running/$id.case" "$OUT/chunks/$id.have" "$OUT/chunks/$id.one"
}

run_chunk() {
   local chunk=$1 id; id=$(basename "$chunk")
   [ -f "$OUT/done/$id" ] && return
   if [ -f "$OUT/running/$id" ]; then
      echo "[$(date +%T)] $id was running during a reboot -> careful mode"
      run_careful "$chunk" "$id"
   else
      echo run > "$OUT/running/$id"; sync
      run_list "$chunk" "$id"
   fi
   rm -f "$OUT/running/$id"; touch "$OUT/done/$id"; sync
}

for group in "$@"; do
   name=$(echo "$group" | tr '/' '_' | sed 's/\.txt$//')
   if [ ! -f "$OUT/chunks/$name.split" ]; then
      split -l "$CHUNK" -d -a 5 "$CTS/vk-default/$group" "$OUT/chunks/$name-"
      touch "$OUT/chunks/$name.split"; sync
   fi
   chunks=("$OUT"/chunks/"$name"-[0-9][0-9][0-9][0-9][0-9])   # exactly the split files
   [ -e "${chunks[0]}" ] || chunks=()
   echo "[$(date +%T)] group $group: ${#chunks[@]} chunks"
   for chunk in "${chunks[@]}"; do
      while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
      run_chunk "$chunk" &
   done
   wait
   echo "[$(date +%T)] group $group finished"
done
