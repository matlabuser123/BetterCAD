@echo off
rem P13-XFORM-001: three-preset regression on the frozen tree. The repeat
rem selection is the placement and assembly tests plus everything they could
rem disturb -- persistence, the object model, parameters and configurations
rem (a placement is parameter-driven), the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Pp]lacement|[Aa]ssembly|[Cc]omponent|[Dd]ocument|[Oo]bject|[Ii]d|[Rr]eference|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Cc]onfigur|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
