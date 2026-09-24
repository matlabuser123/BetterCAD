@echo off
rem P14-PERSIST-001: three-preset regression on the frozen tree.
rem
rem NO PRODUCTION CODE CHANGED in this milestone: the drawing serializers were
rem built by the milestones that introduced each object kind, and this one
rem audits them system-wide. The repeat selection is therefore wide on
rem purpose -- the subject is the whole file format and everything that reads
rem or writes it: serialization and its dispatch, every drawing object kind,
rem the stable references and GD&T semantics that ride in the file, the
rem assembly and configuration state a drawing's references resolve against,
rem regeneration after load, the commands whose edits end up in the file, the
rem committed reference models that must still load, the layering checker and
rem the CLI.
setlocal
set "QUALIFY_REPEAT=[Pp]ersist|[Ss]erial|[Ss]ave|[Ll]oad|[Ff]ile|json|[Rr]ound|[Ss]chema|[Vv]ersion|[Mm]alformed|[Ll]egacy|[Ee]xample|[Rr]eference[Mm]odel|[Cc]ommand|[Uu]ndo|[Rr]edo|[Hh]istor|[Rr]egener|[Dd]ependen|[Gg]raph|[Dd]irty|[Ss]tale|[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Bb]om|[Bb]alloon|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Oo]verride|[Ss]olve|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Pp]roject|[Tt]olerance|[Dd]imension|[Aa]nnotation|[Ss]cene|[Uu]nit|[Pp]arameter|architecture|compile_fail|cli|[Dd]ocument|[Oo]bject|[Ee]xpression|[Vv]alidat|[Ee]xport"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
