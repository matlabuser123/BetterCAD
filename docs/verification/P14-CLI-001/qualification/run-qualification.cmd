@echo off
rem P14-CLI-001: three-preset regression on the frozen tree.
rem
rem The subject is the command line over the drawing model, so the repeat
rem selection is everything the CLI reaches through: the edit spine and its
rem batch driver, the selector grammar the drawing targets share with mates,
rem every drawing object kind and the commands that make them, regeneration
rem (the CLI now registers the drawing handlers, which nothing in production
rem did before), persistence (every edit is load-apply-save), assemblies,
rem configurations and suppression, the BOM and balloon identity the report
rem prints, the layering checker, and every other CLI test.
rem
rem Both halves of the new suite are in it: the in-process cases that drive
rem cli::run, and the process tests that drive the built executable, which is
rem what proves the workflow survives leaving a process.
setlocal
set "QUALIFY_REPEAT=cli|[Cc]ommand|[Bb]atch|[Ee]dit|[Ss]elector|[Aa]rgument|[Uu]ndo|[Rr]edo|[Hh]istor|[Pp]ersist|[Ss]erial|[Ss]ave|[Ll]oad|[Ff]ile|json|[Rr]ound|[Ss]chema|[Mm]alformed|[Ll]egacy|[Ee]xample|[Rr]eference[Mm]odel|[Rr]egener|[Dd]ependen|[Gg]raph|[Dd]irty|[Ss]tale|[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Bb]om|[Bb]alloon|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Oo]verride|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Ss]cene|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Dd]ocument|[Oo]bject|[Ee]xpression|[Vv]alidat|[Ee]xport"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
