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
rem fails immediately, and its build and ctest are skipped.
rem
rem It ALSO checks the zero-match guard. The original claim here -- that ctest
rem reports a filter matching no tests as a failure (exit 8) -- is TRUE, and was
rem re-measured on ctest 4.4.2 rather than taken on trust:
rem
rem     ctest --preset debug-ext -R "nothing-matches-this" -> exit 8
rem     ctest -R "nothing-matches-this"                    -> exit 0
rem
rem It holds because the base TEST PRESET sets "noTestsAction": "error", which
rem every test preset inherits and which qualify.cmd always goes through. A
rem bare ctest by hand does not have it. That distinction is the reason
rem qualify.cmd now also counts the selected tests itself (P15-CLI-001): the
rem gate then does not depend on one field of CMakePresets.json, and the number
rem it ran is recorded rather than assumed.
rem
rem Every stage here fails, so a non-zero exit is what to expect; the exact
rem count depends on how many repeat presets are in force.
setlocal
set "HERE=%~dp0"
set "SCRATCH=%TEMP%\bettercad-harness-check"
if exist "%SCRATCH%" rmdir /s /q "%SCRATCH%"
mkdir "%SCRATCH%"

rem A preset that does not exist, AND a filter that matches nothing. The first
rem fails the configure; the second must fail the repeat stage rather than
rem sailing through it.
rem The repeat preset must be a REAL, configured one, or the guard would fire
rem because the preset could not be resolved rather than because the filter
rem matched nothing -- which would test the wrong thing.
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%c-build"
set "QUALIFY_PRESETS=no-such-preset"
set "QUALIFY_REPEAT_PRESETS=debug-ext"
set "QUALIFY_REPEAT=nothing-matches-this"
call "%HERE%qualify.cmd" "%SCRATCH%"
set "OUTCOME=%errorlevel%"

if "%OUTCOME%"=="0" (
    echo FAIL: a stage failed and qualify.cmd still exited 0.
    endlocal & exit /b 1
)
findstr /c:"matched no tests" "%SCRATCH%\qualification-times.txt" > nul
if errorlevel 1 (
    echo FAIL: a repeat filter matching no tests was not reported as a failure.
    endlocal & exit /b 1
)
echo PASS: a failed stage gave qualify.cmd exit %OUTCOME%, and a zero-match
echo       repeat filter was counted as a failed stage.
endlocal & exit /b 0
