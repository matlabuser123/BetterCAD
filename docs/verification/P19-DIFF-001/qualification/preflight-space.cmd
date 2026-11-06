@echo off
rem ---------------------------------------------------------------------
rem P19-MESH-001 qualification preflight: the free-disk-space gate.
rem
rem This is the thin wrapper run-qualification.cmd calls. All of the measuring,
rem reporting and comparing is in preflight-space.ps1; this file only turns that
rem script's exit code into either "carry on" or "stop the run".
rem
rem WHY A WRAPPER AT ALL, AND WHY THE WORK IS NOT IN run-qualification.cmd.
rem Every line of run-qualification.cmd must begin with rem, set, call, echo,
rem setlocal, endlocal or @echo, and that is checked by a whole-word match before
rem each run -- the rule that file carries after a "remrem" line slipped past a
rem prefix match in P19-ARCH-001. A space check needs a comparison, so it cannot
rem live there: `if` is not an allowed line prefix. It also cannot go in
rem qualify.cmd, which has been byte-identical for fifty-four milestones
rem (d313a64070718c44fae290ac042fe259d1a03c8b) and is not this milestone's to
rem change. So the check is its own pair of files, reached by ONE call line.
rem
rem HOW IT STOPS THE RUN. On a refusal this uses `exit`, not `exit /b`, which
rem ends the whole cmd.exe process: run-qualification.cmd never reaches its
rem `call qualify.cmd` line, so no build, no clean and no stage begins. That is
rem deliberate -- a branch in the caller is what the line-prefix rule forbids --
rem and it is why launch.cmd's closing "launcher saw exit" echo does not appear
rem when the preflight refuses. The preflight's own report does appear, in
rem run-qualification.out, because launch.cmd redirects before calling.
rem
rem TESTING IT BY HAND. `exit` would close an interactive console, so set
rem QUALIFY_SPACE_CHECK_ONLY=1 and this reports and returns instead of stopping:
rem
rem     set QUALIFY_SPACE_CHECK_ONLY=1
rem     set QUALIFY_MIN_FREE_GB=60
rem     preflight-space.cmd
rem
rem Or call the PowerShell script directly, which never hard-stops anything:
rem
rem     powershell -NoProfile -ExecutionPolicy Bypass -File preflight-space.ps1 -WithSizes
rem
rem NO QUOTING CROSSES A PROCESS BOUNDARY HERE beyond the -File path, for the
rem reason launch.cmd records: a pipe would lose the exit code, and an embedded
rem -Command string would need quoting this file cannot guarantee.
rem ---------------------------------------------------------------------
setlocal
set "HERE=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%preflight-space.ps1"
set "SPACE=%errorlevel%"

echo preflight-space.ps1 exit %SPACE%

if "%SPACE%"=="0" (
    endlocal & exit /b 0
)

echo.
echo ===================================================================
echo PREFLIGHT REFUSED THE QUALIFICATION. Nothing was built and no stage
echo ran. Exit 1 means a volume is short; exit 9 means QUALIFY_MIN_FREE_GB
echo is not set. Fix what the report above names, then launch again.
echo ===================================================================

if defined QUALIFY_SPACE_CHECK_ONLY (
    echo QUALIFY_SPACE_CHECK_ONLY is set, so the console is left alive.
    endlocal & exit /b %SPACE%
)

endlocal & exit %SPACE%
