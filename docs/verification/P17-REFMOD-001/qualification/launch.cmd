@echo off
rem Launcher: performs the redirect itself, so no quoting crosses a process
rem boundary. A PIPE would lose the exit code; these are redirects.
cd /d "%~dp0"
call "%~dp0run-qualification.cmd" > "%~dp0run-qualification.out" 2> "%~dp0run-qualification.err"
echo launcher saw exit %errorlevel%
