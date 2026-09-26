@echo off
rem P15-MAT-001 qualification: material identity and library foundation.
rem
rem The first milestone qualified entirely from a build tree OUTSIDE the
rem synchronised folder, which INFRA-QT-DEPLOY-001 made possible. No
rem controlled-rerun policy is expected to be needed for the OneDrive replace
rem fault; if "Permission denied" or "cannot replace" appears here, the
rem infrastructure blocker is not actually closed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over -- and
rem that is the honest choice rather than a narrow one. The milestone adds a tag
rem and an ID alias to core/Id.hpp, which essentially every translation unit
rem includes, so "the tests near the change" is not a meaningful subset. It is
rem also what P15-UNITS-001 used, for the same reason, and a narrowed filter
rem there would have missed the one pre-existing test that failed.
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
