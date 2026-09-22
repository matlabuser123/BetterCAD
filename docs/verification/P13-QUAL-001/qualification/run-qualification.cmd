@echo off
rem P13-QUAL-001: the full P13 phase qualification. Three presets, each
rem configured, fully cleaned, rebuilt and tested from scratch, then the
rem whole suite repeated five times in Release and in Debug.
setlocal
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
