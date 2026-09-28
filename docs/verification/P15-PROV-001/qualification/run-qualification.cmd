@echo off
rem P15-PROV-001 qualification: provenance, completeness and consumer requirements.
rem
rem From a build tree OUTSIDE the synchronised folder, as every milestone has
rem since INFRA-QT-DEPLOY-001. If "Permission denied" or "cannot replace" appears,
rem that fix has regressed and that is the finding.
rem
rem The repeat filter is TOTAL -- `[A-Za-z]`, every test, five times over, in two
rem presets. This milestone adds no geometry and no kernel call, so a narrow filter
rem would be tempting. Four reasons not to:
rem
rem   1. It adds a FIELD to MaterialDefinition, which is compared by operator== and
rem      is what Material::contentEquals and every clone and equivalence test rest
rem      on. A field that failed to take part in equality would not fail to compile.
rem
rem   2. It changes setMaterialMechanical and setMaterialThermal -- the two setters
rem      every material milestone above P15-MECH calls -- to clear stale provenance.
rem      Nothing named "provenance" would run the mass, assignment or custom-material
rem      tests that use those setters constantly.
rem
rem   3. It adds two core sources and two headers to core/materials/, which core,
rem      features, assembly, drawing and io all compile against.
rem
rem   4. Its central claims are ORDERINGS and ABSENCES: deterministic issue order,
rem      no substitution, no fabricated default, no single completeness flag. An
rem      ordering claim is exactly what can differ between Debug and Release, and an
rem      absence is only as good as the whole build agreeing about it.
rem
rem Determinism here is about enumeration and report order rather than floating
rem point, and the two repeat stages are what answer whether Debug and Release
rem enumerate properties and issues identically.
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
