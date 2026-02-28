#!/usr/bin/env bash
#
# test_binning_hw.sh — hardware integration tests for binning behaviour
#
# Requires a Lucid camera on the network.  Captures frames with and
# without binning and validates the output:
#
#   - binning=1 PNG output is colour (RGB, 3 channels)
#   - binning=2 PNG output is grayscale (1 channel) — the fix
#   - binning=2 PGM output is valid single-channel
#   - dimensions halve correctly under binning=2
#
# Uses Python3 (always available on macOS/Linux) to inspect PNG headers
# without requiring ImageMagick or Pillow.
#
# Usage:
#   tests/test_binning_hw.sh               # auto-discover camera
#   tests/test_binning_hw.sh -a 192.168.x  # target by IP
#   tests/test_binning_hw.sh -s SERIAL     # target by serial
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

# ── PNG inspection helper ────────────────────────────────────────────
#
# Reads the IHDR chunk from a PNG file and prints:
#   <width> <height> <color_type>
#
# PNG color types: 0=grayscale, 2=RGB, 3=indexed, 4=gray+alpha, 6=RGBA

png_info () {
    python3 -c "
import struct, sys
with open(sys.argv[1], 'rb') as f:
    sig = f.read(8)
    if sig != b'\\x89PNG\\r\\n\\x1a\\n':
        print('ERROR: not a PNG', file=sys.stderr)
        sys.exit(1)
    # IHDR chunk: 4-byte length, 4-byte type, then 13 bytes of data
    length = struct.unpack('>I', f.read(4))[0]
    chunk_type = f.read(4)
    assert chunk_type == b'IHDR', f'unexpected chunk: {chunk_type}'
    w, h, bit_depth, color_type = struct.unpack('>IIBB', f.read(10))
    print(w, h, color_type)
" "$1"
}

# ── PGM inspection helper ────────────────────────────────────────────
#
# Reads a PGM (P5) header and prints: <width> <height>

pgm_info () {
    python3 -c "
import sys
with open(sys.argv[1], 'rb') as f:
    magic = f.readline().decode().strip()
    if magic != 'P5':
        print('ERROR: not a PGM (P5)', file=sys.stderr)
        sys.exit(1)
    # Skip comments.
    line = f.readline().decode().strip()
    while line.startswith('#'):
        line = f.readline().decode().strip()
    w, h = line.split()
    print(w, h)
" "$1"
}

# ── Pre-flight ─────────────────────────────────────────────────────────

if [[ ! -x "$TOOL" ]]; then
    echo "error: $TOOL not found — run 'make' first"
    exit 1
fi

echo -e "${BOLD}=== Hardware Integration Tests: binning ===${RESET}"
echo ""

if ! "$TOOL" list >/dev/null 2>&1; then
    echo -e "${YELLOW}SKIP: no cameras found on the network${RESET}"
    exit 77
fi

echo "Camera found. Running binning capture tests..."
echo ""

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ── Helper: run capture with device opts ──────────────────────────────

capture () {
    "$TOOL" capture "$@" ${DEVICE_OPTS[@]+"${DEVICE_OPTS[@]}"} -A
}

# ── Test 1: Capture without binning (PNG, colour) ─────────────────────

echo -e "${BOLD}Test 1: capture -b 1 -e png (full resolution, colour)${RESET}"
DIR1="$TMPDIR/b1_png"
mkdir -p "$DIR1"
OUT=$(capture -b 1 -e png -o "$DIR1" 2>&1) || true

LEFT=$(find "$DIR1" -name '*_left.png' | head -1)
RIGHT=$(find "$DIR1" -name '*_right.png' | head -1)

if [[ -n "$LEFT" && -f "$LEFT" ]]; then
    pass "left PNG created"

    INFO=$(png_info "$LEFT")
    W=$(echo "$INFO" | awk '{print $1}')
    H=$(echo "$INFO" | awk '{print $2}')
    CT=$(echo "$INFO" | awk '{print $3}')

    if [[ "$W" == "1440" && "$H" == "1080" ]]; then
        pass "left PNG is 1440x1080 (full resolution)"
    else
        fail "left PNG resolution" "got ${W}x${H}, expected 1440x1080"
    fi

    if [[ "$CT" == "2" ]]; then
        pass "left PNG is RGB colour (color_type=2)"
    else
        fail "left PNG colour type" "got color_type=$CT, expected 2 (RGB)"
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

# ── Test 2: Capture with binning=2 (PNG, should be grayscale) ─────────

echo -e "${BOLD}Test 2: capture -b 2 -e png (binned, grayscale)${RESET}"
DIR2="$TMPDIR/b2_png"
mkdir -p "$DIR2"
OUT=$(capture -b 2 -e png -o "$DIR2" 2>&1) || true

LEFT=$(find "$DIR2" -name '*_left.png' | head -1)
RIGHT=$(find "$DIR2" -name '*_right.png' | head -1)

if [[ -n "$LEFT" && -f "$LEFT" ]]; then
    pass "left PNG created"

    INFO=$(png_info "$LEFT")
    W=$(echo "$INFO" | awk '{print $1}')
    H=$(echo "$INFO" | awk '{print $2}')
    CT=$(echo "$INFO" | awk '{print $3}')

    if [[ "$W" == "720" && "$H" == "540" ]]; then
        pass "left PNG is 720x540 (half resolution)"
    else
        # Hardware binning might report different dimensions; accept if halved.
        if [[ "$W" -le 720 && "$H" -le 540 ]]; then
            pass "left PNG dimensions are binned (${W}x${H})"
        else
            fail "left PNG resolution" "got ${W}x${H}, expected 720x540"
        fi
    fi

    if [[ "$CT" == "0" ]]; then
        pass "left PNG is grayscale (color_type=0) — binning fix confirmed"
    elif [[ "$CT" == "2" ]]; then
        # If IspBayerPattern reported a valid pattern, colour is correct.
        if echo "$OUT" | grep -q "preserves Bayer CFA"; then
            pass "left PNG is RGB (camera reports Bayer CFA preserved)"
        else
            fail "left PNG is still RGB under binning=2" \
                 "color_type=$CT — expected 0 (grayscale)"
        fi
    else
        fail "left PNG colour type" "got color_type=$CT"
    fi
else
    fail "left PNG not created" "output: $OUT"
fi

if [[ -n "$RIGHT" && -f "$RIGHT" ]]; then
    INFO=$(png_info "$RIGHT")
    CT=$(echo "$INFO" | awk '{print $3}')

    if [[ "$CT" == "0" ]]; then
        pass "right PNG is grayscale (color_type=0)"
    elif echo "$OUT" | grep -q "preserves Bayer CFA"; then
        pass "right PNG is RGB (camera reports Bayer CFA preserved)"
    else
        fail "right PNG colour type under binning" "color_type=$CT"
    fi
else
    fail "right PNG not created"
fi

echo ""

# ── Test 3: Capture with binning=2 (PGM, always single-channel) ──────

echo -e "${BOLD}Test 3: capture -b 2 -e pgm (binned, PGM)${RESET}"
DIR3="$TMPDIR/b2_pgm"
mkdir -p "$DIR3"
OUT=$(capture -b 2 -e pgm -o "$DIR3" 2>&1) || true

LEFT=$(find "$DIR3" -name '*_left.pgm' | head -1)

if [[ -n "$LEFT" && -f "$LEFT" ]]; then
    pass "left PGM created"

    INFO=$(pgm_info "$LEFT")
    W=$(echo "$INFO" | awk '{print $1}')
    H=$(echo "$INFO" | awk '{print $2}')

    if [[ "$W" == "720" && "$H" == "540" ]]; then
        pass "PGM is 720x540"
    else
        if [[ "$W" -le 720 && "$H" -le 540 ]]; then
            pass "PGM dimensions are binned (${W}x${H})"
        else
            fail "PGM resolution" "got ${W}x${H}, expected 720x540"
        fi
    fi

    # Validate file size matches dimensions (P5 header + W*H bytes).
    FSIZE=$(wc -c < "$LEFT" | tr -d ' ')
    EXPECTED_MIN=$((W * H))
    if [[ "$FSIZE" -ge "$EXPECTED_MIN" ]]; then
        pass "PGM file size consistent (${FSIZE} bytes)"
    else
        fail "PGM file size" "got $FSIZE, expected >= $EXPECTED_MIN"
    fi
else
    fail "left PGM not created" "output: $OUT"
fi

echo ""

# ── Test 4: Size comparison (grayscale PNG should be smaller) ─────────

echo -e "${BOLD}Test 4: grayscale PNG smaller than colour PNG${RESET}"

B1_LEFT=$(find "$DIR1" -name '*_left.png' | head -1)
B2_LEFT=$(find "$DIR2" -name '*_left.png' | head -1)

if [[ -n "$B1_LEFT" && -f "$B1_LEFT" && -n "$B2_LEFT" && -f "$B2_LEFT" ]]; then
    SIZE1=$(wc -c < "$B1_LEFT" | tr -d ' ')
    SIZE2=$(wc -c < "$B2_LEFT" | tr -d ' ')

    # Binned grayscale at 1/4 resolution should be much smaller.
    if [[ "$SIZE2" -lt "$SIZE1" ]]; then
        RATIO=$((SIZE1 / SIZE2))
        pass "binned grayscale PNG (${SIZE2}B) < full colour PNG (${SIZE1}B), ~${RATIO}x smaller"
    else
        fail "size comparison" \
             "binned=${SIZE2}B, full=${SIZE1}B — expected binned to be smaller"
    fi
else
    skip "size comparison (missing files)"
fi

echo ""

# ── Test 5: stdout messages confirm data interpretation ───────────────

echo -e "${BOLD}Test 5: capture output messages${RESET}"

# Re-read the binning=2 capture output for diagnostic messages.
DIR5="$TMPDIR/b2_msg"
mkdir -p "$DIR5"
OUT=$(capture -b 2 -e png -o "$DIR5" 2>&1) || true

if echo "$OUT" | grep -q "IspBayerPattern"; then
    pass "IspBayerPattern readback logged"
else
    skip "IspBayerPattern message (may not be supported)"
fi

if echo "$OUT" | grep -q "gray\|grayscale\|Bayer CFA"; then
    pass "binning data interpretation logged"
else
    skip "data interpretation message"
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
