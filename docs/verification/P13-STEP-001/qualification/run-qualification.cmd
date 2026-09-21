@echo off
rem P13-STEP-001: three-preset regression on the frozen tree. The repeat
rem selection is STEP export and everything an assembly export reads from --
rem the writer and the read-back path, the part export the 24 example models
rem still take, the assembly model, component transforms and placements,
rem mates and the solve, configurations and suppression, references and
rem their resolution, regeneration, persistence, and the CLI that drives the
rem whole thing headless.
setlocal
set "QUALIFY_REPEAT=[Ss]tep|[Ee]xport|[Aa]ssembl|[Cc]omponent|[Mm]ate|[Pp]lacement|[Tt]ransform|[Ss]olve|[Mm]echanical|[Cc]onfigur|[Ss]uppress|[Rr]egen|[Rr]eference|[Rr]esolv|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|[Ee]xample|[Mm]odel|[Bb]odies|[Bb]ody|[Ss]tl|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
