@echo off
rem P15-MASS-001 qualification: derived mass properties.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears,
rem that fix has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. Three reasons this milestone in particular needs the whole suite and
rem not a mass-shaped subset:
rem
rem   1. It RENAMED a public accessor on geometry::Body, a header included by every
rem      layer above core. The rename compiles or it does not, but Body.hpp also
rem      gained a type and the adapter gained a second integration tolerance, and
rem      nothing named "mass" would exercise the reference models, the drawings or
rem      the STEP round trips that read a body's properties.
rem
rem   2. It TIGHTENED the kernel's integration tolerance for second moments to
rem      1e-14. That is a new constant rather than a change to the existing one, so
rem      volume and area are untouched by construction -- but "by construction" is
rem      an argument, and the suite is evidence.
rem
rem   3. It added four units to the UnitCatalog, whose size is asserted, and whose
rem      contents the parameter reader consults by symbol on load. A unit added to
rem      Units.hpp but not registered would save and fail to LOAD, which only a
rem      persistence test reaches.
rem
rem Determinism matters more here than in most milestones: the integration is
rem floating point, driven near machine precision, and -O2 may reassociate. The
rem three presets and the two repeat stages are what answer whether Debug and
rem Release agree.
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
