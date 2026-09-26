@echo off
rem P15-UNITS-001: controlled rerun of the one stage that failed.
rem
rem The debug determinism stage exited 8 on the recorded OneDrive replace fault:
rem cli.refmod.build passed on repeats 1 and 2 (3.77 s, 1.96 s) and failed on the
rem 3rd with
rem
rem   DrawnHolePlate: save failed: cannot replace
rem   '...build/debug/tests/cli-output/refmod\drawing_hole_plate.bcad':
rem   Permission denied
rem
rem A Windows sharing violation on an atomic REPLACE inside the synchronised
rem build tree, of a file the same test had already written twice in the same
rem run. This is the SIXTH milestone with this signature (P14-DIM-001,
rem P14-ANNO-001, P14-BOM-001, P14-REFMOD-001, P14-QUAL-001, and now here), and
rem every time one controlled rerun has passed.
rem
rem It re-runs the WHOLE debug determinism stage with the SAME [A-Za-z] filter --
rem all 2286 tests, five times over -- and exits non-zero if it fails again.
rem The release stage passed and is NOT re-run: re-running a passing stage would
rem be choosing which result to keep.
setlocal enabledelayedexpansion
chcp 65001 > nul
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\rerun-times.txt"

echo controlled rerun of the debug determinism stage !date! !time! > "%TIMES%"
echo reason: the recorded OneDrive replace fault, 2 passes then a failed replace >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"

ctest --preset debug -j 8 -R "[A-Za-z]" --repeat until-fail:5 > "%EVIDENCE%\ctest-repeat-rerun-debug.log" 2>&1
set "OUTCOME=!errorlevel!"
echo rerun repeat debug exit !OUTCOME! !date! !time! >> "%TIMES%"

if not "!OUTCOME!"=="0" (
    echo RERUN FAILED: the debug determinism stage failed again, exit !OUTCOME!.
) else (
    echo Controlled rerun passed: the debug determinism stage exited 0.
)
endlocal & exit /b %OUTCOME%
