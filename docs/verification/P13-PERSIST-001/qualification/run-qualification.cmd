@echo off
rem P13-PERSIST-001: three-preset regression on the frozen tree. The repeat
rem selection is persistence and everything a file must carry -- components,
rem placements, mates and joints, configurations and suppression, references
rem and resolution, regeneration and the solve, commands, parameters and
rem expressions, the object model, the example models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Ss]erial|[Ss]chema|[Vv]ersion|[Ee]xample|[Cc]ommand|[Uu]ndo|[Rr]edo|[Rr]egen|[Rr]eference|[Rr]esolv|[Cc]onfigur|[Ss]uppress|[Mm]echanical|[Ss]olve|[Mm]ate|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]aram|[Ee]xpression|[Ss]ketch|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
