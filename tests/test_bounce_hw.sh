#!/usr/bin/env bash
#
# test_bounce_hw.sh — integration tests for the bounce (DeviceReset) command
#
# Tests are split into two groups:
#   CLI tests   — argument parsing, help output (no camera needed)
#   HW tests    — actual DeviceReset against a live camera
#
# Usage:
#   make test-hw                     # via Makefile
#   tests/test_bounce_hw.sh          # direct
#   tests/test_bounce_hw.sh -s SN    # target a specific camera
#   tests/test_bounce_hw.sh -a ADDR  # target by IP address
#
# Exit codes:
#   0 = all tests passed
#   1 = one or more tests failed
#   77 = skipped (no camera found)

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────

TOOL="${TOOL:-bin/ag-cam-tools}"
DEVICE_OPTS=()

# Parse optional device selection flags.
while [[ $# -gt 0 ]]; do
    case "$1" in
        -s|--serial)   DEVICE_OPTS+=("--serial" "$2"); shift 2 ;;
        -a|--address)  DEVICE_OPTS+=("--address" "$2"); shift 2 ;;
        -i|--interface) DEVICE_OPTS+=("--interface" "$2"); shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Colour helpers ────────────────────────────────────────────────────

if [[ -t 1 ]]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[0;33m'
    BOLD='\033[1m'
    RESET='\033[0m'
else
    GREEN='' RED='' YELLOW='' BOLD='' RESET=''
fi

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass () {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}PASS${RESET}  $1"
}

fail () {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "  ${RED}FAIL${RESET}  $1"
    if [[ -n "${2:-}" ]]; then
        echo "        $2"
    fi
}

skip () {
    echo -e "  ${YELLOW}SKIP${RESET}  $1"
}

# ── Pre-flight checks ────────────────────────────────────────────────

if [[ ! -x "$TOOL" ]]; then
    echo "error: $TOOL not found — run 'make' first"
    exit 1
fi

echo -e "${BOLD}=== Integration Tests: bounce ===${RESET}"
echo ""

# ── CLI Tests (no camera needed) ─────────────────────────────────────

echo -e "${BOLD}CLI argument tests${RESET}"

# Test 1: --help exits 0 and shows usage
RC=0
OUT=$("$TOOL" bounce --help 2>&1) || RC=$?

if [[ $RC -eq 0 ]] && echo "$OUT" | grep -q "\-\-no-wait"; then
    pass "--help exits 0 and shows options"
else
    fail "--help" "exit=$RC output: $OUT"
fi

# Test 2: --serial and --address together are rejected
RC=0
OUT=$("$TOOL" bounce --serial X --address Y 2>&1) || RC=$?

if [[ $RC -ne 0 ]] && echo "$OUT" | grep -qi "mutually exclusive"; then
    pass "--serial + --address rejected as mutually exclusive"
else
    fail "--serial + --address mutual exclusivity" "exit=$RC output: $OUT"
fi

echo ""

# ── Hardware Tests ────────────────────────────────────────────────────

# Check if a camera is reachable.
if ! "$TOOL" list >/dev/null 2>&1; then
    echo -e "${YELLOW}SKIP: no cameras found — skipping hardware tests${RESET}"
    echo ""
    echo -e "${BOLD}Results: $TESTS_PASSED/$TESTS_RUN passed${RESET}"
    if [[ $TESTS_FAILED -gt 0 ]]; then
        exit 1
    fi
    exit 0
fi

echo -e "${BOLD}Hardware tests (camera required)${RESET}"

# Helper: run bounce with device opts.
bounce () {
    "$TOOL" bounce "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"}
}

# Test 3: bounce --no-wait sends reset and exits immediately
echo -e "${BOLD}Test 3: bounce --no-wait${RESET}"
RC=0
OUT=$(bounce --no-wait 2>&1) || RC=$?

if [[ $RC -eq 0 ]] && echo "$OUT" | grep -q "Reset issued"; then
    pass "bounce --no-wait issues reset and exits"
else
    fail "bounce --no-wait" "exit=$RC output: $OUT"
fi

# After --no-wait the camera is rebooting.  Wait for it to come back
# before running the next test.
echo "  (waiting for camera to finish rebooting...)"
sleep 20

echo ""

# Test 4: bounce (with wait) resets and waits for camera to return
echo -e "${BOLD}Test 4: bounce with wait${RESET}"
RC=0
OUT=$(bounce --timeout 60 2>&1) || RC=$?

if [[ $RC -eq 0 ]] && echo "$OUT" | grep -q "Camera back online"; then
    pass "bounce waits for camera to come back online"
else
    fail "bounce with wait" "exit=$RC output: $OUT"
fi

echo ""

# Test 5: after bounce the camera is usable (connect succeeds)
echo -e "${BOLD}Test 5: camera usable after bounce${RESET}"
RC=0
OUT=$("$TOOL" connect ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"} 2>&1) || RC=$?

if [[ $RC -eq 0 ]] && echo "$OUT" | grep -q "Connected"; then
    pass "camera responds to connect after bounce"
else
    fail "connect after bounce" "exit=$RC output: $OUT"
fi

echo ""

# ── Summary ──────────────────────────────────────────────────────────

echo -e "${BOLD}Results: $TESTS_PASSED/$TESTS_RUN passed, $TESTS_FAILED failed${RESET}"

if [[ $TESTS_FAILED -gt 0 ]]; then
    exit 1
fi

exit 0
