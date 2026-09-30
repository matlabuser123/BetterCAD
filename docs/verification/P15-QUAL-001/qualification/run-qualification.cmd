@echo off
rem P15-QUAL-001 -- the FINAL qualification of the whole P15 phase.
rem
rem Run against an ALREADY-COMMITTED tree. The tests this milestone adds were
rem committed and pushed first (8d4b23c), so the tree being qualified is the tree
rem in origin/main, and the only things that change afterwards are docs/ and
rem TODO.md -- neither of which is in the eight-path source fingerprint, and
rem neither of which any target, test or generator consumes.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace"
rem appears, the infrastructure has regressed: STOP, do not rerun until green,
rem and requalify the infrastructure first.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. For a PHASE qualification nothing narrower is defensible: the gate
rem is the whole product, not the subject of one milestone, and a filter is
rem exactly the mechanism by which a phase gate could pass while something
rem outside the filter was broken.
rem
rem ZERO-MATCH PROTECTION. The test presets set noTestsAction=error, and
rem qualify.cmd counts the selected tests itself and records the number.
rem `[A-Za-z]` matches every test name in the suite.
setlocal enabledelayedexpansion
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_REPEAT=[A-Za-z]"
set "HERE=%~dp0"

echo build root: !BETTERCAD_BUILD_ROOT!
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
set "OUTCOME=!errorlevel!"
echo build root: !BETTERCAD_BUILD_ROOT! >> "%HERE%qualification-times.txt"
endlocal & exit /b %OUTCOME%
