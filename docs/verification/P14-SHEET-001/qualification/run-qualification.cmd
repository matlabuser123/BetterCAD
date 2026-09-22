@echo off
rem P14-SHEET-001: three-preset regression on the frozen tree. The repeat
rem selection is the sheet model and everything this milestone could have
rem disturbed -- the layer table and its checker, the ID system, the document
rem object model, persistence and its dispatch sites, and the CLI.
setlocal
set "QUALIFY_REPEAT=[Ss]heet|[Dd]rawing|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|[Ii]d[A-Z]|cli|[Cc]omponent|[Mm]ate|[Dd]ocument|[Oo]bject"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
