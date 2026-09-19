@echo off
rem P13-MATE-001: three-preset regression on the frozen tree. The repeat
rem selection is the mate and assembly tests plus everything the model
rem touches -- references, datums, the object model, persistence, the
rem dependency graph, the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Mm]ate|[Oo]bjectref|[Rr]eference|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Cc]onfigur|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
