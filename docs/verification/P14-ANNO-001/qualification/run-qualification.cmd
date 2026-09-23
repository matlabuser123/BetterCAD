@echo off
rem P14-ANNO-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- annotations and the scene they produce, the hole semantics a
rem callout reads, dimensions and the formatter they share, the reference
rem resolution they both rest on, the whole drawing model, persistence and
rem its dispatch sites, parameters and configurations, the layering checker
rem and the CLI.
setlocal
set "QUALIFY_REPEAT=[Aa]nnotation|[Ss]cene|[Hh]ole|[Dd]imension|[Dd]atum|[Ff]ace|[Uu]nit|[Pp]arameter|[Cc]onfiguration|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Ee]dge|[Ff]illet|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve|[Ee]xpression"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
