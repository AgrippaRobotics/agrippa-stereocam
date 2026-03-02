#!/usr/bin/env bash
#
# test_burst_hw.sh — hardware integration tests for burst capture
#
# Requires a Lucid camera on the network.
#
# Tests:
#   1. --burst 3 produces a burst subdirectory with 3 stereo pairs
#   2. --burst 2 with --auto-expose succeeds
#   3. --burst 1 rejected (below minimum)
#   4. --burst 101 rejected (above maximum)
#
# Usage:
#   make test-hw                     # via Makefile
#   tests/test_burst_hw.sh           # direct
#   tests/test_burst_hw.sh -a ADDR
#   tests/test_burst_hw.sh -s SERIAL
#
# Exit codes:
#   0 = all tests passed
#   1 = one or more tests failed
#   77 = skipped (no camera found)

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────

TOOL="${TOOL:-bin/ag-cam-tools}"
DEVICE_OPTS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -s|--serial)    DEVICE_OPTS+=("--serial"    "$2"); shift 2 ;;
        -a|--address)   DEVICE_OPTS+=("--address"   "$2"); shift 2 ;;
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

echo -e "${BOLD}=== Hardware Integration Tests: burst capture ===${RESET}"
echo ""

if ! "$TOOL" list >/dev/null 2>&1; then
    echo -e "${YELLOW}SKIP: no cameras found on the network${RESET}"
    exit 77
fi

echo "Camera found. Running burst capture tests..."
echo ""

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ── Helpers ────────────────────────────────────────────────────────────

capture () {
    "$TOOL" capture "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"}
}

# ── Test 1: --burst 3 produces 3 stereo pairs ────────────────────────

echo -e "${BOLD}Test 1: burst 3 produces 3 stereo pairs${RESET}"
DIR1="$TMPDIR/burst3"
mkdir -p "$DIR1"
OUT=$(capture --burst 3 -e png -x 50000 -o "$DIR1" 2>&1) || true

BURST_DIR=$(find "$DIR1" -maxdepth 1 -type d -name 'burst_*' | head -1)
if [[ -n "$BURST_DIR" && -d "$BURST_DIR" ]]; then
    pass "burst subdirectory created"
    LEFT_COUNT=$(find "$BURST_DIR" -name '*_left.png' | wc -l | tr -d ' ')
    RIGHT_COUNT=$(find "$BURST_DIR" -name '*_right.png' | wc -l | tr -d ' ')
    if [[ "$LEFT_COUNT" -eq 3 ]]; then
        pass "3 left images"
    else
        fail "expected 3 left images, got $LEFT_COUNT"
    fi
    if [[ "$RIGHT_COUNT" -eq 3 ]]; then
        pass "3 right images"
    else
        fail "expected 3 right images, got $RIGHT_COUNT"
    fi
else
    fail "burst subdirectory not created" "output: $OUT"
fi

echo ""

# ── Test 2: --burst 2 with --auto-expose ─────────────────────────────

echo -e "${BOLD}Test 2: burst 2 with auto-expose${RESET}"
DIR2="$TMPDIR/burst_ae"
mkdir -p "$DIR2"
OUT=$(capture --burst 2 -A -e png -o "$DIR2" 2>&1) || true

BURST_DIR2=$(find "$DIR2" -maxdepth 1 -type d -name 'burst_*' | head -1)
if [[ -n "$BURST_DIR2" && -d "$BURST_DIR2" ]]; then
    LEFT_COUNT=$(find "$BURST_DIR2" -name '*_left.png' | wc -l | tr -d ' ')
    if [[ "$LEFT_COUNT" -eq 2 ]]; then
        pass "auto-expose + burst produced 2 pairs"
    else
        fail "expected 2 pairs, got $LEFT_COUNT left images"
    fi
else
    fail "burst with auto-expose failed" "output: $OUT"
fi

echo ""

# ── Test 3: --burst 1 rejected ───────────────────────────────────────

echo -e "${BOLD}Test 3: burst count below minimum rejected${RESET}"
OUT=$(capture --burst 1 -e png -o "$TMPDIR/bad1" 2>&1) || true

if echo "$OUT" | grep -qi "error\|must be"; then
    pass "--burst 1 rejected"
else
    fail "--burst 1 not rejected" "output: $OUT"
fi

echo ""

# ── Test 4: --burst 101 rejected ─────────────────────────────────────

echo -e "${BOLD}Test 4: burst count above maximum rejected${RESET}"
OUT=$(capture --burst 101 -e png -o "$TMPDIR/bad2" 2>&1) || true

if echo "$OUT" | grep -qi "error\|must be"; then
    pass "--burst 101 rejected"
else
    fail "--burst 101 not rejected" "output: $OUT"
fi

echo ""

# ── Summary ───────────────────────────────────────────────────────────

echo -e "${BOLD}────────────────────────────────────────${RESET}"
if [[ $TESTS_FAILED -eq 0 ]]; then
    echo -e "${GREEN}${BOLD}All $TESTS_PASSED tests passed.${RESET}"
    exit 0
else
    echo -e "${RED}${BOLD}$TESTS_FAILED of $TESTS_RUN tests failed.${RESET}"
    exit 1
fi
