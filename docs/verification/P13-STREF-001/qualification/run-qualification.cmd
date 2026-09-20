@echo off
rem P13-STREF-001: three-preset regression on the frozen tree. The repeat
rem selection is stable references and everything they are resolved through
rem or against -- the face-naming they rest on, the resolver, configurations
rem and suppression, the solver, mates, components, datums, the object model,
rem persistence, the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Ss]tref|[Ss]table|[Rr]eference|[Rr]esolv|[Uu]nresolv|[Ff]ace|[Cc]onfigur|[Ss]uppress|[Mm]echanical|[Ss]olve|[Ss]olver|[Mm]ate|[Oo]bjectref|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Ss]ketch|[Rr]egener|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
