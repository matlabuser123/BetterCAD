@echo off
rem P13-REGEN-001: three-preset regression on the frozen tree. The repeat
rem selection is regeneration and everything it now drives or is driven by --
rem the assembly solve, mates and components, configurations and suppression,
rem stable references and resolution, datums, parameters and their
rem expressions, the object model, persistence, the reference models and the
rem CLI.
setlocal
set "QUALIFY_REPEAT=[Rr]egen|[Dd]ependen|[Ss]tref|[Ss]table|[Rr]eference|[Rr]esolv|[Uu]nresolv|[Ff]ace|[Cc]onfigur|[Ss]uppress|[Mm]echanical|[Ss]olve|[Ss]olver|[Mm]ate|[Oo]bjectref|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Ss]ketch|[Ee]xpression|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
