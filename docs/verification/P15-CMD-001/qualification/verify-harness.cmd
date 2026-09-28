@echo off
rem Regression for qualify.cmd's exit code (carried from P14-DIM-001).
rem
rem The harness used to end `exit /b 0` whatever happened, so a failed stage
rem reached nobody: the qualification "passed" and the failure sat in a log.
rem This points the harness at a preset that does not exist, which fails the
rem configure stage for real rather than simulating a failure, and requires a
rem non-zero exit.
rem
rem Run this whenever qualify.cmd is edited. It takes seconds: the configure
rem fails immediately, its build and ctest are skipped, and the repeat stages
rem run against a filter that matches nothing -- which ctest also reports as a
rem failure (exit 8). That is worth knowing in itself: a mistyped repeat
rem filter cannot silently skip the determinism gate. Three failures, so
rem three is the exit code to expect.
setlocal
set "HERE=%~dp0"
set "SCRATCH=%TEMP%\bettercad-harness-check"
if exist "%SCRATCH%" rmdir /s /q "%SCRATCH%"
mkdir "%SCRATCH%"

set "QUALIFY_PRESETS=no-such-preset"
set "QUALIFY_REPEAT_PRESETS="
set "QUALIFY_REPEAT=nothing-matches-this"
call "%HERE%qualify.cmd" "%SCRATCH%"
set "OUTCOME=%errorlevel%"

if "%OUTCOME%"=="0" (
    echo FAIL: a stage failed and qualify.cmd still exited 0.
    endlocal & exit /b 1
)
echo PASS: a failed stage gave qualify.cmd exit %OUTCOME%.
endlocal & exit /b 0
