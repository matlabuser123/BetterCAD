@echo off
rem P14-EXPORT-001: three-preset regression on the frozen tree.
rem
rem The subject is the export boundary and the three writers, so the repeat
rem selection is everything that reaches it: the scene and its validation, the
rem sheet assembly, the hidden-line classification whose decisions the styles
rem come from, sections and their hatch, dimensions (which gained a DRAWN form
rem here), annotations, GD&T, BOM and balloons, the views and sheets that place
rem them, assemblies and configurations underneath, the CLI that drives the
rem writers, persistence, the layering checker, and the reference models.
rem
rem Scene.hpp and Dimensions.hpp changed, so everything that draws is in scope
rem whether or not it exports.
setlocal
set "QUALIFY_REPEAT=[Ee]xport|[Ss]cene|pdf|svg|dxf|[Ww]rite|[Ww]riter|[Ss]heet|[Vv]iew|[Dd]rawing|[Pp]roject|[Hh]idden[Ll]ine|hlr|[Ss]ection|[Hh]atch|[Dd]imension|[Aa]nnotation|[Tt]olerance|[Bb]om|[Bb]alloon|[Cc]urve|[Aa]rc|[Cc]ircle|cli|[Cc]ommand|[Bb]atch|[Ee]dit|[Ss]elector|[Aa]rgument|[Uu]ndo|[Rr]edo|[Pp]ersist|[Ss]erial|[Ss]ave|[Ll]oad|[Ff]ile|json|[Rr]ound|[Mm]alformed|[Ll]egacy|[Ee]xample|[Rr]eference[Mm]odel|[Rr]egener|[Dd]ependen|[Gg]raph|[Ss]tale|[Rr]eference|[Rr]esolut|[Ss]tref|[Ff]ace|[Dd]atum|[Nn]ame|[Hh]ole|[Ss]ketch|[Ee]xtrude|[Pp]attern|[Mm]irror|[Aa]ssembl|[Oo]ccurrence|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Ss]olve|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Dd]ocument|[Oo]bject|[Ee]xpression|[Vv]alidat"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
