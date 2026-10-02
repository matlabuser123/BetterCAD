#!/usr/bin/env bash
# Mutation testing for P16-QUALITY-001. A mutation that SURVIVES marks a gap in
# the tests, not a harmless variant. See README.md in this directory.
#
# The pristine source is snapshotted first and restored at the end, including
# after a failure, so a mutation is never left in the tree.
set -u
export BETTERCAD_BUILD_ROOT=${BETTERCAD_BUILD_ROOT:-C:/Users/uqhas/AppData/Local/bc-build}
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$BETTERCAD_BUILD_ROOT/debug-ext"
EXE="$BUILD/bin/bettercad_tests.exe"
RESULTS=$HERE/results-final.txt
: > "$RESULTS"

restore() {
    python "$HERE/apply.py" restore
    cmake --build "$BUILD" --target bettercad_tests > "$HERE/build.log" 2>&1 ||
        echo "RESTORE BUILD FAILED -- the tree is NOT clean, read build.log"
}
trap restore EXIT

python "$HERE/apply.py" snapshot
N=$(python "$HERE/apply.py" count)
for ((i = 0; i < N; i++)); do
    LABEL=$(python "$HERE/apply.py" label "$i")
    STATE=$(python "$HERE/apply.py" "$i")
    if [ "$STATE" != "applied" ]; then
        printf '%-74s %s\n' "$LABEL" "NOT APPLIED ($STATE)" | tee -a "$RESULTS"
        continue
    fi
    if ! cmake --build "$BUILD" --target bettercad_tests > "$HERE/build.log" 2>&1; then
        printf '%-74s %s\n' "$LABEL" "killed by the COMPILER" | tee -a "$RESULTS"
        continue
    fi
    if "$EXE" "[quality]" -r compact > "$HERE/run.log" 2>&1; then
        printf '%-74s %s\n' "$LABEL" "*** SURVIVED ***" | tee -a "$RESULTS"
    else
        SUMMARY=$(grep -oE 'assertions: [0-9]+ \| [0-9]+ passed \| [0-9]+ failed' "$HERE/run.log" |
            tail -1)
        [ -z "$SUMMARY" ] && SUMMARY=$(tail -1 "$HERE/run.log")
        printf '%-74s %s\n' "$LABEL" "killed ($SUMMARY)" | tee -a "$RESULTS"
    fi
done

echo
echo "---- results ----"
cat "$RESULTS"
