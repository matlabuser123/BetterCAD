@echo off
rem P13-COMP-001: three-preset regression on the frozen tree. The repeat
rem selection is the assembly tests plus everything they could disturb --
rem persistence, the object model and the reference models, which are the
rem byte-identity contract this milestone must not break.
setlocal
set "QUALIFY_REPEAT=[Aa]ssembly|[Cc]omponent|[Dd]ocument|[Oo]bject|[Ii]d|[Rr]eference|[Pp]ersist|[Ff]ile|[Jj]son|[Ss]ave|[Ll]oad|cli|architecture"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
