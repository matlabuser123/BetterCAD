#!/usr/bin/env bash
# Mutation testing for P16-CMD-001. A mutation that SURVIVES marks a gap in the
# tests, not a harmless variant. See README.md in this directory.
#
# The pristine sources are snapshotted first and restored at the end, including
# after a failure, so a mutation is never left in the tree.
#
# DO NOT EDIT THE SOURCE TREE WHILE THIS RUNS, AND DO NOT STOP IT BY KILLING
# ONLY ITS PARENT. Both were learned the expensive way on this milestone.
#
# The run below restores every file from the snapshot after each mutation and
# again from the EXIT trap, so an edit made during a run is silently reverted,
# and a concurrent build of a half-edited tree makes a mutation look "killed by
# the COMPILER" when nothing was wrong with it. One such run produced a
# complete, plausible, entirely VOID results.txt -- and a 0-byte test
# executable, because the trap's rebuild and an interactive build were writing
# it at the same time.
#
# Stopping it means killing the process TREE: this script keeps running when
# its parent shell is killed, and its EXIT trap then fires later, restoring the
# tree underneath whatever is happening by then.
#
# The lock below refuses a second concurrent run. It cannot stop a human
# editing the tree, so this comment has to.
set -u
export BETTERCAD_BUILD_ROOT=${BETTERCAD_BUILD_ROOT:-C:/Users/uqhas/AppData/Local/bc-build}
HERE="$(cd "$(dirname "$0")" && pwd)"
LOCK="$HERE/mutate.lock"
if [ -e "$LOCK" ]; then
    echo "REFUSED: $LOCK exists, so a run is already in progress (pid $(cat "$LOCK"))."
    echo "If no run is in progress, the last one was killed; check the tree with"
    echo "  git diff -- src tests   and   python apply.py restore"
    echo "then remove the lock."
    exit 1
fi
echo $$ > "$LOCK"
BUILD="$BETTERCAD_BUILD_ROOT/debug-ext"
EXE="$BUILD/bin/bettercad_tests.exe"
RESULTS=$HERE/results.txt
: > "$RESULTS"

restore() {
    rm -f "$LOCK"
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
        printf '%-78s %s\n' "$LABEL" "NOT APPLIED ($STATE)" | tee -a "$RESULTS"
        continue
    fi
    if ! cmake --build "$BUILD" --target bettercad_tests > "$HERE/build.log" 2>&1; then
        printf '%-78s %s\n' "$LABEL" "killed by the COMPILER" | tee -a "$RESULTS"
        continue
    fi
    # The whole meshing module, not only [map]: an attribution mutation can
    # break the surface or volume contracts the mapping is built on, and that
    # is a kill worth seeing.
    if "$EXE" "[meshcmd]" -r compact > "$HERE/run.log" 2>&1; then
        printf '%-78s %s\n' "$LABEL" "*** SURVIVED ***" | tee -a "$RESULTS"
    else
        SUMMARY=$(grep -oE 'assertions: [0-9]+ \| [0-9]+ passed \| [0-9]+ failed' "$HERE/run.log" |
            tail -1)
        [ -z "$SUMMARY" ] && SUMMARY=$(tail -1 "$HERE/run.log")
        printf '%-78s %s\n' "$LABEL" "killed ($SUMMARY)" | tee -a "$RESULTS"
    fi
done

echo
echo "---- results ----"
cat "$RESULTS"
