#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Herominde Layer-2 Red-Team Runner against Master Chief's HC v0.1
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$ROOT/../.." && pwd)"
HC="${1:-$REPO_ROOT/build/current/hc}"
HC_ASAN="${2:-$REPO_ROOT/build/asan/hc}"
INC="$REPO_ROOT/include"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

ESCAPED=0
CAUGHT=0
FP=0
ACCEPTED=0

echo "=== Herominde Layer-2 Red-Team Run against $HC ==="
"$HC" --version
echo ""

while IFS=$'\t' read -r rel sev code mode desc; do
    [ -z "$rel" ] && continue
    [ "$rel" = "path" ] && continue
    src="$ROOT/$rel"
    obj="$TMPDIR/$(basename "$rel").o"
    rm -f "$obj"

    EXTRA=()
    case "$mode" in
        c_default|hc_default) EXTRA=() ;;
        c_runtime)            EXTRA=(--profile=runtime) ;;
        cli_wp_include)       EXTRA=("-Wp,-include,$ROOT/r25_injected_spoof.h") ;;
        cli_no_audit)         EXTRA=(--no-audit) ;;
        cli_undef_hc)         EXTRA=(-U__HC_COMPILER__) ;;
    esac

    set +e
    OUT=$("$HC" "${EXTRA[@]}" -I"$INC" -c "$src" -o "$obj" 2>&1)
    RC=$?
    set -e

    FIRST_LINE=$(printf '%s\n' "$OUT" | head -n 1)

    if [[ "$rel" == escapes/* ]]; then
        if [ "$RC" -eq 0 ] && [ -f "$obj" ]; then
            echo "[ESCAPED!][$sev] $rel  (wanted $code) -> HC ACCEPTED & EMITTED .o!"
            ESCAPED=$((ESCAPED + 1))
        else
            echo "[CAUGHT  ][$sev] $rel  (wanted $code) -> rc=$RC | $FIRST_LINE"
            CAUGHT=$((CAUGHT + 1))
        fi
    else
        if [ "$RC" -eq 0 ] && [ -f "$obj" ]; then
            echo "[ACCEPTED]      $rel"
            ACCEPTED=$((ACCEPTED + 1))
        else
            echo "[FALSE-POS!]    $rel -> rc=$RC | $FIRST_LINE"
            FP=$((FP + 1))
        fi
    fi
done < "$ROOT/manifest.tsv"

echo "----------------------------------------------------------------"
echo "Adversarial probes : $((ESCAPED + CAUGHT)) total | ESCAPED: $ESCAPED | CAUGHT: $CAUGHT"
echo "Valid edge controls: $((ACCEPTED + FP)) total | ACCEPTED: $ACCEPTED | FALSE-POSITIVES: $FP"

if [ -x "$HC_ASAN" ]; then
    echo ""
    echo "=== ASan Compiler Memory-Safety Probes ($HC_ASAN) ==="
    for cfile in "$ROOT/escapes/crash01_deep_callchain_265.c" "$ROOT/escapes/crash02_unclosed_brace.hc"; do
        set +e
        AOUT=$("$HC_ASAN" --check-only -I"$INC" "$cfile" 2>&1)
        ARC=$?
        set -e
        SUMMARY=$(printf '%s\n' "$AOUT" | grep -m1 'SUMMARY: AddressSanitizer:' || printf 'rc=%d (no ASan crash)' "$ARC")
        echo "[ASAN] $(basename "$cfile") -> rc=$ARC | $SUMMARY"
    done
fi
