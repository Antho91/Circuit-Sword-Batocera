#!/bin/sh
# Host test for batocera-build/scripts/env.sh's space-in-BATOCERA_SRC
# early-fail check. Sources env.sh in subshells (never in this shell
# directly -- it has `set -euo pipefail` and calls `exit` on failure,
# which would kill the test runner too) with different BATOCERA_SRC
# values and checks exit codes / stderr content.
set -e

ENV_SH="/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"

failures=0
check() {
    if [ "$1" = "$2" ]; then
        echo "  ok   $3"
    else
        echo "  FAIL $3 (expected [$2], got [$1])"
        failures=$((failures + 1))
    fi
}

# --- a space-containing BATOCERA_SRC must fail fast with a clear message ---
output=$(BATOCERA_SRC="/tmp/has a space/batocera.linux" sh -c ". '$ENV_SH'" 2>&1) && rc=0 || rc=$?
check "$rc" "1" "space-containing BATOCERA_SRC exits non-zero"
case "$output" in
    *"BATOCERA_SRC contains a space"*) check "matched" "matched" "error message names the problem" ;;
    *) check "not matched" "matched" "error message names the problem" ;;
esac

# --- a space-free BATOCERA_SRC must NOT trip this specific check ---
# (env.sh may still exit non-zero later for other reasons -- e.g.
# require_src's missing-checkout check, if called -- but merely sourcing
# env.sh itself, with no other function called, must succeed for a
# space-free path.)
output=$(BATOCERA_SRC="/tmp/no-space/batocera.linux" sh -c ". '$ENV_SH'" 2>&1) && rc=0 || rc=$?
check "$rc" "0" "space-free BATOCERA_SRC does not trip the space check"
case "$output" in
    *"BATOCERA_SRC contains a space"*) check "matched" "not matched" "space-free path does not trigger the space error" ;;
    *) check "not matched" "not matched" "space-free path does not trigger the space error" ;;
esac

echo ""
echo "$([ "$failures" -eq 0 ] && echo PASSED || echo FAILED) ($failures failures)"
[ "$failures" -eq 0 ]
