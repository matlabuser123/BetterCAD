@echo off
rem P14-DIM-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- dimensions and the references they resolve through, the
rem datum and named-face resolution they rest on, the face and unit
rem infrastructure they read, the whole drawing model they sit in,
rem persistence and its dispatch sites, configurations and parameters,
rem the assembly state a dimension of a component is measured at, the
rem layering checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Dd]imension|[Dd]atum|[Ff]ace|[Uu]nit|[Pp]arameter|[Cc]onfiguration|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Ee]dge|[Ff]illet|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve|[Ee]xpression"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
