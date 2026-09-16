@echo off
rem P12-SKETCH-001: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ss]ketch|[Ss]olver|[Cc]onstraint|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ee]xpression|P9|profile|[Pp]rofile"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
