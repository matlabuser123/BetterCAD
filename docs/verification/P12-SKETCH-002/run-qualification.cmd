@echo off
rem P12-SKETCH-002: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ss]ketch|[Ss]olver|[Cc]onstraint|BSpline|Curved|[Pp]rofile|[Ee]xtru|[Rr]evol|[Ss]weep|[Ll]oft|[Pp]ropert|[Ss]tep|STEP|[Ee]xport|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
