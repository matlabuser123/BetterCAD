@echo off
rem P14-HLR-001: three-preset regression on the frozen tree. The repeat
rem selection is this milestone's subject and everything it could have
rem disturbed -- hidden-line classification and the whole view and sheet
rem model it now runs inside, section and detail geometry, the edge and
rem face queries it is validated against, persistence and its dispatch
rem sites, the assembly state occlusion is computed at, the layering
rem checker and the CLI.
setlocal
set "QUALIFY_REPEAT=[Hh]idden[Ll]ine|hlr|[Ss]ection|[Vv]iew|[Ss]heet|[Dd]rawing|[Hh]atch|[Pp]roject|[Ee]dge|[Ff]illet|[Ss]plit|[Bb]oolean|architecture|compile_fail|[Pp]ersist|[Ss]ave|[Ll]oad|[Ss]erial|cli|[Cc]omponent|[Mm]ate|[Aa]ssembl|[Dd]ocument|[Oo]bject|[Ss]olve"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
