@echo off
rem P14-ASM-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- assembly views and the occurrences they draw, the
rem hidden-line pipeline every view now shares with them, sections,
rem configurations and suppression, the solver that publishes the
rem transforms, the whole drawing model, persistence and its dispatch
rem sites, the layering checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Oo]cclusion|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Dd]atum|[Ss]cene|[Hh]ole|[Ff]ace|[Uu]nit|[Pp]arameter|[Ee]dge|[Ff]illet|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Dd]ocument|[Oo]bject|[Ee]xpression"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
