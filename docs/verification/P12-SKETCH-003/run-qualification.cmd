@echo off
rem P12-SKETCH-003: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ff]ace|[Rr]eference|[Nn]ame|[Cc]hamfer|[Ff]illet|[Dd]atum|[Ss]ketch|[Pp]rofile|[Ee]xtru|[Bb]oolean|[Hh]ole|[Mm]irror|[Cc]ircular|[Ll]inear|[Pp]attern|[Rr]evol|[Ss]weep|[Ll]oft|Curved|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ff]ile|cli|post-row|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
