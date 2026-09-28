@echo off
rem P15-PERSIST-001 qualification: persisting canonical material engineering data.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has since
rem INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears, that fix
rem has regressed and that is the finding -- and this milestone writes and reads more
rem files than any before it, so that fault has more chances to show than usual.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone changes the DOCUMENT FORMAT, which every other domain
rem shares, so a narrow filter would be indefensible. Four specific reasons:
rem
rem   1. It edits DocumentJson.cpp -- the writer's dispatch, the reader's dispatch, the
rem      root key list and the root sections. Every persisted domain goes through that
rem      file: parameters, sketches, features, datums, assemblies, drawings,
rem      configurations. The assembly and drawing persistence suites are the ones that
rem      would notice a mistake there, and neither is named "material".
rem
rem   2. A golden-text test asserts the exact bytes of a document with no materials. It
rem      is the strongest backward-compatibility evidence in the repository and it lives
rem      in the io tests, not the material ones.
rem
rem   3. It removed five tests that asserted materials could NOT be saved. The whole
rem      suite is what confirms nothing else depended on that refusal.
rem
rem   4. Byte determinism is asserted within a preset. Running all three is what says
rem      Debug, Release and a shared build agree about the engineering state they load.
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
