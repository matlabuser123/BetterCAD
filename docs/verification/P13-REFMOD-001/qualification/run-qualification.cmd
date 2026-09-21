@echo off
rem P13-REFMOD-001: three-preset regression on the frozen tree. The repeat
rem selection is the reference models and everything a production assembly
rem workflow touches -- the assembly model, component placements and
rem transforms, mates and the solver, configurations and suppression,
rem references and their resolution, regeneration, commands and undo,
rem persistence, STEP export and read-back, the CLI, and the part reference
rem models the assembly suite is built beside.
setlocal
set "QUALIFY_REPEAT=[Rr]eference|[Aa]ssembl|[Mm]odel|[Cc]omponent|[Mm]ate|[Pp]lacement|[Tt]ransform|[Ss]olve|[Mm]echanical|[Cc]onfigur|[Ss]uppress|[Rr]egen|[Rr]esolv|[Cc]ommand|[Uu]ndo|[Rr]edo|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|[Ee]xample|[Ss]tep|[Ee]xport|[Bb]odies|[Bb]ody|[Ss]tl|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
