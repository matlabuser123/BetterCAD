@echo off
rem P14-BOM-001 milestone qualification: for each of the three presets,
rem configure, remove every build output (retrying while OneDrive or a
rem scanner holds a file open), rebuild with warnings as errors, and run
rem CTest only after a successful build; then repeat the WHOLE suite five
rem times in Release and in Debug, filtered to this milestone's subject and
rem everything it could have disturbed (set QUALIFY_REPEAT).
rem
rem   qualify.cmd <evidence directory>
rem
rem Runs under code page 65001 (the CLI's Unicode test needs it).
rem
rem EXIT CODE: the number of stages that failed, so a failed qualification
rem fails the command that ran it. Until P14-DIM-001 this ended `exit /b 0`
rem whatever happened, and the only place a failure showed was
rem qualification-times.txt -- a gate whose result has to be read out of a log
rem by eye is not a gate. verify-harness.cmd is the regression for that and is
rem meant to be run whenever this file is edited.
rem
rem QUALIFY_PRESETS and QUALIFY_REPEAT_PRESETS override which presets are
rem built and which are repeated, defaulting to the three production presets
rem and to release and debug. verify-harness.cmd uses them to point the
rem harness at a preset that does not exist, which fails a real stage rather
rem than simulating one.
setlocal enabledelayedexpansion
chcp 65001 > nul
set "EVIDENCE=%~1"
set "REPEAT=%QUALIFY_REPEAT%"
rem Every stage that fails adds one. The total becomes the exit code.
set /a FAILURES=0
if not defined QUALIFY_PRESETS set "QUALIFY_PRESETS=debug release debug-shared"
if not defined QUALIFY_REPEAT_PRESETS set "QUALIFY_REPEAT_PRESETS=release debug"
cd /d "%~dp0..\..\..\.."
set "TIMES=%EVIDENCE%\qualification-times.txt"

echo qualification started !date! !time! > "%TIMES%"
echo qualification candidate: >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"
call :trees "before the first build"

for %%P in (%QUALIFY_PRESETS%) do (
    echo %%P configure started !date! !time! >> "%TIMES%"
    cmake --preset %%P > "%EVIDENCE%\configure-%%P.log" 2>&1
    set "CONFIGURE=!errorlevel!"
    echo %%P configure exit !CONFIGURE! !date! !time! >> "%TIMES%"
    rem A failed configure must not fall through to clean, build and ctest
    rem and finish looking complete (P13-COMP-001 adversarial review).
    if not "!CONFIGURE!"=="0" (
        set /a FAILURES+=1
        echo %%P build skipped: the configure failed >> "%TIMES%"
        echo %%P ctest skipped: the configure failed >> "%TIMES%"
    ) else (
        call :clean %%P
        if "!CLEANED!"=="1" (
            cmake --build --preset %%P > "%EVIDENCE%\build-%%P.log" 2>&1
            set "BUILD=!errorlevel!"
        ) else (
            set "BUILD=clean failed"
            set /a FAILURES+=1
        )
        echo %%P build exit !BUILD! !date! !time! >> "%TIMES%"
        if "!BUILD!"=="0" (
            rem Prove the binaries under test are the ones just built: a
            rem second build must have nothing left to do.
            cmake --build --preset %%P > "%EVIDENCE%\rebuild-%%P.log" 2>&1
            set "REBUILD=!errorlevel!"
            if not "!REBUILD!"=="0" set /a FAILURES+=1
            echo %%P no-op rebuild exit !REBUILD! !date! !time! >> "%TIMES%"
            ctest --preset %%P -j 8 > "%EVIDENCE%\ctest-%%P.log" 2>&1
            set "CTEST=!errorlevel!"
            if not "!CTEST!"=="0" set /a FAILURES+=1
            echo %%P ctest exit !CTEST! !date! !time! >> "%TIMES%"
        ) else (
            set /a FAILURES+=1
            echo %%P ctest skipped: the build failed >> "%TIMES%"
        )
    )
)
for %%P in (%QUALIFY_REPEAT_PRESETS%) do (
    echo repeat %%P started !date! !time! >> "%TIMES%"
    ctest --preset %%P -j 8 -R "%REPEAT%" --repeat until-fail:5 > "%EVIDENCE%\ctest-repeat-%%P.log" 2>&1
    set "REPEATED=!errorlevel!"
    if not "!REPEATED!"=="0" set /a FAILURES+=1
    echo repeat %%P exit !REPEATED! !date! !time! >> "%TIMES%"
)
call :trees "after the last test run"
echo qualification finished !date! !time!, !FAILURES! stage^(s^) failed >> "%TIMES%"
if not "!FAILURES!"=="0" (
    echo QUALIFICATION FAILED: !FAILURES! stage^(s^) failed. See "%TIMES%".
) else (
    echo Qualification passed: every stage exited 0.
)
rem %FAILURES% is expanded as this line is parsed, which happens before
rem endlocal runs, so the count survives the scope it was counted in.
endlocal & exit /b %FAILURES%

rem Git tree IDs of the qualified sources, from a scratch index, so the
rem commit can be compared with what was actually built and tested.
:trees
echo qualified source trees %~1: >> "%TIMES%"
set "GIT_INDEX_FILE=%TEMP%\bettercad-qualify-index"
git read-tree HEAD
git add -A apps include src tests examples cmake CMakeLists.txt CMakePresets.json
for %%D in (apps include src tests examples cmake) do (
    for /f %%T in ('git write-tree --prefix=%%D/') do echo   %%D %%T >> "%TIMES%"
)
for /f "tokens=2" %%T in ('git ls-files -s CMakeLists.txt') do echo   CMakeLists.txt %%T >> "%TIMES%"
for /f "tokens=2" %%T in ('git ls-files -s CMakePresets.json') do echo   CMakePresets.json %%T >> "%TIMES%"
del "%GIT_INDEX_FILE%" > nul 2>&1
set "GIT_INDEX_FILE="
exit /b 0

rem Removes every build output of preset %1 (ninja -t clean), up to 5 tries
rem 15 s apart. Sets CLEANED=1 on success. Every attempt is logged.
:clean
set "CLEANED=0"
type nul > "%EVIDENCE%\clean-%~1.log"
for /l %%R in (1,1,5) do (
    if "!CLEANED!"=="0" (
        cmake --build --preset %~1 --target clean >> "%EVIDENCE%\clean-%~1.log" 2>&1
        if !errorlevel! equ 0 (
            set "CLEANED=1"
            echo %~1 clean attempt %%R exit 0 !date! !time! >> "%TIMES%"
        ) else (
            echo %~1 clean attempt %%R failed !date! !time! >> "%TIMES%"
            ping -n 16 127.0.0.1 > nul
        )
    )
)
exit /b 0
