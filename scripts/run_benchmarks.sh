#!/usr/bin/env bash
# ============================================================================
# cwdaemon regression test runner
#
# Runs all baseline samples through the cwdaemon benchmark mode and reports
# pass/fail based on whether character accuracy meets the baseline threshold.
#
# Usage:
#   ./scripts/run_benchmarks.sh              # Run all tests
#   ./scripts/run_benchmarks.sh --threshold 90  # Custom pass threshold
#   ./scripts/run_benchmarks.sh --verbose    # Show full benchmark output
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
CWDAEMON="$BUILD_DIR/cwdaemon"

# Defaults
THRESHOLD=90
VERBOSE=false

# Parse args
while [[ $# -gt 0 ]]; do
    case $1 in
        --threshold) THRESHOLD=$2; shift 2 ;;
        --verbose)   VERBOSE=true; shift ;;
        *)           echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# Check binary exists
if [[ ! -x "$CWDAEMON" ]]; then
    echo "ERROR: cwdaemon binary not found at $CWDAEMON"
    echo "       Run: cd build && cmake --build . --parallel"
    exit 1
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║         CWDAEMON REGRESSION TEST SUITE              ║${NC}"
echo -e "${CYAN}║         Pass threshold: ${THRESHOLD}% character accuracy       ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

TOTAL=0
PASSED=0
FAILED=0
TOTAL_ACCURACY=0

for wav in "$PROJECT_DIR"/baseline_sample_*.wav; do
    [[ ! -f "$wav" ]] && continue

    base=$(basename "$wav" .wav)
    expected="$PROJECT_DIR/${base}_decode.txt"

    if [[ ! -f "$expected" ]]; then
        echo -e "${YELLOW}SKIP${NC} $base (no expected file)"
        continue
    fi

    TOTAL=$((TOTAL + 1))

    # Run benchmark and capture output
    output=$("$CWDAEMON" --wav "$wav" --expected "$expected" 2>/dev/null)

    # Extract accuracy from output
    accuracy=$(echo "$output" | grep "Character Accuracy" | grep -oP '[\d.]+(?=%)' | head -1)
    wpm=$(echo "$output" | grep "Final WPM" | grep -oP '\d+' | tail -1)
    snr=$(echo "$output" | grep "SNR Metric" | grep -oP '\d+' | tail -1)
    edits=$(echo "$output" | grep "Edit Distance" | grep -oP '\d+' | tail -1)

    if [[ -z "$accuracy" ]]; then
        echo -e "${RED}FAIL${NC} $base - could not parse accuracy"
        FAILED=$((FAILED + 1))
        continue
    fi

    # Compare against threshold
    pass=$(python3 -c "print('yes' if float('$accuracy') >= $THRESHOLD else 'no')")
    TOTAL_ACCURACY=$(python3 -c "print(float('$TOTAL_ACCURACY') + float('$accuracy'))")

    if [[ "$pass" == "yes" ]]; then
        echo -e "${GREEN}PASS${NC} $base  ${accuracy}% char  ${wpm} WPM  SNR ${snr}  (${edits} edits)"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}FAIL${NC} $base  ${accuracy}% char  ${wpm} WPM  SNR ${snr}  (${edits} edits)"
        FAILED=$((FAILED + 1))
    fi

    if [[ "$VERBOSE" == "true" ]]; then
        echo "$output"
        echo ""
    fi
done

echo ""
AVG_ACCURACY=$(python3 -c "print(f'{float('$TOTAL_ACCURACY') / max($TOTAL, 1):.1f}')")

echo -e "${CYAN}══════════════════════════════════════════════════════${NC}"
echo -e "  Results: ${GREEN}${PASSED} passed${NC}, ${RED}${FAILED} failed${NC}, ${TOTAL} total"
echo -e "  Average accuracy: ${AVG_ACCURACY}%"
echo -e "${CYAN}══════════════════════════════════════════════════════${NC}"

if [[ $FAILED -gt 0 ]]; then
    exit 1
fi
exit 0
