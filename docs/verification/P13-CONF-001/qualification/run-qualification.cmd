@echo off
rem P13-CONF-001: three-preset regression on the frozen tree. The repeat
rem selection is configurations and suppression and everything they reach --
rem the parameter configurations they extend, the solver that reads them,
rem the mates and components they suppress, references, datums, the object
rem model, persistence, the reference models and the CLI.
setlocal
set "QUALIFY_REPEAT=[Cc]onfigur|[Ss]uppress|[Mm]echanical|[Rr]evolute|[Ss]lider|[Cc]ylindrical|[Pp]lanar|[Ss]olve|[Ss]olver|[Mm]ate|[Oo]bjectref|[Rr]eference|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Cc]onstraint|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Ss]ketch|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
