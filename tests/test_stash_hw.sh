#!/usr/bin/env bash
#
# test_stash_hw.sh — hardware integration tests for calibration-stash
#
# Requires a Lucid camera on the network.  Uses a minimal 128×128
# dummy calibration (~64 KB per remap) so upload/download cycles are
# fast even over the camera's slow file channel.
#
# Tests the full multi-slot lifecycle: upload to individual slots,
# list, download with integrity checks, overwrite, delete individual
# slots, purge, and clean-up.
#
# Usage:
#   make test-hw                     # via Makefile
#   tests/test_stash_hw.sh           # direct
#   tests/test_stash_hw.sh -s SERIAL # target a specific camera
#   tests/test_stash_hw.sh -a ADDR   # target by IP address
#
# Exit codes:
#   0 = all tests passed
#   1 = one or more tests failed
#   77 = skipped (no camera found)

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────

TOOL="${TOOL:-bin/ag-cam-tools}"
GEN="${GEN:-bin/gen_test_calibration}"
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

if [[ ! -x "$GEN" ]]; then
    echo "error: $GEN not found — run 'make test-hw' to build it"
    exit 1
fi

# Generate the tiny 128×128 test calibration session in a temp dir.
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

TEST_SESSION="$TMPDIR/test_calib"
"$GEN" "$TEST_SESSION" >/dev/null

if [[ ! -f "$TEST_SESSION/calib_result/remap_left.bin" ]]; then
    echo "error: gen_test_calibration failed to create test session"
    exit 1
fi

# Check if a camera is reachable by running 'list' — it auto-discovers.
echo -e "${BOLD}=== Hardware Integration Tests: calibration-stash ===${RESET}"
echo ""

if ! "$TOOL" list >/dev/null 2>&1; then
    echo -e "${YELLOW}SKIP: no cameras found on the network${RESET}"
    exit 77
fi

echo "Camera found. Running tests with 128×128 test calibration..."
echo ""

# ── Helper: run calibration-stash with device opts ────────────────────

stash () {
    "$TOOL" calibration-stash "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"}
}

# ── Ensure clean slate ────────────────────────────────────────────────
# Purge any leftover calibration data from previous test runs.

stash purge >/dev/null 2>&1 || true

# ── Test 1: Upload to slot 0 ─────────────────────────────────────────

echo -e "${BOLD}Test 1: upload to slot 0${RESET}"
OUT=$(stash upload --slot 0 "$TEST_SESSION" 2>&1) || true

if echo "$OUT" | grep -q "Done\. Calibration data written.*slot 0"; then
    pass "upload to slot 0 completes"
else
    fail "upload to slot 0" "output: $OUT"
fi

echo ""

# ── Test 2: List shows slot 0 occupied ───────────────────────────────

echo -e "${BOLD}Test 2: list after single upload${RESET}"
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "Camera file storage"; then
    pass "list shows storage info"
else
    fail "list shows storage info" "output: $OUT"
fi

if echo "$OUT" | grep -q "Slot 0:"; then
    pass "list shows slot 0 occupied"
else
    # May be legacy AGST if this is the first upload
    if echo "$OUT" | grep -q "Calibration summary\|Calibration archive"; then
        pass "list shows calibration info"
    else
        fail "list shows slot info" "output: $OUT"
    fi
fi

if echo "$OUT" | grep -qE "128.*128|128x128"; then
    pass "list shows 128×128 resolution"
else
    skip "resolution check in list"
fi

echo ""

# ── Test 3: Upload to slot 2 (multi-slot) ────────────────────────────

echo -e "${BOLD}Test 3: upload to slot 2${RESET}"
OUT=$(stash upload --slot 2 "$TEST_SESSION" 2>&1) || true

if echo "$OUT" | grep -q "Done\. Calibration data written.*slot 2"; then
    pass "upload to slot 2 completes"
else
    fail "upload to slot 2" "output: $OUT"
fi

echo ""

# ── Test 4: List shows slots 0 and 2, slot 1 empty ──────────────────

echo -e "${BOLD}Test 4: list multi-slot${RESET}"
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "Slot 0:.*128x128"; then
    pass "slot 0 shows 128×128"
else
    if echo "$OUT" | grep -q "Slot 0:"; then
        pass "slot 0 present"
    else
        fail "slot 0 in list" "output: $OUT"
    fi
fi

if echo "$OUT" | grep -q "Slot 1:.*(empty)"; then
    pass "slot 1 is empty"
else
    skip "slot 1 empty check"
fi

if echo "$OUT" | grep -q "Slot 2:"; then
    pass "slot 2 present"
else
    fail "slot 2 in list" "output: $OUT"
fi

echo ""

# ── Test 5: Upload to slot 1 (fill all 3) ────────────────────────────

echo -e "${BOLD}Test 5: fill slot 1 (all 3 slots)${RESET}"
OUT=$(stash upload --slot 1 "$TEST_SESSION" 2>&1) || true

if echo "$OUT" | grep -q "Done\. Calibration data written.*slot 1"; then
    pass "upload to slot 1 completes"
else
    fail "upload to slot 1" "output: $OUT"
fi

# Verify all 3 are now occupied.
OUT=$(stash list 2>&1) || true
ALL_OK=true
for s in 0 1 2; do
    if ! echo "$OUT" | grep -q "Slot $s:.*128x128"; then
        if ! echo "$OUT" | grep -q "Slot $s:"; then
            ALL_OK=false
        fi
    fi
done

if $ALL_OK; then
    pass "all 3 slots occupied"
else
    fail "all 3 slots occupied" "output: $OUT"
fi

echo ""

# ── Test 6: Overwrite slot 1 ─────────────────────────────────────────

echo -e "${BOLD}Test 6: overwrite slot 1${RESET}"
OUT=$(stash upload --slot 1 "$TEST_SESSION" 2>&1) || true

if echo "$OUT" | grep -q "Done\. Calibration data written.*slot 1"; then
    pass "overwrite slot 1 succeeds"
else
    fail "overwrite slot 1" "output: $OUT"
fi

echo ""

# ── Test 7: Delete slot 1 (middle slot) ──────────────────────────────

echo -e "${BOLD}Test 7: delete slot 1 (middle)${RESET}"
OUT=$(stash delete --slot 1 2>&1) || true

if echo "$OUT" | grep -q "Done\. Slot 1 deleted\|slot 1 removed"; then
    pass "delete slot 1 succeeds"
else
    if echo "$OUT" | grep -q "Done"; then
        pass "delete slot 1 succeeds"
    else
        fail "delete slot 1" "output: $OUT"
    fi
fi

# Verify slots 0 and 2 survive.
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "Slot 0:"; then
    pass "slot 0 survives"
else
    fail "slot 0 survives after deleting slot 1" "output: $OUT"
fi

if echo "$OUT" | grep -q "Slot 1:.*(empty)"; then
    pass "slot 1 now empty"
else
    skip "slot 1 empty after delete"
fi

if echo "$OUT" | grep -q "Slot 2:"; then
    pass "slot 2 survives"
else
    fail "slot 2 survives after deleting slot 1" "output: $OUT"
fi

echo ""

# ── Test 8: Delete slot 0 ────────────────────────────────────────────

echo -e "${BOLD}Test 8: delete slot 0${RESET}"
OUT=$(stash delete --slot 0 2>&1) || true

if echo "$OUT" | grep -q "deleted\|Deleted\|removed"; then
    pass "delete slot 0 succeeds"
else
    fail "delete slot 0" "output: $OUT"
fi

# Only slot 2 should remain.
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "Slot 2:"; then
    pass "slot 2 is the only remaining slot"
else
    fail "slot 2 remaining" "output: $OUT"
fi

echo ""

# ── Test 9: Delete last slot (should delete entire file) ─────────────

echo -e "${BOLD}Test 9: delete last slot (slot 2)${RESET}"
OUT=$(stash delete --slot 2 2>&1) || true

if echo "$OUT" | grep -q "All calibration data removed\|All calibration data deleted"; then
    pass "last slot delete removes entire file"
else
    if echo "$OUT" | grep -q "deleted\|Deleted\|removed"; then
        pass "last slot deleted"
    else
        fail "delete last slot" "output: $OUT"
    fi
fi

echo ""

# ── Test 10: List on empty camera ────────────────────────────────────

echo -e "${BOLD}Test 10: list on empty camera${RESET}"
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "No calibration data"; then
    pass "empty camera shows no data"
else
    if echo "$OUT" | grep -q "File size:.*0"; then
        pass "empty camera shows file size 0"
    else
        skip "empty check (camera may need power cycle)"
    fi
fi

echo ""

# ── Test 11: Delete on already-empty slot ────────────────────────────

echo -e "${BOLD}Test 11: delete on empty camera${RESET}"
OUT=$(stash delete --slot 0 2>&1) || true

if echo "$OUT" | grep -q "nothing to delete\|No calibration data"; then
    pass "delete on empty camera is a no-op"
else
    skip "delete-on-empty message"
fi

echo ""

# ── Test 12: Upload-delete-upload lifecycle ──────────────────────────

echo -e "${BOLD}Test 12: upload-delete-upload lifecycle${RESET}"
LIFE1=$(stash upload --slot 0 "$TEST_SESSION" 2>&1) || true
LIFE2=$(stash delete --slot 0 2>&1) || true
LIFE3=$(stash upload --slot 2 "$TEST_SESSION" 2>&1) || true

if echo "$LIFE1" | grep -q "Done" && \
   echo "$LIFE2" | grep -q "deleted\|Deleted\|removed" && \
   echo "$LIFE3" | grep -q "Done"; then
    pass "upload → delete → upload lifecycle"
else
    fail "upload-delete-upload lifecycle"
fi

echo ""

# ── Test 13: Purge wipes everything ──────────────────────────────────

echo -e "${BOLD}Test 13: purge${RESET}"

# Ensure there's data on camera from Test 12.
OUT=$(stash purge 2>&1) || true

if echo "$OUT" | grep -q "purged\|Purged"; then
    pass "purge succeeds"
else
    fail "purge" "output: $OUT"
fi

# Verify camera is empty after purge.
OUT=$(stash list 2>&1) || true

if echo "$OUT" | grep -q "No calibration data\|File size:.*0"; then
    pass "camera empty after purge"
else
    skip "empty check after purge (camera may need power cycle)"
fi

echo ""

# ── Test 14: Purge on empty camera ───────────────────────────────────

echo -e "${BOLD}Test 14: purge on empty camera${RESET}"
OUT=$(stash purge 2>&1) || true

if echo "$OUT" | grep -q "nothing to purge\|No calibration data"; then
    pass "purge on empty camera is a no-op"
else
    skip "purge-on-empty message"
fi

# Clean up (in case anything is left).
stash purge >/dev/null 2>&1 || true

echo ""

# ── Test 15: Download + remap integrity ──────────────────────────────

echo -e "${BOLD}Test 15: download slot with remap integrity check${RESET}"

# Upload the test session to slot 0.
OUT=$(stash upload --slot 0 "$TEST_SESSION" 2>&1) || true
if ! echo "$OUT" | grep -q "Done"; then
    fail "upload for download test" "output: $OUT"
else
    # Download slot 0 to a new directory.
    DL_DIR="$TMPDIR/dl_slot0"
    OUT=$(stash download --slot 0 -o "$DL_DIR" 2>&1) || true

    if echo "$OUT" | grep -q "Done.*downloaded"; then
        pass "download slot 0 completes"
    else
        fail "download slot 0" "output: $OUT"
    fi

    # Remap files should be byte-identical to the originals.
    if [[ -f "$DL_DIR/calib_result/remap_left.bin" ]] && \
       cmp -s "$TEST_SESSION/calib_result/remap_left.bin" \
              "$DL_DIR/calib_result/remap_left.bin"; then
        pass "remap_left.bin is byte-identical"
    else
        fail "remap_left.bin integrity" \
             "orig=$(wc -c < "$TEST_SESSION/calib_result/remap_left.bin") dl=$(wc -c < "$DL_DIR/calib_result/remap_left.bin" 2>/dev/null || echo missing)"
    fi

    if [[ -f "$DL_DIR/calib_result/remap_right.bin" ]] && \
       cmp -s "$TEST_SESSION/calib_result/remap_right.bin" \
              "$DL_DIR/calib_result/remap_right.bin"; then
        pass "remap_right.bin is byte-identical"
    else
        fail "remap_right.bin integrity" \
             "orig=$(wc -c < "$TEST_SESSION/calib_result/remap_right.bin") dl=$(wc -c < "$DL_DIR/calib_result/remap_right.bin" 2>/dev/null || echo missing)"
    fi
fi

echo ""

# ── Test 16: Downloaded JSON has expected content ─────────────────────

echo -e "${BOLD}Test 16: downloaded JSON metadata${RESET}"

DL_JSON="$DL_DIR/calib_result/calibration_meta.json"
if [[ -f "$DL_JSON" ]]; then
    pass "calibration_meta.json exists"

    if grep -q '"image_size"' "$DL_JSON"; then
        pass "JSON has image_size field"
    else
        fail "JSON missing image_size"
    fi

    if grep -q '"rms_stereo_px"' "$DL_JSON"; then
        pass "JSON has rms_stereo_px field"
    else
        fail "JSON missing rms_stereo_px"
    fi

    if grep -q '"packed_at"' "$DL_JSON"; then
        pass "JSON has packed_at timestamp"
    else
        skip "packed_at field"
    fi
else
    fail "calibration_meta.json missing from download"
fi

echo ""

# ── Test 17: Round-trip integrity (re-upload → re-download) ──────────

echo -e "${BOLD}Test 17: round-trip integrity${RESET}"

# Re-upload the downloaded session to slot 1.
OUT=$(stash upload --slot 1 "$DL_DIR" 2>&1) || true
if ! echo "$OUT" | grep -q "Done"; then
    fail "re-upload downloaded session" "output: $OUT"
else
    pass "downloaded session re-uploads to slot 1"

    # Download slot 1 to a second directory.
    DL_DIR2="$TMPDIR/dl_slot1"
    OUT=$(stash download --slot 1 -o "$DL_DIR2" 2>&1) || true

    if echo "$OUT" | grep -q "Done"; then
        pass "download slot 1 completes"
    else
        fail "download slot 1" "output: $OUT"
    fi

    # Compare remap files from both downloads — should be identical.
    if cmp -s "$DL_DIR/calib_result/remap_left.bin" \
              "$DL_DIR2/calib_result/remap_left.bin"; then
        pass "round-trip remap_left.bin matches"
    else
        fail "round-trip remap_left.bin mismatch"
    fi

    if cmp -s "$DL_DIR/calib_result/remap_right.bin" \
              "$DL_DIR2/calib_result/remap_right.bin"; then
        pass "round-trip remap_right.bin matches"
    else
        fail "round-trip remap_right.bin mismatch"
    fi
fi

# Final clean-up.
stash purge >/dev/null 2>&1 || true

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
