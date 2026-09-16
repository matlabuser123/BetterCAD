@echo off
rem P12-PARAM-001: the Debug clean in qualify.cmd could not remove one object
rem file (a file lock), so Debug is rebuilt here from a verified clean: the
rem clean is retried until it succeeds, then the build and the tests run.
setlocal enabledelayedexpansion
chcp 65001 > nul
cd /d "%~dp0..\..\.."
set "EVIDENCE=%~dp0"
set "EVIDENCE=%EVIDENCE:~0,-1%"
set "TIMES=%EVIDENCE%\rerun-debug-times.txt"
echo rerun started !date! !time! > "%TIMES%"
git rev-parse HEAD >> "%TIMES%"
cmake --preset debug > "%EVIDENCE%\rerun-configure-debug.log" 2>&1
echo configure exit !errorlevel! !date! !time! >> "%TIMES%"
set "CLEANED=0"
type nul > "%EVIDENCE%\rerun-clean-debug.log"
for /l %%R in (1,1,5) do (
    if "!CLEANED!"=="0" (
        cmake --build --preset debug --target clean >> "%EVIDENCE%\rerun-clean-debug.log" 2>&1
        if !errorlevel! equ 0 (
            set "CLEANED=1"
            echo clean attempt %%R exit 0 !date! !time! >> "%TIMES%"
        ) else (
            echo clean attempt %%R failed !date! !time! >> "%TIMES%"
            ping -n 16 127.0.0.1 > nul
        )
    )
)
if "!CLEANED!"=="1" (
    cmake --build --preset debug > "%EVIDENCE%\rerun-build-debug.log" 2>&1
    set "BUILD=!errorlevel!"
    echo build exit !BUILD! !date! !time! >> "%TIMES%"
    if "!BUILD!"=="0" (
        ctest --preset debug -j 8 > "%EVIDENCE%\rerun-ctest-debug.log" 2>&1
        echo ctest exit !errorlevel! !date! !time! >> "%TIMES%"
    )
) else (
    echo clean failed 5 times; no build >> "%TIMES%"
)
echo rerun finished !date! !time! >> "%TIMES%"
endlocal
