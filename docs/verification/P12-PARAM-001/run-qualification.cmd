@echo off
rem P12-PARAM-001: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ee]xpression|[Pp]arameter|[Rr]egenerat|[Vv]alidat|[Dd]ependenc|[Dd]ocument|[Cc]ommand|[Dd]riven|P9|cli\."
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
