@echo off
rem P15-UNITS-001: three-preset regression of the engineering quantity contracts.
rem
rem THE BLAST RADIUS IS THE WHOLE TREE, which is why the repeat filter is total.
rem This milestone changes core/units -- Dimension.hpp, Units.hpp, Literals.hpp,
rem Format.hpp and UnitCatalog.cpp -- and every module above core uses
rem quantities. Format.hpp in particular changed BEHAVIOUR: toString() no longer
rem prints a negative zero, and toString() appears in diagnostics throughout the
rem system, so anything asserting a formatted message is in scope.
rem
rem P14's discipline is kept rather than relaxed for a smaller milestone: three
rem clean presets, a no-op rebuild proving fresh binaries, and the whole suite
rem five times over in two presets.
setlocal
set "QUALIFY_PRESETS=debug release debug-shared"
set "QUALIFY_REPEAT_PRESETS=release debug"
set "QUALIFY_REPEAT=[A-Za-z]"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
