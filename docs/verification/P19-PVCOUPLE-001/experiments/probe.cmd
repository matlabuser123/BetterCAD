@echo off
rem ---------------------------------------------------------------------
rem P19-CONT-001 mutation probe driver.
rem
rem THE BUILDS HAPPEN HERE, IN cmd, launched the way every successful build in
rem this session has been launched. Python is called only to edit text and to
rem print metadata; it never starts a compiler. Two earlier harnesses drove the
rem build from an MSYS2 python and both produced worthless verdicts -- the first
rem reported "rejected by the compiler" for all 28 probes INCLUDING THE NO-OP
rem CONTROL, which is the only reason it was caught.
rem
rem KILLED   = the suite failed, which is what a behavioural probe must do
rem SURVIVED = the suite passed; correct ONLY for a documented-unreachable guard
rem            and for the no-op control
rem ---------------------------------------------------------------------
setlocal enabledelayedexpansion
set "HERE=%~dp0"
rem PYTHON BY ABSOLUTE PATH, AND THE NATIVE ONE. Calling it this way adds
rem nothing to PATH, which matters: putting a python directory on PATH is how
rem MSYS2 became a find_package prefix candidate and poisoned the CMake cache
rem with Open CASCADE 7.9.3. The native interpreter is used rather than MSYS2's
rem so that nothing it reads or writes is POSIX-converted.
set "PY=C:\Users\Hasib\AppData\Local\Programs\Python\Python313\python.exe"
set "OUT=%HERE%mutations.out"
set "PATH=C:\Users\Hasib\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;C:\Windows\System32;C:\Windows"
set "CURL_CA_BUNDLE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "SSL_CERT_FILE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "BETTERCAD_BUILD_ROOT=C:/bettercad/bc-build"
set "CMAKE_BUILD_PARALLEL_LEVEL=4"
set "EXE=C:\bettercad\bc-build\debug-ext\bin\bettercad_tests.exe"
cd /d C:\Users\Hasib\Desktop\CAD

echo P19-PVCOUPLE-001 mutation probes > "%OUT%"
echo. >> "%OUT%"

echo == saving pristine copies OUTSIDE the tree ==
%PY% -I "%HERE%subst.py" save >> "%OUT%" 2>&1
echo. >> "%OUT%"

echo == baseline ==
cmake --build --preset debug-ext --target bettercad_tests > "%HERE%p-build.log" 2>&1
if errorlevel 1 goto baseline_broken
"%EXE%" "[pvc]" > "%HERE%p-run.log" 2>&1
findstr /c:"All tests passed" "%HERE%p-run.log" > nul
if errorlevel 1 goto baseline_broken
for /f "delims=" %%L in ('findstr /c:"All tests passed" "%HERE%p-run.log"') do set "BASE=%%L"
echo BASELINE !BASE! >> "%OUT%"
echo BASELINE !BASE!
echo. >> "%OUT%"

for /f "delims=" %%N in ('%PY% -I "%HERE%subst.py" count') do set "COUNT=%%N"
set /a LAST=%COUNT%-1

for /l %%I in (0,1,%LAST%) do (
    for /f "tokens=1,2,3 delims=|" %%A in ('%PY% -I "%HERE%subst.py" meta %%I') do (
        set "PID=%%A"
        set "EXPECT=%%C"
    )
    %PY% -I "%HERE%subst.py" apply %%I > "%HERE%p-apply.log" 2>&1
    if errorlevel 3 (
        set "VERDICT=ANCHOR"
    ) else (
        cmake --build --preset debug-ext --target bettercad_tests > "%HERE%p-build.log" 2>&1
        if errorlevel 1 (
            set "VERDICT=COMPILER"
        ) else (
            "%EXE%" "[pvc]" > "%HERE%p-run.log" 2>&1
            findstr /c:"All tests passed" "%HERE%p-run.log" > nul
            if errorlevel 1 ( set "VERDICT=KILLED" ) else ( set "VERDICT=SURVIVED" )
        )
    )
    %PY% -I "%HERE%subst.py" revert >> "%HERE%p-revert.log" 2>&1
    if errorlevel 4 (
        echo !PID! REVERT FAILED -- STOPPING >> "%OUT%"
        goto done
    )
    echo !EXPECT! !VERDICT! !PID! >> "%OUT%"
    echo !EXPECT! !VERDICT! !PID!
)

echo. >> "%OUT%"
echo == final state ==
cmake --build --preset debug-ext --target bettercad_tests > "%HERE%p-build.log" 2>&1
"%EXE%" "[pvc]" > "%HERE%p-run.log" 2>&1
for /f "delims=" %%L in ('findstr /c:"All tests passed" "%HERE%p-run.log"') do echo FINAL %%L >> "%OUT%"
%PY% -I "%HERE%subst.py" save >> "%OUT%" 2>&1
goto done

:baseline_broken
echo BASELINE DOES NOT BUILD OR DOES NOT PASS -- refusing to probe >> "%OUT%"
echo BASELINE DOES NOT BUILD OR DOES NOT PASS -- refusing to probe
type "%HERE%p-build.log" | findstr /c:"CMake Error" /c:"error:" >> "%OUT%"

:done
echo.
echo ---- %OUT% ----
type "%OUT%"
endlocal
