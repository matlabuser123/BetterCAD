@echo off
rem INFRA-QT-DEPLOY-001: does moving the build tree off the synchronised folder
rem actually stop losing the tests that REPLACE a file?
rem
rem Six milestones have failed a determinism repeat on the same signature, and
rem always in one of these two tests:
rem
rem   DrawnHolePlate: save failed: cannot replace
rem   '...build/debug/tests/cli-output/refmod\drawing_hole_plate.bcad':
rem   Permission denied
rem
rem A Windows sharing violation on an atomic replace inside the build tree, of a
rem file the same test had already written successfully earlier in the same run.
rem Never on the first write; always on a replace. It is not a test that fails,
rem it is a file the synchroniser still had open.
rem
rem So the two tests are run twelve times each, back to back, from the build
rem tree OUTSIDE the synchronised folder. Twelve is chosen to exceed the ten
rem consecutive runs asked for, and because the recorded failures came on the
rem 3rd repeat: a run that survives twelve has passed the point where this
rem fault has always appeared.
rem
rem `--repeat until-fail:12` stops at the first failure, so a non-zero exit here
rem means the fault is still present and the move has not fixed it.
setlocal enabledelayedexpansion
chcp 65001 > nul
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build"
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\stress-times.txt"

echo file-replacement stress started !date! !time! > "%TIMES%"
echo build root: !BETTERCAD_BUILD_ROOT! >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"

ctest --preset debug-ext -R "cli.refmod.build|cli.drawing.batch" --repeat until-fail:12 ^
    > "%EVIDENCE%\stress-file-replacement.log" 2>&1
set "OUTCOME=!errorlevel!"
echo stress exit !OUTCOME! !date! !time! >> "%TIMES%"

if not "!OUTCOME!"=="0" (
    echo STRESS FAILED: exit !OUTCOME!. The replace fault is still present.
) else (
    echo Stress passed: 12 consecutive runs of both tests, no failure.
)
endlocal & exit /b %OUTCOME%
