@echo off
rem P12-SWEEP-001: builds the OCCT sweep frame probe against the dependency
rem prefix and runs it, writing the measurement log. The probe uses OCCT
rem directly and is evidence only; it is not part of the build.
setlocal
cd /d "%~dp0..\..\..\.."
set "DEPS=%LOCALAPPDATA%\bettercad-deps\gnu-16-mingw-amd64"
set "OUT=docs\verification\P12-SWEEP-001\kernel-probe"
set "EXE=%TEMP%\sweep_frame_probe.exe"

c++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I"%DEPS%\include\opencascade" ^
    "%OUT%\sweep_frame_probe.cpp" -o "%EXE%" ^
    -L"%DEPS%\lib" -lTKOffset -lTKBO -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo ^
    -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
if errorlevel 1 (
    echo build failed
    exit /b 1
)

set "COMPILER="
for /f "delims=" %%V in ('c++ --version') do if not defined COMPILER set "COMPILER=%%V"

set "PATH=%DEPS%\bin;%PATH%"
set "LOG=%OUT%\sweep-frame-probe.log"
echo OCCT sweep frame probe, %date% %time%> "%LOG%"
echo Source: docs/verification/P12-SWEEP-001/kernel-probe/sweep_frame_probe.cpp>> "%LOG%"
echo Built with: %COMPILER%, -O2, against OCCT 8.0.1 in the dependency prefix.>> "%LOG%"
echo.>> "%LOG%"
"%EXE%" >> "%LOG%" 2>&1
set "CODE=%errorlevel%"
del "%EXE%" > nul 2>&1
echo exit %CODE%
endlocal
exit /b 0
