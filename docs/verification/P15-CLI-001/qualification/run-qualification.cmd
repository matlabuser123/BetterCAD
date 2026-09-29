@echo off
rem P15-CLI-001 qualification: headless engineering-data workflows.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has since
rem INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears, that fix
rem has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone edits the SHARED CLI SPINE, which every other headless
rem domain runs through, so a filter narrowed to "material" would be indefensible.
rem Four specific reasons:
rem
rem   1. EditFailure gained a field, and runEdit and BatchCommand.cpp both changed to
rem      carry it. EVERY assembly and drawing edit verb goes through those two
rem      functions. The suites that would notice a mistake there are
rem      AssemblyCliTests, DrawingCliTests and the cli.assembly.* / cli.drawing.*
rem      process tests -- not one of which is named "material".
rem
rem   2. Arguments.cpp changed: parseSiValue moved out of its anonymous namespace and
rem      parseLength and parseAngle now route through the new template. Every length
rem      and every angle on every command line in the product is parsed by that code.
rem
rem   3. EditRegistry.cpp builds the ONE verb listing that `help` prints and that
rem      `batch` looks up. A third table joined wrongly would break verbs that have
rem      nothing to do with materials, and cli.launch.help asserts the listing.
rem
rem   4. Cli.cpp's command table gained five entries, so the help text changed. That
rem      text is asserted by a process test.
rem
rem ZERO-MATCH PROTECTION. A repeat filter that matched nothing would make the
rem determinism gate pass by running no tests. It cannot: ctest reports a filter
rem matching no tests as a failure (exit 8), which qualify.cmd counts as a failed
rem stage, and verify-harness.cmd exercises exactly that path. `[A-Za-z]` matches
rem every test name in the suite, and the executed count is recorded in README.md
rem and cross-checked against the per-preset total.
setlocal enabledelayedexpansion
if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_REPEAT=[A-Za-z]"
set "HERE=%~dp0"

echo build root: !BETTERCAD_BUILD_ROOT!
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
set "OUTCOME=!errorlevel!"
echo build root: !BETTERCAD_BUILD_ROOT! >> "%HERE%qualification-times.txt"
endlocal & exit /b %OUTCOME%
