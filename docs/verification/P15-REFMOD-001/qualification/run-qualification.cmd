@echo off
rem P15-REFMOD-001 qualification: the engineering-data reference models.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has since
rem INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears, that fix
rem has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. Three reasons, and the first is not about materials at all:
rem
rem   1. THIS MILESTONE FIXED A DEFECT IN A SHIPPED COMMAND. `mass-properties` listed
rem      consumed intermediate bodies as results; the fix routes it through
rem      features::resultFeatures(). Every CLI suite could notice a mistake there, and
rem      the P15-CLI in-process and process suites are the ones that would.
rem
rem   2. It extends tests/reference/Analytic.hpp, which EVERY reference-model test
rem      includes -- the parts, the assemblies and the drawings. A mistake in that
rem      header breaks expectations across P11, P12, P13 and P14, none of which is
rem      named "material".
rem
rem   3. It adds a fourth loop and a fourth suite to the reference-model runner, whose
rem      exit code is a fixture for the P14 drawing CLI tests as well.
rem
rem ZERO-MATCH PROTECTION. A repeat filter matching no tests would let the determinism
rem gate pass by running nothing. It cannot: the test presets set noTestsAction=error,
rem and qualify.cmd counts the selected tests itself and records the number (the guard
rem added by P15-CLI-001). `[A-Za-z]` matches every test name in the suite.
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
