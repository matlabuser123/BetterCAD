@echo off
rem P14-VIEW-001: three-preset regression on the frozen tree. The repeat
rem selection is the view and sheet model and everything this milestone could
rem have disturbed -- the drawing module, the ID system, persistence and its
rem dispatch sites, the assembly state a view of a component consumes, the
rem layering checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
