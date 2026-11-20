#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Handshake Compiler (HC) Adversarial Corpus & Valid Control Harness (C11 HS_* + .hc)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/manifest.tsv"
AUDIT="$ROOT/scripts/nm-zero-lock-audit.sh"
MODE="syntax"
HC_BIN=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --syntax-only) MODE="syntax"; shift ;;
        --hc)          MODE="hc"; HC_BIN="${2:?missing hc path}"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--syntax-only | --hc /path/to/hc]"
            exit 0
            ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0
TOTAL=0

while IFS=$'\t' read -r relpath expected code category desc; do
    [ -z "$relpath" ] && continue
    [[ "$relpath" == \#* ]] && continue
    [[ "$relpath" == "path" ]] && continue
    TOTAL=$((TOTAL + 1))
    src="$ROOT/$relpath"
    if [ ! -f "$src" ]; then
        echo "[FAIL] Missing corpus file: $relpath"
        FAIL=$((FAIL + 1))
        continue
    fi

    if [ "$MODE" = "syntax" ]; then
        if [[ "$relpath" == *.hc ]]; then
            # .hc syntax is parsed by HC's dedicated frontend; validate structural hygiene here
            if grep -qE '\bphase[[:space:]]+(publish|pull|barrier|commit)\b' "$src"; then
                echo "[PASS] HC-DSL-OK     $relpath  (expects $expected $code)"
                PASS=$((PASS + 1))
            else
                echo "[FAIL] HC-DSL-BAD    $relpath"
                FAIL=$((FAIL + 1))
            fi
        elif [ "$expected" = "ACCEPT" ]; then
            obj="$TMPDIR/$(basename "$relpath" .c).o"
            if gcc -std=c11 -Wall -Wextra -Werror -I"$ROOT/include" -c "$src" -o "$obj" 2>"$TMPDIR/err.log"; then
                if "$AUDIT" --profile=component "$obj" >/dev/null 2>&1; then
                    echo "[PASS] ACCEPT+AUDIT  $relpath"
                    PASS=$((PASS + 1))
                else
                    echo "[FAIL] AUDIT-FAIL    $relpath"
                    "$AUDIT" --profile=component "$obj" || true
                    FAIL=$((FAIL + 1))
                fi
            else
                echo "[FAIL] COMPILE-FAIL  $relpath"
                cat "$TMPDIR/err.log"
                FAIL=$((FAIL + 1))
            fi
        else
            if gcc -std=gnu11 -fsyntax-only -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -I"$ROOT/include" "$src" 2>"$TMPDIR/err.log"; then
                echo "[PASS] SYNTAX-OK     $relpath  (expects $code)"
                PASS=$((PASS + 1))
            else
                echo "[FAIL] SYNTAX-BAD    $relpath"
                cat "$TMPDIR/err.log"
                FAIL=$((FAIL + 1))
            fi
        fi
    else
        obj="$TMPDIR/$(basename "$relpath").o"
        set +e
        OUT=$("$HC_BIN" -I"$ROOT/include" -c "$src" -o "$obj" 2>&1)
        RC=$?
        set -e
        if [ "$expected" = "ACCEPT" ]; then
            if [ "$RC" -eq 0 ] && [ -f "$obj" ] && "$AUDIT" --profile=component "$obj" >/dev/null 2>&1; then
                echo "[PASS] HC-ACCEPT     $relpath"
                PASS=$((PASS + 1))
            else
                echo "[FAIL] HC-FALSE-REJ  $relpath (rc=$RC)"
                echo "$OUT" | head -n 10
                FAIL=$((FAIL + 1))
            fi
        else
            if [ "$RC" -ne 0 ] && printf '%s\n' "$OUT" | grep -q "$code"; then
                echo "[PASS] HC-REJECT     $relpath  ($code)"
                PASS=$((PASS + 1))
            elif [ "$RC" -ne 0 ]; then
                echo "[FAIL] HC-WRONG-CODE $relpath  (expected $code, got rc=$RC)"
                echo "$OUT" | head -n 5
                FAIL=$((FAIL + 1))
            else
                echo "[FAIL] HC-ESCAPED!   $relpath  (expected REJECT $code, but HC ACCEPTED!)"
                FAIL=$((FAIL + 1))
            fi
        fi
    fi
done < "$MANIFEST"

echo "------------------------------------------------------------"
echo "Mode: $MODE | Total: $TOTAL | Passed: $PASS | Failed: $FAIL"
[ "$FAIL" -eq 0 ]
