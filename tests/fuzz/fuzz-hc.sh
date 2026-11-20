#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Mutation fuzz of hc (ASan+UBSan build) over the in-tree C sources. Shell = glue only; the mutator
# is tests/fuzz/hc_mutate.c. A sanitizer report or a signal exit (>=128) is a finding; ordinary
# compile errors (rc 1/2) are the expected outcome for mangled input.
# usage: tests/fuzz/fuzz-hc.sh OUTDIR ITERS SEED_FILES...
set -u
cd "$(dirname "$0")/../.."
OUT=${1:?OUTDIR}; IT=${2:?ITERS}; shift 2
mkdir -p "$OUT/cases"
make -s asan ASAN="$OUT/hc-asan" >/dev/null || { echo "FUZZ_HC build failed"; exit 1; }
gcc -std=c11 -O2 -Wall -Wextra -Werror -o "$OUT/hc_mutate" tests/fuzz/hc_mutate.c || exit 1
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export TMPDIR="$OUT"
n=0; bad=0; rejected=0
for s in "$@"; do
  for i in $(seq 1 "$IT"); do
    c="$OUT/cases/case.c"
    "$OUT/hc_mutate" "$s" "$c" "$((n + 1))" || continue
    timeout 20 "$OUT/hc-asan" --check-only -std=c11 -c "$c" -o "$OUT/cases/case.o" > "$OUT/cases/log" 2>&1; rc=$?
    n=$((n + 1))
    if [ $rc -ge 124 ] || grep -q 'runtime error\|AddressSanitizer' "$OUT/cases/log"; then
      bad=$((bad + 1)); cp "$c" "$OUT/cases/crash-$bad.c"; cp "$OUT/cases/log" "$OUT/cases/crash-$bad.log"
      echo "FINDING seed=$s iter=$i rc=$rc -> $OUT/cases/crash-$bad.c"
    elif [ $rc -ne 0 ]; then rejected=$((rejected + 1)); fi
  done
done
rm -f "$OUT/cases/case.c" "$OUT/cases/case.o" "$OUT/cases/log"
echo "FUZZ_HC cases=$n findings=$bad rejected=$rejected"
[ "$bad" -eq 0 ]
