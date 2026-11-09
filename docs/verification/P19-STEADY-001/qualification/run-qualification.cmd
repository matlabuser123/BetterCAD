rem P19-STEADY-001 -- Steady Incompressible Solver. The qualification launcher.
rem
rem WHAT THIS MILESTONE IS. The outer loop of SIMPLE, the convergence verdict,
rem the mapping of the coupling's sixteen refusals onto the result's nine failure
rem stages, and the atomic publication of a CfdResult. ADR-077.
rem
rem IT ORCHESTRATES AND IT DOES NOT COMPUTE. Ten of the nineteen checklist items
rem name work qualified under an earlier milestone, and CfdCoupling.hpp named
rem three of them as this milestone's BY NAME before any code was written here:
rem "NO LOOP. One iteration, and the criteria a loop would consult are
rem cfdCouplingVerdict. 'Convergence loop' is P19-STEADY-001's item." So the new
rem numerics are none: solveCfdSteady calls cfdMassFlux,
rem reconstructCfdPressureGradient, assembleCfdMomentum three times and
rem advanceCfdCoupling, in that order, every outer iteration.
rem
rem TWO EXISTING FILES ARE MODIFIED, AND ONE OF THEM IS A QUALIFIED TYPE.
rem include/bettercad/cfd/CfdResult.hpp and src/cfd/CfdResult.cpp gain the
rem solution's three field arrays, which ADR-061, ADR-066 Decision 1, ADR-068 and
rem CfdResult.hpp itself all deferred to "the first milestone that computes a
rem field". This is that milestone and CfdResultSlot's first production consumer
rem in the tree. CfdResult::create gains a layout and a field set, so
rem P19-DATA-001's result tests move with it, and the size mirror that stopped
rem the build until the change was stated is extended and renamed rather than
rem relaxed. ADR-077 Decision 2 has the candidates.
rem
rem THE SETTINGS ARE NOT A PARAMETER, and that is a correctness requirement.
rem canonicalText writes iteration.velocity_relaxation,
rem iteration.pressure_relaxation, convergence.momentum_residual_tolerance,
rem convergence.continuity_residual_tolerance, convergence.divergence_factor,
rem linear_solver.relative_residual_tolerance and steady.max_iterations into the
rem definition fingerprint that CfdResultSource::definition carries. An earlier
rem draft took them as arguments, which would have published provenance naming
rem one set of settings while the numbers came from another. ADR-077 Decision 7.
rem
rem A CHECK THAT COULD NOT FIRE WAS WRITTEN AND THEN REMOVED. The first draft
rem looped over the corrected fields looking for a non-finite value;
rem applyCfdCouplingCorrection already ends with
rem "if (!allFinite(out.faceFlux) || !allFinite(out.velocity) ||
rem !allFinite(out.pressure))" and refuses as ResultIsNotFinite, so the loop was
rem dead. P19-LINSOLVE-001 needed a mutation probe to find the same shape of
rem defect in two linalg kernels one milestone ago (its M4e); here the layer below
rem was read instead. ADR-077 Decision 4.
rem
rem THREE OF THE NINE FAILURE STAGES ARE UNREACHABLE FROM HERE, measured over
rem fifteen driving ports in experiments/stage-probe.out and recorded rather than
rem papered over: MassBalanceFailed is validation's (P19-VALID-001),
rem ContinuityNotClosed would need the flag ADR-075 Decision 5 deleted as
rem unfalsifiable, and every route to NonFiniteField is intercepted by a guard in
rem a lower qualified layer. ADR-077 L8.
rem
rem MEASURED AGAINST THE FROZEN TREE, so the logs can be read against a number
rem rather than for an absence of the word "fail". The figures are in FREEZE.md
rem with their derivations, counted off the frozen tree by `ctest -N` rather than
rem predicted -- P19-GEOM-001's freeze recorded getting that wrong once, and
rem P19-LINSOLVE-001's post-gate reconciliation found a cfd assertion figure four
rem short that was INTERNALLY CONSISTENT and therefore unfalsifiable from inside
rem its own document.
rem
rem     full suite, per preset      6896 = 6870 + 12 unit.CfdSteady_
rem                                             + 14 compile_fail.cfdsteady
rem     repeat set, per preset      3486 = 3474 + 12 matching the filter
rem     repeat EXECUTIONS per log  17430 = 3486 x 5
rem
rem compile_fail stays EXCLUDED from the repeat, as in every milestone: each case
rem is a real compilation and the groups take minutes serialised on the build-tree
rem lock. The new group's 14 cases are in the full run of all three presets.
rem
rem unit\.Structural and unit\.Thermal stay in the repeat filter. They are NOT
rem required here the way they were for P19-LINSOLVE-001, which modified a linalg
rem kernel both physics link: this milestone touches cfd only, and [structural]
rem 271,414 in 415 and [thermal] 595,622 in 1168 were measured IDENTICAL to their
rem baselines before the freeze. They are kept because removing a preset's
rem coverage to save time is how a regression gets missed.
rem
rem THE VERDICT IS THE HARNESS'S OWN TWO STATEMENTS, never this launcher's exit
rem code: run-qualification.out's sentence and qualification-times.txt's failed-
rem stage count. P19-MOM-001's attempt 1 was reported as "completed (exit code 0)"
rem while the harness had written "QUALIFICATION FAILED: 2 stage(s) failed".
rem
@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
set "CURL_CA_BUNDLE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "SSL_CERT_FILE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "BETTERCAD_BUILD_ROOT=C:/bettercad/bc-build"
set "CMAKE_BUILD_PARALLEL_LEVEL=4"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_MIN_FREE_GB=20"
set "QUALIFY_REPEAT=architecture|unit\.Linalg|unit\.Structural|unit\.Cfd|unit\.Fluid|unit\.Provenance|unit\.CustomMaterial|unit\.MaterialFile|unit\.MaterialCli|unit\.Thermal|unit\.P18Qualification|unit\.P17Qualification|unit\.Quality|unit\.MeshDofMap|unit\.ConstraintSet|unit\.FreeEquationMap|unit\.Tet4Element|unit\.Mesh|unit\.Volume|unit\.Tet|unit\.Siz|unit\.Id|unit\.ReferenceModel|unit\.Analytic|unit\.View|unit\.RigidTransform|unit\.Transform|unit\.Point|unit\.Direction|unit\.Frame|unit\.Unit|unit\.Dimension|unit\.MassProperties|unit\.SurfaceMesh|refmod\.|gui\.|cli\.thermal\.|cli\.structural\.|cli\.mesh\."
call "%~dp0preflight-space.cmd"
call "%~dp0qualify.cmd" "%~dp0."
set "RESULT=%errorlevel%"
echo qualify.cmd exit %RESULT%
endlocal & exit /b %RESULT%
