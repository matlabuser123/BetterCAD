@echo off
rem P14-VIEW-002: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- section geometry and the view and sheet model, the splitting
rem and boolean primitives a cut is composed of, persistence and its dispatch
rem sites, the assembly state a section of a component consumes, the layering
rem checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Hh]atch|[Pp]roject|[Ss]plit|[Bb]oolean|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
