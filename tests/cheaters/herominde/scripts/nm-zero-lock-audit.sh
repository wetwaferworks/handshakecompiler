#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Handshake Compiler (HC) Layer-1 ELF/Object Zero-Lock & Fair-Play Symbol Audit
# Usage:
#   nm-zero-lock-audit.sh [--profile=component|runtime] <obj_or_bin>...
set -euo pipefail

PROFILE="component"
FILES=()
for arg in "$@"; do
    case "$arg" in
        --profile=component) PROFILE="component" ;;
        --profile=runtime)   PROFILE="runtime" ;;
        -h|--help)
            echo "Usage: $0 [--profile=component|runtime] <elf-file>..."
            exit 0
            ;;
        *) FILES+=("$arg") ;;
    esac
done

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "ERROR: no ELF object or binary files provided" >&2
    exit 2
fi

# Always banned in BOTH component and runtime profiles (ZERO exceptions):
BANNED_ALWAYS='^(pthread_mutex|pthread_rwlock|pthread_cond|pthread_spin|pthread_barrier|pthread_once|sem_|mtx_|cnd_|call_once|tss_|flock|lockf|dlopen|dlsym|dlvsym|rand|srand|random|srandom|drand48|erand48|lrand48|mrand48|getrandom|arc4random|gettimeofday|__gettimeofday|time$|ftime|GOMP_|__kmpc_)'

# Additional symbols banned in component profile (allowed ONLY in audited runtime profile):
BANNED_COMPONENT='^(pthread_create|pthread_join|pthread_detach|thrd_create|thrd_join|clone|fork|vfork|syscall|clock_gettime|timespec_get|clock$|sleep|usleep|nanosleep|clock_nanosleep|pause|select|pselect|poll|ppoll|epoll_wait|epoll_pwait|waitpid|wait4|sched_yield|sched_getcpu|gettid|pthread_self)'

FAILURES=0
for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "[FAIL] Missing file: $f" >&2
        FAILURES=$((FAILURES + 1))
        continue
    fi
    # Extract undefined/imported symbol names (strip GLIBC version suffixes like @GLIBC_2.34)
    SYMS=$(nm -u "$f" 2>/dev/null | awk '{print $NF}' | sed 's/@.*//' | sort -u || true)
    BAD_ALWAYS=$(printf '%s\n' "$SYMS" | grep -E "$BANNED_ALWAYS" || true)
    BAD_COMP=""
    if [ "$PROFILE" = "component" ]; then
        BAD_COMP=$(printf '%s\n' "$SYMS" | grep -E "$BANNED_COMPONENT" || true)
    fi

    if [ -n "$BAD_ALWAYS" ] || [ -n "$BAD_COMP" ]; then
        echo "[FAIL] $f (profile=$PROFILE) imports forbidden symbols:"
        [ -n "$BAD_ALWAYS" ] && printf '  [ALWAYS-BANNED] %s\n' $BAD_ALWAYS
        [ -n "$BAD_COMP" ]   && printf '  [COMPONENT-BANNED] %s\n' $BAD_COMP
        FAILURES=$((FAILURES + 1))
    else
        echo "[PASS] $f (profile=$PROFILE): zero forbidden lock/nondeterminism symbols"
    fi
done

if [ "$FAILURES" -gt 0 ]; then
    echo "AUDIT FAILED: $FAILURES file(s) violated zero-lock/fair-play symbol policy." >&2
    exit 1
fi
echo "AUDIT PASSED: ${#FILES[@]} file(s) clean (profile=$PROFILE)."
