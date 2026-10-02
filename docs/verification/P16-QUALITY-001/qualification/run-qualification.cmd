@echo off
rem P16-QUALITY-001 qualification: the exact invocation, recorded so the run is
rem reproducible rather than described.
rem
rem qualify.cmd itself is carried UNCHANGED from P15-QUAL-001, byte for byte;
rem verify-harness.cmd beside it is its regression and is meant to be run
rem whenever it is edited, which it was not. This wrapper only supplies the
rem three variables the harness reads.
rem
rem The -ext presets build outside OneDrive (BETTERCAD_BUILD_ROOT), which the
rem repository requires: a build tree inside a synchronised folder loses files
rem to the synchroniser mid-link.
rem
rem THE REPEAT FILTER IS THE BLAST RADIUS, not the subject. P16-QUALITY-001 adds
rem a new translation unit to bettercad_meshing and changes no existing
rem behaviour, so what could be disturbed is the meshing module in full -- its
rem data model, geometry preparation, surface mesh, volume mesh, sizing and the
rem Netgen backend -- plus the three compile-failure groups that compile against
rem meshing headers, plus the architecture checks, which are what would notice a
rem containment or layering violation.
rem
rem It is expanded as !REPEAT! inside qualify.cmd, never %REPEAT%: a %VAR%
rem holding | ( or ) is substituted when the for-block is PARSED and closes the
rem block early, killing the run after every preset has already passed.
setlocal
set "BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_REPEAT=unit\.Quality|unit\.Mesh|unit\.Vol|unit\.Size|unit\.Surf|unit\.Geom|unit\.Tet|unit\.Netgen|compile_fail\.meshquality|compile_fail\.meshids|compile_fail\.volumemesh|architecture"
call "%~dp0qualify.cmd" "%~dp0."
set "RESULT=%errorlevel%"
echo qualify.cmd exit %RESULT%
endlocal & exit /b %RESULT%
