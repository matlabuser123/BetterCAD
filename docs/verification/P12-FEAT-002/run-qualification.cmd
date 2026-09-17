@echo off
rem P12-FEAT-002: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Ss]plit|[Cc]ombine|BodyOps|GatherSolids|[Rr]esult|[Ee]xtru|ThroughAll|[Pp]attern|[Mm]irror|[Cc]ircular|[Ll]inear|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|body-ops|through-slab|post-row|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
