@echo off
rem P12-FEAT-001: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ee]xtru|ThroughAll|[Pp]attern|[Mm]irror|[Cc]ircular|[Ll]inear|[Ff]ace|[Rr]eference|[Ss]ketch|[Hh]ole|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|cli|through-slab|post-row|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
