@echo off
rem P12-REF-001: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Rr]eference|[Mm]otor|[Gg]earbox|[Mm]anifold|[Dd]uct|[Ii]ndex|[Bb]racket|[Cc]onfigurat|[Pp]aram|[Ee]xpression|[Ll]oft|[Ss]weep|[Ss]wept|[Ss]patial|[Tt]wist|[Gg]uide|[Pp]attern|[Mm]irror|[Ii]nstance|[Ss]uppress|[Hh]ole|[Ss]tandard|[Tt]hread|[Vv]ariable|[Rr]ib|[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Ff]ace|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|[Ss]ketch|[Pp]rofile|tapped-plate|tapered-block|ribbed-bracket|drafted-block|shelled-block|body-ops|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
