@echo off
rem P14-CMD-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject -- drawing commands, the one command
rem system they join, and undo/redo -- plus everything the change could have
rem disturbed: the document's object lifecycle and ID allocation (undo restores
rem objects by ID), configurations and their overrides (a deletion clears them
rem and undo puts them back), serialization (every state comparison in the new
rem suite IS the serialization), regeneration and the dependency graph (a
rem command's edit has to propagate), the whole drawing model the commands
rem wrap, assemblies and BOM/balloon identity, the layering checker and the CLI.
rem
rem removeView() was refactored -- its precondition extracted as
rem checkRemoveView() so the command and the free function cannot drift -- so
rem every view test is in scope whether or not it deletes anything.
setlocal
set "QUALIFY_REPEAT=[Cc]ommand|[Uu]ndo|[Rr]edo|[Hh]istor|[Tt]ransaction|[Rr]egener|[Dd]ependen|[Gg]raph|[Dd]irty|[Ss]tale|[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Bb]om|[Bb]alloon|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Oo]verride|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Ss]cene|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Dd]ocument|[Oo]bject|[Ee]xpression|[Vv]alidat|[Ee]xport"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
