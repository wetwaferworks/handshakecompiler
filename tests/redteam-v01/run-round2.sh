#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Herominde Layer-2 Round-2 Red-Team Harness for HC v0.1.1+
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HC="${1:-$REPO_ROOT/build/current/hc}"
HC_ASAN="${2:-$REPO_ROOT/build/asan/hc}"
ROOT="$SCRIPT_DIR/round2"
INC="$REPO_ROOT/include"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== HC Layer-2 Round-2 Adversarial Suite ==="
echo "Binary : $HC ($("$HC" --version 2>&1 | head -n1))"
echo "ASan   : $HC_ASAN"

ESC=0; CAU=0; VAL_OK=0; VAL_FP=0

run_case() {
  local name="$1" kind="$2" mode="$3"
  local src="$ROOT/$name"
  local obj="$TMP/${name}.${mode}.o"
  rm -f "$obj"
  local EXTRA=()
  case "$mode" in
    default)        EXTRA=() ;;
    arm64)          EXTRA=(--target=aarch64-linux-gnu) ;;
    xpreproc_undef) EXTRA=(-Xpreprocessor -U__HC_COMPILER__) ;;
    xpreproc_inc)   EXTRA=(-Xpreprocessor -include -Xpreprocessor "$SCRIPT_DIR/r25_injected_spoof.h") ;;
    fopenmp)        EXTRA=(-fopenmp) ;;
    noaudit_omp)    EXTRA=(--no-audit -fopenmp) ;;
  esac
  set +e
  OUT=$("$HC" "${EXTRA[@]}" -I"$INC" -c "$src" -o "$obj" 2>&1)
  RC=$?
  set -e
  FL=$(printf '%s\n' "$OUT" | head -n1)
  if [ "$kind" = "ESCAPE" ]; then
    if [ "$RC" -eq 0 ] && [ -f "$obj" ]; then
      printf '[R2-ESCAPED!] %-44s (%s)\n' "$name" "$mode"
      ESC=$((ESC+1))
    else
      printf '[R2-CAUGHT  ] %-44s (%s) | %s\n' "$name" "$mode" "$FL"
      CAU=$((CAU+1))
    fi
  elif [ "$kind" = "SYNTAXERR" ]; then
    if [ "$RC" -eq 0 ]; then
      printf '[R2-BAD-OK! ] %-44s (%s) -> rc=0 on malformed syntax!\n' "$name" "$mode"
      ESC=$((ESC+1))
    else
      printf '[R2-CAUGHT  ] %-44s (%s) | %s\n' "$name" "$mode" "$FL"
      CAU=$((CAU+1))
    fi
  else
    if [ "$RC" -eq 0 ] && [ -f "$obj" ]; then
      printf '[R2-VALID-OK] %-44s (%s)\n' "$name" "$mode"
      VAL_OK=$((VAL_OK+1))
    else
      printf '[R2-FALSEPOS] %-44s (%s) | %s\n' "$name" "$mode" "$FL"
      VAL_FP=$((VAL_FP+1))
    fi
  fi
}

# Escapes / malformed checks
run_case w01_const_ptr_to_mutable_global.c        ESCAPE    default
run_case w02_array_decay_global_write.c           ESCAPE    default
run_case w03_toplevel_asm_init_array_spinlock.c   ESCAPE    default
run_case w04_cli_xpreprocessor_undef.c            ESCAPE    xpreproc_undef
run_case w05_cli_xpreprocessor_include.c          ESCAPE    xpreproc_inc
run_case w06_const_cast_paren_expr.c              ESCAPE    default
run_case w07_cross_shard_ptr_arith_and_const0.c   ESCAPE    default
run_case w08_cross_shard_const_index0.c           ESCAPE    default
run_case w09_for_step_zero_infinite.c             ESCAPE    default
run_case w10_fopenmp_critical_lock.c              ESCAPE    fopenmp
run_case w10_fopenmp_critical_lock.c              ESCAPE    noaudit_omp
run_case w11_unannotated_pull_caller_mutates_global.c ESCAPE default
run_case w15_file_scope_ptr_copy_write.c          ESCAPE    default
run_case w16_for_wrong_var_step.c                 ESCAPE    default
run_case w17_const_cast_addrof_subscript.c        ESCAPE    default
run_case w19_unclosed_attr_asan.c                 SYNTAXERR default
run_case w20_arm64_toplevel_asm.c                 ESCAPE    arm64
run_case w21_toplevel_asm_raw_futex_syscall.c     ESCAPE    default

# Valid edge controls
run_case w13_valid_hc_block_comment_edge.hc       VALID     default
run_case w14_valid_pull_reads_global_const_array.c VALID    default

# ASan stability check
ASAN_CRASH=0
if [ -x "$HC_ASAN" ]; then
  for f in w18_malformed_hc_no_brace.hc w19_unclosed_attr_asan.c w03_toplevel_asm_init_array_spinlock.c; do
    set +e
    OUT=$("$HC_ASAN" -I"$INC" -c "$ROOT/$f" -o "$TMP/asan_$f.o" 2>&1)
    set -e
    if printf '%s' "$OUT" | grep -qE "AddressSanitizer|UndefinedBehaviorSanitizer"; then
      echo "[R2-ASAN-CRASH!] $f"
      ASAN_CRASH=$((ASAN_CRASH+1))
    fi
  done
fi

echo "----------------------------------------------------------------"
echo "Round-2 Summary: CAUGHT=$CAU / $((CAU+ESC)) | ESCAPED=$ESC | VALID_OK=$VAL_OK / $((VAL_OK+VAL_FP)) (FP=$VAL_FP) | ASAN_CRASH=$ASAN_CRASH"
if [ "$ESC" -eq 0 ] && [ "$VAL_FP" -eq 0 ] && [ "$ASAN_CRASH" -eq 0 ]; then
  exit 0
else
  exit 1
fi
