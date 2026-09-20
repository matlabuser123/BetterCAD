@echo off
rem P13-MATE-002: three-preset regression on the frozen tree. The repeat
rem selection is the mechanical mates and everything they are built on or
rem could disturb -- the basic mates, the solver, references, components and
rem placements, datums, the object model, persistence, the reference models
rem and the CLI.
setlocal
set "QUALIFY_REPEAT=[Mm]echanical|[Rr]evolute|[Ss]lider|[Cc]ylindrical|[Pp]lanar|[Ss]olve|[Ss]olver|[Mm]ate|[Oo]bjectref|[Rr]eference|[Aa]ssembly|[Cc]omponent|[Pp]lacement|[Cc]onstraint|[Dd]atum|[Dd]ocument|[Oo]bject|[Ii]d|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Pp]aram|[Cc]onfigur|[Ss]ketch|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
