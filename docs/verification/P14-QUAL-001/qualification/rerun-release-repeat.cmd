@echo off
rem P14-QUAL-001: the controlled rerun of the ONE stage that failed.
rem
rem WHY. The release determinism stage exited 8 on the recorded OneDrive
rem replace fault: cli.refmod.build passed on repeats 1, 2 and 3 (1.69 s,
rem 1.57 s, 1.49 s) and failed on the 4th with
rem
rem   cannot replace '...build/release/tests/cli-output/refmod\
rem   drawing_angle_bracket.dxf': Permission denied
rem
rem A Windows sharing violation on REPLACING a file the same test had already
rem written three times in the same run, inside the synchronised build tree.
rem TODO.md records this signature for P14-DIM-001, P14-ANNO-001, P14-BOM-001
rem and P14-REFMOD-001, and records that one controlled rerun passed each time.
rem
rem WHAT THIS IS NOT. It is not a retry of the failing test, not a narrowed
rem filter, and not an exclusion. It re-runs the WHOLE release determinism
rem stage with the SAME filter the gate used -- [A-Za-z], every one of the 2258
rem tests -- five times over, and exits non-zero if it fails again. A rerun of
rem part of a gate is not a rerun of the gate.
rem
rem The debug determinism stage passed in the qualification and is NOT re-run:
rem re-running a stage that passed would be choosing which result to keep.
setlocal enabledelayedexpansion
chcp 65001 > nul
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\rerun-times.txt"

echo controlled rerun of the release determinism stage !date! !time! > "%TIMES%"
echo reason: the recorded OneDrive replace fault, 3 passes then a failed replace >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"

ctest --preset release -j 8 -R "[A-Za-z]" --repeat until-fail:5 > "%EVIDENCE%\ctest-repeat-rerun-release.log" 2>&1
set "OUTCOME=!errorlevel!"
echo rerun repeat release exit !OUTCOME! !date! !time! >> "%TIMES%"

if not "!OUTCOME!"=="0" (
    echo RERUN FAILED: the release determinism stage failed again, exit !OUTCOME!.
) else (
    echo Controlled rerun passed: the release determinism stage exited 0.
)
endlocal & exit /b %OUTCOME%
