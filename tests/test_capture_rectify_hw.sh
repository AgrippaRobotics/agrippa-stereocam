#!/usr/bin/env bash
#
# test_capture_rectify_hw.sh — hardware integration tests for capture
#                               with --calibration-local / --calibration-slot
#
# Requires a Lucid camera on the network and the sample calibration
# at calibration/sample_calibration/.
#
# Tests:
#   1. Capture with --calibration-local produces rectified output
#   2. Capture with --calibration-slot (upload → capture → purge)
#   3. Mutual exclusivity of --calibration-local and --calibration-slot
#   4. Invalid slot number rejected
#
# Usage:
#   make test-hw                     # via Makefile
#   tests/test_capture_rectify_hw.sh # direct
#   tests/test_capture_rectify_hw.sh -s SERIAL
#   tests/test_capture_rectify_hw.sh -a ADDR
#
# Exit codes:
#   0 = all tests passed
#   1 = one or more tests failed
#   77 = skipped (no camera found)

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────

TOOL="${TOOL:-bin/ag-cam-tools}"
SAMPLE_SESSION="calibration/sample_calibration"
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

if [[ ! -d "$SAMPLE_SESSION/calib_result" ]]; then
    echo "error: $SAMPLE_SESSION not found"
    exit 1
fi

echo -e "${BOLD}=== Hardware Integration Tests: capture rectification ===${RESET}"
echo ""

if ! "$TOOL" list >/dev/null 2>&1; then
    echo -e "${YELLOW}SKIP: no cameras found on the network${RESET}"
    exit 77
fi

echo "Camera found. Running capture rectification tests..."
echo ""

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ── Helpers ────────────────────────────────────────────────────────────

capture () {
    "$TOOL" capture "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"} -A
}

stash () {
    "$TOOL" calibration-stash "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"}
}

# ── Test 1: Capture with --calibration-local ──────────────────────────

echo -e "${BOLD}Test 1: capture with --calibration-local${RESET}"
DIR1="$TMPDIR/local"
mkdir -p "$DIR1"
OUT=$(capture --calibration-local "$SAMPLE_SESSION" -e png -o "$DIR1" 2>&1) || true

if echo "$OUT" | grep -qi "rectif"; then
    pass "output mentions rectification"
else
    fail "rectification message" "output: $OUT"
fi

LEFT=$(find "$DIR1" -name '*_left.png' | head -1)
RIGHT=$(find "$DIR1" -name '*_right.png' | head -1)

if [[ -n "$LEFT" && -f "$LEFT" ]]; then
    pass "left PNG created"
    SIZE=$(wc -c < "$LEFT" | tr -d ' ')
    if [[ "$SIZE" -gt 0 ]]; then
        pass "left PNG is non-empty (${SIZE} bytes)"
    else
        fail "left PNG is empty"
    fi
else
    fail "left PNG not created" "output: $OUT"
fi

if [[ -n "$RIGHT" && -f "$RIGHT" ]]; then
    pass "right PNG created"
else
    fail "right PNG not created"
fi

echo ""

# ── Test 2: Capture with --calibration-slot ───────────────────────────

echo -e "${BOLD}Test 2: capture with --calibration-slot${RESET}"

# Setup: purge any existing data, upload sample to slot 0.
stash purge >/dev/null 2>&1 || true
UPLOAD_OUT=$(stash upload --slot 0 "$SAMPLE_SESSION" 2>&1) || true

if echo "$UPLOAD_OUT" | grep -q "Done"; then
    pass "setup: uploaded sample calibration to slot 0"
else
    fail "setup: upload to slot 0" "output: $UPLOAD_OUT"
fi

DIR2="$TMPDIR/slot"
mkdir -p "$DIR2"
OUT=$(capture --calibration-slot 0 -e png -o "$DIR2" 2>&1) || true

if echo "$OUT" | grep -qi "rectif"; then
    pass "output mentions rectification"
else
    fail "rectification message" "output: $OUT"
fi

LEFT=$(find "$DIR2" -name '*_left.png' | head -1)
RIGHT=$(find "$DIR2" -name '*_right.png' | head -1)

if [[ -n "$LEFT" && -f "$LEFT" ]]; then
    pass "left PNG created"
    SIZE=$(wc -c < "$LEFT" | tr -d ' ')
    if [[ "$SIZE" -gt 0 ]]; then
        pass "left PNG is non-empty (${SIZE} bytes)"
    else
        fail "left PNG is empty"
    fi
else
    fail "left PNG not created" "output: $OUT"
fi

if [[ -n "$RIGHT" && -f "$RIGHT" ]]; then
    pass "right PNG created"
else
    fail "right PNG not created"
fi

# Cleanup: purge.
stash purge >/dev/null 2>&1 || true

echo ""

# ── Test 3: Mutual exclusivity rejected ───────────────────────────────

echo -e "${BOLD}Test 3: mutual exclusivity${RESET}"
DIR3="$TMPDIR/mutex"
mkdir -p "$DIR3"
OUT=$(capture --calibration-local "$SAMPLE_SESSION" --calibration-slot 0 -e png -o "$DIR3" 2>&1) || true
RC=$?

# The command should fail (nonzero exit or error message).
if [[ $RC -ne 0 ]] || echo "$OUT" | grep -qi "mutually exclusive\|cannot.*both\|error"; then
    pass "both args together is rejected"
else
    fail "mutual exclusivity not enforced" "exit=$RC output: $OUT"
fi

echo ""

# ── Test 4: Invalid slot number rejected ──────────────────────────────

echo -e "${BOLD}Test 4: invalid slot rejected${RESET}"
DIR4="$TMPDIR/badslot"
mkdir -p "$DIR4"
OUT=$(capture --calibration-slot 5 -e png -o "$DIR4" 2>&1) || true
RC=$?

if [[ $RC -ne 0 ]] || echo "$OUT" | grep -qi "slot.*must be\|invalid slot\|error"; then
    pass "slot 5 is rejected"
else
    fail "invalid slot not rejected" "exit=$RC output: $OUT"
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
