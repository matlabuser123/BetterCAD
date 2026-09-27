@echo off
rem P15-THERM-001 qualification: physical and thermal material properties.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears
rem here, that fix has regressed and that is the finding, not something to rerun
rem past.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets -- and this milestone is the reason to keep insisting on it. The change
rem added one field to MaterialProperty<Value>, which P15-MECH-001 qualified, and
rem that field gave `known()` a second overload. Four compile-fail cases in
rem tests/compile_fail/MechanicalMisuse.cpp then went stale: they still failed to
rem compile, correctly, but GCC's wording changed from "cannot convert" to "no
rem matching function", so their regexes no longer matched. Nothing about the
rem thermal subject would have run them.
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
