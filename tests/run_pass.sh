#!/usr/bin/env bash
# Build+run the positive suite with HC on x86_64 and ARM64(qemu); audit each .o.
set -euo pipefail
HC=/workspace/hc/build/current/hc
CI="-I/workspace/scratch/hc-herominde/include"
OUT=/workspace/scratch/hc/pass-out
mkdir -p "$OUT"
NM=nm; ANM=aarch64-linux-gnu-nm
LOCKRE='pthread_mutex|pthread_rwlock|pthread_cond|pthread_spin|pthread_barrier|sem_|mtx_|cnd_|flock|lockf|dlopen|dlsym'
run_one() { # <mode> <src> <isrun>
  local mode="$1" src="$2" base; base=$(basename "$src" | sed 's/\.[ch]*$//')
  local obj exe tgt link runner
  if [ "$mode" = cross ]; then tgt="--target=aarch64-linux-gnu"; link=aarch64-linux-gnu-gcc; runner="qemu-aarch64-static"; obj="$OUT/${base}.a64.o"; exe="$OUT/${base}.a64"; lf="-static";
  else tgt=""; link=gcc; runner=""; obj="$OUT/${base}.o"; exe="$OUT/${base}"; lf=""; fi
  "$HC" $tgt $CI -c "$src" -o "$obj"
  # external nm zero-lock check
  local bad
  bad=$( ( $NM -u "$obj" 2>/dev/null || $ANM -u "$obj" 2>/dev/null ) | awk '{print $NF}' | sed 's/@.*//' | grep -E "^($LOCKRE)" || true)
  [ -z "$bad" ] || { echo "  nm LOCK in $obj: $bad"; return 1; }
  "$HC" --audit-elf "$obj" >/dev/null
  $link $lf -o "$exe" "$obj" -pthread
  echo "  [$mode] $(basename "$src"): $($runner "$exe" | tail -1)"
}
for mode in host cross; do
  echo "== $mode =="
  for s in tests/pass/*.c; do run_one "$mode" "$s"; done
done
echo "PASS SUITE DONE"
