@echo off
rem P13-SOLVE-001: three-preset regression on the frozen tree. The repeat
rem selection is the assembly solver and everything it reads or could
rem disturb -- mates, references, components and placements, datums, the
rem object model, persistence, the sketch solver (whose loop this one
rem mirrors), the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Ss]olve|[Ss]olver|[Mm]ate|[Oo]bjectref|[Rr]eference|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Cc]onstraint|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Cc]onfigur|[Ss]ketch|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
