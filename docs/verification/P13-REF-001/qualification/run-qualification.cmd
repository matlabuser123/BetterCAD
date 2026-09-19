@echo off
rem P13-REF-001: three-preset regression on the frozen tree. The repeat
rem selection is the reference and assembly tests plus everything a wider
rem `part` could disturb -- the object model, persistence, the dependency
rem graph, the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Oo]bjectref|[Rr]eference|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Cc]onfigur|[Uu]uid|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
