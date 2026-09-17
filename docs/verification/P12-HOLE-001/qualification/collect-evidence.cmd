@echo off
rem P12-HOLE-001: the measured-value evidence, after qualify.cmd has run.
rem
rem   1. the milestone's tests in Release, with every measured value;
rem   2. the same in all three presets, for the cross-configuration check;
rem   3. every test case of the whole suite in Release, for the comparison
rem      with the previous milestone's values.
rem
rem Catch2 selector: this milestone's own cases are [standards] (the
rem standards, feature, file and CLI tests) and [hole][p12] (the three
rem geometry cases).
setlocal
chcp 65001 > nul
rem values.py prints the tests' own text, which holds characters cp1252 has not.
set "PYTHONIOENCODING=utf-8"
cd /d "%~dp0..\..\..\.."
set "HERE=%~dp0"
set "SCRATCH=%~1"
if "%SCRATCH%"=="" set "SCRATCH=%TEMP%"

for %%P in (release debug debug-shared) do (
    build\%%P\bin\bettercad_tests.exe "[standards],[hole][p12]" -s --reporter xml --rng-seed 1 ^
        > "%SCRATCH%\hole001-%%P.xml" 2>&1
    echo %%P tests exit %errorlevel%
    python "%HERE%values.py" "%SCRATCH%\hole001-%%P.xml" "%HERE%values-header.txt" ^
        > "%SCRATCH%\hole001-%%P-values.txt"
)
copy /y "%SCRATCH%\hole001-release-values.txt" "%HERE%reference-values-release.txt" > nul

build\release\bin\bettercad_tests.exe -s --reporter xml --rng-seed 1 > "%SCRATCH%\all-hole001-release.xml" 2>&1
echo all-release tests exit %errorlevel%
python "%HERE%values.py" "%SCRATCH%\all-hole001-release.xml" "%HERE%values-header.txt" ^
    > "%SCRATCH%\all-hole001-values.txt"
endlocal
