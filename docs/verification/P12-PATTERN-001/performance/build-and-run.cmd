@echo off
rem P12-PATTERN-001: builds pattern_instances_timing.cpp against the Release
rem libraries of the current tree and runs it, writing the measurement log.
rem Run after a Release build of the tree being measured.
setlocal
cd /d "%~dp0..\..\..\.."
set "DEPS=%LOCALAPPDATA%\bettercad-deps\gnu-16-mingw-amd64"
set "OUT=docs\verification\P12-PATTERN-001\performance"
set "EXE=%TEMP%\pattern_instances_timing.exe"

c++ -std=c++23 -O2 -DNDEBUG -Iinclude -Ibuild\release\generated\include ^
    "%OUT%\pattern_instances_timing.cpp" -o "%EXE%" ^
    -Lbuild\release\lib -lbettercad_features -lbettercad_sketch -lbettercad_geometry -lbettercad_core ^
    -L"%DEPS%\lib" -lTKBO -lTKBRep -lTKBool -lTKDESTEP -lTKFillet -lTKG2d -lTKG3d -lTKGeomAlgo ^
    -lTKGeomBase -lTKMath -lTKMesh -lTKOffset -lTKPrim -lTKShHealing -lTKTopAlgo -lTKXSBase -lTKernel
if errorlevel 1 (
    echo build failed
    exit /b 1
)

rem The compiler's own first line, which names the build of GCC used.
set "COMPILER="
for /f "delims=" %%V in ('c++ --version') do if not defined COMPILER set "COMPILER=%%V"

set "PATH=%DEPS%\bin;%PATH%"
set "LOG=%OUT%\pattern-instances-release.log"
echo Linear pattern instance timing, %date% %time%> "%LOG%"
echo Source: docs/verification/P12-PATTERN-001/performance/pattern_instances_timing.cpp>> "%LOG%"
echo Built with: %COMPILER%, -O2, against build/release of the qualified tree ^(OCCT 8.0.1^)>> "%LOG%"
echo CPU: AMD Ryzen 7 5800H with Radeon Graphics; single-threaded kernel calls.>> "%LOG%"
echo Each line: one full regeneration of a fresh document, wall-clock, with the result checked against the analytic volume.>> "%LOG%"
echo.>> "%LOG%"
"%EXE%" >> "%LOG%"
set "CODE=%errorlevel%"
del "%EXE%" > nul 2>&1
echo exit %CODE%
endlocal
exit /b 0
