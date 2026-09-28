@echo off
rem P15-CMD-001 qualification: material commands with exact undo and redo.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears,
rem that fix has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone adds no geometry and touches no kernel call, so a narrow
rem filter would be tempting. Four reasons not to:
rem
rem   1. Two of the five commands WRAP AddObjectCommand and DeleteObjectCommand, the
rem      generic object commands every other module's create and delete are built on.
rem      Nothing named "material" would run the sketch, feature, assembly, drawing or
rem      configuration command tests that exercise the same two classes.
rem
rem   2. It adds a compile-fail GROUP, and tests/compile_fail/CMakeLists.txt is shared
rem      by every group. Four milestones running, a compile-fail case has gone stale
rem      without failing to compile; the total filter is what caught each one.
rem
rem   3. Its central claim is an ABSENCE -- no derived state in any command payload --
rem      and an absence is only as good as the whole build agreeing about it. The
rem      previous milestone's first qualification failed on debug-shared-ext alone,
rem      on exactly that kind of whole-build disagreement.
rem
rem   4. Undo/redo determinism is an ORDERING claim, and an ordering claim is what can
rem      differ between Debug and Release. The two repeat stages are what answer
rem      whether a five-command chain undoes and redoes identically in both.
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
