@echo off
rem P15-CUSTOM-001 qualification: custom material cloning and controlled overrides.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears,
rem that fix has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone touches no geometry and no kernel call, so a narrower
rem filter would be tempting. Three reasons not to:
rem
rem   1. It extends features/Materials.hpp, which the assignment and mass layers
rem      both include, and it adds a `namespace {}` block to Materials.cpp beside
rem      code those layers call. Nothing named "custom" would run the mass,
rem      drawing or STEP tests that consume a material.
rem
rem   2. It adds a compile-fail GROUP, and tests/compile_fail/CMakeLists.txt is
rem      shared by every group. Three milestones running, a compile-fail case has
rem      gone stale without failing to compile; the total filter is what caught
rem      each one.
rem
rem   3. Its central claims are ABSENCES -- no override map, no inherited state, no
rem      mutable path to the library. An absence is only as good as the whole
rem      build agreeing about it, and debug-shared-ext in particular has caught
rem      dll-import linkage problems in this repository before.
rem
rem Determinism here is about enumeration order and clone content rather than
rem floating point, and the two repeat stages are what answer whether Debug and
rem Release enumerate custom properties identically.
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
