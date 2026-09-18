@echo off
rem P12-QUAL-001: the P12 PHASE qualification.
rem
rem Unlike a milestone qualification, which repeats the tests related to one
rem milestone, this repeats the WHOLE suite: "." matches every test name. A
rem phase qualification has no reason to pick a subset, and choosing one by
rem hand risks leaving out exactly the test that would have failed.
setlocal
set "QUALIFY_REPEAT=."
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal
