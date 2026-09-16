@echo off
rem P12 milestone qualification: for each preset, configure, rebuild from
rem clean (warnings are errors), and run CTest only after a successful build;
rem then repeat the milestone's related tests 5 times in Release and Debug.
rem
rem   set "QUALIFY_REPEAT=<ctest -R regex for the repeats>"
rem   qualify.cmd <evidence directory>
rem
rem The regex comes from the environment because cmd would read its '|' as
rem pipes on a command line (see run-qualification.cmd).
rem Runs under code page 65001 (the CLI's Unicode test needs it).
setlocal enabledelayedexpansion
chcp 65001 > nul
set "EVIDENCE=%~1"
set "REPEAT=%QUALIFY_REPEAT%"
cd /d "%~dp0..\..\.."
set "TIMES=%EVIDENCE%\qualification-times.txt"

echo qualification started !date! !time! > "%TIMES%"
echo HEAD before the milestone commit: >> "%TIMES%"
git rev-parse HEAD >> "%TIMES%"
rem Git tree IDs of the qualified sources, from a scratch index, so the commit
rem can be compared with what was built.
set "GIT_INDEX_FILE=%TEMP%\bettercad-qualify-index"
git read-tree HEAD
git add -A apps include src tests examples cmake CMakeLists.txt CMakePresets.json
echo qualified source trees: >> "%TIMES%"
for %%D in (apps include src tests examples cmake) do (
    for /f %%T in ('git write-tree --prefix=%%D/') do echo   %%D %%T >> "%TIMES%"
)
for /f "tokens=2" %%T in ('git ls-files -s CMakeLists.txt') do echo   CMakeLists.txt %%T >> "%TIMES%"
for /f "tokens=2" %%T in ('git ls-files -s CMakePresets.json') do echo   CMakePresets.json %%T >> "%TIMES%"
del "%GIT_INDEX_FILE%" > nul 2>&1
set "GIT_INDEX_FILE="
for %%P in (debug release debug-shared) do (
    echo %%P configure started !date! !time! >> "%TIMES%"
    cmake --preset %%P > "%EVIDENCE%\configure-%%P.log" 2>&1
    echo %%P configure exit !errorlevel! !date! !time! >> "%TIMES%"
    cmake --build --preset %%P --clean-first > "%EVIDENCE%\build-%%P.log" 2>&1
    set "BUILD=!errorlevel!"
    echo %%P build exit !BUILD! !date! !time! >> "%TIMES%"
    if "!BUILD!"=="0" (
        ctest --preset %%P -j 8 > "%EVIDENCE%\ctest-%%P.log" 2>&1
        echo %%P ctest exit !errorlevel! !date! !time! >> "%TIMES%"
    ) else (
        echo %%P ctest skipped: the build failed >> "%TIMES%"
    )
)
for %%P in (release debug) do (
    echo repeat %%P started !date! !time! >> "%TIMES%"
    ctest --preset %%P -j 8 -R "%REPEAT%" --repeat until-fail:5 > "%EVIDENCE%\ctest-repeat-%%P.log" 2>&1
    echo repeat %%P exit !errorlevel! !date! !time! >> "%TIMES%"
)
echo qualification finished !date! !time! >> "%TIMES%"
endlocal
