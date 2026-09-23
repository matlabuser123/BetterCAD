@echo off
rem P14-REGEN-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject -- drawing objects in the dependency
rem graph, what marks them dirty, and what a failure does -- plus everything
rem the change could have disturbed: the regenerator and its dirty set, the
rem whole drawing model (the handlers resolve through all of it), the face
rem naming and datums a dimension resolves through, assemblies,
rem configurations and suppression (a balloon's target is an occurrence in
rem force), the assembly solve, persistence, the layering checker and the CLI.
rem
rem View::dependencies() changed, so anything that reads the dependency graph
rem is in scope whether or not it draws: the graph, regeneration, validation
rem and export all do.
setlocal
set "QUALIFY_REPEAT=[Rr]egener|[Dd]ependen|[Gg]raph|[Dd]irty|[Rr]ebuild|[Ss]tale|[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Bb]om|[Bb]alloon|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Ss]cene|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Dd]ocument|[Oo]bject|[Ee]xpression|[Vv]alidat|[Ee]xport"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
