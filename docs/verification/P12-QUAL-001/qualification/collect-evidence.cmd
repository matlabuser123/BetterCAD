@echo off
rem P12-QUAL-001: the measured-value evidence, after qualify.cmd has run.
rem
rem Unlike a milestone collection, which takes one milestone's tests, this
rem takes the WHOLE suite under each of the three presets, so the
rem cross-preset gate compares every measured value the suite produces and
rem not a chosen subset.
setlocal
chcp 65001 > nul
set "PYTHONIOENCODING=utf-8"
cd /d "%~dp0..\..\..\.."
set "HERE=%~dp0"
set "SCRATCH=%~1"
if "%SCRATCH%"=="" set "SCRATCH=%TEMP%"

for %%P in (release debug debug-shared) do (
    build\%%P\bin\bettercad_tests.exe -s --reporter xml --rng-seed 1 > "%SCRATCH%\qual001-%%P.xml" 2>&1
    echo %%P tests exit %errorlevel%
    python "%HERE%values.py" "%SCRATCH%\qual001-%%P.xml" "%HERE%values-header.txt" > "%SCRATCH%\qual001-%%P-values.txt"
)
rem The three per-preset value files are about 5.3 MB each and are NOT
rem committed: what the cross-preset gate rests on is their comparison and
rem their hashes, which compare-values.py and reference-model-hashes.py
rem write into this directory. Re-run this script to regenerate them.
endlocal
