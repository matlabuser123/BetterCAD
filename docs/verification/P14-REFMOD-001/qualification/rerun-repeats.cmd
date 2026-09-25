@echo off
rem P14-REFMOD-001: the controlled rerun of the two determinism repeat stages.
rem
rem WHY THIS FILE EXISTS. Both repeat stages of the qualification exited 8 on
rem the same environment fault, in the same shape it has taken in three earlier
rem milestones:
rem
rem   release  cli.drawing.batch passed on repeats 1, 2 and 3 and failed on 4:
rem            "cannot replace '...build/release/tests/cli-output/drawing/
rem            drawn.bcad': Permission denied"
rem   debug    the example program's DXF export failed the same way:
rem            "cannot replace '...build/debug/tests/cli-output/refmod/
rem            drawing_tolerance_block.dxf': Permission denied"
rem
rem Both are a Windows sharing violation on an ATOMIC REPLACE of a file inside
rem the OneDrive-synced build tree, on a file the same test had already written
rem successfully in the same run. TODO.md records the identical signature for
rem P14-DIM-001, P14-ANNO-001 and P14-BOM-001, and records that each time one
rem controlled rerun passed. This is that rerun.
rem
rem It runs the SAME two stages with the SAME filter, not a subset: a rerun of
rem part of a gate is not a rerun of the gate. Nothing is relaxed, no test is
rem excluded, and the exit code is the number of stages that failed, so a
rem second failure fails this command too.
rem
rem   rerun-repeats.cmd
setlocal enabledelayedexpansion
chcp 65001 > nul
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\rerun-times.txt"
set /a FAILURES=0

rem The filter run-qualification.cmd uses, verbatim.
call "%EVIDENCE%\repeat-filter.cmd"

echo controlled rerun started !date! !time! > "%TIMES%"
echo reason: both repeat stages failed on the recorded OneDrive replace fault >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"

for %%P in (release debug) do (
    echo rerun repeat %%P started !date! !time! >> "%TIMES%"
    ctest --preset %%P -j 8 -R "%QUALIFY_REPEAT%" --repeat until-fail:5 > "%EVIDENCE%\ctest-repeat-rerun-%%P.log" 2>&1
    set "REPEATED=!errorlevel!"
    if not "!REPEATED!"=="0" set /a FAILURES+=1
    echo rerun repeat %%P exit !REPEATED! !date! !time! >> "%TIMES%"
)

echo controlled rerun finished !date! !time!, !FAILURES! stage^(s^) failed >> "%TIMES%"
if not "!FAILURES!"=="0" (
    echo RERUN FAILED: !FAILURES! stage^(s^) failed. See "%TIMES%".
) else (
    echo Controlled rerun passed: both repeat stages exited 0.
)
endlocal & exit /b %FAILURES%
