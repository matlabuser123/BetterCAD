@echo off
rem P13-CLI-001: three-preset regression on the frozen tree. The repeat
rem selection is the CLI and everything a headless workflow drives -- the
rem command line and its arguments, the edits, batches and scripts, the
rem assembly model, configurations and suppression, references, regeneration
rem and the solve, persistence, and the example models the process tests run.
setlocal
set "QUALIFY_REPEAT=cli|[Cc]ommand|[Bb]atch|[Ss]cript|[Aa]rgument|[Ss]elector|[Aa]ssembly|[Cc]omponent|[Mm]ate|[Pp]lacement|[Ss]olve|[Mm]echanical|[Cc]onfigur|[Ss]uppress|[Rr]egen|[Rr]eference|[Rr]esolv|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|[Ss]erial|[Ee]xample|[Uu]ndo|[Rr]edo|[Dd]ocument|[Oo]bject|[Pp]aram|[Ee]xpression|[Ss]ketch|[Dd]atum|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
