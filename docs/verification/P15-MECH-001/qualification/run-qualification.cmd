@echo off
rem P15-MECH-001 qualification: mechanical material properties.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace"
rem appears here, that infrastructure fix has regressed and that is the finding,
rem not something to rerun past.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. The change itself is narrower than P15-MAT-001's: no ID tag, no
rem CMakeLists reshuffle, and the only pre-existing type touched is
rem MaterialDefinition, which gained a field. The filter stays total anyway,
rem because the thing a narrow filter misses is never the new code -- it is a
rem pre-existing test that pinned something the change moved. P15-UNITS-001 lost
rem a whole regression to exactly that (a unit catalog size this milestone did
rem not write), and a filter narrowed to the subject would not have run it.
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
