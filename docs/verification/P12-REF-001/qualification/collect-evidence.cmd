@echo off
rem P12-REF-001: the measured-value evidence, after qualify.cmd has run.
setlocal
chcp 65001 > nul
set "PYTHONIOENCODING=utf-8"
cd /d "%~dp0..\..\..\.."
set "HERE=%~dp0"
set "SCRATCH=%~1"
if "%SCRATCH%"=="" set "SCRATCH=%TEMP%"

for %%P in (release debug debug-shared) do (
    build\%%P\bin\bettercad_tests.exe "[reference]" -s --reporter xml --rng-seed 1 ^
        > "%SCRATCH%\ref001-%%P.xml" 2>&1
    echo %%P tests exit %errorlevel%
    python "%HERE%values.py" "%SCRATCH%\ref001-%%P.xml" "%HERE%values-header.txt" ^
        > "%SCRATCH%\ref001-%%P-values.txt"
)
copy /y "%SCRATCH%\ref001-release-values.txt" "%HERE%reference-values-release.txt" > nul

build\release\bin\bettercad_tests.exe -s --reporter xml --rng-seed 1 > "%SCRATCH%\all-ref001-release.xml" 2>&1
echo all-release tests exit %errorlevel%
python "%HERE%values.py" "%SCRATCH%\all-ref001-release.xml" "%HERE%values-header.txt" ^
    > "%SCRATCH%\all-ref001-values.txt"
endlocal
