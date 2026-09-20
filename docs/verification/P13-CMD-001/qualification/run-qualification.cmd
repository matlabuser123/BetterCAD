@echo off
rem P13-CMD-001: three-preset regression on the frozen tree. The repeat
rem selection is commands and undo/redo and everything they edit or trigger --
rem configurations and suppression, regeneration and the solve, mates and
rem components, references and resolution, parameters and expressions, the
rem object model, persistence, the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Cc]ommand|[Uu]ndo|[Rr]edo|[Hh]istory|[Rr]egen|[Dd]ependen|[Ss]tref|[Ss]table|[Rr]eference|[Rr]esolv|[Cc]onfigur|[Ss]uppress|[Mm]echanical|[Ss]olve|[Mm]ate|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Ee]xpression|[Ss]ketch|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
