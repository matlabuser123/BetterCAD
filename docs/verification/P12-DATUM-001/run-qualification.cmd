@echo off
rem P12-DATUM-001: runs qualify.cmd with this milestone's repeat selection.
setlocal
set "QUALIFY_REPEAT=[Dd]atum|[Ss]ketch|[Mm]irror|[Cc]ircular|[Pp]attern|[Ee]xtru|[Rr]evol|[Ss]weep|[Ll]oft|Curved|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ff]ile|cli|P9"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
