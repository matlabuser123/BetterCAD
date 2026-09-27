@echo off
rem P15-ASSIGN-001 qualification: stable material assignment.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears,
rem that fix has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone touches Document, which every layer above it uses, and
rem it adds a member to Document::clone() -- a function that copies its members by
rem hand, so a field left out of it would not fail to compile, it would silently
rem fail to clone. Nothing about "assignment" would run the persistence, assembly
rem or drawing tests that would notice.
rem
rem It also extends tests/compile_fail/CMakeLists.txt, which every compile-fail
rem group shares. Two milestones running, a compile-fail case has gone stale
rem without failing to compile; the total filter is what caught both.
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
