@echo off
rem P14-STREF-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject -- every drawing reference and
rem the resolution of each -- plus everything those references reach: the
rem face-naming system, datums, features that generate named faces,
rem assemblies, configurations and suppression, the whole drawing model,
rem persistence and its dispatch sites, the layering checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Cc]hamfer|[Ff]illet|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Bb]om|[Bb]alloon|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Ss]cene|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Dd]ocument|[Oo]bject|[Ee]xpression"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
