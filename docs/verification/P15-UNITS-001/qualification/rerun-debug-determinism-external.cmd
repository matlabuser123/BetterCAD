@echo off
rem P15-UNITS-001: the debug determinism gate, re-run from a build tree OUTSIDE
rem the synchronised folder.
rem
rem This is NOT another controlled rerun. The gate failed twice in the same
rem environment -- ctest-repeat-debug.log and ctest-repeat-rerun-debug.log, on
rem cli.refmod.build and then cli.drawing.batch, 0 test-logic assertions failing
rem both times -- and a third attempt under the same conditions would only be
rem choosing which result to keep. INFRA-QT-DEPLOY-001 changed the conditions:
rem the build tree is no longer inside the folder the synchroniser watches.
rem
rem The filter is the SAME as the stage it replaces -- `[A-Za-z]`, every test,
rem five times over -- so the result is comparable with the two failures rather
rem than a narrower run that avoids them.
rem
rem Requires the debug-ext tree to be built: run
rem docs/verification/INFRA-QT-DEPLOY-001/qualification/run-qualification.cmd
rem first.
setlocal enabledelayedexpansion
chcp 65001 > nul
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build"
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\rerun-external-times.txt"

echo debug determinism from a build tree outside OneDrive !date! !time! > "%TIMES%"
echo build root: !BETTERCAD_BUILD_ROOT! >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"

ctest --preset debug-ext -j 8 -R "[A-Za-z]" --repeat until-fail:5 ^
    > "%EVIDENCE%\ctest-repeat-debug-ext.log" 2>&1
set "OUTCOME=!errorlevel!"
echo repeat debug-ext exit !OUTCOME! !date! !time! >> "%TIMES%"

if not "!OUTCOME!"=="0" (
    echo DETERMINISM FAILED: exit !OUTCOME!. The gate stays BLOCKED.
) else (
    echo Determinism passed: every test, five times over, exit 0.
)
endlocal & exit /b %OUTCOME%
