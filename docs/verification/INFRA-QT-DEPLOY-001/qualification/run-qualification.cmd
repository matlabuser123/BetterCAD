@echo off
rem INFRA-QT-DEPLOY-001: three-preset qualification of BetterCAD built OUTSIDE
rem the source tree. That is the milestone, so these are the *-ext presets and
rem not the standard ones.
rem
rem P14-STREF-001 tried this first and recorded the reason it could not:
rem
rem   windeployqt resolves the Qt runtime RELATIVE TO THE EXECUTABLE it is
rem   deploying, as <exe dir>/../../<toolchain key>/bin.
rem
rem That diagnosis is WRONG, and this milestone's README shows why: windeployqt
rem resolves Qt through the dependency directory's 8.3 SHORT NAME, and the build
rem root chosen then -- %LOCALAPPDATA%\BetterCAD-build -- shared that short name
rem with %LOCALAPPDATA%\bettercad-deps and sorted before it, so the short name
rem resolved to the build root. Nothing about the build's location was ever the
rem cause; the same failure reproduces with the build tree anywhere, and no
rem failure occurs from any location once the name collision is gone.
rem
rem The repeat filter is `infra|gui|cli`: the new build-location and deployment
rem tests, the GUI target whose deployment the change rewrote, and the CLI tests
rem that REPLACE files -- the ones the synchronised build tree has been losing.
rem Moving the build tree is supposed to fix those, so they are the stage that
rem has to hold up five times over.
rem
rem BETTERCAD_BUILD_ROOT is taken from the environment when it is set, so the
rem location is the runner's choice and no absolute path is committed here. The
rem default is derived from %LOCALAPPDATA%: local, not synchronised, and its
rem name does not collide with the dependency prefix's.
setlocal enabledelayedexpansion
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=debug-ext"
set "QUALIFY_REPEAT=infra|gui|cli"
set "HERE=%~dp0"

echo build root: !BETTERCAD_BUILD_ROOT!
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
set "OUTCOME=!errorlevel!"
echo build root: !BETTERCAD_BUILD_ROOT! >> "%HERE%qualification-times.txt"
endlocal & exit /b %OUTCOME%
