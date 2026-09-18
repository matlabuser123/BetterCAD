@echo off
rem P12-PARAM-002: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Cc]onfigurat|[Pp]aram|[Ee]xpression|[Ee]quation|[Ll]oft|[Ss]hape|[Ss]mooth|[Cc]hain|[Ss]weep|[Ss]wept|[Ss]patial|[Tt]wist|[Gg]uide|[Pp]attern|[Mm]irror|[Ii]nstance|[Ss]uppress|[Ss]ymmetric|[Nn]est|[Hh]ole|[Ss]tandard|[Tt]hread|[Cc]learance|[Tt]oleranc|[Vv]ariable|[Rr]ib|[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|[Ss]ketch|[Pp]rofile|[Rr]igidTransform|tapped-plate|tapered-block|ribbed-bracket|drafted-block|shelled-block|body-ops|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
