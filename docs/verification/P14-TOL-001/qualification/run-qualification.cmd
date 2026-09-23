@echo off
rem P14-TOL-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- tolerances and the fit tables they read, the dimension
rem formatter they now share a precision rule with, the annotations whose
rem frame carries a tolerance, the datum letters both sides validate, the
rem scene they produce, the reference resolution they rest on, the whole
rem drawing model, persistence and its dispatch sites, the layering checker
rem and the CLI.
setlocal
set "QUALIFY_REPEAT=[Tt]olerance|[Ff]it|ISO|[Ss]tandard|[Dd]eviation|[Dd]atum|[Aa]nnotation|[Ss]cene|[Hh]ole|[Dd]imension|[Ff]ace|[Uu]nit|[Pp]arameter|[Cc]onfiguration|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Ee]dge|[Ff]illet|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve|[Ee]xpression"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
